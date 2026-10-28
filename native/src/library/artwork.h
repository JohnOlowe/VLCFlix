// Poster, backdrop and hover preview for each film, drawn with ffmpeg.
//
// Ported from server/library.js with the same command lines, the same temp-file
// discipline and the same attempt accounting, plus two fixes from the audit:
//   A9  the stills are versioned too, not only the preview clip, so a format change
//       reaches existing libraries without a manual scan.
//   A5  "is this a 10-bit film" is decided from the real pixel format rather than
//       by whether the name contains "10" or "12" (which matched nv12).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/model.h"

namespace vf {

constexpr int kPreviewVersion = 2;
constexpr int kArtMaxAttempts = 3;

struct PreviewSegment {
  double start = 0;
  double length = 0;
};

std::vector<PreviewSegment> preview_segments(double duration, int count = 5, double length = 2.0);

struct ArtPaths {
  std::string poster;
  std::string backdrop;
  std::string preview;
};

ArtPaths art_paths(const std::string& media_dir, const std::string& id);

struct MissingArt {
  bool poster = false;
  bool backdrop = false;
  bool preview = false;
  bool any() const { return poster || backdrop || preview; }
};

// What is actually missing on disk, which is not the same as what the catalog
// claims: a file can be deleted, or left half-written by an interrupted run.
MissingArt missing_art(const Item& item, const std::string& media_dir);
bool needs_artwork(const Item& item, const std::string& media_dir);
bool should_retry_art(const Item& item);

// A pass killed between writing a file and saving the catalog, or a catalog field
// lost to a bug, leaves a finished file on disk with the entry empty. Adopt it
// instead of drawing it again, and clear fields that point at files which are gone
// (a 404 is worse than the gradient fallback). Returns how many fields changed.
int adopt_existing_art(Item& item, const std::string& media_dir);

// Half-written files from a killed pass are never shown and never reused.
int sweep_temp_art(const std::string& media_dir);

// Mean luma (0-255) via ffmpeg's signalstats; -1 when it cannot be measured.
double measure_luma(const std::string& file);

struct ArtResult {
  std::vector<std::string> produced;
  std::vector<std::string> failures;
};

ArtResult render_art(const std::string& video_path, double duration, const std::string& media_dir,
                     const std::string& id, MissingArt needs);

struct ArtProgress {
  int done = 0;
  int total = 0;
  std::string current;
};

struct ArtRunResult {
  int total = 0;
  int drawn = 0;
  int remaining = 0;
};

// Draws everything that is missing and nothing else, so a pass can be stopped at
// any point and the next one picks up the remainder. Two workers, as in the Node
// version (measured: eight clips in ~9 s on this 2-core box).
ArtRunResult render_missing_artwork(std::vector<Item>& items, const std::string& media_dir,
                                    bool retry_failed, const std::function<void(const ArtProgress&)>& on_progress,
                                    const std::function<void()>& on_catalog,
                                    const std::function<bool()>& cancel);

// True when the pixel format really is 10/12-bit (bug A5).
bool is_deep_color(const std::string& pix_fmt);

}  // namespace vf
