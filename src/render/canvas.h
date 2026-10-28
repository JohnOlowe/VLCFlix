// A software canvas: RGBA8, no window, no GPU.
//
// Everything the app draws goes through here, which is what makes the UI
// verifiable without a display: a screen is a function of state, and the result
// can be written to a PNG and looked at (see ui::App::render_to_png).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vf {

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
};

constexpr Color rgb(uint32_t hex) {
  return Color{static_cast<uint8_t>((hex >> 16) & 0xFF), static_cast<uint8_t>((hex >> 8) & 0xFF),
               static_cast<uint8_t>(hex & 0xFF), 255};
}
constexpr Color rgba(uint32_t hex, uint8_t alpha) {
  return Color{static_cast<uint8_t>((hex >> 16) & 0xFF), static_cast<uint8_t>((hex >> 8) & 0xFF),
               static_cast<uint8_t>(hex & 0xFF), alpha};
}
Color mix(Color a, Color b, double t);
Color with_alpha(Color c, uint8_t alpha);
Color from_hsl(double hue_degrees, double saturation, double lightness);

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(float px, float py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
  Rect inset(float amount) const { return Rect{x + amount, y + amount, w - 2 * amount, h - 2 * amount}; }
  float right() const { return x + w; }
  float bottom() const { return y + h; }
};

struct Image;

class Canvas {
 public:
  Canvas(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<uint8_t>& pixels() const { return pixels_; }
  std::vector<uint8_t>& pixels() { return pixels_; }

  void clear(Color color);
  void fill_rect(const Rect& rect, Color color);
  void fill_round_rect(const Rect& rect, float radius, Color color);
  void stroke_round_rect(const Rect& rect, float radius, float thickness, Color color);
  void fill_gradient_v(const Rect& rect, Color top, Color bottom);
  // A horizontal scrim: transparent at the left, `color` at the right. Used to make
  // white text legible over an arbitrary film still.
  void fill_gradient_h(const Rect& rect, Color left, Color right);
  void fill_circle(float cx, float cy, float radius, Color color);
  // Filled right-pointing triangle, for play buttons.
  void fill_triangle_right(const Rect& area, Color color);
  void stroke_circle(float cx, float cy, float radius, float thickness, Color color);

  // Draws `image` into `dst`. `cover` crops the source to fill dst without
  // distorting it, which is what posters and backdrops need.
  void blit(const Image& image, const Rect& dst, bool cover);
  // Same as blit() but for a raw RV32 buffer, which is what libVLC hands us. Keeping
  // it here means the player never copies a frame just to satisfy a type.
  void blit_raw(const uint8_t* pixels, int src_width, int src_height, const Rect& dst,
                bool cover);
  void blit_src(const Image& image, const Rect& src, const Rect& dst);
  // Same as blit(), but with rounded corners: the corners are anti-aliased against
  // whatever is already on the canvas, so a card sits on the background cleanly.
  void blit_rounded(const Image& image, const Rect& dst, float radius, bool cover);

  // 8-bit coverage bitmap (glyphs) tinted with `color`.
  void blend_bitmap(int x, int y, int w, int h, const uint8_t* coverage, int stride, Color color);

  void push_clip(const Rect& rect);
  void pop_clip();
  Rect clip() const { return clip_stack_.empty() ? Rect{0, 0, float(width_), float(height_)} : clip_stack_.back(); }

 private:
  void blend_pixel(int x, int y, Color color);
  void blend_span(int x, int y, int length, Color color);

  int width_;
  int height_;
  std::vector<uint8_t> pixels_;  // RGBA, row-major
  std::vector<Rect> clip_stack_;
};

// Writes a PNG. Returns false and says why in the log on failure.
bool write_png(const std::string& path, const Canvas& canvas);

}  // namespace vf
