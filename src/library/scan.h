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
#include "library/artwork.h"

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
  // ffmpeg gets `-hwaccel auto` when true (the `hwDecode` setting). Off by default:
  // software decoding is predictable, and the setting exists to opt in.
  bool hw_decode = false;
  std::function<void(const ScanProgress&)> on_progress;
  std::function<void(const Catalog&)> on_catalog;
  std::function<bool()> cancel;
  // Filled in by the artwork pass so the caller can say what it drew instead of
  // guessing. A second scan over an unchanged library fills this with 0 drawn.
  ArtRunResult* art_result = nullptr;
};

Catalog scan_library(const ScanOptions& options);

bool read_sidecar(const std::string& video_path, Item* item);

}  // namespace vf
