// PNG output, used for screenshots and for the golden-image tests.
#include <cstdio>
#include <vector>

#include <png.h>

#include "core/log.h"
#include "render/canvas.h"

namespace vf {

bool write_png(const std::string& path, const Canvas& canvas) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) {
    LOG_ERROR("cannot open %s for writing", path.c_str());
    return false;
  }
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    std::fclose(file);
    LOG_ERROR("png_create_write_struct failed");
    return false;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    std::fclose(file);
    return false;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    LOG_ERROR("libpng error while writing %s", path.c_str());
    return false;
  }
  png_init_io(png, file);
  png_set_IHDR(png, info, static_cast<png_uint_32>(canvas.width()),
               static_cast<png_uint_32>(canvas.height()), 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  std::vector<png_bytep> rows(static_cast<std::size_t>(canvas.height()));
  const std::vector<uint8_t>& pixels = canvas.pixels();
  for (int y = 0; y < canvas.height(); ++y) {
    rows[static_cast<std::size_t>(y)] =
        const_cast<png_bytep>(&pixels[static_cast<std::size_t>(y) * canvas.width() * 4]);
  }
  png_write_image(png, rows.data());
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  std::fclose(file);
  return true;
}

}  // namespace vf
