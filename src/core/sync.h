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
