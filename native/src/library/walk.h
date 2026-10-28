// Finding the video files in the folders the user pointed at.
#pragma once
#include <string>
#include <vector>

namespace vf {

bool is_video_file(const std::string& path);
std::string mime_for(const std::string& path);

// Recursive, depth-capped, dot-files and system folders skipped. The depths and
// the skip list match server/library.js:walk so a scan finds the same files.
std::vector<std::string> walk_for_videos(const std::string& folder, int max_depth = 6);

}  // namespace vf
