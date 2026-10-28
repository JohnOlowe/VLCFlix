#include "library/subtitles.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "core/paths.h"

namespace vf {
namespace {

const std::map<std::string, std::string>& language_names() {
  static const std::map<std::string, std::string> kNames = {
      {"en", "English"},   {"eng", "English"},  {"fr", "French"},    {"fre", "French"},
      {"fra", "French"},   {"es", "Spanish"},   {"spa", "Spanish"},  {"de", "German"},
      {"ger", "German"},   {"deu", "German"},   {"it", "Italian"},   {"ita", "Italian"},
      {"pt", "Portuguese"},{"por", "Portuguese"},{"ru", "Russian"},  {"rus", "Russian"},
      {"ar", "Arabic"},    {"ara", "Arabic"},   {"zh", "Chinese"},   {"chi", "Chinese"},
      {"zho", "Chinese"},  {"ja", "Japanese"},  {"jpn", "Japanese"}, {"ko", "Korean"},
      {"kor", "Korean"},   {"hi", "Hindi"},     {"hin", "Hindi"},    {"nl", "Dutch"},
      {"pl", "Polish"},    {"tr", "Turkish"},   {"sv", "Swedish"},   {"no", "Norwegian"},
      {"da", "Danish"},    {"fi", "Finnish"},   {"yo", "Yoruba"},    {"yor", "Yoruba"},
      {"ig", "Igbo"},      {"ibo", "Igbo"},     {"ha", "Hausa"},     {"hau", "Hausa"}};
  return kNames;
}

bool is_subtitle_file(const std::string& name) {
  const std::string ext = extension(name);
  return ext == ".srt" || ext == ".vtt";
}

// "en", "pt-BR", "pt_BR" -> language code, else empty.
std::string language_code_of(const std::string& suffix) {
  std::string lower = suffix;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::size_t sep = lower.find_first_of("-_");
  const std::string head = sep == std::string::npos ? lower : lower.substr(0, sep);
  bool ok = head.size() >= 2 && head.size() <= 3;
  for (unsigned char c : head) {
    if (!std::isalpha(c)) ok = false;
  }
  return ok ? head : std::string();
}

std::string capitalize_words(const std::string& text) {
  std::string out = text;
  bool at_word_start = true;
  for (char& c : out) {
    if (c == '-' || c == '_' || c == ' ') {
      c = ' ';
      at_word_start = true;
    } else if (at_word_start) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      at_word_start = false;
    }
  }
  return out;
}

bool matches_convention(const std::string& stem, const std::string& video_stem) {
  return stem == video_stem || stem.rfind(video_stem + ".", 0) == 0;
}

}  // namespace

std::string language_name(const std::string& code) {
  const auto it = language_names().find(code);
  return it == language_names().end() ? std::string() : it->second;
}

std::string label_for(const std::string& base_stem, const std::string& filename) {
  const std::string stem_name = stem(filename);
  if (stem_name == base_stem) return "Default";
  std::string suffix = stem_name;
  if (stem_name.rfind(base_stem + ".", 0) == 0) suffix = stem_name.substr(base_stem.size() + 1);
  std::string lower = suffix;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string lang = language_code_of(lower);
  const std::string named = language_name(lower);
  if (!named.empty()) return named;
  const std::string by_lang = language_name(lang);
  if (!by_lang.empty()) return by_lang;
  return suffix;
}

std::vector<SubtitleTrack> find_subtitles(const std::string& video_path,
                                          const std::vector<std::string>& folder_entries) {
  const std::string base = stem(video_path);
  std::vector<SubtitleTrack> tracks;
  for (const auto& name : folder_entries) {
    if (!is_subtitle_file(name)) continue;
    const std::string file_stem = stem(name);
    if (!matches_convention(file_stem, base)) continue;

    SubtitleTrack track;
    track.name = name;
    const std::string suffix =
        file_stem == base ? std::string() : file_stem.substr(base.size() + 1);
    const std::string lang = language_code_of(suffix);
    track.lang = lang;
    if (suffix.empty()) {
      track.label = "Default";
    } else {
      // A language suffix gets its language name ("English"); anything else gets
      // its own words capitalised ("forced" -> "Forced"), as library.js did.
      const std::string label = label_for(base, name);
      track.label = (label == suffix) ? capitalize_words(suffix) : label;
    }
    track.matches = true;
    tracks.push_back(track);
  }
  // A plain Movie.srt is what people expect on by default, so it sorts first.
  std::sort(tracks.begin(), tracks.end(), [](const SubtitleTrack& a, const SubtitleTrack& b) {
    const bool a_default = a.label == "Default";
    const bool b_default = b.label == "Default";
    if (a_default != b_default) return a_default;
    return a.label < b.label;
  });
  return tracks;
}

std::vector<SubtitleTrack> list_folder_subtitles(const std::string& video_path,
                                                 const std::vector<std::string>& folder_entries) {
  const std::string base = stem(video_path);
  std::vector<SubtitleTrack> found;
  for (const auto& name : folder_entries) {
    if (!is_subtitle_file(name)) continue;
    SubtitleTrack track;
    track.name = name;
    track.label = label_for(base, name);
    track.lang = language_code_of(stem(name).substr(base.size() + 1 > stem(name).size()
                                                       ? 0
                                                       : std::min(base.size() + 1, stem(name).size())));
    track.matches = matches_convention(stem(name), base);
    found.push_back(track);
  }
  std::sort(found.begin(), found.end(), [](const SubtitleTrack& a, const SubtitleTrack& b) {
    if (a.matches != b.matches) return a.matches;
    return a.name < b.name;
  });
  return found;
}

/* ------------------------------- cues ---------------------------------- */

namespace {

double parse_stamp(const std::string& text) {
  // HH:MM:SS.mmm or MM:SS.mmm (both separators accepted: SRT uses ',').
  std::vector<double> parts;
  std::string current;
  for (char c : text) {
    if (c == ':' || c == '.' || c == ',') {
      if (!current.empty()) {
        parts.push_back(std::stod(current));
        current.clear();
      }
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(std::stod(current));
  if (parts.size() == 4) return parts[0] * 3600 + parts[1] * 60 + parts[2] + parts[3] / 1000.0;
  if (parts.size() == 3) return parts[0] * 60 + parts[1] + parts[2] / 1000.0;
  if (parts.size() == 2) return parts[0] + parts[1] / 1000.0;
  return 0;
}

std::string trim(const std::string& s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  return s.substr(begin, end - begin);
}

bool is_cue_number(const std::string& line) {
  if (line.empty()) return false;
  for (unsigned char c : line) {
    if (!std::isdigit(c) && c != ' ') return false;
  }
  return true;
}

}  // namespace

std::vector<Cue> parse_cues(const std::string& text) {
  std::vector<Cue> cues;
  std::string body = text;
  // Normalise newlines and strip a BOM.
  std::string normalised;
  normalised.reserve(body.size());
  for (std::size_t i = 0; i < body.size(); ++i) {
    if (body[i] == '\r') {
      if (i + 1 < body.size() && body[i + 1] == '\n') continue;
      normalised.push_back('\n');
    } else if (!(i == 0 && static_cast<unsigned char>(body[i]) == 0xEF)) {
      normalised.push_back(body[i]);
    }
  }

  Cue current;
  bool in_cue = false;
  std::string buffer;
  bool saw_webvtt = false;

  auto flush = [&]() {
    if (in_cue) {
      current.text = trim(buffer);
      if (current.end > current.start && !current.text.empty()) cues.push_back(current);
    }
    in_cue = false;
    buffer.clear();
  };

  std::size_t pos = 0;
  while (pos <= normalised.size()) {
    const std::size_t nl = normalised.find('\n', pos);
    const std::string line =
        normalised.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? normalised.size() + 1 : nl + 1;

    if (!saw_webvtt && line.rfind("WEBVTT", 0) == 0) {
      saw_webvtt = true;
      continue;
    }
    const std::size_t arrow = line.find("-->");
    if (arrow != std::string::npos) {
      flush();
      const std::string from = trim(line.substr(0, arrow));
      std::string to = trim(line.substr(arrow + 3));
      // Cue settings ("position:50% align:middle") live after the end stamp.
      const std::size_t space = to.find(' ');
      if (space != std::string::npos) to = to.substr(0, space);
      current = Cue{};
      current.start = parse_stamp(from);
      current.end = parse_stamp(to);
      in_cue = true;
      continue;
    }
    if (trim(line).empty()) {
      flush();
      continue;
    }
    if (in_cue) {
      if (!buffer.empty()) buffer.push_back('\n');
      buffer += line;
    } else if (is_cue_number(line)) {
      // SRT cue index: skip it, the timing line follows.
      continue;
    }
  }
  flush();
  return cues;
}

}  // namespace vf
