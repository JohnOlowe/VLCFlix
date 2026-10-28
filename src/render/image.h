// Loading a JPEG or PNG into RGBA, with a small cache.
//
// Posters and backdrops are JPEGs written by ffmpeg; screenshots are PNGs. A failed
// load returns an invalid image, and the caller draws its gradient fallback, so a
// missing or corrupt still degrades instead of crashing.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vf {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // RGBA

  bool valid() const { return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4; }
};

// Decodes from memory, guessing the format by signature.
Image decode_image(const std::string& bytes);

class ImageCache {
 public:
  // Returns nullptr only if the file is missing or cannot be decoded; the failure
  // is logged once per path, not once per frame.
  const Image* load(const std::string& path);
  void clear() { entries_.clear(); }
  std::size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    Image image;
    long long mtime = 0;
    bool attempted = false;
  };
  std::map<std::string, Entry> entries_;
};

}  // namespace vf
