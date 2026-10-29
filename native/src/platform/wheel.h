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

// What an Elantech-style scrollbar message is worth, in mouse-wheel notches.
//
// Lenovo ELAN drivers do not send WM_MOUSEWHEEL for "drag two fingers to scroll".
// They send WM_VSCROLL / WM_HSCROLL, the messages a window with a standard scrollbar
// would get. Chrome turns those into a wheel. An SDL window has no scrollbar, so it
// drops them, and a physical mouse wheel — which really is WM_MOUSEWHEEL — still works.
//
// msg/wparam/lparam are the Win32 values (WM_VSCROLL is 0x0115, WM_HSCROLL is 0x0114).
// A child scrollbar control sets lparam to its HWND; the touchpad driver sets it to 0,
// or to the window the gesture is aimed at. `owner` is that window, so a real control
// is left alone.
// `lines_per_notch` is SPI_GETWHEELSCROLLLINES (3 unless the user changed it): one
// SB_LINE is one line, and one notch is that many lines. `page_notches` is one screen.
struct LegacyScroll {
  bool from_driver = false;
  float x = 0.0f;
  float y = 0.0f;
};

inline LegacyScroll legacy_scroll_message(unsigned msg, unsigned wparam, long long lparam,
                                          long long owner, float lines_per_notch,
                                          float page_notches) {
  LegacyScroll out;
  if (msg != 0x0114u && msg != 0x0115u) return out;
  if (lparam != 0 && lparam != owner) return out;
  out.from_driver = true;
  const unsigned request = wparam & 0xFFFFu;
  const float lines = lines_per_notch < 1.0f ? 1.0f : lines_per_notch;
  const float page = page_notches < 1.0f ? 1.0f : page_notches;
  // SB_LINEUP / SB_LINELEFT are 0. Positive notches scroll toward the start, which is
  // what this app does with a positive wheel (the mouse-wheel tests rely on that).
  float sign = 0.0f;
  float mag = 0.0f;
  switch (request) {
    case 0: sign = 1.0f; mag = 1.0f / lines; break;   // line up / left
    case 1: sign = -1.0f; mag = 1.0f / lines; break;  // line down / right
    case 2: sign = 1.0f; mag = page; break;           // page up / left
    case 3: sign = -1.0f; mag = page; break;          // page down / right
    case 6: sign = 1.0f; mag = page * 8.0f; break;    // top / left end
    case 7: sign = -1.0f; mag = page * 8.0f; break;   // bottom / right end
    default: return out;  // thumb-track and end-scroll are not this gesture
  }
  if (msg == 0x0115u) out.y = sign * mag;
  else out.x = sign * mag;
  return out;
}

}  // namespace vf

#endif  // VLCFLIX_PLATFORM_WHEEL_H
