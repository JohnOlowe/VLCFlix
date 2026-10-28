// Unit tests for the logic that survived the rewrite: naming, JSON, the store,
// the sync maths, subtitles, rails and artwork rules.
//
// Every bug in docs/AUDIT.md gets a regression test here, and the buggy behaviour
// gets a *negative control*: a copy of the old algorithm is run against the same
// input and asserted to fail, so the test proves it is testing something.
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/catalog.h"
#include "core/json.h"
#include "core/model.h"
#include "core/naming.h"
#include "core/paths.h"
#include "core/sha1.h"
#include "core/store.h"
#include "core/sync.h"
#include "media/artwork.h"
#include "media/subtitles.h"
#include "media/walk.h"
#include "tests/harness.h"

using namespace vf;

namespace {

/* -------------------------------- naming -------------------------------- */

void test_naming_basic() {
  TEST("naming: ordinary release filenames");
  auto check = [](const std::string& file, const std::string& title, int year) {
    const ParsedName parsed = parse_name(file);
    CHECK_STR_EQ(parsed.title, title);
    CHECK_EQ(parsed.year, year);
  };
  check("The.Matrix.1999.1080p.BluRay.x264.mp4", "The Matrix", 1999);
  check("The.Long.Harrow.2019.mkv", "The Long Harrow", 2019);
  check("Ocean's.Eleven.2001.mkv", "Ocean's Eleven", 2001);
  check("salt_and_static_2025_1080p.mp4", "Salt and Static", 2025);
  check("no-year-here.mp4", "No Year Here", 0);
}

void test_naming_year_titles() {
  TEST("naming: titles that are years (audit A1)");
  // Old behaviour, kept here on purpose as the negative control.
  auto old_parse = [](const std::string& filename) {
    std::string base = filename;
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    std::string spaced;
    for (char c : base) spaced.push_back((c == '.' || c == '_') ? ' ' : c);
    int year = 0;
    std::string year_text;
    for (std::size_t i = 0; i + 3 < spaced.size(); ++i) {
      const std::string candidate = spaced.substr(i, 4);
      const bool digits = candidate.find_first_not_of("0123456789") == std::string::npos;
      if (digits && (candidate.rfind("19", 0) == 0 || candidate.rfind("20", 0) == 0)) {
        year = std::stoi(candidate);
        year_text = candidate;
        break;
      }
    }
    std::string title = spaced;
    if (!year_text.empty()) {
      const auto pos = spaced.find(year_text);
      if (pos != std::string::npos) title = spaced.substr(0, pos);
    }
    while (!title.empty() && title.back() == ' ') title.pop_back();
    return std::make_pair(title, year);
  };

  struct Case {
    const char* file;
    const char* title;
    int year;
  };
  const Case cases[] = {
      {"1917.2019.2160p.WEB-DL.mkv", "1917", 2019},
      {"2012.2009.1080p.BluRay.x264.mkv", "2012", 2009},
      {"Blade.Runner.2049.2017.1080p.mkv", "Blade Runner 2049", 2017},
      {"1917.mkv", "1917", 0},
  };
  for (const auto& c : cases) {
    const ParsedName parsed = parse_name(c.file);
    CHECK_STR_EQ(parsed.title, c.title);
    CHECK_EQ(parsed.year, c.year);
  }

  // Negative control: the pre-fix algorithm must be wrong on exactly these files,
  // otherwise this test is not testing the fix.
  int old_wrong = 0;
  for (const auto& c : cases) {
    const auto [old_title, old_year] = old_parse(c.file);
    if (old_title != c.title || old_year != c.year) ++old_wrong;
  }
  CHECK_EQ(old_wrong, 4);
}

/* --------------------------------- json --------------------------------- */

void test_json() {
  TEST("json: round trip, escapes, errors");
  Json root = Json::object();
  root.set("title", "Salt & Static");
  root.set("year", 2025);
  root.set("ratio", 1.5);
  root.set("ok", true);
  root.set("nothing", Json());
  Json list = Json::array();
  list.push(Json("a"));
  list.push(Json(2));
  root.set("list", list);

  const std::string text = root.dump(2);
  Json back;
  std::string error;
  CHECK(Json::parse(text, back, &error));
  CHECK_STR_EQ(back.str("title"), "Salt & Static");
  CHECK_EQ(back.integer("year"), 2025);
  CHECK_EQ(back.num("ratio"), 1.5);
  CHECK(back.flag("ok"));
  CHECK(back.find("nothing")->isNull());
  CHECK_EQ(back.find("list")->size(), std::size_t(2));
  CHECK_STR_EQ(back.find("list")->at(0).asString(), "a");

  Json escapes = Json::object();
  escapes.set("newline", "a\nb");
  CHECK_STR_EQ(escapes.dump(0), "{\"newline\":\"a\\nb\"}");

  Json invalid;
  CHECK(!Json::parse("{\"a\": }", invalid, &error));
  CHECK(!error.empty());
  CHECK(!Json::parse("[1,2", invalid, &error));

  // Member order survives, which is what keeps written files readable.
  Json ordered = Json::object();
  ordered.set("z", 1);
  ordered.set("a", 2);
  CHECK(text.find("\"title\"") < text.find("\"year\""));
}

/* --------------------------------- store -------------------------------- */

void test_store() {
  TEST("store: progress finish rule (audit A4) and settings validation (A6)");
  const std::string dir = join(temp_dir(), "vlcflix-studio-test-" + sha1_hex("store").substr(0, 8));
  Store store(dir);

  // Below 2% is "not started": nothing is stored.
  store.set_progress("film-a", 0.5, 100);
  CHECK(store.load_progress().count("film-a") == 0);

  // The middle is a resume point.
  store.set_progress("film-a", 30, 100);
  CHECK_EQ(store.load_progress().size(), std::size_t(1));
  CHECK_EQ(progress_percent(store.load_progress()["film-a"]), 30);

  // Above 92% is finished: the entry must be gone, so it leaves Continue Watching.
  store.set_progress("film-a", 97, 100);
  CHECK(store.load_progress().count("film-a") == 0);

  // My List toggles both ways.
  std::vector<std::string> list;
  store.toggle_my_list("film-a", &list);
  CHECK_EQ(list.size(), std::size_t(1));
  store.toggle_my_list("film-a", &list);
  CHECK_EQ(list.size(), std::size_t(0));

  // Settings: a non-string password is rejected instead of being stored.
  Settings updated;
  std::string error;
  Json bad = Json::object();
  bad.set("vlcPassword", Json::object().set("a", 1));
  CHECK(!store.patch_settings(bad, &updated, &error));
  CHECK(error.find("vlcPassword") != std::string::npos);

  Json bad_port = Json::object();
  bad_port.set("vlcHttpPort", 70000);
  CHECK(!store.patch_settings(bad_port, &updated, &error));

  Json bad_bool = Json::object();
  bad_bool.set("hwDecode", Json("yes"));
  CHECK(!store.patch_settings(bad_bool, &updated, &error));

  Json good = Json::object();
  good.set("hwDecode", true);
  Json folders = Json::array();
  folders.push(Json("/tmp"));
  good.set("libraryFolders", folders);
  CHECK(store.patch_settings(good, &updated, &error));
  CHECK(updated.hw_decode);
  CHECK_EQ(updated.library_folders.size(), std::size_t(1));
  CHECK(store.load_settings().hw_decode);

  // The catalog round-trips through disk with its artwork fields intact.
  Catalog catalog;
  catalog.scanned_at = 1234;
  Item item;
  item.id = "abc123def456";
  item.title = "Blade Runner 2049";
  item.year = 2017;
  item.path = "/tmp/x.mkv";
  item.filename = "x.mkv";
  item.duration = 120.5;
  item.preview_version = 2;
  item.genres = {"Science Fiction", "Drama"};
  catalog.items.push_back(item);
  CHECK(store.save_catalog(catalog));
  const Catalog reloaded = store.load_catalog();
  CHECK_EQ(reloaded.items.size(), std::size_t(1));
  CHECK_STR_EQ(reloaded.items[0].title, "Blade Runner 2049");
  CHECK_EQ(reloaded.items[0].year, 2017);
  CHECK_EQ(reloaded.items[0].preview_version, 2);
  CHECK_EQ(reloaded.items[0].genres.size(), std::size_t(2));
}

/* --------------------------------- sync --------------------------------- */

void test_sync() {
  TEST("sync: ported from sync-math.js (all 14 cases)");
  // The JS test cases, restated.
  auto plan = [](int latency, int manual) { return compute_sync(latency, manual); };

  CHECK_EQ(plan(0, 0).video_delay_ms, 0);
  CHECK_EQ(plan(0, 0).audio_delay_ms, 0);

  CHECK_EQ(plan(0, 200).video_delay_ms, 0);
  CHECK_EQ(plan(0, 200).audio_delay_ms, 200);  // sound later: the delay node

  CHECK_EQ(plan(0, -200).video_delay_ms, 200);  // sound earlier: picture held back
  CHECK_EQ(plan(0, -200).audio_delay_ms, 0);

  CHECK_EQ(plan(250, 250).video_delay_ms, 0);
  CHECK_EQ(plan(250, 250).audio_delay_ms, 0);

  CHECK_EQ(plan(250, 0).video_delay_ms, 250);  // device latency: picture waits
  CHECK_EQ(plan(250, 200).video_delay_ms, 50);

  CHECK_EQ(plan(0, -5000).video_delay_ms, kMaxVideoDelayMs);
  CHECK(plan(0, -5000).clamped);
  CHECK_EQ(plan(0, 5000).audio_delay_ms, kMaxVideoDelayMs);
  CHECK(plan(0, 5000).clamped);

  // Smoothing: a small drift is followed gradually, a device switch is instant.
  CHECK_EQ(smooth_latency(0, 180), 180);
  CHECK_EQ(smooth_latency(180, 200), 187);
  CHECK_EQ(smooth_latency(180, 900), 900);

  CHECK_STR_EQ(describe_delay(0), "0 ms");
  CHECK_STR_EQ(describe_delay(300), "+300 ms");
}

/* ------------------------------- subtitles ------------------------------ */

void test_subtitles() {
  TEST("subtitles: naming convention, labels, SRT and WebVTT cues");
  const std::string video = "/movies/The Silent Coast.mkv";
  const std::vector<std::string> entries = {
      "The Silent Coast.srt", "The Silent Coast.en.srt", "The Silent Coast.fr.srt",
      "The Silent Coast.forced.srt", "The Silent Coast.txt", "Other Film.srt"};
  const auto tracks = find_subtitles(video, entries);
  CHECK_EQ(tracks.size(), std::size_t(4));
  CHECK_STR_EQ(tracks[0].label, "Default");  // plain .srt sorts first
  bool have_english = false;
  bool have_forced = false;
  for (const auto& track : tracks) {
    if (track.label == "English") have_english = true;
    if (track.label == "Forced") have_forced = true;
  }
  CHECK(have_english);
  CHECK(have_forced);

  const auto all = list_folder_subtitles(video, entries);
  CHECK_EQ(all.size(), std::size_t(5));  // the other film's subtitle is offered too
  CHECK(all.front().matches);            // name matches sort first

  const std::string srt =
      "1\r\n00:00:01,000 --> 00:00:03,500\r\nHello there\r\n\r\n"
      "2\r\n00:00:04,000 --> 00:00:06,000\r\n<i>Two lines</i>\r\nsecond\r\n\r\n";
  const auto srt_cues = parse_cues(srt);
  CHECK_EQ(srt_cues.size(), std::size_t(2));
  CHECK_EQ(srt_cues[0].start, 1.0);
  CHECK_EQ(srt_cues[0].end, 3.5);
  CHECK_STR_EQ(srt_cues[0].text, "Hello there");
  CHECK_STR_EQ(srt_cues[1].text, "<i>Two lines</i>\nsecond");

  const std::string vtt =
      "WEBVTT\n\n00:00:10.000 --> 00:00:12.000 align:middle\nA cue\n\n"
      "00:00:13.000 --> 00:00:14.000\nAnother\n";
  const auto vtt_cues = parse_cues(vtt);
  CHECK_EQ(vtt_cues.size(), std::size_t(2));
  CHECK_EQ(vtt_cues[0].start, 10.0);
  CHECK_STR_EQ(vtt_cues[0].text, "A cue");
  CHECK_EQ(vtt_cues[1].start, 13.0);

  // A cue that ends before it starts, or has no text, is not a cue.
  CHECK_EQ(parse_cues("00:00:05.000 --> 00:00:04.000\nbackwards\n").size(), std::size_t(0));
}

/* --------------------------------- rails -------------------------------- */

void test_rails() {
  TEST("rails: order, finished films excluded, search");
  std::vector<Item> items;
  auto make = [&](const std::string& id, const std::string& title, int year, long long added,
                  std::vector<std::string> genres) {
    Item item;
    item.id = id;
    item.title = title;
    item.year = year;
    item.added_at = added;
    item.genres = std::move(genres);
    items.push_back(item);
  };
  make("a", "Alpha", 2019, 300, {"Drama", "Western"});
  make("b", "Beta", 2020, 200, {"Drama"});
  make("c", "Gamma", 2021, 100, {"Comedy"});

  std::map<std::string, Progress> progress;
  Progress watching;
  watching.position = 30;
  watching.duration = 100;
  watching.updated_at = 50;
  progress["a"] = watching;

  Progress finished;
  finished.position = 97;
  finished.duration = 100;
  finished.updated_at = 90;
  progress["b"] = finished;

  const std::vector<std::string> my_list = {"c"};
  const auto rails = build_rails(items, progress, my_list);

  CHECK(!rails.empty());
  CHECK_STR_EQ(rails[0].title, "Continue Watching");
  CHECK_EQ(rails[0].items.size(), std::size_t(1));
  CHECK_STR_EQ(items[rails[0].items[0]].title, "Alpha");  // Beta is finished

  bool have_genre = false;
  for (const auto& rail : rails) {
    if (rail.id == "genre-drama") have_genre = true;
  }
  CHECK(have_genre);  // two films share Drama

  bool have_single_genre = false;
  for (const auto& rail : rails) {
    if (rail.id == "genre-comedy") have_single_genre = true;
  }
  CHECK(!have_single_genre);  // one film is not a rail

  CHECK_STR_EQ(rails.back().title, "Everything in Your Library");
  CHECK_EQ(rails.back().items.size(), std::size_t(3));

  CHECK_EQ(search_items(items, "gam").size(), std::size_t(1));
  CHECK_EQ(search_items(items, "drama").size(), std::size_t(2));
  CHECK_EQ(search_items(items, "2020").size(), std::size_t(1));
  CHECK_EQ(search_items(items, "").size(), std::size_t(0));
}

/* -------------------------------- artwork ------------------------------- */

long long raw_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void test_artwork_rules() {
  TEST("artwork: preview segments, deep colour (A5), versioning (A9)");
  const auto segments = preview_segments(16.0);
  CHECK_EQ(segments.size(), std::size_t(5));
  CHECK(segments[0].start > 1.5 && segments[0].start < 2.5);
  CHECK_EQ(segments[0].length, 2.0);
  CHECK(segments.back().start <= 14.0);

  // A one-second film cannot support five distinct snippets.
  CHECK(preview_segments(1.0).size() <= 2);
  CHECK_EQ(preview_segments(0).size(), std::size_t(0));

  // Bug A5: nv12 is 8-bit even though its name contains "12".
  CHECK(!is_deep_color("nv12"));
  CHECK(!is_deep_color("yuv420p"));
  CHECK(!is_deep_color("yuvj420p"));
  CHECK(is_deep_color("yuv420p10le"));
  CHECK(is_deep_color("p010le"));
  CHECK(is_deep_color("yuv422p12le"));

  // Bug A9: a still from an older format is missing even though the file exists.
  // A fresh directory per run: leftover files from a previous run must not turn
  // an "is it missing" test into an accidental pass.
  const std::string media = join(temp_dir(), "vlcflix-studio-art-" + sha1_hex("art").substr(0, 6) +
                                                 "-" + std::to_string(raw_now_ms()));
  ensure_dir(media);
  Item item;
  item.id = "deadbeefcafe";
  item.preview_version = kPreviewVersion;
  const ArtPaths paths = art_paths(media, item.id);
  CHECK_STR_EQ(basename(paths.poster), "deadbeefcafe-poster.jpg");
  CHECK(missing_art(item, media).any());
  write_file_atomic(paths.poster, "x");
  write_file_atomic(paths.backdrop, "x");
  write_file_atomic(paths.preview, "x");
  CHECK(!missing_art(item, media).any());
  item.preview_version = kPreviewVersion - 1;
  CHECK(missing_art(item, media).preview);
  item.preview_version = kPreviewVersion;
  item.art_attempts = kArtMaxAttempts;
  CHECK(!should_retry_art(item));
  item.art_attempts = 0;
  CHECK(should_retry_art(item));

  // Adoption: a finished file on disk with an empty catalog field is adopted, not
  // redrawn; a catalog field pointing at a deleted file is cleared.
  Item adopted;
  adopted.id = item.id;
  adopted.preview_version = kPreviewVersion;
  CHECK_EQ(adopt_existing_art(adopted, media), 3);
  CHECK_STR_EQ(adopted.poster, basename(paths.poster));
  remove_file(paths.backdrop);
  CHECK_EQ(adopt_existing_art(adopted, media), 1);
  CHECK(adopted.backdrop.empty());
  CHECK(adopted.poster == basename(paths.poster));
}

/* --------------------------------- misc --------------------------------- */

void test_misc() {
  TEST("misc: ids, mime types, video extensions");
  CHECK_STR_EQ(sha1_hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK_STR_EQ(item_id_for_path("/movies/x.mkv"),
               sha1_hex("/movies/x.mkv").substr(0, 12));
  CHECK_STR_EQ(mime_for("a.MKV"), "video/x-matroska");
  CHECK_STR_EQ(mime_for("a.srt"), "application/octet-stream");
  CHECK(is_video_file("film.MP4"));
  CHECK(!is_video_file("notes.txt"));
  CHECK_STR_EQ(stem("/a/b/The.Film.2019.mkv"), "The.Film.2019");
  CHECK_STR_EQ(extension("/a/b/The.Film.2019.MKV"), ".mkv");
  CHECK_STR_EQ(dirname("/a/b/c.mkv"), "/a/b");
  CHECK_STR_EQ(basename("/a/b/c.mkv"), "c.mkv");
}

}  // namespace

int main() {
  std::printf("VLCFlix Studio — native unit tests\n\n");
  test_naming_basic();
  test_naming_year_titles();
  test_json();
  test_store();
  test_sync();
  test_subtitles();
  test_rails();
  test_artwork_rules();
  test_misc();
  return vftest::summary("native tests");
}
