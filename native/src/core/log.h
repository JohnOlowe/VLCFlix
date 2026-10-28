// Crash-proof logging.
//
// Requirement (user, 2026-10-28): logs are always produced and written, even if
// the app crashes on open. The design follows from that:
//
//   * log::init() is the FIRST statement of main(). Nothing may run before it.
//   * Three candidate directories are tried in order (user data, beside the exe,
//     temp). If one cannot be opened the next one is used, and the failure is
//     recorded in the file that did open.
//   * Every line is flushed, so a hard kill loses at most the line in flight.
//   * Crash handlers are installed immediately after the file is open:
//     std::terminate, POSIX signals (SEGV/ABRT/FPE/ILL/BUS/INT), and on Windows an
//     unhandled-exception filter plus a first-chance vectored handler. Each writes
//     a CRASH record with a stack trace, flushes, then lets the process die.
//   * A run-start record is written on init and a clean-exit record on shutdown,
//     so the next launch can say "the previous run ended unexpectedly" - which is
//     exactly what the 1.0.x black-window reports were missing.
//   * 5 files x 2 MB, oldest dropped: a crash loop cannot fill a user's disk.
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
