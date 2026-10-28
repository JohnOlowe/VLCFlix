// Turning a filename into a title and a year.
//
// Ported from server/library.js:parseName with one deliberate fix. The JS version
// took the FIRST 19xx/20xx token as the year and stopped the title there, which
// destroys films whose title *is* a year and picks the wrong year when the title
// contains one:
//
//   1917.2019.2160p.WEB-DL  ->  title "1917 2019 2160p WEB-DL", year 1917   (wrong)
//   Blade.Runner.2049.2017  ->  year 2049                                  (wrong)
//
// The rule now: release tags end the title, and the year is the LAST year-shaped
// token before that boundary. A title is only stripped of a year if something is
// left behind, so "1917" stays "1917".
#pragma once
#include <string>
#include <vector>

namespace vf {

struct ParsedName {
  std::string title;
  int year = 0;  // 0 = no year found
};

ParsedName parse_name(const std::string& filename);

// Lower-cased, punctuation-stripped token used by the tag table.
std::string fold_token(const std::string& token);
bool is_release_tag(const std::string& token);
bool looks_like_year(const std::string& token);
std::string title_case(const std::vector<std::string>& words);

}  // namespace vf
