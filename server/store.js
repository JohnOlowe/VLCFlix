'use strict';

/**
 * Tiny JSON-file store. Everything VLCFlix remembers between runs lives in
 * ./data as plain JSON so it can be inspected, backed up, or deleted by hand.
 */

const fs = require('node:fs');
const path = require('node:path');

const DATA_DIR = process.env.VLCFLIX_DATA_DIR
  ? path.resolve(process.env.VLCFLIX_DATA_DIR)
  : path.join(__dirname, '..', 'data');

const DEFAULTS = {
  settings: {
    // Folders scanned for video files. Empty on first run.
    libraryFolders: [],
    // Absolute path to the VLC executable. null = auto-detect.
    vlcPath: null,
    // Port VLC's HTTP control interface listens on.
    vlcHttpPort: 8080,
    // VLC refuses to expose its HTTP interface without one.
    vlcPassword: 'vlcflix',
    // 'vlc' drives the real player, 'browser' uses the built-in player.
    playMode: 'vlc',
    // Ask VLC to go fullscreen when playback starts.
    vlcFullscreen: true,
  },
  progress: {}, // { [itemId]: { position, duration, updatedAt } }
  myList: [], // [itemId]
};

function ensureDir() {
  fs.mkdirSync(DATA_DIR, { recursive: true });
}

function fileFor(name) {
  return path.join(DATA_DIR, name + '.json');
}

function read(name) {
  ensureDir();
  const file = fileFor(name);
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (err) {
    if (err.code !== 'ENOENT') {
      console.warn(`[store] ignoring unreadable ${file}: ${err.message}`);
    }
    return null;
  }
}

function write(name, value) {
  ensureDir();
  const file = fileFor(name);
  const tmp = file + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(value, null, 2));
  fs.renameSync(tmp, file); // atomic enough to survive a mid-write crash
  return value;
}

function getSettings() {
  return { ...DEFAULTS.settings, ...(read('settings') || {}) };
}

function saveSettings(patch) {
  return write('settings', { ...getSettings(), ...patch });
}

function getCatalog() {
  return read('catalog') || { scannedAt: null, items: [], errors: [] };
}

function saveCatalog(catalog) {
  return write('catalog', catalog);
}

function getProgress() {
  return read('progress') || {};
}

function setProgress(id, position, duration) {
  const all = getProgress();
  // Below 2% we treat it as "not started"; above 92% it is finished.
  if (position / Math.max(duration, 1) < 0.02) delete all[id];
  else all[id] = { position, duration, updatedAt: Date.now() };
  return write('progress', all);
}

function getMyList() {
  return read('myList') || [];
}

function toggleMyList(id) {
  const list = getMyList();
  const next = list.includes(id) ? list.filter((x) => x !== id) : [...list, id];
  write('myList', next);
  return next;
}

module.exports = {
  DATA_DIR,
  getSettings,
  saveSettings,
  getCatalog,
  saveCatalog,
  getProgress,
  setProgress,
  getMyList,
  toggleMyList,
};
