// Product identity. One place, because it is also what makes the Windows build a
// *different* application from VLCFlix 1.2.0: a different executable name, a
// different install directory, a different data directory, a different uninstall
// key and a different single-instance mutex. Changing the product name here and in
// packaging/ is all that is needed.
#pragma once

namespace vf {

constexpr const char* kProductName = "VLCFlix Studio";
constexpr const char* kProductSlug = "VLCFlixStudio";  // exe, dirs, registry, mutex
constexpr const char* kVersion = "2.0.0-turn1";
constexpr const char* kBuildDate = __DATE__ " " __TIME__;

// Data directory names. The Node app used "VLCFlix"; the native port must never
// read or write there, so the two can be installed side by side.
constexpr const char* kDataDirName = "VLCFlixStudio";
constexpr const char* kLegacyDataDirName = "VLCFlix";

// Log filenames.
constexpr const char* kLogBaseName = "vlcflix-studio";

}  // namespace vf
