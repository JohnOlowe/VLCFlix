// The only file that knows SDL exists.
//
// Everything above it draws into a Canvas, which is why the UI can be rendered to a
// PNG with no display at all. This class is the thin part: a window, a streaming
// texture, and event translation.
#pragma once
#include <string>

#include "render/canvas.h"
#include "ui/app.h"

namespace vf {

class Window {
 public:
  ~Window();

  bool open(int width, int height, const std::string& title);
  void close();
  void present(const Canvas& canvas);
  // Waits up to `timeout_ms` for input. Returns false only when the window asked to
  // close; a timeout with no event returns true and leaves `event` untouched.
  bool poll(ui::InputEvent* event, int timeout_ms = 16);
  bool valid() const { return window_ != nullptr; }
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  void* window_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace vf
