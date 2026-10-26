'use strict';

const path = require('node:path');
const fs = require('node:fs');
const { spawn, execFileSync } = require('node:child_process');
const express = require('express');

const store = require('./store');
const library = require('./library');
const vlc = require('./vlc');

const app = express();
const PORT = Number(process.env.PORT) || 4173;
// The dev server wants 0.0.0.0 so the sandbox preview proxy can reach it. The
// packaged desktop app sets HOST=127.0.0.1: a single-user desktop app has no
// business exposing an unauthenticated API to the LAN.
const HOST = process.env.HOST || '0.0.0.0';
// Derived from the store so VLCFLIX_DATA_DIR relocates artwork too: an
// installed build must not write into Program Files.
const MEDIA_DIR = path.join(store.DATA_DIR, 'media');
const SAMPLES_DIR = path.join(__dirname, '..', 'samples');

app.use(express.json());
app.use(express.static(path.join(__dirname, '..', 'public')));
app.use('/media', express.static(MEDIA_DIR, { maxAge: '1h', immutable: true }));

/* ------------------------------------------------------------------ */
/* Scan state (in-memory; a scan does not survive a restart)            */
/* ------------------------------------------------------------------ */

const scanState = {
  running: false,
  phase: null, // 'read' (fast metadata) then 'art' (thumbnails, in the background)
  done: 0,
  total: 0,
  current: '',
  startedAt: null,
  finishedAt: null,
  error: null,
};

async function runScan() {
  if (scanState.running) return;
  const settings = store.getSettings();
  Object.assign(scanState, {
    running: true, phase: null, done: 0, total: 0, current: '',
    startedAt: Date.now(), finishedAt: null, error: null,
  });
  try {
    const catalog = await library.scan(settings.libraryFolders, {
      previous: store.getCatalog(),
      mediaDir: MEDIA_DIR,
      onProgress: (p) => Object.assign(scanState, p),
      // Persist whenever the catalog improves, so a reload mid-scan shows
      // everything that has been found so far instead of nothing.
      onCatalog: (next) => store.saveCatalog(next),
    });
    store.saveCatalog(catalog);
  } catch (err) {
    scanState.error = err.message;
    console.error('[scan] failed:', err);
  } finally {
    scanState.running = false;
    scanState.finishedAt = Date.now();
  }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

const itemUrl = (item) => ({
  posterUrl: item.poster ? `/media/${item.poster}` : null,
  backdropUrl: item.backdrop ? `/media/${item.backdrop}` : null,
  previewUrl: item.preview ? `/media/${item.preview}` : null,
  subtitles: (item.subtitles || []).map((track, index) => ({
    url: `/api/subtitles/${item.id}/${index}`,
    label: track.label,
    lang: track.lang || '',
    default: index === 0,
  })),
});

function publicSettings(settings) {
  const { vlcPassword, ...safe } = settings;
  return { ...safe, vlcPasswordSet: Boolean(vlcPassword) };
}

function decorate(items, progress, myList) {
  return items.map((item) => {
    const p = progress[item.id];
    const percent = p ? Math.min(100, Math.round((p.position / Math.max(p.duration, 1)) * 100)) : 0;
    return {
      id: item.id,
      title: item.title,
      year: item.year,
      rating: item.rating,
      genres: item.genres,
      synopsis: item.synopsis,
      duration: item.duration,
      width: item.width,
      height: item.height,
      accent: item.accent,
      filename: item.filename,
      size: item.size,
      addedAt: item.addedAt,
      ...itemUrl(item),
      hasArtwork: Boolean(item.poster),
      inMyList: myList.includes(item.id),
      progress: p ? { position: p.position, duration: p.duration, percent } : null,
    };
  });
}

function buildRails(items, progress, myList) {
  const rails = [];
  const continueShelf = items
    .filter((i) => progress[i.id])
    .sort((a, b) => progress[b.id].updatedAt - progress[a.id].updatedAt);
  if (continueShelf.length) rails.push({ id: 'continue', title: 'Continue Watching', items: continueShelf });

  const newest = [...items].sort((a, b) => b.addedAt - a.addedAt).slice(0, 12);
  if (newest.length) rails.push({ id: 'new', title: 'Recently Added', items: newest });

  if (myList.length) {
    const mine = myList.map((id) => items.find((i) => i.id === id)).filter(Boolean);
    if (mine.length) rails.push({ id: 'mylist', title: 'My List', items: mine });
  }

  const genres = new Map();
  for (const item of items) {
    for (const genre of item.genres || []) {
      if (!genres.has(genre)) genres.set(genre, []);
      genres.get(genre).push(item);
    }
  }
  const ranked = [...genres.entries()]
    .filter(([, list]) => list.length >= 2)
    .sort((a, b) => b[1].length - a[1].length);
  for (const [genre, list] of ranked) {
    rails.push({ id: `genre-${genre.toLowerCase().replace(/\s+/g, '-')}`, title: genre, items: list.slice(0, 12) });
  }

  if (items.length) rails.push({ id: 'all', title: 'Everything in Your Library', items });
  return rails;
}

async function vlcStatus(settings) {
  const detected = vlc.detect(settings.vlcPath);
  const client = new vlc.VlcClient({
    port: settings.vlcHttpPort,
    password: settings.vlcPassword,
  });
  const running = detected.path ? await client.ping() : false;
  let playback = null;
  if (running) {
    try {
      playback = await client.status();
    } catch {
      /* transient */
    }
  }
  return {
    binary: detected.path,
    source: detected.source,
    platform: process.platform,
    httpPort: settings.vlcHttpPort,
    running,
    playback,
  };
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

app.get('/api/bootstrap', async (req, res) => {
  const settings = store.getSettings();
  const catalog = store.getCatalog();
  const progress = store.getProgress();
  const myList = store.getMyList();
  const items = decorate(catalog.items, progress, myList);
  const tools = catalog.tools || { ffmpeg: false, ffprobe: false, canRender: false };

  res.json({
    settings: publicSettings(settings),
    library: {
      items,
      rails: buildRails(items, progress, myList),
      scannedAt: catalog.scannedAt,
      folders: catalog.folders || [],
      errors: catalog.errors || [],
      tools,
      isDemo: (catalog.folders || []).some((f) => path.resolve(f) === path.resolve(SAMPLES_DIR)),
    },
    scan: scanState,
    vlc: await vlcStatus(settings),
    nowPlaying: nowPlaying.state,
  });
});

app.get('/api/catalog', (req, res) => {
  const catalog = store.getCatalog();
  const progress = store.getProgress();
  const myList = store.getMyList();
  const items = decorate(catalog.items, progress, myList);
  res.json({ items, scannedAt: catalog.scannedAt, tools: catalog.tools });
});

app.post('/api/library/scan', (req, res) => {
  if (scanState.running) {
    return res.status(409).json({ error: 'A scan is already running', scan: scanState });
  }
  runScan(); // deliberately not awaited: the client polls /api/scan/status
  res.status(202).json({ started: true, scan: scanState });
});

app.get('/api/scan/status', (req, res) => res.json(scanState));

app.post('/api/settings', async (req, res) => {
  const allowed = ['libraryFolders', 'vlcPath', 'vlcHttpPort', 'vlcPassword', 'playMode', 'vlcFullscreen'];
  const patch = {};
  for (const key of allowed) {
    if (Object.prototype.hasOwnProperty.call(req.body, key)) patch[key] = req.body[key];
  }
  if ('libraryFolders' in patch) {
    if (!Array.isArray(patch.libraryFolders)) {
      return res.status(400).json({ error: 'libraryFolders must be an array of paths' });
    }
    patch.libraryFolders = patch.libraryFolders
      .map((f) => String(f).trim())
      .filter(Boolean)
      .map((f) => path.resolve(f.replace(/^~(?=$|\/|\\)/, require('node:os').homedir())));
  }
  if ('vlcHttpPort' in patch) {
    const port = Number(patch.vlcHttpPort);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      return res.status(400).json({ error: 'vlcHttpPort must be 1-65535' });
    }
    patch.vlcHttpPort = port;
  }
  if ('playMode' in patch && !['vlc', 'browser'].includes(patch.playMode)) {
    return res.status(400).json({ error: "playMode must be 'vlc' or 'browser'" });
  }
  store.saveSettings(patch);
  res.json({ settings: publicSettings(store.getSettings()) });
});

app.get('/api/system', async (req, res) => {
  const settings = store.getSettings();
  res.json({
    platform: process.platform,
    node: process.version,
    tools: await library.probeTools(),
    vlc: await vlcStatus(settings),
    samplesDir: SAMPLES_DIR,
  });
});

/* ------------------------------- player ---------------------------- */

/** The in-app (browser) player's state, so a reload can restore it. */
const nowPlaying = {
  state: null, // { id, mode, startedAt }
};

function findItem(id) {
  return store.getCatalog().items.find((i) => i.id === id) || null;
}

app.post('/api/player/play', async (req, res) => {
  const { id, mode, startAt } = req.body || {};
  const item = findItem(id);
  if (!item) return res.status(404).json({ error: 'Unknown item' });

  const settings = store.getSettings();
  const chosen = mode || settings.playMode;

  if (chosen === 'ffmpeg') {
    nowPlaying.state = { id, mode: 'browser', startedAt: Date.now() };
    // seekTo stays 0: ffmpeg is handed the start offset, and a seek restarts the
    // stream at a new offset rather than scrubbing an unseekable pipe.
    return res.json({
      mode: 'browser',
      engine: 'ffmpeg',
      streamUrl: `/api/stream/${id}?engine=ffmpeg${startAt ? `&t=${Math.round(startAt)}` : ''}`,
      title: item.title,
      seekTo: 0,
      accelerated: detectHwAccel() || 'software',
    });
  }

  if (chosen !== 'vlc') {
    nowPlaying.state = { id, mode: 'browser', startedAt: Date.now() };
    return res.json({
      mode: 'browser',
      streamUrl: `/api/stream/${id}${startAt ? `?t=${Math.round(startAt)}` : ''}`,
      title: item.title,
      seekTo: Number(startAt) || 0,
    });
  }

  const detected = vlc.detect(settings.vlcPath);
  if (!detected.path) {
    return res.status(409).json({
      error: 'VLC was not found on this machine',
      hint: 'Install VLC, or set its path in Settings, or switch to the built-in player.',
      code: 'VLC_NOT_FOUND',
    });
  }

  const client = new vlc.VlcClient({ port: settings.vlcHttpPort, password: settings.vlcPassword });

  try {
    if (await client.ping()) {
      // An instance is already listening: hand it the file rather than
      // spawning a second player window.
      await client.playFile(item.path);
    } else {
      const started = await vlc.launch({
        binary: detected.path,
        port: settings.vlcHttpPort,
        password: settings.vlcPassword,
        file: item.path,
        fullscreen: settings.vlcFullscreen,
      });
      if (!started.ok) {
        return res.status(504).json({ error: started.error, code: 'VLC_UNREACHABLE' });
      }
    }

    if (startAt > 5) {
      setTimeout(() => client.seek(startAt).catch(() => {}), 2500);
    }
    nowPlaying.state = { id, mode: 'vlc', startedAt: Date.now() };
    res.json({ mode: 'vlc', title: item.title, binary: detected.path });
  } catch (err) {
    res.status(502).json({
      error: `VLC refused the request: ${err.message}`,
      code: err.code || 'VLC_ERROR',
    });
  }
});

app.get('/api/player/status', async (req, res) => {
  const settings = store.getSettings();
  const client = new vlc.VlcClient({ port: settings.vlcHttpPort, password: settings.vlcPassword });
  try {
    const status = await client.status({ timeout: 2500 });
    // VLC reports state "stopped" with length 0 once the file ends or the user
    // closes the item. The memoized "what did we send it" record must not
    // outlive that, or the dock keeps showing the last title at 0:00.
    const active = status.state !== 'stopped' && status.length > 0;
    if (!active) nowPlaying.state = null;
    const item = active && nowPlaying.state?.mode === 'vlc' ? findItem(nowPlaying.state.id) : null;
    return res.json({
      mode: 'vlc',
      running: true,
      active,
      status,
      itemId: item?.id || null,
      title: item?.title || (active ? status.title : ''),
      posterUrl: item?.poster ? `/media/${item.poster}` : null,
    });
  } catch {
    return res.json({ mode: 'vlc', running: false, status: null });
  }
});

app.post('/api/player/command', async (req, res) => {
  const { action, value } = req.body || {};
  const settings = store.getSettings();
  const client = new vlc.VlcClient({ port: settings.vlcHttpPort, password: settings.vlcPassword });
  try {
    let status;
    switch (action) {
      case 'play':
      case 'resume':
        status = await client.resume();
        break;
      case 'pause':
        status = await client.pause();
        break;
      case 'toggle':
        status = await client.togglePlay();
        break;
      case 'stop':
        status = await client.stop();
        break;
      case 'seek':
        status = await client.seek(Number(value) || 0);
        break;
      case 'seekBy': {
        const current = await client.status();
        status = await client.seek(current.time + (Number(value) || 0));
        break;
      }
      case 'volume':
        status = await client.setVolume(Number(value) || 0);
        break;
      default:
        return res.status(400).json({ error: `Unknown action '${action}'` });
    }
    res.json({ ok: true, status });
  } catch (err) {
    res.status(502).json({ ok: false, error: err.message, code: err.code });
  }
});

app.post('/api/progress', (req, res) => {
  const { id, position, duration } = req.body || {};
  if (!id || !findItem(id)) return res.status(404).json({ error: 'Unknown item' });
  const saved = store.setProgress(id, Number(position) || 0, Number(duration) || 0);
  res.json({ ok: true, progress: saved[id] || null });
});

app.post('/api/mylist', (req, res) => {
  const { id } = req.body || {};
  if (!id || !findItem(id)) return res.status(404).json({ error: 'Unknown item' });
  res.json({ myList: store.toggleMyList(id) });
});

/* ------------------------------- streaming ------------------------- */

/* --------------------------- ffmpeg engine ------------------------- */

/**
 * Which hardware decoder ffmpeg can use on this machine, resolved once.
 *
 * Only accelerators that work by simply passing `-hwaccel <name>` are listed
 * (QSV / D3D11VA / DXVA2 on Windows, VideoToolbox on macOS). VAAPI on Linux needs
 * an explicit device and format negotiation, so it is deliberately not guessed.
 * An empty string means software, and every failure falls back to it.
 */
let hwAccelCache;
function detectHwAccel() {
  if (hwAccelCache !== undefined) return hwAccelCache;
  hwAccelCache = '';
  try {
    const out = execFileSync('ffmpeg', ['-hide_banner', '-hwaccels'], {
      encoding: 'utf8', timeout: 8000, stdio: ['ignore', 'pipe', 'ignore'],
    });
    const available = out.split(/\r?\n/).slice(1).map((line) => line.trim().toLowerCase()).filter(Boolean);
    const wanted = process.platform === 'win32'
      ? ['qsv', 'd3d11va', 'dxva2']
      : process.platform === 'darwin' ? ['videotoolbox'] : [];
    hwAccelCache = wanted.find((name) => available.includes(name)) || '';
  } catch {
    hwAccelCache = '';
  }
  return hwAccelCache;
}

/**
 * Transcode/remux to fragmented MP4 so the built-in player can show containers it
 * cannot read natively (MKV, AVI, HEVC, AC3 ...).
 *
 * H.264 is copied rather than re-encoded: decoding and re-encoding a file the
 * browser can already decode is pure waste, and hardware acceleration is only
 * relevant when pixels actually have to be decoded. Seeking restarts the process
 * at the requested offset, which is why `t` exists.
 */
function streamViaFfmpeg(req, res, item) {
  const start = Math.max(0, Math.floor(Number(req.query.t) || 0));
  const accel = detectHwAccel();
  const copyVideo = item.vcodec === 'h264';
  const copyAudio = copyVideo && item.acodec === 'aac';

  const args = ['-hide_banner', '-loglevel', 'error', '-nostdin'];
  if (start > 0) args.push('-ss', String(start));
  if (!copyVideo && accel) args.push('-hwaccel', accel);
  args.push('-i', item.path, '-sn', '-dn', '-map', '0:v:0', '-map', '0:a:0?');
  if (copyVideo) {
    args.push('-c:v', 'copy');
  } else {
    args.push('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '22',
              '-pix_fmt', 'yuv420p', '-g', '48');
  }
  args.push('-c:a', copyAudio ? 'copy' : 'aac');
  if (!copyAudio) args.push('-b:a', '160k', '-ac', '2');
  args.push('-movflags', 'frag_keyframe+empty_moov+default_base_moof', '-f', 'mp4', 'pipe:1');

  console.log(`[ffmpeg] ${item.filename}: ${copyVideo ? 'copy video' : `transcode (${accel || 'software'})`} at ${start}s`);

  const child = spawn('ffmpeg', args, { stdio: ['ignore', 'pipe', 'pipe'] });
  const stop = () => { try { child.kill('SIGKILL'); } catch { /* already gone */ } };
  req.on('close', stop);
  res.on('close', stop);

  let started = false;
  child.stdout.on('data', (chunk) => {
    if (!started) {
      started = true;
      res.writeHead(200, { 'Content-Type': 'video/mp4', 'Cache-Control': 'no-store' });
    }
    res.write(chunk);
  });
  child.stdout.on('end', () => {
    if (started) res.end();
    else if (!res.headersSent) res.status(502).json({ error: 'ffmpeg produced no output', code: 'FFMPEG_EMPTY' });
  });
  child.stderr.on('data', (buf) => {
    const line = buf.toString().trim();
    if (line) console.warn('[ffmpeg]', line.split('\n')[0]);
  });
  child.on('error', (err) => {
    if (!res.headersSent) {
      res.status(500).json({ error: `Could not run ffmpeg: ${err.message}`, code: 'FFMPEG_MISSING' });
    } else {
      res.end();
    }
  });
}

/** Range-aware local video streaming for the built-in player. */
app.get('/api/stream/:id', (req, res) => {
  const item = findItem(req.params.id);
  if (!item) return res.status(404).end();
  if (req.query.engine === 'ffmpeg') return streamViaFfmpeg(req, res, item);

  let stat;
  try {
    stat = fs.statSync(item.path);
  } catch {
    return res.status(410).json({ error: 'File is no longer on disk' });
  }

  const headers = {
    'Content-Type': library.mimeFor(item.path),
    'Accept-Ranges': 'bytes',
    'Cache-Control': 'no-store',
  };
  const range = req.headers.range;
  if (!range) {
    res.writeHead(200, { ...headers, 'Content-Length': stat.size });
    return fs.createReadStream(item.path).pipe(res);
  }

  const match = /bytes=(\d*)-(\d*)/.exec(range);
  if (!match) return res.status(416).set('Content-Range', `bytes */${stat.size}`).end();

  let start = match[1] ? Number(match[1]) : 0;
  let end = match[2] ? Number(match[2]) : stat.size - 1;
  if (Number.isNaN(start) || Number.isNaN(end) || start > end || start >= stat.size) {
    return res.status(416).set('Content-Range', `bytes */${stat.size}`).end();
  }
  end = Math.min(end, stat.size - 1);

  res.writeHead(206, {
    ...headers,
    'Content-Range': `bytes ${start}-${end}/${stat.size}`,
    'Content-Length': end - start + 1,
  });
  fs.createReadStream(item.path, { start, end }).pipe(res);
});

/* ------------------------------- misc ------------------------------ */

/** Sidecar subtitles, converted to WebVTT because <track> cannot read SRT. */
app.get('/api/subtitles/:id/:index', (req, res) => {
  const item = findItem(req.params.id);
  if (!item) return res.status(404).json({ error: 'Unknown item' });
  const track = (item.subtitles || [])[Number(req.params.index)];
  if (!track) return res.status(404).json({ error: 'Unknown subtitle track' });

  const file = path.join(path.dirname(item.path), track.name);
  let raw;
  try {
    raw = fs.readFileSync(file);
  } catch {
    return res.status(410).json({ error: 'Subtitle file is no longer on disk' });
  }
  res.set('Content-Type', 'text/vtt; charset=utf-8');
  res.set('Cache-Control', 'no-store');
  res.send(library.toVtt(raw));
});

app.get('/api/health', (req, res) => res.json({ ok: true, uptime: process.uptime() }));

app.use((req, res) => res.status(404).json({ error: 'Not found' }));

if (require.main === module) {
  app.listen(PORT, HOST, () => {
    console.log(`VLCFlix on http://${HOST}:${PORT}`);
    const settings = store.getSettings();
    if (!settings.libraryFolders.length) {
      console.log('No library folders configured yet — point VLCFlix at your videos from Settings.');
    }
  });
}

module.exports = { app, runScan, scanState };
