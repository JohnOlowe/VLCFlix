// Sidecar subtitles: which files belong to a film, and their cues.
//
// The browser build converted SRT to WebVTT so a <video> element could consume it.
// The native player draws cues itself, so the conversion step is gone and the
// parser produces cues directly (SRT and WebVTT are read by the same code).
#pragma once
#include <string>
#include <vector>

#include "core/model.h"

namespace vf {

// Files beside the film that follow the naming convention:
//   Movie.srt, Movie.en.srt, Movie.forced.srt
std::vector<SubtitleTrack> find_subtitles(const std::string& video_path,
                                          const std::vector<std::string>& folder_entries);

// Every subtitle file in the folder, name matches first. Downloaded subtitles are
// often named after the release, so the picker offers everything.
std::vector<SubtitleTrack> list_folder_subtitles(const std::string& video_path,
                                                 const std::vector<std::string>& folder_entries);

std::string label_for(const std::string& base_stem, const std::string& filename);
std::string language_name(const std::string& code);

struct Cue {
  double start = 0;
  double end = 0;
  std::string text;  // inline <i>/<b>/<u> kept, everything else escaped on draw
};

// Reads SRT or WebVTT text. Cue settings and identifiers are ignored.
std::vector<Cue> parse_cues(const std::string& text);

}  // namespace vf
