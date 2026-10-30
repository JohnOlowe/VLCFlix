// The data the app keeps between runs, and the catalog entries it builds.
#pragma once
#include <string>
#include <vector>

namespace vf {

struct SubtitleTrack {
  std::string name;
  std::string label;
  std::string lang;
  bool matches = false;
};

struct Item {
  std::string id;
  std::string title;
  int year = 0;
  std::string synopsis;
  std::vector<std::string> genres;
  std::string rating;
  std::string accent;
  std::string path;
  std::string filename;
  long long size = 0;
  long long mtime_ms = 0;
  long long added_at = 0;
  double duration = 0;
  int width = 0;
  int height = 0;
  std::string vcodec;
  std::string acodec;
  std::string pix_fmt;
  std::vector<SubtitleTrack> subtitles;
  std::string poster;
  std::string backdrop;
  std::string preview;
  int preview_version = 0;
  int art_attempts = 0;
  std::string art_error;
  // The prober could not read this file (a partial download, a truncated file, a
  // container it does not know). It stays in the library so the user can see it and
  // so a later scan can retry it, but the UI marks it and Play refuses it with a
  // reason instead of opening a player that can only show black.
  bool unreadable = false;
};

struct Progress {
  double position = 0;
  double duration = 0;
  long long updated_at = 0;
};

struct Settings {
  std::vector<std::string> library_folders;
  std::string vlc_path;               // retained for compatible settings
  int vlc_http_port = 8080;           // stored compatibility setting
  std::string vlc_password = "vlcflix";
  std::string play_mode = "vlc";      // informational only in the native app
  bool vlc_fullscreen = true;
  bool hw_decode = false;             // now only feeds artwork/preview rendering
  // Two-finger drag, as a fraction of the speed that first worked. 1 is that
  // speed. The mouse wheel does not use this.
  float pad_scroll = 1.0f;
};

// The settings slider slows the gesture from the speed that works, down to 1%.
// The track is logarithmic, so 1% to 10% is the whole left half. It cannot stop
// the gesture. The mouse wheel does not use this.
inline constexpr float kPadScrollMin = 0.01f;
inline constexpr float kPadScrollMax = 1.0f;

struct Catalog {
  long long scanned_at = 0;
  std::vector<std::string> folders;
  bool have_ffmpeg = false;
  bool have_ffprobe = false;
  bool can_render = false;
  std::vector<Item> items;
  std::vector<std::string> errors;
};

}  // namespace vf
