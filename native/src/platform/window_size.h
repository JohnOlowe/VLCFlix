// How big the window is allowed to be.
//
// Kept in its own SDL-free header so the rule can be tested on a machine with no
// display: the first Windows run opened a 1600x900 window on a smaller screen, which
// put the title bar - and therefore minimise, maximise and close - outside the
// desktop, leaving a window that could not be moved, resized or dismissed.
#pragma once

namespace vf {

struct WindowSize {
  int width = 0;
  int height = 0;
};

// Shrinks a requested size until it fits the usable desktop, and never goes below a
// size the layout can still draw (640x420 is what the UI tests use for the smallest
// screen). The desktop is not filled edge to edge: the frame, shadows and taskbar need
// room, so 96% of the width and 90% of the height are the ceilings.
inline WindowSize fit_window_to_display(int wanted_width, int wanted_height, int display_width,
                                        int display_height) {
  constexpr int kMinWidth = 640;
  constexpr int kMinHeight = 420;

  WindowSize size{wanted_width, wanted_height};
  if (display_width <= 0 || display_height <= 0) return size;  // unknown: ask for what we want

  const int limit_width = display_width * 96 / 100;
  const int limit_height = display_height * 90 / 100;

  if (size.width > limit_width) size.width = limit_width;
  if (size.height > limit_height) size.height = limit_height;

  // A screen smaller than the minimum still gets a window that fits it, rather than one
  // that hangs off the edge.
  if (size.width < kMinWidth) size.width = limit_width < kMinWidth ? limit_width : kMinWidth;
  if (size.height < kMinHeight) size.height = limit_height < kMinHeight ? limit_height : kMinHeight;
  return size;
}

}  // namespace vf
