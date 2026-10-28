#include "core/naming.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

namespace vf {
namespace {

const std::set<std::string>& tag_noise() {
  static const std::set<std::string> kTags = {
      "1080p", "2160p", "720p", "480p", "4k", "uhd", "hdr", "hdr10", "dv", "sdr",
      "bluray", "blu-ray", "brrip", "bdrip", "webrip", "web-dl", "webdl", "web",
      "hdtv", "dvdrip", "dvdscr", "hdrip", "remux", "x264", "x265", "h264", "h265",
      "hevc", "avc", "aac", "ac3", "dts", "ddp", "dd5", "truehd", "atmos",
      "10bit", "8bit", "proper", "repack", "extended", "unrated", "remastered",
      "multi", "dual", "audio", "subs", "subbed", "dubbed", "internal"};
  return kTags;
}

const std::set<std::string>& small_words() {
  static const std::set<std::string> kSmall = {"a", "an", "and", "as", "at", "but", "by",
                                               "for", "in", "of", "on", "or", "the", "to",
                                               "vs", "with"};
  return kSmall;
}

std::string strip_brackets(const std::string& word) {
  std::size_t begin = 0;
  std::size_t end = word.size();
  while (begin < end && (word[begin] == '[' || word[begin] == '(')) ++begin;
  while (end > begin && (word[end - 1] == ']' || word[end - 1] == ')')) --end;
  return word.substr(begin, end - begin);
}

bool all_digits(const std::string& s) {
  if (s.empty()) return false;
  for (unsigned char c : s) {
    if (!std::isdigit(c)) return false;
  }
  return true;
}

}  // namespace

std::string fold_token(const std::string& token) {
  std::string out;
  out.reserve(token.size());
  for (unsigned char c : token) {
    if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

bool is_release_tag(const std::string& token) { return tag_noise().count(fold_token(token)) > 0; }

bool looks_like_year(const std::string& token) {
  const std::string word = strip_brackets(token);
  if (word.size() != 4 || !all_digits(word)) return false;
  const int value = std::stoi(word);
  return value >= 1900 && value <= 2099;
}

std::string title_case(const std::vector<std::string>& words) {
  std::string out;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const std::string& raw = words[i];
    if (raw.empty()) continue;
    std::string word = raw;
    const bool has_digit =
        std::any_of(word.begin(), word.end(), [](unsigned char c) { return std::isdigit(c); });
    bool shouty = word.size() > 1;
    for (unsigned char c : word) {
      if (std::isalpha(c) && !std::isupper(c)) shouty = false;
    }
    if (!has_digit && !shouty) {
      std::string lower = word;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (i > 0 && small_words().count(lower)) {
        word = lower;
      } else {
        lower[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(lower[0])));
        word = lower;
      }
    }
    if (!out.empty()) out.push_back(' ');
    out += word;
  }
  return out;
}

ParsedName parse_name(const std::string& filename) {
  ParsedName result;

  // 1. Drop the extension and split on the separators real libraries use.
  std::string base = filename;
  const std::size_t dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);

  std::string spaced;
  spaced.reserve(base.size());
  for (char c : base) {
    if (c == '.' || c == '_' || c == '-') {
      spaced.push_back(' ');
    } else {
      spaced.push_back(c);
    }
  }

  std::vector<std::string> tokens;
  {
    std::string current;
    for (char c : spaced) {
      if (c == ' ' || c == '\t') {
        if (!current.empty()) {
          tokens.push_back(current);
          current.clear();
        }
      } else {
        current.push_back(c);
      }
    }
    if (!current.empty()) tokens.push_back(current);
  }
  if (tokens.empty()) {
    result.title = base.empty() ? filename : base;
    return result;
  }

  // 2. Everything from the first release tag onwards is noise.
  std::size_t head = tokens.size();
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (is_release_tag(tokens[i])) {
      head = i;
      break;
    }
  }
  if (head == 0) head = tokens.size();  // a tag-looking first word is the title

  // 3. The year is the LAST year-shaped token in the head, and only if a title
  //    survives without it.
  std::size_t year_index = head;  // head = none
  for (std::size_t i = 0; i < head; ++i) {
    if (looks_like_year(tokens[i])) year_index = i;
  }

  std::vector<std::string> title_tokens;
  if (year_index != head && year_index + 1 == head && year_index >= 1) {
    // Normal case: "[Title...] [Year]".
    for (std::size_t i = 0; i < year_index; ++i) title_tokens.push_back(strip_brackets(tokens[i]));
    result.year = std::stoi(strip_brackets(tokens[year_index]));
  } else if (year_index != head && year_index + 1 < head) {
    // More tokens after the year inside the head (e.g. "Movie 2019 Director Cut"):
    // keep them in the title, take the year out of the middle.
    for (std::size_t i = 0; i < head; ++i) {
      if (i == year_index) {
        result.year = std::stoi(strip_brackets(tokens[i]));
        continue;
      }
      title_tokens.push_back(strip_brackets(tokens[i]));
    }
  } else {
    // No usable year, or the year *is* the title ("1917", "2012").
    for (std::size_t i = 0; i < head; ++i) title_tokens.push_back(strip_brackets(tokens[i]));
    if (year_index != head && year_index == 0) {
      // The only year-shaped token is the first token and it is all we have:
      // it stays in the title, and the real release year is unknown.
      result.year = 0;
    }
  }

  title_tokens.erase(std::remove_if(title_tokens.begin(), title_tokens.end(),
                                    [](const std::string& t) { return t.empty(); }),
                     title_tokens.end());

  if (title_tokens.empty()) {
    // Never return an empty title: fall back to the filename itself.
    result.title = base.empty() ? filename : base;
  } else {
    result.title = title_case(title_tokens);
  }
  return result;
}

}  // namespace vf
