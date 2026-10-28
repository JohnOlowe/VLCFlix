#include "ui/theme.h"

#include <cstdint>
#include <string>

namespace vf {
namespace theme {

Color poster_fallback(const std::string& title, bool darken) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : title) {
    hash ^= c;
    hash *= 16777619u;
  }
  const double hue = static_cast<double>(hash % 360u);
  const double saturation = darken ? 0.42 : 0.52;
  const double lightness = darken ? 0.16 : 0.28;
  return from_hsl(hue, saturation, lightness);
}

}  // namespace theme
}  // namespace vf
