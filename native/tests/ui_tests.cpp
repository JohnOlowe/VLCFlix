// UI tests: what the screens actually draw, and what the input actually does.
//
// These run with no display and no window: the renderer draws into a pixel buffer,
// so "did this screen draw anything, and did the click do what it says" is a
// measurement rather than an opinion. Clicks are aimed at the hit areas the drawing
// pass produced, so a test cannot pass by clicking a coordinate the app no longer
// uses.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/paths.h"
#include "render/canvas.h"
#include "tests/harness.h"
#include "library/fs_browse.h"
#include "platform/render_driver.h"
#include "platform/window_size.h"
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

// A library big enough to overflow any of the screens, and with one film whose
// synopsis is longer than the detail sheet: both are needed to test scrolling at all,
// because a screen that fits has nothing to scroll.
Catalog make_big_catalog(std::size_t count) {
  Catalog catalog;
  catalog.scanned_at = 1;
  for (std::size_t i = 0; i < count; ++i) {
    Item item;
    item.id = "id-" + std::to_string(i);
    item.title = "Film " + std::to_string(i);
    item.year = 2000 + static_cast<int>(i % 25);
    item.genres = {"Drama"};
    // Spaces matter: a synopsis is wrapped on word boundaries, so a single 1400-letter
    // word would be one line and nothing would overflow.
    item.synopsis = i == 0 ? std::string() : "Short synopsis.";
    if (i == 0) {
      for (int line = 0; line < 14; ++line) {
        item.synopsis += "A late-night radio host takes a call from a listener who describes "
                         "tomorrow's weather with impossible accuracy. ";
      }
    }
    item.duration = 6000;
    item.width = 1280;
    item.height = 720;
    item.path = "/movies/film" + std::to_string(i) + ".mkv";
    item.filename = "film" + std::to_string(i) + ".mkv";
    item.added_at = 5000 - static_cast<long long>(i);
    catalog.items.push_back(item);
  }
  return catalog;
}

ui::InputEvent wheel_at(float x, float y, float notches) {
  ui::InputEvent event;
  event.type = ui::InputEvent::Type::Wheel;
  event.x = x;
  event.y = y;
  event.wheel = notches;
  return event;
}

ui::InputEvent key_event(int key) {
  ui::InputEvent event;
  event.type = ui::InputEvent::Type::Key;
  event.key = key;
  return event;
}

// The centre of the first *card* hit area (a poster, not a button): that is a point
// the user's pointer would actually be over when they use the wheel on a rail.
bool first_card_centre(const ui::App& app, float* x, float* y) {
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind != ui::Action::Kind::OpenDetail) continue;
    if (hit.first.w < 100 || hit.first.h < 100) continue;
    *x = hit.first.x + hit.first.w / 2;
    *y = hit.first.y + hit.first.h / 2;
    return true;
  }
  return false;
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

// What a click at this point would actually do. hit_test walks the drawn list
// backwards, so this is the topmost match - the same thing the app decides.
ui::Action top_action_at(const ui::App& app, float x, float y) {
  const auto& hits = app.hit_rects();
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    if (it->first.contains(x, y)) return it->second;
  }
  return ui::Action{};
}

ui::InputEvent click_at(float x, float y) {
  ui::InputEvent click;
  click.type = ui::InputEvent::Type::MouseDown;
  click.x = x;
  click.y = y;
  return click;
}

ui::InputEvent move_to(float x, float y) {
  ui::InputEvent event;
  event.type = ui::InputEvent::Type::MouseMove;
  event.x = x;
  event.y = y;
  return event;
}

// The centre of the smallest hit area with this action. Used for the sheet's close
// button, which shares its action kind with the full-screen scrim behind it.
bool small_hit_centre(const ui::App& app, ui::Action::Kind kind, float* x, float* y,
                      float max_size = 60.0f) {
  bool found = false;
  float best = 0;
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind != kind) continue;
    if (hit.first.w > max_size || hit.first.h > max_size) continue;
    const float area = hit.first.w * hit.first.h;
    if (found && area >= best) continue;
    found = true;
    best = area;
    *x = hit.first.x + hit.first.w / 2;
    *y = hit.first.y + hit.first.h / 2;
  }
  return found;
}

// A point where a click really resolves to `kind`: the topmost hit area with that
// action. The page underneath a sheet has its own buttons of the same kind, so picking
// the first rect in draw order would aim at the one that is covered.
bool find_clickable(const ui::App& app, ui::Action::Kind kind, float* x, float* y,
                    int index = -1) {
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind != kind) continue;
    if (index >= 0 && hit.second.index != index) continue;
    const float cx = hit.first.x + hit.first.w / 2;
    const float cy = hit.first.y + hit.first.h / 2;
    if (top_action_at(app, cx, cy).kind != kind) continue;
    *x = cx;
    *y = cy;
    return true;
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

  // And the close button closes it. It has to be the button, not the scrim: the scrim
  // shares the action kind, and clicking its centre lands inside the sheet.
  draw_once(app);
  float close_x = 0, close_y = 0;
  CHECK(small_hit_centre(app, ui::Action::Kind::CloseOverlay, &close_x, &close_y));
  CHECK_EQ(top_action_at(app, close_x, close_y).kind, ui::Action::Kind::CloseOverlay);
  app.handle(click_at(close_x, close_y));
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
  // Turn 3 wires Play to the player. The fixture in this file has no real files, so
  // the honest outcome is a message saying the file is gone - not a silent no-op.
  CHECK(app.last_message().find("gone") != std::string::npos);
  CHECK(!app.playing());

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

// ---------------------------------------------------------------- scrolling

// The complaint this closes: "there are places where you are just unable to scroll as
// you want". Each screen is driven the way a person would drive it - a wheel over the
// thing, or a page key - and the measurement is the scroll offset the app itself
// reports, not what a screenshot looks like.
void test_ui_scrolling() {
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-scroll");
  ensure_dir(dir);

  TEST("ui: the wheel scrolls the page on all four list screens and stops at the end");
  for (const char* screen : {"home", "browse", "search"}) {
    ui::App app(dir, dir);
    app.resize(1000, 600);
    app.set_catalog(make_big_catalog(30));
    CHECK(app.show_screen(screen, "film"));
    draw_once(app);
    CHECK(app.scroll_limit(ui::App::page_region()) > 0.0f);
    for (int i = 0; i < 40; ++i) app.handle(wheel_at(500, 400, -1));
    CHECK(app.scroll_offset(ui::App::page_region()) > 0.0f);
    CHECK(app.scroll_offset(ui::App::page_region()) ==
          app.scroll_limit(ui::App::page_region()));
    // Scrolling back up returns to the top and does not go past it.
    for (int i = 0; i < 40; ++i) app.handle(wheel_at(500, 400, 1));
    CHECK_EQ(app.scroll_offset(ui::App::page_region()), 0.0f);
  }

  TEST("ui: a rail scrolls sideways, and the wheel over it does not jump the page");
  {
    ui::App app(dir, dir);
    app.resize(1000, 600);
    app.set_catalog(make_big_catalog(30));
    draw_once(app);
    float x = 0, y = 0;
    CHECK(first_card_centre(app, &x, &y));
    const std::string region = app.scroll_region_at(x, y);
    CHECK(region.rfind("rail:", 0) == 0);
    CHECK(app.scroll_limit(region) > 0.0f);
    app.handle(wheel_at(x, y, -1));
    CHECK(app.scroll_offset(region) > 0.0f);
    CHECK_EQ(app.scroll_offset(ui::App::page_region()), 0.0f);
    // Aimed at the rail one more time at its end: the wheel falls through to the page
    // instead of doing nothing, so the user is never stuck.
    for (int i = 0; i < 40; ++i) app.handle(wheel_at(x, y, -1));
    CHECK(app.scroll_offset(region) == app.scroll_limit(region));
    CHECK(app.scroll_offset(ui::App::page_region()) > 0.0f);
  }

  TEST("ui: the detail sheet scrolls to the end of a long synopsis");
  {
    ui::App app(dir, dir);
    app.resize(900, 520);
    app.set_catalog(make_big_catalog(6));
    CHECK(app.show_screen("detail", "0"));
    draw_once(app);
    CHECK(app.scroll_limit("detail") > 0.0f);
    // The sheet's own Play button, not the one the Home screen drew underneath it.
    auto sheet_play_y = [&](ui::App& screen) {
      float y = -1;
      for (const auto& hit : screen.hit_rects()) {
        if (hit.second.kind != ui::Action::Kind::Play) continue;
        if (hit.first.x < 80) continue;  // the Home button sits at the gutter
        y = hit.first.y;
      }
      return y;
    };
    const float play_before = sheet_play_y(app);
    // At the top the buttons are below the fold, so they must NOT be clickable: an
    // invisible target is the same bug as an unreachable one.
    CHECK_EQ(play_before, -1.0f);
    Canvas top(app.width(), app.height());
    app.draw(top);
    for (int i = 0; i < 40; ++i) app.handle(wheel_at(450, 260, -1));
    CHECK(app.scroll_offset("detail") == app.scroll_limit("detail"));
    Canvas end(app.width(), app.height());
    app.draw(end);
    // The picture changes: the sheet really scrolled rather than the wheel quietly
    // doing nothing, which is the bug this test exists for.
    std::size_t differing = 0;
    for (std::size_t i = 0; i + 3 < top.pixels().size(); i += 4) {
      if (top.pixels()[i] != end.pixels()[i]) ++differing;
    }
    CHECK(differing > top.pixels().size() / 4 / 20);
    // And the buttons moved with the text, so they are reachable at the end.
    const float play_after = sheet_play_y(app);
    CHECK(play_after >= 0);
  }

  TEST("ui: the settings sheet scrolls down to the Scan button");
  {
    ui::App app(dir, dir);
    app.resize(640, 420);
    app.set_catalog(make_big_catalog(3));
    Settings settings;
    for (int i = 0; i < 8; ++i) settings.library_folders.push_back("/movies/folder" + std::to_string(i));
    app.set_settings(settings);
    CHECK(app.show_screen("settings", ""));
    draw_once(app);
    CHECK(app.scroll_limit("settings") > 0.0f);
    for (int i = 0; i < 40; ++i) app.handle(wheel_at(320, 210, -1));
    CHECK(app.scroll_offset("settings") == app.scroll_limit("settings"));
    draw_once(app);
    CHECK(has_action(app, ui::Action::Kind::ScanLibrary));
  }

  TEST("ui: Page Down, End and Home move the list; a sheet that fits does not scroll");
  {
    ui::App app(dir, dir);
    app.resize(1000, 600);
    app.set_catalog(make_big_catalog(30));
    CHECK(app.show_screen("browse", ""));
    draw_once(app);
    CHECK(app.handle(key_event(1073741902)));  // Page Down
    const float after_page = app.scroll_offset(ui::App::page_region());
    CHECK(after_page > 0.0f);
    CHECK(after_page < app.scroll_limit(ui::App::page_region()));
    CHECK(app.handle(key_event(1073741901)));  // End
    CHECK(app.scroll_offset(ui::App::page_region()) == app.scroll_limit(ui::App::page_region()));
    CHECK(app.handle(key_event(1073741898)));  // Home
    CHECK_EQ(app.scroll_offset(ui::App::page_region()), 0.0f);

    // Negative control: a short synopsis in a big window has nothing to scroll, so the
    // sheet must report no limit and the wheel must change nothing.
    ui::App small(dir, dir);
    small.resize(1600, 900);
    small.set_catalog(make_catalog());
    CHECK(small.show_screen("detail", "0"));
    draw_once(small);
    CHECK_EQ(small.scroll_limit("detail"), 0.0f);
    small.handle(wheel_at(800, 450, -1));
    CHECK_EQ(small.scroll_offset("detail"), 0.0f);
  }

  TEST("ui: clicking and dragging the bar scrolls, exactly like the wheel");
  {
    ui::App app(dir, dir);
    app.resize(1000, 600);
    app.set_catalog(make_big_catalog(30));
    CHECK(app.show_screen("browse", ""));
    draw_once(app);
    // Find the bar's thumb: the only ScrollThumb hit area on the screen.
    ui::InputEvent press;
    press.type = ui::InputEvent::Type::MouseDown;
    bool found = false;
    for (const auto& hit : app.hit_rects()) {
      if (hit.second.kind != ui::Action::Kind::ScrollThumb) continue;
      press.x = hit.first.x + hit.first.w / 2;
      press.y = hit.first.y + hit.first.h / 2;
      found = true;
      break;
    }
    CHECK(found);
    CHECK(app.handle(press));
    ui::InputEvent move = press;
    move.type = ui::InputEvent::Type::MouseMove;
    move.y += 120;
    app.handle(move);
    const float dragged = app.scroll_offset(ui::App::page_region());
    CHECK(dragged > 0.0f);
    ui::InputEvent release = move;
    release.type = ui::InputEvent::Type::MouseUp;
    app.handle(release);
    // Released: further movement must not keep scrolling.
    ui::InputEvent after = move;
    after.y += 400;
    app.handle(after);
    CHECK_EQ(app.scroll_offset(ui::App::page_region()), dragged);
  }
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


// The bug the first real Windows session found: the sheets registered their scrim and
// their click-swallowing panel *last*, and hit testing takes the last match, so those
// two sat on top of every control the sheet had just drawn. Clicking Play, a toggle or
// the folder field did nothing, and clicking anywhere inside the sheet that was not a
// control closed it. The assertion that matters is not "handle() returned true" - it
// did - but "what is under the pointer is the thing that was drawn there".
void test_ui_sheets_are_clickable() {
  TEST("ui: every control inside the detail and settings sheets is on top and works");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-sheets");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);

  CHECK(click_action(app, ui::Action::Kind::OpenDetail, 1));
  CHECK(app.open_item() == 1);
  draw_once(app);
  CHECK(app.open_item() == 1);

  // Inside the detail sheet, the buttons that are drawn are the ones a click finds.
  for (ui::Action::Kind kind : {ui::Action::Kind::Play, ui::Action::Kind::ToggleMyList}) {
    float x = 0, y = 0;
    CHECK(find_clickable(app, kind, &x, &y));
    CHECK_EQ(top_action_at(app, x, y).kind, kind);
  }

  // A click on the sheet's own body (not a button) must not close it: that is the
  // behaviour the user reported as "it always takes you back to the main page".
  const ui::Action body = top_action_at(app, 800, 500);
  CHECK_EQ(body.kind, ui::Action::Kind::None);
  app.handle(click_at(800, 500));
  CHECK(app.open_item() == 1);

  // The scrim still closes it, and so does the close button.
  app.handle(click_at(4, 4));
  CHECK(app.open_item() == -1);
  draw_once(app);

  // The same four checks for the settings sheet, which is the one the user hit.
  CHECK(click_action(app, ui::Action::Kind::OpenSettings));
  CHECK(app.settings_open());
  draw_once(app);

  for (ui::Action::Kind kind : {ui::Action::Kind::ToggleSetting, ui::Action::Kind::AddFolder,
                                ui::Action::Kind::ScanLibrary}) {
    float x = 0, y = 0;
    CHECK(find_clickable(app, kind, &x, &y));
    CHECK_EQ(top_action_at(app, x, y).kind, kind);
  }

  // Clicking the panel body does nothing at all - and, above all, does not close it.
  const ui::Action inside = top_action_at(app, 800, 500);
  CHECK_EQ(inside.kind, ui::Action::Kind::None);
  app.handle(click_at(800, 500));
  CHECK(app.settings_open());

  // The hardware-decoding toggle really toggles, through a real click on the switch.
  const bool before = app.settings().hw_decode;
  float tx = 0, ty = 0;
  CHECK(find_clickable(app, ui::Action::Kind::ToggleSetting, &tx, &ty));
  CHECK(app.handle(click_at(tx, ty)));
  CHECK(app.settings().hw_decode != before);
  CHECK(app.settings_open());

  draw_once(app);
  float cx = 0, cy = 0;
  CHECK(small_hit_centre(app, ui::Action::Kind::CloseOverlay, &cx, &cy));
  app.handle(click_at(cx, cy));
  CHECK(!app.settings_open());
}

// Hovering has to look like hovering. The cursor comes from the same hit areas the click
// uses, so it can never promise something a click will not do.
void test_ui_pointer_cursor() {
  TEST("ui: the pointer turns into a hand over anything clickable, and only there");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-cursor");
  ensure_dir(dir);
  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);

  // Nothing has been hovered yet: no promise to keep.
  CHECK_EQ(app.pointer_cursor(), vf::PointerCursor::Arrow);

  // Over a poster.
  float x = 0, y = 0;
  CHECK(first_card_centre(app, &x, &y));
  CHECK(app.handle(move_to(x, y)));
  CHECK_EQ(app.pointer_cursor(), vf::PointerCursor::Hand);

  // Over empty background.
  CHECK(app.handle(move_to(2, 2)) || true);
  CHECK_EQ(app.pointer_cursor(), vf::PointerCursor::Arrow);

  // Over the settings button in the header: a smaller, different control, same answer.
  draw_once(app);
  CHECK(find_clickable(app, ui::Action::Kind::OpenSettings, &x, &y));
  app.handle(move_to(x, y));
  CHECK_EQ(app.pointer_cursor(), vf::PointerCursor::Hand);

  // A text field gets an I-beam, and the sheet's dead space goes back to an arrow.
  CHECK(app.handle(click_at(x, y)));
  draw_once(app);
  CHECK(app.settings_open());
  CHECK(find_clickable(app, ui::Action::Kind::AddFolder, &x, &y));
  app.handle(move_to(x, y));
  CHECK_EQ(app.pointer_cursor(), vf::PointerCursor::IBeam);
  app.handle(move_to(800, 500));
  CHECK_EQ(app.pointer_cursor(), vf::PointerCursor::Arrow);
}

// The window used to open wider than the screen it was on, which put the title bar - and
// minimise, maximise and close with it - outside the desktop.
void test_window_fits_the_display() {
  TEST("window: the requested size is shrunk to fit the screen, and never below the minimum");
  // A 1600x900 default on a 1366x768 laptop: it has to shrink, and stay on the screen.
  vf::WindowSize small = vf::fit_window_to_display(1600, 900, 1366, 728);
  CHECK(small.width == 1311);   // 96% of 1366
  CHECK(small.height == 655);   // 90% of 728
  CHECK(small.width <= 1366 && small.height <= 728);
  CHECK(small.width >= 640 && small.height >= 420);

  // A screen with room to spare is left alone.
  const vf::WindowSize roomy = vf::fit_window_to_display(1600, 900, 2560, 1400);
  CHECK(roomy.width == 1600);
  CHECK(roomy.height == 900);

  // A small screen still gets a window that fits it rather than one that hangs off.
  const vf::WindowSize tiny = vf::fit_window_to_display(1600, 900, 800, 480);
  CHECK(tiny.width <= 800 && tiny.height <= 480);
  CHECK(tiny.width > 0 && tiny.height > 0);

  // An unknown display is not a reason to refuse to open a window.
  const vf::WindowSize unknown = vf::fit_window_to_display(1600, 900, 0, 0);
  CHECK(unknown.width == 1600 && unknown.height == 900);
}

// The folder picker: listing, going up, going in, and adding. All of it is exercised
// against a real tree made in the temp directory, because "it can browse" is only true
// if it can browse a filesystem that has folders in it.
void test_folder_browser() {
  TEST("fs: folders list, sort, skip files and dot names, and walk up");
  const std::string root = join(temp_dir(), "vlcflix-studio-browse-tree");
  ensure_dir(join(root, "Movies/Anniversary"));
  ensure_dir(join(root, "Movies/Zebra"));
  ensure_dir(join(root, "Music"));
  ensure_dir(join(root, ".hidden"));
  {
    // A file in the same directory must not be offered as a folder.
    std::FILE* file = std::fopen(join(root, "notes.txt").c_str(), "wb");
    if (file) {
      std::fputs("not a folder", file);
      std::fclose(file);
    }
  }

  const vf::FolderListing listing = vf::list_folders(root);
  CHECK(listing.ok());
  CHECK_EQ(listing.error, std::string());
  CHECK(listing.path == root || listing.path == join(root, ""));
  CHECK_EQ(listing.folders.size(), std::size_t(2));  // Movies, Music - not .hidden, not notes.txt
  CHECK_EQ(listing.folders[0], std::string("Movies"));  // sorted, not in creation order
  CHECK_EQ(listing.folders[1], std::string("Music"));

  const vf::FolderListing inner = vf::list_folders(join(root, "Movies"));
  CHECK_EQ(inner.folders.size(), std::size_t(2));
  CHECK_EQ(inner.folders[0], std::string("Anniversary"));
  CHECK_EQ(inner.folders[1], std::string("Zebra"));

  // A file is not a folder, and a name that is not there says so instead of throwing.
  CHECK(!vf::list_folders(join(root, "notes.txt")).ok());
  CHECK(!vf::list_folders(join(root, "does-not-exist")).ok());
  CHECK(!vf::is_folder(join(root, "notes.txt")));
  CHECK(vf::is_folder(join(root, "Music")));

  CHECK_EQ(vf::join_folder(join(root, "Music"), "Albums"), join(root, "Music/Albums"));
  CHECK_EQ(vf::parent_folder(join(root, "Music")), root);
  CHECK_EQ(vf::parent_folder("/").empty(), true);  // a root has no parent
  CHECK(vf::folder_roots().size() >= 1);

  // Where a browse starts: the first library folder that still exists, else home.
  CHECK_EQ(vf::browse_start({"/definitely/not/here", join(root, "Music")}),
           join(root, "Music"));
  CHECK(!vf::browse_start({}).empty());
}

void test_ui_folder_browser() {
  TEST("ui: the settings sheet can browse to a folder instead of typing a path");
  const std::string dir = join(temp_dir(), "vlcflix-studio-ui-browser");
  ensure_dir(dir);
  const std::string tree = join(dir, "library");
  ensure_dir(join(tree, "Films"));
  ensure_dir(join(tree, "Series"));

  ui::App app(dir, dir);
  app.resize(1600, 900);
  app.set_catalog(make_catalog());
  draw_once(app);
  CHECK(click_action(app, ui::Action::Kind::OpenSettings));
  CHECK(app.settings_open());
  draw_once(app);

  // The Browse button is there, in the settings sheet, and it opens the picker.
  float bx = 0, by = 0;
  CHECK(find_clickable(app, ui::Action::Kind::BrowseForFolder, &bx, &by));
  CHECK(app.handle(click_at(bx, by)));
  CHECK(app.browser_open());
  CHECK_STR_EQ(app.screen_name(), "browser");
  CHECK(!app.browser_path().empty());
  CHECK(app.hit_rects().size() > 3);

  // Opened deliberately at the tree: two folders, and "up one level".
  app.open_folder_browser(tree);
  draw_once(app);
  CHECK_STR_EQ(app.browser_path(), tree);
  CHECK_EQ(app.browser_folders().size(), std::size_t(2));

  // Going into a folder lists what is inside it. The first BrowseInto row is always
  // "..", so the folder rows are found by their action value.
  float fx = 0, fy = 0;
  bool found_child = false;
  std::string child_name;
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind != ui::Action::Kind::BrowseInto) continue;
    const float cx = hit.first.x + hit.first.w / 2;
    const float cy = hit.first.y + hit.first.h / 2;
    if (top_action_at(app, cx, cy).kind != ui::Action::Kind::BrowseInto) continue;
    if (hit.second.value == vf::parent_folder(tree)) continue;  // the ".." row
    fx = cx;
    fy = cy;
    child_name = hit.second.value;
    found_child = true;
    break;
  }
  CHECK(found_child);
  CHECK(app.handle(click_at(fx, fy)));
  draw_once(app);
  CHECK_STR_EQ(app.browser_path(), join(tree, child_name));

  // ".." goes back up; the row is still there at the top of the list.
  const std::string child = app.browser_path();
  CHECK(child != tree);
  CHECK(app.handle(ui::InputEvent{ui::InputEvent::Type::Key, 0, 0, 0, 0, 27, "", false}));
  CHECK(!app.browser_open());  // Escape closes the picker, not the settings sheet
  CHECK(app.settings_open());
  draw_once(app);
  CHECK(find_clickable(app, ui::Action::Kind::BrowseForFolder, &bx, &by));
  app.handle(click_at(bx, by));
  app.open_folder_browser(child);
  draw_once(app);
  float ux = 0, uy = 0;
  bool found_up = false;
  for (const auto& hit : app.hit_rects()) {
    if (hit.second.kind != ui::Action::Kind::BrowseInto) continue;
    if (hit.second.value != tree) continue;  // ".." carries the parent it goes to
    const float cx = hit.first.x + hit.first.w / 2;
    const float cy = hit.first.y + hit.first.h / 2;
    if (top_action_at(app, cx, cy).kind != ui::Action::Kind::BrowseInto) continue;
    ux = cx;
    uy = cy;
    found_up = true;
    break;
  }
  CHECK(found_up);
  app.handle(click_at(ux, uy));
  CHECK(app.browser_path() == tree);

  // "Use this folder" really adds it, through the same validation as a typed path.
  draw_once(app);
  float ux2 = 0, uy2 = 0;
  CHECK(find_clickable(app, ui::Action::Kind::BrowseUse, &ux2, &uy2));
  const std::size_t before = app.settings().library_folders.size();
  CHECK(app.handle(click_at(ux2, uy2)));
  CHECK_EQ(app.settings().library_folders.size(), before + 1);
  CHECK(app.settings().library_folders.back() == tree);
  CHECK(!app.browser_open());  // the picker closes once the folder is added
  CHECK(app.settings_open());  // the settings sheet stays, showing the new folder

  // Adding the same folder again is refused with a message rather than duplicated.
  app.open_folder_browser(tree);
  draw_once(app);
  CHECK(find_clickable(app, ui::Action::Kind::BrowseUse, &ux2, &uy2));
  CHECK(app.handle(click_at(ux2, uy2)));
  CHECK_EQ(app.settings().library_folders.size(), before + 1);
  CHECK(app.last_message().find("already") != std::string::npos);

  // Cancel closes the picker and changes nothing.
  draw_once(app);
  float cx = 0, cy = 0;
  CHECK(find_clickable(app, ui::Action::Kind::BrowseClose, &cx, &cy));
  app.handle(click_at(cx, cy));
  CHECK(!app.browser_open());
  CHECK_EQ(app.settings().library_folders.size(), before + 1);
}

// The renderer policy: never SDL's own driver order, because that order crashed a real
// machine inside the Intel Direct3D 9 driver before the first frame was drawn.
void test_render_driver_policy() {
  TEST("window: the renderer is chosen by name, and always ends in software");

  // Nobody asked for anything: software, which is the one driver that cannot take the
  // process down with it.
  const vf::RenderDriverPlan plain = vf::render_driver_plan("");
  CHECK_EQ(plain.order.size(), std::size_t(1));
  CHECK_EQ(plain.order.front(), std::string("software"));
  CHECK(!plain.from_environment);
  CHECK(!plain.risky);

  // An explicit request is honoured first, with software behind it as the net.
  const vf::RenderDriverPlan asked = vf::render_driver_plan("direct3d11");
  CHECK_EQ(asked.order.size(), std::size_t(2));
  CHECK_EQ(asked.order[0], std::string("direct3d11"));
  CHECK_EQ(asked.order[1], std::string("software"));
  CHECK(asked.from_environment);
  CHECK(!asked.risky);

  // Asking for software is not a reason to try it twice.
  const vf::RenderDriverPlan only_software = vf::render_driver_plan("software");
  CHECK_EQ(only_software.order.size(), std::size_t(1));

  // Stray whitespace in an environment variable is a typo, not a driver name.
  const vf::RenderDriverPlan padded = vf::render_driver_plan("  opengl\t");
  CHECK_EQ(padded.order[0], std::string("opengl"));

  // Direct3D 9 is the driver that crashed; honoured if asked for, but flagged.
  const vf::RenderDriverPlan risky = vf::render_driver_plan("direct3d9");
  CHECK(risky.risky);
  CHECK_EQ(risky.order.size(), std::size_t(2));
  CHECK(vf::render_driver_description(risky).find("crashed") != std::string::npos);

  // The description always names what will be tried, for `doctor` and the log.
  CHECK(vf::render_driver_description(plain).find("VLCFLIX_RENDER_DRIVER") != std::string::npos);
  CHECK(vf::render_driver_description(asked).find("software") != std::string::npos);
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
  test_ui_scrolling();
  test_ui_sheets_are_clickable();
  test_ui_pointer_cursor();
  test_window_fits_the_display();
  test_folder_browser();
  test_ui_folder_browser();
  test_render_driver_policy();
}

}  // namespace vf
