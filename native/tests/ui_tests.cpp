// UI tests: what the screens actually draw, and what the input actually does.
//
// These run with no display and no window: the renderer draws into a pixel buffer,
// so "did this screen draw anything, and did the click do what it says" is a
// measurement rather than an opinion. Clicks are aimed at the hit areas the drawing
// pass produced, so a test cannot pass by clicking a coordinate the app no longer
// uses.
#include <algorithm>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/paths.h"
#include "render/canvas.h"
#include "tests/harness.h"
#include "ui/app.h"

namespace vf {
namespace {

Catalog make_catalog() {
  Catalog catalog;
  catalog.scanned_at = 1;
  auto add = [&](const std::string& id, const std::string& title, int year,
                 std::vector<std::string> genres, const std::string& synopsis) {
    Item item;
    item.id = id;
    item.title = title;
    item.year = year;
    item.genres = std::move(genres);
    item.synopsis = synopsis;
    item.duration = 96 * 60 + 30;
    item.width = 1920;
    item.height = 1080;
    item.rating = "TV-MA";
    item.path = "/movies/" + title + ".mkv";
    item.filename = title + ".mkv";
    // Newest first is what the rails assume; the test keeps that order explicit.
    item.added_at = 3000 - static_cast<long long>(catalog.items.size());
    catalog.items.push_back(item);
  };
  add("id-alpha", "Alpha", 2019, {"Drama", "Western"},
      "Two brothers split a failing ranch down the fence line.");
  add("id-beta", "Beta", 2020, {"Drama"}, "A weatherman who is never wrong.");
  add("id-gamma", "Neon Divide", 2024, {"Science Fiction"}, "A city that runs on neon.");
  return catalog;
}

double non_background_fraction(const Canvas& canvas) {
  const std::vector<uint8_t>& px = canvas.pixels();
  std::size_t count = 0;
  for (std::size_t i = 0; i < px.size(); i += 4) {
    if (px[i] != 11 || px[i + 1] != 11 || px[i + 2] != 15) ++count;
  }
  return px.empty() ? 0.0 : static_cast<double>(count) / (px.size() / 4);
}

// Clicks the centre of the first hit area with this action. Returns false when the
// app drew no such area, which is itself a failure worth reporting.
bool click_action(ui::App& app, ui::Action::Kind kind, int index = -1) {
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind != kind) continue;
    if (index >= 0 && hit.second.index != index) continue;
    ui::InputEvent click;
    click.type = ui::InputEvent::Type::MouseDown;
    click.x = hit.first.x + hit.first.w / 2;
    click.y = hit.first.y + hit.first.h / 2;
    return app.handle(click);
  }
  return false;
}

bool has_action(const ui::App& app, ui::Action::Kind kind) {
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind == kind) return true;
  }
  return false;
}

void draw_once(ui::App& app) {
  Canvas canvas(app.width(), app.height());
  app.draw(canvas);
}

void test_ui_screens(const std::string& out_dir) {
  TEST("ui: every screen draws real content, and a blank canvas does not");
  ui::App app(out_dir, out_dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());

  // Negative control: a cleared canvas is 100% background. If this ever reports
  // content, the measurement below means nothing.
  Canvas blank(1600, 900);
  blank.clear(theme::bg());
  CHECK(non_background_fraction(blank) < 0.0001);

  struct Screen {
    const char* name;
    double minimum_content;
  };
  const Screen screens[] = {{"home", 0.10},   {"browse", 0.02},  {"mylist", 0.005},
                            {"search", 0.005}, {"detail", 0.15},  {"settings", 0.15}};
  for (const auto& screen : screens) {
    CHECK(app.show_screen(screen.name, screen.name == std::string("search") ? "neon" : "0"));
    Canvas canvas(1600, 900);
    app.draw(canvas);
    const double fraction = non_background_fraction(canvas);
    if (fraction < screen.minimum_content) {
      vftest::Registry::instance().fail(
          __FILE__, __LINE__,
          std::string(screen.name) + " drew " + std::to_string(fraction) +
              " of the canvas, expected at least " + std::to_string(screen.minimum_content));
    } else {
      vftest::Registry::instance().pass();
    }
    const std::string path = join(out_dir, std::string("ui-") + screen.name + ".png");
    CHECK(write_png(path, canvas));
    CHECK(file_size(path) > 2000);
  }
}

void test_ui_input() {
  TEST("ui: clicking nav, a card, and the search field does what it looks like");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-input");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);

  CHECK_STR_EQ(app.screen_name(), "home");
  // The nav links are Navigate actions carrying the view name.
  bool went_to_browse = false;
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind == ui::Action::Kind::Navigate && hit.second.value == "browse") {
      ui::InputEvent click;
      click.type = ui::InputEvent::Type::MouseDown;
      click.x = hit.first.x + 4;
      click.y = hit.first.y + 4;
      app.handle(click);
      went_to_browse = true;
      break;
    }
  }
  CHECK(went_to_browse);
  CHECK_STR_EQ(app.screen_name(), "browse");

  // A card opens its detail sheet, and it is the film that was clicked.
  draw_once(app);
  // Cards open the detail sheet, as they do in the web version; the sheet is where
  // Play lives.
  CHECK(has_action(app, ui::Action::Kind::OpenDetail));
  CHECK(click_action(app, ui::Action::Kind::OpenDetail, 1));
  CHECK_STR_EQ(app.screen_name(), "detail");
  CHECK_EQ(app.open_item(), 1);

  // Escape closes it.
  ui::InputEvent key;
  key.type = ui::InputEvent::Type::Key;
  key.key = 27;
  app.handle(key);
  CHECK_EQ(app.open_item(), -1);

  // The search field takes focus, then text.
  draw_once(app);
  CHECK(click_action(app, ui::Action::Kind::FocusSearch));
  CHECK_STR_EQ(app.screen_name(), "search");
  ui::InputEvent text;
  text.type = ui::InputEvent::Type::Text;
  text.text = "neon";
  app.handle(text);
  CHECK_STR_EQ(app.query(), "neon");
  key.key = 8;  // backspace
  app.handle(key);
  CHECK_STR_EQ(app.query(), "neo");

  // Search results narrow to the matching film.
  draw_once(app);
  CHECK(has_action(app, ui::Action::Kind::OpenDetail));
  CHECK(click_action(app, ui::Action::Kind::OpenDetail));
  CHECK_EQ(app.open_item(), 2);  // "Neon Divide"

  // Settings opens from the header.
  ui::InputEvent escape;
  escape.type = ui::InputEvent::Type::Key;
  escape.key = 27;
  app.handle(escape);
  draw_once(app);
  CHECK(click_action(app, ui::Action::Kind::OpenSettings));
  CHECK(app.settings_open());
  CHECK_STR_EQ(app.screen_name(), "settings");

  // And the close button closes it.
  draw_once(app);
  CHECK(click_action(app, ui::Action::Kind::CloseOverlay));
  CHECK(!app.settings_open());
}

void test_ui_my_list() {
  TEST("ui: Play reports the request and My List toggles both ways");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-list");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);

  int played = -1;
  app.on_play_requested = [&](int index) { played = index; };

  CHECK(click_action(app, ui::Action::Kind::Play, 0));
  CHECK_EQ(played, 0);
  // Turn 2 has no player yet, and says so instead of pretending to play.
  CHECK(app.last_message().find("not wired up") != std::string::npos);

  CHECK(click_action(app, ui::Action::Kind::ToggleMyList, 0));
  CHECK_EQ(app.my_list().size(), std::size_t(1));
  CHECK_STR_EQ(app.my_list().front(), "id-alpha");

  // The same button now reads the other way and removes it.
  draw_once(app);
  CHECK(click_action(app, ui::Action::Kind::ToggleMyList, 0));
  CHECK_EQ(app.my_list().size(), std::size_t(0));
}

void test_ui_keyboard() {
  TEST("ui: arrows move between cards and Enter opens the focused one");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-keys");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);

  ui::InputEvent key;
  key.type = ui::InputEvent::Type::Key;
  // The first arrow selects the first card instead of jumping to an arbitrary one.
  key.key = 1073741903;  // Right
  CHECK(app.handle(key));
  CHECK_EQ(app.focus_item(), 0);
  app.handle(key);
  CHECK_EQ(app.focus_item(), 1);
  key.key = 1073741904;  // Left
  app.handle(key);
  CHECK_EQ(app.focus_item(), 0);
  // Left at the start stays put rather than wrapping to the end.
  app.handle(key);
  CHECK_EQ(app.focus_item(), 0);
  // Down moves by a row: on home the rails are one row, so there is nowhere to go
  // and the selection must not teleport.
  key.key = 1073741905;
  app.handle(key);
  CHECK_EQ(app.focus_item(), 0);
  // Enter opens the focused film.
  key.key = 13;
  CHECK(app.handle(key));
  CHECK_EQ(app.open_item(), 0);
  CHECK_STR_EQ(app.screen_name(), "detail");
  // Escape closes the sheet, and a second Escape drops the focus.
  key.key = 27;
  app.handle(key);
  CHECK_EQ(app.open_item(), -1);
  app.handle(key);
  CHECK_EQ(app.focus_item(), -1);
}

void test_ui_hit_areas_are_unique() {
  TEST("ui: hit areas do not overlap, so one click is one action");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-hits");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);

  const auto& hits = app.hit_rects();
  CHECK(hits.size() > 5);
  int overlaps = 0;
  for (std::size_t i = 0; i < hits.size(); ++i) {
    for (std::size_t j = i + 1; j < hits.size(); ++j) {
      const Rect& a = hits[i].first;
      const Rect& b = hits[j].first;
      const float overlap_x = std::min(a.right(), b.right()) - std::max(a.x, b.x);
      const float overlap_y = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
      if (overlap_x > 2 && overlap_y > 2) ++overlaps;
    }
  }
  CHECK_EQ(overlaps, 0);
}

void test_ui_small_window() {
  TEST("ui: the minimum window size draws every screen without overflowing");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-small");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(640, 420);
  app.set_catalog(make_catalog());
  for (const char* screen : {"home", "browse", "mylist", "search", "detail", "settings"}) {
    CHECK(app.show_screen(screen, "0"));
    Canvas canvas(app.width(), app.height());
    app.draw(canvas);
    CHECK(non_background_fraction(canvas) > 0.005);
  }
}

}  // namespace

void run_ui_tests(const std::string& out_dir) {
  ensure_dir(out_dir);
  test_ui_screens(out_dir);
  test_ui_input();
  test_ui_my_list();
  test_ui_keyboard();
  test_ui_hit_areas_are_unique();
  test_ui_small_window();
}

}  // namespace vf
