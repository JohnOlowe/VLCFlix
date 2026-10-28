// Text: FreeType glyphs rasterised on demand and cached per pixel size.
//
// The UI needs three things from text — measure it, wrap it, draw it — and the
// screenshot tests need it to be deterministic, so the same font file is used
// everywhere and the fallback list is explicit.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render/canvas.h"

namespace vf {

struct TextMetrics {
  float width = 0;
  float height = 0;
  float ascent = 0;
  float descent = 0;
};

class Font {
 public:
  Font();
  ~Font();

  // Loads the first font file that exists from a small platform list. `bold`
  // prefers a bold face and falls back to the regular one.
  bool load_default(bool bold = false);
  bool load_file(const std::string& path);
  bool valid() const { return valid_; }
  const std::string& path() const { return path_; }

  TextMetrics measure(const std::string& text, float size) const;
  float line_height(float size) const;

  // Draws `text` with its baseline-derived box top-left at (x, y).
  void draw(Canvas& canvas, const std::string& text, float x, float y, float size,
            Color color) const;
  // Draws with the box centred horizontally in `area`.
  void draw_centered(Canvas& canvas, const std::string& text, const Rect& area, float size,
                     Color color) const;
  // Draws right-aligned to area.right().
  void draw_right(Canvas& canvas, const std::string& text, const Rect& area, float size,
                  Color color) const;

  // Greedy wrapping to `max_width`; returns the lines actually drawn and, in
  // `used_height`, how tall they were.
  std::vector<std::string> wrap(const std::string& text, float size, float max_width) const;
  std::vector<std::string> draw_wrapped(Canvas& canvas, const std::string& text, Rect area,
                                        float size, Color color, int max_lines = 0) const;

  // Truncates with an ellipsis so a long title cannot overflow its card.
  std::string ellipsize(const std::string& text, float size, float max_width) const;

 private:
  struct Glyph {
    int width = 0;
    int height = 0;
    int bearing_x = 0;
    int bearing_y = 0;
    float advance = 0;
    std::vector<uint8_t> coverage;
  };

  const Glyph* glyph(char32_t codepoint, float size) const;

  bool valid_ = false;
  std::string path_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  mutable std::map<long long, Glyph> cache_;  // key = size << 24 | codepoint
};

// Decodes UTF-8 into code points. Invalid sequences become U+FFFD rather than
// aborting, because subtitle files in the wild are not always clean.
std::vector<char32_t> utf8_decode(const std::string& text);

}  // namespace vf
