// The ffmpeg command line for in-window playback. Kept separate so the Windows
// hardware plan can be checked without launching ffmpeg.
#pragma once
#include <string>
#include <vector>

namespace vf {

struct FfmpegPlayRequest {
  std::string path;
  std::string vcodec;
  std::string acodec;
  double start = 0;
  bool hardware = false;
  int port = 0;
  // "win32", "darwin", or "linux". Empty means this machine.
  std::string platform;
};

std::string ffmpeg_play_url(int port);
std::vector<std::string> ffmpeg_play_args(const FfmpegPlayRequest& request);

}  // namespace vf
