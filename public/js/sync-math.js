/**
 * Playback sync arithmetic, shared by the browser and the test suite.
 *
 * The problem this solves: a browser can delay sound, but it can never play sound
 * earlier than the file's own timeline. Bluetooth earbuds add output latency, so
 * the sound arrives late and the picture looks early -- and the "obvious" fix,
 * advancing the audio, is the one thing a <video> element cannot do.
 *
 * The way out is that "sound 200 ms earlier than the picture" and "picture 200 ms
 * later than the sound" are the same outcome. Delaying the picture is always
 * possible, so every request for negative audio offset becomes a positive video
 * delay, and only positive audio offsets need the delay node.
 *
 *   d          manual audio offset, ms, positive = sound later than picture
 *   L          measured device output latency, ms, 0 when compensation is off
 *
 *   sound for media time t is heard at t + L + audioDelay
 *   picture for media time t is shown at t + videoDelay
 *   sync requires   t + L + audioDelay = t + videoDelay + d
 *   so              videoDelay = L + audioDelay - d,  both >= 0
 *
 * Choosing the smallest non-negative pair gives the answer below.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.SyncMath = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const MAX_VIDEO_DELAY_MS = 2000;

  /**
   * @param {object} options
   * @param {number} [options.deviceLatencyMs] measured output latency (0 if off)
   * @param {number} [options.manualMs] manual audio offset, + = sound later
   * @param {number} [options.maxVideoDelayMs] how far the picture may be delayed
   * @returns {{videoDelayMs:number, audioDelayMs:number, clamped:boolean}}
   */
  function computeSync({ deviceLatencyMs = 0, manualMs = 0, maxVideoDelayMs = MAX_VIDEO_DELAY_MS } = {}) {
    const latency = Math.max(0, Number(deviceLatencyMs) || 0);
    const manual = Number(manualMs) || 0;

    // Sound later than the picture by `manual`, on top of the device latency.
    const net = latency - manual;

    let videoDelayMs;
    let audioDelayMs;
    if (net >= 0) {
      videoDelayMs = net;
      audioDelayMs = 0;
    } else {
      videoDelayMs = 0;
      audioDelayMs = -net;
    }

    let clamped = false;
    if (videoDelayMs > maxVideoDelayMs) {
      videoDelayMs = maxVideoDelayMs;
      clamped = true;
    }
    if (audioDelayMs > maxVideoDelayMs) {
      audioDelayMs = maxVideoDelayMs;
      clamped = true;
    }

    return {
      videoDelayMs: Math.round(videoDelayMs),
      audioDelayMs: Math.round(audioDelayMs),
      clamped,
    };
  }

  /**
   * Bluetooth latency wanders as the link renegotiates, so a raw reading is too
   * jumpy to apply directly. Large changes (a device switch) are taken at once;
   * small ones are followed gradually so the picture never lurches.
   */
  function smoothLatency(previousMs, measuredMs, { jumpMs = 250, follow = 0.35 } = {}) {
    const previous = Math.max(0, Number(previousMs) || 0);
    const measured = Math.max(0, Number(measuredMs) || 0);
    if (!previous || Math.abs(measured - previous) > jumpMs) return measured;
    return Math.round(previous + (measured - previous) * follow);
  }

  return { computeSync, smoothLatency, MAX_VIDEO_DELAY_MS };
});
