#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#ifdef _WIN32
// For RaiseException and EXCEPTION_ACCESS_VIOLATION in the deliberate-crash command.
// Windows only, so the POSIX build never sees it; SDL headers stay out of this file.
#include <windows.h>
#endif

#include "core/brand.h"
#include "core/catalog.h"
#include "core/json.h"
#include "core/log.h"
#include "core/model.h"
#include "core/naming.h"
#include "core/paths.h"
#include "core/sha1.h"
#include "core/store.h"
#include "core/sync.h"
#include "library/artwork.h"
#include "library/probe.h"
#include "library/scan.h"
#include "library/walk.h"
#include "platform/render_driver.h"
#include "platform/window_sdl.h"
#include "render/canvas.h"
#include "render/font.h"
#include "render/image.h"
#include "ui/app.h"

using namespace vf;

namespace {

const char* cursor_name(PointerCursor kind) {
  switch (kind) {
    case PointerCursor::Hand: return "hand";
    case PointerCursor::IBeam: return "ibeam";
    case PointerCursor::Arrow: return "arrow";
  }
  return "arrow";
}

}  // namespace

namespace {

struct Args {
  // No arguments at all means "open the window": that is what happens when the desktop
  // or Start Menu shortcut is double-clicked, and it is what people expect of an app.
  // `help` still prints the full command list.
  std::string command;
  std::string log_path;
  std::string data_dir;
  std::string folders;  // comma-separated
  std::string key_value;
  bool console = false;
  bool no_art = false;
  bool json = false;
  std::string screen = "home";
  std::string png;
  std::string all_dir;
  std::string query;
  std::string item;
  int width = 1600;
  int height = 900;
  // ui --frames N draws N frames and exits. It exists so the window loop can be
  // exercised on a machine with no display (SDL_VIDEODRIVER=dummy) instead of only
  // being trusted.
  int frames = 0;
  int focus = -1;
  int hover = -1;
  float scroll = 0;  // render --scroll N: draw the page already scrolled, for the fold
  bool play_subtitles = false;
  std::string play;      // render --play <path|index>: a player screenshot
  int play_index = 0;
  double play_seek = 0;
  double play_seconds = 0;
  std::string screenshot_dir;
  std::string positional;  // the first bare word after the command, e.g. `play 3`
};

Args parse_args(int argc, char** argv) {
  Args args;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (args.command.empty() && a.size() && a[0] != '-') {
      args.command = a;  // the command name
      continue;
    }
    if (a.size() && a[0] != '-' && args.positional.empty()) {
      args.positional = a;  // `play 3`, `naming "file.mkv"`
      continue;
    }
    auto value = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") {
      args.command = "help";
      continue;
    }
    if (a == "--log") args.log_path = value("--log");
    else if (a == "--data") args.data_dir = value("--data");
    else if (a == "--folders") args.folders = value("--folders");
    else if (a == "--set") args.key_value = value("--set");
    else if (a == "--console") args.console = true;
    else if (a == "--no-art") args.no_art = true;
    else if (a == "--json") args.json = true;
    else if (a == "--screen") args.screen = value("--screen");
    else if (a == "--png") args.png = value("--png");
    else if (a == "--all") args.all_dir = value("--all");
    else if (a == "--query") args.query = value("--query");
    else if (a == "--item") args.item = value("--item");
    else if (a == "--frames") args.frames = std::atoi(value("--frames").c_str());
    else if (a == "--focus") args.focus = std::atoi(value("--focus").c_str());
    else if (a == "--hover") args.hover = std::atoi(value("--hover").c_str());
    else if (a == "--scroll") args.scroll = std::atof(value("--scroll").c_str());
    else if (a == "--play") {
      args.play = value("--play");
      args.play_index = std::atoi(args.play.c_str());
    }
    else if (a == "--seek") args.play_seek = std::atof(value("--seek").c_str());
    else if (a == "--subtitles") args.play_subtitles = true;
    else if (a == "--wait") args.play_seconds = std::atof(value("--wait").c_str());
    else if (a == "--size") {
      const std::string size = value("--size");
      const std::size_t x = size.find('x');
      if (x != std::string::npos) {
        args.width = std::atoi(size.substr(0, x).c_str());
        args.height = std::atoi(size.substr(x + 1).c_str());
      }
    }
    else if (a.rfind("--", 0) == 0) positional.push_back(a.substr(2));
    else positional.push_back(a);
  }
  if (!positional.empty()) args.command = positional.front();
  return args;
}

std::string human_duration(double seconds) {
  if (!(seconds > 0)) return "--:--";
  const int total = static_cast<int>(seconds + 0.5);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
  return buf;
}

std::vector<std::string> split_commas(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  for (char c : text) {
    if (c == ',' || c == ';') {
      if (!current.empty()) out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

std::unique_ptr<ui::App> make_app() {
  Store store;
  auto app = std::make_unique<ui::App>(media_dir(), store.dir());
  app->set_catalog(store.load_catalog());
  app->set_progress(store.load_progress());
  app->set_my_list(store.load_my_list());
  app->set_settings(store.load_settings());
  // The samples that ship with this repository, so a first run has something to
  // point at without the user hunting for a path.
  const std::string samples = find_dir_upwards("samples");
  if (!samples.empty()) {
    app->set_sample_folder(samples);
    LOG_INFO("samples folder: %s", samples.c_str());
  }
  return app;
}

double steady_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int command_render(const Args& args) {
  auto app = make_app();
  app->resize(args.width, args.height);

  if (!args.play.empty()) {
    // A real decoded frame, so the picture in the screenshot is the engine's, not a
    // placeholder drawn for the occasion.
    const int index = args.play_index >= 0 ? args.play_index : 0;
    std::string error;
    if (!app->start_playback(index)) {
      std::fprintf(stderr, "could not start playback: %s\n", app->last_message().c_str());
      return 1;
    }
    const int timeout_ms = args.play_seconds > 0 ? static_cast<int>(args.play_seconds * 1000) : 12000;
    int waited = 0;
    while (waited < timeout_ms && !app->presenter().has_frame()) {
      app->tick(steady_seconds());
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      waited += 20;
    }
    if (!app->presenter().has_frame()) {
      std::fprintf(stderr, "no frame arrived in %d ms\n", timeout_ms);
      return 1;
    }
    if (args.play_subtitles) {
      // Turn the first subtitle track on for the screenshot, the same way the CC
      // button does.
      ui::InputEvent key;
      key.type = ui::InputEvent::Type::Key;
      key.key = 's';
      app->handle(key);
    }
    if (args.play_seek > 0) {
      // Let the seek land before the still is taken.
      app->engine().seek(args.play_seek);
      waited = 0;
      while (waited < 4000 && app->presenter().shown() < 2) {
        app->tick(steady_seconds());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waited += 20;
      }
    }
    if (!app->render_to_png(args.png)) return 1;
    std::printf("wrote %s (player, %lld frames decoded)\n", args.png.c_str(),
                static_cast<long long>(app->player_status().frames_decoded));
    app->stop_playback();
    return 0;
  }

  std::vector<std::string> screens;
  if (!args.all_dir.empty()) {
    ensure_dir(args.all_dir);
    screens = {"home", "browse", "mylist", "search", "detail", "settings"};
    if (app->catalog().items.empty()) {
      LOG_WARN("rendering screens with an empty library: the UI has nothing to show");
    }
  } else {
    screens = {args.screen};
  }

  int failures = 0;
  for (const auto& screen : screens) {
    if (!app->show_screen(screen, screen == "search" ? args.query : args.item)) {
      ++failures;
      continue;
    }
    if (args.hover >= 0) {
      // Force a hover so a screenshot can show the preview card without a mouse.
      app->ensure_layout();
      app->set_hover(args.hover);
    }
    if (args.scroll > 0) {
      // The limit is declared by a draw. A wheel on the hero (not on a rail) then
      // moves the page; the app clamps it, so a large N is "the bottom".
      app->ensure_layout();
      ui::InputEvent wheel;
      wheel.type = ui::InputEvent::Type::Wheel;
      wheel.x = 80;
      wheel.y = 120;
      wheel.wheel = -args.scroll / 96.0f;
      app->handle(wheel);
    }
    if (args.focus >= 0) {
      // Lay the screen out once (which is what records the card order), then put the
      // keyboard focus where it was asked to go, so a screenshot can show what a
      // keyboard user would see.
      app->ensure_layout();
      app->clear_focus();
      for (int i = 0; i <= args.focus; ++i) app->move_focus(1, 0);
    }
    const std::string path = args.all_dir.empty()
                                 ? args.png
                                 : join(args.all_dir, screen + ".png");
    if (path.empty()) {
      std::fprintf(stderr, "--png <file> or --all <dir> is required\n");
      return 2;
    }
    if (!app->render_to_png(path)) ++failures;
    else std::printf("wrote %s (%s)\n", path.c_str(), app->screen_name().c_str());
  }
  return failures == 0 ? 0 : 1;
}

// `vlcflix play <index|file>` runs a film for N seconds and prints what the engine
// actually did. It exists so playback can be checked without a window and without
// trusting the player view to report on itself.
int command_play(const Args& args) {
  auto app = make_app();
  app->resize(1280, 720);

  // `play 3` / `play /path/film.mkv`, and the older `--play` form still works.
  const std::string target = !args.positional.empty() ? args.positional : args.play;
  int index = -1;
  if (!target.empty()) {
    index = std::atoi(target.c_str());
    if (index < 0 || index >= static_cast<int>(app->catalog().items.size())) {
      // Maybe it is a file path rather than an index.
      if (file_exists(target)) {
        Item item;
        // The same id the scanner would give this file, so its progress is the same
        // progress: playing a sample from the CLI and then from the window continues
        // where the other left off instead of starting two histories.
        item.id = item_id_for_path(absolute_path(target));
        item.title = stem(target);
        item.path = absolute_path(target);
        item.filename = basename(target);
        Catalog catalog;
        catalog.items.push_back(item);
        app->set_catalog(catalog);
        index = 0;
      }
    }
  }
  if (app->catalog().items.empty()) {
    // Say what to do, not just that it failed: a fresh install has no library yet.
    std::fprintf(stderr,
                 "the library is empty, so there is nothing to play.\n"
                 "  scan a folder first:  vlcflix scan --folders /path/to/movies\n"
                 "  or name a file:       vlcflix play /path/to/film.mkv\n");
    return 2;
  }
  if (index < 0) index = 0;
  if (index >= static_cast<int>(app->catalog().items.size())) {
    std::fprintf(stderr, "there is no film %d: the library has %zu.\n", index,
                 app->catalog().items.size());
    return 2;
  }

  const double seconds = args.play_seconds > 0 ? args.play_seconds : 5.0;
  if (!app->start_playback(index)) {
    std::fprintf(stderr, "could not start: %s\n", app->last_message().c_str());
    return 1;
  }
  if (args.play_seek > 0) app->engine().seek(args.play_seek);
  if (args.play_subtitles) {
    ui::InputEvent key;
    key.type = ui::InputEvent::Type::Key;
    key.key = 's';
    app->handle(key);
  }

  // Real-time loop: tick, and draw a frame occasionally so the player view is on the
  // same code path as the window, not a special case.
  Canvas canvas(1280, 720);
  const std::string out = args.png;
  double started = steady_seconds();
  long long pictures = 0;
  while (steady_seconds() - started < seconds) {
    if (app->tick(steady_seconds())) ++pictures;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  // The still is taken of the final state, which is what makes it a check on the end
  // card as well as the chrome.
  if (!out.empty()) {
    app->draw(canvas);
    app->render_to_png(out);
  }
  const EngineStatus status = app->player_status();
  std::printf("state=%s time=%.2f s length=%.2f s decoded=%lld presented=%lld sync(video=%d audio=%d)\n",
              play_state_name(status.state), status.time, status.length,
              static_cast<long long>(status.frames_decoded),
              static_cast<long long>(app->presenter().shown()), app->sync_plan().video_delay_ms,
              app->sync_plan().audio_delay_ms);
  if (!out.empty()) std::printf("wrote %s\n", out.c_str());
  const std::string cue = app->active_subtitle_text(status.time);
  if (!cue.empty()) std::printf("subtitle: %s\n", cue.c_str());
  app->stop_playback();
  return status.frames_decoded > 0 ? 0 : 1;
}

int command_ui(const Args& args) {
  auto app = make_app();
  app->resize(args.width, args.height);
  if (app->catalog().items.empty()) {
    LOG_WARN("no catalog yet: the window will offer to scan, which is what it should do");
  }

  Window window;
  if (!window.open(args.width, args.height, "VLCFlix Studio")) {
    LOG_ERROR("window could not be opened");
    show_error_box("VLCFlix Studio",
                   std::string("VLCFlix Studio could not open its window.\n\n") +
                       "The full log is here:\n" + log::path());
    return 5;
  }

  // The canvas must be the size the window *actually* is, not the size that was asked
  // for: the window shrinks itself to fit the desktop, and SDL reports mouse positions
  // in window pixels. A 1600x900 canvas stretched into a 1228x900 window put every hit
  // area ~23 % away from where it was drawn - clicks landed on the wrong control and
  // the cursor over a button stayed an arrow, because nothing was under the pointer at
  // the coordinates the UI was testing.
  app->resize(window.width(), window.height());
  Canvas canvas(app->width(), app->height());
  LOG_INFO("window %dx%d, canvas %dx%d", window.width(), window.height(), app->width(),
           app->height());
  if (window.width() != args.width || window.height() != args.height) {
    LOG_INFO("window is %dx%d rather than %dx%d: drawing and hit testing both use the real size",
             window.width(), window.height(), args.width, args.height);
  }
  bool dirty = true;
  // `ui --play <index>` starts a film inside the real window loop. With --frames it
  // becomes a headless end-to-end run: window, engine, presenter, drawing, exit.
  if (!args.play.empty()) {
    if (!app->start_playback(std::atoi(args.play.c_str()))) {
      LOG_ERROR("--play %s could not start: %s", args.play.c_str(), app->last_message().c_str());
    }
  }
  app->on_toggle_fullscreen = [&window] { window.toggle_fullscreen(); };
  // The scan runs on this thread; `pump` is how it keeps the window painting.
  app->pump = [&]() {
    app->draw(canvas);
    window.present(canvas);
  };
  int ticks = 0;
  PointerCursor last_cursor = PointerCursor::Arrow;
  for (;;) {
    // In --frames mode each iteration counts and each iteration draws, so a
    // headless run terminates even with no input to react to.
    if (args.frames > 0) {
      if (ticks >= args.frames) break;
      ++ticks;
      dirty = true;
    }
    ui::InputEvent event;
    if (!window.poll(&event, dirty ? 0 : 16)) break;  // false = the window closed
    // Some platforms resize the window without a SIZE_CHANGED event (and the window may
    // have been shrunk before the first frame). The canvas follows the window, always:
    // hit testing is in window coordinates.
    if (window.width() != app->width() || window.height() != app->height()) {
      app->resize(window.width(), window.height());
      canvas = Canvas(app->width(), app->height());
      dirty = true;
    }
    if (event.type == ui::InputEvent::Type::Resize) {
      app->resize(static_cast<int>(event.x), static_cast<int>(event.y));
      canvas = Canvas(app->width(), app->height());
      dirty = true;
    } else if (event.type != ui::InputEvent::Type::None) {
      if (app->handle(event)) dirty = true;
    }
    // Playback progress, resume, the finished state and the chrome's auto-hide all
    // happen on this tick, once per frame.
    const double now = std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    if (app->tick(now)) dirty = true;
    if (dirty) {
      app->draw(canvas);
      window.present(canvas);
      dirty = false;
    }
    // The cursor is decided by what the UI has under the pointer, so it is updated
    // after the frame it describes: hovering a poster or a button must look clickable.
    const PointerCursor cursor = app->pointer_cursor();
    if (cursor != last_cursor) {
      last_cursor = cursor;
      LOG_INFO("cursor: %s at %.0f,%.0f", cursor_name(cursor), app->pointer_x(),
               app->pointer_y());
    }
    window.set_cursor(cursor);
    if (app->playing()) dirty = true;  // a player needs a frame even when nothing changed
  }

  // My List is the only thing the UI mutates without saving at the time; settings
  // and the catalog were already written when they changed.
  Store store;
  store.save_my_list(app->my_list());
  LOG_INFO("ui closed; my_list=%zu", app->my_list().size());
  return 0;
}

int command_scan(const Args& args) {
  Store store;
  Settings settings = store.load_settings();
  std::vector<std::string> folders =
      args.folders.empty() ? settings.library_folders : split_commas(args.folders);
  if (folders.empty()) {
    LOG_ERROR("no library folders configured; pass --folders <dir> or set them first");
    std::fprintf(stderr, "no library folders configured — pass --folders <dir>\n");
    return 3;
  }

  const std::string media = media_dir();
  ensure_dir(media);
  const int swept = sweep_temp_art(media);
  if (swept) LOG_INFO("cleared %d half-written artwork file(s) from an interrupted run", swept);

  Catalog previous = store.load_catalog();
  ScanOptions options;
  options.folders = folders;
  options.media_dir = media;
  options.previous = previous;
  options.render_artwork = !args.no_art;
  options.on_progress = [](const ScanProgress& p) {
    LOG_INFO("scan %s %d/%d %s", p.phase.c_str(), p.done, p.total, p.current.c_str());
  };
  options.on_catalog = [&](const Catalog& catalog) { store.save_catalog(catalog); };
  ArtRunResult art;
  options.art_result = &art;

  const Catalog catalog = scan_library(options);
  store.save_catalog(catalog);

  std::printf("\n%-28s %6s %8s %11s  %s\n", "TITLE", "YEAR", "LENGTH", "RESOLUTION", "ARTWORK");
  std::printf("%s\n", std::string(74, '-').c_str());
  for (const auto& item : catalog.items) {
    char resolution[32] = "-";
    if (item.width && item.height) {
      std::snprintf(resolution, sizeof resolution, "%dx%d", item.width, item.height);
    }
    const std::string art = (item.poster.empty() ? "-" : "poster") + std::string(" ") +
                            (item.backdrop.empty() ? "-" : "backdrop") + " " +
                            (item.preview.empty() ? "-" : "preview");
    std::printf("%-28s %6s %8s %11s  %s\n", item.title.c_str(),
                item.year ? std::to_string(item.year).c_str() : "-", human_duration(item.duration).c_str(),
                resolution, art.c_str());
  }
  int unreadable = 0;
  for (const auto& item : catalog.items) {
    if (item.unreadable) ++unreadable;
  }
  if (!catalog.errors.empty()) {
    // A count alone sends people to the log; the reasons belong on the screen, next to
    // the library they are about.
    std::printf("%s\n", std::string(74, '-').c_str());
    for (const auto& error : catalog.errors) std::printf("  ! %s\n", error.c_str());
  }
  std::printf("%s\n", std::string(74, '-').c_str());
  // "0 drawn" is the honest way to say that a rescan reused the artwork on disk: a
  // count of finished work would look the same whether it was drawn or adopted.
  std::printf("%d of %d artwork set(s) drawn, %d still missing\n", art.drawn, art.total,
              art.remaining);
  if (unreadable) {
    std::printf("%d item(s) in the library could not be read and will not play\n", unreadable);
  }
  std::printf("%zu item(s), %zu error(s), catalog at %s\n", catalog.items.size(),
              catalog.errors.size(), store.file_for("catalog").c_str());
  return 0;
}

int command_status(const Args& args) {
  Store store;
  const Settings settings = store.load_settings();
  const Catalog catalog = store.load_catalog();
  const auto progress = store.load_progress();
  const auto my_list = store.load_my_list();

  if (args.json) {
    Json root = Json::object();
    root.set("dataDir", store.dir());
    root.set("mediaDir", media_dir());
    root.set("logFile", log::path());
    root.set("items", static_cast<long long>(catalog.items.size()));
    Json library = Json::array();
    for (const auto& item : catalog.items) {
      Json entry = Json::object();
      entry.set("id", item.id);
      entry.set("title", item.title);
      entry.set("year", static_cast<long long>(item.year));
      entry.set("path", item.path);
      entry.set("duration", item.duration);
      entry.set("pixFmt", item.pix_fmt);
      entry.set("width", static_cast<long long>(item.width));
      entry.set("height", static_cast<long long>(item.height));
      entry.set("rating", item.rating);
      entry.set("poster", item.poster);
      entry.set("backdrop", item.backdrop);
      entry.set("preview", item.preview);
      entry.set("unreadable", item.unreadable);
      Json genres = Json::array();
      for (const auto& g : item.genres) genres.push(Json(g));
      entry.set("genres", genres);
      Json subs = Json::array();
      for (const auto& t : item.subtitles) {
        Json sub = Json::object();
        sub.set("lang", t.lang);
        sub.set("label", t.label);
        sub.set("name", t.name);
        subs.push(sub);
      }
      entry.set("subtitles", subs);
      library.push(entry);
    }
    root.set("library", library);
    root.set("scannedAt", static_cast<long long>(catalog.scanned_at));
    Json folders = Json::array();
    for (const auto& f : settings.library_folders) folders.push(Json(f));
    root.set("libraryFolders", folders);
    Json watching = Json::array();
    for (const auto& kv : progress) watching.push(Json(kv.first));
    root.set("inProgress", watching);
    root.set("myList", static_cast<long long>(my_list.size()));
    root.set("previousRunCrashed", log::previous_run_crashed());
    std::printf("%s\n", root.dump(2).c_str());
    return 0;
  }

  std::printf("data dir      %s\n", store.dir().c_str());
  std::printf("media dir     %s\n", media_dir().c_str());
  std::printf("log file      %s\n", log::path().c_str());
  std::printf("items         %zu\n", catalog.items.size());
  std::printf("library       %zu folder(s)\n", settings.library_folders.size());
  for (const auto& folder : settings.library_folders) std::printf("              %s\n", folder.c_str());
  std::printf("in progress   %zu\n", progress.size());
  std::printf("my list       %zu\n", my_list.size());
  std::printf("last scan     %s\n", catalog.scanned_at ? "yes" : "never");

  const auto rails = build_rails(catalog.items, progress, my_list);
  std::printf("rails         %zu:", rails.size());
  for (const auto& rail : rails) std::printf(" %s(%zu)", rail.title.c_str(), rail.items.size());
  std::printf("\n");
  return 0;
}

int command_settings(const Args& args) {
  Store store;
  if (args.key_value.empty()) {
    const Settings settings = store.load_settings();
    Json out = Json::object();
    Json folders = Json::array();
    for (const auto& f : settings.library_folders) folders.push(Json(f));
    out.set("libraryFolders", folders);
    out.set("vlcPath", settings.vlc_path.empty() ? Json() : Json(settings.vlc_path));
    out.set("vlcHttpPort", settings.vlc_http_port);
    out.set("vlcPasswordSet", !settings.vlc_password.empty());
    out.set("playMode", settings.play_mode);
    out.set("vlcFullscreen", settings.vlc_fullscreen);
    out.set("hwDecode", settings.hw_decode);
    std::printf("%s\n", out.dump(2).c_str());
    return 0;
  }

  const std::size_t eq = args.key_value.find('=');
  if (eq == std::string::npos) {
    std::fprintf(stderr, "--set expects key=<json value>\n");
    return 2;
  }
  const std::string key = args.key_value.substr(0, eq);
  const std::string raw = args.key_value.substr(eq + 1);
  Json value;
  std::string error;
  if (!Json::parse(raw, value, &error)) {
    // Bare words are accepted as strings so the CLI stays usable.
    value = Json(raw);
  }
  Json patch = Json::object();
  patch.set(key, value);
  Settings updated;
  if (!store.patch_settings(patch, &updated, &error)) {
    std::fprintf(stderr, "rejected: %s\n", error.c_str());
    return 4;
  }
  std::printf("saved %s\n", key.c_str());
  return 0;
}

int command_naming(int argc, char** argv) {
  // `--naming a.mkv b.mkv ...` prints what the parser makes of each name. Used by
  // the tests and by anyone checking a rename before committing to it.
  int printed = 0;
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--", 0) == 0) continue;
    if (a == "naming") continue;
    const ParsedName parsed = parse_name(a);
    std::printf("%s\n  title: %s\n  year:  %s\n", a.c_str(), parsed.title.c_str(),
                parsed.year ? std::to_string(parsed.year).c_str() : "(none)");
    ++printed;
  }
  return printed ? 0 : 2;
}

int command_crash() {
  // Deliberate crash, used by the test suite to prove that a crash still leaves a
  // log with a stack trace behind. Refuses to run without an explicit --log or a
  // data directory, so it cannot hurt a real installation by accident.
  LOG_WARN("crash test requested: raising SIGSEGV through the crash handler");
  // A real null dereference is optimised away at -O2 (it is undefined behaviour), so
  // the signal is raised explicitly: that exercises exactly the handler chain a real
  // crash uses, which is what this test is for.
#ifdef _WIN32
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
  std::raise(SIGSEGV);
#endif
  return 0;  // unreachable when the handler is installed
}

void print_help() {
  std::printf(
      "%s %s\n\n"
      "usage: vlcflix [command] [options]\n\n"
      "commands\n"
      "  scan                 walk the library, probe films, draw artwork\n"
      "  status               what is stored right now (--json for machines)\n"
      "  settings [--set k=v] read or patch settings, with validation\n"
      "  naming <file>...     show how filenames are turned into titles\n"
      "  doctor               environment report: paths, tools, log location\n"
      "  play [index|file]    play for --wait <seconds>, then report what happened\n"
      "                       play 3 --seek 5 --subtitles --png out.png --wait 3\n"
      "  ui                   open the window (needs a display; SDL_VIDEODRIVER=dummy\n"
      "                       works headless, and --frames N exits after N frames)\n"
      "  render               draw a screen to a PNG, no display required\n"
      "                       render --screen home|browse|mylist|search|detail|settings\n"
      "                              --png out.png [--size 1600x900] [--query text] [--scroll N]\n"
      "                       render --all <dir>  draws every screen\n"
      "  crash                deliberately crash (proves the crash log works)\n"
      "  version | help\n\n"
      "options\n"
      "  --data <dir>         use this data directory\n"
      "  --folders <a,b,...>  library folders for this scan\n"
      "  --no-art             scan metadata only, draw nothing\n"
      "  --log <file>         explicit log file\n"
      "  --console            also mirror the log to stderr\n"
      "  --json               machine-readable output where supported\n",
      kProductName, kVersion);
}

int command_doctor(const Args& args) {
  const Store store;
  const auto catalog = store.load_catalog();
  std::printf("%s %s (built %s)\n\n", kProductName, kVersion, kBuildDate);
  std::printf("log file        %s\n", log::path().empty() ? "(stderr only)" : log::path().c_str());
  std::printf("log candidates  ");
  for (const auto& dir : log_dir_candidates()) std::printf("%s%s", dir.c_str(), " ");
  std::printf("\n");
  std::printf("data dir        %s\n", store.dir().c_str());
  std::printf("media dir       %s\n", media_dir().c_str());
  std::printf("exe dir         %s\n", exe_dir().c_str());
  const ToolProbe tools = probe_tools();
  std::printf("ffmpeg          %s\n", tools.ffmpeg ? "found" : "MISSING (no artwork)");
  std::printf("ffprobe         %s\n", tools.ffprobe ? "found" : "MISSING (no durations)");
  // The renderer is a policy, not a device: `doctor` can say what the window will ask
  // for without opening one, which matters when the window is what fails.
  const char* requested_driver = std::getenv("VLCFLIX_RENDER_DRIVER");
  if (!requested_driver || !*requested_driver) requested_driver = std::getenv("SDL_RENDER_DRIVER");
  std::printf("renderer        %s\n",
              render_driver_description(render_driver_plan(requested_driver ? requested_driver : ""))
                  .c_str());
  std::printf("catalog         %zu item(s)%s\n", catalog.items.size(),
              catalog.scanned_at ? "" : " (never scanned)");
  std::printf("previous run    %s\n",
              log::previous_run_crashed() ? "ENDED UNEXPECTEDLY" : "clean or first run");
  if (log::previous_run_crashed()) {
    std::printf("\nlast lines of that run:\n%s", log::previous_run_tail(15).c_str());
  }
  (void)args;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // ---- FIRST STATEMENT ------------------------------------------------------
  // Open the log and install the crash handlers. Nothing above this line may
  // allocate, parse anything or touch a device: if the app is going to die before
  // opening a window, this is the code that must already be working.
  // The two flags that can move the log are read first because they decide where
  // the log goes; the full parse happens afterwards.
  log::Options log_options;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--log") == 0 && i + 1 < argc) log_options.path = argv[++i];
    else if (std::strcmp(a, "--console") == 0) log_options.console = true;
  }
  log::init(log_options);

#ifdef _WIN32
  // The Windows binary is built as a GUI executable, because a double-clicked app
  // must not open a console window behind itself. The cost of that choice is that a
  // program run *from* a command prompt has nowhere to write, so attach to the
  // parent console when there is one: typed at cmd.exe, output appears; double
  // clicked, nothing happens and the window opens. Neither path affects the log file,
  // which was already opened above.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
    std::freopen("CONIN$", "r", stdin);
  }
#endif
  // ---- everything below is allowed to fail, and will be logged ---------------

  struct ExitGuard {
    int code = 0;
    ~ExitGuard() {
      LOG_INFO("exit code %d", code);
      log::shutdown();
    }
  } guard;

  Args args = parse_args(argc, argv);
  if (!args.data_dir.empty()) {
    set_data_dir(args.data_dir);
    LOG_INFO("data directory overridden: %s", args.data_dir.c_str());
  }
  LOG_INFO("command: %s", args.command.c_str());

  // In --json mode the banner goes to stderr: a caller parsing stdout must get JSON and
  // nothing else, and the banner must still be visible to a human watching the terminal.
  FILE* banner = args.json ? stderr : stdout;
  std::fprintf(banner, "%s %s\n", kProductName, kVersion);
  std::fprintf(banner, "log: %s\n", log::path().empty() ? "(stderr only)" : log::path().c_str());
  if (log::previous_run_crashed()) {
    std::fprintf(banner,
                 "NOTE: the previous run ended unexpectedly; see the lines above the last run-start\n");
  }

  if (args.command.empty()) args.command = "ui";  // double-clicked: open the window
  int code = 0;
  if (args.command == "scan") code = command_scan(args);
  else if (args.command == "status") code = command_status(args);
  else if (args.command == "settings") code = command_settings(args);
  else if (args.command == "naming") code = command_naming(argc, argv);
  else if (args.command == "doctor") code = command_doctor(args);
  else if (args.command == "ui") code = command_ui(args);
  else if (args.command == "render") code = command_render(args);
  else if (args.command == "play") code = command_play(args);
  else if (args.command == "crash") code = command_crash();
  else if (args.command == "version") std::printf("%s\n", kVersion);
  else if (args.command == "help" || args.command == "--help") print_help();
  else {
    std::fprintf(stderr, "unknown command '%s' (try: help)\n", args.command.c_str());
    LOG_ERROR("unknown command: %s", args.command.c_str());
    code = 2;
  }
  guard.code = code;
  return code;
}
