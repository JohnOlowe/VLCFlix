// PNG output, used for screenshots and for the golden-image tests.
#include <cstdio>
#include <string>
#include <vector>

#include <png.h>

#include "core/log.h"
#include "core/paths.h"
#include "render/canvas.h"

namespace vf {
namespace {

// libpng writes through a callback into memory, not through a FILE*.
//
// This is not a preference. On Windows the bundled libpng16-16.dll was built against
// the MSVCRT while this program is built against the UCRT, so handing it a FILE*
// opened by our CRT is undefined behaviour across two different C runtimes. Measured
// under Wine with the real Windows binaries: every fwrite inside libpng failed
// ("libpng error: Write Error", a zero-byte PNG), and the render command died with an
// integer divide-by-zero inside msvcrt. Passing our own writer keeps the bytes on our
// side of the boundary, so it cannot happen on any CRT pairing.
void write_to_string(png_structp png, png_bytep data, png_size_t length) {
  auto* out = static_cast<std::string*>(png_get_io_ptr(png));
  out->append(reinterpret_cast<const char*>(data), length);
}

void flush_noop(png_structp) {}

}  // namespace

bool write_png(const std::string& path, const Canvas& canvas) {
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    LOG_ERROR("png_create_write_struct failed");
    return false;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    return false;
  }

  std::string bytes;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    LOG_ERROR("libpng error while writing %s", path.c_str());
    return false;
  }
  png_set_write_fn(png, &bytes, write_to_string, flush_noop);
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

  if (!write_file_atomic(path, bytes)) {
    LOG_ERROR("cannot write %s", path.c_str());
    return false;
  }
  // A PNG that was produced but written to a file we cannot read back is not a
  // success, and this is exactly the case that produced zero-byte screenshots.
  std::string check;
  if (!read_file(path, &check) || check.size() != bytes.size()) {
    LOG_ERROR("wrote %s but it does not read back (expected %zu bytes)", path.c_str(),
              bytes.size());
    return false;
  }
  return true;
}

}  // namespace vf
