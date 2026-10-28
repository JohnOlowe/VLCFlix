#include "library/probe.h"

#include <string>

#include "core/json.h"
#include "core/log.h"
#include "platform/proc.h"

namespace vf {
namespace {

// ffprobe reports numbers as strings ("16.000000") in -of json output, so a plain
// Json::num() would read 0. Accept both forms.
double number_field(const Json& object, const std::string& key, double fallback = 0) {
  const Json* value = object.find(key);
  if (!value) return fallback;
  if (value->isNumber()) return value->asNumber(fallback);
  if (value->isString()) {
    try {
      return std::stod(value->asString());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

long long integer_field(const Json& object, const std::string& key, long long fallback = 0) {
  const double value = number_field(object, key, static_cast<double>(fallback));
  return static_cast<long long>(value + 0.5);
}

}  // namespace

ToolProbe probe_tools() {
  ToolProbe tools;
  tools.ffprobe = have_binary("ffprobe");
  tools.ffmpeg = have_binary("ffmpeg");
  tools.can_render = tools.ffmpeg;
  return tools;
}

VideoInfo probe_video(const std::string& path) {
  VideoInfo info;
  const ProcResult r = run_process(
      {"ffprobe", "-v", "error", "-show_entries",
       "format=duration:stream=codec_type,codec_name,width,height,pix_fmt", "-of", "json", path},
      20000);
  if (!r.ok()) {
    LOG_DEBUG("ffprobe failed for %s: %s", path.c_str(), r.error_line().c_str());
    return info;
  }
  Json root;
  std::string error;
  if (!Json::parse(r.out, root, &error)) {
    LOG_WARN("ffprobe JSON unreadable for %s: %s", path.c_str(), error.c_str());
    return info;
  }
  if (const Json* format = root.find("format")) info.duration = number_field(*format, "duration", 0);
  if (const Json* streams = root.find("streams"); streams && streams->isArray()) {
    bool have_video = false;
    for (const auto& stream : streams->items()) {
      const std::string kind = stream.str("codec_type");
      if (kind == "video" && !have_video) {
        have_video = true;
        info.width = static_cast<int>(integer_field(stream, "width", 0));
        info.height = static_cast<int>(integer_field(stream, "height", 0));
        info.vcodec = stream.str("codec_name");
        info.pix_fmt = stream.str("pix_fmt");
      } else if (kind == "audio" && info.acodec.empty()) {
        info.acodec = stream.str("codec_name");
      }
    }
  }
  return info;
}

}  // namespace vf
