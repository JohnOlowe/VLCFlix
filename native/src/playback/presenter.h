// The presenter: turns "a decoded frame arrived" into "this frame is on screen now".
//
// Why this exists at all. libVLC can delay the sound (libvlc_audio_set_delay), but the
// picture is ours to schedule. `compute_sync()` says how much of a correction must be
// paid for by the picture; this class pays it by holding frames for that long, so the
// two senses land together.
//
// With no correction (the default) there is no queue and no latency: the newest frame
// replaces the previous one, exactly as a normal player behaves.
#pragma once
#include <cstdint>
#include <vector>

#include "playback/engine.h"

namespace vf {

class Presenter {
 public:
  void configure(int video_delay_ms, double frame_rate_hint = 60.0);
  // Called with a frame the engine just delivered. `now` is seconds on the shared
  // clock, which keeps the presenter testable without sleeping.
  void push(Frame&& frame, double now);
  // Called every UI frame. Returns true when the picture on screen changed.
  bool advance(double now);
  const Frame& current() const { return current_; }
  bool has_frame() const { return has_frame_; }

  int delay_ms() const { return delay_ms_; }
  // How many frames the queue holds right now, and the depth the maths asked for.
  int queued() const { return static_cast<int>(queue_.size()); }
  int target_depth() const { return target_depth_; }
  long long shown() const { return shown_; }
  long long dropped() const { return dropped_; }
  long long held() const { return held_; }
  void clear();

 private:
  struct Slot {
    Frame frame;
    double due = 0;  // seconds on the shared clock
  };
  std::vector<Slot> queue_;
  Frame current_;
  bool has_frame_ = false;
  int delay_ms_ = 0;
  int target_depth_ = 1;
  long long shown_ = 0;
  long long dropped_ = 0;
  long long held_ = 0;
};

}  // namespace vf
