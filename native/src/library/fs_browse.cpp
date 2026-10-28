#include "library/fs_browse.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace vf {
namespace {

std::string to_string_path(const fs::path& path) { return path.generic_string(); }

// A dot name is hidden everywhere; on Windows an empty name cannot exist either.
bool is_listable(const fs::directory_entry& entry, std::error_code& ec) {
  const std::string name = entry.path().filename().string();
  if (name.empty() || name.front() == '.') return false;
  ec.clear();
  if (!entry.is_directory(ec) || ec) return false;
  return true;
}

std::string home_folder() {
  for (const char* variable : {"HOME", "USERPROFILE"}) {
    const char* value = std::getenv(variable);
    if (value && *value && is_folder(value)) return value;
  }
  return std::string();
}

}  // namespace

bool is_folder(const std::string& path) {
  if (path.empty()) return false;
  std::error_code ec;
  return fs::is_directory(fs::path(path), ec) && !ec;
}

FolderListing list_folders(const std::string& path, std::size_t limit) {
  FolderListing listing;
  std::error_code ec;
  fs::path target = path.empty() ? fs::current_path(ec) : fs::path(path);
  if (ec) {
    listing.error = "cannot resolve " + path + ": " + ec.message();
    return listing;
  }
  // A relative path is resolved once, here, so the sheet always shows something
  // absolute that can be stored as a library folder.
  target = fs::absolute(target, ec);
  if (ec) {
    listing.error = "cannot resolve " + path + ": " + ec.message();
    return listing;
  }
  target = target.lexically_normal();
  listing.path = to_string_path(target);

  if (!fs::exists(target, ec) || ec) {
    listing.error = "no such folder: " + listing.path;
    return listing;
  }
  if (!fs::is_directory(target, ec) || ec) {
    listing.error = "not a folder: " + listing.path;
    return listing;
  }

  // Permission problems surface as an error_code from the iterator rather than an
  // exception, which is what keeps an unreadable network share from killing the app.
  fs::directory_iterator it(target, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    listing.error = "cannot read " + listing.path + ": " + ec.message();
    return listing;
  }
  for (const auto& entry : it) {
    std::error_code entry_ec;
    if (!is_listable(entry, entry_ec)) continue;
    listing.folders.push_back(entry.path().filename().string());
    if (listing.folders.size() >= limit) {
      listing.truncated = true;
      break;
    }
  }
  std::sort(listing.folders.begin(), listing.folders.end());
  return listing;
}

std::string parent_folder(const std::string& path) {
  if (path.empty()) return std::string();
  const fs::path target = fs::path(path).lexically_normal();
  if (target == target.root_path()) return std::string();  // already at a drive or "/"
  const fs::path parent = target.parent_path();
  if (parent.empty() || parent == target) return std::string();
  return to_string_path(parent);
}

std::string join_folder(const std::string& parent, const std::string& child) {
  if (parent.empty()) return child;
  if (child.empty()) return parent;
  fs::path result(parent);
  result /= child;
  return to_string_path(result.lexically_normal());
}

std::string browse_start(const std::vector<std::string>& library_folders) {
  for (const auto& folder : library_folders) {
    if (is_folder(folder)) {
      std::error_code ec;
      return to_string_path(fs::absolute(fs::path(folder), ec).lexically_normal());
    }
  }
  const std::string home = home_folder();
  if (!home.empty()) {
    std::error_code ec;
    return to_string_path(fs::absolute(fs::path(home), ec).lexically_normal());
  }
  std::error_code ec;
  return to_string_path(fs::current_path(ec));
}

std::vector<std::string> folder_roots() {
  std::vector<std::string> roots;
#ifdef _WIN32
  // Every drive that is actually there: fixed disks, USB sticks, mapped shares. A
  // folder picker that cannot reach D: is not a picker.
  const DWORD mask = GetLogicalDrives();
  for (int letter = 0; letter < 26; ++letter) {
    if ((mask & (1u << letter)) == 0) continue;
    std::string root;
    root.push_back(static_cast<char>('A' + letter));
    root += ":\\";
    if (is_folder(root)) roots.push_back(root);
  }
#endif
  if (roots.empty()) roots.push_back("/");
  return roots;
}

}  // namespace vf
