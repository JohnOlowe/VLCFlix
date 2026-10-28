#pragma once

namespace vf {

constexpr const char* kProductName = "VLCFlix Studio";
constexpr const char* kProductSlug = "VLCFlixStudio";  // exe, dirs, registry, mutex
// 2.0.1: the window fits the screen, the sheets are clickable, the cursor says what
// is clickable. Bumped so a report can name the build it came from.
constexpr const char* kVersion = "2.0.2";
constexpr const char* kBuildDate = __DATE__ " " __TIME__;

constexpr const char* kDataDirName = "VLCFlixStudio";

// Log filenames.
constexpr const char* kLogBaseName = "vlcflix-studio";

}  // namespace vf
