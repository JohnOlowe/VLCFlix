// Playback tests: the engine, the presenter, and the player view.
//
// These are not mocks. libVLC really decodes the bundled samples, the presenter really
// holds frames, and the App really starts, resumes and finishes a film. Where a test
// needs a clock it injects one, so nothing here sleeps longer than it must.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "core/model.h"
#include "core/paths.h"
#include "core/store.h"
#include "core/sync.h"
#include "library/subtitles.h"
#include "playback/engine.h"
#include "playback/presenter.h"
#include "render/canvas.h"
#include "tests/harness.h"
#include "ui/app.h"

namespace vf {
namespace {

double now_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string sample(const std::string& name) {
  static const std::string dir = find_dir_upwards("samples");
  if (dir.empty()) return "";
  const std::string path = join(dir, name);
  return file_exists(path) ? path : "";
}

Frame make_frame(int width, int height, uint8_t value, double arrival) {
  Frame frame;
  frame.width = width;
  frame.height = height;
  frame.arrival = arrival;
  frame.pixels.assign(static_cast<std::size_t>(width) * height * 4, value);
  return frame;
}

// A frame with structure, so "is there a picture" is measurable rather than assumed.
bool frame_has_content(const Frame& frame) {
  if (frame.pixels.empty() || frame.width <= 0) return false;
  uint8_t lowest = 255;
  uint8_t highest = 0;
  for (std::size_t i = 0; i < frame.pixels.size(); i += 4 * 97) {
    lowest = std::min(lowest, frame.pixels[i]);
    highest = std::max(highest, frame.pixels[i]);
  }
  return highest - lowest > 8;
}

void test_presenter() {
  TEST("presenter: with no delay the newest frame is shown at once");
  Presenter presenter;
  presenter.configure(0);
  CHECK_EQ(presenter.delay_ms(), 0);
  CHECK_EQ(presenter.target_depth(), 1);
  presenter.push(make_frame(4, 4, 10, 0.0), 0.0);
  presenter.push(make_frame(4, 4, 20, 0.01), 0.01);
  CHECK(presenter.advance(0.01));
  CHECK_EQ(static_cast<int>(presenter.current().pixels[0]), 20);  // the newer one, not the older
  CHECK_EQ(presenter.queued(), 0);

  TEST("presenter: a 200 ms picture delay holds the frame for 200 ms");
  Presenter delayed;
  delayed.configure(200, 60);
  CHECK_EQ(delayed.delay_ms(), 200);
  // 200 ms of delay at 60 fps is several frames, so the queue is deeper than one.
  CHECK(delayed.target_depth() >= 2);
  delayed.push(make_frame(4, 4, 30, 0.0), 0.0);
  CHECK(!delayed.advance(0.05));  // not due yet
  CHECK(delayed.held() > 0);
  CHECK(delayed.advance(0.21));  // due
  CHECK(delayed.shown() == 1);

  TEST("presenter: the queue is bounded, and overflow drops the oldest");
  Presenter bounded;
  bounded.configure(100, 60);
  const int depth = bounded.target_depth();
  for (int i = 0; i < depth + 5; ++i) {
    bounded.push(make_frame(2, 2, static_cast<uint8_t>(i), i * 0.001), i * 0.001);
  }
  CHECK_EQ(bounded.queued(), depth);
  CHECK(bounded.dropped() >= 5);
}

void test_engine_plays_a_sample() {
  const std::string path = sample("ghostlight.mp4");
  if (path.empty()) {
    TEST("engine: playback of a real sample");
    vftest::Registry::instance().fail(__FILE__, __LINE__, "samples/ghostlight.mp4 not found");
    return;
  }
  TEST("engine: libVLC decodes a real sample into our buffer");
  Engine engine;
  engine.add_argument("--aout=dummy");  // no sound card in CI, and none needed here
  engine.configure(320, 180);
  std::string error;
  CHECK(engine.open(path, &error));
  engine.play();

  Frame frame;
  bool got_frame = false;
  const double deadline = now_seconds() + 10.0;
  while (now_seconds() < deadline && !got_frame) {
    engine.take_frame(&frame);
    if (frame.width > 0) got_frame = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(got_frame);
  CHECK_EQ(frame.width, 320);
  CHECK_EQ(frame.height, 180);
  CHECK(frame_has_content(frame));

  // Duration comes from the engine, not from ffprobe.
  EngineStatus status = engine.status();
  const double length_deadline = now_seconds() + 5.0;
  while (status.length <= 0 && now_seconds() < length_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    status = engine.status();
  }
  CHECK(std::fabs(status.length - 16.0) < 1.0);
  CHECK(status.frames_decoded > 0);
  engine.stop();
}

void test_engine_seeks_where_the_pipe_engine_could_not() {
  const std::string path = sample("the-silent-coast.mp4");
  if (path.empty()) {
    TEST("engine: seek accuracy (audit A2)");
    vftest::Registry::instance().fail(__FILE__, __LINE__, "samples/the-silent-coast.mp4 not found");
    return;
  }
  TEST("engine: seeks a single-keyframe file the ffmpeg pipe engine could not (A2)");
  Engine engine;
  engine.add_argument("--aout=dummy");
  engine.configure(320, 180);
  std::string error;
  CHECK(engine.open(path, &error));
  engine.play();

  Frame frame;
  const double deadline = now_seconds() + 10.0;
  while (now_seconds() < deadline) {
    engine.take_frame(&frame);
    if (engine.status().time > 0.2) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // The file has a single keyframe. The ffmpeg pipe engine could only restart at the
  // beginning (bug A2); libVLC seeks properly, which is what this measures.
  engine.seek(12.0);
  long long frames_after_seek = 0;
  bool reached = false;
  const double seek_deadline = now_seconds() + 8.0;
  while (now_seconds() < seek_deadline) {
    if (engine.take_frame(&frame)) ++frames_after_seek;
    if (engine.status().time > 11.5) reached = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(reached);              // the clock moved to 12 s and kept going
  CHECK(frames_after_seek > 0);  // and pictures came with it
  CHECK(engine.status().time > 11.5);
  engine.stop();
}

Catalog catalog_for_samples() {
  Catalog catalog;
  for (const char* name : {"ghostlight", "the-silent-coast"}) {
    const std::string video = sample(std::string(name) + ".mp4");
    if (video.empty()) continue;
    Item item;
    item.id = std::string("id-") + name;
    item.title = name;
    item.path = video;
    item.filename = std::string(name) + ".mp4";
    item.duration = 16;
    item.width = 1280;
    item.height = 720;
    catalog.items.push_back(item);
  }
  return catalog;
}

void test_app_player_journey() {
  const std::string dir = join(temp_dir(), "vlcflix-studio-player");
  ensure_dir(dir);
  Catalog catalog = catalog_for_samples();
  if (catalog.items.empty()) {
    TEST("player: journey");
    vftest::Registry::instance().fail(__FILE__, __LINE__, "samples missing");
    return;
  }

  TEST("player: starts, resumes at the saved position, and reports a real picture");
  ui::App app(dir, dir);
  app.resize(1280, 720);
  app.set_catalog(catalog);
  app.engine().add_argument("--aout=dummy");

  // Half-watched: the player should seek there by itself once the length is known.
  std::map<std::string, Progress> progress;
  progress[catalog.items[0].id] = Progress{8.0, 16.0, 1};
  app.set_progress(progress);

  CHECK(app.start_playback(0));
  CHECK(app.playing());
  const double deadline = now_seconds() + 12.0;
  while (now_seconds() < deadline) {
    app.tick(app.clock_now());
    if (app.player_status().time > 7.0 && app.presenter().has_frame()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(app.presenter().has_frame());
  CHECK(app.player_status().time > 7.0);  // resumed from 8 s, not from zero
  CHECK(app.presenter().shown() > 0);

  // The chrome is drawn over a real frame, and every control is clickable.
  Canvas canvas(1280, 720);
  app.draw(canvas);
  auto has = [&](ui::Action::Kind kind) {
    for (const auto& hit : app.hit_rects()) {
      if (hit.second.kind == kind) return true;
    }
    return false;
  };
  CHECK(has(ui::Action::Kind::TogglePause));
  CHECK(has(ui::Action::Kind::Seek));
  CHECK(has(ui::Action::Kind::SeekRelative));
  CHECK(has(ui::Action::Kind::CycleSubtitles));
  CHECK(has(ui::Action::Kind::ClosePlayer));
  CHECK(has(ui::Action::Kind::NudgeSync));

  TEST("player: pause, and the sync offset actually reach the engine");
  ui::InputEvent key;
  key.type = ui::InputEvent::Type::Key;
  key.key = ' ';
  app.handle(key);
  // libVLC applies the pause asynchronously, so wait for the state rather than
  // assuming it lands within the same call.
  double pause_deadline = now_seconds() + 3.0;
  while (now_seconds() < pause_deadline && app.player_status().state != PlayState::Paused) {
    app.tick(app.clock_now());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(app.player_status().state == PlayState::Paused);
  key.key = ' ';
  app.handle(key);

  // compute_sync() decides which side pays: a positive manual offset is already
  // "sound later", so no picture delay is needed and libVLC delays the audio itself.
  key.key = ']';
  app.handle(key);
  CHECK_EQ(app.sync_audio_offset_ms(), 20);
  CHECK_EQ(app.sync_plan().video_delay_ms, 0);
  CHECK_EQ(app.sync_plan().audio_delay_ms, 20);
  // Press until the offset is where the test wants it, so the test does not depend on
  // how many steps the key takes.
  for (int press = 0; press < 20 && app.sync_audio_offset_ms() > -60; ++press) {
    key.key = '[';
    app.handle(key);
  }
  CHECK_EQ(app.sync_audio_offset_ms(), -60);
  CHECK_EQ(app.sync_plan().audio_delay_ms, 0);
  CHECK_EQ(app.sync_plan().video_delay_ms, 60);
  // The presenter was reconfigured with the plan, which is how the picture pays.
  CHECK_EQ(app.presenter().delay_ms(), 60);
  CHECK(app.presenter().target_depth() >= 2);

  TEST("player: the sidecar subtitles are found, read and timed off the engine clock");
  // samples/ghostlight.en.srt sits beside the film, so the scanner finds it the same
  // way it would for a real library. Restart playback so the new catalog is picked up.
  app.stop_playback();
  catalog.items[0].subtitles =
      find_subtitles(catalog.items[0].path, list_dir(find_dir_upwards("samples")));
  CHECK(!catalog.items[0].subtitles.empty());
  app.set_catalog(catalog);
  CHECK(app.start_playback(0));
  CHECK(!app.subtitle_tracks().empty());
  key.key = 's';
  app.handle(key);
  CHECK_EQ(app.subtitle_track(), 0);
  const std::string at_two = app.active_subtitle_text(2.0);
  const std::string at_twenty = app.active_subtitle_text(20.0);
  CHECK(!at_two.empty());
  CHECK(at_two.find("stage lights") != std::string::npos);
  CHECK(at_twenty.empty());
  // An offset moves the window: +3 s makes 0.5 s read as 3.5 s of the cue list.
  app.set_subtitle_offset_ms(3000);
  CHECK(!app.active_subtitle_text(0.5).empty());

  TEST("player: stopping saves progress; finishing removes it from Continue Watching (A4)");
  app.stop_playback();
  CHECK(!app.playing());
  Store store(dir);
  const auto saved = store.load_progress();
  CHECK(saved.count(catalog.items[0].id) == 1);
  CHECK(saved.at(catalog.items[0].id).position > 5.0);

  // Now the end of a film: the same store rule that fixes A4 must apply here too.
  CHECK(app.start_playback(0));
  app.tick(app.clock_now());
  app.finish_film();
  const auto after = store.load_progress();
  CHECK(after.count(catalog.items[0].id) == 0);
  CHECK(app.last_message().find("Finished") != std::string::npos);
  app.stop_playback();
}

void test_hover_preview() {
  const std::string dir = join(temp_dir(), "vlcflix-studio-preview");
  ensure_dir(dir);
  Catalog catalog = catalog_for_samples();
  if (catalog.items.empty()) return;

  TEST("preview: hovering a card decodes a muted preview, leaving stops it");
  ui::App app(dir, dir);
  app.resize(1280, 720);
  app.set_catalog(catalog);
  app.show_screen("home", "");

  // Move the pointer over the first rail card, the same way the window would.
  Canvas canvas(1280, 720);
  app.draw(canvas);
  ui::InputEvent move;
  move.type = ui::InputEvent::Type::MouseMove;
  move.x = 150;
  move.y = 620;
  app.handle(move);
  CHECK(app.preview_playing());
  const double deadline = now_seconds() + 10.0;
  while (now_seconds() < deadline && !app.preview_has_picture()) {
    app.tick(app.clock_now());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(app.preview_has_picture());

  // Off the cards: the preview stops and forgets its picture.
  move.x = 1270;
  move.y = 5;
  app.handle(move);
  CHECK(!app.preview_playing());
  CHECK(!app.preview_has_picture());
}

void test_player_missing_file() {
  TEST("player: a film whose file is gone says so instead of showing black");
  const std::string dir = join(temp_dir(), "vlcflix-studio-player-missing");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1280, 720);
  Catalog catalog;
  Item item;
  item.id = "id-gone";
  item.title = "Gone";
  item.path = join(dir, "not-here.mp4");
  catalog.items.push_back(item);
  app.set_catalog(catalog);
  CHECK(!app.start_playback(0));
  CHECK(!app.playing());
  CHECK(app.last_message().find("gone") != std::string::npos);
}

}  // namespace

void run_player_tests() {
  test_presenter();
  test_engine_plays_a_sample();
  test_engine_seeks_where_the_pipe_engine_could_not();
  test_app_player_journey();
  test_hover_preview();
  test_player_missing_file();
}

}  // namespace vf
