#include "platform/proc.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "core/log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vf {
namespace {

constexpr std::size_t kChildErrCap = 4000;

void append_capped(std::string* sink, const char* data, std::size_t n) {
  if (sink->size() >= kChildErrCap || n == 0) return;
  sink->append(data, std::min(n, kChildErrCap - sink->size()));
}

#ifdef _WIN32
std::mutex& process_spawn_mutex() {
  static std::mutex mu;
  return mu;
}

std::string windows_command(const std::vector<std::string>& argv) {
  std::string command_line;
  for (const auto& arg : argv) {
    if (!command_line.empty()) command_line.push_back(' ');
    command_line += "\"" + arg + "\"";
  }
  return command_line;
}
#endif

}  // namespace

std::string ProcResult::error_line() const {
  std::size_t start = 0;
  while (start < err.size()) {
    const std::size_t end = err.find('\n', start);
    const std::string line = err.substr(start, end == std::string::npos ? std::string::npos : end - start);
    bool blank = true;
    for (unsigned char c : line) {
      if (c != ' ' && c != '\t' && c != '\r') {
        blank = false;
        break;
      }
    }
    if (!blank) return line;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out.empty() ? std::string() : out.substr(0, out.find('\n'));
}

#ifndef _WIN32

ProcResult run_process(const std::vector<std::string>& argv, int timeout_ms) {
  ProcResult result;
  if (argv.empty()) {
    result.start_error = "empty command";
    return result;
  }

  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    result.start_error = std::string("pipe: ") + std::strerror(errno);
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    result.start_error = std::string("fork: ") + std::strerror(errno);
    return result;
  }
  if (pid == 0) {
    // Child: stdin from /dev/null, stdout/stderr into the pipes.
    const int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      close(devnull);
    }
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[0]);
    close(err_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);  // exec failed; the parent sees 127 and the missing binary is named
  }

  close(out_pipe[1]);
  close(err_pipe[1]);
  result.started = true;

  [[maybe_unused]] auto readAll = [](int fd, std::string* sink) {
    char buf[8192];
    for (;;) {
      const ssize_t n = read(fd, buf, sizeof buf);
      if (n > 0) {
        sink->append(buf, static_cast<std::size_t>(n));
      } else if (n == 0) {
        break;
      } else if (errno != EINTR) {
        break;
      }
    }
  };

  // Two pipes, one process: poll so neither can block the other, and so the
  // timeout covers a child that produces nothing at all.
  bool out_open = true;
  bool err_open = true;
  int elapsed = 0;
  const int slice = 50;
  while (out_open || err_open) {
    struct pollfd fds[2];
    nfds_t count = 0;
    if (out_open) fds[count++] = {out_pipe[0], POLLIN, 0};
    if (err_open) fds[count++] = {err_pipe[0], POLLIN, 0};
    const int ready = poll(fds, count, slice);
    elapsed += slice;
    if (ready > 0) {
      for (nfds_t i = 0; i < count; ++i) {
        if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        bool* open_flag = (fds[i].fd == out_pipe[0]) ? &out_open : &err_open;
        std::string* sink = (fds[i].fd == out_pipe[0]) ? &result.out : &result.err;
        char buf[8192];
        const ssize_t n = read(fds[i].fd, buf, sizeof buf);
        if (n > 0) {
          sink->append(buf, static_cast<std::size_t>(n));
        } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
          *open_flag = false;
        }
      }
    } else if (ready == 0 && timeout_ms > 0 && elapsed >= timeout_ms) {
      result.timed_out = true;
      kill(pid, SIGKILL);
      break;
    }
  }

  close(out_pipe[0]);
  close(err_pipe[0]);

  int status = 0;
  if (!result.timed_out) {
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (WIFSIGNALED(status)) result.exit_code = -WTERMSIG(status);
  } else {
    waitpid(pid, &status, 0);
    result.exit_code = -1;
  }

  if (result.exit_code == 127) {
    result.started = false;
    result.start_error = "'" + argv[0] + "' was not found on PATH";
  }
  return result;
}

bool have_binary(const std::string& name) {
  const ProcResult r = run_process({name, "-version"}, 5000);
  return r.started && !r.timed_out;
}

#else  // _WIN32

// Windows: CreateProcess with inherited pipes, drained by two reader threads.
//
// This was rewritten after the first real execution of the Windows binary (under
// Wine 11.19): the previous version polled the pipes with PeekNamedPipe, and every
// child that did report output - ffprobe, ffmpeg - arrived here with an empty stdout
// AND an empty stderr, so a library scan called every file unreadable. A process that
// starts, exits 0 and produces nothing is the worst kind of failure: silent.
//
// A blocking read on a thread per pipe is simpler and race-free. The thread returns
// when the pipe reaches EOF, which happens once the child and every copy of its write
// handle are gone, so data buffered at exit is still read - no polling, no 40 ms sleep.
//
// The handle list below is not optional decoration. bInheritHandles=TRUE inherits EVERY
// inheritable handle the process owns, so with a scan running eight probes at once one
// child inherited another child's pipe write end. That pipe then never reached EOF, the
// reader thread blocked forever, and Windows/Wine refused the launches outright:
// eight concurrent probes produced "CreateProcess failed, error 1359" and three
// timeouts, while the same code run one process at a time was perfect. Restricting
// inheritance to exactly our three handles is the documented fix, and it is what makes
// scanning a folder faster than one file at a time.
ProcResult run_process(const std::vector<std::string>& argv, int timeout_ms) {
  ProcResult result;
  if (argv.empty()) {
    result.start_error = "empty command";
    return result;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
  if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
    result.start_error = "CreatePipe failed";
    return result;
  }
  // The read ends must not be inherited, or the pipe can never reach EOF.
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  // A GUI-subsystem process has no standard handles at all. Give the child a real
  // (empty) stdin rather than NULL: NULL in STARTUPINFO is not a valid handle, and
  // tools that check their stdin behave differently when it is missing.
  HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr);

  std::string command_line;
  for (const auto& a : argv) {
    if (!command_line.empty()) command_line.push_back(' ');
    command_line += "\"" + a + "\"";
  }
  std::vector<char> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back('\0');

  STARTUPINFOEXA si{};
  si.StartupInfo.cb = sizeof si;
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdOutput = out_w;
  si.StartupInfo.hStdError = err_w;
  si.StartupInfo.hStdInput = nul;

  // Only these three handles cross into the child - nothing else the process happens
  // to have open, which is what made concurrent probes fail.
  HANDLE inherited[3] = {out_w, err_w, nul};
  SIZE_T attr_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
  std::vector<char> attr_buffer(attr_size > 0 ? attr_size : 1);
  si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buffer.data());
  bool list_ok = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
  if (list_ok) {
    list_ok = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                        inherited, sizeof inherited, nullptr, nullptr) != FALSE;
  }

  PROCESS_INFORMATION pi{};
  // Process creation itself is serialised. Two threads launching children at the same
  // moment made Wine 11.19 refuse the second with ERROR_INTERNAL_ERROR (1359) or 731,
  // roughly at random - 5 of 8 concurrent probes died before they started. Serialising
  // only the launch costs microseconds and the children still run in parallel, so a
  // scan is just as fast and the failure mode is gone.
  std::lock_guard<std::mutex> spawn_lock(process_spawn_mutex());
  const BOOL started =
      list_ok ? CreateProcessA(nullptr, mutable_line.data(), nullptr, nullptr, TRUE,
                               EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                               &si.StartupInfo, &pi)
              : FALSE;
  if (list_ok) DeleteProcThreadAttributeList(si.lpAttributeList);
  // Our copies of the write ends go immediately: the readers only see EOF once no
  // process holds them.
  CloseHandle(out_w);
  CloseHandle(err_w);
  if (!started) {
    result.start_error = "CreateProcess failed, error " + std::to_string(GetLastError());
    CloseHandle(out_r);
    CloseHandle(err_r);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    return result;
  }
  result.started = true;

  auto read_all = [](HANDLE pipe, std::string* sink) {
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(pipe, buf, sizeof buf, &got, nullptr) && got > 0) {
      sink->append(buf, got);
    }
  };
  std::thread reader_out(read_all, out_r, &result.out);
  std::thread reader_err(read_all, err_r, &result.err);

  const DWORD wait = WaitForSingleObject(
      pi.hProcess, timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : INFINITE);
  if (wait == WAIT_TIMEOUT) {
    result.timed_out = true;
    // Killing the child closes the last write handles, so both readers return.
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
  }

  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  result.exit_code = static_cast<int>(code);

  reader_out.join();
  reader_err.join();
  CloseHandle(out_r);
  CloseHandle(err_r);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  return result;
}

bool have_binary(const std::string& name) {
  // "-version" must actually succeed and actually print something. Checking only that
  // a process was created is how doctor once reported "ffmpeg found" on Windows while
  // every probe came back empty.
  const ProcResult r = run_process({name, "-version"}, 8000);
  return r.started && !r.timed_out && r.exit_code == 0 && !r.out.empty();
}

#endif

struct Child::Impl {
  mutable std::mutex mu;
  std::string err;
  std::thread reader;
#ifdef _WIN32
  HANDLE process = nullptr;
  HANDLE thread_handle = nullptr;
  HANDLE err_read = nullptr;
#else
  pid_t pid = -1;
  int err_read = -1;
#endif
};

Child::~Child() { stop(); }

bool Child::start(const std::vector<std::string>& argv, std::string* error) {
  stop();
  if (argv.empty()) {
    if (error) *error = "empty command";
    return false;
  }
  impl_ = new Impl();

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  HANDLE err_read = nullptr;
  HANDLE err_write = nullptr;
  if (!CreatePipe(&err_read, &err_write, &sa, 0)) {
    delete impl_;
    impl_ = nullptr;
    if (error) *error = "CreatePipe failed";
    return false;
  }
  SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

  std::string command_line = windows_command(argv);
  std::vector<char> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back('\0');

  STARTUPINFOEXA si{};
  si.StartupInfo.cb = sizeof si;
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = nul;
  si.StartupInfo.hStdOutput = nul;
  si.StartupInfo.hStdError = err_write;
  HANDLE inherited[2] = {nul, err_write};
  SIZE_T attr_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
  std::vector<char> attr_buffer(attr_size > 0 ? attr_size : 1);
  si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buffer.data());
  bool list_ok = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
  if (list_ok) {
    list_ok = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                        inherited, sizeof inherited, nullptr, nullptr) != FALSE;
  }

  PROCESS_INFORMATION pi{};
  BOOL started = FALSE;
  if (list_ok) {
    std::lock_guard<std::mutex> spawn_lock(process_spawn_mutex());
    started = CreateProcessA(nullptr, mutable_line.data(), nullptr, nullptr, TRUE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                             &si.StartupInfo, &pi);
  }
  if (list_ok) DeleteProcThreadAttributeList(si.lpAttributeList);
  CloseHandle(err_write);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (!started) {
    CloseHandle(err_read);
    delete impl_;
    impl_ = nullptr;
    if (error) *error = "CreateProcess failed, error " + std::to_string(GetLastError());
    return false;
  }
  impl_->process = pi.hProcess;
  impl_->thread_handle = pi.hThread;
  impl_->err_read = err_read;
  impl_->reader = std::thread([impl = impl_, err_read] {
    char buf[1024];
    DWORD got = 0;
    while (ReadFile(err_read, buf, sizeof buf, &got, nullptr) && got > 0) {
      std::lock_guard<std::mutex> lock(impl->mu);
      append_capped(&impl->err, buf, got);
    }
  });
  return true;
#else
  int err_pipe[2] = {-1, -1};
  if (pipe(err_pipe) != 0) {
    delete impl_;
    impl_ = nullptr;
    if (error) *error = std::string("pipe: ") + std::strerror(errno);
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(err_pipe[0]);
    close(err_pipe[1]);
    delete impl_;
    impl_ = nullptr;
    if (error) *error = std::string("fork: ") + std::strerror(errno);
    return false;
  }
  if (pid == 0) {
    const int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      if (devnull > 2) close(devnull);
    }
    dup2(err_pipe[1], STDERR_FILENO);
    close(err_pipe[0]);
    close(err_pipe[1]);
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(err_pipe[1]);
  impl_->pid = pid;
  impl_->err_read = err_pipe[0];
  impl_->reader = std::thread([impl = impl_, fd = err_pipe[0]] {
    char buf[1024];
    for (;;) {
      const ssize_t n = read(fd, buf, sizeof buf);
      if (n > 0) {
        std::lock_guard<std::mutex> lock(impl->mu);
        append_capped(&impl->err, buf, static_cast<std::size_t>(n));
      } else if (n == 0 || errno != EINTR) {
        break;
      }
    }
  });
  return true;
#endif
}

void Child::stop() {
  if (!impl_) return;
#ifdef _WIN32
  if (impl_->process) {
    TerminateProcess(impl_->process, 1);
    WaitForSingleObject(impl_->process, 2000);
    CloseHandle(impl_->process);
    if (impl_->thread_handle) CloseHandle(impl_->thread_handle);
  }
  if (impl_->err_read) CloseHandle(impl_->err_read);
#else
  if (impl_->pid > 0) {
    kill(impl_->pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
      int status = 0;
      const pid_t got = waitpid(impl_->pid, &status, WNOHANG);
      if (got == impl_->pid || (got < 0 && errno != EINTR)) {
        impl_->pid = -1;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (impl_->pid > 0) {
      kill(impl_->pid, SIGKILL);
      int status = 0;
      while (waitpid(impl_->pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
  if (impl_->err_read >= 0) close(impl_->err_read);
#endif
  if (impl_->reader.joinable()) impl_->reader.join();
  delete impl_;
  impl_ = nullptr;
}

bool Child::running() {
  if (!impl_) return false;
#ifdef _WIN32
  if (!impl_->process) return false;
  return WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
#else
  if (impl_->pid <= 0) return false;
  int status = 0;
  const pid_t got = waitpid(impl_->pid, &status, WNOHANG);
  if (got == 0 || (got < 0 && errno == EINTR)) return true;
  impl_->pid = -1;
  return false;
#endif
}

std::string Child::recent_error() const {
  if (!impl_) return {};
  std::lock_guard<std::mutex> lock(impl_->mu);
  std::string text = impl_->err;
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.pop_back();
  }
  const std::size_t cut = text.rfind('\n');
  if (cut != std::string::npos) text = text.substr(cut + 1);
  if (text.size() > 180) text.resize(180);
  return text;
}

bool spawn_detached(const std::vector<std::string>& argv, std::string* error) {
  if (argv.empty()) {
    if (error) *error = "empty command";
    return false;
  }
#ifdef _WIN32
  std::string command_line = windows_command(argv);
  std::vector<char> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back('\0');
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;
  PROCESS_INFORMATION pi{};
  BOOL started = FALSE;
  {
    std::lock_guard<std::mutex> spawn_lock(process_spawn_mutex());
    started = CreateProcessA(nullptr, mutable_line.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP,
                             nullptr, nullptr, &si, &pi);
  }
  if (!started) {
    if (error) *error = "CreateProcess failed, error " + std::to_string(GetLastError());
    return false;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return true;
#else
  const pid_t pid = fork();
  if (pid < 0) {
    if (error) *error = std::string("fork: ") + std::strerror(errno);
    return false;
  }
  if (pid == 0) {
    setsid();
    const pid_t inner = fork();
    if (inner == 0) {
      const int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
      }
      std::vector<char*> args;
      args.reserve(argv.size() + 1);
      for (const auto& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
      args.push_back(nullptr);
      execvp(args[0], args.data());
      _exit(127);
    }
    _exit(inner < 0 ? 127 : 0);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return true;
#endif
}

}  // namespace vf
