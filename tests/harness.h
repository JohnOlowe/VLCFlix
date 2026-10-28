// A deliberately tiny test harness: no dependency to reinstall after a snapshot,
// and `make test` works from a bare clone.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vftest {

struct Registry {
  int checks = 0;
  int failures = 0;
  std::string current;
  std::vector<std::string> failed_names;

  static Registry& instance() {
    static Registry registry;
    return registry;
  }

  void begin(const std::string& name) {
    current = name;
    std::printf("- %s\n", name.c_str());
  }

  void pass() { ++checks; }

  void fail(const char* file, int line, const std::string& why) {
    ++checks;
    ++failures;
    if (failed_names.empty() || failed_names.back() != current) failed_names.push_back(current);
    std::printf("    FAIL %s:%d  %s\n", file, line, why.c_str());
  }
};

inline int summary(const char* suite) {
  const Registry& r = Registry::instance();
  std::printf("\n%s: %d check(s), %d failure(s)\n", suite, r.checks, r.failures);
  if (r.failures) {
    std::printf("failed tests:\n");
    for (const auto& name : r.failed_names) std::printf("  - %s\n", name.c_str());
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}

}  // namespace vftest

#define TEST(name) vftest::Registry::instance().begin(name)

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      vftest::Registry::instance().pass();                                     \
    } else {                                                                   \
      vftest::Registry::instance().fail(__FILE__, __LINE__, #condition);        \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    const auto& a_ = (actual);                                                 \
    const auto& e_ = (expected);                                               \
    if (a_ == e_) {                                                            \
      vftest::Registry::instance().pass();                                     \
    } else {                                                                   \
      vftest::Registry::instance().fail(__FILE__, __LINE__,                    \
                                        std::string(#actual) + " == " + #expected); \
    }                                                                          \
  } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                  \
  do {                                                                         \
    const double a_ = static_cast<double>(actual);                             \
    const double e_ = static_cast<double>(expected);                           \
    if (std::fabs(a_ - e_) <= static_cast<double>(tolerance)) {                \
      vftest::Registry::instance().pass();                                     \
    } else {                                                                   \
      vftest::Registry::instance().fail(                                       \
          __FILE__, __LINE__,                                                  \
          std::string(#actual) + " ~= " + #expected + " (" + std::to_string(a_) + \
              " vs " + std::to_string(e_) + ")");                              \
    }                                                                          \
  } while (0)

#define CHECK_STR_EQ(actual, expected)                                         \
  do {                                                                         \
    const std::string a_ = (actual);                                           \
    const std::string e_ = (expected);                                         \
    if (a_ == e_) {                                                            \
      vftest::Registry::instance().pass();                                     \
    } else {                                                                   \
      vftest::Registry::instance().fail(__FILE__, __LINE__,                    \
                                        std::string(#actual) + " -> \"" + a_ +   \
                                            "\" expected \"" + e_ + "\"");     \
    }                                                                          \
  } while (0)
