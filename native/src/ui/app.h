// The browsing UI: state, layout, drawing and input.
//
// Deliberately one file for one screen's worth of behaviour per function, and no
// widget library: layout and hit-testing are produced by the same code path, so a
// button can never be drawn somewhere it cannot be clicked. Drawing writes into a
// Canvas, which means every screen can be rendered to a PNG and inspected without a
// display (see render_to_png).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/store.h"
#include "render/canvas.h"
#include "render/font.h"
#include "render/image.h"
#include "ui/theme.h"

namespace vf {
namespace ui {

enum class View { Home, Browse, MyList, Search };

struct InputEvent {
  enum class Type { None, MouseMove, MouseDown, MouseUp, Wheel, Key, Text, Back, Resize } type = Type::None;
  float x = 0, y = 0;
  float wheel = 0;
  int key = 0;         // SDLK_* values, kept as int so this header stays toolkit-free
  std::string text;    // for Type::Text
  bool ctrl = false;
};

// What a click was for. Recorded while drawing, consumed by handle().
struct Action {
  enum class Kind {
    None,
    Navigate,       // value = view name
    OpenDetail,     // index
    CloseOverlay,
    Play,           // index
    ToggleMyList,   // index
    OpenSettings,
    ToggleSetting,  // value = setting key
    FocusSearch,
    ClearSearch,
    ScanLibrary,
    AddFolder,      // value = the typed path
    RemoveFolder,   // value = the folder
    UseSampleFolder,
  } kind = Kind::None;
  int index = -1;
  std::string value;
};

class App {
 public:
  App(std::string media_dir, std::string data_dir);

  void set_catalog(Catalog catalog);
  void set_progress(std::map<std::string, Progress> progress);
  void set_my_list(std::vector<std::string> my_list);
  void set_settings(Settings settings);
  void set_scan_state(const std::string& label);

  const Catalog& catalog() const { return catalog_; }
  const Settings& settings() const { return settings_; }
  std::vector<std::string> my_list() const { return my_list_; }

  void resize(int width, int height);
  int width() const { return width_; }
  int height() const { return height_; }

  void draw(Canvas& canvas);
  void ensure_layout();

  // Keyboard focus over the cards. `order` is the drawing order of the current
  // screen, so arrows move the way the cards are laid out rather than the way the
  // catalog happens to be sorted.
  int focus_item() const { return focus_item_; }
  void move_focus(int dx, int dy);
  void clear_focus() { focus_item_ = -1; }
  // Used by screenshots and tests to put the pointer somewhere without a mouse.
  void set_hover(int index) { hover_item_ = index; }

  // Returns true when the event changed something that needs a redraw.
  bool handle(const InputEvent& event);

  // For tests and screenshots: put the UI into a known state without a mouse.
  bool show_screen(const std::string& screen, const std::string& argument);
  bool render_to_png(const std::string& path);

  // Read-only introspection, used by the journey tests to assert what the UI thinks.
  View view() const { return view_; }
  const std::string& query() const { return query_; }
  int open_item() const { return open_item_; }
  bool settings_open() const { return settings_open_; }
  const std::string& last_message() const { return last_message_; }
  std::string screen_name() const;

  // The hit areas produced by the last draw. Used by tests so they click the real
  // geometry instead of guessed pixel coordinates.
  const std::vector<std::pair<Rect, Action>>& hit_rects() const { return hits_; }
  std::string hover_title() const;

  // Called when the user clicks Play. The player arrives in turn 3; until then the
  // callback is what a test can hook to observe the request.
  std::function<void(int index)> on_play_requested;

  // Set by the window loop to redraw and present a frame. The library scan is
  // synchronous (it is the same code the CLI runs, and a second thread would need a
  // second copy of every rule), but it calls this between items so the window keeps
  // painting instead of freezing into "not responding".
  std::function<void()> pump;

  // Runs the real scan over the configured folders and returns how many films it
  // found. Persists the catalog through the same store the CLI uses.
  int scan_now();

  // Settings editing, persisted immediately. Both return false and set `error` when
  // the change was rejected (a folder that does not exist, for instance).
  bool add_library_folder(const std::string& folder, std::string* error);
  bool remove_library_folder(const std::string& folder, std::string* error);
  const std::string& folder_input() const { return folder_input_; }
  bool folder_input_focused() const { return folder_input_focused_; }
  const std::string& scan_label() const { return scan_label_; }
  bool scanning() const { return scanning_; }
  // The samples folder shipped with the repository, if this build knows where it is.
  void set_sample_folder(std::string folder) { sample_folder_ = std::move(folder); }
  const std::string& sample_folder() const { return sample_folder_; }

 private:
  void draw_header(Canvas& canvas);
  void draw_home(Canvas& canvas);
  void draw_grid_view(Canvas& canvas, const std::string& title,
                      const std::vector<std::size_t>& items, const std::string& subtitle);
  void draw_detail(Canvas& canvas, const Item& item);
  void draw_settings(Canvas& canvas);
  void draw_toast(Canvas& canvas);
  void draw_footer(Canvas& canvas);

  Rect hero_rect(float scroll) const;
  Rect rails_rect(float scroll) const;

  void draw_poster_card(Canvas& canvas, const Rect& rect, const Item& item, bool wide,
                        float scroll);
  void draw_button(Canvas& canvas, const Rect& rect, const std::string& label, bool primary,
                   Action action);
  // Buttons are sized to their label, so no label can be clipped by a fixed width
  // (the first screenshots had "Open Setting" cut off).
  float button_width(const std::string& label, bool primary) const;
  void draw_backdrop_or_fallback(Canvas& canvas, const Rect& rect, const Item& item, float radius);
  // The hover card: the 16:9 preview still, the title, the meta line and a Play hint,
  // floating next to the hovered poster. No animation yet - motion needs a decoder,
  // which arrives with the player in turn 3 - but the still is real artwork.
  void draw_hover_preview(Canvas& canvas);
  const Image* backdrop_for(const Item& item);
  const Image* poster_for(const Item& item);

  void add_hit(const Rect& rect, Action action);
  Action hit_test(float x, float y) const;
  void notify(const std::string& message);
  std::string format_duration(double seconds) const;
  std::string meta_line(const Item& item) const;
  int percent_for(const Item& item) const;

  std::string media_dir_;
  std::string data_dir_;
  Store store_;
  std::string folder_input_;
  bool folder_input_focused_ = false;
  bool scanning_ = false;
  std::string sample_folder_;
  Catalog catalog_;
  std::map<std::string, Progress> progress_;
  std::vector<std::string> my_list_;
  Settings settings_;
  std::string scan_label_;

  View view_ = View::Home;
  std::string query_;
  bool search_focused_ = false;
  int open_item_ = -1;
  bool settings_open_ = false;
  float home_scroll_ = 0;
  float grid_scroll_ = 0;
  int hover_item_ = -1;
  int focus_item_ = -1;
  std::vector<int> order_;   // catalog indices in drawing order, rebuilt every frame
  int order_columns_ = 1;
  float order_row_height_ = 0;
  std::string last_message_;
  double message_until_ = 0;

  int width_ = 1600;
  int height_ = 900;

  Font regular_;
  Font bold_;
  ImageCache images_;
  std::vector<std::pair<Rect, Action>> hits_;
};

}  // namespace ui
}  // namespace vf
