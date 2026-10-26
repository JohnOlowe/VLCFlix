'use strict';

/**
 * Parser tests run against real VLC 3.0.24 output captured in tests/fixtures.
 * Hand-written XML would only prove the parser matches my assumptions; these
 * fixtures prove it matches the player.
 */

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { normalizeStatus, normalizePlaylist, detect } = require('../server/vlc');

const fixture = (name) => fs.readFileSync(path.join(__dirname, 'fixtures', name), 'utf8');

test('normalizeStatus reads state, timing and volume from real VLC output', () => {
  const status = normalizeStatus(fixture('status.xml'));

  assert.equal(status.state, 'playing');
  assert.equal(status.length, 16);
  assert.equal(typeof status.time, 'number');
  assert.ok(status.position >= 0 && status.position <= 1, 'position is a 0-1 fraction');
  assert.equal(status.rate, 1);
  assert.equal(status.filename === undefined, true);
  assert.equal(status.meta.filename, 'ghostlight.mp4');
  assert.match(status.version, /^3\.0\./);
});

test('normalizeStatus converts VLC 256-scale volume to percent', () => {
  const withVolume = (raw) => `<?xml version="1.0"?><root><state>playing</state><length>10</length><time>5</time><volume>${raw}</volume></root>`;
  assert.equal(normalizeStatus(withVolume(256)).volumePercent, 100);
  assert.equal(normalizeStatus(withVolume(128)).volumePercent, 50);
  assert.equal(normalizeStatus(withVolume(0)).volumePercent, 0);
});

test('normalizeStatus tolerates a stopped player with no media', () => {
  const status = normalizeStatus('<?xml version="1.0"?><root><state>stopped</state><length>0</length><time>0</time></root>');
  assert.equal(status.state, 'stopped');
  assert.equal(status.length, 0);
  assert.equal(status.position, 0);
  assert.equal(status.title, '');
});

test('normalizePlaylist finds <leaf> entries nested in <node> folders', () => {
  const items = normalizePlaylist(fixture('playlist.xml'));

  assert.equal(items.length, 1, 'the single playlist entry is found');
  assert.deepEqual(items.map((i) => i.name), ['ghostlight.mp4']);
  assert.ok(items.every((i) => i.uri.startsWith('file:///')), 'URIs survive parsing');
  assert.ok(items.every((i) => i.duration === 16), 'durations are numbers, not strings');
  assert.equal(items.filter((i) => i.current).length, 1, 'exactly one entry is marked current');
});

test('normalizePlaylist returns an empty list for an empty playlist', () => {
  assert.deepEqual(normalizePlaylist('<?xml version="1.0"?><node name="" id="0"></node>'), []);
});

test('detect returns a path, never a boolean', () => {
  // Regression: looksExecutable() used to return the boolean from isFile(),
  // so detect() handed `true` to spawn() instead of an executable path.
  for (const override of [null, '/definitely/not/vlc', '/usr/bin/vlc']) {
    const result = detect(override);
    assert.ok(result.path === null || typeof result.path === 'string', `path is a string or null for ${override}`);
    assert.equal(typeof result.source, 'string');
  }
});
