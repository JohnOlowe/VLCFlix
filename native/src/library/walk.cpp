#include "library/walk.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#include "core/log.h"
#include "core/paths.h"

namespace fs = std::filesystem;

namespace vf {
namespace {

const std::set<std::string>& video_extensions() {
  static const std::set<std::string> kExt = {
      ".mp4",  ".mkv", ".avi", ".mov", ".m4v",  ".webm", ".wmv", ".flv",
      ".mpg",  ".mpeg", ".ts", ".m2ts", ".ogv", ".3gp",  ".divx", ".vob"};
  return kExt;
}

bool is_system_folder(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower == "node_modules" || lower == "system volume information" || lower == "$recycle.bin";
}

}  // namespace

bool is_video_file(const std::string& path) {
  return video_extensions().count(extension(path)) > 0;
}

std::string mime_for(const std::string& path) {
  static const std::pair<const char*, const char*> kMap[] = {
      {".mp4", "video/mp4"},      {".m4v", "video/mp4"},     {".webm", "video/webm"},
      {".ogv", "video/ogg"},      {".mov", "video/quicktime"}, {".mkv", "video/x-matroska"},
      {".avi", "video/x-msvideo"}, {".wmv", "video/x-ms-wmv"}, {".flv", "video/x-flv"},
      {".ts", "video/mp2t"},      {".m2ts", "video/mp2t"},   {".mpg", "video/mpeg"},
      {".mpeg", "video/mpeg"},    {".3gp", "video/3gpp"},    {".vob", "video/dvd"},
      {".divx", "video/divx"}};
  const std::string ext = extension(path);
  for (const auto& kv : kMap) {
    if (ext == kv.first) return kv.second;
  }
  return "application/octet-stream";
}

std::vector<std::string> walk_for_videos(const std::string& folder, int max_depth) {
  std::vector<std::string> found;
  std::vector<std::pair<std::string, int>> stack{{folder, 0}};
  while (!stack.empty()) {
    const auto [dir, depth] = stack.back();
    stack.pop_back();
    if (depth > max_depth) continue;
    std::error_code ec;
    fs::directory_iterator it(dir, ec), end;
    if (ec) {
      LOG_WARN("cannot read %s: %s", dir.c_str(), ec.message().c_str());
      continue;
    }
    for (; it != end; it.increment(ec)) {
      if (ec) break;
      const std::string name = it->path().filename().string();
      if (name.empty() || name[0] == '.') continue;
      std::error_code type_ec;
      if (it->is_directory(type_ec)) {
        if (is_system_folder(name)) continue;
        stack.emplace_back(it->path().string(), depth + 1);
      } else if (it->is_regular_file(type_ec) && is_video_file(name)) {
        found.push_back(absolute_path(it->path().string()));
      }
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

}  // namespace vf
