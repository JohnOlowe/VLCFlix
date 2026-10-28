// The only file that knows SDL exists.
//
// Everything above it draws into a Canvas, which is why the UI can be rendered to a
// PNG with no display at all. This class is the thin part: a window, a streaming
// texture, and event translation.
#pragma once
#include <string>

#include "platform/wheel.h"
#include "render/canvas.h"
#include "ui/app.h"

namespace vf {

// A last-resort notice for the user. The Windows build is a GUI-subsystem program, so
// when it is started from the desktop shortcut there is no console to print to: a
// failure would look exactly like nothing happening at all. This is the one thing the
// platform layer does for the rest of the program that is not drawing.
void show_error_box(const std::string& title, const std::string& text);

class Window {
 public:
  ~Window();

  bool open(int width, int height, const std::string& title);
  void close();
  void present(const Canvas& canvas);
  // The SDL renderer actually in use ("software", "direct3d11", ...). Empty before open.
  const std::string& renderer_name() const;
  // Called from the event loop every frame; cheap and idempotent.
  void set_cursor(PointerCursor kind);
  // Waits up to `timeout_ms` for input. Returns false only when the window asked to
  // close; a timeout with no event returns true and leaves `event` untouched.
  bool poll(ui::InputEvent* event, int timeout_ms = 16);
  bool valid() const { return window_ != nullptr; }
  // Fullscreen is the window's business, not the UI's.
  void toggle_fullscreen();
  bool fullscreen() const { return fullscreen_; }
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  void close_cursors();
  struct Impl;
  Impl* impl_ = nullptr;
  // One shared cursor per shape: creating them per frame leaks handles on Windows.
  void* cursors_[8] = {};
  void* window_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  // Last known pointer position. The wheel event carries its own position only from
  // SDL 2.26, and the app needs it to scroll the rail under the pointer rather than
  // always the page.
  float last_mouse_x_ = 0;
  float last_mouse_y_ = 0;
  bool fullscreen_ = false;
  // Fractional wheel input from a precision touchpad, waiting to add up to a scroll.
  WheelAccumulator wheel_;
};

}  // namespace vf
