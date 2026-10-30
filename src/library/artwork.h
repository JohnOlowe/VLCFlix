#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/model.h"

namespace vf {

// 3: 6 silent 3s cuts at 360p, input-seek, no hardware decode. Older clips re-render.
constexpr int kPreviewVersion = 3;
constexpr int kArtMaxAttempts = 3;

struct PreviewSegment {
  double start = 0;
  double length = 0;
};

// Six cuts across the film, skipping the opening titles and the end credits.
// A short file gets fewer, and never repeats the same moment.
std::vector<PreviewSegment> preview_segments(double duration, int count = 6, double length = 3.0);

// The hover rule, with no ffmpeg involved: the film under the pointer goes to the
// front. If it was not queued, it is inserted there. Background work stays behind it.
std::vector<std::string> preview_queue_after_hover(std::vector<std::string> pending,
                                                   const std::string& hovered);

// Remember which film the pointer is on. The artwork workers finish the cut they
// have already started, then take this id next. They do not kill ffmpeg. Empty
// clears it, so leaving a card returns the queue to background order.
void prioritize_artwork(const std::string& id);

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

// Hardware decoding for the artwork passes. This is the real meaning of the
// `hwDecode` setting: ffmpeg gets `-hwaccel auto`, which uses the GPU when the machine
// has a usable device and silently falls back to software when it does not (verified:
// on a machine with no Vulkan video queue the command still produces the still).
void set_hwaccel_decoding(bool enabled);
bool hwaccel_decoding();

// Mean luma (0-255) via ffmpeg's signalstats; -1 when it cannot be measured.
double measure_luma(const std::string& file);

struct ArtResult {
  std::vector<std::string> produced;
  std::vector<std::string> failures;
  // The hovered film jumped the queue before this one finished. Not a failure:
  // the caller puts it back and draws the hovered one next.
  bool yielded = false;
};

// `yield_cut` is asked before each preview cut. True means a hovered film is
// waiting: stop starting cuts (do not kill the one already running) and return
// with yielded set. Poster and backdrop are not interrupted.
ArtResult render_art(const std::string& video_path, double duration, const std::string& media_dir,
                     const std::string& id, MissingArt needs,
                     const std::function<bool()>& yield_cut = {});

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
// any point and the next one picks up the remainder. Two workers, never more:
// a Celeron N3350 swaps if six ffmpegs start at once. A hover moves that film
// to the front; the worker finishes its current cut, then takes the hovered one.
ArtRunResult render_missing_artwork(std::vector<Item>& items, const std::string& media_dir,
                                    bool retry_failed, const std::function<void(const ArtProgress&)>& on_progress,
                                    const std::function<void()>& on_catalog,
                                    const std::function<bool()>& cancel);

// True when the pixel format really is 10/12-bit (bug A5).
bool is_deep_color(const std::string& pix_fmt);

}  // namespace vf
