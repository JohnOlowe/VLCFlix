// The two-phase library scan.
//
// Phase 1 reads: stat, ffprobe, filename parsing, sidecar metadata, subtitle
// discovery. No pixels are touched, so the catalog is publishable in about a
// second. Phase 2 draws artwork in the background and republishes as it goes.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/model.h"

namespace vf {

struct ScanProgress {
  std::string phase;  // "read" | "art"
  int done = 0;
  int total = 0;
  std::string current;
};

struct ScanOptions {
  std::vector<std::string> folders;
  std::string media_dir;
  Catalog previous;  // used to skip unchanged files
  bool render_artwork = true;
  std::function<void(const ScanProgress&)> on_progress;
  std::function<void(const Catalog&)> on_catalog;
  std::function<bool()> cancel;
};

Catalog scan_library(const ScanOptions& options);

// Same idea as readSidecar() in library.js: a JSON file beside the video,
// named after it, overrides everything the filename implied.
bool read_sidecar(const std::string& video_path, Item* item);

}  // namespace vf
