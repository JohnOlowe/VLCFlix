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

}  // namespace vf
