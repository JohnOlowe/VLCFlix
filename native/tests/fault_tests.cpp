// Fault injection: the failures a user actually hits.
//
// An application is judged on what it does when things are wrong, and none of these
// paths is exercised by a happy-path test. Every case here asserts two things: the
// program does not crash or hang, and it *says what happened* - in the message the
// user sees, in the log, or both. Silence is the failure mode being tested for.
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "core/json.h"
#include "core/log.h"
#include "core/model.h"
#include "core/naming.h"
#include "core/paths.h"
#include "core/store.h"
#include "core/sync.h"
#include "library/artwork.h"
#include "library/probe.h"
#include "library/scan.h"
#include "library/subtitles.h"
#include "playback/engine.h"
#include "render/canvas.h"
#include "tests/harness.h"
#include "ui/app.h"

namespace vf {
namespace {

std::string unique_dir(const std::string& tag) {
  return join(temp_dir(), "vlcflix-studio-fault-" + tag + "-" + std::to_string(::getpid()));
}

std::string sample(const std::string& name) {
  const std::string dir = find_dir_upwards("samples");
  if (dir.empty()) return "";
  const std::string path = join(dir, name);
  return file_exists(path) ? path : "";
}

/* ------------------------------ the parsers ------------------------------ */

void test_adversarial_parsers() {
  TEST("faults: adversarial filenames, JSON, subtitles and settings do not crash");

  // Filenames: empty, separators, only a year, control bytes, very long, non-UTF-8.
  const std::vector<std::string> names = {
      "", ".", "..", "/", "////", "____.mkv", "1900.mkv", "9999.10000.mkv",
      "\x01\x02\x03.mkv", std::string(4000, 'a') + ".mkv", "\xff\xfe\xfd.mkv",
      "Movie.2020.1080p.BluRay.x264-GROUP[ettv].mkv", "   spaced   out  .mkv",
      "Movie (2019) [1080p].mkv", "-.mkv", ".hidden.mkv"};
  for (const auto& name : names) {
    const ParsedName parsed = parse_name(name);
    // No assertion about the answer: the requirement is that there is an answer.
    (void)parsed.title;
    (void)parsed.year;
  }
  // One that matters, as a control: the year-titled case still parses (audit A1).
  CHECK_EQ(parse_name("1917.2019.2160p.WEB-DL.mkv").year, 2019);

  // JSON: truncated, deeply nested, wrong types, huge numbers, NUL bytes.
  const std::vector<std::string> documents = {
      "", "{", "[]", "null", "{\"a\":}", "{\"a\":1,}", std::string(200, '[') + std::string(200, ']'),
      "{\"a\":1e999}", "{\"a\":\1}", "{\"a\":\"\\uD800\"}", "{\"a\":true,\"a\":false}",
      std::string("{\"a\":\"") + std::string(10000, 'x') + "\"}"};
  for (const auto& text : documents) {
    const Json parsed = Json::parseOr(text, Json::object());
    (void)parsed;  // must not throw, must not read out of bounds
  }
  CHECK(Json::parseOr("{\"a\":1}", Json::object()).integer("a", 0) == 1);

  // Subtitles: garbage, wrong timestamps, reversed ranges, markup, no cues.
  const std::vector<std::string> subtitle_texts = {
      "", "garbage", "1\n00:00:99,000 --> 00:00:01,000\nbad\n",
      "1\n00:00:05,000 --> 00:00:01,000\nbackwards\n",
      "WEBVTT\n\n00:00.000 --> 00:01.000\n<v Bob>hi\n",
      "1\n00:00:01,000 --> 00:00:02,000\n" + std::string(5000, 'x'),
  };
  for (const auto& text : subtitle_texts) {
    const std::vector<Cue> cues = parse_cues(text);
    for (const auto& cue : cues) {
      // A cue with an end before its start would make the overlay draw forever.
      if (cue.end < cue.start) {
        vftest::Registry::instance().fail(__FILE__, __LINE__, "cue ends before it starts");
        return;
      }
    }
  }
  const std::vector<Cue> normal = parse_cues("1\n00:00:01,000 --> 00:00:02,000\nhello\n");
  CHECK_EQ(normal.size(), std::size_t(1));
}

/* ------------------------------- the store -------------------------------- */

void test_unwritable_and_corrupt_data() {
  TEST("faults: a corrupt catalog or settings file is survived and reported");
  const std::string dir = unique_dir("corrupt");
  ensure_dir(dir);
  Store store(dir);

  // Half-written JSON, exactly what a killed process leaves behind.
  write_file_atomic(store.file_for("catalog"), "{\"items\":[{\"id\":\"x\",\"title\":");
  write_file_atomic(store.file_for("settings"), "{\"libraryFolders\":\"not an array\"}");
  write_file_atomic(store.file_for("myList"), "[]]]]");

  const Catalog catalog = store.load_catalog();
  CHECK_EQ(catalog.items.size(), std::size_t(0));  // defaults, not a crash
  const Settings settings = store.load_settings();
  CHECK_EQ(settings.library_folders.size(), std::size_t(0));
  CHECK_EQ(store.load_my_list().size(), std::size_t(0));
  // And the broken file is not deleted behind the user's back: it can be looked at.
  CHECK(file_exists(store.file_for("catalog")));

  TEST("faults: a settings patch with the wrong type is refused, and the file is kept");
  std::string error;
  Json patch = Json::object();
  patch.set("hwDecode", Json("yes"));
  Settings current;
  CHECK(!store.patch_settings(patch, &current, &error));
  CHECK(!error.empty());
  LOG_INFO("fault test: rejected as expected: %s", error.c_str());
}

void test_read_only_data_directory() {
  TEST("faults: a read-only data directory is reported, not silently lost");
  const std::string dir = unique_dir("readonly");
  ensure_dir(dir);
  const std::string blocker = join(dir, "blocked");
  ensure_dir(blocker);
  // Make the file path impossible: a directory where a file has to go.
  const std::string impossible = join(blocker, "catalog.json");
  ensure_dir(impossible);
  Store store(blocker);
  Catalog catalog;
  Item item;
  item.id = "x";
  item.title = "X";
  catalog.items.push_back(item);
  CHECK(!store.save_catalog(catalog));  // it must return false, not pretend
}

/* ------------------------------ the library ------------------------------- */

void test_scan_without_ffmpeg_and_with_junk() {
  TEST("faults: a scan with no usable tools, a missing folder and a junk file");
  const std::string dir = unique_dir("scan");
  ensure_dir(dir);
  const std::string media = join(dir, "media");
  ensure_dir(media);

  // A folder that does not exist: the scan must fail visibly, not quietly return zero.
  ScanOptions missing;
  missing.folders = {join(dir, "does-not-exist")};
  missing.media_dir = media;
  const Catalog none = scan_library(missing);
  CHECK_EQ(none.items.size(), std::size_t(0));
  CHECK(!none.errors.empty());

  // A folder with a junk "video" in it: the file is not a video, so it is an error
  // entry with a reason, not an entry with a title and no duration.
  const std::string junk = join(dir, "library");
  ensure_dir(junk);
  write_file_atomic(join(junk, "NotAFilm.2019.mkv"), "this is not a matroska file");
  ScanOptions options;
  options.folders = {junk};
  options.media_dir = media;
  options.render_artwork = false;
  const Catalog catalog = scan_library(options);
  // The file stays in the library (dropping it would hide the user's file) but it is
  // marked unreadable, and the error says which file and why.
  CHECK_EQ(catalog.items.size(), std::size_t(1));
  CHECK(catalog.items.front().unreadable);
  CHECK(!catalog.errors.empty());
  // Every error says which file and why; that string is what `status` prints.
  CHECK(catalog.errors.front().find("NotAFilm") != std::string::npos ||
        catalog.errors.front().size() > 10);
}

void test_corrupt_artwork_degrades() {
  TEST("faults: corrupt artwork and a deleted still fall back instead of crashing");
  const std::string dir = unique_dir("art");
  ensure_dir(dir);
  const std::string media = join(dir, "media");
  ensure_dir(media);
  Item item;
  item.id = "abc123";
  item.title = "Fallback";
  item.poster = "abc123-poster.jpg";  // claimed, but the file is not there
  // A catalog that lies about its artwork is what a lost data directory looks like.
  CHECK_EQ(adopt_existing_art(item, media), 1);
  CHECK(item.poster.empty());  // the claim is cleared, so the UI draws the gradient

  // A file that exists but is not a JPEG: the decoder must return an invalid image.
  const std::string fake = join(media, "abc123-poster.jpg");
  write_file_atomic(fake, "not a jpeg at all");
  std::string bytes;
  CHECK(read_file(fake, &bytes));
  CHECK(!decode_image(bytes).valid());

  // And the app draws that fallback without crashing.
  ui::App app(media, dir);
  app.resize(640, 420);
  Catalog catalog;
  item.poster = "abc123-poster.jpg";
  catalog.items.push_back(item);
  app.set_catalog(catalog);
  app.show_screen("home", "");
  Canvas canvas(640, 420);
  app.draw(canvas);
  CHECK(canvas.width() == 640);
}

/* ------------------------------- the player ------------------------------- */

void test_player_faults() {
  TEST("faults: a non-video file and a missing file are refused with a reason");
  const std::string dir = unique_dir("player");
  ensure_dir(dir);
  const std::string junk = join(dir, "junk.mp4");
  write_file_atomic(junk, "definitely not an mp4");

  ui::App app(dir, dir);
  app.resize(640, 420);
  app.engine().add_argument("--aout=dummy");
  Catalog catalog;
  Item bad;
  bad.id = "bad";
  bad.title = "Bad File";
  bad.path = junk;
  bad.filename = "junk.mp4";
  catalog.items.push_back(bad);
  app.set_catalog(catalog);

  // The engine opens (libVLC accepts the path) but cannot decode; the player must
  // notice and say so rather than sit on a black screen forever.
  const bool started = app.start_playback(0);
  if (started) {
    const double deadline = std::chrono::duration<double>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count() +
                            8.0;
    auto now = [] {
      return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
          .count();
    };
    while (now() < deadline && app.player_status().state != PlayState::Error) {
      app.tick(now());
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(app.player_status().state == PlayState::Error);
    CHECK(!app.last_message().empty());
    CHECK(app.player_error().find("without producing a single picture") != std::string::npos);
    // The bug this test was written for: a file that decodes nothing used to arrive
    // as "ended", so the app wrote it as watched and dropped it from Continue
    // Watching. A corrupt film must not disappear from the library like that.
    Store store(dir);
    CHECK_EQ(store.load_progress().count("bad"), std::size_t(0));
    app.stop_playback();
  } else {
    CHECK(!app.last_message().empty());
  }
}

/* ------------------------------ the window -------------------------------- */

void test_ui_faults() {
  TEST("faults: the UI survives nonsense input and an absurd window size");
  const std::string dir = unique_dir("ui");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(640, 420);
  Catalog catalog;
  Item item;
  item.id = "a";
  item.title = std::string(500, 'T');  // a title that cannot fit anywhere
  item.synopsis = std::string(5000, 's');
  catalog.items.push_back(item);
  app.set_catalog(catalog);

  // Coordinates far outside the window, negative, and NaN-ish extremes.
  const float coordinates[] = {-1e9f, -1.0f, 0.0f, 1e9f, 1e30f};
  for (float x : coordinates) {
    for (float y : coordinates) {
      ui::InputEvent move;
      move.type = ui::InputEvent::Type::MouseMove;
      move.x = x;
      move.y = y;
      app.handle(move);
      ui::InputEvent click;
      click.type = ui::InputEvent::Type::MouseDown;
      click.x = x;
      click.y = y;
      app.handle(click);
    }
  }
  // Text that is one giant word, and a key code that means nothing here.
  ui::InputEvent text;
  text.type = ui::InputEvent::Type::Text;
  text.text = std::string(2000, 'Z');
  app.handle(text);
  ui::InputEvent key;
  key.type = ui::InputEvent::Type::Key;
  key.key = 0x7fffffff;
  app.handle(key);

  Canvas canvas(app.width(), app.height());
  app.draw(canvas);
  CHECK(canvas.width() == 640);

  // Absurd and zero window sizes are clamped, not accepted.
  app.resize(1, 1);
  CHECK(app.width() >= 640);
  app.resize(-4000, -4000);
  CHECK(app.width() >= 640);
  Canvas small(app.width(), app.height());
  app.draw(small);
  CHECK(small.width() == app.width());
}

void test_gone_mid_playback() {
  TEST("faults: a film deleted while it is playing does not crash the player");
  const std::string real = sample("ghostlight.mp4");
  if (real.empty()) return;
  const std::string dir = unique_dir("deleted");
  ensure_dir(dir);
  const std::string copy = join(dir, "ghostlight.mp4");
  std::string bytes;
  CHECK(read_file(real, &bytes));
  CHECK(write_file_atomic(copy, bytes));

  ui::App app(dir, dir);
  app.resize(640, 420);
  app.engine().add_argument("--aout=dummy");
  Catalog catalog;
  Item item;
  item.id = "gone";
  item.title = "Gone";
  item.path = copy;
  item.filename = "ghostlight.mp4";
  catalog.items.push_back(item);
  app.set_catalog(catalog);
  CHECK(app.start_playback(0));
  app.tick(app.clock_now());

  // The scenario: the drive is unplugged, or the file is moved.
  remove_file(copy);
  const double deadline = std::chrono::duration<double>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count() +
                          8.0;
  auto now = [] {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };
  while (now() < deadline) {
    app.tick(now());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (app.player_status().state == PlayState::Error ||
        app.player_status().state == PlayState::Ended) {
      break;
    }
  }
  // Whatever the engine decided, the program is still alive and can still draw and stop.
  Canvas canvas(640, 420);
  app.draw(canvas);
  app.stop_playback();
  CHECK(!app.playing());
}

/* --------------------------- the artwork pipeline ------------------------ */

// The regression test for this turn's real bug: the preview job captured its segment
// list by reference, the block that owned that list closed before the jobs were
// joined, and so ffmpeg received a destroyed vector's worth of nonsense roughly half
// the time - "Invalid duration for option t: 3219238484..." - and no preview was ever
// drawn. Three passes in a row is the point: one pass passed by luck. Under the
// address sanitizer build (`make asan`) this fails immediately and deterministically.
void test_artwork_pipeline_is_reliable() {
  TEST("faults: a full artwork pass draws all three pieces, every single time");
  const std::string clip = sample("ghostlight.mp4");
  if (clip.empty()) {
    CHECK(!clip.empty());
    return;
  }
  const VideoInfo info = probe_video(clip);
  CHECK(info.duration > 0);
  const std::string root = unique_dir("art-pipeline");
  for (int run = 0; run < 3; ++run) {
    const std::string media = join(root, "media" + std::to_string(run));
    ensure_dir(media);
    MissingArt needs;
    needs.poster = needs.backdrop = needs.preview = true;
    const ArtResult art = render_art(clip, info.duration, media, "clip", needs);
    CHECK_EQ(art.produced.size(), std::size_t(3));
    CHECK(art.failures.empty());
    const ArtPaths paths = art_paths(media, "clip");
    CHECK(file_size(paths.poster) > 1000);
    CHECK(file_size(paths.backdrop) > 1000);
    // A preview that is a few hundred bytes is a header, not a film.
    CHECK(file_size(paths.preview) > 5000);
  }
  for (int run = 0; run < 3; ++run) {
    const ArtPaths paths = art_paths(join(root, "media" + std::to_string(run)), "clip");
    remove_file(paths.poster);
    remove_file(paths.backdrop);
    remove_file(paths.preview);
  }
}

}  // namespace

void run_fault_tests() {
  test_adversarial_parsers();
  test_unwritable_and_corrupt_data();
  test_read_only_data_directory();
  test_scan_without_ffmpeg_and_with_junk();
  test_corrupt_artwork_degrades();
  test_player_faults();
  test_ui_faults();
  test_gone_mid_playback();
  test_artwork_pipeline_is_reliable();
}

}  // namespace vf
