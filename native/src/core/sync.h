// Playback sync arithmetic — ported from public/js/sync-math.js, same maths.
//
// The problem: sound can be delayed but never played earlier than the media
// timeline. "Sound 200 ms earlier than the picture" and "picture 200 ms later than
// the sound" are the same outcome, and delaying the picture is always possible, so
// every negative audio offset becomes a positive video delay.
//
//   d   manual audio offset, ms, + = sound later than the picture
//   L   measured device output latency, ms (0 when compensation is off)
//
//   sound for media time t is heard at  t + L + audioDelay
//   picture for media time t is shown at t + videoDelay
//   sync requires  t + L + audioDelay = t + videoDelay + d
//   so             videoDelay = L + audioDelay - d, both >= 0
//
// The smallest non-negative pair is the answer.
#pragma once
#include <string>

namespace vf {

constexpr int kMaxVideoDelayMs = 2000;

struct SyncPlan {
  int video_delay_ms = 0;
  int audio_delay_ms = 0;
  bool clamped = false;

  bool operator==(const SyncPlan& o) const {
    return video_delay_ms == o.video_delay_ms && audio_delay_ms == o.audio_delay_ms &&
           clamped == o.clamped;
  }
};

SyncPlan compute_sync(int device_latency_ms, int manual_ms, int max_video_delay_ms = kMaxVideoDelayMs);

// Bluetooth latency wanders; small changes are followed gradually so the picture
// never lurches, large ones (a different device) are applied at once.
int smooth_latency(int previous_ms, int measured_ms, int jump_ms = 250, double follow = 0.35);

// "+300 ms" / "−300 ms" / "0 ms" for the UI and the log.
std::string describe_delay(int ms);

}  // namespace vf
