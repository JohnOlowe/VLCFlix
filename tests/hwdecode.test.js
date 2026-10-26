'use strict';

/**
 * Hardware decoding is opt-in, and even then it is only a trial: the command
 * line has to carry hwdownload (or the GPU frames never reach the encoder, which
 * is the black screen v1.1.0-v1.1.10 shipped), and every failure has to be able
 * to fall back to software.
 */

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'vlcflix-hw-'));
process.env.VLCFLIX_DATA_DIR = TMP;

const store = require('../server/store');
const {
  app, buildTranscodeArgs, hwCandidates, nextHwPlan, hwBroken, orderDownloads,
} = require('../server/index');

const item = (over = {}) => ({
  path: '/films/Title 15.mp4', filename: 'Title 15.mp4', vcodec: 'hevc', acodec: 'aac',
  pixFmt: 'yuv420p10le', ...over,
});
const qsv = { accel: 'qsv', outputFormat: 'qsv', download: 'nv12', key: 'qsv:nv12:hevc' };

test('without a hardware plan the command line is pure software', () => {
  const args = buildTranscodeArgs(item(), 0, null);
  assert.equal(args.includes('-hwaccel'), false, 'no GPU decoder must be asked for');
  assert.equal(args.includes('-hwaccel_output_format'), false);
  assert.equal(args.includes('hwdownload,format=nv12'), false);
  assert.ok(args.includes('-c:v') && args[args.indexOf('-c:v') + 1] === 'libx264');
});

test('an H.264 film is stream-copied even when a GPU plan is offered', () => {
  // Copying never decodes, so the GPU has nothing to do and must not be touched.
  const args = buildTranscodeArgs(item({ vcodec: 'h264' }), 0, qsv);
  assert.equal(args[args.indexOf('-c:v') + 1], 'copy');
  assert.equal(args.includes('-hwaccel'), false);
});

test('a hardware plan puts the decoder before -i and the download after it', () => {
  const args = buildTranscodeArgs(item(), 0, qsv);
  const inputAt = args.indexOf('-i');
  assert.ok(args.indexOf('-hwaccel') < inputAt, '-hwaccel is an input option');
  assert.equal(args[args.indexOf('-hwaccel') + 1], 'qsv');
  assert.ok(args.indexOf('-hwaccel_output_format') < inputAt);
  assert.equal(args[args.indexOf('-hwaccel_output_format') + 1], 'qsv');

  const vf = args.indexOf('-vf');
  assert.ok(vf > inputAt, '-vf is an output option');
  assert.equal(args[vf + 1], 'hwdownload,format=nv12',
    'without hwdownload the scaler and x264 cannot read the GPU frames');
  assert.ok(args.indexOf('-c:v') > vf);
});

test('a seek offset is applied before the input, GPU plan or not', () => {
  const args = buildTranscodeArgs(item(), 42, qsv);
  assert.equal(args[args.indexOf('-ss') + 1], '42');
  assert.ok(args.indexOf('-ss') < args.indexOf('-i'));
});

test('8-bit films try nv12 first, 10-bit films try the 10-bit format first', () => {
  assert.deepEqual(orderDownloads(['nv12', 'p010le'], false), ['nv12', 'p010le']);
  assert.deepEqual(orderDownloads(['nv12', 'p010le'], true), ['p010le', 'nv12']);
  // A decoder that only offers one format is untouched either way.
  assert.deepEqual(orderDownloads(['nv12'], true), ['nv12']);
});

test('candidates are only made for decoders this ffmpeg lists, and carry unique keys', () => {
  const plans = hwCandidates(item());
  const keys = plans.map((plan) => plan.key);
  assert.equal(new Set(keys).size, keys.length, 'keys identify a plan per film');
  for (const plan of plans) {
    assert.ok(plan.accel && plan.download, 'a plan names both the decoder and the download');
  }
});

test('once a plan is proved broken it is skipped, and software is chosen', () => {
  const plans = hwCandidates(item());
  if (!plans.length) return; // no GPU decoder in this ffmpeg build
  for (const plan of plans) hwBroken.add(plan.key);
  try {
    assert.equal(nextHwPlan(item()), null, 'no candidate left: the caller must transcode in software');
  } finally {
    for (const plan of plans) hwBroken.delete(plan.key);
  }
  assert.ok(nextHwPlan(item()), 'and a fresh look offers the plan again');
});

test('hardware decoding is off by default and can only be set to a boolean', async () => {
  const server = app.listen(0);
  await new Promise((resolve) => server.once('listening', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    assert.equal(store.getSettings().hwDecode, false, 'software is the default');

    const bad = await fetch(`${base}/api/settings`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ hwDecode: 'yes' }),
    });
    assert.equal(bad.status, 400);
    assert.equal(store.getSettings().hwDecode, false, 'a bad value changes nothing');

    const ok = await fetch(`${base}/api/settings`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ hwDecode: true }),
    });
    assert.equal(ok.status, 200);
    assert.equal(store.getSettings().hwDecode, true);

    const system = await fetch(`${base}/api/system`).then((r) => r.json());
    assert.ok(Array.isArray(system.hwDecode.available), 'the UI is told what the machine offers');
  } finally {
    server.close();
  }
});
