#pragma once
#include <string>

namespace vf {

struct VideoInfo {
  double duration = 0;
  int width = 0;
  int height = 0;
  std::string vcodec;
  std::string acodec;
  std::string pix_fmt;
};

struct ToolProbe {
  bool ffmpeg = false;
  bool ffprobe = false;
  bool can_render = false;
};

ToolProbe probe_tools();
VideoInfo probe_video(const std::string& path);

}  // namespace vf
