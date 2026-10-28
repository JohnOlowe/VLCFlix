#include "core/log.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "core/brand.h"
#include "core/paths.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace vf {
namespace log {
namespace {

constexpr long long kMaxBytes = 2 * 1024 * 1024;
constexpr int kMaxFiles = 5;

std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::string g_path;
bool g_console = false;
std::atomic<bool> g_in_crash{false};
bool g_prev_crashed = false;
std::string g_prev_tail;
long long g_written = 0;
std::string g_session_id;

std::string timestamp(bool date = true) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[64];
  if (date) {
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                  tm.tm_sec, static_cast<int>(ms.count()));
  } else {
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
  }
  return buf;
}

// Rotation: <base>.log is current, <base>.1.log .. <base>.4.log are older.
void rotateIfNeeded(const std::string& path) {
  const long long size = file_size(path);
  if (size < kMaxBytes) return;
  std::remove((path + "." + std::to_string(kMaxFiles - 1) + ".log").c_str());
  for (int i = kMaxFiles - 2; i >= 1; --i) {
    const std::string from = path + "." + std::to_string(i) + ".log";
    const std::string to = path + "." + std::to_string(i + 1) + ".log";
    std::rename(from.c_str(), to.c_str());
  }
  std::rename(path.c_str(), (path + ".1.log").c_str());
}

// Was the last session closed properly? Read the tail of any existing log.
void inspectPreviousRun(const std::string& path) {
  std::string text;
  if (!read_file(path, &text)) return;
  const std::size_t keep = 8192;
  const std::string tail = text.size() > keep ? text.substr(text.size() - keep) : text;
  g_prev_tail = tail;
  const std::size_t runStart = tail.rfind("[run] start");
  const std::size_t cleanExit = tail.rfind("[run] clean exit");
  g_prev_crashed = runStart != std::string::npos &&
                   (cleanExit == std::string::npos || cleanExit < runStart);
}

void writeRaw(const std::string& line) {
  if (g_file) {
    std::fputs(line.c_str(), g_file);
    std::fputc('\n', g_file);
    std::fflush(g_file);  // every line: a crash must not cost buffered output
    g_written += static_cast<long long>(line.size()) + 1;
  }
  if (g_console) {
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
  }
}

void emit(const char* level, const char* fmt, va_list args) {
  char body[4096];
  std::vsnprintf(body, sizeof body, fmt, args);
  const std::string line = "[" + timestamp() + "] [" + level + "] " + body;
  std::lock_guard<std::mutex> lock(g_mutex);
  writeRaw(line);
}

void emitFormatted(const char* level, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit(level, fmt, args);
  va_end(args);
}

std::string stackTrace() {
  std::string out;
#ifndef _WIN32
  void* frames[32];
  const int n = backtrace(frames, 32);
  char** symbols = backtrace_symbols(frames, n);
  for (int i = 0; i < n; ++i) {
    out += "\n    #" + std::to_string(i) + " ";
    out += symbols ? symbols[i] : "?";
  }
  if (symbols) std::free(symbols);
#else
  void* frames[32];
  const USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
  HANDLE proc = GetCurrentProcess();
  SymInitialize(proc, nullptr, TRUE);
  char buffer[sizeof(SYMBOL_INFO) + 256];
  SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  for (USHORT i = 0; i < n; ++i) {
    DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
    IMAGEHLP_MODULE64 module;
    module.SizeOfStruct = sizeof(module);
    std::string holder = "?";
    if (SymGetModuleInfo64(proc, addr, &module)) holder = module.ModuleName;
    if (SymFromAddr(proc, addr, nullptr, symbol)) {
      out += "\n    #" + std::to_string(i) + " " + holder + "!" + symbol->Name;
    } else {
      char buf[64];
      std::snprintf(buf, sizeof buf, " 0x%llx", static_cast<unsigned long long>(addr));
      out += "\n    #" + std::to_string(i) + " " + holder + buf;
    }
  }
#endif
  return out;
}

// Async-signal-safe enough for our purposes: it writes with fputs to an already
// open stream and never allocates beyond a small fixed buffer.
void crashRecord(const char* what, const std::string& detail) {
  const std::string line = "[" + timestamp() + "] [CRASH] " + what + " " + detail +
                           stackTrace();
  writeRaw(line);
  writeRaw("[" + timestamp() + "] [CRASH] flushed; the process is going down");
}

void onTerminate() {
  if (!g_in_crash.exchange(true)) {
    std::string detail;
    try {
      std::exception_ptr e = std::current_exception();
      if (e) {
        try {
          std::rethrow_exception(e);
        } catch (const std::exception& ex) {
          detail = std::string("what(): ") + ex.what();
        } catch (...) {
          detail = "non-standard exception";
        }
      }
    } catch (...) {
    }
    crashRecord("std::terminate", detail);
  }
  std::abort();
}

#ifndef _WIN32
void onSignal(int sig) {
  if (!g_in_crash.exchange(true)) {
    const char* name = "signal";
    switch (sig) {
      case SIGSEGV: name = "SIGSEGV"; break;
      case SIGABRT: name = "SIGABRT"; break;
      case SIGFPE: name = "SIGFPE"; break;
      case SIGILL: name = "SIGILL"; break;
      case SIGBUS: name = "SIGBUS"; break;
      case SIGINT: name = "SIGINT"; break;
      case SIGTERM: name = "SIGTERM"; break;
      default: break;
    }
    crashRecord(name, "code=" + std::to_string(sig));
  }
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
#else
LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
  if (!g_in_crash.exchange(true)) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "code=0x%08lx address=%p",
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0UL,
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress
                                                : nullptr);
    crashRecord("unhandled exception", buf);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI onFirstChance(EXCEPTION_POINTERS* info) {
  // Record the first-chance code even though it may be handled: the black-window
  // reports of 1.0.x had no such record.
  if (info && info->ExceptionRecord && info->ExceptionRecord->ExceptionCode != 0xE06D7363) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "first-chance code=0x%08lx address=%p",
                  info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    emitFormatted("TRACE", "%s", buf);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void installCrashHandlers() {
  std::set_terminate(onTerminate);
#ifndef _WIN32
  for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGINT, SIGTERM}) {
    std::signal(sig, onSignal);
  }
#else
  SetUnhandledExceptionFilter(onUnhandledException);
  AddVectoredExceptionHandler(1, onFirstChance);
#endif
}

}  // namespace

bool init(const Options& options) {
  g_console = options.console;

  std::vector<std::string> candidates;
  if (!options.path.empty()) {
    candidates.push_back(options.path);
  } else {
    for (const auto& dir : log_dir_candidates()) {
      candidates.push_back(join(dir, std::string(kLogBaseName) + ".log"));
    }
  }

  std::string problems;
  for (const auto& candidate : candidates) {
    const std::string dir = dirname(candidate);
    if (!dir.empty() && !ensure_dir(dir)) {
      problems += "cannot create " + dir + "; ";
      continue;
    }
    rotateIfNeeded(candidate);
    inspectPreviousRun(candidate);
    std::FILE* f = std::fopen(candidate.c_str(), "ab");
    if (!f) {
      problems += "cannot open " + candidate + "; ";
      continue;
    }
    g_file = f;
    g_path = candidate;
    break;
  }

  g_session_id = timestamp(false) + "-" + std::to_string(
#ifdef _WIN32
                                           static_cast<unsigned long>(GetCurrentProcessId())
#else
                                           static_cast<unsigned long>(getpid())
#endif
  );

  if (!g_file) {
    // Nothing on disk worked. Say so loudly on stderr and keep going: a missing
    // log must not stop the app from starting, but the failure is never silent.
    std::fprintf(stderr,
                 "[log] FATAL: no log file could be opened (%s)\n"
                 "[log] stderr logging is all that remains\n",
                 problems.c_str());
    writeRaw("[run] start (stderr only: " + problems + ")");
    installCrashHandlers();
    return false;
  }

  writeRaw("");
  writeRaw("================================================================================");
  emitFormatted("run", "start %s %s build %s", kProductName, kVersion, kBuildDate);
  emitFormatted("run", "log=%s", g_path.c_str());
  emitFormatted("run", "session=%s data=%s", g_session_id.c_str(), data_dir().c_str());
  emitFormatted("run", "previous run: %s",
                g_prev_crashed ? "ENDED UNEXPECTEDLY (no clean-exit record)"
                               : "closed cleanly or first run");
  if (!problems.empty()) emitFormatted("warn", "log location fallbacks: %s", problems.c_str());

  if (options.install_crash_handlers) installCrashHandlers();
  return true;
}

const std::string& path() { return g_path; }

bool previous_run_crashed() { return g_prev_crashed; }

std::string previous_run_tail(std::size_t lines) {
  std::vector<std::string> all;
  std::string current;
  for (char c : g_prev_tail) {
    if (c == '\n') {
      all.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) all.push_back(current);
  const std::size_t start = all.size() > lines ? all.size() - lines : 0;
  std::string out;
  for (std::size_t i = start; i < all.size(); ++i) out += all[i] + "\n";
  return out;
}

void debug(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit("debug", fmt, args);
  va_end(args);
}

void info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit("info", fmt, args);
  va_end(args);
}

void warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit("warn", fmt, args);
  va_end(args);
}

void error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit("error", fmt, args);
  va_end(args);
}

void crash(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char body[2048];
  std::vsnprintf(body, sizeof body, fmt, args);
  va_end(args);
  std::lock_guard<std::mutex> lock(g_mutex);
  writeRaw("[" + timestamp() + "] [CRASH] " + body);
}

void shutdown() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
      writeRaw("[" + timestamp() + "] [run] clean exit session=" + g_session_id);
      std::fclose(g_file);
      g_file = nullptr;
    }
  }
}

}  // namespace log
}  // namespace vf
