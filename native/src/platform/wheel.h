// Wheel and touchpad input, as pure arithmetic so it can be tested without a window.
//
// Two different devices send SDL_MOUSEWHEEL:
//   * a mouse wheel sends whole notches (y = 1 per click);
//   * a precision touchpad sends hundreds of fractional deltas (y = 0.03, 0.07, ...),
//     and SDL rounds the integer fields to zero - so a build that reads only wheel.y
//     ignores two-finger scrolling completely.
// The accumulator turns either into a stream of scroll events: whole notches pass
// straight through, and fractions add up until there is enough to move the page.
#ifndef VLCFLIX_PLATFORM_WHEEL_H
#define VLCFLIX_PLATFORM_WHEEL_H

#include <algorithm>
#include <cmath>

namespace vf {

// How much of a notch must pile up before a touchpad swipe moves anything. Small enough
// that the first movement of a finger is felt, large enough that a swipe is a handful of
// redraws rather than a hundred.
inline constexpr float kWheelStep = 0.05f;

// Five deltas of 0.01 do not add up to exactly 0.05 in binary floating point, and
// trunc(0.04999999 / 0.05) is zero - the tenth of a notch would be held back forever.
// A tiny epsilon makes the threshold mean what it says.
inline constexpr double kWheelEpsilon = 1e-6;

struct WheelAccumulator {
  float x = 0.0f;
  float y = 0.0f;

  void reset() { x = 0.0f; y = 0.0f; }

  // Add one device delta. Returns true when the totals are worth emitting an event, and
  // writes the amount to scroll into *out_x / *out_y (the remainder is kept for next
  // time, so no motion is lost).
  bool push(float dx, float dy, float* out_x, float* out_y) {
    x += dx;
    y += dy;
    const float fx = static_cast<float>(
        std::trunc((static_cast<double>(x) + kWheelEpsilon) / kWheelStep) * kWheelStep);
    const float fy = static_cast<float>(
        std::trunc((static_cast<double>(y) + kWheelEpsilon) / kWheelStep) * kWheelStep);
    if (fx == 0.0f && fy == 0.0f) return false;
    x = static_cast<float>(x - fx);
    y = static_cast<float>(y - fy);
    if (out_x) *out_x = fx;
    if (out_y) *out_y = fy;
    return true;
  }
};

// Did SDL give us a fractional (touchpad) delta? -1, 0 or 1 is the integer field SDL
// guarantees; anything else, or a nonzero precise value, means a precision device.
inline float wheel_axis(float precise, float integer) {
  return precise != 0.0f ? precise : integer;
}

// A precision touchpad event carries FLIPPED direction only when the user chose
// "natural" scrolling; flip the numbers to the direction the user expects.
inline float wheel_direction(float value, bool flipped) { return flipped ? -value : value; }

// How far a finger has to travel to equal one mouse-wheel notch, when the driver
// delivers the gesture as two touch contacts instead of a wheel message. The UI
// treats one notch as about a hundred pixels of page, so this is a modest
// amplification: a short drag moves the page, a long one does not fling it.
inline constexpr float kTouchDragPixelsPerNotch = 48.0f;

// Two fingers dragging. dx/dy are window-normalized (SDL's touch events). One notch
// is `pixels_per_notch` of travel.
//
// `down_scrolls_up` is the Windows default ("Down motion scrolls up", registry
// ScrollDirection = 0): a finger moving down produces a positive vertical notch,
// which this app treats as scrolling toward the top. The other setting flips both
// axes, matching the one switch Windows exposes.
inline void touch_drag_to_wheel(float dx, float dy, float window_w, float window_h,
                                float pixels_per_notch, bool down_scrolls_up, float* out_x,
                                float* out_y) {
  const float step = pixels_per_notch < 1.0f ? 1.0f : pixels_per_notch;
  const float sign = down_scrolls_up ? 1.0f : -1.0f;
  // A contact that jumps (palm, driver glitch) must not fling the page.
  const float px = std::max(-240.0f, std::min(240.0f, dx * window_w));
  const float py = std::max(-240.0f, std::min(240.0f, dy * window_h));
  if (out_x) *out_x = sign * px / step;
  if (out_y) *out_y = sign * py / step;
}

}  // namespace vf

#endif  // VLCFLIX_PLATFORM_WHEEL_H
