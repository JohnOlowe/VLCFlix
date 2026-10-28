#include "core/store.h"

#include <algorithm>
#include <chrono>

#include "core/json.h"
#include "core/log.h"
#include "core/paths.h"

namespace vf {
namespace {

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

Json progress_to_json(const std::map<std::string, Progress>& all) {
  Json out = Json::object();
  for (const auto& kv : all) {
    Json entry = Json::object();
    entry.set("position", kv.second.position);
    entry.set("duration", kv.second.duration);
    entry.set("updatedAt", kv.second.updated_at);
    out.set(kv.first, entry);
  }
  return out;
}

}  // namespace

Store::Store(std::string data_dir_path) : dir_(data_dir_path.empty() ? data_dir() : data_dir_path) {}

std::string Store::file_for(const std::string& name) const { return join(dir_, name + ".json"); }

Settings Store::load_settings() const {
  Settings settings;  // defaults
  std::string text;
  if (!read_file(file_for("settings"), &text)) return settings;
  Json root = Json::parseOr(text, Json::object());
  if (!root.isObject()) {
    LOG_WARN("settings.json is not an object; using defaults");
    return settings;
  }
  if (const Json* folders = root.find("libraryFolders"); folders && folders->isArray()) {
    settings.library_folders.clear();
    for (const auto& f : folders->items()) {
      if (f.isString() && !f.asString().empty()) settings.library_folders.push_back(f.asString());
    }
  }
  settings.vlc_path = root.str("vlcPath", settings.vlc_path);
  settings.vlc_http_port = static_cast<int>(root.integer("vlcHttpPort", settings.vlc_http_port));
  settings.vlc_password = root.str("vlcPassword", settings.vlc_password);
  settings.play_mode = root.str("playMode", settings.play_mode);
  settings.vlc_fullscreen = root.flag("vlcFullscreen", settings.vlc_fullscreen);
  settings.hw_decode = root.flag("hwDecode", settings.hw_decode);
  return settings;
}

bool Store::patch_settings(const Json& patch, Settings* out, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    LOG_WARN("settings patch rejected: %s", why.c_str());
    return false;
  };
  if (!patch.isObject()) return fail("settings patch must be an object");

  Settings current = load_settings();
  if (const Json* v = patch.find("libraryFolders"); v) {
    if (!v->isArray()) return fail("libraryFolders must be an array of paths");
    std::vector<std::string> folders;
    for (const auto& f : v->items()) {
      if (!f.isString()) return fail("libraryFolders must contain strings");
      std::string path = f.asString();
      if (path.rfind("~/", 0) == 0) path = join(home_dir(), path.substr(2));
      if (!path.empty()) folders.push_back(absolute_path(path));
    }
    current.library_folders = folders;
  }
  if (const Json* v = patch.find("vlcPath"); v) {
    if (!v->isString() && !v->isNull()) return fail("vlcPath must be a string or null");
    current.vlc_path = v->isNull() ? "" : v->asString();
  }
  if (const Json* v = patch.find("vlcHttpPort"); v) {
    if (!v->isNumber()) return fail("vlcHttpPort must be a number");
    const long long port = v->asInt(-1);
    if (port < 1 || port > 65535) return fail("vlcHttpPort must be 1-65535");
    current.vlc_http_port = static_cast<int>(port);
  }
  if (const Json* v = patch.find("vlcPassword"); v) {
    // The audit found this key accepted any JSON type, which produced a Basic
    // header of ": [object Object]" at run time. Strings only.
    if (!v->isString()) return fail("vlcPassword must be a string");
    current.vlc_password = v->asString();
  }
  if (const Json* v = patch.find("playMode"); v) {
    if (!v->isString()) return fail("playMode must be a string");
    const std::string mode = v->asString();
    if (mode != "vlc" && mode != "ffmpeg" && mode != "browser")
      return fail("playMode must be 'vlc', 'ffmpeg' or 'browser'");
    current.play_mode = mode;
  }
  for (const char* key : {"vlcFullscreen", "hwDecode"}) {
    if (const Json* v = patch.find(key); v) {
      if (!v->isBool()) return fail(std::string(key) + " must be true or false");
      if (std::string(key) == "vlcFullscreen") current.vlc_fullscreen = v->asBool();
      if (std::string(key) == "hwDecode") current.hw_decode = v->asBool();
    }
  }

  Json out_json = Json::object();
  Json folders = Json::array();
  for (const auto& f : current.library_folders) folders.push(Json(f));
  out_json.set("libraryFolders", folders);
  out_json.set("vlcPath", current.vlc_path.empty() ? Json() : Json(current.vlc_path));
  out_json.set("vlcHttpPort", current.vlc_http_port);
  out_json.set("vlcPassword", current.vlc_password);
  out_json.set("playMode", current.play_mode);
  out_json.set("vlcFullscreen", current.vlc_fullscreen);
  out_json.set("hwDecode", current.hw_decode);

  if (!write_file_atomic(file_for("settings"), out_json.dump(2))) {
    return fail("could not write " + file_for("settings"));
  }
  if (out) *out = current;
  LOG_INFO("settings saved: %zu folder(s), hwDecode=%s", current.library_folders.size(),
           current.hw_decode ? "on" : "off");
  return true;
}

Catalog Store::load_catalog() const {
  Catalog catalog;
  std::string text;
  if (!read_file(file_for("catalog"), &text)) return catalog;
  Json root = Json::parseOr(text, Json::object());
  catalog.scanned_at = root.integer("scannedAt", 0);
  if (const Json* folders = root.find("folders"); folders && folders->isArray()) {
    for (const auto& f : folders->items()) {
      if (f.isString()) catalog.folders.push_back(f.asString());
    }
  }
  if (const Json* tools = root.find("tools"); tools && tools->isObject()) {
    catalog.have_ffmpeg = tools->flag("ffmpeg");
    catalog.have_ffprobe = tools->flag("ffprobe");
    catalog.can_render = tools->flag("canRender");
  }
  if (const Json* errors = root.find("errors"); errors && errors->isArray()) {
    for (const auto& e : errors->items()) {
      catalog.errors.push_back(e.isString() ? e.asString() : e.str("message"));
    }
  }
  if (const Json* items = root.find("items"); items && items->isArray()) {
    for (const auto& j : items->items()) {
      if (!j.isObject()) continue;
      Item item;
      item.id = j.str("id");
      item.title = j.str("title");
      item.year = static_cast<int>(j.integer("year", 0));
      item.synopsis = j.str("synopsis");
      item.rating = j.str("rating");
      item.accent = j.str("accent");
      item.path = j.str("path");
      item.filename = j.str("filename");
      item.size = j.integer("size", 0);
      item.mtime_ms = j.integer("mtimeMs", 0);
      item.added_at = j.integer("addedAt", 0);
      item.duration = j.num("duration", 0);
      item.width = static_cast<int>(j.integer("width", 0));
      item.height = static_cast<int>(j.integer("height", 0));
      item.vcodec = j.str("vcodec");
      item.acodec = j.str("acodec");
      item.pix_fmt = j.str("pixFmt");
      item.poster = j.str("poster");
      item.backdrop = j.str("backdrop");
      item.preview = j.str("preview");
      item.preview_version = static_cast<int>(j.integer("previewVersion", 0));
      item.art_attempts = static_cast<int>(j.integer("artAttempts", 0));
      item.art_error = j.str("artError");
      item.unreadable = j.flag("unreadable", false);
      if (const Json* genres = j.find("genres"); genres && genres->isArray()) {
        for (const auto& g : genres->items()) {
          if (g.isString()) item.genres.push_back(g.asString());
        }
      }
      if (const Json* subs = j.find("subtitles"); subs && subs->isArray()) {
        for (const auto& s : subs->items()) {
          SubtitleTrack track;
          track.name = s.str("name");
          track.label = s.str("label");
          track.lang = s.str("lang");
          if (!track.name.empty()) item.subtitles.push_back(track);
        }
      }
      if (item.id.empty() || item.path.empty()) continue;
      catalog.items.push_back(std::move(item));
    }
  }
  return catalog;
}

bool Store::save_catalog(const Catalog& catalog) const {
  Json root = Json::object();
  root.set("scannedAt", catalog.scanned_at);
  Json folders = Json::array();
  for (const auto& f : catalog.folders) folders.push(Json(f));
  root.set("folders", folders);
  Json tools = Json::object();
  tools.set("ffmpeg", catalog.have_ffmpeg);
  tools.set("ffprobe", catalog.have_ffprobe);
  tools.set("canRender", catalog.can_render);
  root.set("tools", tools);
  Json errors = Json::array();
  for (const auto& e : catalog.errors) errors.push(Json(e));
  root.set("errors", errors);
  Json items = Json::array();
  for (const auto& item : catalog.items) {
    Json j = Json::object();
    j.set("id", item.id);
    j.set("title", item.title);
    if (item.year) j.set("year", item.year);
    if (!item.synopsis.empty()) j.set("synopsis", item.synopsis);
    Json genres = Json::array();
    for (const auto& g : item.genres) genres.push(Json(g));
    j.set("genres", genres);
    if (!item.rating.empty()) j.set("rating", item.rating);
    if (!item.accent.empty()) j.set("accent", item.accent);
    j.set("path", item.path);
    j.set("filename", item.filename);
    j.set("size", static_cast<long long>(item.size));
    j.set("mtimeMs", static_cast<long long>(item.mtime_ms));
    j.set("addedAt", static_cast<long long>(item.added_at));
    j.set("duration", item.duration);
    j.set("width", item.width);
    j.set("height", item.height);
    if (!item.vcodec.empty()) j.set("vcodec", item.vcodec);
    if (!item.acodec.empty()) j.set("acodec", item.acodec);
    if (!item.pix_fmt.empty()) j.set("pixFmt", item.pix_fmt);
    Json subs = Json::array();
    for (const auto& track : item.subtitles) {
      Json s = Json::object();
      s.set("name", track.name);
      s.set("label", track.label);
      s.set("lang", track.lang);
      subs.push(s);
    }
    j.set("subtitles", subs);
    if (!item.poster.empty()) j.set("poster", item.poster);
    if (!item.backdrop.empty()) j.set("backdrop", item.backdrop);
    if (!item.preview.empty()) j.set("preview", item.preview);
    j.set("previewVersion", item.preview_version);
    if (item.art_attempts) j.set("artAttempts", item.art_attempts);
    if (!item.art_error.empty()) j.set("artError", item.art_error);
    if (item.unreadable) j.set("unreadable", true);
    items.push(j);
  }
  root.set("items", items);
  const bool ok = write_file_atomic(file_for("catalog"), root.dump(2));
  if (!ok) LOG_ERROR("could not write %s", file_for("catalog").c_str());
  return ok;
}

std::map<std::string, Progress> Store::load_progress() const {
  std::map<std::string, Progress> out;
  std::string text;
  if (!read_file(file_for("progress"), &text)) return out;
  Json root = Json::parseOr(text, Json::object());
  for (const auto& kv : root.members()) {
    Progress p;
    p.position = kv.second.num("position", 0);
    p.duration = kv.second.num("duration", 0);
    p.updated_at = kv.second.integer("updatedAt", 0);
    out[kv.first] = p;
  }
  return out;
}

bool Store::save_progress(const std::map<std::string, Progress>& progress) const {
  return write_file_atomic(file_for("progress"), progress_to_json(progress).dump(2));
}

bool Store::set_progress(const std::string& id, double position, double duration) {
  auto all = load_progress();
  const double ratio = position / std::max(duration, 1.0);
  if (ratio < 0.02 || ratio >= 0.92) {
    // Not started, or finished. Either way there is nothing to resume, and a
    // finished film must leave Continue Watching (audit A4).
    all.erase(id);
    LOG_DEBUG("progress %s cleared (ratio %.3f)", id.c_str(), ratio);
  } else {
    Progress p;
    p.position = position;
    p.duration = duration;
    p.updated_at = now_ms();
    all[id] = p;
  }
  return save_progress(all);
}

std::vector<std::string> Store::load_my_list() const {
  std::vector<std::string> out;
  std::string text;
  if (!read_file(file_for("myList"), &text)) return out;
  Json root = Json::parseOr(text, Json::array());
  if (!root.isArray()) return out;
  for (const auto& v : root.items()) {
    if (v.isString()) out.push_back(v.asString());
  }
  return out;
}

bool Store::save_my_list(const std::vector<std::string>& list) const {
  Json root = Json::array();
  for (const auto& id : list) root.push(Json(id));
  return write_file_atomic(file_for("myList"), root.dump(2));
}

bool Store::toggle_my_list(const std::string& id, std::vector<std::string>* out) const {
  auto list = load_my_list();
  const auto it = std::find(list.begin(), list.end(), id);
  if (it != list.end()) {
    list.erase(it);
  } else {
    list.push_back(id);
  }
  const bool ok = save_my_list(list);
  if (out) *out = list;
  return ok;
}

std::map<std::string, std::string> Store::load_subtitle_choices() const {
  std::map<std::string, std::string> out;
  std::string text;
  if (!read_file(file_for("subtitleChoices"), &text)) return out;
  Json root = Json::parseOr(text, Json::object());
  for (const auto& kv : root.members()) {
    if (kv.second.isString()) out[kv.first] = kv.second.asString();
  }
  return out;
}

bool Store::set_subtitle_choice(const std::string& id, const std::string& name) const {
  auto all = load_subtitle_choices();
  if (name.empty()) {
    all.erase(id);
  } else {
    all[id] = name;
  }
  Json root = Json::object();
  for (const auto& kv : all) root.set(kv.first, Json(kv.second));
  return write_file_atomic(file_for("subtitleChoices"), root.dump(2));
}

}  // namespace vf
