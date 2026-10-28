#include "core/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace vf {
namespace {

std::string lower(const std::string& text) {
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string slug(const std::string& text) {
  std::string out;
  bool last_dash = false;
  for (unsigned char c : lower(text)) {
    if (std::isalnum(c)) {
      out.push_back(static_cast<char>(c));
      last_dash = false;
    } else if (!last_dash && !out.empty()) {
      out.push_back('-');
      last_dash = true;
    }
  }
  if (!out.empty() && out.back() == '-') out.pop_back();
  return out;
}

}  // namespace

int progress_percent(const Progress& progress) {
  if (progress.duration <= 0) return 0;
  const double percent = (progress.position / progress.duration) * 100.0;
  return static_cast<int>(std::min(100.0, percent + 0.5));
}

bool is_finished(const Progress& progress) {
  if (progress.duration <= 0) return false;
  return progress.position / progress.duration >= 0.92;
}

std::vector<Rail> build_rails(const std::vector<Item>& items,
                              const std::map<std::string, Progress>& progress,
                              const std::vector<std::string>& my_list) {
  std::vector<Rail> rails;

  // Continue Watching: anything with a resume point, most recent first. Finished
  // films never reach here because set_progress removes them (audit A4).
  {
    Rail rail{"continue", "Continue Watching", {}};
    std::vector<std::size_t> watching;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const auto it = progress.find(items[i].id);
      if (it == progress.end() || is_finished(it->second)) continue;
      watching.push_back(i);
    }
    std::sort(watching.begin(), watching.end(), [&](std::size_t a, std::size_t b) {
      const auto pa = progress.find(items[a].id);
      const auto pb = progress.find(items[b].id);
      const long long ta = pa == progress.end() ? 0 : pa->second.updated_at;
      const long long tb = pb == progress.end() ? 0 : pb->second.updated_at;
      return ta > tb;
    });
    if (!watching.empty()) {
      rail.items = watching;
      rails.push_back(rail);
    }
  }

  // Recently Added: newest first, capped at twelve.
  {
    Rail rail{"new", "Recently Added", {}};
    std::vector<std::size_t> order(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return items[a].added_at > items[b].added_at; });
    for (std::size_t i = 0; i < order.size() && i < 12; ++i) rail.items.push_back(order[i]);
    if (!rail.items.empty()) rails.push_back(rail);
  }

  // My List, in the order the user added them.
  if (!my_list.empty()) {
    Rail rail{"mylist", "My List", {}};
    for (const auto& id : my_list) {
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].id == id) {
          rail.items.push_back(i);
          break;
        }
      }
    }
    if (!rail.items.empty()) rails.push_back(rail);
  }

  // Genres with at least two films, most populated first.
  {
    std::map<std::string, std::vector<std::size_t>> by_genre;
    for (std::size_t i = 0; i < items.size(); ++i) {
      for (const auto& genre : items[i].genres) by_genre[genre].push_back(i);
    }
    std::vector<std::pair<std::string, std::vector<std::size_t>>> ranked;
    for (auto& kv : by_genre) {
      if (kv.second.size() >= 2) ranked.emplace_back(kv.first, kv.second);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
      if (a.second.size() != b.second.size()) return a.second.size() > b.second.size();
      return a.first < b.first;
    });
    for (auto& entry : ranked) {
      Rail rail;
      rail.id = "genre-" + slug(entry.first);
      rail.title = entry.first;
      for (std::size_t i = 0; i < entry.second.size() && i < 12; ++i) {
        rail.items.push_back(entry.second[i]);
      }
      rails.push_back(rail);
    }
  }

  if (!items.empty()) {
    Rail rail{"all", "Everything in Your Library", {}};
    for (std::size_t i = 0; i < items.size(); ++i) rail.items.push_back(i);
    rails.push_back(rail);
  }
  return rails;
}

std::vector<std::size_t> search_items(const std::vector<Item>& items, const std::string& query) {
  const std::string needle = lower(query);
  std::vector<std::size_t> found;
  if (needle.empty()) return found;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const Item& item = items[i];
    bool hit = lower(item.title).find(needle) != std::string::npos;
    if (!hit && item.year) hit = std::to_string(item.year) == needle;
    if (!hit) {
      for (const auto& genre : item.genres) {
        if (lower(genre).find(needle) != std::string::npos) {
          hit = true;
          break;
        }
      }
    }
    if (!hit) hit = lower(item.synopsis).find(needle) != std::string::npos;
    if (hit) found.push_back(i);
  }
  return found;
}

}  // namespace vf
