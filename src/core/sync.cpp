#include "core/sync.h"

#include <algorithm>
#include <cmath>

namespace vf {

SyncPlan compute_sync(int device_latency_ms, int manual_ms, int max_video_delay_ms) {
  const int latency = std::max(0, device_latency_ms);
  const int net = latency - manual_ms;  // sound later than the picture by `manual`

  SyncPlan plan;
  if (net >= 0) {
    plan.video_delay_ms = net;
    plan.audio_delay_ms = 0;
  } else {
    plan.video_delay_ms = 0;
    plan.audio_delay_ms = -net;
  }
  if (plan.video_delay_ms > max_video_delay_ms) {
    plan.video_delay_ms = max_video_delay_ms;
    plan.clamped = true;
  }
  if (plan.audio_delay_ms > max_video_delay_ms) {
    plan.audio_delay_ms = max_video_delay_ms;
    plan.clamped = true;
  }
  return plan;
}

int smooth_latency(int previous_ms, int measured_ms, int jump_ms, double follow) {
  const int previous = std::max(0, previous_ms);
  const int measured = std::max(0, measured_ms);
  if (previous == 0 || std::abs(measured - previous) > jump_ms) return measured;
  const double next = previous + (measured - previous) * follow;
  return static_cast<int>(std::lround(next));
}

std::string describe_delay(int ms) {
  if (ms == 0) return "0 ms";
  // The minus sign is U+2212 so the value reads like the rest of the UI.
  return (ms > 0 ? "+" : "\xE2\x88\x92") + std::to_string(std::abs(ms)) + " ms";
}

}  // namespace vf
