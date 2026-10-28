// Golden-image tests: every screen is rendered and compared with a committed baseline.
//
// Why this layer exists: "the UI looks right" is an opinion. A pixel difference is a
// measurement, and it fails a build when a layout moves by three pixels in a way that
// nobody would notice by reading the diff.
//
// The comparison is tolerant in a defined way, not fuzzy: a pixel counts as changed
// when any channel moves by more than `threshold` (antialiasing and bilinear blits move
// values by one or two), and a screen fails when more than `allowed` of its pixels
// changed. Both numbers are printed, so a failure says *how far* off it is.
//
// `VLCFLIX_UPDATE_GOLDEN=1` rewrites the baselines; that is how they are created and
// how an intentional design change is recorded - and the diff is then reviewable in the
// commit, which is the whole point of committing images.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/paths.h"
#include "render/canvas.h"
#include "render/image.h"
#include "tests/harness.h"
#include "ui/app.h"

namespace vf {
namespace {

constexpr int kThreshold = 6;      // per-channel difference that counts as a change
constexpr double kAllowed = 0.0015;  // fraction of pixels allowed to change (0.15 %)

struct Diff {
  long long changed = 0;
  long long total = 0;
  int worst = 0;
  int worst_x = 0;
  int worst_y = 0;
  double fraction() const { return total ? static_cast<double>(changed) / total : 0.0; }
};

Diff compare(const Image& want, const Image& got) {
  Diff diff;
  diff.total = static_cast<long long>(want.width) * want.height;
  const int width = std::min(want.width, got.width);
  const int height = std::min(want.height, got.height);
  if (want.width != got.width || want.height != got.height) {
    // A size change is not a pixel difference; count every pixel as changed.
    diff.changed = diff.total;
    return diff;
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * want.width + x) * 4;
      const int dr = std::abs(int(want.pixels[i + 0]) - int(got.pixels[i + 0]));
      const int dg = std::abs(int(want.pixels[i + 1]) - int(got.pixels[i + 1]));
      const int db = std::abs(int(want.pixels[i + 2]) - int(got.pixels[i + 2]));
      const int delta = std::max(dr, std::max(dg, db));
      if (delta > diff.worst) {
        diff.worst = delta;
        diff.worst_x = x;
        diff.worst_y = y;
      }
      if (delta > kThreshold) ++diff.changed;
    }
  }
  return diff;
}

bool updating() {
  const char* flag = std::getenv("VLCFLIX_UPDATE_GOLDEN");
  return flag && *flag && std::string(flag) != "0";
}

// A catalog with no artwork at all, so the baseline does not depend on ffmpeg having
// run: the fallback gradients are drawn by our code and are what gets compared.
Catalog fixed_catalog() {
  Catalog catalog;
  catalog.scanned_at = 1700000000;
  auto add = [&](const std::string& id, const std::string& title, int year,
                 std::vector<std::string> genres, const std::string& synopsis) {
    Item item;
    item.id = id;
    item.title = title;
    item.year = year;
    item.genres = std::move(genres);
    item.synopsis = synopsis;
    item.duration = 101 * 60 + 12;
    item.width = 1920;
    item.height = 1080;
    item.rating = "TV-14";
    item.path = "/library/" + title + ".mkv";
    item.filename = title + ".mkv";
    item.added_at = 1700000000 - static_cast<long long>(catalog.items.size()) * 3600;
    catalog.items.push_back(item);
  };
  add("golden-1", "Golden Hour", 2019, {"Drama", "Western"},
      "A surveyor walks the same fence line for thirty years and never once crosses it.");
  add("golden-2", "Second Light", 2021, {"Drama"}, "A projectionist keeps a cinema open alone.");
  add("golden-3", "Third Rail", 2023, {"Science Fiction", "Thriller"},
      "The last train runs on a line that was never built.");
  return catalog;
}

void check_screen(const std::string& baseline_dir, const std::string& name, ui::App& app,
                  const std::string& argument) {
  app.show_screen(name, argument);
  Canvas canvas(app.width(), app.height());
  app.draw(canvas);

  const std::string want_path = join(baseline_dir, name + ".png");
  const std::string got_path = join(temp_dir(), "vlcflix-studio-golden-" + name + ".png");
  if (!write_png(got_path, canvas)) {
    vftest::Registry::instance().fail(__FILE__, __LINE__, "could not write " + got_path);
    return;
  }
  if (updating() || !file_exists(want_path)) {
    if (!write_png(want_path, canvas)) {
      vftest::Registry::instance().fail(__FILE__, __LINE__, "could not write " + want_path);
      return;
    }
    std::printf("    (baseline %s %s)\n", file_exists(want_path) ? "rewritten:" : "created:",
                want_path.c_str());
    vftest::Registry::instance().pass();
    return;
  }

  std::string bytes;
  if (!read_file(want_path, &bytes)) {
    vftest::Registry::instance().fail(__FILE__, __LINE__, "cannot read " + want_path);
    return;
  }
  const Image want = decode_image(bytes);
  const Image got = decode_image([&] {
    std::string out;
    read_file(got_path, &out);
    return out;
  }());
  if (!want.valid() || !got.valid()) {
    vftest::Registry::instance().fail(__FILE__, __LINE__,
                                      "baseline or render did not decode: " + name);
    return;
  }
  const Diff diff = compare(want, got);
  if (diff.fraction() > kAllowed) {
    char message[512];
    std::snprintf(message, sizeof message,
                  "%s differs from its baseline: %.2f%% of pixels changed (%.0f allowed), "
                  "worst channel delta %d at (%d,%d).\n      render: %s\n      baseline: %s\n"
                  "      (VLCFLIX_UPDATE_GOLDEN=1 make test to accept the change)",
                  name.c_str(), diff.fraction() * 100.0, kAllowed * 100.0, diff.worst,
                  diff.worst_x, diff.worst_y, got_path.c_str(), want_path.c_str());
    vftest::Registry::instance().fail(__FILE__, __LINE__, message);
  } else {
    std::printf("    %s: %.3f%% pixels changed\n", name.c_str(), diff.fraction() * 100.0);
    vftest::Registry::instance().pass();
  }
}

}  // namespace

void run_golden_tests(const std::string& baseline_dir) {
  TEST("golden: every screen matches its committed baseline");
  ensure_dir(baseline_dir);
  ui::App app(baseline_dir, temp_dir());
  app.resize(1280, 720);
  app.set_catalog(fixed_catalog());
  app.set_settings(Settings{});
  app.set_my_list({"golden-2"});

  check_screen(baseline_dir, "home", app, "0");
  check_screen(baseline_dir, "browse", app, "0");
  check_screen(baseline_dir, "mylist", app, "0");
  check_screen(baseline_dir, "search", app, "third");
  check_screen(baseline_dir, "detail", app, "0");
  check_screen(baseline_dir, "settings", app, "0");

  // The empty library is its own screen, and has to stay an honest empty state.
  ui::App empty_app(baseline_dir, temp_dir());
  empty_app.resize(1280, 720);
  empty_app.set_sample_folder("");
  empty_app.show_screen("home", "");
  check_screen(baseline_dir, "empty", empty_app, "");

  // A moved pixel budget: the same image compared against itself must be identical,
  // which proves the comparison is not simply always passing.
  Canvas canvas(1280, 720);
  app.show_screen("home", "");
  app.draw(canvas);
  const std::string again = join(temp_dir(), "vlcflix-studio-golden-selfcheck.png");
  write_png(again, canvas);
  std::string bytes;
  read_file(again, &bytes);
  const Image first = decode_image(bytes);
  const Image second = decode_image(bytes);
  const Diff self = compare(first, second);
  CHECK_EQ(self.changed, 0LL);

  // And a one-pixel change must be caught, or the whole layer proves nothing.
  Canvas nudged = canvas;
  nudged.fill_rect(Rect{10, 10, 4, 4}, rgb(0xff00ff));
  const std::string nudged_path = join(temp_dir(), "vlcflix-studio-golden-nudged.png");
  write_png(nudged_path, nudged);
  std::string nudged_bytes;
  read_file(nudged_path, &nudged_bytes);
  const Diff changed = compare(first, decode_image(nudged_bytes));
  CHECK_EQ(changed.changed, 16LL);  // 4x4 pixels, exactly
}

}  // namespace vf
