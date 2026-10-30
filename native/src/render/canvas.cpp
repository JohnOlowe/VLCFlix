#include "render/canvas.h"

#include <algorithm>
#include <cmath>

#include "render/image.h"

namespace vf {
namespace {

inline uint8_t clamp_byte(double v) {
  if (v <= 0) return 0;
  if (v >= 255) return 255;
  return static_cast<uint8_t>(v + 0.5);
}

inline double smooth(double edge0, double edge1, double x) {
  if (edge1 <= edge0) return x < edge0 ? 0 : 1;
  const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

}  // namespace

Color mix(Color a, Color b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  return Color{clamp_byte(a.r + (b.r - a.r) * t), clamp_byte(a.g + (b.g - a.g) * t),
               clamp_byte(a.b + (b.b - a.b) * t), clamp_byte(a.a + (b.a - a.a) * t)};
}

Color with_alpha(Color c, uint8_t alpha) {
  c.a = alpha;
  return c;
}

Color from_hsl(double hue_degrees, double saturation, double lightness) {
  // Deterministic fallback artwork for films with no stills: same hue for the same
  // title, every time, so a card does not change colour between runs.
  const double h = std::fmod(hue_degrees, 360.0) / 360.0;
  const double s = std::clamp(saturation, 0.0, 1.0);
  const double l = std::clamp(lightness, 0.0, 1.0);
  auto hue_to_rgb = [](double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 1.0 / 2) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
  };
  double r = l, g = l, b = l;
  if (s > 0) {
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    r = hue_to_rgb(p, q, h + 1.0 / 3);
    g = hue_to_rgb(p, q, h);
    b = hue_to_rgb(p, q, h - 1.0 / 3);
  }
  return Color{clamp_byte(r * 255), clamp_byte(g * 255), clamp_byte(b * 255), 255};
}

Canvas::Canvas(int width, int height)
    : width_(std::max(1, width)), height_(std::max(1, height)),
      pixels_(static_cast<std::size_t>(width_) * height_ * 4, 0) {}

void Canvas::blend_pixel(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  const Rect area = clip();
  if (!area.contains(x + 0.5f, y + 0.5f)) return;
  uint8_t* px = &pixels_[(static_cast<std::size_t>(y) * width_ + x) * 4];
  const double a = color.a / 255.0;
  if (a >= 1.0) {
    px[0] = color.r;
    px[1] = color.g;
    px[2] = color.b;
    px[3] = 255;
    return;
  }
  px[0] = clamp_byte(px[0] * (1 - a) + color.r * a);
  px[1] = clamp_byte(px[1] * (1 - a) + color.g * a);
  px[2] = clamp_byte(px[2] * (1 - a) + color.b * a);
  px[3] = 255;
}

void Canvas::blend_span(int x, int y, int length, Color color) {
  for (int i = 0; i < length; ++i) blend_pixel(x + i, y, color);
}

void Canvas::clear(Color color) {
  for (std::size_t i = 0; i < pixels_.size(); i += 4) {
    pixels_[i] = color.r;
    pixels_[i + 1] = color.g;
    pixels_[i + 2] = color.b;
    pixels_[i + 3] = 255;
  }
}

void Canvas::fill_rect(const Rect& rect, Color color) {
  if (rect.empty() || color.a == 0) return;
  const Rect clip_rect = clip();
  const int x0 = std::max(0, static_cast<int>(std::round(std::max(rect.x, clip_rect.x))));
  const int y0 = std::max(0, static_cast<int>(std::round(std::max(rect.y, clip_rect.y))));
  const int x1 = std::min(width_, static_cast<int>(std::round(std::min(rect.right(), clip_rect.right()))));
  const int y1 = std::min(height_, static_cast<int>(std::round(std::min(rect.bottom(), clip_rect.bottom()))));
  for (int y = y0; y < y1; ++y) {
    if (color.a == 255) {
      uint8_t* row = &pixels_[(static_cast<std::size_t>(y) * width_ + x0) * 4];
      for (int x = x0; x < x1; ++x) {
        row[0] = color.r;
        row[1] = color.g;
        row[2] = color.b;
        row[3] = 255;
        row += 4;
      }
    } else {
      blend_span(x0, y, x1 - x0, color);
    }
  }
}

void Canvas::fill_round_rect(const Rect& rect, float radius, Color color) {
  if (rect.empty() || color.a == 0) return;
  const float r = std::max(0.0f, std::min(radius, std::min(rect.w, rect.h) / 2));
  if (r < 0.5f) {
    fill_rect(rect, color);
    return;
  }
  const Rect clip_rect = clip();
  const int x0 = std::max(0, static_cast<int>(std::floor(std::max(rect.x, clip_rect.x))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::max(rect.y, clip_rect.y))));
  const int x1 = std::min(width_, static_cast<int>(std::ceil(std::min(rect.right(), clip_rect.right()))));
  const int y1 = std::min(height_, static_cast<int>(std::ceil(std::min(rect.bottom(), clip_rect.bottom()))));
  for (int y = y0; y < y1; ++y) {
    const float py = y + 0.5f;
    for (int x = x0; x < x1; ++x) {
      const float px = x + 0.5f;
      // Distance into the nearest corner circle; 0 outside, >1 inside.
      float dx = 0, dy = 0;
      if (px < rect.x + r) dx = rect.x + r - px;
      else if (px > rect.right() - r) dx = px - (rect.right() - r);
      if (py < rect.y + r) dy = rect.y + r - py;
      else if (py > rect.bottom() - r) dy = py - (rect.bottom() - r);
      const float distance = std::sqrt(dx * dx + dy * dy);
      const float coverage = 1.0f - smooth(r - 1.0f, r + 1.0f, distance);
      if (coverage <= 0.01f) continue;
      blend_pixel(x, y, with_alpha(color, static_cast<uint8_t>(color.a * coverage)));
    }
  }
}

void Canvas::stroke_round_rect(const Rect& rect, float radius, float thickness, Color color) {
  if (rect.empty() || color.a == 0) return;
  // One pass over the bounding box using the rounded-rect distance field. The
  // earlier edges-plus-arcs version left circular blobs at the corners whenever the
  // radius and the edge lengths disagreed, which showed up in the first screenshot.
  const float r = std::max(0.0f, std::min(radius, std::min(rect.w, rect.h) / 2));
  const Rect clip_rect = clip();
  const int x0 = std::max(0, static_cast<int>(std::floor(std::max(rect.x, clip_rect.x))) - 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(std::max(rect.y, clip_rect.y))) - 1);
  const int x1 = std::min(width_, static_cast<int>(std::ceil(std::min(rect.right(), clip_rect.right()))) + 1);
  const int y1 = std::min(height_, static_cast<int>(std::ceil(std::min(rect.bottom(), clip_rect.bottom()))) + 1);
  const float half_w = rect.w / 2;
  const float half_h = rect.h / 2;
  const float cx = rect.x + half_w;
  const float cy = rect.y + half_h;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const float px = x + 0.5f;
      const float py = y + 0.5f;
      const float qx = std::fabs(px - cx) - (half_w - r);
      const float qy = std::fabs(py - cy) - (half_h - r);
      const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                                      std::max(qy, 0.0f) * std::max(qy, 0.0f));
      const float signed_distance = outside + std::min(std::max(qx, qy), 0.0f) - r;
      // Ring of width `thickness` centred just inside the boundary.
      const float coverage = (1.0f - smooth(-thickness + 0.5f, 0.5f, signed_distance)) *
                             smooth(-0.5f, 0.5f, signed_distance + thickness * 0.0f + 0.0f);
      const float ring = (1.0f - smooth(-0.5f, 0.5f, signed_distance)) *
                         smooth(-thickness - 0.5f, -thickness + 0.5f, signed_distance);
      (void)coverage;
      if (ring > 0.02f) {
        blend_pixel(x, y, with_alpha(color, static_cast<uint8_t>(color.a * ring)));
      }
    }
  }
}

void Canvas::fill_gradient_v(const Rect& rect, Color top, Color bottom) {
  if (rect.empty()) return;
  const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
  const int y1 = std::min(height_, static_cast<int>(std::ceil(rect.bottom())));
  for (int y = y0; y < y1; ++y) {
    const double t = rect.h > 0 ? (y - rect.y) / rect.h : 0;
    const Color color = mix(top, bottom, t);
    fill_rect(Rect{rect.x, float(y), rect.w, 1}, color);
  }
}

void Canvas::fill_gradient_h(const Rect& rect, Color left, Color right) {
  if (rect.empty()) return;
  const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
  const int x1 = std::min(width_, static_cast<int>(std::ceil(rect.right())));
  for (int x = x0; x < x1; ++x) {
    const double t = rect.w > 0 ? (x - rect.x) / rect.w : 0;
    fill_rect(Rect{float(x), rect.y, 1, rect.h}, mix(left, right, t));
  }
}

void Canvas::fill_circle(float cx, float cy, float radius, Color color) {
  const int x0 = static_cast<int>(cx - radius) - 1;
  const int y0 = static_cast<int>(cy - radius) - 1;
  const int size = static_cast<int>(radius * 2) + 3;
  for (int y = y0; y < y0 + size; ++y) {
    for (int x = x0; x < x0 + size; ++x) {
      const float dx = x + 0.5f - cx;
      const float dy = y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float coverage = 1.0f - smooth(radius - 1.0f, radius + 1.0f, d);
      if (coverage > 0.02f) {
        blend_pixel(x, y, with_alpha(color, static_cast<uint8_t>(color.a * coverage)));
      }
    }
  }
}

void Canvas::fill_triangle_right(const Rect& area, Color color) {
  const int rows = std::max(1, static_cast<int>(area.h));
  for (int i = 0; i < rows; ++i) {
    const double t = rows > 1 ? static_cast<double>(i) / (rows - 1) : 0.0;
    const double half = (1.0 - std::fabs(2.0 * t - 1.0)) * area.w;
    fill_rect(Rect{area.x, area.y + i, static_cast<float>(half), 1}, color);
  }
}

void Canvas::stroke_circle(float cx, float cy, float radius, float thickness, Color color) {
  const float inner = std::max(0.0f, radius - thickness);
  const int x0 = static_cast<int>(cx - radius) - 1;
  const int y0 = static_cast<int>(cy - radius) - 1;
  const int size = static_cast<int>(radius * 2) + 3;
  for (int y = y0; y < y0 + size; ++y) {
    for (int x = x0; x < x0 + size; ++x) {
      const float dx = x + 0.5f - cx;
      const float dy = y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float coverage = smooth(inner - 0.8f, inner + 0.8f, d) *
                             (1.0f - smooth(radius - 0.8f, radius + 0.8f, d));
      if (coverage > 0.02f) {
        blend_pixel(x, y, with_alpha(color, static_cast<uint8_t>(color.a * coverage)));
      }
    }
  }
}

void Canvas::blit_raw(const uint8_t* pixels, int src_width, int src_height, const Rect& dst,
                      bool cover) {
  if (!pixels || src_width <= 0 || src_height <= 0 || dst.empty()) return;
  const Rect clip_rect = clip();
  const int x0 = std::max(0, static_cast<int>(std::floor(std::max(dst.x, clip_rect.x))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::max(dst.y, clip_rect.y))));
  const int x1 = std::min(width_, static_cast<int>(std::ceil(std::min(dst.right(), clip_rect.right()))));
  const int y1 = std::min(height_, static_cast<int>(std::ceil(std::min(dst.bottom(), clip_rect.bottom()))));
  if (x0 >= x1 || y0 >= y1) return;
  // Cover crops the source instead of stretching it, which is what a video needs when
  // the window aspect and the film aspect disagree.
  double sx0 = 0, sy0 = 0, sw = src_width, sh = src_height;
  if (cover) {
    const double src_aspect = static_cast<double>(src_width) / src_height;
    const double dst_aspect = dst.w / dst.h;
    if (src_aspect > dst_aspect) {
      sw = src_height * dst_aspect;
      sx0 = (src_width - sw) / 2;
    } else {
      sh = src_width / dst_aspect;
      sy0 = (src_height - sh) / 2;
    }
  }
  // libVLC's "RV32" is native-endian RGB32. On this machine that is B,G,R,X in
  // memory. The canvas is R,G,B,A. Reading the bytes in order paints red as blue,
  // which is why skin came out blue. The fourth byte is padding, not alpha.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  constexpr int kR = 0, kG = 1, kB = 2;
#else
  constexpr int kR = 2, kG = 1, kB = 0;
#endif
  const double x_scale = dst.w > 0 ? sw / dst.w : 0;
  const double y_scale = dst.h > 0 ? sh / dst.h : 0;
  for (int y = y0; y < y1; ++y) {
    const double sy = sy0 + (y + 0.5 - dst.y) * y_scale - 0.5;
    const int iy0 = static_cast<int>(std::floor(sy));
    const int fy = std::clamp(static_cast<int>((sy - iy0) * 256.0), 0, 256);
    const int ay = std::clamp(iy0, 0, src_height - 1);
    const int by = std::clamp(iy0 + 1, 0, src_height - 1);
    uint8_t* row = &pixels_[(static_cast<std::size_t>(y) * width_ + x0) * 4];
    for (int x = x0; x < x1; ++x, row += 4) {
      const double sx = sx0 + (x + 0.5 - dst.x) * x_scale - 0.5;
      const int ix0 = static_cast<int>(std::floor(sx));
      const int fx = std::clamp(static_cast<int>((sx - ix0) * 256.0), 0, 256);
      const int ax = std::clamp(ix0, 0, src_width - 1);
      const int bx = std::clamp(ix0 + 1, 0, src_width - 1);
      const uint8_t* p00 = pixels + (static_cast<std::size_t>(ay) * src_width + ax) * 4;
      const uint8_t* p10 = pixels + (static_cast<std::size_t>(ay) * src_width + bx) * 4;
      const uint8_t* p01 = pixels + (static_cast<std::size_t>(by) * src_width + ax) * 4;
      const uint8_t* p11 = pixels + (static_cast<std::size_t>(by) * src_width + bx) * 4;
      auto mix = [&](int channel) {
        const int top = (p00[channel] * (256 - fx) + p10[channel] * fx) >> 8;
        const int bottom = (p01[channel] * (256 - fx) + p11[channel] * fx) >> 8;
        return static_cast<uint8_t>((top * (256 - fy) + bottom * fy) >> 8);
      };
      // Opaque write. blend_pixel's per-pixel clip test was most of the cost of a
      // frame, and a film has no alpha to blend.
      row[0] = mix(kR);
      row[1] = mix(kG);
      row[2] = mix(kB);
      row[3] = 255;
    }
  }
}

void Canvas::blit_src(const Image& image, const Rect& src, const Rect& dst) {
  if (!image.valid() || src.empty() || dst.empty()) return;
  const Rect clip_rect = clip();
  const int x0 = std::max(0, static_cast<int>(std::floor(std::max(dst.x, clip_rect.x))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::max(dst.y, clip_rect.y))));
  const int x1 = std::min(width_, static_cast<int>(std::ceil(std::min(dst.right(), clip_rect.right()))));
  const int y1 = std::min(height_, static_cast<int>(std::ceil(std::min(dst.bottom(), clip_rect.bottom()))));
  for (int y = y0; y < y1; ++y) {
    const double v = dst.h > 0 ? (y + 0.5 - dst.y) / dst.h : 0;
    const double sy = src.y + v * src.h - 0.5;
    const int iy0 = static_cast<int>(std::floor(sy));
    const double fy = sy - iy0;
    for (int x = x0; x < x1; ++x) {
      const double u = dst.w > 0 ? (x + 0.5 - dst.x) / dst.w : 0;
      const double sx = src.x + u * src.w - 0.5;
      const int ix0 = static_cast<int>(std::floor(sx));
      const double fx = sx - ix0;
      // Bilinear sample, clamped to the source.
      const int ax = std::clamp(ix0, 0, image.width - 1);
      const int bx = std::clamp(ix0 + 1, 0, image.width - 1);
      const int ay = std::clamp(iy0, 0, image.height - 1);
      const int by = std::clamp(iy0 + 1, 0, image.height - 1);
      const uint8_t* p00 = &image.pixels[(static_cast<std::size_t>(ay) * image.width + ax) * 4];
      const uint8_t* p10 = &image.pixels[(static_cast<std::size_t>(ay) * image.width + bx) * 4];
      const uint8_t* p01 = &image.pixels[(static_cast<std::size_t>(by) * image.width + ax) * 4];
      const uint8_t* p11 = &image.pixels[(static_cast<std::size_t>(by) * image.width + bx) * 4];
      Color sample;
      for (int channel = 0; channel < 4; ++channel) {
        const double top = p00[channel] + (p10[channel] - p00[channel]) * fx;
        const double bottom = p01[channel] + (p11[channel] - p01[channel]) * fx;
        reinterpret_cast<uint8_t*>(&sample)[channel] = clamp_byte(top + (bottom - top) * fy);
      }
      blend_pixel(x, y, sample);
    }
  }
}

void Canvas::blit(const Image& image, const Rect& dst, bool cover) {
  if (!image.valid() || dst.empty()) return;
  if (!cover) {
    blit_src(image, Rect{0, 0, float(image.width), float(image.height)}, dst);
    return;
  }
  // Cover: scale to fill, crop the overflow, keep the aspect ratio.
  const double src_aspect = static_cast<double>(image.width) / image.height;
  const double dst_aspect = dst.w / dst.h;
  Rect src{0, 0, float(image.width), float(image.height)};
  if (src_aspect > dst_aspect) {
    src.w = static_cast<float>(image.height * dst_aspect);
    src.x = (image.width - src.w) / 2;
  } else {
    src.h = static_cast<float>(image.width / dst_aspect);
    src.y = (image.height - src.h) / 2;
  }
  blit_src(image, src, dst);
}

void Canvas::blit_rounded(const Image& image, const Rect& dst, float radius, bool cover) {
  if (!image.valid() || dst.empty()) return;
  Rect src{0, 0, float(image.width), float(image.height)};
  if (cover) {
    const double src_aspect = static_cast<double>(image.width) / image.height;
    const double dst_aspect = dst.w / dst.h;
    if (src_aspect > dst_aspect) {
      src.w = static_cast<float>(image.height * dst_aspect);
      src.x = (image.width - src.w) / 2;
    } else {
      src.h = static_cast<float>(image.width / dst_aspect);
      src.y = (image.height - src.h) / 2;
    }
  }
  const float r = std::max(0.0f, std::min(radius, std::min(dst.w, dst.h) / 2));
  const Rect clip_rect = clip();
  const int x0 = std::max(0, static_cast<int>(std::floor(std::max(dst.x, clip_rect.x))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::max(dst.y, clip_rect.y))));
  const int x1 = std::min(width_, static_cast<int>(std::ceil(std::min(dst.right(), clip_rect.right()))));
  const int y1 = std::min(height_, static_cast<int>(std::ceil(std::min(dst.bottom(), clip_rect.bottom()))));
  for (int y = y0; y < y1; ++y) {
    const double v = dst.h > 0 ? (y + 0.5 - dst.y) / dst.h : 0;
    const double sy = src.y + v * src.h - 0.5;
    const int iy0 = static_cast<int>(std::floor(sy));
    const double fy = sy - iy0;
    for (int x = x0; x < x1; ++x) {
      float coverage = 1.0f;
      if (r > 0.5f) {
        const float px = x + 0.5f;
        const float py = y + 0.5f;
        float dx = 0, dy = 0;
        if (px < dst.x + r) dx = dst.x + r - px;
        else if (px > dst.right() - r) dx = px - (dst.right() - r);
        if (py < dst.y + r) dy = dst.y + r - py;
        else if (py > dst.bottom() - r) dy = py - (dst.bottom() - r);
        const float distance = std::sqrt(dx * dx + dy * dy);
        coverage = 1.0f - smooth(r - 1.0f, r + 1.0f, distance);
        if (coverage <= 0.01f) continue;
      }
      const double u = dst.w > 0 ? (x + 0.5 - dst.x) / dst.w : 0;
      const double sx = src.x + u * src.w - 0.5;
      const int ix0 = static_cast<int>(std::floor(sx));
      const double fx = sx - ix0;
      const int ax = std::clamp(ix0, 0, image.width - 1);
      const int bx = std::clamp(ix0 + 1, 0, image.width - 1);
      const int ay = std::clamp(iy0, 0, image.height - 1);
      const int by = std::clamp(iy0 + 1, 0, image.height - 1);
      const uint8_t* p00 = &image.pixels[(static_cast<std::size_t>(ay) * image.width + ax) * 4];
      const uint8_t* p10 = &image.pixels[(static_cast<std::size_t>(ay) * image.width + bx) * 4];
      const uint8_t* p01 = &image.pixels[(static_cast<std::size_t>(by) * image.width + ax) * 4];
      const uint8_t* p11 = &image.pixels[(static_cast<std::size_t>(by) * image.width + bx) * 4];
      Color sample;
      for (int channel = 0; channel < 4; ++channel) {
        const double top = p00[channel] + (p10[channel] - p00[channel]) * fx;
        const double bottom = p01[channel] + (p11[channel] - p01[channel]) * fx;
        reinterpret_cast<uint8_t*>(&sample)[channel] = clamp_byte(top + (bottom - top) * fy);
      }
      sample.a = static_cast<uint8_t>(std::min(255.0f, sample.a * coverage));
      blend_pixel(x, y, sample);
    }
  }
}

void Canvas::blend_bitmap(int x, int y, int w, int h, const uint8_t* coverage, int stride,
                          Color color) {
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      const uint8_t alpha = coverage[row * stride + col];
      if (!alpha) continue;
      blend_pixel(x + col, y + row,
                  with_alpha(color, static_cast<uint8_t>(color.a * alpha / 255)));
    }
  }
}

void Canvas::push_clip(const Rect& rect) {
  Rect next = rect;
  if (!clip_stack_.empty()) {
    const Rect& current = clip_stack_.back();
    const float x0 = std::max(current.x, rect.x);
    const float y0 = std::max(current.y, rect.y);
    const float x1 = std::min(current.right(), rect.right());
    const float y1 = std::min(current.bottom(), rect.bottom());
    next = Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
  }
  clip_stack_.push_back(next);
}

void Canvas::pop_clip() {
  if (!clip_stack_.empty()) clip_stack_.pop_back();
}

}  // namespace vf
