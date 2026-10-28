#include "render/image.h"

#include <cstdio>
#include <cstring>
#include <setjmp.h>

#include <jpeglib.h>
#include <png.h>

#include "core/log.h"
#include "core/paths.h"

namespace vf {
namespace {

Image decode_jpeg(const std::string& bytes) {
  Image image;
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);

  // libjpeg's default error handler calls exit(); route it through setjmp instead so
  // one corrupt file cannot take the application with it.
  struct ErrorHook {
    jpeg_error_mgr base;
    jmp_buf jump;
  } hook;
  cinfo.err = jpeg_std_error(&hook.base);
  hook.base.error_exit = [](j_common_ptr info) {
    auto* h = reinterpret_cast<ErrorHook*>(info->err);
    longjmp(h->jump, 1);
  };
  if (setjmp(hook.jump)) {
    jpeg_destroy_decompress(&cinfo);
    return image;
  }

  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(bytes.data()),
               static_cast<unsigned long>(bytes.size()));
  jpeg_read_header(&cinfo, TRUE);
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);

  image.width = static_cast<int>(cinfo.output_width);
  image.height = static_cast<int>(cinfo.output_height);
  image.pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4, 255);
  std::vector<uint8_t> row(static_cast<std::size_t>(image.width) * cinfo.output_components);
  while (cinfo.output_scanline < cinfo.output_height) {
    uint8_t* rows[1] = {row.data()};
    jpeg_read_scanlines(&cinfo, rows, 1);
    const int y = static_cast<int>(cinfo.output_scanline) - 1;
    uint8_t* dst = &image.pixels[static_cast<std::size_t>(y) * image.width * 4];
    for (int x = 0; x < image.width; ++x) {
      dst[x * 4 + 0] = row[static_cast<std::size_t>(x) * cinfo.output_components + 0];
      dst[x * 4 + 1] = row[static_cast<std::size_t>(x) * cinfo.output_components + 1];
      dst[x * 4 + 2] = row[static_cast<std::size_t>(x) * cinfo.output_components + 2];
      dst[x * 4 + 3] = 255;
    }
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return image;
}

Image decode_png(const std::string& bytes) {
  Image image;
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size())) {
    return image;
  }
  png.format = PNG_FORMAT_RGBA;
  image.width = static_cast<int>(png.width);
  image.height = static_cast<int>(png.height);
  image.pixels.assign(PNG_IMAGE_SIZE(png), 0);
  if (!png_image_finish_read(&png, nullptr, image.pixels.data(), 0, nullptr)) {
    png_image_free(&png);
    image.pixels.clear();
    return image;
  }
  return image;
}

}  // namespace

Image decode_image(const std::string& bytes) {
  if (bytes.size() >= 2 && static_cast<uint8_t>(bytes[0]) == 0xFF &&
      static_cast<uint8_t>(bytes[1]) == 0xD8) {
    return decode_jpeg(bytes);
  }
  if (bytes.size() >= 8 && static_cast<uint8_t>(bytes[0]) == 0x89 && bytes[1] == 'P' &&
      bytes[2] == 'N' && bytes[3] == 'G') {
    return decode_png(bytes);
  }
  return Image{};
}

const Image* ImageCache::load(const std::string& path) {
  if (path.empty()) return nullptr;
  const long long mtime = file_mtime_ms(path);
  auto it = entries_.find(path);
  if (it != entries_.end()) {
    if (it->second.mtime == mtime) return it->second.image.valid() ? &it->second.image : nullptr;
    entries_.erase(it);
  }
  Entry entry;
  entry.mtime = mtime;
  entry.attempted = true;
  std::string bytes;
  if (!read_file(path, &bytes)) {
    LOG_DEBUG("image missing: %s", path.c_str());
    entries_[path] = entry;
    return nullptr;
  }
  entry.image = decode_image(bytes);
  if (!entry.image.valid()) LOG_WARN("image undecodable: %s", path.c_str());
  const bool ok = entry.image.valid();
  entries_[path] = std::move(entry);
  return ok ? &entries_[path].image : nullptr;
}

}  // namespace vf
