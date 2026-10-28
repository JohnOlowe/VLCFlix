// Listing folders so the user can browse to one instead of typing its path.
//
// All of this is plain std::filesystem with no UI in it, so a temporary tree can be
// listed and navigated in a test. The two platform differences that matter are handled
// here as well: Windows has drive letters to offer, POSIX has a single root.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace vf {

struct FolderListing {
  // Absolute, normalized, and never empty when `ok` is true.
  std::string path;
  // Subdirectory names only - a library folder is a folder, not a file.
  std::vector<std::string> folders;
  // True when the listing was cut off at `limit`.
  bool truncated = false;
  // Set when the directory could not be read; `folders` is then empty.
  std::string error;
  bool ok() const { return error.empty(); }
};

// Lists the subdirectories of `path`, sorted, skipping names that start with a dot (on
// Windows: also the hidden/system attribute is not consulted - dot names are the
// portable subset). A relative or empty path is resolved to an absolute one first.
FolderListing list_folders(const std::string& path, std::size_t limit = 500);

// The folder above this one, or "" when it is already a root ("C:\", "/").
std::string parent_folder(const std::string& path);

// Join a child name onto a folder, with exactly one separator.
std::string join_folder(const std::string& parent, const std::string& child);

// Where a browse should start: the first library folder that still exists, then the
// user's home, then the root of the current working directory.
std::string browse_start(const std::vector<std::string>& library_folders);

// Drive letters (Windows, only those that exist) or "/" (POSIX).
std::vector<std::string> folder_roots();

// True when the path exists and is a directory.
bool is_folder(const std::string& path);

}  // namespace vf
