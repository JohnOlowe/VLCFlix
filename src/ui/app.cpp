#include "ui/app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "core/catalog.h"
#include "core/log.h"
#include "core/paths.h"
#include "library/scan.h"

namespace vf {
namespace ui {
namespace {

double now_seconds() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// SDL key codes, repeated here so this file has no toolkit dependency.
constexpr int kKeyBackspace = 8;
constexpr int kKeyTab = 9;
constexpr int kKeyReturn = 13;
constexpr int kKeyEscape = 27;
constexpr int kKeyLeft = 1073741904;
constexpr int kKeyUp = 1073741906;
constexpr int kKeyDown = 1073741905;
constexpr int kKeyRight = 1073741903;
constexpr int kKeySlash = '/';
constexpr int kKeyF = 'f';

constexpr float kPosterWidth = 156.0f;
constexpr float kWideWidth = 268.0f;
constexpr float kGridWidth = 172.0f;

void draw_triangle_right(Canvas& canvas, const Rect& area, Color color) {
  // Filled play triangle, drawn as scanlines: the canvas has no path filling.
  const int rows = static_cast<int>(area.h);
  for (int i = 0; i < rows; ++i) {
    const double t = rows > 1 ? static_cast<double>(i) / (rows - 1) : 0.0;
    const double half = (1.0 - std::fabs(2.0 * t - 1.0)) * area.w;
    canvas.fill_rect(Rect{area.x, area.y + i, static_cast<float>(half), 1}, color);
  }
}

std::string upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return text;
}

}  // namespace

App::App(std::string media_dir, std::string data_dir)
    : media_dir_(std::move(media_dir)), data_dir_(data_dir), store_(data_dir) {
  if (!regular_.load_default(false)) LOG_ERROR("UI: no font, text will be missing");
  if (!bold_.load_default(true)) LOG_WARN("UI: no bold font, using regular");
}

void App::set_catalog(Catalog catalog) { catalog_ = std::move(catalog); }
void App::set_progress(std::map<std::string, Progress> progress) { progress_ = std::move(progress); }
void App::set_my_list(std::vector<std::string> my_list) { my_list_ = std::move(my_list); }
void App::set_settings(Settings settings) { settings_ = std::move(settings); }
void App::set_scan_state(const std::string& label) { scan_label_ = label; }

void App::resize(int width, int height) {
  width_ = std::max(640, width);
  height_ = std::max(420, height);
}

void App::add_hit(const Rect& rect, Action action) {
  if (action.kind == Action::Kind::None) return;
  hits_.push_back({rect, std::move(action)});
}

Action App::hit_test(float x, float y) const {
  // Last drawn wins, which is what "on top" means for an immediate-mode UI.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->first.contains(x, y)) return it->second;
  }
  return Action{};
}

void App::notify(const std::string& message) {
  last_message_ = message;
  message_until_ = now_seconds() + 3.0;
  LOG_INFO("ui: %s", message.c_str());
}

std::string App::format_duration(double seconds) const {
  if (!(seconds > 0)) return "";
  const int total = static_cast<int>(seconds + 0.5);
  char buf[32];
  if (total >= 3600) {
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
  } else {
    std::snprintf(buf, sizeof buf, "%d min", std::max(1, total / 60));
  }
  return buf;
}

std::string App::meta_line(const Item& item) const {
  std::vector<std::string> parts;
  if (item.year) parts.push_back(std::to_string(item.year));
  const std::string duration = format_duration(item.duration);
  if (!duration.empty()) parts.push_back(duration);
  if (!item.rating.empty()) parts.push_back(item.rating);
  if (!item.genres.empty()) {
    std::string genres;
    for (std::size_t i = 0; i < item.genres.size() && i < 3; ++i) {
      if (i) genres += " · ";
      genres += item.genres[i];
    }
    parts.push_back(genres);
  }
  if (item.width && item.height) {
    parts.push_back(std::to_string(item.height) + "p");
  }
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += "   ·   ";
    out += parts[i];
  }
  return out;
}

int App::percent_for(const Item& item) const {
  const auto it = progress_.find(item.id);
  if (it == progress_.end()) return 0;
  return progress_percent(it->second);
}

const Image* App::poster_for(const Item& item) {
  if (item.poster.empty()) return nullptr;
  return images_.load(join(media_dir_, item.poster));
}

const Image* App::backdrop_for(const Item& item) {
  if (item.backdrop.empty()) return nullptr;
  return images_.load(join(media_dir_, item.backdrop));
}

/* --------------------------------- drawing -------------------------------- */

void App::draw(Canvas& canvas) {
  hits_.clear();
  if (player_open_) {
    // The player owns the whole surface: no header, no rails, no overlays.
    draw_player_frame(canvas);
    return;
  }
  order_.clear();
  // Home is a set of single-row rails, so vertical movement there has nowhere to go;
  // Browse and My List are grids and get their real column count below.
  order_columns_ = 1 << 20;
  canvas.clear(theme::bg());

  if (view_ == View::Home) {
    draw_home(canvas);
  } else if (view_ == View::Browse) {
    std::vector<std::size_t> all(catalog_.items.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    draw_grid_view(canvas, "Browse All", all,
                   std::to_string(catalog_.items.size()) + " titles in your library");
  } else if (view_ == View::MyList) {
    std::vector<std::size_t> mine;
    for (const auto& id : my_list_) {
      for (std::size_t i = 0; i < catalog_.items.size(); ++i) {
        if (catalog_.items[i].id == id) mine.push_back(i);
      }
    }
    draw_grid_view(canvas, "My List", mine,
                   mine.empty() ? "Nothing here yet — add films with the + button"
                                : std::to_string(mine.size()) + " saved");
  } else {
    const std::vector<std::size_t> found = search_items(catalog_.items, query_);
    draw_grid_view(canvas, query_.empty() ? "Search" : "Results for “" + query_ + "”", found,
                   query_.empty() ? "Type to search titles, genres and years"
                                  : std::to_string(found.size()) + " match(es)");
  }

  draw_hover_preview(canvas);
  draw_header(canvas);

  if (open_item_ >= 0 && open_item_ < static_cast<int>(catalog_.items.size())) {
    draw_detail(canvas, catalog_.items[static_cast<std::size_t>(open_item_)]);
  }
  if (settings_open_) draw_settings(canvas);
  draw_toast(canvas);
}

namespace {
// The hero's height, and therefore where the rails begin, is derived from its own
// content: title (52), meta line, two synopsis lines and a row of buttons. The rails
// start *below* that block, which is what stops a rail title from being drawn over
// the hero's buttons (a real overlap I found in the first screenshot, not a guess).
constexpr float kHeroTextBlock = 200.0f;
constexpr float kRailsGap = 26.0f;
}  // namespace

Rect App::hero_rect(float scroll) const {
  const float available = static_cast<float>(height_) - theme::kHeaderHeight - 120.0f;
  const float hero_height = std::clamp(static_cast<float>(height_) * 0.64f, 360.0f, 560.0f);
  (void)available;
  return Rect{0, -scroll, static_cast<float>(width_), hero_height};
}

Rect App::rails_rect(float scroll) const {
  const float hero_height = hero_rect(0).h;
  const float top = hero_height + kRailsGap + scroll;
  return Rect{0, top, static_cast<float>(width_), static_cast<float>(height_) - top};
}

void App::draw_backdrop_or_fallback(Canvas& canvas, const Rect& rect, const Item& item,
                                    float radius) {
  if (const Image* image = backdrop_for(item)) {
    canvas.blit_rounded(*image, rect, radius, true);
    return;
  }
  Color accent = item.accent.empty() ? theme::poster_fallback(item.title, false)
                                     : rgb(0);
  if (!item.accent.empty()) {
    // "#rrggbb" from a sidecar file.
    const std::string& hex = item.accent;
    if (hex.size() >= 7 && hex[0] == '#') {
      const uint32_t value = static_cast<uint32_t>(std::strtoul(hex.c_str() + 1, nullptr, 16));
      accent = rgb(value);
    }
  }
  canvas.fill_round_rect(rect, radius, accent);
  canvas.fill_gradient_v(rect, rgba(0x000000, 0), rgba(0x000000, 140));
}

float App::button_width(const std::string& label, bool primary, bool icon) const {
  const float text = bold_.measure(label, theme::kFontBody).width;
  const float left = primary ? (icon ? 50.0f : 18.0f) : 18.0f;
  return left + text + 20.0f;
}

void App::draw_button(Canvas& canvas, const Rect& rect, const std::string& label, bool primary,
                      Action action, bool icon) {
  const bool hovered = false;
  if (primary) {
    canvas.fill_round_rect(rect, theme::kRadius + 2, theme::accent());
    Color text_color = rgb(0x1a0d00);
    if (icon) {
      draw_triangle_right(canvas, Rect{rect.x + 16, rect.y + rect.h / 2 - 7, 11, 14}, text_color);
    }
    bold_.draw(canvas, label, rect.x + (icon ? 36 : 18),
               rect.y + (rect.h - bold_.line_height(theme::kFontBody)) / 2, theme::kFontBody,
               text_color);
  } else {
    canvas.fill_round_rect(rect, theme::kRadius + 2, rgba(0xffffff, hovered ? 40 : 22));
    canvas.stroke_round_rect(rect, theme::kRadius + 2, 1.0f, rgba(0xffffff, 60));
    bold_.draw(canvas, label, rect.x + 18,
               rect.y + (rect.h - bold_.line_height(theme::kFontBody)) / 2, theme::kFontBody,
               theme::text());
  }
  add_hit(rect, action);
}

void App::draw_poster_card(Canvas& canvas, const Rect& rect, const Item& item, bool wide,
                           float scroll) {
  (void)scroll;
  const float card_height = wide ? rect.w / theme::kBackdropAspect : rect.w / theme::kPosterAspect;
  const Rect card{rect.x, rect.y, rect.w, card_height};
  const auto is_index = [&](int index) {
    return index >= 0 && static_cast<std::size_t>(index) < catalog_.items.size() &&
           catalog_.items[static_cast<std::size_t>(index)].id == item.id;
  };
  const bool hovered = is_index(hover_item_);
  const bool focused = is_index(focus_item_);

  // Shadow / lift on hover.
  if (hovered) {
    canvas.fill_round_rect(Rect{card.x - 4, card.y - 4, card.w + 8, card.h + 8},
                           theme::kCardRadius + 4, rgba(theme::kAccent, 46));
  }

  const Image* image = wide ? backdrop_for(item) : poster_for(item);
  if (image) {
    canvas.blit_rounded(*image, card, theme::kCardRadius, true);
  } else if (wide) {
    draw_backdrop_or_fallback(canvas, card, item, theme::kCardRadius);
  } else {
    Color base = theme::poster_fallback(item.title, false);
    canvas.fill_round_rect(card, theme::kCardRadius, base);
    // Initial letter, so a card with no artwork still reads as a film.
    if (!item.title.empty()) {
      const std::string initial = item.title.substr(0, 1);
      bold_.draw(canvas, upper(initial), card.x + 14, card.y + card.h - 64, 54,
                 rgba(0xffffff, 70));
    }
  }

  // Bottom scrim keeps the progress bar and title legible over a bright still.
  canvas.push_clip(card);
  canvas.fill_gradient_v(Rect{card.x, card.bottom() - 74, card.w, 74}, rgba(0x000000, 0),
                         rgba(0x000000, 190));
  canvas.pop_clip();

  if (const int percent = percent_for(item); percent > 0) {
    const float bar_height = 4;
    canvas.fill_round_rect(Rect{card.x + 10, card.bottom() - bar_height - 8, card.w - 20, bar_height},
                           bar_height / 2, rgba(0xffffff, 60));
    canvas.fill_round_rect(
        Rect{card.x + 10, card.bottom() - bar_height - 8, (card.w - 20) * percent / 100.0f, bar_height},
        bar_height / 2, theme::accent());
  }

  if (hovered || focused) {
    // Hover and keyboard focus look the same on purpose: both mean "this is the card
    // that a click or Enter would act on".
    canvas.stroke_round_rect(card, theme::kCardRadius, hovered ? 2.0f : 2.5f,
                             rgba(theme::kAccent, focused ? 255 : 220));
  }

  const Rect label{card.x, card.bottom() + 8, card.w, 18};
  bold_.draw(canvas, bold_.ellipsize(item.title, theme::kFontSmall, card.w), label.x, label.y,
             theme::kFontSmall, theme::text());

  Action action;
  action.kind = Action::Kind::OpenDetail;
  action.index = static_cast<int>(&item - catalog_.items.data());
  add_hit(card, action);
}

void App::set_hover_preview(int index) {
  if (index == preview_index_) return;
  preview_index_ = index;
  preview_has_frame_ = false;
  preview_engine_.stop();
  if (index < 0 || static_cast<std::size_t>(index) >= catalog_.items.size()) return;
  const Item& item = catalog_.items[static_cast<std::size_t>(index)];
  std::string path = !item.preview.empty() ? join(media_dir_, item.preview) : item.path;
  if (!file_exists(path)) path = item.path;
  if (!file_exists(path)) return;
  preview_engine_.add_argument("--aout=dummy");  // hover previews are silent by design
  preview_engine_.configure(640, 360);
  preview_engine_.set_clock([this] { return clock_now(); });
  std::string error;
  if (!preview_engine_.open(path, &error)) {
    LOG_WARN("preview: %s", error.c_str());
    preview_index_ = -1;
    return;
  }
  preview_engine_.set_muted(true);
  preview_engine_.play();
  if (item.duration > 10) preview_engine_.seek(item.duration * 0.2);
  preview_started_ = clock_now();
  LOG_INFO("preview: playing %s", path.c_str());
}

void App::draw_hover_preview(Canvas& canvas) {
  if (view_ == View::Search && query_.empty()) return;
  if (open_item_ >= 0 || settings_open_) return;
  if (hover_item_ < 0 || static_cast<std::size_t>(hover_item_) >= catalog_.items.size()) return;
  // Which card is hovered, and where was it drawn? The hit areas were recorded while
  // drawing, so this uses the same geometry the mouse is being tested against.
  const Rect* card = nullptr;
  for (const auto& hit : hits_) {
    if (hit.second.kind == Action::Kind::OpenDetail && hit.second.index == hover_item_) {
      card = &hit.first;
      break;
    }
  }
  if (!card) return;
  const Item& item = catalog_.items[static_cast<std::size_t>(hover_item_)];

  const float width = 336;
  const float media_height = width / theme::kBackdropAspect;
  const float height = media_height + 104;
  float x = card->x + card->w / 2 - width / 2;
  x = std::max(static_cast<float>(theme::kGutter),
               std::min(x, static_cast<float>(width_) - theme::kGutter - width));
  // Above the card when there is room, below it otherwise: never over the card the
  // mouse is actually on.
  float y = card->y - height - 12;
  if (y < theme::kHeaderHeight + 8) y = card->bottom() + 44;

  canvas.push_clip(Rect{0, static_cast<float>(theme::kHeaderHeight), static_cast<float>(width_),
                        static_cast<float>(height_) - theme::kHeaderHeight});
  const Rect panel{x, y, width, height};
  canvas.fill_round_rect(Rect{panel.x - 2, panel.y + 6, panel.w + 4, panel.h}, 12, rgba(0x000000, 130));
  canvas.fill_round_rect(panel, 12, theme::panel());
  canvas.stroke_round_rect(panel, 12, 1.0f, rgba(0xffffff, 40));

  const Rect media{panel.x, panel.y, width, media_height};
  canvas.push_clip(media);
  const Image* preview = backdrop_for(item);
  if (preview_has_frame_ && preview_frame_.width > 0 && preview_index_ == hover_item_) {
    canvas.blit_raw(preview_frame_.pixels.data(), preview_frame_.width, preview_frame_.height,
                    media, true);
  } else if (preview) {
    canvas.blit(*preview, media, false);
  } else {
    Color base = theme::poster_fallback(item.title, true);
    canvas.fill_gradient_v(media, base, rgba(theme::kBg, 255));
  }
  canvas.pop_clip();

  bold_.draw(canvas, bold_.ellipsize(item.title, theme::kFontBody, panel.w - 32), panel.x + 16,
             panel.y + media_height + 14, theme::kFontBody, theme::text());
  regular_.draw(canvas, regular_.ellipsize(meta_line(item), theme::kFontTiny, panel.w - 32),
                panel.x + 16, panel.y + media_height + 40, theme::kFontTiny, theme::dim());
  // A compact hint instead of a full button: the card underneath is still the click
  // target, and a second Play button here would mean two ways to do one thing.
  const Rect play_icon{panel.x + 16, panel.bottom() - 34, 18, 18};
  canvas.fill_circle(play_icon.x + 9, play_icon.y + 9, 9, theme::accent());
  draw_triangle_right(canvas, Rect{play_icon.x + 6, play_icon.y + 4, 7, 10},
                      rgba(0x1a0d00, 255));
  regular_.draw(canvas, "Click to open, Enter to play", play_icon.right() + 8,
                panel.y + media_height + 62, theme::kFontTiny, theme::faint());
  canvas.pop_clip();
}

void App::draw_header(Canvas& canvas) {
  const float header_height = theme::kHeaderHeight;
  // A transparent header over the hero, solid elsewhere.
  if (view_ == View::Home && home_scroll_ < 40) {
    canvas.fill_gradient_v(Rect{0, 0, float(width_), header_height + 60}, rgba(0x000000, 190),
                           rgba(0x000000, 0));
  } else {
    canvas.fill_rect(Rect{0, 0, float(width_), header_height}, rgba(theme::kBgRaised, 246));
    canvas.fill_rect(Rect{0, header_height, float(width_), 1}, rgba(0xffffff, 18));
  }

  float x = theme::kGutter;
  const float mid = header_height / 2;
  bold_.draw(canvas, "VLC", x, mid - 11, 21, theme::accent());
  const float vlc_width = bold_.measure("VLC", 21).width;
  bold_.draw(canvas, "FLIX", x + vlc_width, mid - 11, 21, theme::text());
  x += vlc_width + bold_.measure("FLIX", 21).width + 42;

  struct NavEntry {
    const char* label;
    View view;
    bool selected;
    Action action;
  };
  const NavEntry nav[] = {
      {"Home", View::Home, view_ == View::Home, Action{Action::Kind::Navigate, -1, "home"}},
      {"Browse All", View::Browse, view_ == View::Browse, Action{Action::Kind::Navigate, -1, "browse"}},
      {"My List", View::MyList, view_ == View::MyList, Action{Action::Kind::Navigate, -1, "mylist"}},
  };
  for (const auto& entry : nav) {
    const float text_width = regular_.measure(entry.label, theme::kFontBody).width;
    const Rect area{x, mid - 11, text_width + 8, 22};
    regular_.draw(canvas, entry.label, x, mid - 9, theme::kFontBody,
                  entry.selected ? theme::text() : theme::dim());
    add_hit(area, entry.action);
    x += text_width + 26;
  }

  // Right-hand cluster: search, engine chip, settings.
  float right = width_ - theme::kGutter;
  const Rect settings_rect{right - 100, mid - 17, 100, 34};
  canvas.fill_round_rect(settings_rect, 17, rgba(0xffffff, 16));
  regular_.draw_centered(canvas, "Settings", settings_rect, theme::kFontSmall, theme::text());
  add_hit(settings_rect, Action{Action::Kind::OpenSettings, -1, ""});
  right = settings_rect.x - 12;

  const std::string chip = scan_label_.empty() ? "libVLC · native" : scan_label_;
  const float chip_width = regular_.measure(chip, theme::kFontTiny).width + 34;
  const Rect chip_rect{right - chip_width, mid - 15, chip_width, 30};
  canvas.fill_round_rect(chip_rect, 15, rgba(theme::kAccent, 30));
  canvas.fill_circle(chip_rect.x + 13, mid, 4, theme::accent());
  regular_.draw(canvas, chip, chip_rect.x + 23, mid - 8, theme::kFontTiny, theme::text());
  right = chip_rect.x - 12;

  const Rect search_rect{right - 250, mid - 18, 250, 36};
  canvas.fill_round_rect(search_rect, 18, rgba(0xffffff, search_focused_ ? 34 : 18));
  if (search_focused_) {
    canvas.stroke_round_rect(search_rect, 18, 1.5f, rgba(theme::kAccent, 200));
  }
  const std::string shown = query_.empty() ? "Titles, genres, years" : query_;
  regular_.draw(canvas, shown, search_rect.x + 34, mid - 9, theme::kFontSmall,
                query_.empty() ? theme::faint() : theme::text());
  if (search_focused_ && query_.empty()) {
    canvas.fill_rect(Rect{search_rect.x + 32, mid - 10, 1.5f, 20}, theme::accent());
  }
  // Magnifier.
  canvas.stroke_circle(search_rect.x + 19, mid - 2, 6.5f, 1.6f, theme::dim());
  canvas.fill_rect(Rect{search_rect.x + 24, mid + 3, 7, 1.6f}, theme::dim());
  add_hit(search_rect, Action{Action::Kind::FocusSearch, -1, ""});
}

void App::draw_home(Canvas& canvas) {
  const Rect hero = hero_rect(home_scroll_);
  const Item* featured = nullptr;
  if (!catalog_.items.empty()) {
    featured = &catalog_.items.front();
  }

  if (featured) {
    canvas.push_clip(Rect{0, 0, float(width_), hero.h + hero.y + (hero.y < 0 ? 0 : 0)});
    const Rect full{0, hero.y, float(width_), hero.h};
    draw_backdrop_or_fallback(canvas, full, *featured, 0);
    // Scrims: left-to-right for the text, bottom-to-top into the page background.
    canvas.fill_gradient_h(Rect{0, hero.y, width_ * 0.62f, hero.h}, rgba(0x000000, 205),
                           rgba(0x000000, 0));
    // Opaque band under the text: the gradient above it fades the still, so the rails
    // start on solid background and nothing is ever drawn over the title or synopsis.
    const float band_top = hero.bottom() - kHeroTextBlock - 36;
    canvas.fill_gradient_v(Rect{0, band_top - 150, float(width_), 150}, rgba(theme::kBg, 0),
                           theme::bg());
    canvas.fill_rect(Rect{0, band_top, float(width_), hero.bottom() - band_top + 1}, theme::bg());
    canvas.pop_clip();

    const float text_x = theme::kGutter;
    const float text_width = std::min(width_ * 0.46f, 620.0f);
    const float title_size = theme::kFontHero;
    float y = hero.bottom() - kHeroTextBlock + 4;
    bold_.draw(canvas, bold_.ellipsize(featured->title, title_size, text_width), text_x, y,
               title_size, theme::text());
    y += bold_.line_height(title_size) + 10;
    regular_.draw(canvas, meta_line(*featured), text_x, y, theme::kFontSmall, theme::dim());
    y += 26;
    if (!featured->synopsis.empty()) {
      regular_.draw_wrapped(canvas, featured->synopsis, Rect{text_x, y, text_width, 44},
                            theme::kFontSmall, theme::text(), 2);
      y += 50;
    } else {
      y += 8;
    }
    const Rect play_rect{text_x, y, button_width("Play", true), 42};
    draw_button(canvas, play_rect, "Play", true,
                Action{Action::Kind::Play, 0, ""});
    const bool in_list = std::find(my_list_.begin(), my_list_.end(), featured->id) != my_list_.end();
    const std::string list_label = in_list ? "In My List" : "My List";
    const Rect list_rect{play_rect.right() + 12, y, button_width(list_label, false), 42};
    draw_button(canvas, list_rect, list_label, false,
                Action{Action::Kind::ToggleMyList, 0, ""});
    draw_button(canvas, Rect{list_rect.right() + 12, y, button_width("Details", false), 42},
                "Details", false, Action{Action::Kind::OpenDetail, 0, ""});
  } else {
    // Empty library: say what to do rather than showing a blank window.
    const Rect box{theme::kGutter, 140, width_ - 2 * theme::kGutter, 236};
    canvas.fill_round_rect(box, 12, theme::panel());
    bold_.draw(canvas, "Your library is empty", box.x + 32, box.y + 40, 26, theme::text());
    regular_.draw_wrapped(canvas,
                          "Point VLCFlix Studio at the folders that hold your films in Settings, "
                          "then press Scan library. Artwork is generated from the films themselves.",
                          Rect{box.x + 32, box.y + 92, box.w - 64, 90}, theme::kFontBody, theme::dim());
    draw_button(canvas, Rect{box.x + 32, box.y + 168, button_width("Open Settings", true), 40},
                "Open Settings", true, Action{Action::Kind::OpenSettings, -1, ""});
  }

  /* --------------------------------- rails -------------------------------- */
  const std::vector<Rail> rails = build_rails(catalog_.items, progress_, my_list_);
  float y = rails_rect(home_scroll_).y;
  canvas.push_clip(Rect{0, theme::kHeaderHeight, float(width_), float(height_) - theme::kHeaderHeight});

  for (const auto& rail : rails) {
    const bool wide = rail.id == "continue";
    const float card_width = wide ? kWideWidth : kPosterWidth;
    const float card_height = wide ? card_width / theme::kBackdropAspect : card_width / theme::kPosterAspect;

    if (y > -card_height && y < height_) {
      bold_.draw(canvas, rail.title, theme::kGutter, y, theme::kFontLabel, theme::text());
      regular_.draw_right(canvas, std::to_string(rail.items.size()) + " films",
                          Rect{theme::kGutter, y, width_ - 2 * theme::kGutter, 22},
                          theme::kFontTiny, theme::faint());
    }
    y += 30;
    float x = theme::kGutter;
    for (std::size_t index : rail.items) {
      if (x + card_width > width_ - theme::kGutter / 2) break;  // one row, as the CSS rails do
      if (x + card_width > 0 && x < width_) {
        draw_poster_card(canvas, Rect{x, y, card_width, card_height}, catalog_.items[index], wide,
                         home_scroll_);
        order_.push_back(static_cast<int>(index));
      }
      x += card_width + theme::kRailGap;
    }
    y += card_height + 48;
  }
  canvas.pop_clip();

  // Scroll bounds, so the last rail can be reached but no further.
  const float content_height = y - rails_rect(0).y;
  const float max_scroll = std::max(0.0f, content_height - (height_ - theme::kHeaderHeight) + 80);
  if (home_scroll_ > max_scroll) home_scroll_ = max_scroll;

  if (rails.empty() && featured) {
    regular_.draw(canvas, "Nothing in progress yet — press Play to start a film.",
                  theme::kGutter, rails_rect(home_scroll_).y, theme::kFontSmall, theme::faint());
  }
}

void App::draw_grid_view(Canvas& canvas, const std::string& title,
                         const std::vector<std::size_t>& items, const std::string& subtitle) {
  const float top = theme::kHeaderHeight + 34;
  bold_.draw(canvas, title, theme::kGutter, top, 30, theme::text());
  regular_.draw(canvas, subtitle, theme::kGutter, top + 44, theme::kFontSmall, theme::dim());

  const float grid_top = top + 84;
  canvas.push_clip(Rect{0, grid_top - 8, float(width_), float(height_) - grid_top + 8});

  const float available = static_cast<float>(width_) - 2 * theme::kGutter;
  const float gap = 18;
  const int columns = std::max(1, static_cast<int>((available + gap) / (kGridWidth + gap)));
  const float card_width = (available - gap * (columns - 1)) / columns;
  const float card_height = card_width / theme::kPosterAspect;
  const float row_height = card_height + 44;

  order_columns_ = columns;
  order_row_height_ = card_height + 44;
  for (std::size_t i = 0; i < items.size(); ++i) {
    order_.push_back(static_cast<int>(items[i]));
    const int column = static_cast<int>(i) % columns;
    const int row = static_cast<int>(i) / columns;
    const Rect card{theme::kGutter + column * (card_width + gap),
                    grid_top + row * row_height - grid_scroll_, card_width, card_height};
    if (card.bottom() < grid_top - 40 || card.y > height_) continue;
    draw_poster_card(canvas, card, catalog_.items[items[i]], false, grid_scroll_);
  }
  canvas.pop_clip();

  const int rows_total = columns > 0 ? static_cast<int>((items.size() + columns - 1) / columns) : 0;
  const float content = rows_total * row_height;
  const float max_scroll = std::max(0.0f, content - (height_ - grid_top) + 40);
  if (grid_scroll_ > max_scroll) grid_scroll_ = max_scroll;
  if (grid_scroll_ < 0) grid_scroll_ = 0;

  if (items.empty()) {
    regular_.draw(canvas, "No films here.", theme::kGutter, grid_top, theme::kFontBody, theme::faint());
  }
}

void App::draw_detail(Canvas& canvas, const Item& item) {
  canvas.fill_rect(Rect{0, 0, float(width_), float(height_)}, rgba(0x000000, 190));

  const float panel_width = std::min(width_ - 120.0f, 980.0f);
  const float art_height = std::min(panel_width / 2.55f, 360.0f);
  // The panel is as tall as its content: art, then title, meta, synopsis and the
  // button row. A fixed height left a dead band in the middle of the sheet.
  const float panel_height = std::min(height_ - 100.0f, art_height + 136.0f);
  const Rect panel{(width_ - panel_width) / 2, (height_ - panel_height) / 2, panel_width,
                   panel_height};
  canvas.fill_round_rect(panel, 14, theme::panel());

  const Rect art{panel.x, panel.y, panel.w, art_height};
  canvas.push_clip(panel);
  draw_backdrop_or_fallback(canvas, art, item, 14);
  canvas.fill_gradient_v(Rect{art.x, art.bottom() - art.h * 0.7f, art.w, art.h * 0.7f},
                         rgba(theme::kPanel, 0), theme::panel());
  canvas.pop_clip();

  float x = panel.x + 36;
  float y = art.bottom() - 96;
  const float text_width = panel.w - 72;
  bold_.draw(canvas, bold_.ellipsize(item.title, 36, text_width), x, y, 36, theme::text());
  y += bold_.line_height(36) + 8;
  regular_.draw(canvas, meta_line(item), x, y, theme::kFontSmall, theme::dim());
  y += 30;
  if (!item.synopsis.empty()) {
    regular_.draw_wrapped(canvas, item.synopsis, Rect{x, y, text_width, 80}, theme::kFontSmall,
                          theme::text(), 4);
  } else {
    regular_.draw(canvas, "No synopsis. Drop a JSON file beside the film with the same name to add "
                          "one.",
                  x, y, theme::kFontSmall, theme::faint());
  }
  y = panel.bottom() - 76;
  const Rect play{ x, y, button_width("Play", true), 42 };
  draw_button(canvas, play, "Play", true, Action{Action::Kind::Play, open_item_, ""});
  const bool in_list = std::find(my_list_.begin(), my_list_.end(), item.id) != my_list_.end();
  const std::string list_label = in_list ? "In My List" : "My List";
  draw_button(canvas, Rect{play.right() + 12, y, button_width(list_label, false), 42}, list_label,
              false, Action{Action::Kind::ToggleMyList, open_item_, ""});

  const int percent = percent_for(item);
  if (percent > 0) {
    regular_.draw_right(canvas, std::to_string(percent) + "% watched",
                        Rect{x, y, text_width, 42}, theme::kFontSmall, theme::dim());
  }

  // Close.
  const Rect close{panel.right() - 52, panel.y + 18, 34, 34};
  canvas.fill_circle(close.x + 17, close.y + 17, 17, rgba(0x000000, 150));
  regular_.draw_centered(canvas, "×", close, 20, theme::text());
  add_hit(close, Action{Action::Kind::CloseOverlay, -1, ""});
  // Clicking the scrim also closes.
  add_hit(Rect{0, 0, float(width_), float(height_)}, Action{Action::Kind::CloseOverlay, -1, ""});
  add_hit(panel, Action{});
}

namespace {
Json folder_array(const std::vector<std::string>& folders) {
  Json array = Json::array();
  for (const auto& folder : folders) array.push(Json(folder));
  return array;
}
}  // namespace

bool App::add_library_folder(const std::string& folder, std::string* error) {
  std::string path = folder;
  // Trailing separators are noise and make two spellings of one folder.
  while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
  if (path.empty()) {
    if (error) *error = "Type a folder path first";
    return false;
  }
  if (!dir_exists(path)) {
    if (error) *error = "There is no folder at " + path;
    return false;
  }
  if (std::find(settings_.library_folders.begin(), settings_.library_folders.end(), path) !=
      settings_.library_folders.end()) {
    if (error) *error = path + " is already in the list";
    return false;
  }
  Json patch = Json::object();
  std::vector<std::string> next = settings_.library_folders;
  next.push_back(path);
  patch.set("libraryFolders", folder_array(next));
  return store_.patch_settings(patch, &settings_, error);
}

bool App::remove_library_folder(const std::string& folder, std::string* error) {
  std::vector<std::string> next;
  for (const auto& existing : settings_.library_folders) {
    if (existing != folder) next.push_back(existing);
  }
  Json patch = Json::object();
  patch.set("libraryFolders", folder_array(next));
  return store_.patch_settings(patch, &settings_, error);
}

int App::scan_now() {
  if (settings_.library_folders.empty()) {
    notify(sample_folder_.empty()
               ? "Add a library folder first"
               : "Add a library folder first — the samples are at " + sample_folder_);
    return -1;
  }
  scanning_ = true;
  scan_label_ = "Reading files…";
  if (pump) pump();

  ScanOptions options;
  options.folders = settings_.library_folders;
  options.media_dir = media_dir_;
  options.previous = store_.load_catalog();
  options.render_artwork = true;
  const double started = now_seconds();
  options.on_progress = [&](const ScanProgress& progress) {
    scan_label_ = progress.phase == "read"
                      ? "Reading " + progress.current
                      : "Drawing artwork " + std::to_string(progress.done) + "/" +
                            std::to_string(progress.total);
    if (pump) pump();
  };
  options.on_catalog = [&](const Catalog& catalog) {
    catalog_ = catalog;
    store_.save_catalog(catalog);
    if (pump) pump();
  };
  const Catalog result = scan_library(options);
  catalog_ = result;
  store_.save_catalog(result);
  scanning_ = false;
  scan_label_.clear();
  const double seconds = now_seconds() - started;
  notify("Scanned " + std::to_string(result.items.size()) + " film(s) in " +
         std::to_string(static_cast<int>(seconds + 0.5)) + " s");
  LOG_INFO("ui: scan finished with %zu item(s) in %.2f s", result.items.size(), seconds);
  return static_cast<int>(result.items.size());
}

void App::draw_settings(Canvas& canvas) {
  canvas.fill_rect(Rect{0, 0, float(width_), float(height_)}, rgba(0x000000, 190));
  const float panel_width = std::min(width_ - 160.0f, 760.0f);
  // The panel is as tall as its rows. A once-fixed 560 left a dead band under two
  // toggles; the arithmetic below is the layout, so it has to stay in step with the
  // drawing order underneath it.
  const float folder_rows = 22.0f * static_cast<float>(settings_.library_folders.size());
  const float samples_row =
      (!sample_folder_.empty() && settings_.library_folders.empty()) ? 38.0f : 0.0f;
  const float needed = 84 + 34 + folder_rows + 42 + samples_row + 8 + 34 * 3 + 44 * 2 + 26 + 42 + 34;
  const float panel_height = std::min(static_cast<float>(height_) - 100.0f, needed);
  const Rect panel{(width_ - panel_width) / 2, (height_ - panel_height) / 2, panel_width,
                   panel_height};
  canvas.fill_round_rect(panel, 14, theme::panel());

  bold_.draw(canvas, "Settings", panel.x + 32, panel.y + 28, 24, theme::text());
  const Rect close{panel.right() - 54, panel.y + 22, 32, 32};
  canvas.fill_circle(close.x + 16, close.y + 16, 16, rgba(0xffffff, 18));
  regular_.draw_centered(canvas, "×", close, 20, theme::text());
  add_hit(close, Action{Action::Kind::CloseOverlay, -1, ""});

  float y = panel.y + 84;
  const float row_width = panel.w - 64;
  auto row = [&](const std::string& label, const std::string& value) {
    regular_.draw(canvas, label, panel.x + 32, y, theme::kFontBody, theme::dim());
    regular_.draw_right(canvas, value, Rect{panel.x + 32, y, row_width, 22}, theme::kFontBody,
                        theme::text());
    y += 34;
  };

  row("Library folders", std::to_string(settings_.library_folders.size()) + " configured");
  for (const auto& folder : settings_.library_folders) {
    regular_.draw(canvas, regular_.ellipsize(folder, theme::kFontSmall, row_width - 90),
                  panel.x + 44, y, theme::kFontSmall, theme::faint());
    const Rect remove{panel.right() - 120, y - 3, 76, 20};
    regular_.draw_centered(canvas, "Remove", remove, theme::kFontTiny, theme::dim());
    add_hit(remove, Action{Action::Kind::RemoveFolder, -1, folder});
    y += 22;
  }
  const Rect field{panel.x + 32, y + 2, row_width, 32};
  canvas.fill_round_rect(field, theme::kRadius, rgba(0xffffff, 12));
  canvas.stroke_round_rect(field, theme::kRadius, 1.0f,
                           folder_input_focused_ ? rgba(theme::kAccent, 220) : rgba(0xffffff, 45));
  if (folder_input_.empty() && !folder_input_focused_) {
    regular_.draw(canvas, "Add a folder path, then press Enter", field.x + 12, field.y + 7,
                  theme::kFontSmall, theme::faint());
  } else {
    regular_.draw(canvas, regular_.ellipsize(folder_input_, theme::kFontSmall, field.w - 24),
                  field.x + 12, field.y + 7, theme::kFontSmall, theme::text());
    if (folder_input_focused_) {
      const float caret_x = field.x + 12 + regular_.measure(folder_input_, theme::kFontSmall).width;
      canvas.fill_rect(Rect{caret_x + 1, field.y + 8, 1.5f, 16}, theme::accent());
    }
  }
  add_hit(field, Action{Action::Kind::AddFolder, -1, folder_input_});
  y += 42;
  if (!sample_folder_.empty() && settings_.library_folders.empty()) {
    const Rect link{panel.x + 32, y, button_width("Use the bundled samples", false), 30};
    draw_button(canvas, link, "Use the bundled samples", false,
                Action{Action::Kind::UseSampleFolder, -1, sample_folder_});
    y += 38;
  }
  y += 8;

  row("Playback engine", "libVLC (in this window)");
  row("Films in library", std::to_string(catalog_.items.size()));
  row("With artwork", std::to_string(std::count_if(catalog_.items.begin(), catalog_.items.end(),
                                                   [](const Item& item) {
                                                     return !item.poster.empty();
                                                   })));

  // Two toggles, drawn as switches and hit-tested as rows.
  auto toggle = [&](const std::string& label, bool on, const std::string& key) {
    regular_.draw(canvas, label, panel.x + 32, y + 6, theme::kFontBody, theme::text());
    const Rect track{panel.right() - 104, y, 52, 26};
    canvas.fill_round_rect(track, 13, on ? rgba(theme::kAccent, 220) : rgba(0xffffff, 40));
    canvas.fill_circle(on ? track.right() - 13 : track.x + 13, track.y + 13, 9,
                       on ? rgb(0x1a0d00) : rgb(0xd8d8de));
    add_hit(Rect{panel.x + 24, y - 6, row_width, 40},
            Action{Action::Kind::ToggleSetting, -1, key});
    y += 44;
  };
  toggle("Hardware decoding (for artwork)", settings_.hw_decode, "hwDecode");
  toggle("Audio latency compensation", settings_.auto_latency, "autoLatency");

  y += 6;
  const Rect scan_button{panel.x + 32, panel.bottom() - 78, button_width("Scan library", true), 42};
  draw_button(canvas, scan_button, "Scan library", true, Action{Action::Kind::ScanLibrary, -1, ""});
  regular_.draw(canvas, "Scans the folders above and draws missing artwork.",
                scan_button.right() + 16, scan_button.y + 12, theme::kFontSmall, theme::faint());

  add_hit(Rect{0, 0, float(width_), float(height_)}, Action{Action::Kind::CloseOverlay, -1, ""});
  add_hit(panel, Action{});
}

void App::draw_footer(Canvas& canvas) {
  (void)canvas;
}

void App::draw_toast(Canvas& canvas) {
  if (last_message_.empty() || now_seconds() > message_until_) return;
  const float text_width = regular_.measure(last_message_, theme::kFontBody).width;
  const Rect pill{(static_cast<float>(width_) - std::min(text_width + 56, static_cast<float>(width_) - 80.0f)) / 2,
                  static_cast<float>(height_) - 84,
                  std::min(text_width + 56, static_cast<float>(width_) - 80.0f), 46};
  canvas.fill_round_rect(pill, 23, rgba(0xffffff, 236));
  regular_.draw_centered(canvas, last_message_, pill, theme::kFontBody, rgb(0x15151b));
}

/* ------------------------------- keyboard -------------------------------- */

void App::move_focus(int dx, int dy) {
  if (order_.empty()) return;
  if (focus_item_ < 0) {
    // First arrow press selects the first card rather than jumping somewhere
    // arbitrary.
    focus_item_ = order_.front();
    return;
  }
  std::size_t position = 0;
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (order_[i] == focus_item_) {
      position = i;
      break;
    }
  }
  const int columns = std::max(1, order_columns_);
  const int step = dx != 0 ? dx : dy * columns;
  const long long target = static_cast<long long>(position) + step;
  if (target < 0 || target >= static_cast<long long>(order_.size())) return;
  focus_item_ = order_[static_cast<std::size_t>(target)];
  // Keep keyboard focus visible: scroll the card into view.
  const int row = static_cast<int>(target) / columns;
  const float row_height = order_row_height_ > 0 ? order_row_height_ : kGridWidth / theme::kPosterAspect + 44;
  if (view_ != View::Home) {
    const float visible = static_cast<float>(height_) - theme::kHeaderHeight - 118;
    const float top_of_row = row * row_height;
    if (top_of_row - grid_scroll_ < 0) grid_scroll_ = top_of_row;
    if (top_of_row + row_height - grid_scroll_ > visible + 118) {
      grid_scroll_ = std::max(0.0f, top_of_row + row_height - visible - 100);
    }
  }
}

/* --------------------------------- input -------------------------------- */

void App::ensure_layout() {
  if (!hits_.empty()) return;
  // Input is hit-tested against what was drawn. If a click arrives before the first
  // frame (a very fast user, or a test), lay the screen out into a scratch canvas
  // rather than swallowing the event.
  Canvas scratch(width_, height_);
  draw(scratch);
}

bool App::handle(const InputEvent& event) {
  if (player_open_) {
    // The player's hit areas come from its own drawing pass, so lay it out first if
    // the window has not drawn a frame yet.
    if (hits_.empty()) {
      Canvas scratch(width_, height_);
      draw_player_frame(scratch);
    }
    return handle_player(event);
  }
  ensure_layout();
  switch (event.type) {
    case InputEvent::Type::MouseMove: {
      const Action action = hit_test(event.x, event.y);
      int hover = -1;
      if (action.kind == Action::Kind::OpenDetail) hover = action.index;
      if (hover != hover_item_) {
        hover_item_ = hover;
        set_hover_preview(hover);
        return true;
      }
      return false;
    }
    case InputEvent::Type::MouseDown: {
      const Action action = hit_test(event.x, event.y);
      // A click anywhere but the folder field takes the caret out of it.
      if (action.kind != Action::Kind::AddFolder) folder_input_focused_ = false;
      switch (action.kind) {
        case Action::Kind::None:
          return false;
        case Action::Kind::Navigate:
          view_ = action.value == "browse" ? View::Browse
                  : action.value == "mylist" ? View::MyList
                                             : View::Home;
          search_focused_ = false;
          grid_scroll_ = 0;
          if (view_ != View::Home) home_scroll_ = 0;
          LOG_INFO("ui: navigate to %s", action.value.c_str());
          return true;
        case Action::Kind::OpenDetail:
          open_item_ = action.index;
          settings_open_ = false;
          LOG_INFO("ui: open detail for %s",
                   catalog_.items[static_cast<std::size_t>(action.index)].title.c_str());
          return true;
        case Action::Kind::CloseOverlay:
          open_item_ = -1;
          settings_open_ = false;
          return true;
        case Action::Kind::OpenSettings:
          settings_open_ = true;
          open_item_ = -1;
          return true;
        case Action::Kind::Play: {
          // Play really plays now: the click goes straight into the libVLC engine.
          const int index = action.index >= 0 ? action.index : 0;
          set_hover_preview(-1);
          start_playback(index);
          return true;
        }
        case Action::Kind::ToggleMyList: {
          if (action.index < 0 || action.index >= static_cast<int>(catalog_.items.size())) return false;
          const std::string& id = catalog_.items[static_cast<std::size_t>(action.index)].id;
          auto it = std::find(my_list_.begin(), my_list_.end(), id);
          if (it == my_list_.end()) {
            my_list_.push_back(id);
            notify("Added to My List");
          } else {
            my_list_.erase(it);
            notify("Removed from My List");
          }
          return true;
        }
        case Action::Kind::ToggleSetting: {
          if (action.value == "hwDecode") settings_.hw_decode = !settings_.hw_decode;
          if (action.value == "autoLatency") settings_.auto_latency = !settings_.auto_latency;
          notify(action.value == "hwDecode"
                     ? std::string("Hardware decoding ") + (settings_.hw_decode ? "on" : "off")
                     : std::string("Latency compensation ") + (settings_.auto_latency ? "on" : "off"));
          return true;
        }
        case Action::Kind::FocusSearch:
          search_focused_ = true;
          view_ = View::Search;
          return true;
        case Action::Kind::ClearSearch:
          query_.clear();
          return true;
        case Action::Kind::ScanLibrary: {
          // Synchronous on purpose: one copy of the scan rules, and `pump` keeps the
          // window painting while it runs.
          const int found = scan_now();
          if (found >= 0) settings_open_ = false;
          return true;
        }
        case Action::Kind::AddFolder: {
          folder_input_focused_ = true;
          return true;
        }
        case Action::Kind::RemoveFolder: {
          std::string error;
          if (!remove_library_folder(action.value, &error)) notify(error);
          else notify("Removed " + action.value);
          return true;
        }
        case Action::Kind::UseSampleFolder: {
          std::string error;
          if (!add_library_folder(action.value, &error)) {
            notify(error);
          } else {
            folder_input_.clear();
            folder_input_focused_ = false;
            notify("Added the samples folder");
          }
          return true;
        }
        // The player's own actions only exist once a film is playing; reaching here
        // means the browsing UI drew something it should not have.
        case Action::Kind::Replay:
        case Action::Kind::ClosePlayer:
        case Action::Kind::Seek:
        case Action::Kind::SeekRelative:
        case Action::Kind::TogglePause:
        case Action::Kind::Mute:
        case Action::Kind::Volume:
        case Action::Kind::CycleSubtitles:
        case Action::Kind::NudgeSync:
          return false;
      }
      return false;
    }
    case InputEvent::Type::Wheel: {
      if (open_item_ >= 0 || settings_open_) return false;
      if (view_ == View::Home) {
        home_scroll_ = std::max(0.0f, home_scroll_ - event.wheel * 90.0f);
      } else {
        grid_scroll_ = std::max(0.0f, grid_scroll_ - event.wheel * 90.0f);
      }
      return true;
    }
    case InputEvent::Type::Key: {
      if (event.key == kKeyEscape) {
        if (open_item_ >= 0) {
          open_item_ = -1;
          return true;
        }
        if (settings_open_) {
          settings_open_ = false;
          return true;
        }
        if (search_focused_) {
          search_focused_ = false;
          return true;
        }
        if (focus_item_ >= 0) {
          focus_item_ = -1;
          return true;
        }
        return false;
      }
      if (folder_input_focused_) {
        // Typing a path: Enter commits it, Backspace edits it, Escape leaves it.
        if (event.key == 13 || event.key == 10) {
          std::string error;
          if (!add_library_folder(folder_input_, &error)) {
            notify(error);
          } else {
            notify("Added " + folder_input_);
            folder_input_.clear();
          }
          return true;
        }
        if (event.key == kKeyBackspace && !folder_input_.empty()) {
          std::size_t cut = folder_input_.size() - 1;
          while (cut > 0 && (static_cast<unsigned char>(folder_input_[cut]) & 0xC0) == 0x80) --cut;
          folder_input_.erase(cut);
          return true;
        }
        return false;
      }
      if (event.key == kKeySlash && !search_focused_) {
        search_focused_ = true;
        view_ = View::Search;
        return true;
      }
      if (event.key == kKeyBackspace && search_focused_) {
        if (!query_.empty()) {
          // Remove one UTF-8 code point, not one byte.
          std::size_t cut = query_.size() - 1;
          while (cut > 0 && (static_cast<unsigned char>(query_[cut]) & 0xC0) == 0x80) --cut;
          query_.erase(cut);
          view_ = View::Search;
        }
        return true;
      }
      if (event.key == kKeyLeft) {
        move_focus(-1, 0);
        return true;
      }
      if (event.key == kKeyRight) {
        move_focus(1, 0);
        return true;
      }
      if (event.key == kKeyUp) {
        move_focus(0, -1);
        return true;
      }
      if (event.key == kKeyDown) {
        move_focus(0, 1);
        return true;
      }
      if (event.key == kKeyReturn) {
        if (focus_item_ >= 0 && static_cast<std::size_t>(focus_item_) < catalog_.items.size()) {
          open_item_ = focus_item_;
          settings_open_ = false;
          return true;
        }
        return false;
      }
      return false;
    }
    case InputEvent::Type::Text: {
      if (folder_input_focused_) {
        folder_input_ += event.text;
        return true;
      }
      if (!search_focused_) return false;
      query_ += event.text;
      view_ = View::Search;
      return true;
    }
    case InputEvent::Type::Back: {
      if (open_item_ >= 0) {
        open_item_ = -1;
        return true;
      }
      if (settings_open_) {
        settings_open_ = false;
        return true;
      }
      if (view_ != View::Home) {
        view_ = View::Home;
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

/* ------------------------- headless control ------------------------------ */

bool App::show_screen(const std::string& screen, const std::string& argument) {
  open_item_ = -1;
  settings_open_ = false;
  home_scroll_ = 0;
  grid_scroll_ = 0;
  if (screen == "home") {
    view_ = View::Home;
  } else if (screen == "browse") {
    view_ = View::Browse;
  } else if (screen == "mylist") {
    view_ = View::MyList;
  } else if (screen == "search") {
    view_ = View::Search;
    query_ = argument;
    search_focused_ = true;
  } else if (screen == "detail") {
    view_ = View::Home;
    open_item_ = argument.empty() ? 0 : std::atoi(argument.c_str());
    if (open_item_ >= static_cast<int>(catalog_.items.size())) open_item_ = 0;
  } else if (screen == "settings") {
    view_ = View::Home;
    settings_open_ = true;
  } else {
    LOG_ERROR("unknown screen '%s'", screen.c_str());
    return false;
  }
  return true;
}

bool App::render_to_png(const std::string& path) {
  Canvas canvas(width_, height_);
  draw(canvas);
  const bool ok = write_png(path, canvas);
  if (ok) {
    LOG_INFO("rendered %s at %dx%d", path.c_str(), width_, height_);
  }
  return ok;
}

std::string App::hover_title() const {
  if (hover_item_ < 0 || hover_item_ >= static_cast<int>(catalog_.items.size())) return "";
  return catalog_.items[static_cast<std::size_t>(hover_item_)].title;
}

std::string App::screen_name() const {
  if (open_item_ >= 0) return "detail";
  if (settings_open_) return "settings";
  switch (view_) {
    case View::Home: return "home";
    case View::Browse: return "browse";
    case View::MyList: return "mylist";
    case View::Search: return "search";
  }
  return "home";
}

}  // namespace ui
}  // namespace vf
