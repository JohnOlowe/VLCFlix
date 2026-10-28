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
