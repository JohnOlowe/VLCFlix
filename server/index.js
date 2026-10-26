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

/* Artwork resume (in-memory). A full scan is not needed to finish artwork: the
   catalogue already knows every file, its duration and what has been drawn. */
const artState = {
  running: false,
  done: 0,
  total: 0,
  current: '',
  startedAt: null,
  finishedAt: null,
  cancelled: false,
};

/** What the header chip should show: a scan if one is running, else artwork. */
function liveStatus() {
  if (scanState.running) return scanState;
  // The pass that just ended reported itself, so the window can say "artwork
  // finished" instead of pretending a library scan ran.
  if (!artState.running) return artState.finishedAt ? { ...scanState, running: false, kind: 'art', phase: 'art', done: artState.done, total: artState.total, finishedAt: artState.finishedAt } : scanState;
  return {
    running: true,
    kind: 'art', // resumes what is missing; not a library scan, so say so
    phase: 'art',
    done: artState.done,
    total: artState.total,
    current: artState.current,
    startedAt: artState.startedAt,
    finishedAt: null,
    error: null,
  };
}

/**
 * Finish the artwork that is missing, without walking the library again. This is
 * what makes reopening the app continue where it stopped instead of starting
 * over: only items with no usable file on disk are drawn, and each one is
 * recorded as soon as it lands.
 */
async function resumeArtwork(pending) {
  if (artState.running || scanState.running) return { total: 0, drawn: 0 };
  if (!pending || !pending.length) return { total: 0, drawn: 0 };
  const catalog = store.getCatalog();

  Object.assign(artState, { running: true, done: 0, total: pending.length, current: '', startedAt: Date.now(), cancelled: false });
  try {
    const result = await library.renderMissingArtwork(pending, {
      mediaDir: MEDIA_DIR,
      retryFailed: true, // the list has already been filtered by shouldRetryArt
      cancel: () => artState.cancelled || scanState.running,
      onProgress: (p) => Object.assign(artState, p),
      onCatalog: () => store.saveCatalog(catalog),
    });
    console.log(`[artwork] pass finished: ${result.drawn} drawn, ${result.remaining} still missing`);
    return result;
  } catch (err) {
    console.error('[artwork] pass failed:', err);
    return { total: pending.length, drawn: artState.done, error: err.message };
  } finally {
    artState.running = false;
    artState.finishedAt = Date.now();
  }
}

/** The startup pass. Kept separate so the wait loop stays readable. */
function renderPending(pending) {
  if (!pending || !pending.length) return;
  resumeArtwork(pending).catch((err) => console.error('[artwork] pass threw:', err));
}

async function runScan() {
  if (scanState.running) return;
  artState.finishedAt = null;
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

const subtitleUrl = (id, name) =>
  `/api/subtitles/${id}/file?name=${encodeURIComponent(name)}`;

/**
 * The tracks a player should offer for a film: the file the user picked by hand
 * if there is one, otherwise the ones whose names match the video.
 */
const itemUrl = (item, choices = {}) => {
  const chosen = choices[item.id];
  const tracks = chosen
    ? [{ name: chosen, label: 'Chosen', lang: '' }]
    : (item.subtitles || []).map((t) => ({ name: t.name, label: t.label, lang: t.lang || '' }));
  // Artwork keeps its filename when it is re-rendered, and the media route is
  // served "immutable", so a browser would happily replay the old clip for an
  // hour. The version and the source file's mtime both go in the URL, which
  // makes a re-rendered asset a different URL and therefore a fresh request.
  const artVersion = `v=${item.previewVersion || 0}-${item.mtimeMs || 0}`;
  const artUrl = (name) => (name ? `/media/${name}?${artVersion}` : null);
  return {
    posterUrl: artUrl(item.poster),
    backdropUrl: artUrl(item.backdrop),
    previewUrl: artUrl(item.preview),
    subtitles: tracks.map((track, index) => ({
      url: subtitleUrl(item.id, track.name),
      name: track.name,
      label: track.label,
      lang: track.lang || '',
      default: index === 0,
    })),
  };
};

function publicSettings(settings) {
  const { vlcPassword, ...safe } = settings;
  return { ...safe, vlcPasswordSet: Boolean(vlcPassword) };
}

function decorate(items, progress, myList, choices = {}) {
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
      ...itemUrl(item, choices),
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
    scan: liveStatus(),
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
  // A background artwork pass is not a scan: let the scan take over, and let the
  // pass stop at its next item boundary instead of running two ffmpeg pools.
  artState.cancelled = true;
  runScan(); // deliberately not awaited: the client polls /api/scan/status
  res.status(202).json({ started: true, scan: scanState });
});

app.get('/api/scan/status', (req, res) => res.json(liveStatus()));

app.post('/api/settings', async (req, res) => {
  const allowed = ['libraryFolders', 'vlcPath', 'vlcHttpPort', 'vlcPassword', 'playMode',
    'vlcFullscreen', 'autoLatency', 'hwDecode'];
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
  if ('autoLatency' in patch && typeof patch.autoLatency !== 'boolean') {
    return res.status(400).json({ error: 'autoLatency must be true or false' });
  }
  if ('hwDecode' in patch && typeof patch.hwDecode !== 'boolean') {
    return res.status(400).json({ error: 'hwDecode must be true or false' });
  }
  if ('playMode' in patch && !['vlc', 'ffmpeg', 'browser'].includes(patch.playMode)) {
    return res.status(400).json({ error: "playMode must be 'vlc', 'ffmpeg' or 'browser'" });
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
    // What this machine could offer if the viewer turns hardware decoding on.
    hwDecode: { available: hwPlansForPlatform().map((plan) => plan.accel).filter((a) => availableHwAccels().includes(a)) },
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

/** Every ffmpeg build can decode; not every one can encode. Cached after the
 *  first question, because it spawns a process. */
let x264Cache;
function hasX264() {
  if (x264Cache !== undefined) return x264Cache;
  x264Cache = false;
  try {
    const out = execFileSync('ffmpeg', ['-hide_banner', '-encoders'],
      { encoding: 'utf8', timeout: 8000, stdio: ['ignore', 'pipe', 'ignore'] });
    x264Cache = /\blibx264\b/.test(out);
  } catch {
    x264Cache = false;
  }
  if (!x264Cache) console.warn('[ffmpeg] this build has no libx264 encoder: files that need transcoding will use the built-in player');
  return x264Cache;
}

/* ------------------------- hardware decoding ----------------------- */

/**
 * Hardware decode is opt-in (`settings.hwDecode`) and always a trial. Even when
 * it is on, the GPU pipe must deliver a first frame inside HW_FIRST_FRAME_MS;
 * otherwise the decoder is marked broken and the same request is served by the
 * software pipe. Nothing about playback depends on the GPU being right.
 */
const HW_FIRST_FRAME_MS = 2500;

/**
 * ffmpeg does not insert `hwdownload` by itself. A GPU decoder leaves frames in
 * a surface format (`qsv`, `d3d11`, `dxva2_vld`) that the implicit scaler cannot
 * consume, which is the "Impossible to convert between the formats supported by
 * the filter 'Parsed_null_0' and the filter 'auto_scale_0'" failure that made
 * v1.1.0-v1.1.10 black. Each plan names the download pixel format to use.
 */
function hwPlansForPlatform() {
  return {
    win32: [
      { accel: 'qsv', outputFormat: 'qsv', pixelFormats: ['nv12', 'p010le'] },
      { accel: 'd3d11va', outputFormat: 'd3d11', pixelFormats: ['nv12', 'p010le'] },
      { accel: 'dxva2', outputFormat: 'dxva2_vld', pixelFormats: ['nv12'] },
    ],
    darwin: [
      { accel: 'videotoolbox', outputFormat: null, pixelFormats: ['nv12', 'p010le'] },
    ],
    linux: [
      { accel: 'qsv', outputFormat: 'qsv', pixelFormats: ['nv12', 'p010le'] },
      { accel: 'vaapi', outputFormat: null, pixelFormats: ['nv12'] },
    ],
  }[process.platform] || [];
}

/** `ffmpeg -hwaccels`, asked once. It reports what the build knows, not what the machine can open. */
let hwAccelsCache;
function availableHwAccels() {
  if (hwAccelsCache !== undefined) return hwAccelsCache;
  hwAccelsCache = [];
  try {
    const out = execFileSync('ffmpeg', ['-hide_banner', '-hwaccels'],
      { encoding: 'utf8', timeout: 8000, stdio: ['ignore', 'pipe', 'ignore'] });
    hwAccelsCache = out.split(/\r?\n/).slice(1).map((line) => line.trim().toLowerCase()).filter(Boolean);
  } catch { /* no ffmpeg, or a build that answers nothing */ }
  return hwAccelsCache;
}

/** Plans already proved broken on this machine, keyed per accel/format/codec. */
const hwBroken = new Set();

/**
 * Download order for one decoder: nv12 first for 8-bit films, a 10-bit format
 * first for 10/12-bit ones — else the first trial is a conversion the hardware
 * usually refuses, and the viewer waits 2.5 s to learn nothing.
 */
function orderDownloads(formats, deep) {
  const isNv12 = (f) => f === 'nv12';
  const primary = deep ? formats.filter((f) => !isNv12(f)) : formats.filter(isNv12);
  return [...primary, ...formats.filter((f) => !primary.includes(f))];
}

/** Every hardware attempt worth making for this file, best first. */
function hwCandidates(item) {
  const listed = availableHwAccels();
  const deep = /10|12/.test(String(item.pixFmt || ''));
  const out = [];
  for (const plan of hwPlansForPlatform()) {
    if (!listed.includes(plan.accel)) continue;
    for (const download of orderDownloads(plan.pixelFormats, deep)) {
      out.push({
        accel: plan.accel,
        outputFormat: plan.outputFormat,
        download,
        key: `${plan.accel}:${download}:${item.vcodec || '?'}`,
      });
    }
  }
  return out;
}

/** The first hardware plan not already proved broken, or null for software. */
function nextHwPlan(item) {
  return hwCandidates(item).find((plan) => !hwBroken.has(plan.key)) || null;
}

/** The ffmpeg command line for one file at one offset, with or without a GPU plan. */
function buildTranscodeArgs(item, start, plan) {
  const copyVideo = item.vcodec === 'h264';
  const copyAudio = copyVideo && item.acodec === 'aac';
  const args = ['-hide_banner', '-loglevel', 'error', '-nostdin'];
  if (start > 0) args.push('-ss', String(start));
  if (!copyVideo && plan) {
    args.push('-hwaccel', plan.accel);
    if (plan.outputFormat) args.push('-hwaccel_output_format', plan.outputFormat);
  }
  args.push('-i', item.path, '-sn', '-dn', '-map', '0:v:0', '-map', '0:a:0?');
  if (copyVideo) {
    args.push('-c:v', 'copy');
  } else {
    // The download is what makes the GPU frames usable by the scaler and x264.
    if (plan) args.push('-vf', `hwdownload,format=${plan.download}`);
    args.push('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '22',
              '-pix_fmt', 'yuv420p', '-g', '48');
  }
  args.push('-c:a', copyAudio ? 'copy' : 'aac');
  if (!copyAudio) args.push('-b:a', '160k', '-ac', '2');
  args.push('-movflags', 'frag_keyframe+empty_moov+default_base_moof', '-f', 'mp4', 'pipe:1');
  return args;
}

function streamViaFfmpeg(req, res, item, opts = {}) {
  // A repeated ?t= arrives as an array; the newest one is the seek the viewer
  // actually asked for. Without this the offset parsed to NaN and became 0.
  const rawStart = Array.isArray(req.query.t) ? req.query.t[req.query.t.length - 1] : req.query.t;
  const start = Math.max(0, Math.floor(Number(rawStart) || 0));
  const copyVideo = item.vcodec === 'h264';
  const copyAudio = copyVideo && item.acodec === 'aac';

  if (!copyVideo && !hasX264()) {
    // Without an encoder the pipe would die silently and the player would show
    // a black screen. Say so, and let the client use the file directly.
    return res.status(501).json({
      error: 'This build of ffmpeg cannot transcode video (no libx264)',
      code: 'NO_X264',
    });
  }

  // Hardware only when asked for, only for files that need transcoding, and
  // never on the retry that follows a failed GPU attempt.
  const wantHardware = !copyVideo && !opts.forceSoftware && store.getSettings().hwDecode === true;
  const plan = wantHardware ? nextHwPlan(item) : null;
  const args = buildTranscodeArgs(item, start, plan);
  const label = copyVideo ? 'copy video'
    : plan ? `transcode (hardware ${plan.accel} -> ${plan.download})`
    : 'transcode (software decode)';
  console.log(`[ffmpeg] ${item.filename}: ${label} at ${start}s`);

  const child = spawn('ffmpeg', args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let started = false;   // first byte reached the browser
  let settled = false;   // this response has been decided one way or the other
  let lastError = '';

  const stop = () => { try { child.kill('SIGKILL'); } catch { /* already gone */ } };
  req.on('close', stop);
  res.on('close', stop);

  /** The GPU pipe did not produce anything: remember it and replay in software. */
  const failover = (why) => {
    if (settled || started || !plan) return;
    settled = true;
    clearTimeout(watchdog);
    stop();
    hwBroken.add(plan.key);
    console.warn(`[ffmpeg] hardware ${plan.accel} (${plan.download}) ${why} — retrying in software`);
    req.removeListener('close', stop);
    res.removeListener('close', stop);
    streamViaFfmpeg(req, res, item, { ...opts, forceSoftware: true });
  };

  const watchdog = plan ? setTimeout(() => {
    if (!started) failover(`produced no frame in ${HW_FIRST_FRAME_MS} ms`);
  }, HW_FIRST_FRAME_MS) : null;
  if (watchdog && typeof watchdog.unref === 'function') watchdog.unref();

  child.stdout.on('data', (chunk) => {
    if (!started) {
      started = true;
      settled = true;
      clearTimeout(watchdog);
      res.writeHead(200, { 'Content-Type': 'video/mp4', 'Cache-Control': 'no-store' });
    }
    res.write(chunk);
  });
  child.stdout.on('end', () => {
    if (started) { res.end(); return; }
    if (plan) return failover(`exited without a frame${lastError ? ` (${lastError})` : ''}`);
    if (!res.headersSent) {
      res.status(502).json({
        error: `ffmpeg produced no output${lastError ? `: ${lastError}` : ''}`,
        code: 'FFMPEG_EMPTY',
      });
    }
  });
  child.stderr.on('data', (buf) => {
    const line = buf.toString().trim();
    if (line) {
      lastError = line.split('\n')[0];
      console.warn('[ffmpeg]', lastError);
    }
  });
  child.on('error', (err) => {
    if (plan && !started) return failover(`could not be started (${err.message})`);
    if (!res.headersSent) {
      res.status(500).json({ error: `Could not run ffmpeg: ${err.message}`, code: 'FFMPEG_MISSING' });
    } else {
      res.end();
    }
  });
  child.on('close', (code) => {
    if (!started && !settled) {
      if (plan) return failover(`exited with code ${code}`);
      if (!res.headersSent) {
        res.status(502).json({
          error: `ffmpeg produced no output${lastError ? `: ${lastError}` : ''}`,
          code: 'FFMPEG_EMPTY',
        });
      }
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

/* ---------------------------- subtitles ---------------------------- */

/** Every subtitle file beside the film, plus which one is in force. */
app.get('/api/subtitles/:id/tracks', (req, res) => {
  const item = findItem(req.params.id);
  if (!item) return res.status(404).json({ error: 'Unknown item' });

  let entries = [];
  try {
    entries = fs.readdirSync(path.dirname(item.path));
  } catch {
    return res.status(410).json({ error: 'Folder is no longer readable' });
  }

  const chosen = store.getSubtitleChoices()[item.id] || null;
  const found = library.listFolderSubtitles(item.path, entries);
  res.json({
    selected: chosen || (item.subtitles || [])[0]?.name || null,
    tracks: found.map((entry) => ({
      name: entry.name,
      label: entry.label,
      matches: entry.matches,
      url: subtitleUrl(item.id, entry.name),
    })),
  });
});

/** Remember a hand-picked subtitle for one film, or clear the choice. */
app.post('/api/subtitles/:id/track', (req, res) => {
  const item = findItem(req.params.id);
  if (!item) return res.status(404).json({ error: 'Unknown item' });

  const name = req.body?.name ?? null;
  if (name !== null) {
    // Only files that actually sit in this film's folder, and only subtitle files:
    // the value arrives from the client and is used to build a path.
    let entries = [];
    try {
      entries = fs.readdirSync(path.dirname(item.path));
    } catch {
      return res.status(410).json({ error: 'Folder is no longer readable' });
    }
    const allowed = library.listFolderSubtitles(item.path, entries).map((t) => t.name);
    if (!allowed.includes(name)) {
      return res.status(400).json({ error: 'That file is not a subtitle in this folder' });
    }
  }
  store.setSubtitleChoice(item.id, name);
  res.json({ selected: name });
});

/** Sidecar subtitles, converted to WebVTT on the way out. */
app.get('/api/subtitles/:id/file', (req, res) => {
  const item = findItem(req.params.id);
  if (!item) return res.status(404).json({ error: 'Unknown item' });

  const name = String(req.query.name || '');
  let entries = [];
  try {
    entries = fs.readdirSync(path.dirname(item.path));
  } catch {
    return res.status(410).json({ error: 'Folder is no longer readable' });
  }
  // Resolve the request against the real listing rather than the raw string, so a
  // crafted name cannot escape the folder.
  const match = library.listFolderSubtitles(item.path, entries).find((t) => t.name === name);
  if (!match) return res.status(404).json({ error: 'Unknown subtitle file' });

  let raw;
  try {
    raw = fs.readFileSync(path.join(path.dirname(item.path), match.name));
  } catch {
    return res.status(410).json({ error: 'Subtitle file is no longer on disk' });
  }
  res.set('Content-Type', 'text/vtt; charset=utf-8');
  res.set('Cache-Control', 'no-store');
  res.send(library.toVtt(raw));
});

// The page says when it has actually painted. Background artwork waits for it:
// drawing first competes with WebView2 for the CPU, and on a cold start the
// window took longer than the launcher's watchdog to appear.
let uiReady = false;
app.post('/api/ui/ready', (req, res) => {
  uiReady = true;
  res.json({ ok: true });
});

app.get('/api/health', (req, res) => res.json({ ok: true, uptime: process.uptime() }));

app.use((req, res) => res.status(404).json({ error: 'Not found' }));

/**
 * Is anything in the stored catalogue out of date?
 *
 * Artwork format changes between releases, and a re-install must not leave the
 * user staring at clips rendered by an older version -- which is exactly what
 * happened in 1.1.2, when the preview format changed but nothing re-rendered it
 * until the user pressed Scan. Checking is cheap (the catalogue is already in
 * memory); doing the work is not, so it only happens when something needs it.
 */
function sweepTempArtwork() {
  const removed = library.sweepTempArt(MEDIA_DIR);
  if (removed) console.log(`[startup] cleared ${removed} half-written artwork file(s) from an interrupted run`);
}

/**
 * A pass killed between writing a file and saving the catalogue leaves the file
 * on disk with the entry still empty. Adopt those files instead of drawing them
 * again, so a crash costs nothing but the item that was in flight.
 */
function artworkOnDisk() {
  const catalog = store.getCatalog();
  const items = catalog.items || [];
  let adopted = 0;
  let dropped = 0;
  const missing = [];
  for (const item of items) {
    const files = library.artPaths(MEDIA_DIR, item.id);
    const state = library.missingArt(item, MEDIA_DIR); // one stat per file
    // A pass killed between writing a file and saving the catalogue leaves the
    // file on disk with the entry empty: adopt it instead of drawing it again.
    for (const key of ['poster', 'backdrop', 'preview']) {
      if (!item[key] && !state[key]) { item[key] = path.basename(files[key]); adopted += 1; }
      // Pointing at a file that is gone makes the page request a 404 and show a
      // broken image; the gradient fallback is better until it is redrawn.
      else if (item[key] && state[key]) { item[key] = null; dropped += 1; }
    }
    if (state.poster || state.backdrop || state.preview) missing.push(item);
  }
  if (adopted || dropped) store.saveCatalog(catalog);
  return { catalog, items, missing, adopted };
}

function pendingArtwork() {
  return artworkOnDisk().missing.filter((item) => library.shouldRetryArt(item));
}

function renderWhenUiIsUp(pending) {
  const deadline = Date.now() + 20000; // never wait forever: the window may fail
  const wait = () => {
    if (artState.cancelled || scanState.running) { artState.running = false; return; }
    if (uiReady || Date.now() >= deadline) {
      artState.running = false;
      renderPending(pending);
      return;
    }
    setTimeout(wait, 250);
  };
  setTimeout(wait, 250);
}

if (require.main === module) {
  app.listen(PORT, HOST, () => {
    console.log(`VLCFlix on http://${HOST}:${PORT}`);
    const settings = store.getSettings();
    if (!settings.libraryFolders.length) {
      console.log('No library folders configured yet — point VLCFlix at your videos from Settings.');
      return;
    }

    const started = Date.now();
    sweepTempArtwork();
    const { items, missing, adopted } = artworkOnDisk();
    const pending = missing.filter((item) => library.shouldRetryArt(item));
    console.log(`[startup] artwork check: ${items.length} item(s) in ${Date.now() - started} ms, ${missing.length} missing, ${adopted} adopted`);
    if (!pending.length) {
      console.log(`[startup] artwork complete for all ${items.length} item(s) — nothing to draw`);
      return;
    }
    console.log(`[startup] artwork: ${items.length - missing.length} of ${items.length} already drawn, resuming ${pending.length} — no rescan needed`);
    // Announced now so the window paints a progress chip when it opens; the
    // drawing itself waits until the page is on screen.
    Object.assign(artState, { running: true, done: 0, total: pending.length, current: '', startedAt: Date.now() });
    renderWhenUiIsUp(pending);
  });
}

module.exports = { app, runScan, scanState, pendingArtwork, artworkOnDisk,
  buildTranscodeArgs, hwCandidates, nextHwPlan, hwBroken, hwPlansForPlatform,
  availableHwAccels, orderDownloads };
