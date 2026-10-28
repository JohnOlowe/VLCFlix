// SHA-1, because catalog ids are sha1(path)[0..12] and they keys progress and
// My List. Changing the hash would silently orphan every saved resume point, so
// this is not allowed to be "close enough".
#pragma once
#include <string>

namespace vf {

std::string sha1_hex(const std::string& input);
std::string item_id_for_path(const std::string& absolute_path);  // first 12 hex digits

}  // namespace vf
