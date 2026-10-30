#pragma once
#include <string>
#include <vector>

namespace vf {

struct ProcResult {
  int exit_code = -1;
  bool timed_out = false;
  bool started = false;   // false = the binary could not be launched at all
  std::string out;
  std::string err;

  bool ok() const { return started && !timed_out && exit_code == 0; }
  std::string start_error;

  // First non-empty line of stderr, for logs and error messages.
  std::string error_line() const;
};

ProcResult run_process(const std::vector<std::string>& argv, int timeout_ms = 20000);

// True if `name` can be found on PATH and executed.
bool have_binary(const std::string& name);

// A child that outlives the call. Used for the ffmpeg playback stream, which has to
// keep running until the film stops. The destructor stops it.
class Child {
 public:
  Child() = default;
  ~Child();
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  bool start(const std::vector<std::string>& argv, std::string* error);
  void stop();
  bool running();
  std::string recent_error() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

// Start a process and forget it. Used to open the installed VLC: closing this app
// must not kill the film. Returns false only when the process could not be created.
bool spawn_detached(const std::vector<std::string>& argv, std::string* error);

}  // namespace vf
