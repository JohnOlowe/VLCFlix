// The rows the home screen is built from, and the "is this finished" rule.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/model.h"

namespace vf {

struct Rail {
  std::string id;
  std::string title;
  std::vector<std::size_t> items;  // indices into the catalog
};

// Order matches buildRails() in server/index.js: Continue Watching, Recently
// Added, My List, genres with at least two films, then everything.
std::vector<Rail> build_rails(const std::vector<Item>& items,
                              const std::map<std::string, Progress>& progress,
                              const std::vector<std::string>& my_list);

int progress_percent(const Progress& progress);
bool is_finished(const Progress& progress);

// Simple search across title, genres and year, used by the search view and by the
// headless tests.
std::vector<std::size_t> search_items(const std::vector<Item>& items, const std::string& query);

}  // namespace vf
