#pragma once
#include <string>
#include <vector>

namespace vf {

// Directory the app stores settings/catalog/progress in.
// Order: --data flag (set via set_data_dir), $VLCFLIX_STUDIO_DATA, then the
// platform's user-data directory, then ./data as a last resort.
std::string data_dir();
void set_data_dir(const std::string& path);

std::string media_dir();   // <data>/media
std::string logs_dir();    // platform log directory for the app
std::string exe_dir();     // directory containing the running binary
std::string temp_dir();

// Candidate log directories, most preferred first. The logger walks this list and
// uses the first it can actually write to (see log.cpp), because a crash before
// the window opens is exactly when a log matters.
std::vector<std::string> log_dir_candidates();

bool ensure_dir(const std::string& path);
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
long long file_size(const std::string& path);        // -1 when missing
long long file_mtime_ms(const std::string& path);    // 0 when missing
bool read_file(const std::string& path, std::string* out);
bool write_file_atomic(const std::string& path, const std::string& content);
bool remove_file(const std::string& path);

std::string join(const std::string& a, const std::string& b);
std::string basename(const std::string& path);
std::string dirname(const std::string& path);
std::string stem(const std::string& path);          // basename without extension
std::string extension(const std::string& path);     // lower-cased, with the dot
std::string absolute_path(const std::string& path);
std::string home_dir();
std::vector<std::string> list_dir(const std::string& path);  // basenames, sorted

}  // namespace vf
