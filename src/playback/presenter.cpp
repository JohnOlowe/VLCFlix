#include "playback/presenter.h"

#include <algorithm>

#include "core/log.h"

namespace vf {

void Presenter::configure(int video_delay_ms, double frame_rate_hint) {
  delay_ms_ = std::max(0, video_delay_ms);
  // One slot per frame interval that the delay covers, plus one to hold the frame
  // while it waits. With no delay this is 1, which is the no-latency path.
  const double frame_ms = frame_rate_hint > 1 ? 1000.0 / frame_rate_hint : 1000.0 / 60;
  target_depth_ = delay_ms_ == 0 ? 1 : std::max(2, static_cast<int>(delay_ms_ / frame_ms) + 2);
  // A hard cap: memory is the reason the delay is bounded, and a silent 2-second hold
  // at 1080p would be half a gigabyte.
  const int kMaxDepth = 90;
  if (target_depth_ > kMaxDepth) target_depth_ = kMaxDepth;
  queue_.clear();
}

void Presenter::push(Frame&& frame, double now) {
  Slot slot;
  slot.frame = std::move(frame);
  slot.due = now + delay_ms_ / 1000.0;
  queue_.push_back(std::move(slot));
  // The queue is bounded by the delay it is paying for. Anything beyond that is a
  // stall (a seek, a slow disk), not a reason to grow without limit.
  while (static_cast<int>(queue_.size()) > target_depth_) {
    queue_.erase(queue_.begin());
    ++dropped_;
  }
}

bool Presenter::advance(double now) {
  if (queue_.empty()) return false;
  // Present the newest frame whose holding time has elapsed. With no delay the first
  // entry is always due, so this is "show the newest frame now".
  int chosen = -1;
  for (int i = static_cast<int>(queue_.size()) - 1; i >= 0; --i) {
    if (queue_[static_cast<std::size_t>(i)].due <= now) {
      chosen = i;
      break;
    }
  }
  if (chosen < 0) {
    ++held_;
    return false;
  }
  if (chosen > 0) {
    dropped_ += chosen;  // older frames we passed over
  }
  current_ = std::move(queue_[static_cast<std::size_t>(chosen)].frame);
  queue_.erase(queue_.begin(), queue_.begin() + chosen + 1);
  has_frame_ = true;
  ++shown_;
  return true;
}

void Presenter::clear() {
  queue_.clear();
  current_ = Frame();
  has_frame_ = false;
  shown_ = 0;
  dropped_ = 0;
  held_ = 0;
}

}  // namespace vf
