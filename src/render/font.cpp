#include "render/font.h"

#include <algorithm>
#include <cmath>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "core/log.h"
#include "core/paths.h"

namespace vf {

std::vector<char32_t> utf8_decode(const std::string& text) {
  std::vector<char32_t> out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    char32_t code = 0xFFFD;
    std::size_t extra = 0;
    if (c < 0x80) {
      code = c;
    } else if ((c & 0xE0) == 0xC0) {
      code = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      code = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      code = c & 0x07;
      extra = 3;
    } else {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }
    if (i + extra >= text.size()) {
      out.push_back(0xFFFD);
      break;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      const unsigned char cc = static_cast<unsigned char>(text[i + k]);
      if ((cc & 0xC0) != 0x80) {
        code = 0xFFFD;
        break;
      }
      code = (code << 6) | (cc & 0x3F);
    }
    out.push_back(code);
    i += extra + 1;
  }
  return out;
}

struct Font::Impl {
  FT_Library library = nullptr;
  FT_Face face = nullptr;
};

namespace {

const char* candidates_regular[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    nullptr};

const char* candidates_bold[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "C:\\Windows\\Fonts\\segoeuib.ttf",
    "C:\\Windows\\Fonts\\arialbd.ttf",
    nullptr};

}  // namespace

Font::Font() : impl_(new Impl()) {}

Font::~Font() {
  if (impl_) {
    if (impl_->face) FT_Done_Face(impl_->face);
    if (impl_->library) FT_Done_FreeType(impl_->library);
  }
}

bool Font::load_file(const std::string& path) {
  if (!impl_->library && FT_Init_FreeType(&impl_->library) != 0) {
    LOG_ERROR("FreeType could not be initialised: there will be no text");
    return false;
  }
  if (!file_exists(path)) return false;
  if (FT_New_Face(impl_->library, path.c_str(), 0, &impl_->face) != 0) {
    LOG_WARN("FreeType could not open %s", path.c_str());
    impl_->face = nullptr;
    return false;
  }
  valid_ = true;
  path_ = path;
  cache_.clear();
  return true;
}

bool Font::load_default(bool bold) {
  const char** list = bold ? candidates_bold : candidates_regular;
  for (int i = 0; list[i]; ++i) {
    if (load_file(list[i])) return true;
  }
  if (bold) {
    LOG_WARN("no bold face found; using the regular one");
    return load_default(false);
  }
  LOG_ERROR("no usable font found; the UI will draw no text");
  return false;
}

const Font::Glyph* Font::glyph(char32_t codepoint, float size) const {
  if (!valid_ || !impl_->face) return nullptr;
  const int pixel_size = std::max(4, static_cast<int>(std::lround(size)));
  const long long key = (static_cast<long long>(pixel_size) << 24) | (codepoint & 0x1FFFFF);
  auto it = cache_.find(key);
  if (it != cache_.end()) return &it->second;

  if (FT_Set_Pixel_Sizes(impl_->face, 0, static_cast<FT_UInt>(pixel_size)) != 0) return nullptr;
  if (FT_Load_Char(impl_->face, codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
    return nullptr;
  }
  const FT_GlyphSlot slot = impl_->face->glyph;
  Glyph glyph;
  glyph.width = static_cast<int>(slot->bitmap.width);
  glyph.height = static_cast<int>(slot->bitmap.rows);
  glyph.bearing_x = slot->bitmap_left;
  glyph.bearing_y = slot->bitmap_top;
  glyph.advance = static_cast<float>(slot->advance.x) / 64.0f;
  glyph.coverage.assign(static_cast<std::size_t>(std::max(0, glyph.width)) *
                            std::max(0, glyph.height),
                        0);
  for (int y = 0; y < glyph.height; ++y) {
    for (int x = 0; x < glyph.width; ++x) {
      glyph.coverage[static_cast<std::size_t>(y) * glyph.width + x] =
          slot->bitmap.buffer[y * slot->bitmap.pitch + x];
    }
  }
  auto inserted = cache_.emplace(key, std::move(glyph));
  return &inserted.first->second;
}

TextMetrics Font::measure(const std::string& text, float size) const {
  TextMetrics metrics;
  if (!valid_ || !impl_->face) {
    metrics.height = size;
    return metrics;
  }
  FT_Set_Pixel_Sizes(impl_->face, 0, static_cast<FT_UInt>(std::max(4, static_cast<int>(size))));
  metrics.ascent = static_cast<float>(impl_->face->size->metrics.ascender) / 64.0f;
  metrics.descent = static_cast<float>(-impl_->face->size->metrics.descender) / 64.0f;
  metrics.height = metrics.ascent + metrics.descent;
  for (char32_t code : utf8_decode(text)) {
    const Glyph* g = glyph(code, size);
    metrics.width += g ? g->advance : size * 0.5f;
  }
  return metrics;
}

float Font::line_height(float size) const {
  const TextMetrics metrics = measure("Ag", size);
  return metrics.height > 0 ? metrics.height : size * 1.3f;
}

void Font::draw(Canvas& canvas, const std::string& text, float x, float y, float size,
                Color color) const {
  if (!valid_ || text.empty()) return;
  const TextMetrics metrics = measure(text, size);
  float pen = x;
  const float baseline = y + metrics.ascent;
  for (char32_t code : utf8_decode(text)) {
    const Glyph* g = glyph(code, size);
    if (!g) continue;
    if (g->width > 0 && g->height > 0) {
      canvas.blend_bitmap(static_cast<int>(std::lround(pen + g->bearing_x)),
                          static_cast<int>(std::lround(baseline - g->bearing_y)), g->width,
                          g->height, g->coverage.data(), g->width, color);
    }
    pen += g->advance;
  }
}

void Font::draw_centered(Canvas& canvas, const std::string& text, const Rect& area, float size,
                         Color color) const {
  const TextMetrics metrics = measure(text, size);
  draw(canvas, text, area.x + (area.w - metrics.width) / 2, area.y + (area.h - metrics.height) / 2,
       size, color);
}

void Font::draw_right(Canvas& canvas, const std::string& text, const Rect& area, float size,
                      Color color) const {
  const TextMetrics metrics = measure(text, size);
  draw(canvas, text, area.right() - metrics.width, area.y + (area.h - metrics.height) / 2, size,
       color);
}

std::vector<std::string> Font::wrap(const std::string& text, float size, float max_width) const {
  std::vector<std::string> lines;
  std::string current;
  std::string word;
  auto flush_line = [&]() {
    while (!current.empty() && current.back() == ' ') current.pop_back();
    if (!current.empty()) lines.push_back(current);
    current.clear();
  };
  for (std::size_t i = 0; i <= text.size(); ++i) {
    const char c = i < text.size() ? text[i] : ' ';
    if (c == '\n') {
      flush_line();
      continue;
    }
    if (c == ' ') {
      if (!word.empty()) {
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (measure(candidate, size).width > max_width && !current.empty()) {
          flush_line();
          current = word;
        } else {
          current = candidate;
        }
        word.clear();
      }
      continue;
    }
    word.push_back(c);
  }
  if (!word.empty()) {
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (measure(candidate, size).width > max_width && !current.empty()) {
      flush_line();
      current = word;
    } else {
      current = candidate;
    }
  }
  flush_line();
  return lines;
}

std::vector<std::string> Font::draw_wrapped(Canvas& canvas, const std::string& text, Rect area,
                                            float size, Color color, int max_lines) const {
  const std::vector<std::string> lines = wrap(text, size, area.w);
  const float height = line_height(size) * 1.16f;
  const int limit = max_lines > 0 ? std::min(max_lines, static_cast<int>(lines.size()))
                                  : static_cast<int>(lines.size());
  for (int i = 0; i < limit; ++i) {
    std::string line = lines[static_cast<std::size_t>(i)];
    if (max_lines > 0 && i == max_lines - 1 && static_cast<int>(lines.size()) > max_lines) {
      line = ellipsize(line + "…", size, area.w);
    }
    draw(canvas, line, area.x, area.y + i * height, size, color);
  }
  return lines;
}

std::string Font::ellipsize(const std::string& text, float size, float max_width) const {
  if (measure(text, size).width <= max_width) return text;
  const std::string dots = "…";
  std::string result;
  const std::vector<char32_t> codes = utf8_decode(text);
  for (char32_t code : codes) {
    std::string candidate = result;
    // Re-encode the code point (all UI text is UTF-8; the common case is ASCII).
    if (code < 0x80) {
      candidate.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      candidate.push_back(static_cast<char>(0xC0 | (code >> 6)));
      candidate.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      candidate.push_back(static_cast<char>(0xE0 | (code >> 12)));
      candidate.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      candidate.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    if (measure(candidate + dots, size).width > max_width) break;
    result = candidate;
  }
  return result + dots;
}

}  // namespace vf
