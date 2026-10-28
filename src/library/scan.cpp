#include "library/scan.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include "core/json.h"
#include "core/log.h"
#include "core/naming.h"
#include "core/paths.h"
#include "core/sha1.h"
#include "library/artwork.h"
#include "library/probe.h"
#include "library/subtitles.h"
#include "library/walk.h"

namespace vf {
namespace {

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

bool read_sidecar(const std::string& video_path, Item* item) {
  if (!item) return false;
  const std::string candidate = join(dirname(video_path), stem(video_path) + ".json");
  std::string text;
  if (!read_file(candidate, &text)) return false;
  Json sidecar;
  std::string error;
  if (!Json::parse(text, sidecar, &error) || !sidecar.isObject()) {
    LOG_WARN("sidecar %s ignored: %s", candidate.c_str(), error.c_str());
    return false;
  }
  if (sidecar.has("title")) item->title = sidecar.str("title", item->title);
  if (sidecar.has("year")) item->year = static_cast<int>(sidecar.integer("year", item->year));
  if (sidecar.has("synopsis")) item->synopsis = sidecar.str("synopsis");
  if (sidecar.has("rating")) item->rating = sidecar.str("rating");
  if (sidecar.has("accent")) item->accent = sidecar.str("accent");
  if (const Json* genres = sidecar.find("genres"); genres && genres->isArray()) {
    item->genres.clear();
    for (const auto& g : genres->items()) {
      if (g.isString()) item->genres.push_back(g.asString());
    }
  }
  return true;
}

Catalog scan_library(const ScanOptions& options) {
  Catalog catalog;
  catalog.scanned_at = now_ms();
  catalog.folders = options.folders;

  const ToolProbe tools = probe_tools();
  catalog.have_ffmpeg = tools.ffmpeg;
  catalog.have_ffprobe = tools.ffprobe;
  catalog.can_render = tools.can_render;
  LOG_INFO("tools: ffmpeg=%s ffprobe=%s", tools.ffmpeg ? "yes" : "no",
           tools.ffprobe ? "yes" : "no");

  if (!options.render_artwork || !tools.can_render) {
    if (!tools.can_render) LOG_WARN("ffmpeg missing: artwork will not be drawn, the UI falls back");
  }

  /* ------------------------- phase 1: which files ------------------------ */

  std::vector<std::string> files;
  std::map<std::string, bool> seen;
  for (const auto& folder : options.folders) {
    if (!dir_exists(folder)) {
      catalog.errors.push_back("folder does not exist: " + folder);
      LOG_WARN("folder does not exist: %s", folder.c_str());
      continue;
    }
    for (const auto& file : walk_for_videos(folder)) {
      const std::string absolute = absolute_path(file);
      if (seen[absolute]) continue;
      seen[absolute] = true;
      files.push_back(absolute);
    }
  }
  LOG_INFO("walk found %zu video file(s) in %zu folder(s)", files.size(), options.folders.size());

  // One listing per directory, so sidecar subtitles cost nothing per file.
  std::map<std::string, std::vector<std::string>> entries_by_dir;
  for (const auto& file : files) {
    const std::string dir = dirname(file);
    if (entries_by_dir.count(dir)) continue;
    entries_by_dir[dir] = list_dir(dir);
  }

  std::map<std::string, Item> previous_by_id;
  for (const auto& item : options.previous.items) previous_by_id[item.id] = item;

  /* ------------------------ phase 1: read metadata ---------------------- */

  std::vector<Item> items(files.size());
  std::vector<char> keep(files.size(), 0);
  std::atomic<int> next{0};
  std::atomic<int> done{0};
  std::mutex error_mutex;

  auto worker = [&] {
    for (;;) {
      const int index = next++;
      if (index >= static_cast<int>(files.size())) return;
      const std::string& file = files[static_cast<std::size_t>(index)];

      Item item;
      item.id = item_id_for_path(file);
      item.path = file;
      item.filename = basename(file);
      item.size = file_size(file);
      item.mtime_ms = file_mtime_ms(file);
      if (item.size < 0) {
        std::lock_guard<std::mutex> lock(error_mutex);
        catalog.errors.push_back("cannot stat " + file);
        continue;
      }

      const auto dir_it = entries_by_dir.find(dirname(file));
      const std::vector<std::string> entries =
          dir_it == entries_by_dir.end() ? std::vector<std::string>() : dir_it->second;
      item.subtitles = find_subtitles(file, entries);

      const auto prev_it = previous_by_id.find(item.id);
      const bool unchanged = prev_it != previous_by_id.end() &&
                             prev_it->second.size == item.size &&
                             prev_it->second.mtime_ms / 1000 == item.mtime_ms / 1000;

      if (unchanged) {
        // Reuse what the previous scan learned, but refresh what is cheap and can
        // change without the video changing (sidecar subtitles sitting next to it).
        Item cached = prev_it->second;
        cached.subtitles = item.subtitles;
        cached.path = file;
        cached.filename = item.filename;
        item = std::move(cached);
      } else {
        const ParsedName parsed = parse_name(item.filename);
        item.title = parsed.title;
        item.year = parsed.year;
        item.added_at = prev_it != previous_by_id.end() ? prev_it->second.added_at : item.mtime_ms;
        read_sidecar(file, &item);

        if (tools.ffprobe) {
          const VideoInfo info = probe_video(file);
          item.duration = info.duration;
          item.width = info.width;
          item.height = info.height;
          item.vcodec = info.vcodec;
          item.acodec = info.acodec;
          item.pix_fmt = info.pix_fmt;
          if (info.duration == 0) {
            std::lock_guard<std::mutex> lock(error_mutex);
            catalog.errors.push_back("unreadable video: " + item.filename);
            LOG_WARN("unreadable video: %s", file.c_str());
          }
        }
        // A changed file invalidates the old clip.
        item.preview_version = 0;
      }
      keep[static_cast<std::size_t>(index)] = 1;
      items[static_cast<std::size_t>(index)] = std::move(item);

      const int progress = ++done;
      if (options.on_progress) {
        options.on_progress(
            ScanProgress{"read", progress, static_cast<int>(files.size()), basename(file)});
      }
    }
  };

  {
    std::vector<std::thread> workers;
    const int worker_count = std::min<int>(6, std::max<int>(1, static_cast<int>(files.size())));
    for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (auto& t : workers) t.join();
  }

  for (std::size_t i = 0; i < items.size(); ++i) {
    if (keep[i]) catalog.items.push_back(std::move(items[i]));
  }
  std::sort(catalog.items.begin(), catalog.items.end(),
            [](const Item& a, const Item& b) { return a.added_at > b.added_at; });

  LOG_INFO("phase 1 complete: %zu item(s), %zu error(s)", catalog.items.size(),
           catalog.errors.size());
  if (options.on_progress) {
    options.on_progress(ScanProgress{"art", 0, static_cast<int>(catalog.items.size()), ""});
  }
  if (options.on_catalog) options.on_catalog(catalog);

  /* --------------------------- phase 2: draw ---------------------------- */

  if (options.render_artwork && tools.can_render) {
    render_missing_artwork(
        catalog.items, options.media_dir, true,
        [&](const ArtProgress& p) {
          if (options.on_progress) {
            options.on_progress(ScanProgress{"art", p.done, p.total, p.current});
          }
        },
        [&]() {
          if (options.on_catalog) options.on_catalog(catalog);
        },
        options.cancel);
  }

  return catalog;
}

}  // namespace vf
