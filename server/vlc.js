'use strict';

/**
 * VLC integration.
 *
 * VLC ships a documented HTTP control interface. Launching it with
 *   --extraintf http --http-port N --http-password P
 * exposes /requests/status.xml (player state) and /requests/playlist.xml
 * (queue), and accepts commands as query parameters, e.g.
 *   /requests/status.xml?command=pl_pause
 *
 * That is the whole trick behind VLCFlix: we own the browsing UI, and drive
 * the genuine VLC engine underneath instead of re-implementing a player.
 */

const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { pathToFileURL } = require('node:url');
const { XMLParser } = require('fast-xml-parser');

const parser = new XMLParser({
  ignoreAttributes: false,
  attributeNamePrefix: '',
  textNodeName: '#text',
  parseAttributeValue: false,
  parseTagValue: false,
  trimValues: true,
});

const asArray = (v) => (v == null ? [] : Array.isArray(v) ? v : [v]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* ------------------------------------------------------------------ */
/* Binary detection                                                    */
/* ------------------------------------------------------------------ */

const PATH_CANDIDATES = {
  win32: [
    'C:\\Program Files\\VideoLAN\\VLC\\vlc.exe',
    'C:\\Program Files (x86)\\VideoLAN\\VLC\\vlc.exe',
    'vlc.exe',
  ],
  darwin: ['/Applications/VLC.app/Contents/MacOS/VLC', 'vlc'],
  linux: ['/usr/bin/vlc', '/usr/local/bin/vlc', '/snap/bin/vlc', 'vlc'],
};

function looksExecutable(p) {
  try {
    if (path.isAbsolute(p)) {
      fs.accessSync(p, fs.constants.X_OK);
      // Return the path itself -- returning the boolean from isFile() here is
      // how detect() handed `true` to spawn() instead of an executable.
      return fs.statSync(p).isFile() ? p : null;
    }
    // Bare name: scan PATH ourselves so this works without spawning a shell.
    const exts = process.platform === 'win32' ? ['', '.exe', '.cmd', '.bat'] : [''];
    for (const dir of (process.env.PATH || '').split(path.delimiter)) {
      if (!dir) continue;
      for (const ext of exts) {
        const candidate = path.join(dir, p + ext);
        try {
          fs.accessSync(candidate, fs.constants.X_OK);
          if (fs.statSync(candidate).isFile()) return candidate;
        } catch {
          /* keep looking */
        }
      }
    }
    return null;
  } catch {
    return null;
  }
}

/** @returns {{ path: string|null, source: string }} */
function detect(overridePath) {
  if (overridePath) {
    const resolved = looksExecutable(overridePath);
    if (resolved) return { path: resolved, source: 'settings' };
    return { path: null, source: 'settings (not found)' };
  }
  if (process.env.VLCFLIX_VLC_PATH) {
    const resolved = looksExecutable(process.env.VLCFLIX_VLC_PATH);
    if (resolved) return { path: resolved, source: 'VLCFLIX_VLC_PATH' };
  }
  for (const candidate of PATH_CANDIDATES[process.platform] || PATH_CANDIDATES.linux) {
    const resolved = looksExecutable(candidate);
    if (resolved) return { path: resolved, source: 'auto-detect' };
  }
  return { path: null, source: 'not found' };
}

/* ------------------------------------------------------------------ */
/* XML parsing                                                         */
/* ------------------------------------------------------------------ */

/** Flatten VLC's <information><category name="meta"><info name=".."> tree. */
function readMeta(information) {
  const meta = {};
  if (!information) return meta;
  for (const category of asArray(information.category)) {
    if (!category || category.name !== 'meta') continue;
    for (const info of asArray(category.info)) {
      if (info && info.name) meta[info.name] = String(info['#text'] ?? '').trim();
    }
  }
  return meta;
}

const num = (v, fallback = 0) => {
  const n = Number.parseFloat(String(v));
  return Number.isFinite(n) ? n : fallback;
};

function normalizeStatus(xml) {
  const root = parser.parse(xml)?.root || {};
  const meta = readMeta(root.information);
  const length = num(root.length);
  const time = num(root.time);
  return {
    state: String(root.state || 'stopped').toLowerCase(), // playing | paused | stopped
    time,
    length,
    position: length > 0 ? Math.min(1, time / length) : num(root.position),
    volume: num(root.volume), // VLC scale: 256 = 100%
    volumePercent: Math.round((num(root.volume) / 256) * 100),
    rate: num(root.rate, 1),
    fullscreen: String(root.fullscreen) === '1',
    version: String(root.version || ''),
    title: meta.title || meta.filename || meta.now_playing || '',
    meta,
  };
}

function normalizePlaylist(xml) {
  const root = parser.parse(xml)?.node || {};
  const items = [];

  // VLC puts entries in <leaf> elements and folders in <node> elements, which
  // can nest arbitrarily (Playlist, Media Library, ...). Walking only <node>
  // silently yields an empty queue -- verified against VLC 3.0.24 output.
  const toItem = (leaf) => ({
    id: String(leaf.id ?? ''),
    name: String(leaf.name ?? ''),
    uri: String(leaf.uri),
    duration: num(leaf.duration),
    current: leaf.current === 'current',
  });

  const walk = (node) => {
    if (!node || typeof node !== 'object') return;
    for (const leaf of asArray(node.leaf)) {
      if (leaf && leaf.uri) items.push(toItem(leaf));
    }
    for (const child of asArray(node.node)) walk(child);
  };

  walk(root);
  return items;
}

/* ------------------------------------------------------------------ */
/* HTTP client                                                         */
/* ------------------------------------------------------------------ */

class VlcClient {
  constructor({ host = '127.0.0.1', port, password }) {
    this.baseUrl = `http://${host}:${port}/`;
    this.password = password || '';
  }

  get authHeader() {
    return 'Basic ' + Buffer.from(`:${this.password}`).toString('base64');
  }

  async request(pathname, { timeout = 4000 } = {}) {
    const res = await fetch(new URL(pathname, this.baseUrl), {
      headers: { Authorization: this.authHeader },
      signal: AbortSignal.timeout(timeout),
    });
    if (res.status === 401) {
      const err = new Error('VLC rejected the password');
      err.code = 'VLC_AUTH';
      throw err;
    }
    if (!res.ok) {
      const err = new Error(`VLC returned HTTP ${res.status}`);
      err.code = 'VLC_HTTP';
      err.status = res.status;
      throw err;
    }
    return res.text();
  }

  async status(opts) {
    return normalizeStatus(await this.request('/requests/status.xml', opts));
  }

  async playlist(opts) {
    return normalizePlaylist(await this.request('/requests/playlist.xml', opts));
  }

  /** Send a control command, e.g. cmd({ command: 'seek', val: '+10' }). */
  async cmd(params) {
    const qs = new URLSearchParams(params).toString();
    return normalizeStatus(await this.request(`/requests/status.xml?${qs}`));
  }

  /** True if a VLC with the HTTP interface is reachable right now. */
  async ping(timeout = 1200) {
    try {
      await this.status({ timeout });
      return true;
    } catch {
      return false;
    }
  }

  async playFile(absolutePath) {
    return this.cmd({ command: 'in_play', input: pathToFileURL(absolutePath).href });
  }

  async pause() {
    const s = await this.status();
    if (s.state === 'playing') return this.cmd({ command: 'pl_pause' });
    return s;
  }

  async resume() {
    const s = await this.status();
    if (s.state === 'paused') return this.cmd({ command: 'pl_pause' });
    return s;
  }

  async togglePlay() {
    return this.cmd({ command: 'pl_pause' });
  }

  async stop() {
    return this.cmd({ command: 'pl_stop' });
  }

  async seek(seconds) {
    return this.cmd({ command: 'seek', val: String(Math.max(0, Math.round(seconds))) });
  }

  async setVolume(percent) {
    const clamped = Math.max(0, Math.min(100, Math.round(percent)));
    return this.cmd({ command: 'volume', val: String(Math.round((clamped / 100) * 256)) });
  }
}

/* ------------------------------------------------------------------ */
/* Launching VLC                                                       */
/* ------------------------------------------------------------------ */

/**
 * Start a fresh VLC with the HTTP interface enabled, optionally auto-playing a
 * file, then wait until the control interface answers.
 *
 * The child is detached so it outlives the VLCFlix server: closing the web UI
 * should not kill your video.
 */
async function launch({ binary, port, password, file, fullscreen = true, waitMs = 15000 }) {
  const args = [
    '--extraintf=http',
    '--http-host=127.0.0.1',
    `--http-port=${port}`,
    `--http-password=${password}`,
    '--no-video-title-show',
  ];
  if (fullscreen) args.push('--fullscreen');
  if (file) args.push(pathToFileURL(file).href);

  // Keep the child's stderr: if VLC dies on startup we want to show its own
  // complaint ("no suitable interface module", a bad option, ...) instead of a
  // bare timeout twenty seconds later.
  const child = spawn(binary, args, {
    detached: true,
    stdio: ['ignore', 'ignore', 'pipe'],
    windowsHide: false,
  });

  let stderr = '';
  child.stderr?.on('data', (chunk) => {
    stderr = (stderr + chunk.toString()).slice(-4000);
  });
  child.on('error', (err) => {
    stderr += `\nspawn error: ${err.message}`;
  });

  let exit = null;
  child.on('exit', (code, signal) => { exit = { code, signal }; });
  child.unref();

  const client = new VlcClient({ port, password });
  const deadline = Date.now() + waitMs;
  while (Date.now() < deadline) {
    if (exit) {
      const detail = stderr.split('\n').filter(Boolean).slice(-2).join(' ').trim();
      return {
        ok: false,
        client,
        error: `VLC exited immediately (${exit.signal ? `signal ${exit.signal}` : `code ${exit.code}`})`
          + (detail ? `: ${detail}` : ''),
      };
    }
    await sleep(300);
    if (await client.ping(800)) return { ok: true, client };
  }

  const detail = stderr.split('\n').filter(Boolean).slice(-2).join(' ').trim();
  return {
    ok: false,
    client,
    error: 'VLC did not open its control interface in time'
      + (detail ? `: ${detail}` : ' (is another program using that port?)'),
  };
}

module.exports = {
  VlcClient,
  detect,
  launch,
  normalizeStatus,
  normalizePlaylist,
  readMeta,
};
