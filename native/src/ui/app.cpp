#include "ui/app.h"

#include "library/fs_browse.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

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
constexpr int kKeyHome = 1073741898;
constexpr int kKeyPageUp = 1073741899;
constexpr int kKeyEnd = 1073741901;
constexpr int kKeyPageDown = 1073741902;
// One notch of the wheel, and one page of a keyboard scroll.
constexpr float kWheelStep = 96.0f;
constexpr float kRailStep = 140.0f;
constexpr int kKeyF = 'f';

constexpr float kGridWidth = 216.0f;  // CSS grid minmax(216px, 1fr)

// The web hero omits a runtime under 30 seconds ("0:16" never appears in the meta
// line) and names resolution from the frame height, not the width.
std::string hero_runtime(double seconds) {
  if (!(seconds >= 30.0)) return "";
  const int total = static_cast<int>(seconds / 60.0 + 0.5);
  const int hours = total / 60;
  const int minutes = total % 60;
  if (hours > 0) return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  return std::to_string(std::max(1, minutes)) + "m";
}

std::string resolution_label(int height) {
  if (height >= 2000) return "4K";
  if (height >= 1000) return "1080p";
  if (height >= 700) return "720p";
  if (height > 0) return std::to_string(height) + "p";
  return "";
}

bool plausible_unix_ms(long long ms) {
  // 2000-01-01 through 2100. A negative value is the file-clock bug, not a date.
  return ms > 946684800000LL && ms < 4102444800000LL;
}

std::string long_date(long long ms) {
  if (!plausible_unix_ms(ms)) return "";
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm local {};
  if (!localtime_r(&t, &local)) return "";
  char buf[64];
  if (std::strftime(buf, sizeof buf, "%B %e, %Y", &local) == 0) return "";
  std::string out = buf;
  // %e pads the day with a space. "October  9, 2026" should read "October 9, 2026".
  const std::size_t pad = out.find("  ");
  if (pad != std::string::npos) out.erase(pad, 1);
  return out;
}

std::string format_bytes(long long bytes) {
  if (bytes <= 0) return "";
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buf[32];
  if (unit > 1) std::snprintf(buf, sizeof buf, "%.1f %s", value, units[unit]);
  else std::snprintf(buf, sizeof buf, "%.0f %s", value, units[unit]);
  return buf;
}

std::string clock_label(double seconds) {
  const int total = std::max(0, static_cast<int>(seconds));
  const int hours = total / 3600;
  const int minutes = (total % 3600) / 60;
  const int secs = total % 60;
  char buf[16];
  if (hours > 0) std::snprintf(buf, sizeof buf, "%d:%02d:%02d", hours, minutes, secs);
  else std::snprintf(buf, sizeof buf, "%d:%02d", minutes, secs);
  return buf;
}

std::string scan_clock(long long ms) {
  if (ms <= 0) return "";
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm local {};
  if (!localtime_r(&t, &local)) return "";
  char buf[32];
  if (std::strftime(buf, sizeof buf, "%I:%M:%S %p", &local) == 0) return "";
  std::string out = buf;
  if (!out.empty() && out[0] == '0') out.erase(0, 1);
  return out;
}

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

void App::swallow(const Rect& rect) { hits_.push_back({rect, Action{}}); }

Action App::hit_test(float x, float y) const {
  // Last drawn wins, which is what "on top" means for an immediate-mode UI.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->first.contains(x, y)) return it->second;
  }
  return Action{};
}

/* -------------------------------- scrolling ------------------------------ */

// Everything that can be taller (or wider) than the space it is given declares a
// scroll region while it draws. One name, "page", covers Home, Browse, My List and
// Search because only one of them is on screen at a time; each rail on Home gets
// "rail:<id>" for sideways movement; the two sheets get "detail" and "settings".
//
// This was a real hole, not a nicety: the wheel was ignored whenever the detail or
// settings sheet was open, a rail past the first row of cards could not be reached at
// all, and nothing on screen said whether more content existed. Each region now draws
// its own bar when (and only when) it actually overflows, and the bar is draggable.

float App::scroll_offset(const std::string& region) const {
  const auto it = scrolls_.find(region);
  return it == scrolls_.end() ? 0.0f : it->second.offset;
}

float App::scroll_limit(const std::string& region) const {
  const auto it = scrolls_.find(region);
  return it == scrolls_.end() ? 0.0f : it->second.max;
}

void App::declare_scroll(const std::string& region, float max_offset, const Rect& viewport,
                         bool horizontal) {
  ScrollState& state = scrolls_[region];
  state.max = std::max(0.0f, max_offset);
  state.viewport = viewport;
  state.horizontal = horizontal;
  state.offset = std::clamp(state.offset, 0.0f, state.max);
}

void App::set_scroll(const std::string& region, float value) {
  ScrollState& state = scrolls_[region];
  state.offset = std::clamp(value, 0.0f, std::max(0.0f, state.max));
}

bool App::scroll_by(const std::string& region, float delta) {
  ScrollState& state = scrolls_[region];
  const float before = state.offset;
  state.offset = std::clamp(state.offset + delta, 0.0f, std::max(0.0f, state.max));
  return state.offset != before;
}

const std::string& App::rail_at(float x, float y) const {
  static const std::string none;
  // Last drawn first, so a rail drawn over another one wins, exactly like hit_test.
  for (auto it = rail_bands_.rbegin(); it != rail_bands_.rend(); ++it) {
    if (it->first.contains(x, y)) return it->second;
  }
  return none;
}

std::string App::scroll_region_at(float x, float y) const {
  if (browser_open_) return "browser";
  if (open_item_ >= 0) return "detail";
  if (settings_open_) return "settings";
  const std::string& rail = rail_at(x, y);
  if (!rail.empty() && scroll_limit("rail:" + rail) > 0.5f) return "rail:" + rail;
  return "page";
}

void App::draw_scrollbar(Canvas& canvas, const Rect& track, const std::string& region) {
  ScrollState& state = scrolls_[region];
  if (state.max <= 0.5f) {
    state.thumb = Rect{0, 0, 0, 0};
    return;
  }
  const bool horizontal = state.horizontal;
  const float extent = horizontal ? track.w : track.h;
  const float content = extent + state.max;
  // A thumb never shrinks below 28 px, or a long library turns it into a hairline that
  // cannot be grabbed.
  const float thumb_len = std::min(extent, std::max(28.0f, extent * (extent / content)));
  const float span = extent - thumb_len;
  const float t = state.max > 0 ? std::clamp(state.offset / state.max, 0.0f, 1.0f) : 0.0f;
  const Rect thumb = horizontal ? Rect{track.x + span * t, track.y, thumb_len, track.h}
                                : Rect{track.x, track.y + span * t, track.w, thumb_len};
  state.thumb = thumb;

  const Color rail_color = rgba(0xffffff, 26);
  const Color thumb_color = rgba(0xffffff, 96);
  canvas.fill_round_rect(track, horizontal ? track.h / 2 : track.w / 2, rail_color);
  canvas.fill_round_rect(thumb, horizontal ? thumb.h / 2 : thumb.w / 2, thumb_color);

  // The track either side of the thumb jumps a page. Registering two rects rather
  // than one that contains the thumb keeps "what is drawn is what is clickable"
  // true - a click can only ever land on one action.
  if (horizontal) {
    if (thumb.x > track.x) {
      add_hit(Rect{track.x, track.y, thumb.x - track.x, track.h},
              Action{Action::Kind::ScrollTo, -1, region});
    }
    if (thumb.right() < track.right()) {
      add_hit(Rect{thumb.right(), track.y, track.right() - thumb.right(), track.h},
              Action{Action::Kind::ScrollTo, -1, region});
    }
  } else {
    if (thumb.y > track.y) {
      add_hit(Rect{track.x, track.y, track.w, thumb.y - track.y},
              Action{Action::Kind::ScrollTo, -1, region});
    }
    if (thumb.bottom() < track.bottom()) {
      add_hit(Rect{track.x, thumb.bottom(), track.w, track.bottom() - thumb.bottom()},
              Action{Action::Kind::ScrollTo, -1, region});
    }
  }
  add_hit(thumb, Action{Action::Kind::ScrollThumb, -1, region});
}

void App::reveal_rect(const Rect& rect) {
  if (view_ == View::Home && open_item_ < 0 && !settings_open_) {
    const std::string& rail = rail_at(rect.x + rect.w / 2, rect.y + rect.h / 2);
    if (rail.empty()) return;
    const std::string region = "rail:" + rail;
    const ScrollState& state = scrolls_[region];
    const float left = state.viewport.x + 8;
    const float right = state.viewport.right() - 8;
    if (rect.x < left) set_scroll(region, state.offset - (left - rect.x));
    else if (rect.right() > right) set_scroll(region, state.offset + (rect.right() - right));
    return;
  }
  const std::string region = open_item_ >= 0 ? "detail" : settings_open_ ? "settings" : "page";
  const ScrollState& state = scrolls_[region];
  const float top = state.viewport.y + 8;
  const float bottom = state.viewport.bottom() - 8;
  if (rect.y < top) set_scroll(region, state.offset - (top - rect.y));
  else if (rect.bottom() > bottom) set_scroll(region, state.offset + (rect.bottom() - bottom));
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
  order_rects_.clear();
  rail_bands_.clear();
  // Home is a set of single-row rails, so vertical movement there has nowhere to go;
  // Browse and My List are grids and get their real column count below.
  order_columns_ = 1 << 20;
  hover_card_valid_ = false;
  focus_card_valid_ = false;
  canvas.clear(theme::bg());

  if (view_ == View::Home) {
    draw_home(canvas);
  } else if (view_ == View::Browse) {
    std::vector<std::size_t> all(catalog_.items.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    const std::size_t folders = !settings_.library_folders.empty() ? settings_.library_folders.size()
                                                                  : catalog_.folders.size();
    const std::size_t count = catalog_.items.size();
    draw_grid_view(canvas, "Browse All", all,
                   std::to_string(count) + (count == 1 ? " title" : " titles") + " in " +
                       std::to_string(folders) + " folder(s)");
  } else if (view_ == View::MyList) {
    std::vector<std::size_t> mine;
    for (const auto& id : my_list_) {
      for (std::size_t i = 0; i < catalog_.items.size(); ++i) {
        if (catalog_.items[i].id == id) mine.push_back(i);
      }
    }
    draw_grid_view(canvas, "My List", mine,
                   mine.empty() ? "Titles you save appear here."
                                : std::to_string(mine.size()) +
                                      (mine.size() == 1 ? " saved title" : " saved titles"));
  } else {
    const std::vector<std::size_t> found = search_items(catalog_.items, query_);
    const std::string matches = std::to_string(found.size()) + (found.size() == 1 ? " match" : " matches");
    draw_grid_view(canvas, query_.empty() ? "Search" : "Results for “" + query_ + "”", found,
                   query_.empty() ? "Type to search titles, genres and years." : matches);
  }

  draw_hover_preview(canvas);
  draw_header(canvas);

  if (open_item_ >= 0 && open_item_ < static_cast<int>(catalog_.items.size())) {
    draw_detail(canvas, catalog_.items[static_cast<std::size_t>(open_item_)]);
  }
  if (settings_open_) draw_settings(canvas);
  if (browser_open_) draw_folder_browser(canvas);  // over the settings sheet
  draw_toast(canvas);
}

Rect App::hero_rect(float scroll) const {
  // The web hero is min(88vh, 860px). A very short window cannot honour the 540px
  // CSS floor or the title is pushed off the top, so the floor yields on those.
  const float wanted = std::min(static_cast<float>(height_) * 0.88f, 860.0f);
  const float floor = std::min(540.0f, static_cast<float>(height_) * 0.72f);
  const float hero_height =
      std::clamp(wanted, floor, std::min(860.0f, static_cast<float>(height_) * 0.92f));
  return Rect{0, -scroll, static_cast<float>(width_), hero_height};
}

Rect App::rails_rect(float scroll) const {
  const Rect hero = hero_rect(scroll);
  // The web rails overlap the hero by 96px (margin-top: -96px) so "Recently Added"
  // sits in the fade under the buttons. The overlap is a constant, not a function
  // of the scroll offset: a gap that grows with scroll is the page tearing open.
  const float overlap = std::min(96.0f, hero.h * 0.18f);
  return Rect{0, hero.bottom() - overlap, static_cast<float>(width_),
              static_cast<float>(height_) - (hero.bottom() - overlap)};
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
                           float scroll, bool title_on_art) {
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

  // Bottom scrim keeps the progress bar and, on the home rails, the title legible
  // over a bright still. Landscape cards use the same 62% shade as the web build.
  const float shade = title_on_art ? card.h * (wide ? 0.62f : 0.46f) : 74.0f;
  canvas.push_clip(card);
  canvas.fill_gradient_v(Rect{card.x, card.bottom() - shade, card.w, shade}, rgba(0x06060a, 0),
                         rgba(0x06060a, title_on_art ? 240 : 190));
  canvas.pop_clip();

  if (const int percent = percent_for(item); percent > 0) {
    if (title_on_art && !wide) {
      // Poster grids put a thin progress line on the bottom edge, like the web cards.
      const float bar_height = 3.0f;
      canvas.fill_rect(Rect{card.x, card.bottom() - bar_height, card.w, bar_height}, rgba(0xffffff, 40));
      canvas.fill_rect(Rect{card.x, card.bottom() - bar_height, card.w * percent / 100.0f, bar_height},
                       theme::accent());
    } else {
      const float label_lift = title_on_art ? 34.0f : 0.0f;
      const float bar_height = 4;
      const float bar_y = card.bottom() - bar_height - 8 - label_lift;
      canvas.fill_round_rect(Rect{card.x + 10, bar_y, card.w - 20, bar_height}, bar_height / 2,
                             rgba(0xffffff, 60));
      canvas.fill_round_rect(Rect{card.x + 10, bar_y, (card.w - 20) * percent / 100.0f, bar_height},
                             bar_height / 2, theme::accent());
    }
  }

  if (hovered || focused) {
    // Hover and keyboard focus look the same on purpose: both mean "this is the card
    // that a click or Enter would act on".
    canvas.stroke_round_rect(card, theme::kCardRadius, hovered ? 2.0f : 2.5f,
                             rgba(theme::kAccent, focused ? 255 : 220));
  }

  if (item.unreadable) {
    // A file the prober could not read: say so on the card, so the user is not left
    // wondering why this one cannot be played.
    const std::string badge = "unreadable";
    const float badge_width = regular_.measure(badge, theme::kFontTiny).width + 16;
    const Rect badge_rect{card.x + 8, card.y + 8, badge_width, 20};
    canvas.fill_round_rect(badge_rect, 10, rgba(0xff5a5a, 220));
    regular_.draw_centered(canvas, badge, badge_rect, theme::kFontTiny, rgb(0x1a0505));
  }

  if (title_on_art) {
    // Home cards carry the title and the year/rating/genre line on the still, the
    // same place the web card paints them. A title under the art is the old layout.
    const float title_size = wide ? 13.0f : 14.0f;
    // Poster grids keep the type above the thin progress line on the bottom edge.
    const float title_y = card.bottom() - (wide ? 30.0f : 44.0f);
    bold_.draw(canvas, bold_.ellipsize(item.title, title_size, card.w - 22), card.x + 11, title_y,
               title_size, theme::text());
    std::string year;
    std::string rest;
    if (item.year) year = std::to_string(item.year);
    const std::string runtime = hero_runtime(item.duration);
    if (!runtime.empty()) rest += runtime;
    if (!item.rating.empty()) {
      if (!rest.empty()) rest += "  ·  ";
      rest += item.rating;
    }
    if (!item.genres.empty()) {
      if (!rest.empty()) rest += "  ·  ";
      rest += item.genres.front();
    }
    float meta_x = card.x + 11;
    const float meta_y = title_y + 15.0f;
    if (!year.empty()) {
      regular_.draw(canvas, year, meta_x, meta_y, 10.5f, rgba(0xffffff, 210));
      meta_x += regular_.measure(year, 10.5f).width + 6;
    }
    if (!rest.empty()) {
      regular_.draw(canvas, regular_.ellipsize(rest, 10.5f, card.right() - meta_x - 10), meta_x,
                    meta_y, 10.5f, rgba(0xffffff, 158));
    }
  } else {
    // Title under the art is only for a card that is not using the web overlay.
    const Rect label{card.x, card.bottom() + 8, card.w, 18};
    bold_.draw(canvas, bold_.ellipsize(item.title, theme::kFontSmall, card.w), label.x, label.y,
               theme::kFontSmall, theme::text());
  }

  Action action;
  action.kind = Action::Kind::OpenDetail;
  action.index = static_cast<int>(&item - catalog_.items.data());
  add_hit(card, action);

  // Remember where this card actually is, so the hover can grow it in place. Looking the
  // rectangle up in the hit list afterwards found the wrong thing (the hero has clickable
  // areas for the same film), which drew the enlarged card over the buttons instead of
  // over the poster the pointer is on.
  // A real pointer has to be on this card. A render that forced the hover has no
  // pointer yet, and still needs the card it named.
  if (hovered && (!pointer_known_ || card.contains(pointer_x_, pointer_y_))) {
    hover_card_rect_ = card;
    hover_card_valid_ = true;
  }
  if (focused) {
    focus_card_rect_ = card;
    focus_card_valid_ = true;
  }
}

void App::set_hover_preview(int index) {
  if (index == preview_index_) return;
  preview_index_ = index;
  preview_has_frame_ = false;
  preview_engine_.stop();
  if (index < 0 || static_cast<std::size_t>(index) >= catalog_.items.size()) return;
  const Item& item = catalog_.items[static_cast<std::size_t>(index)];
  // Prefer the generated preview montage (that is what the web build played); fall
  // back to the film itself, from a fifth of the way in, so there is always motion.
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
  // Do not seek here. libVLC answers "no length yet" for the first moments after a file
  // is opened, and a seek into that goes nowhere: on the Windows machine the preview
  // opened, reported "playing", and never produced a picture, so the card fell back to
  // the artwork and looked like the preview was simply broken. The seek is queued and
  // performed by tick() as soon as the demuxer reports the length.
  preview_seek_at_ = item.duration > 10 ? item.duration * 0.2 : -1;
  preview_warned_ = false;
  preview_started_ = clock_now();
  LOG_INFO("preview: playing %s", path.c_str());
}

// Hovering grows the card under the pointer, in place. The earlier version drew a
// separate panel above (or below) the card, which had two problems the user named: the
// panel covered whatever was in that direction, and it was not what the web version did
// - there the selected card itself got bigger and nothing else moved.
void App::draw_hover_card(Canvas& canvas) {
  if (view_ == View::Search && query_.empty()) return;
  if (open_item_ >= 0 || settings_open_ || browser_open_ || player_open_) return;
  // Pointer hover wins. Keyboard focus grows the same way when nothing is hovered,
  // which is what the web card does for :focus-visible.
  int shown = -1;
  const Rect* card = nullptr;
  if (hover_item_ >= 0 && hover_card_valid_) {
    shown = hover_item_;
    card = &hover_card_rect_;
  } else if (focus_item_ >= 0 && focus_card_valid_) {
    shown = focus_item_;
    card = &focus_card_rect_;
  }
  if (card == nullptr || shown < 0 || static_cast<std::size_t>(shown) >= catalog_.items.size()) return;
  const Item& item = catalog_.items[static_cast<std::size_t>(shown)];
  const bool wide = card->w > card->h * 1.2f;  // "Continue Watching" cards are 16:9

  // Grow around the card's own centre, so it looks like the card rising towards the
  // pointer rather than sliding away from it.
  // Home rails grow like the web card (1.34). Poster grids only nudge (1.06).
  const float scale = wide ? 1.34f : 1.06f;
  float w = card->w * scale;
  float h = card->h * scale;
  float x = card->x + card->w / 2 - w / 2;
  float y = card->y + card->h / 2 - h / 2;

  // Keep the grown card on screen and below the header; a hover that leaves the window
  // is no better than the panel this replaces.
  const float top_limit = theme::kHeaderHeight + 8.0f;
  const float bottom_limit = static_cast<float>(height_) - 8.0f;
  x = std::max(static_cast<float>(theme::kGutter) - 12.0f,
               std::min(x, static_cast<float>(width_) - theme::kGutter - w + 12.0f));
  if (y + h > bottom_limit) y = bottom_limit - h;
  if (y < top_limit) y = top_limit;
  h = std::min(h, bottom_limit - y);
  if (w <= 0 || h <= 0) return;

  const Rect grown{x, y, w, h};
  const float radius = theme::kCardRadius + 4;

  // It sits above the rails, so it is drawn after them - and its hit area is registered
  // now, on top, so a click anywhere on the grown card opens the film it shows.
  canvas.fill_round_rect(Rect{grown.x - 3, grown.y + 6, grown.w + 6, grown.h + 6}, radius,
                         rgba(0x000000, 150));
  canvas.fill_round_rect(grown, radius, theme::panel());

  const Rect media = grown;
  canvas.push_clip(Rect{grown.x + 2, grown.y + 2, grown.w - 4, grown.h - 4});
  const Image* still = wide ? backdrop_for(item) : poster_for(item);
  if (preview_has_frame_ && preview_frame_.width > 0 && preview_index_ >= 0 &&
      preview_index_ == hover_item_) {
    // The preview itself, from the film: the montage when the scan made one, otherwise
    // the film playing from a fifth of the way in. This is the motion the web build had.
    canvas.blit_raw(preview_frame_.pixels.data(), preview_frame_.width, preview_frame_.height,
                    media, true);
  } else if (still) {
    canvas.blit(*still, media, false);
    // "Preview starting" is worth saying only while it is true: a card that silently
    // shows a still looks broken on a machine where the preview cannot start.
    if (preview_index_ >= 0 && preview_index_ == hover_item_) {
      const std::string waiting = "preview starting...";
      const float badge_width = regular_.measure(waiting, theme::kFontTiny).width + 18;
      const Rect badge{grown.x + 10, grown.y + 10, badge_width, 20};
      canvas.fill_round_rect(badge, 10, rgba(0x000000, 170));
      regular_.draw_centered(canvas, waiting, badge, theme::kFontTiny, rgba(0xffffff, 210));
    }
  } else {
    canvas.fill_gradient_v(media, theme::poster_fallback(item.title, wide), rgba(theme::kBg, 255));
  }
  canvas.pop_clip();

  // The bottom of the card carries the title, the meta line and the progress, exactly as
  // the web version did when it grew.
  const float text_block = 62.0f;
  canvas.push_clip(grown);
  canvas.fill_gradient_v(Rect{grown.x, grown.bottom() - text_block - 26, grown.w, text_block + 26},
                         rgba(0x000000, 0), rgba(0x000000, 215));
  bold_.draw(canvas, bold_.ellipsize(item.title, theme::kFontBody, grown.w - 24), grown.x + 12,
             grown.bottom() - text_block + 6, theme::kFontBody, theme::text());
  // The web hover hides the meta line and the progress bar, and shows play / add / info.
  const float by = grown.bottom() - text_block - 36;
  auto mini = [&](float x, bool filled, const char* mark, Action action) {
    const Rect box{x, by, 25, 25};
    if (filled) canvas.fill_circle(box.x + 12.5f, box.y + 12.5f, 12.5f, rgb(0xffffff));
    else canvas.stroke_circle(box.x + 12.5f, box.y + 12.5f, 12.0f, 1.5f, rgba(0xffffff, 180));
    if (filled) canvas.fill_triangle_right(Rect{box.x + 9, box.y + 7, 9, 11}, rgb(0x0b0b0f));
    else regular_.draw_centered(canvas, mark, box, 12, theme::text());
    add_hit(box, action);
  };
  canvas.pop_clip();

  add_hit(grown, Action{Action::Kind::OpenDetail, shown, ""});
  mini(grown.x + 12, true, "", Action{Action::Kind::Play, shown, ""});
  mini(grown.x + 43, false, "+", Action{Action::Kind::ToggleMyList, shown, ""});
  mini(grown.x + 74, false, "i", Action{Action::Kind::OpenDetail, shown, ""});
}

void App::draw_hover_preview(Canvas& canvas) { draw_hover_card(canvas); }

void App::draw_header(Canvas& canvas) {
  const float header_height = theme::kHeaderHeight;
  // The web header stays a fade over the hero until the page has moved 26px, then
  // it goes solid and the nav fades in. Other screens always show the nav.
  const bool header_solid = view_ != View::Home || scroll_offset("page") > 26.0f;
  // Nav appears once the home page has moved, the same rule as the web header.
  // A short home page does not invent a scroll just to reveal it.
  const bool reveal_nav = header_solid;
  if (!header_solid) {
    canvas.fill_gradient_v(Rect{0, 0, float(width_), header_height}, rgba(0x000000, 184),
                           rgba(0x000000, 0));
  } else {
    canvas.fill_rect(Rect{0, 0, float(width_), header_height}, rgb(0x0b0b0f));
    canvas.fill_rect(Rect{0, header_height, float(width_), 1}, rgba(0xffffff, 16));
  }

  float x = theme::kGutter;
  const float mid = header_height / 2;
  bold_.draw(canvas, "VLC", x, mid - 11, 21, theme::accent());
  const float vlc_width = bold_.measure("VLC", 21).width;
  bold_.draw(canvas, "FLIX", x + vlc_width + 1, mid - 11, 21, theme::text());
  x += vlc_width + bold_.measure("FLIX", 21).width + 36;

  if (reveal_nav) {
    struct NavEntry {
      const char* label;
      bool selected;
      Action action;
    };
    const NavEntry nav[] = {
        {"Home", view_ == View::Home, Action{Action::Kind::Navigate, -1, "home"}},
        {"Browse All", view_ == View::Browse, Action{Action::Kind::Navigate, -1, "browse"}},
        {"My List", view_ == View::MyList, Action{Action::Kind::Navigate, -1, "mylist"}},
    };
    for (const auto& entry : nav) {
      const float text_width = regular_.measure(entry.label, 13.5f).width;
      const Rect area{x, mid - 11, text_width + 8, 22};
      regular_.draw(canvas, entry.label, x, mid - 8, 13.5f,
                    entry.selected ? theme::text() : theme::dim());
      add_hit(area, entry.action);
      x += text_width + 22;
    }
  }

  // Right-hand cluster: search icon, VLC pill, gear. The search field only opens
  // once the icon is used, which is what the web header does.
  float right = static_cast<float>(width_) - theme::kGutter;
  const Rect gear{right - 32, mid - 16, 32, 32};
  const float gear_cx = gear.x + 16;
  const float gear_cy = mid;
  canvas.stroke_circle(gear_cx, gear_cy, 6.5f, 1.5f, theme::text());
  canvas.fill_circle(gear_cx, gear_cy, 2.1f, theme::text());
  for (int tooth = 0; tooth < 8; ++tooth) {
    const float angle = tooth * 3.1415926f / 4.0f;
    canvas.fill_rect(Rect{gear_cx + std::cos(angle) * 8.4f - 1.3f,
                          gear_cy + std::sin(angle) * 8.4f - 1.3f, 2.6f, 2.6f},
                     theme::text());
  }
  add_hit(gear, Action{Action::Kind::OpenSettings, -1, ""});
  right = gear.x - 12;

  const std::string pill = "VLC";
  const float pill_w = regular_.measure(pill, 12).width + 28;
  const Rect pill_rect{right - pill_w, mid - 13, pill_w, 26};
  canvas.fill_round_rect(pill_rect, 13, rgba(0xffffff, 22));
  canvas.stroke_round_rect(pill_rect, 13, 1, rgba(0xffffff, 36));
  canvas.fill_circle(pill_rect.x + 11, mid, 3.2f, theme::accent());
  regular_.draw(canvas, pill, pill_rect.x + 18, mid - 7, 12, theme::text());
  right = pill_rect.x - 12;

  const bool show_field = search_focused_ || view_ == View::Search;
  const float field_w = show_field ? std::min(220.0f, std::max(96.0f, width_ * 0.18f)) : 0.0f;
  const Rect search_icon{right - 32, mid - 16, 32, 32};
  if (show_field) {
    const Rect field{search_icon.x - 6 - field_w, mid - 16, field_w, 32};
    canvas.fill_round_rect(field, 4, rgba(0x0c0c10, 240));
    canvas.stroke_round_rect(field, 4, 1, rgba(0xffffff, search_focused_ ? 70 : 28));
    const std::string shown = query_.empty() ? "Titles, genres, years" : query_;
    regular_.draw(canvas, regular_.ellipsize(shown, theme::kFontSmall, field.w - 18), field.x + 10,
                  mid - 8, theme::kFontSmall, query_.empty() ? theme::faint() : theme::text());
    if (search_focused_ && query_.empty()) {
      canvas.fill_rect(Rect{field.x + 10, mid - 9, 1.5f, 18}, theme::accent());
    }
    add_hit(field, Action{Action::Kind::FocusSearch, -1, ""});
  }
  const float mag_x = search_icon.x + 13;
  const float mag_y = mid - 1;
  canvas.stroke_circle(mag_x, mag_y, 6.2f, 1.6f, theme::text());
  for (int i = 0; i < 5; ++i) {
    canvas.fill_rect(Rect{mag_x + 4.2f + i * 0.9f, mag_y + 4.2f + i * 0.9f, 1.7f, 1.7f}, theme::text());
  }
  add_hit(search_icon, Action{Action::Kind::FocusSearch, -1, ""});
}

void App::draw_home(Canvas& canvas) {
  const float page = scroll_offset("page");
  const Rect hero = hero_rect(page);
  const Item* featured = nullptr;
  if (!catalog_.items.empty()) {
    // The hero is the most recently added film, which is what the CSS version did.
    featured = &catalog_.items.front();
  }

  if (featured) {
    const float visible_h = hero.bottom() > 0 ? std::min(hero.bottom(), hero.h) : 0.0f;
    if (visible_h > 0) {
      canvas.push_clip(Rect{0, std::max(0.0f, hero.y), float(width_), visible_h});
    }
    const Rect full{0, hero.y, float(width_), hero.h};
    draw_backdrop_or_fallback(canvas, full, *featured, 0);
    // Left scrim for the title, bottom fade so the rails emerge from the still
    // instead of sitting under an opaque band.
    canvas.fill_gradient_h(Rect{0, hero.y, width_ * 0.72f, hero.h}, rgba(0x000000, 220),
                           rgba(0x000000, 0));
    canvas.fill_gradient_v(Rect{0, hero.y + hero.h * 0.42f, float(width_), hero.h * 0.58f},
                           rgba(theme::kBg, 0), rgba(theme::kBg, 250));

    const float text_x = theme::kGutter;
    const float text_width = std::min(540.0f, width_ * 0.46f);
    const float title_size = std::clamp(width_ * 0.05f, 40.0f, 70.4f);
    const float btn_h = 42.0f;
    // Buttons end just above the first rail title, which overlaps the hero fade.
    const float btn_y = rails_rect(page).y - 16.0f - btn_h;
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    // A real added-date is Unix milliseconds. file_clock on this toolchain is not
    // that, so a scan can store a negative mtime; fall back to the scan time rather
    // than treating a garbage clock as "added in the 1800s".
    const auto plausible_ms = [](long long ms) {
      return ms > 946684800000LL && ms < 4102444800000LL;
    };
    long long added = featured->added_at;
    if (!plausible_ms(added)) added = catalog_.scanned_at;
    const bool is_new =
        plausible_ms(added) && (now_ms - added) < 14LL * 24 * 60 * 60 * 1000;
    const std::string flag = is_new ? "ADDED TO YOUR LIBRARY" : "FEATURED IN YOUR LIBRARY";
    const float synopsis_h = 52.0f;
    const float synopsis_y = btn_y - 22.0f - synopsis_h;
    const float meta_y = synopsis_y - 28.0f;
    const float title_y = meta_y - 14.0f - bold_.line_height(title_size);
    const float flag_y = title_y - 22.0f;

    canvas.fill_rect(Rect{text_x, flag_y + 8, 16, 2.4f}, theme::accent());
    bold_.draw(canvas, flag, text_x + 24, flag_y, 12, theme::accent());
    const float title_max = std::max(text_width, static_cast<float>(width_) - text_x - 64.0f);
    bold_.draw(canvas, bold_.ellipsize(featured->title, title_size, title_max), text_x, title_y,
               title_size, theme::text());

    float meta_x = text_x;
    if (!featured->rating.empty()) {
      const float chip_w = regular_.measure(featured->rating, 11).width + 12;
      const Rect chip{meta_x, meta_y, chip_w, 18};
      canvas.stroke_round_rect(chip, 2, 1, rgba(0xffffff, 107));
      regular_.draw_centered(canvas, featured->rating, chip, 11, rgb(0xf0f0f2));
      meta_x = chip.right() + 8;
    }
    if (is_new) {
      const float badge_w = bold_.measure("NEW", 11).width + 12;
      const Rect badge{meta_x, meta_y, badge_w, 18};
      canvas.fill_round_rect(badge, 2, theme::accent());
      bold_.draw_centered(canvas, "NEW", badge, 11, rgb(0x140800));
      meta_x = badge.right() + 10;
    }
    std::string meta;
    auto add_meta = [&](const std::string& bit) {
      if (bit.empty()) return;
      if (!meta.empty()) meta += "   ·   ";
      meta += bit;
    };
    if (featured->year) add_meta(std::to_string(featured->year));
    add_meta(hero_runtime(featured->duration));
    add_meta(resolution_label(featured->height));
    if (!featured->genres.empty()) {
      std::string genres;
      for (std::size_t i = 0; i < featured->genres.size() && i < 3; ++i) {
        if (i) genres += " · ";
        genres += featured->genres[i];
      }
      add_meta(genres);
    }
    if (!meta.empty()) {
      regular_.draw(canvas, regular_.ellipsize(meta, 13.5f, text_width), meta_x, meta_y + 1, 13.5f,
                    rgb(0xe2e2e6));
    }

    const std::string synopsis = !featured->synopsis.empty()
                                     ? featured->synopsis
                                     : featured->filename;
    if (!synopsis.empty()) {
      regular_.draw_wrapped(canvas, synopsis, Rect{text_x, synopsis_y, text_width, synopsis_h + 8},
                            15.5f, rgb(0xd8d8de), 3);
    }

    const int watched = percent_for(*featured);
    const std::string play_label = watched > 0 && watched < 95 ? "Resume" : "Play";
    const float play_w = 34.0f + bold_.measure(play_label, 15.5f).width + 22.0f;
    const float info_w = 34.0f + bold_.measure("More Info", 15.5f).width + 22.0f;
    const Rect play_rect{text_x, btn_y, play_w, btn_h};
    const Rect info_rect{play_rect.right() + 12, btn_y, info_w, btn_h};
    const Rect plus_rect{info_rect.right() + 12, btn_y - 2, 46, 46};
    const Color ink = rgb(0x0b0b0f);

    canvas.fill_round_rect(play_rect, 4, rgb(0xffffff));
    draw_triangle_right(canvas, Rect{play_rect.x + 16, play_rect.y + btn_h / 2 - 6, 10, 12}, ink);
    bold_.draw(canvas, play_label, play_rect.x + 34,
               play_rect.y + (btn_h - bold_.line_height(15.5f)) / 2, 15.5f, ink);
    add_hit(play_rect, Action{Action::Kind::Play, 0, ""});

    canvas.fill_round_rect(info_rect, 4, rgba(0x6d6d6e, 158));
    const float info_cx = info_rect.x + 20;
    const float info_cy = info_rect.y + btn_h / 2;
    canvas.stroke_circle(info_cx, info_cy, 8, 1.5f, theme::text());
    canvas.fill_circle(info_cx, info_cy - 3.2f, 1.2f, theme::text());
    canvas.fill_rect(Rect{info_cx - 1.0f, info_cy - 0.4f, 2.0f, 6.0f}, theme::text());
    bold_.draw(canvas, "More Info", info_rect.x + 34,
               info_rect.y + (btn_h - bold_.line_height(15.5f)) / 2, 15.5f, theme::text());
    add_hit(info_rect, Action{Action::Kind::OpenDetail, 0, ""});

    canvas.stroke_circle(plus_rect.x + 23, plus_rect.y + 23, 16, 1.7f, rgba(0xffffff, 210));
    canvas.fill_rect(Rect{plus_rect.x + 16, plus_rect.y + 22, 14, 2.2f}, theme::text());
    canvas.fill_rect(Rect{plus_rect.x + 21.9f, plus_rect.y + 16, 2.2f, 14}, theme::text());
    add_hit(plus_rect, Action{Action::Kind::ToggleMyList, 0, ""});

    if (visible_h > 0) canvas.pop_clip();
  } else {
    // The web empty state is centred type, not a panel. Copy follows whether a
    // folder has been chosen yet.
    const float top = theme::kHeaderHeight + 90.0f - page;
    const char* title = "Your shelf is empty";
    const float title_size = 26.0f;
    const float title_w = bold_.measure(title, title_size).width;
    bold_.draw(canvas, title, (static_cast<float>(width_) - title_w) / 2.0f, top, title_size,
               theme::text());
    const bool have_folders = !settings_.library_folders.empty();
    const char* body = have_folders
                           ? "The folders you selected contain no video files yet — or the last scan has not "
                             "run. Try scanning again."
                           : "Point VLCFlix at a folder of videos. It reads the files, generates artwork "
                             "with ffmpeg, and lays everything out like a streaming service.";
    const float body_w = std::min(560.0f, static_cast<float>(width_) - 2 * theme::kGutter);
    regular_.draw_wrapped(canvas, body,
                          Rect{(static_cast<float>(width_) - body_w) / 2.0f, top + 46, body_w, 72}, 14.5f,
                          theme::dim(), 3);
    const float btn_h = 42.0f;
    const float choose_w = button_width("Choose library folders", true, false);
    const float demo_w = button_width("Load the demo library", false, false);
    const float gap = 12.0f;
    float bx = (static_cast<float>(width_) - (choose_w + gap + demo_w)) / 2.0f;
    const float by = top + 132.0f;
    draw_button(canvas, Rect{bx, by, choose_w, btn_h}, "Choose library folders", true,
                Action{Action::Kind::OpenSettings, -1, ""}, false);
    const Rect demo{bx + choose_w + gap, by, demo_w, btn_h};
    canvas.stroke_round_rect(demo, 6, 1.0f, rgba(0xffffff, 82));
    bold_.draw_centered(canvas, "Load the demo library", demo, 14.5f, theme::text());
    add_hit(demo, Action{Action::Kind::UseSampleFolder, -1, sample_folder_});
  }

  /* --------------------------------- rails -------------------------------- */
  // Home rails are the web's 16:9 cards (246px, 7px gap), title on the still.
  // Continue Watching used to be the only wide row; every home row is wide now.
  const std::vector<Rail> rails = build_rails(catalog_.items, progress_, my_list_);
  float y = rails_rect(page).y;
  if (catalog_.items.empty()) {
    // The empty copy is drawn in the hero area. On a short window the hero ends
    // before those buttons do, and the footer was cutting through them.
    y = std::max(y, theme::kHeaderHeight + 90.0f + 132.0f + 42.0f + 28.0f - page);
  }
  const Rect page_view{0, static_cast<float>(theme::kHeaderHeight), static_cast<float>(width_),
                       static_cast<float>(height_) - theme::kHeaderHeight};
  canvas.push_clip(Rect{0, theme::kHeaderHeight, float(width_), float(height_) - theme::kHeaderHeight});

  const float card_width = 246.0f;
  const float card_height = card_width / theme::kBackdropAspect;
  const float card_gap = 7.0f;
  const float title_size = 21.0f;
  for (const auto& rail : rails) {
    const std::string region = "rail:" + rail.id;
    const float title_block = bold_.line_height(title_size) + 12.0f;
    if (y > -40.0f && y < height_) {
      bold_.draw(canvas, rail.title, theme::kGutter, y, title_size, theme::text());
    }

    const float cards_y = y + title_block;
    const float rail_view_width = static_cast<float>(width_) - 2 * theme::kGutter;
    const float content_width = rail.items.empty()
                                    ? 0.0f
                                    : static_cast<float>(rail.items.size()) * (card_width + card_gap) -
                                          card_gap;
    const Rect rail_view{theme::kGutter, cards_y, rail_view_width, card_height};
    declare_scroll(region, content_width - rail_view_width, rail_view, true);

    // A rail scrolls sideways. Before this it simply stopped drawing at the right
    // edge, so a library of more than about eight films per row had films in it that
    // could not be reached by any input at all.
    const float rail_offset = scroll_offset(region);
    if (cards_y > -card_height && cards_y < height_) {
      rail_bands_.push_back({Rect{0, cards_y, static_cast<float>(width_), card_height}, rail.id});
    }

    float x = theme::kGutter - rail_offset;
    for (std::size_t index : rail.items) {
      const Rect card{x, cards_y, card_width, card_height};
      if (card.x > width_) break;
      if (card.right() > 0) {
        draw_poster_card(canvas, card, catalog_.items[index], true, page, true);
        order_.push_back(static_cast<int>(index));
        order_rects_.push_back(card);
      }
      x += card_width + card_gap;
    }
    y = cards_y + card_height + 34.0f;
  }

  // Footer scrolls with the page, the same place the web build puts it. Advance y
  // even when the footer is below the fold, or the scroll limit forgets it exists.
  y += 8.0f;
  const float footer_h = 148.0f;
  if (y < height_ && y + footer_h > theme::kHeaderHeight) {
    canvas.fill_rect(Rect{0, y, float(width_), 1}, rgba(0xffffff, 18));
    float fy = y + 28.0f;
    bold_.draw(canvas, "VLCFLIX", theme::kGutter, fy, 16, theme::text());
    fy += 28.0f;
    regular_.draw_wrapped(
        canvas,
        "Browsing front-end for your own media. Playback is handled by VLC through its HTTP control "
        "interface, or by the built-in player.",
        Rect{theme::kGutter, fy, std::min(620.0f, static_cast<float>(width_) - 2 * theme::kGutter), 52},
        12.5f, theme::faint(), 3);
    fy += 56.0f;
    const std::size_t count = catalog_.items.size();
    std::string meta = std::to_string(count) + (count == 1 ? " title in library" : " titles in library");
    meta += catalog_.have_ffmpeg ? "  ·  ffmpeg artwork: on" : "  ·  ffmpeg artwork: off";
    std::string vlc = settings_.vlc_path;
    if (vlc.empty() && file_exists("/usr/bin/vlc")) vlc = "/usr/bin/vlc";
    meta += vlc.empty() ? "  ·  VLC: not detected" : "  ·  VLC: " + vlc;
    const std::string clock = scan_clock(catalog_.scanned_at);
    if (!clock.empty()) meta += "  ·  last scan " + clock;
    regular_.draw(canvas,
                  regular_.ellipsize(meta, 11.5f, static_cast<float>(width_) - 2 * theme::kGutter),
                  theme::kGutter, fy, 11.5f, rgb(0x56565f));
  }
  y += footer_h;
  canvas.pop_clip();

  // Measure the unscrolled stack. `y` was advanced from the already-scrolled origin,
  // so adding the offset back is required: subtracting rails_rect(0) from the
  // scrolled `y` makes the limit shrink by whatever you have already scrolled, and
  // the page sticks after one notch with the last rail still clipped.
  const float content_bottom = y + page;
  const float rails_top = rails_rect(0).y;
  const float content_height = content_bottom - rails_top;
  const float visible = static_cast<float>(height_) - rails_top;
  const float max_scroll = std::max(0.0f, content_height - visible + 8.0f);
  declare_scroll("page", max_scroll, page_view, false);
  // The web home has no page bar (the wheel is the control). A bar here sat on top
  // of the last card in the row. The region is still declared, so the wheel, Page
  // Down and End keep working.

  if (rails.empty() && featured) {
    regular_.draw(canvas, "Nothing in progress yet — press Play to start a film.",
                  theme::kGutter, rails_rect(scroll_offset("page")).y, theme::kFontSmall,
                  theme::faint());
  }
}

void App::draw_grid_view(Canvas& canvas, const std::string& title,
                         const std::vector<std::size_t>& items, const std::string& subtitle) {
  const float top = theme::kHeaderHeight + 28;
  const float title_size = std::clamp(static_cast<float>(width_) * 0.026f, 27.0f, 37.0f);
  bold_.draw(canvas, title, theme::kGutter, top, title_size, theme::text());
  regular_.draw(canvas, subtitle, theme::kGutter, top + 44, theme::kFontSmall, theme::dim());

  const float grid_top = top + 84;
  const float page_offset = scroll_offset("page");
  canvas.push_clip(Rect{0, grid_top - 8, float(width_), float(height_) - grid_top + 8});

  // Auto-fill, like the CSS grid this replaces: as many columns of at least
  // kGridWidth as fit, then the extra space is shared out so the right margin matches
  // the left one instead of leaving a ragged gap.
  const float available = static_cast<float>(width_) - 2 * theme::kGutter;
  const float gap_x = 12.0f;
  const float gap_y = 18.0f;
  const int columns = std::max(1, static_cast<int>((available + gap_x) / (kGridWidth + gap_x)));
  const float card_width = (available - gap_x * (columns - 1)) / columns;
  const float card_height = card_width / theme::kPosterAspect;
  // The title sits on the art now, so the row is the card plus the CSS row gap,
  // not a second line of type under the poster.
  const float row_height = card_height + gap_y;

  order_columns_ = columns;
  order_row_height_ = row_height;
  for (std::size_t i = 0; i < items.size(); ++i) {
    order_.push_back(static_cast<int>(items[i]));
    const int column = static_cast<int>(i) % columns;
    const int row = static_cast<int>(i) / columns;
    const Rect card{theme::kGutter + column * (card_width + gap_x),
                    grid_top + row * row_height - page_offset, card_width, card_height};
    if (card.bottom() < grid_top - 40 || card.y > height_) continue;
    draw_poster_card(canvas, card, catalog_.items[items[i]], false, page_offset, true);
    order_rects_.push_back(card);
  }

  const int rows_total = columns > 0 ? static_cast<int>((items.size() + columns - 1) / columns) : 0;
  const float footer_top = grid_top + static_cast<float>(rows_total) * row_height + 28.0f - page_offset;
  if (footer_top < height_ && footer_top + 120.0f > grid_top) {
    canvas.fill_rect(Rect{0, footer_top, float(width_), 1}, rgba(0xffffff, 18));
    bold_.draw(canvas, "VLCFLIX", theme::kGutter, footer_top + 28, 16, theme::text());
    regular_.draw_wrapped(
        canvas,
        "Browsing front-end for your own media. Playback is handled by VLC through its HTTP control "
        "interface, or by the built-in player.",
        Rect{theme::kGutter, footer_top + 52, std::min(620.0f, static_cast<float>(width_) - 2 * theme::kGutter),
             48},
        12.5f, theme::faint(), 3);
    const std::size_t count = catalog_.items.size();
    std::string meta = std::to_string(count) + (count == 1 ? " title in library" : " titles in library");
    meta += catalog_.have_ffmpeg ? "  ·  ffmpeg artwork: on" : "  ·  ffmpeg artwork: off";
    std::string vlc = settings_.vlc_path;
    if (vlc.empty() && file_exists("/usr/bin/vlc")) vlc = "/usr/bin/vlc";
    meta += vlc.empty() ? "  ·  VLC: not detected" : "  ·  VLC: " + vlc;
    const std::string clock = scan_clock(catalog_.scanned_at);
    if (!clock.empty()) meta += "  ·  last scan " + clock;
    regular_.draw(canvas, regular_.ellipsize(meta, 11.5f, static_cast<float>(width_) - 2 * theme::kGutter),
                  theme::kGutter, footer_top + 108, 11.5f, rgb(0x56565f));
  }
  canvas.pop_clip();

  const float footer_h = 140.0f;
  const float content = static_cast<float>(rows_total) * row_height + 28.0f + footer_h;
  const float max_scroll = std::max(0.0f, content - (static_cast<float>(height_) - grid_top) + 8.0f);
  const Rect page_view{0, grid_top, static_cast<float>(width_), static_cast<float>(height_) - grid_top};
  declare_scroll("page", max_scroll, page_view, false);
  // The bar is drawn outside the clip so it does not scroll with the cards.
  draw_scrollbar(canvas, Rect{static_cast<float>(width_) - 9, grid_top + 6, 4, page_view.h - 18},
                 "page");

  if (items.empty()) {
    const char* empty = "No films here.";
    if (view_ == View::MyList) empty = "Nothing saved yet — hover a card and press +.";
    else if (view_ == View::Search && !query_.empty()) empty = "No titles matched. Try a different word.";
    else if (view_ == View::Search) empty = "";
    if (empty[0] != '\0') {
      regular_.draw(canvas, empty, theme::kGutter, grid_top, theme::kFontBody, theme::faint());
    }
  }
}

void App::draw_detail(Canvas& canvas, const Item& item) {
  canvas.fill_rect(Rect{0, 0, float(width_), float(height_)}, rgba(0x050508, 209));

  // The web sheet is min(940px, the window), hero 16:8.2, then a two-column body.
  // Play in browser is not drawn: that path was removed from the native app.
  const float panel_width = std::min(static_cast<float>(width_) - 48.0f, 940.0f);
  const float art_height = panel_width * 8.2f / 16.0f;
  const float pad_x = 28.0f;
  const float gap = 34.0f;
  const float inner = panel_width - pad_x * 2;
  const float left_w = inner * 1.7f / 2.7f;
  const float right_x_off = pad_x + left_w + gap;
  const float right_w = panel_width - right_x_off - pad_x;
  const float synopsis_size = 14.5f;
  const float synopsis_line = regular_.line_height(synopsis_size);
  const std::string synopsis =
      item.synopsis.empty()
          ? "No synopsis. Add a sidecar JSON file next to the video with a \"synopsis\" field to "
            "describe it."
          : item.synopsis;
  const std::vector<std::string> synopsis_lines = regular_.wrap(synopsis, synopsis_size, left_w);
  const float synopsis_height = std::max(synopsis_line, static_cast<float>(synopsis_lines.size()) * synopsis_line);

  // Body is the taller of the two columns, counted from the rows that will
  // actually be drawn. A fixed allowance here made a sheet that fitted claim a
  // scrollbar. Measured unscrolled, so the limit does not shrink after one wheel.
  long long added_ms = item.added_at;
  if (!plausible_unix_ms(added_ms)) added_ms = catalog_.scanned_at;
  const std::string added_label = long_date(added_ms);
  int info_rows = 0;
  if (!item.genres.empty()) ++info_rows;
  if (!added_label.empty()) ++info_rows;
  if (item.size > 0) ++info_rows;
  if (item.width > 0 && item.height > 0) ++info_rows;
  const float left_body = 26.0f + 22.0f + 16.0f + synopsis_height +
                          (item.genres.empty() ? 0.0f : 18.0f + synopsis_line) + 28.0f;
  float right_body = 26.0f + static_cast<float>(info_rows) * 40.0f + 28.0f;
  if (!item.filename.empty()) right_body += 24.0f;
  const float body_height = std::max(left_body, right_body);
  const float content_height = art_height + body_height;
  const float max_panel = std::max(180.0f, static_cast<float>(height_) - 40.0f);
  const float panel_height = std::min(content_height, max_panel);
  const float panel_y = content_height <= max_panel
                            ? (static_cast<float>(height_) - panel_height) / 2.0f
                            : 36.0f;
  const Rect panel{(static_cast<float>(width_) - panel_width) / 2.0f, panel_y, panel_width, panel_height};
  canvas.fill_round_rect(panel, 8, theme::panel());
  // Scrim first, then the panel swallow, then the controls. Hit testing takes the
  // last match, so this order is what keeps Play clickable and a click on the
  // sheet body from closing it.
  add_hit(Rect{0, 0, float(width_), float(height_)}, Action{Action::Kind::CloseOverlay, -1, ""});
  swallow(panel);

  const Rect viewport{panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2};
  declare_scroll("detail", std::max(0.0f, content_height - panel_height), viewport, false);
  const float scroll = scroll_offset("detail");

  canvas.push_clip(viewport);
  const Rect art{panel.x, panel.y - scroll, panel.w, art_height};
  draw_backdrop_or_fallback(canvas, art, item, 8);
  // Square off the art's bottom corners so the body meets it, and fade into the panel.
  canvas.fill_rect(Rect{art.x, art.bottom() - 12, art.w, 12}, theme::panel());
  canvas.fill_gradient_v(Rect{art.x, art.y + art.h * 0.34f, art.w, art.h * 0.66f},
                         rgba(theme::kPanel, 0), theme::panel());

  const float title_size = std::clamp(panel_width * 0.044f, 27.0f, 42.0f);
  const float btn_h = 42.0f;
  const float btn_y = art.bottom() - 22.0f - btn_h;
  const float title_y = btn_y - 16.0f - bold_.line_height(title_size);
  const float text_x = panel.x + pad_x;
  bold_.draw(canvas, bold_.ellipsize(item.title, title_size, panel.w - 120), text_x, title_y,
             title_size, theme::text());

  const int percent = percent_for(item);
  const std::string play_label =
      percent > 0 ? "Resume from " + clock_label(progress_.count(item.id) ? progress_.at(item.id).position : 0)
                  : "Play";
  const float play_w = 34.0f + bold_.measure(play_label, 15.5f).width + 22.0f;
  const Rect play{text_x, btn_y, play_w, btn_h};
  canvas.fill_round_rect(play, 4, rgb(0xffffff));
  draw_triangle_right(canvas, Rect{play.x + 16, play.y + btn_h / 2 - 6, 10, 12}, rgb(0x0b0b0f));
  bold_.draw(canvas, play_label, play.x + 34, play.y + (btn_h - bold_.line_height(15.5f)) / 2, 15.5f,
             rgb(0x0b0b0f));
  add_hit(play, Action{Action::Kind::Play, open_item_, ""});

  const bool in_list = std::find(my_list_.begin(), my_list_.end(), item.id) != my_list_.end();
  const Rect plus{play.right() + 10, btn_y - 2, 46, 46};
  canvas.stroke_circle(plus.x + 23, plus.y + 23, 16, 1.7f, rgba(0xffffff, in_list ? 255 : 210));
  if (in_list) {
    canvas.fill_circle(plus.x + 23, plus.y + 23, 14, rgba(theme::kAccent, 230));
  }
  canvas.fill_rect(Rect{plus.x + 16, plus.y + 22, 14, 2.2f}, in_list ? rgb(0x140800) : theme::text());
  canvas.fill_rect(Rect{plus.x + 21.9f, plus.y + 16, 2.2f, 14}, in_list ? rgb(0x140800) : theme::text());
  add_hit(plus, Action{Action::Kind::ToggleMyList, open_item_, ""});

  // Two columns under the hero. Left is the synopsis. Right is the file facts.
  const float body_y = art.bottom() + 26.0f;
  float meta_x = text_x;
  const float meta_y = body_y;
  if (percent > 0) {
    const std::string watched = std::to_string(percent) + "% watched";
    regular_.draw(canvas, watched, meta_x, meta_y, 13, rgb(0x46d369));
    meta_x += regular_.measure(watched, 13).width + 10;
  }
  const std::string runtime = hero_runtime(item.duration);
  if (!runtime.empty()) {
    regular_.draw(canvas, runtime, meta_x, meta_y, 13, rgb(0xd5d5db));
    meta_x += regular_.measure(runtime, 13).width + 10;
  }
  if (item.year) {
    const std::string year = std::to_string(item.year);
    regular_.draw(canvas, year, meta_x, meta_y, 13, rgb(0xd5d5db));
    meta_x += regular_.measure(year, 13).width + 10;
  }
  if (!item.rating.empty()) {
    const float chip_w = regular_.measure(item.rating, 11).width + 12;
    const Rect chip{meta_x, meta_y - 1, chip_w, 18};
    canvas.stroke_round_rect(chip, 2, 1, rgba(0xffffff, 107));
    regular_.draw_centered(canvas, item.rating, chip, 11, rgb(0xf0f0f2));
    meta_x = chip.right() + 10;
  }
  const std::string res = resolution_label(item.height);
  if (!res.empty()) {
    regular_.draw(canvas, res, meta_x, meta_y, 13, rgb(0xd5d5db));
    meta_x += regular_.measure(res, 13).width + 10;
  }
  regular_.draw(canvas, "HD", meta_x, meta_y, 13, rgb(0xd5d5db));

  regular_.draw_wrapped(canvas, synopsis,
                        Rect{text_x, meta_y + 22.0f + 16.0f, left_w, synopsis_height + synopsis_line},
                        synopsis_size, rgb(0xd2d2d9));
  if (!item.genres.empty()) {
    const float genres_y = meta_y + 22.0f + 16.0f + synopsis_height + 18.0f;
    const std::string label = "Genres: ";
    regular_.draw(canvas, label, text_x, genres_y, synopsis_size, theme::faint());
    std::string genres;
    for (std::size_t i = 0; i < item.genres.size(); ++i) {
      if (i) genres += " · ";
      genres += item.genres[i];
    }
    regular_.draw(canvas, regular_.ellipsize(genres, synopsis_size, left_w - regular_.measure(label, synopsis_size).width),
                  text_x + regular_.measure(label, synopsis_size).width, genres_y, synopsis_size, rgb(0xd2d2d9));
  }

  float info_y = body_y;
  const float info_x = panel.x + right_x_off;
  auto info_row = [&](const std::string& label, const std::string& value) {
    if (value.empty()) return;
    regular_.draw(canvas, label, info_x, info_y, 13, theme::faint());
    info_y += 18;
    regular_.draw(canvas, regular_.ellipsize(value, 13, right_w), info_x, info_y, 13, rgb(0xe6e6ea));
    info_y += 22;
  };
  std::string genre_list;
  for (std::size_t i = 0; i < item.genres.size(); ++i) {
    if (i) genre_list += ", ";
    genre_list += item.genres[i];
  }
  info_row("Genres:", genre_list);
  info_row("Added:", added_label);
  info_row("File size:", format_bytes(item.size));
  if (item.width > 0 && item.height > 0) {
    info_row("Resolution:", std::to_string(item.width) + " × " + std::to_string(item.height));
  }
  if (!item.filename.empty()) {
    info_y += 8;
    regular_.draw(canvas, regular_.ellipsize(item.filename, 11.5f, right_w), info_x, info_y, 11.5f,
                  theme::faint());
  }
  canvas.pop_clip();

  // Buttons that have scrolled out of the sheet must not stay clickable: what is not
  // on screen is not a target. Their hit areas were added inside the clip, so drop the
  // ones that ended up outside it.
  std::vector<std::pair<Rect, Action>> kept;
  kept.reserve(hits_.size());
  for (const auto& hit : hits_) {
    const bool is_sheet_action = hit.second.kind == Action::Kind::Play ||
                                 hit.second.kind == Action::Kind::ToggleMyList;
    if (is_sheet_action && (hit.first.bottom() < viewport.y || hit.first.y > viewport.bottom())) {
      continue;
    }
    kept.push_back(hit);
  }
  hits_.swap(kept);

  draw_scrollbar(canvas, Rect{panel.right() - 16, panel.y + 70, 4, panel.h - 92}, "detail");

  // Close.
  const Rect close{panel.right() - 52, panel.y + 18, 34, 34};
  canvas.fill_circle(close.x + 17, close.y + 17, 17, rgba(0x000000, 150));
  regular_.draw_centered(canvas, "\u00d7", close, 20, theme::text());
  add_hit(close, Action{Action::Kind::CloseOverlay, -1, ""});
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
    // The samples that ship with the app are the obvious first library, and the web
    // version offered them too; say where they are instead of leaving a dead end.
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
  options.hw_decode = settings_.hw_decode;
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

/* ------------------------------ folder picker ----------------------------- */
// The settings sheet used to be a text field: you had to know and type a path. This is
// a small browser over the real filesystem - drives/volumes, the folders inside them,
// ".." to go up - that ends in the same add_library_folder() call the typed path used,
// so there is one set of validation rules rather than two.

void App::browse_to(const std::string& path) {
  const FolderListing listing = list_folders(path);
  browser_path_ = listing.path.empty() ? path : listing.path;
  browser_folders_ = listing.folders;
  browser_error_ = listing.error;
  browser_truncated_ = listing.truncated;
  set_scroll("browser", 0);
  if (!listing.ok()) LOG_WARN("browser: %s", listing.error.c_str());
}

void App::open_folder_browser(const std::string& start) {
  browser_open_ = true;
  folder_input_focused_ = false;
  // Prefer where the user pointed us; otherwise the folder they typed, then their
  // library, then home.
  std::string first = start;
  if (first.empty() || !is_folder(first)) first = folder_input_;
  if (first.empty() || !is_folder(first)) first = browse_start(settings_.library_folders);
  browse_to(first);
  LOG_INFO("browser: opened at %s (%zu folder(s))", browser_path_.c_str(),
           browser_folders_.size());
}

void App::draw_folder_browser(Canvas& canvas) {
  canvas.fill_rect(Rect{0, 0, float(width_), float(height_)}, rgba(0x000000, 200));
  const float panel_width = std::min(width_ - 120.0f, 720.0f);
  const float panel_height = std::min(static_cast<float>(height_) - 120.0f, 560.0f);
  const Rect panel{(width_ - panel_width) / 2, (height_ - panel_height) / 2, panel_width,
                   panel_height};
  canvas.fill_round_rect(panel, 14, theme::panel());
  // Bottom of this sheet's z-order; everything inside it is registered after this.
  add_hit(Rect{0, 0, float(width_), float(height_)}, Action{Action::Kind::BrowseClose, -1, ""});
  swallow(panel);

  bold_.draw(canvas, "Choose a folder", panel.x + 32, panel.y + 26, 22, theme::text());
  const Rect close{panel.right() - 54, panel.y + 22, 32, 32};
  canvas.fill_circle(close.x + 16, close.y + 16, 16, rgba(0xffffff, 18));
  regular_.draw_centered(canvas, "\u00d7", close, 20, theme::text());
  add_hit(close, Action{Action::Kind::BrowseClose, -1, ""});

  // Where we are. The tail of a long path is the part that says where you are, so the
  // front is what gets dropped when it does not fit.
  std::string shown = browser_path_;
  const float path_width = panel.w - 64;
  if (regular_.measure(shown, theme::kFontSmall).width > path_width) {
    while (shown.size() > 12 && regular_.measure("..." + shown, theme::kFontSmall).width > path_width) {
      shown.erase(0, 1);
    }
    shown = "..." + shown;
  }
  regular_.draw(canvas, shown, panel.x + 32, panel.y + 62, theme::kFontSmall, theme::faint());

  const Rect viewport{panel.x + 1, panel.y + 88, panel.w - 2, panel.h - 88 - 76};
  const float row_height = 34.0f;
  const float row_width = panel.w - 64 - 12;
  const std::string parent = parent_folder(browser_path_);
  const std::vector<std::string> roots = folder_roots();
  const bool show_roots = roots.size() > 1;

  // The content height is known before anything is drawn, so the scroll limit is the
  // real one rather than a guess made afterwards (a bar that cannot reach the last row
  // is worse than no bar).
  float content = 6.0f;
  if (!parent.empty()) content += row_height;
  content += row_height * static_cast<float>(browser_folders_.size());
  if (!browser_error_.empty() || browser_folders_.empty()) content += 30.0f;
  if (browser_truncated_) content += 30.0f;
  if (show_roots) content += 8.0f + 22.0f + row_height * static_cast<float>(roots.size());
  content += 8.0f;
  declare_scroll("browser", content - viewport.h, viewport, false);
  const float scroll = scroll_offset("browser");

  canvas.push_clip(viewport);
  float y = viewport.y + 6 - scroll;

  // A row: a folder glyph drawn as shapes (no icon font, no image), the name, and the
  // hit area that navigates into it. Rows scrolled out of view are neither drawn nor
  // clickable.
  auto folder_row = [&](const std::string& label, const std::string& value,
                        const Action::Kind kind, bool muted) {
    const Rect row{panel.x + 24, y, row_width, row_height - 4};
    const bool visible = row.bottom() > viewport.y && row.y < viewport.bottom();
    if (visible) {
      canvas.fill_round_rect(row, theme::kRadius, rgba(0xffffff, 8));
      const Color glyph = muted ? rgba(0xffffff, 70) : rgba(theme::kAccent, 210);
      canvas.fill_round_rect(Rect{row.x + 12, row.y + 9, 18, 12}, 2, glyph);
      canvas.fill_rect(Rect{row.x + 12, row.y + 7, 8, 3}, glyph);
      regular_.draw(canvas, regular_.ellipsize(label, theme::kFontBody, row.w - 60),
                    row.x + 42, row.y + 7, theme::kFontBody,
                    muted ? theme::dim() : theme::text());
      add_hit(row, Action{kind, -1, value});
    }
    y += row_height;
  };

  if (!parent.empty()) folder_row("..   up one level", parent, Action::Kind::BrowseInto, true);
  for (const auto& folder : browser_folders_) {
    folder_row(folder, folder, Action::Kind::BrowseInto, false);
  }
  auto note = [&](const std::string& text, Color color) {
    if (y + 24 > viewport.y && y < viewport.bottom()) {
      regular_.draw(canvas, text, panel.x + 32, y + 6, theme::kFontSmall, color);
    }
    y += 30;
  };
  if (!browser_error_.empty()) {
    note(browser_error_, rgba(0xff6b6b, 255));
  } else if (browser_folders_.empty()) {
    note("No folders in here. Use this one, or go up.", theme::faint());
  }
  if (browser_truncated_) note("(only the first 500 folders are shown)", theme::faint());

  // Drives or volumes: on Windows this is the only way to reach another disk.
  if (show_roots) {
    y += 8;
    if (y + 18 > viewport.y && y < viewport.bottom()) {
      regular_.draw(canvas, "DRIVES", panel.x + 32, y, theme::kFontTiny, theme::faint());
    }
    y += 22;
    for (const auto& root : roots) folder_row(root, root, Action::Kind::BrowseRoot, true);
  }
  canvas.pop_clip();

  draw_scrollbar(canvas, Rect{panel.right() - 16, viewport.y + 4, 4, viewport.h - 8}, "browser");

  const Rect use{panel.x + 32, panel.bottom() - 60, button_width("Use this folder", true), 42};
  draw_button(canvas, use, "Use this folder", true, Action{Action::Kind::BrowseUse, -1, ""});
  const Rect cancel{use.right() + 12, use.y, button_width("Cancel", false, false), 42};
  draw_button(canvas, cancel, "Cancel", false, Action{Action::Kind::BrowseClose, -1, ""});
  regular_.draw(canvas, "Adds the folder shown at the top to your library.",
                cancel.right() + 16, use.y + 12, theme::kFontSmall, theme::faint());
}

void App::draw_settings(Canvas& canvas) {
  // Same scrim as the detail sheet. The panel is the web settings sheet: 660 wide,
  // pinned near the top, not the old centred dialog.
  canvas.fill_rect(Rect{0, 0, float(width_), float(height_)}, rgba(0x050508, 210));
  const float panel_width = std::min(static_cast<float>(width_) - 40.0f, 660.0f);
  const float pad = 24.0f;
  const float header_h = 58.0f;
  const float inner = panel_width - pad * 2.0f;
  const bool samples = !sample_folder_.empty() && settings_.library_folders.empty();
  const int folders = static_cast<int>(settings_.library_folders.size());

  // This sum is the drawing order below. A short window has to scroll to Scan; a
  // 1600x900 sheet has to keep Add, Scan and the hardware switch on screen.
  float body = 18.0f;
  body += 22.0f;
  body += folders > 0 ? folders * 44.0f : 28.0f;
  body += 10.0f + 38.0f;
  body += 10.0f + 38.0f;
  body += 8.0f + 40.0f;
  body += 12.0f + 40.0f;
  body += 22.0f;
  body += 16.0f;
  body += 22.0f;
  body += 36.0f;
  body += 52.0f;
  body += 66.0f;
  body += 20.0f;
  body += 22.0f;
  body += 36.0f;
  body += 56.0f;
  body += 18.0f;

  const float max_panel = static_cast<float>(height_) - 72.0f;
  const float panel_height = std::max(220.0f, std::min(max_panel, header_h + body));
  const Rect panel{(static_cast<float>(width_) - panel_width) / 2.0f, 36.0f, panel_width, panel_height};
  canvas.fill_round_rect(panel, 8, rgb(0x15151b));
  add_hit(Rect{0, 0, float(width_), float(height_)}, Action{Action::Kind::CloseOverlay, -1, ""});
  swallow(panel);

  bold_.draw(canvas, "Settings", panel.x + pad, panel.y + 18, 22, theme::text());
  const Rect close{panel.right() - 24 - 32, panel.y + 14, 32, 32};
  regular_.draw_centered(canvas, "\u00d7", close, 22, theme::text());
  add_hit(close, Action{Action::Kind::CloseOverlay, -1, ""});
  canvas.fill_rect(Rect{panel.x, panel.y + header_h - 1, panel.w, 1}, rgba(0xffffff, 18));

  const Rect viewport{panel.x + 1, panel.y + header_h, panel.w - 2, std::max(40.0f, panel.h - header_h)};
  declare_scroll("settings", std::max(0.0f, body - viewport.h), viewport, false);
  const float scroll = scroll_offset("settings");
  canvas.push_clip(viewport);

  float y = viewport.y + 18.0f - scroll;
  const float left = panel.x + pad;
  auto section = [&](const char* label) {
    regular_.draw(canvas, label, left, y, 11.5f, rgb(0x8a8a96));
    y += 22.0f;
  };

  section("LIBRARY FOLDERS");
  if (folders == 0) {
    regular_.draw(canvas, "No folders yet. Add one below, or load the demo library.", left, y, 12.5f,
                  theme::faint());
    y += 28.0f;
  }
  for (const auto& folder : settings_.library_folders) {
    const Rect row{left, y, inner, 36};
    canvas.fill_round_rect(row, 6, rgb(0x0d0d12));
    canvas.stroke_round_rect(row, 6, 1.0f, rgba(0xffffff, 26));
    regular_.draw(canvas, regular_.ellipsize(folder, 12.5f, inner - 52), row.x + 12, row.y + 10, 12.5f,
                  theme::text());
    const Rect remove{row.right() - 32, row.y + 4, 28, 28};
    regular_.draw_centered(canvas, "\u00d7", remove, 16, theme::dim());
    add_hit(remove, Action{Action::Kind::RemoveFolder, -1, folder});
    y += 44.0f;
  }

  y += 10.0f;
  const Rect field{left, y, inner, 38};
  canvas.fill_round_rect(field, 6, rgb(0x0d0d12));
  canvas.stroke_round_rect(field, 6, 1.0f,
                           folder_input_focused_ ? rgba(theme::kAccent, 220) : rgba(0xffffff, 36));
  if (folder_input_.empty() && !folder_input_focused_) {
    regular_.draw(canvas, "e.g. D:\\Movies  or  /home/me/Videos", field.x + 12, field.y + 11, 13.0f,
                  theme::faint());
  } else {
    regular_.draw(canvas, regular_.ellipsize(folder_input_, 13.0f, field.w - 24), field.x + 12, field.y + 11,
                  13.0f, theme::text());
    if (folder_input_focused_) {
      const float caret_x = field.x + 12 + regular_.measure(folder_input_, 13.0f).width;
      canvas.fill_rect(Rect{caret_x + 1, field.y + 11, 1.5f, 16}, theme::accent());
    }
  }
  add_hit(field, Action{Action::Kind::AddFolder, -1, folder_input_});
  y += 38.0f;

  y += 10.0f;
  const Rect add_btn{left, y, 132, 36};
  draw_button(canvas, add_btn, "Add folder", true, Action{Action::Kind::AddFolder, -1, folder_input_}, false);
  const Rect browse_btn{add_btn.right() + 10, y, 118, 36};
  draw_button(canvas, browse_btn, "Browse...", false, Action{Action::Kind::BrowseForFolder, -1, ""});
  y += 38.0f;

  y += 8.0f;
  regular_.draw_wrapped(
      canvas,
      "VLCFlix reads files only — it never moves or deletes them. Artwork and previews are written to "
      "data/media.",
      Rect{left, y, inner, 36}, 12.0f, theme::faint(), 2);
  y += 40.0f;

  y += 12.0f;
  const Rect scan{left, y, 168, 40};
  canvas.fill_round_rect(scan, 6, rgb(0xffffff));
  bold_.draw(canvas, "Scan library", scan.x + 18, scan.y + 11, 14.5f, rgb(0x141418));
  add_hit(scan, Action{Action::Kind::ScanLibrary, -1, ""});
  if (samples) {
    const Rect demo{scan.right() + 10, y, 230, 40};
    draw_button(canvas, demo, "Use the bundled samples", false,
                Action{Action::Kind::UseSampleFolder, -1, sample_folder_});
  }
  y += 40.0f;

  y += 6.0f;
  std::string scan_line;
  const std::string clock = scan_clock(catalog_.scanned_at);
  if (!clock.empty()) {
    scan_line = "Last scan finished " + clock + " — " + std::to_string(catalog_.items.size()) + " titles.";
  } else if (!catalog_.items.empty()) {
    scan_line = std::to_string(catalog_.items.size()) + " titles in the library.";
  }
  if (!scan_line.empty()) {
    regular_.draw(canvas, scan_line, left, y, 12.0f, theme::faint());
  }
  y += 16.0f;
  canvas.fill_rect(Rect{left, y, inner, 2}, theme::accent());
  y += 16.0f;

  section("PLAYBACK");
  // Display only. The native app always plays in this window, so the other two engines
  // are not buttons — a click must not pretend the engine changed.
  const Rect seg{left, y, std::min(inner, 420.0f), 32};
  canvas.fill_round_rect(seg, 6, rgb(0x0d0d12));
  canvas.stroke_round_rect(seg, 6, 1.0f, rgba(0xffffff, 30));
  const char* modes[] = {"Real VLC", "FFmpeg", "Built-in player"};
  float mx = seg.x + 4;
  for (int i = 0; i < 3; ++i) {
    const float mw = i == 0 ? 96.0f : (i == 1 ? 84.0f : 130.0f);
    if (i == 0) canvas.fill_round_rect(Rect{mx, seg.y + 3, mw, 26}, 4, rgba(0xe50914, 40));
    regular_.draw(canvas, modes[i], mx + 12, seg.y + 8, 13.0f, i == 0 ? theme::accent() : theme::text());
    mx += mw;
  }
  y += 36.0f;
  regular_.draw_wrapped(
      canvas,
      "“Real VLC” launches VLC and controls it over its HTTP interface. “Built-in player” plays inside this "
      "window using the browser engine, which cannot decode every codec VLC can.",
      Rect{left, y, inner, 48}, 12.5f, theme::faint(), 3);
  y += 52.0f;

  auto status = [&](Color dot, const std::string& text) {
    canvas.fill_circle(left + 5, y + 6, 4, dot);
    regular_.draw(canvas, regular_.ellipsize(text, 12.5f, inner - 20), left + 18, y, 12.5f, theme::text());
    y += 22.0f;
  };
  std::string vlc = settings_.vlc_path;
  if (vlc.empty() && file_exists("/usr/bin/vlc")) vlc = "/usr/bin/vlc";
  if (vlc.empty()) status(rgb(0xe23b3b), "VLC was not found on this machine.");
  else status(rgb(0x46d369), "VLC found (auto-detect): " + vlc);
  status(rgb(0xe8a317), "VLC is not running yet — it starts on first play.");
  if (catalog_.have_ffmpeg) status(rgb(0x46d369), "ffmpeg available — artwork and previews will be generated.");
  else status(rgb(0xe8a317), "ffmpeg not on PATH — cards fall back to generated gradients.");
  y += 20.0f;

  section("HARDWARE DECODING (EXPERIMENTAL)");
  const Rect track{left, y + 2, 44, 24};
  canvas.fill_round_rect(track, 12, settings_.hw_decode ? rgb(0xe50914) : rgba(0xffffff, 40));
  canvas.fill_circle(settings_.hw_decode ? track.right() - 12 : track.x + 12, track.y + 12, 8, rgb(0xffffff));
  add_hit(Rect{track.x - 6, track.y - 6, track.w + 12, track.h + 12},
          Action{Action::Kind::ToggleSetting, -1, "hwDecode"});
  y += 36.0f;
  regular_.draw_wrapped(
      canvas,
      "Off by default. When on, artwork that needs transcoding is tried on the GPU first, and falls back "
      "to software if no frame arrives.",
      Rect{left, y, inner, 48}, 12.0f, theme::faint(), 3);
  y += 56.0f;

  canvas.pop_clip();
  if (scroll_limit("settings") > 0.0f) {
    draw_scrollbar(canvas, Rect{panel.right() - 14, viewport.y + 4, 4, viewport.h - 8}, "settings");
  }
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
  // Keep keyboard focus visible: scroll whatever holds the card - a rail sideways, a
  // grid or the page vertically - just far enough. The drawn rectangles come from the
  // last frame, so this asks the same question the user's eye does.
  if (static_cast<std::size_t>(target) < order_rects_.size()) {
    reveal_rect(order_rects_[static_cast<std::size_t>(target)]);
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

PointerCursor App::pointer_cursor() const {
  // A drag in progress is a pointer doing something, so it stays a hand even when it
  // has wandered off the bar it grabbed.
  if (!pointer_known_) return PointerCursor::Arrow;
  if (!drag_region_.empty()) return PointerCursor::Hand;
  const Action action = hit_test(pointer_x_, pointer_y_);
  switch (action.kind) {
    case Action::Kind::None:
      return PointerCursor::Arrow;  // includes the sheet's click-swallowing panel
    case Action::Kind::AddFolder:
      return PointerCursor::IBeam;  // a text field
    default:
      return PointerCursor::Hand;
  }
}

bool App::handle(const InputEvent& event) {
  if (event.type == InputEvent::Type::MouseMove || event.type == InputEvent::Type::MouseDown) {
    set_pointer(event.x, event.y);
  }
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
      if (!drag_region_.empty()) {
        ScrollState& state = scrolls_[drag_region_];
        const bool horizontal = state.horizontal;
        const float thumb_len = horizontal ? state.thumb.w : state.thumb.h;
        const float extent = horizontal ? state.viewport.w : state.viewport.h;
        const float span = std::max(1.0f, extent - thumb_len);
        const float origin = horizontal ? state.viewport.x : state.viewport.y;
        const float pointer = horizontal ? event.x : event.y;
        const float t = std::clamp((pointer - origin - drag_grab_) / span, 0.0f, 1.0f);
        const float before = state.offset;
        state.offset = t * state.max;
        return state.offset != before;
      }
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
      // A scroll bar takes the pointer until it is released, so dragging works even
      // when the pointer leaves the bar.
      if (action.kind == Action::Kind::ScrollThumb || action.kind == Action::Kind::ScrollTo) {
        ScrollState& state = scrolls_[action.value];
        const bool horizontal = state.horizontal;
        const float pointer = horizontal ? event.x : event.y;
        drag_region_ = action.value;
        if (action.kind == Action::Kind::ScrollThumb) {
          drag_grab_ = pointer - (horizontal ? state.thumb.x : state.thumb.y);
        } else {
          // Clicking the track centres the thumb on the pointer, which is what every
          // scroll bar does.
          const float thumb_len = horizontal ? state.thumb.w : state.thumb.h;
          drag_grab_ = thumb_len / 2;
          const float extent = horizontal ? state.viewport.w : state.viewport.h;
          const float span = std::max(1.0f, extent - thumb_len);
          const float origin = horizontal ? state.viewport.x : state.viewport.y;
          const float t = std::clamp((pointer - origin - drag_grab_) / span, 0.0f, 1.0f);
          state.offset = t * state.max;
        }
        return true;
      }
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
          // A new screen starts at the top; rails keep their own sideways position.
          set_scroll("page", 0);
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
          browser_open_ = false;
          LOG_INFO("ui: overlay closed");
          return true;
        case Action::Kind::OpenSettings:
          settings_open_ = true;
          open_item_ = -1;
          LOG_INFO("ui: settings open");
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

          notify(action.value == "hwDecode"
                     ? std::string("Hardware decoding ") + (settings_.hw_decode ? "on" : "off")
                     : std::string("Unknown setting"));
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
        case Action::Kind::BrowseForFolder: {
          open_folder_browser(folder_input_);
          return true;
        }
        case Action::Kind::BrowseInto: {
          if (action.value == "..") {
            const std::string up = parent_folder(browser_path_);
            if (up.empty()) return false;
            browse_to(up);
            return true;
          }
          if (!action.value.empty() && (action.value[0] == '/' ||
                                        (action.value.size() > 1 && action.value[1] == ':'))) {
            browse_to(action.value);  // an absolute path: a drive or a root
          } else {
            browse_to(join_folder(browser_path_, action.value));
          }
          LOG_INFO("browser: now at %s", browser_path_.c_str());
          return true;
        }
        case Action::Kind::BrowseRoot: {
          browse_to(action.value);
          LOG_INFO("browser: now at %s", browser_path_.c_str());
          return true;
        }
        case Action::Kind::BrowseUse: {
          std::string error;
          if (add_library_folder(browser_path_, &error)) {
            notify("Added " + browser_path_);
            browser_open_ = false;
            set_scroll("settings", 0);
            LOG_INFO("browser: added %s", browser_path_.c_str());
          } else {
            // Already in the list, or gone: say so and stay where we are.
            notify(error);
            browser_error_ = error;
          }
          return true;
        }
        case Action::Kind::BrowseClose: {
          browser_open_ = false;
          return true;
        }
        // Scroll bars are consumed above, before the switch; listing them here keeps
        // the compiler's exhaustive-switch warning useful.
        case Action::Kind::ScrollTo:
        case Action::Kind::ScrollThumb:
          return false;
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
        case Action::Kind::ToggleFullscreen:
          return false;
      }
      return false;
    }
    case InputEvent::Type::Wheel: {
      // Everywhere the wheel is pointed at something that overflows, it scrolls that
      // thing: the open sheet, the rail under the pointer, or the page. The sheets used
      // to ignore the wheel completely, which is the "I cannot scroll where I want" bug.
      const float vertical = event.wheel != 0 ? event.wheel : 0.0f;
      const float horizontal = event.wheel_x;
      if (browser_open_ || open_item_ >= 0 || settings_open_) {
        const std::string region = browser_open_ ? "browser"
                                   : open_item_ >= 0 ? "detail"
                                                     : "settings";
        return scroll_by(region, -(vertical != 0 ? vertical : horizontal) * kWheelStep);
      }
      if (horizontal != 0) {
        const std::string& rail = rail_at(event.x, event.y);
        if (!rail.empty()) {
          if (scroll_by("rail:" + rail, -horizontal * kRailStep)) return true;
        }
      }
      if (view_ == View::Home && vertical != 0) {
        const std::string& rail = rail_at(event.x, event.y);
        if (!rail.empty()) {
          // Wheel over a rail scrolls it sideways while it still has somewhere to go,
          // then falls through to the page rather than swallowing the input at the end.
          if (scroll_by("rail:" + rail, -vertical * kRailStep)) return true;
        }
      }
      return scroll_by("page", -(vertical != 0 ? vertical : horizontal) * kWheelStep);
    }
    case InputEvent::Type::MouseUp: {
      if (drag_region_.empty()) return false;
      drag_region_.clear();
      return true;
    }
    case InputEvent::Type::Key: {
      if (event.key == kKeyEscape) {
        if (browser_open_) {
          browser_open_ = false;
          return true;
        }
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
      // Page keys scroll whatever is in front: a page at a time, or to the very end.
      if (event.key == kKeyPageUp || event.key == kKeyPageDown || event.key == kKeyHome ||
          event.key == kKeyEnd) {
        const std::string region = browser_open_ ? "browser"
                                 : open_item_ >= 0 ? "detail"
                                 : settings_open_  ? "settings"
                                                   : "page";
        const ScrollState& state = scrolls_[region];
        const float page = std::max(80.0f, state.viewport.h - 80.0f);
        if (event.key == kKeyPageDown) return scroll_by(region, page);
        if (event.key == kKeyPageUp) return scroll_by(region, -page);
        if (event.key == kKeyHome) return scroll_by(region, -state.max);
        return scroll_by(region, state.max);
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
      // Keyboard browsing. The web version only had `/` and Escape; a native window
      // is expected to be usable without a mouse, so arrows move between cards and
      // Enter opens the focused one.
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
  scrolls_.clear();
  drag_region_.clear();
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
  } else if (screen == "browser") {
    // The folder picker over the settings sheet, which is how a user sees it. The
    // argument is the folder to open at.
    view_ = View::Home;
    settings_open_ = true;
    open_folder_browser(argument);
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
  if (browser_open_) return "browser";
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
