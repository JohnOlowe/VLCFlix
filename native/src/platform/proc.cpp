#include "platform/proc.h"

#include <cerrno>
#include <cstring>

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

  auto readAll = [](int fd, std::string* sink) {
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

// Windows: CreateProcess with inherited pipes. Written to be correct but NOT yet
// executed anywhere - the sandbox has no Windows toolchain, so this path is
// unverified until the first build on the laptop (stated in docs/PORT_TURNS).
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
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  std::string command_line;
  for (const auto& a : argv) {
    if (!command_line.empty()) command_line.push_back(' ');
    command_line += "\"" + a + "\"";
  }
  std::vector<char> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back('\0');

  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  si.hStdInput = nullptr;
  PROCESS_INFORMATION pi{};
  const BOOL started = CreateProcessA(nullptr, mutable_line.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(out_w);
  CloseHandle(err_w);
  if (!started) {
    result.start_error = "CreateProcess failed, error " + std::to_string(GetLastError());
    CloseHandle(out_r);
    CloseHandle(err_r);
    return result;
  }
  result.started = true;

  const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_ms > 0 ? timeout_ms : 0);
  auto drain = [&](HANDLE pipe, std::string* sink) {
    char buf[8192];
    DWORD available = 0;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
      DWORD read_now = 0;
      if (!ReadFile(pipe, buf, sizeof buf, &read_now, nullptr) || read_now == 0) break;
      sink->append(buf, read_now);
    }
  };
  for (;;) {
    drain(out_r, &result.out);
    drain(err_r, &result.err);
    if (WaitForSingleObject(pi.hProcess, 40) == WAIT_OBJECT_0) break;
    if (timeout_ms > 0 && GetTickCount() > deadline) {
      result.timed_out = true;
      TerminateProcess(pi.hProcess, 1);
      break;
    }
  }
  drain(out_r, &result.out);
  drain(err_r, &result.err);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  result.exit_code = static_cast<int>(code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(out_r);
  CloseHandle(err_r);
  return result;
}

bool have_binary(const std::string& name) {
  const ProcResult r = run_process({name, "-version"}, 5000);
  return r.started && !r.timed_out;
}

#endif

}  // namespace vf
