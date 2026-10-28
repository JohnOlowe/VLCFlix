#include "core/paths.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "core/brand.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vf {
namespace {

std::string g_data_override;

std::string env_or_empty(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

}  // namespace

void set_data_dir(const std::string& path) {
  g_data_override = path;
}

std::string data_dir() {
  if (!g_data_override.empty()) return absolute_path(g_data_override);
  const std::string env = env_or_empty("VLCFLIX_STUDIO_DATA");
  if (!env.empty()) return absolute_path(env);
#ifdef _WIN32
  const std::string local = env_or_empty("LOCALAPPDATA");
  if (!local.empty()) return join(local, kDataDirName);
#elif defined(__APPLE__)
  const std::string home = home_dir();
  if (!home.empty()) return join(join(home, "Library/Application Support"), kDataDirName);
#else
  const std::string xdg = env_or_empty("XDG_DATA_HOME");
  if (!xdg.empty()) return join(xdg, kDataDirName);
  const std::string home = home_dir();
  if (!home.empty()) return join(join(home, ".local/share"), kDataDirName);
#endif
  return absolute_path("data");
}

std::string media_dir() { return join(data_dir(), "media"); }

std::string home_dir() {
#ifdef _WIN32
  return env_or_empty("USERPROFILE");
#else
  const std::string h = env_or_empty("HOME");
  if (!h.empty()) return h;
  return "/tmp";
#endif
}

std::string temp_dir() {
  const std::string t = env_or_empty("TMPDIR");
  if (!t.empty()) return t;
#ifdef _WIN32
  const std::string w = env_or_empty("TEMP");
  if (!w.empty()) return w;
  return ".\\";
#else
  return "/tmp";
#endif
}

std::string exe_dir() {
#ifdef _WIN32
  char buf[MAX_PATH] = {0};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n == 0) return ".";
  return dirname(std::string(buf, n));
#else
  char buf[4096] = {0};
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return ".";
  return dirname(std::string(buf, static_cast<std::size_t>(n)));
#endif
}

std::string logs_dir() {
#ifdef _WIN32
  const std::string local = env_or_empty("LOCALAPPDATA");
  if (!local.empty()) return join(join(local, kDataDirName), "logs");
#elif defined(__APPLE__)
  const std::string home = home_dir();
  if (!home.empty()) return join(join(home, "Library/Logs"), kDataDirName);
#else
  const std::string state = env_or_empty("XDG_STATE_HOME");
  if (!state.empty()) return join(join(state, kDataDirName), "logs");
  const std::string home = home_dir();
  if (!home.empty()) return join(join(home, ".local/state"), kDataDirName) + "/logs";
#endif
  return join(data_dir(), "logs");
}

std::vector<std::string> log_dir_candidates() {
  // Preference order. log.cpp tries each in turn: an app that cannot write to the
  // user-data directory (a locked profile, a read-only image) still produces a log.
  return {logs_dir(), join(exe_dir(), "logs"), join(temp_dir(), kProductSlug)};
}

bool ensure_dir(const std::string& path) {
  std::error_code ec;
  if (path.empty()) return false;
  if (fs::exists(path, ec)) return fs::is_directory(path, ec);
  fs::create_directories(path, ec);
  return !ec && fs::is_directory(path, ec);
}

bool file_exists(const std::string& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

bool dir_exists(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

long long file_size(const std::string& path) {
  std::error_code ec;
  const auto n = fs::file_size(path, ec);
  if (ec) return -1;
  return static_cast<long long>(n);
}

long long file_mtime_ms(const std::string& path) {
  std::error_code ec;
  const auto t = fs::last_write_time(path, ec);
  if (ec) return 0;
  const auto since = t.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
}

bool read_file(const std::string& path, std::string* out) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::string data;
  char buf[65536];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
  std::fclose(f);
  if (out) *out = std::move(data);
  return true;
}

std::string join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  const char last = a.back();
  if (last == '/' || last == '\\') return a + b;
#ifdef _WIN32
  return a + "\\" + b;
#else
  return a + "/" + b;
#endif
}

std::string basename(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string dirname(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  if (pos == 0) return path.substr(0, 1);
  return path.substr(0, pos);
}

std::string extension(const std::string& path) {
  const std::string base = basename(path);
  const std::size_t pos = base.find_last_of('.');
  if (pos == std::string::npos || pos == 0) return "";
  std::string ext = base.substr(pos);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

std::string stem(const std::string& path) {
  const std::string base = basename(path);
  const std::size_t pos = base.find_last_of('.');
  if (pos == std::string::npos || pos == 0) return base;
  return base.substr(0, pos);
}

std::string absolute_path(const std::string& path) {
  std::error_code ec;
  const auto p = fs::absolute(fs::path(path), ec);
  if (ec) return path;
  return p.lexically_normal().string();
}

bool write_file_atomic(const std::string& path, const std::string& content) {
  const std::string dir = dirname(path);
  if (!dir.empty() && !ensure_dir(dir)) return false;
  const std::string tmp = path + ".tmp";
  {
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const std::size_t n = std::fwrite(content.data(), 1, content.size(), f);
    const bool flushed = std::fflush(f) == 0;
    std::fclose(f);
    if (n != content.size() || !flushed) {
      std::remove(tmp.c_str());
      return false;
    }
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

bool remove_file(const std::string& path) {
  std::error_code ec;
  return fs::remove(path, ec);
}

std::vector<std::string> list_dir(const std::string& path) {
  std::vector<std::string> out;
  std::error_code ec;
  fs::directory_iterator it(path, ec), end;
  if (ec) return out;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    out.push_back(it->path().filename().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string find_dir_upwards(const std::string& name, int max_levels) {
  std::vector<std::string> starts = {absolute_path("."), exe_dir()};
  for (const auto& start : starts) {
    std::string current = start;
    for (int level = 0; level <= max_levels; ++level) {
      const std::string candidate = join(current, name);
      if (dir_exists(candidate)) return absolute_path(candidate);
      const std::string parent = dirname(current);
      if (parent == current || parent.empty()) break;
      current = parent;
    }
  }
  return "";
}

}  // namespace vf
