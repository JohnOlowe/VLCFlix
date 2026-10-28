#pragma once
#include <cstddef>
#include <string>

namespace vf {
namespace log {

struct Options {
  std::string path;      // explicit file path; empty = choose a directory
  bool console = false;  // mirror to stderr/stdout
  bool install_crash_handlers = true;
};

// Opens the log and writes the header. Returns false only if no candidate
// directory accepted a file, in which case stderr is used and the caller is told.
bool init(const Options& options);

// Path actually written to (empty if only stderr works).
const std::string& path();

// True when the previous session's log ends with a run-start and no clean-exit.
bool previous_run_crashed();
std::string previous_run_tail(std::size_t lines);

void debug(const char* fmt, ...);
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);
void crash(const char* fmt, ...);  // also flushes and does not return twice

void shutdown();  // writes the clean-exit record and closes

}  // namespace log
}  // namespace vf

#define LOG_DEBUG(...) ::vf::log::debug(__VA_ARGS__)
#define LOG_INFO(...) ::vf::log::info(__VA_ARGS__)
#define LOG_WARN(...) ::vf::log::warn(__VA_ARGS__)
#define LOG_ERROR(...) ::vf::log::error(__VA_ARGS__)
