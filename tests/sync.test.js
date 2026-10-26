'use strict';

/**
 * Sync arithmetic.
 *
 * These cases encode the reasoning, not just the implementation: every request for
 * negative audio offset has to come back as a positive video delay, because a
 * browser can delay the picture but can never advance the sound.
 */

const test = require('node:test');
const assert = require('node:assert/strict');
const { computeSync, smoothLatency, MAX_VIDEO_DELAY_MS } = require('../public/js/sync-math');

test('with everything at zero, nothing is delayed', () => {
  assert.deepEqual(computeSync({}), { videoDelayMs: 0, audioDelayMs: 0, clamped: false });
});

test('device latency alone delays the picture, never the sound', () => {
  // Sound arriving 180 ms late is fixed by showing the picture 180 ms later.
  const r = computeSync({ deviceLatencyMs: 180 });
  assert.equal(r.videoDelayMs, 180);
  assert.equal(r.audioDelayMs, 0);
});

test('negative manual offset becomes video delay', () => {
  // "Audio 200 ms earlier" == "picture 200 ms later".
  const r = computeSync({ manualMs: -200 });
  assert.equal(r.videoDelayMs, 200);
  assert.equal(r.audioDelayMs, 0);
});

test('positive manual offset uses the audio delay node', () => {
  const r = computeSync({ manualMs: 150 });
  assert.equal(r.videoDelayMs, 0);
  assert.equal(r.audioDelayMs, 150);
});

test('manual offset is applied on top of the measured latency', () => {
  // 180 ms of device latency, user asks for sound 50 ms later:
  // the picture is delayed by the remaining 130 ms.
  const r = computeSync({ deviceLatencyMs: 180, manualMs: 50 });
  assert.equal(r.videoDelayMs, 130);
  assert.equal(r.audioDelayMs, 0);
});

test('a manual offset larger than the latency flips to the audio side', () => {
  const r = computeSync({ deviceLatencyMs: 100, manualMs: 300 });
  assert.equal(r.videoDelayMs, 0);
  assert.equal(r.audioDelayMs, 200);
});

test('automatic compensation stays separate from the manual offset', () => {
  // Turning the manual control to zero must not cancel the device correction.
  const withManual = computeSync({ deviceLatencyMs: 220, manualMs: -40 });
  const without = computeSync({ deviceLatencyMs: 220, manualMs: 0 });
  assert.equal(without.videoDelayMs, 220, 'device correction survives manual zero');
  assert.equal(withManual.videoDelayMs, 260, 'manual earlier adds to the device delay');
});

test('never returns a negative delay of either kind', () => {
  for (const latency of [0, 50, 500]) {
    for (const manual of [-3000, -100, 0, 100, 3000]) {
      const r = computeSync({ deviceLatencyMs: latency, manualMs: manual });
      assert.ok(r.videoDelayMs >= 0, `videoDelay ${r.videoDelayMs} < 0`);
      assert.ok(r.audioDelayMs >= 0, `audioDelay ${r.audioDelayMs} < 0`);
    }
  }
});

test('an impossible video delay is clamped and reported', () => {
  const r = computeSync({ manualMs: -60000 });
  assert.equal(r.videoDelayMs, MAX_VIDEO_DELAY_MS);
  assert.equal(r.clamped, true);
});

test('a reachable delay is not reported as clamped', () => {
  assert.equal(computeSync({ deviceLatencyMs: 180 }).clamped, false);
});

test('nonsense input does not produce nonsense output', () => {
  const r = computeSync({ deviceLatencyMs: NaN, manualMs: undefined });
  assert.deepEqual(r, { videoDelayMs: 0, audioDelayMs: 0, clamped: false });
});

test('latency smoothing follows small changes gradually', () => {
  const next = smoothLatency(180, 220);
  assert.ok(next > 180 && next < 220, `expected a value between, got ${next}`);
});

test('latency smoothing jumps on a large change', () => {
  // Switching from earbuds to speakers is not a drift, it is a new device.
  assert.equal(smoothLatency(180, 900), 900);
});

test('the first measurement is taken as-is', () => {
  assert.equal(smoothLatency(0, 175), 175);
});
