'use strict';

/**
 * API tests boot the real Express app against a throwaway data directory, so
 * they never read or write the developer's library.
 */

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'vlcflix-test-'));
process.env.VLCFLIX_DATA_DIR = TMP;

const store = require('../server/store');
const { app } = require('../server/index');

let server;
let base;

test.before(async () => {
  server = app.listen(0);
  await new Promise((resolve) => server.once('listening', resolve));
  base = `http://127.0.0.1:${server.address().port}`;
});

test.after(() => {
  server?.close();
  fs.rmSync(TMP, { recursive: true, force: true });
});

const get = async (p) => {
  const res = await fetch(base + p);
  return { status: res.status, body: await res.json().catch(() => null) };
};

const post = async (p, body) => {
  const res = await fetch(base + p, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  return { status: res.status, body: await res.json().catch(() => null) };
};

/** Put one fake item in the catalog, backed by a real file on disk. */
function seedCatalog() {
  const file = path.join(TMP, 'sample.mp4');
  const bytes = Buffer.alloc(2048, 7);
  fs.writeFileSync(file, bytes);
  const id = 'testitem0001';
  store.saveCatalog({
    scannedAt: Date.now(),
    folders: [TMP],
    tools: { ffmpeg: false, ffprobe: false, canRender: false },
    errors: [],
    items: [{
      id,
      title: 'Sample Title',
      year: 2024,
      genres: ['Drama'],
      synopsis: null,
      path: file,
      filename: 'sample.mp4',
      size: bytes.length,
      mtimeMs: 1,
      addedAt: 1,
      duration: 12,
      width: 1280,
      height: 720,
      poster: null,
      backdrop: null,
      preview: null,
    }],
  });
  return { id, file, bytes };
}

test('health endpoint responds', async () => {
  const res = await get('/api/health');
  assert.equal(res.status, 200);
  assert.equal(res.body.ok, true);
});

test('bootstrap returns a stable shape on an empty library', async () => {
  const res = await get('/api/bootstrap');
  assert.equal(res.status, 200);
  assert.equal(Array.isArray(res.body.library.items), true);
  assert.equal(Array.isArray(res.body.library.rails), true);
  assert.equal(typeof res.body.settings, 'object');
  assert.equal(typeof res.body.vlc, 'object');
  assert.equal('vlcPassword' in res.body.settings, false, 'the VLC password is never sent to the client');
});

test('settings rejects an out-of-range port', async () => {
  const res = await post('/api/settings', { vlcHttpPort: 99999 });
  assert.equal(res.status, 400);
  assert.match(res.body.error, /port/i);
});

test('settings rejects an unknown play mode', async () => {
  const res = await post('/api/settings', { playMode: 'telepathy' });
  assert.equal(res.status, 400);
});

test('settings normalises and de-duplicates nothing silently', async () => {
  const res = await post('/api/settings', { libraryFolders: ['  /tmp/videos  ', ''] });
  assert.equal(res.status, 200);
  assert.deepEqual(res.body.settings.libraryFolders, ['/tmp/videos']);
});

test('playing an unknown item is a 404', async () => {
  const res = await post('/api/player/play', { id: 'nope' });
  assert.equal(res.status, 404);
});

test('an unknown player command is a 400', async () => {
  const res = await post('/api/player/command', { action: 'selfdestruct' });
  assert.equal(res.status, 400);
});

test('built-in playback returns a stream url and honours the resume point', async () => {
  const { id } = seedCatalog();
  const res = await post('/api/player/play', { id, mode: 'browser', startAt: 4.5 });
  assert.equal(res.status, 200);
  assert.equal(res.body.mode, 'browser');
  assert.match(res.body.streamUrl, new RegExp(`/api/stream/${id}`));
  assert.equal(res.body.seekTo, 4.5);
});

test('progress round-trips through bootstrap', async () => {
  const { id } = seedCatalog();
  await post('/api/progress', { id, position: 6, duration: 12 });
  const res = await get('/api/bootstrap');
  const item = res.body.library.items.find((i) => i.id === id);
  assert.equal(item.progress.percent, 50);
  assert.equal(item.inMyList, false);
});

test('progress under 2% is treated as not started', async () => {
  const { id } = seedCatalog();
  await post('/api/progress', { id, position: 0.1, duration: 100 });
  const progress = store.getProgress();
  assert.equal(progress[id], undefined);
});

test('my list toggles on and off', async () => {
  const { id } = seedCatalog();
  const on = await post('/api/mylist', { id });
  assert.deepEqual(on.body.myList, [id]);
  const off = await post('/api/mylist', { id });
  assert.deepEqual(off.body.myList, []);
});

test('streaming supports range requests', async () => {
  const { id, bytes } = seedCatalog();
  const res = await fetch(`${base}/api/stream/${id}`, { headers: { Range: 'bytes=0-99' } });
  assert.equal(res.status, 206);
  assert.equal(res.headers.get('content-type'), 'video/mp4');
  assert.equal(res.headers.get('content-range'), `bytes 0-99/${bytes.length}`);
  const body = Buffer.from(await res.arrayBuffer());
  assert.equal(body.length, 100);
  assert.equal(body[0], 7);
});

test('streaming serves the whole file without a range header', async () => {
  const { id, bytes } = seedCatalog();
  const res = await fetch(`${base}/api/stream/${id}`);
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('content-length'), String(bytes.length));
  assert.equal(res.headers.get('accept-ranges'), 'bytes');
});

test('an unsatisfiable range is rejected with 416', async () => {
  const { id, bytes } = seedCatalog();
  const res = await fetch(`${base}/api/stream/${id}`, { headers: { Range: `bytes=${bytes.length + 10}-` } });
  assert.equal(res.status, 416);
});

test('streaming a missing item is a 404', async () => {
  const res = await fetch(`${base}/api/stream/does-not-exist`);
  assert.equal(res.status, 404);
});

test('an item whose file vanished reports 410 rather than crashing', async () => {
  const { id, file } = seedCatalog();
  fs.rmSync(file);
  const res = await fetch(`${base}/api/stream/${id}`);
  assert.equal(res.status, 410);
});

test('unknown api routes return json 404', async () => {
  const res = await get('/api/nope');
  assert.equal(res.status, 404);
  assert.equal(res.body.error, 'Not found');
});
