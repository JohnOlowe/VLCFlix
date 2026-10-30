#include "playback/ffmpeg_args.h"

#include <initializer_list>
#include <string>

namespace vf {
namespace {

void push(std::vector<std::string>* args, std::initializer_list<const char*> more) {
  for (const char* part : more) args->emplace_back(part);
}

std::string platform_of(const FfmpegPlayRequest& request) {
  if (!request.platform.empty()) return request.platform;
#ifdef _WIN32
  return "win32";
#elif defined(__APPLE__)
  return "darwin";
#else
  return "linux";
#endif
}

}  // namespace

std::string ffmpeg_play_url(int port) { return "http://127.0.0.1:" + std::to_string(port); }

std::vector<std::string> ffmpeg_play_args(const FfmpegPlayRequest& request) {
  // h264 can be copied into the window. Anything else has to be transcoded, and
  // that is the only path that decodes — so hardware decode is not added to a copy,
  // where it has nothing to do and can make ffmpeg refuse the command.
  const bool copy_video = request.vcodec == "h264";
  const bool copy_audio = copy_video && request.acodec == "aac";
  const bool hardware = request.hardware && !copy_video;
  const std::string platform = platform_of(request);

  std::vector<std::string> args = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin"};
  if (request.start >= 0.5) {
    args.push_back("-ss");
    args.push_back(std::to_string(static_cast<int>(request.start)));
  }
  if (hardware && platform == "win32") {
    push(&args, {"-hwaccel", "d3d11va", "-hwaccel_output_format", "d3d11"});
  } else if (hardware && platform == "darwin") {
    push(&args, {"-hwaccel", "videotoolbox"});
  } else if (hardware) {
    push(&args, {"-hwaccel", "auto"});
  }
  args.push_back("-i");
  args.push_back(request.path);
  push(&args, {"-sn", "-dn", "-map", "0:v:0", "-map", "0:a:0?"});
  if (copy_video) {
    push(&args, {"-c:v", "copy"});
  } else {
    if (hardware && platform == "win32") push(&args, {"-vf", "hwdownload,format=nv12"});
    push(&args, {"-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency", "-crf", "22",
                 "-pix_fmt", "yuv420p", "-g", "48"});
  }
  args.push_back("-c:a");
  args.push_back(copy_audio ? "copy" : "aac");
  if (!copy_audio) push(&args, {"-b:a", "160k", "-ac", "2"});
  // mpegts over a local listen, not a pipe: libVLC can open a URL, and a growing
  // file would look finished the moment the reader caught the writer.
  push(&args, {"-f", "mpegts", "-listen", "1"});
  args.push_back(ffmpeg_play_url(request.port));
  return args;
}

}  // namespace vf
