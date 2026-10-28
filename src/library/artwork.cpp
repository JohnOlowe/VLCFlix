#include "library/artwork.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

#include "core/log.h"
#include "core/paths.h"
#include "library/probe.h"
#include "platform/proc.h"

namespace vf {

namespace {
bool g_hwaccel = false;

// One place that builds ffmpeg's argv, so the hardware flag cannot be forgotten at one
// call site out of five.
std::vector<std::string> ffmpeg_args(const std::vector<std::string>& rest) {
  std::vector<std::string> args = {"ffmpeg"};
  if (g_hwaccel) {
    args.push_back("-hwaccel");
    args.push_back("auto");
  }
  args.insert(args.end(), rest.begin(), rest.end());
  return args;
}
}  // namespace
namespace {

// A touch of contrast and a vignette keeps white text legible over a real frame.
constexpr const char* kGrade = "eq=contrast=1.1:saturation=1.22,vignette=angle=PI/5.2";

bool has_content(const std::string& file) { return file_size(file) > 0; }

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string unique_tag() {
  static std::atomic<int> counter{0};
  return ".tmp" + std::to_string(++counter);
}

}  // namespace

bool is_deep_color(const std::string& pix_fmt) {
  if (pix_fmt.empty()) return false;
  const std::vector<std::string> deep = {"10le", "10be", "12le", "12be", "p010", "p012", "p210"};
  for (const auto& marker : deep) {
    if (pix_fmt.find(marker) != std::string::npos) return true;
  }
  return false;
}

std::vector<PreviewSegment> preview_segments(double duration, int count, double length) {
  std::vector<PreviewSegment> segments;
  if (!(duration > 0) || count <= 0) return segments;
  const double len = std::min(length, std::max(0.6, duration / count));
  const double fractions[5] = {0.12, 0.32, 0.52, 0.72, 0.90};
  std::vector<double> starts;
  for (int i = 0; i < count && i < 5; ++i) {
    double start = std::max(0.0, std::min(duration * fractions[i], duration - len));
    bool duplicate = false;
    for (double existing : starts) {
      if (std::fabs(existing - start) < 0.15) duplicate = true;
    }
    if (duplicate) continue;
    starts.push_back(start);
  }
  for (double start : starts) segments.push_back(PreviewSegment{start, len});
  return segments;
}

ArtPaths art_paths(const std::string& media_dir, const std::string& id) {
  ArtPaths paths;
  paths.poster = join(media_dir, id + "-poster.jpg");
  paths.backdrop = join(media_dir, id + "-backdrop.jpg");
  paths.preview = join(media_dir, id + "-preview.mp4");
  return paths;
}

MissingArt missing_art(const Item& item, const std::string& media_dir) {
  const ArtPaths paths = art_paths(media_dir, item.id);
  MissingArt missing;
  missing.poster = !has_content(paths.poster);
  missing.backdrop = !has_content(paths.backdrop);
  missing.preview = !has_content(paths.preview) || item.preview_version != kPreviewVersion;
  return missing;
}

bool needs_artwork(const Item& item, const std::string& media_dir) {
  return missing_art(item, media_dir).any();
}

bool should_retry_art(const Item& item) { return item.art_attempts < kArtMaxAttempts; }

int adopt_existing_art(Item& item, const std::string& media_dir) {
  const ArtPaths paths = art_paths(media_dir, item.id);
  int changes = 0;
  struct Field {
    std::string* value;
    const std::string* path;
  };
  const Field fields[3] = {{&item.poster, &paths.poster},
                           {&item.backdrop, &paths.backdrop},
                           {&item.preview, &paths.preview}};
  for (const auto& field : fields) {
    const bool on_disk = file_size(*field.path) > 0;
    if (on_disk && field.value->empty()) {
      *field.value = basename(*field.path);
      ++changes;
    } else if (!on_disk && !field.value->empty()) {
      field.value->clear();
      ++changes;
    }
  }
  return changes;
}

int sweep_temp_art(const std::string& media_dir) {
  int removed = 0;
  for (const auto& name : list_dir(media_dir)) {
    const std::string lower = [&] {
      std::string s = name;
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }();
    const bool temp = lower.find(".tmp") != std::string::npos &&
                      (lower.size() > 4 && (lower.substr(lower.size() - 4) == ".jpg" ||
                                            lower.substr(lower.size() - 4) == ".mp4"));
    if (!temp) continue;
    const std::string full = join(media_dir, name);
    // A pass running right now may own this file; only clear old ones.
    if (now_ms() - file_mtime_ms(full) < 5 * 60 * 1000) continue;
    if (remove_file(full)) ++removed;
  }
  return removed;
}

double measure_luma(const std::string& file) {
  const ProcResult r = run_process(
      ffmpeg_args({"-hide_banner", "-nostats", "-i", file, "-frames:v", "1", "-vf",
       "signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-", "-f", "null", "-"}),
      20000);
  const std::string text = r.out + r.err;
  const std::string key = "lavfi.signalstats.YAVG=";
  const std::size_t pos = text.find(key);
  if (pos == std::string::npos) return -1;
  try {
    return std::stod(text.substr(pos + key.size()));
  } catch (...) {
    return -1;
  }
}

namespace {

// Dark openings and washed-out masters are the norm. Gamma lifts the midtones that
// dominate a frame while leaving the black point at black, so the image becomes
// readable without fog.
void correct_exposure(const std::string& file, double target) {
  const double luma = measure_luma(file);
  if (luma <= 0) return;
  const double ratio = target / std::max(luma, 1.0);
  if (ratio > 0.86 && ratio < 1.16) return;
  const double gamma = std::max(0.62, std::min(1.95, std::pow(ratio, 0.62)));
  const double contrast = ratio > 1 ? 1.07 : 1.03;
  char filter[128];
  std::snprintf(filter, sizeof filter, "eq=gamma=%.3f:contrast=%.2f:saturation=1.04", gamma,
                contrast);
  const ProcResult r = run_process(ffmpeg_args({"-y", "-hide_banner", "-loglevel", "error", "-i", file,
                                 "-vf", filter, "-q:v", "3", file}),
                                   30000);
  if (!r.ok()) LOG_DEBUG("exposure correction failed for %s", basename(file).c_str());
}

}  // namespace


void set_hwaccel_decoding(bool enabled) { g_hwaccel = enabled; }
bool hwaccel_decoding() { return g_hwaccel; }

ArtResult render_art(const std::string& video_path, double duration, const std::string& media_dir,
                     const std::string& id, MissingArt needs) {
  ArtResult result;
  const ArtPaths final_paths = art_paths(media_dir, id);
  const std::string tag = unique_tag();
  auto temp_for = [&](const std::string& path) {
    const std::string ext = extension(path);
    return path.substr(0, path.size() - ext.size()) + tag + ext;
  };
  const std::string tmp_poster = temp_for(final_paths.poster);
  const std::string tmp_backdrop = temp_for(final_paths.backdrop);
  const std::string tmp_preview = temp_for(final_paths.preview);

  auto at = [&](double fraction) {
    return std::max(0.0, std::min(duration * fraction, std::max(0.0, duration - 1)));
  };

  std::vector<std::thread> jobs;
  std::mutex failures_mutex;
  std::vector<std::string> failed;
  auto track = [&](const std::string& key, ProcResult r, const std::string& output) {
    if (r.ok() && has_content(output)) return;
    {
      std::lock_guard<std::mutex> lock(failures_mutex);
      failed.push_back(key);
    }
    LOG_WARN("%s failed for %s: %s", key.c_str(), basename(video_path).c_str(),
             r.error_line().c_str());
  };

  if (needs.poster) {
    jobs.emplace_back([&] {
      char seconds[32];
      std::snprintf(seconds, sizeof seconds, "%.3f", at(0.38));
      const std::string filter =
          std::string("scale=400:600:force_original_aspect_ratio=increase,crop=400:600,") + kGrade;
      track("poster",
            run_process(ffmpeg_args({"-y", "-loglevel", "error", "-ss", seconds, "-i", video_path,
                                     "-frames:v", "1", "-vf", filter, "-q:v", "4", tmp_poster}),
                        60000),
            tmp_poster);
    });
  }
  if (needs.backdrop) {
    jobs.emplace_back([&] {
      char seconds[32];
      std::snprintf(seconds, sizeof seconds, "%.3f", at(0.22));
      const std::string filter =
          std::string("scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720,") + kGrade;
      track("backdrop",
            run_process(ffmpeg_args({"-y", "-loglevel", "error", "-ss", seconds, "-i", video_path,
                                     "-frames:v", "1", "-vf", filter, "-q:v", "3", tmp_backdrop}),
                        60000),
            tmp_backdrop);
    });
  }
  if (needs.preview) {
    const auto segments = preview_segments(duration);
    if (!segments.empty()) {
      // By value, deliberately: `segments` lives only inside this if-block, and the
      // jobs are joined after it closes. Capturing it by reference let the preview
      // thread read a destroyed vector, which reached ffmpeg as an absurd `-t` and
      // failed the preview roughly half the time - a real bug this turn found, not a
      // theoretical one.
      jobs.emplace_back([&, segments] {
        // One ffmpeg pass: seek to each snippet, normalise, concatenate.
        std::vector<std::string> args = ffmpeg_args({"-y", "-loglevel", "error"});
        for (const auto& seg : segments) {
          char start[32];
          char len[32];
          std::snprintf(start, sizeof start, "%.3f", seg.start);
          std::snprintf(len, sizeof len, "%.3f", seg.length);
          args.insert(args.end(), {"-ss", start, "-t", len, "-i", video_path});
        }
        std::string filter_complex;
        for (std::size_t i = 0; i < segments.size(); ++i) {
          filter_complex += "[" + std::to_string(i) + ":v]scale=640:-2,setsar=1,fps=24[v" +
                            std::to_string(i) + "];";
        }
        for (std::size_t i = 0; i < segments.size(); ++i) {
          filter_complex += "[v" + std::to_string(i) + "]";
        }
        filter_complex += "concat=n=" + std::to_string(segments.size()) + ":v=1:a=0[out]";
        args.insert(args.end(), {"-filter_complex", filter_complex, "-map", "[out]", "-an", "-c:v",
                                 "libx264", "-preset", "veryfast", "-crf", "30", "-pix_fmt",
                                 "yuv420p", "-movflags", "+faststart", tmp_preview});
        track("preview", run_process(args, 180000), tmp_preview);
      });
    } else {
      std::lock_guard<std::mutex> lock(failures_mutex);
      failed.push_back("preview");
      LOG_WARN("preview skipped for %s: duration unknown", basename(video_path).c_str());
    }
  }

  for (auto& job : jobs) job.join();

  // Fix stills that came out too dark or too flat for white text to sit on.
  if (needs.poster && has_content(tmp_poster)) correct_exposure(tmp_poster, 64);
  if (needs.backdrop && has_content(tmp_backdrop)) correct_exposure(tmp_backdrop, 72);

  auto publish = [&](const std::string& key, const std::string& tmp, const std::string& final_path) {
    if (!has_content(tmp)) {
      std::lock_guard<std::mutex> lock(failures_mutex);
      if (std::find(failed.begin(), failed.end(), key) == failed.end()) failed.push_back(key);
      return;
    }
    if (std::rename(tmp.c_str(), final_path.c_str()) == 0) {
      result.produced.push_back(key);
    } else {
      std::lock_guard<std::mutex> lock(failures_mutex);
      failed.push_back(key);
      remove_file(tmp);
    }
  };
  if (needs.poster) publish("poster", tmp_poster, final_paths.poster);
  if (needs.backdrop) publish("backdrop", tmp_backdrop, final_paths.backdrop);
  if (needs.preview) publish("preview", tmp_preview, final_paths.preview);

  for (const auto& tmp : {tmp_poster, tmp_backdrop, tmp_preview}) {
    if (has_content(tmp)) remove_file(tmp);
  }
  result.failures = failed;
  return result;
}

ArtRunResult render_missing_artwork(std::vector<Item>& items, const std::string& media_dir,
                                    bool retry_failed,
                                    const std::function<void(const ArtProgress&)>& on_progress,
                                    const std::function<void()>& on_catalog,
                                    const std::function<bool()>& cancel) {
  std::vector<std::size_t> pending;
  for (std::size_t i = 0; i < items.size(); ++i) {
    // A file the prober could not read has no duration to seek into, so it cannot have
    // artwork. Counting it as "missing" for ever would turn one bad file into a
    // permanent artwork job.
    if (items[i].unreadable) continue;
    if (!retry_failed && !should_retry_art(items[i])) continue;
    if (!needs_artwork(items[i], media_dir)) continue;
    pending.push_back(i);
  }

  ArtRunResult summary;
  summary.total = static_cast<int>(pending.size());
  if (pending.empty()) return summary;

  ensure_dir(media_dir);
  std::atomic<int> next{0};
  std::atomic<int> drawn{0};
  std::atomic<int> remaining{0};
  std::mutex publish_mutex;
  long long last_publish = now_ms();

  auto worker = [&] {
    for (;;) {
      const int index = next++;
      if (index >= static_cast<int>(pending.size())) return;
      if (cancel && cancel()) {
        remaining++;
        continue;
      }
      Item& item = items[pending[static_cast<std::size_t>(index)]];

      // A catalog entry with no duration has never been probed (an interrupted
      // first run): probe just this file rather than rescanning the library.
      if (!(item.duration > 0)) {
        if (!file_exists(item.path)) {
          item.art_attempts++;
          item.art_error = "the video file is missing";
          remaining++;
          continue;
        }
        const VideoInfo info = probe_video(item.path);
        item.duration = info.duration;
        if (info.width) item.width = info.width;
        if (info.height) item.height = info.height;
        item.vcodec = info.vcodec;
        item.acodec = info.acodec;
        item.pix_fmt = info.pix_fmt;
      }

      // Adopt anything already on disk first: a file that exists is finished work.
      if (const int adopted = adopt_existing_art(item, media_dir); adopted > 0) {
        LOG_DEBUG("adopted %d artwork field(s) for %s", adopted, item.filename.c_str());
      }
      const MissingArt needs = missing_art(item, media_dir);
      if (item.duration > 0) {
        const ArtResult art = render_art(item.path, item.duration, media_dir, item.id, needs);
        const ArtPaths final_paths = art_paths(media_dir, item.id);
        for (const auto& key : art.produced) {
          if (key == "poster") item.poster = basename(final_paths.poster);
          if (key == "backdrop") item.backdrop = basename(final_paths.backdrop);
          if (key == "preview") item.preview = basename(final_paths.preview);
        }
        // Only claim the new format once a clip exists, or a transient ffmpeg
        // failure would mark it done and never retry.
        if (!needs.preview || std::find(art.produced.begin(), art.produced.end(), "preview") !=
                                  art.produced.end()) {
          item.preview_version = kPreviewVersion;
        }
        if (!needs_artwork(item, media_dir)) {
          drawn++;
          item.art_attempts = 0;
          item.art_error.clear();
        } else {
          remaining++;
          item.art_attempts++;
          item.art_error = art.failures.empty() ? "artwork incomplete"
                                                : art.failures.front() + " could not be drawn";
          LOG_WARN("artwork failed for %s: %s", item.filename.c_str(), item.art_error.c_str());
        }
      } else {
        remaining++;
        item.art_attempts++;
        item.art_error = "the video could not be read";
        LOG_WARN("artwork skipped for %s: %s", item.filename.c_str(), item.art_error.c_str());
      }

      if (on_progress) on_progress(ArtProgress{drawn.load(), static_cast<int>(pending.size()), item.filename});
      // Publish at a sane rate; one write per frame would thrash the disk.
      {
        std::lock_guard<std::mutex> lock(publish_mutex);
        const bool finished = drawn.load() + remaining.load() == static_cast<int>(pending.size());
        if (finished || now_ms() - last_publish > 1200) {
          last_publish = now_ms();
          if (on_catalog) on_catalog();
        }
      }
    }
  };

  std::vector<std::thread> workers;
  const int worker_count = std::min<int>(2, static_cast<int>(pending.size()));
  for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
  for (auto& t : workers) t.join();

  if (on_progress) on_progress(ArtProgress{drawn.load(), static_cast<int>(pending.size()), ""});
  if (on_catalog) on_catalog();
  summary.drawn = drawn.load();
  summary.remaining = remaining.load();
  return summary;
}

}  // namespace vf
