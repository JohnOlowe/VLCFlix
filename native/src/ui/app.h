// The browsing UI: state, layout, drawing and input.
//
// Deliberately one file for one screen's worth of behaviour per function, and no
// widget library: layout and hit-testing are produced by the same code path, so a
// button can never be drawn somewhere it cannot be clicked. Drawing writes into a
// Canvas, which means every screen can be rendered to a PNG and inspected without a
// display (see render_to_png).
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/model.h"
#include "core/store.h"
#include "core/sync.h"
#include "library/subtitles.h"
#include "playback/engine.h"
#include "playback/presenter.h"
#include "render/canvas.h"
#include "render/font.h"
#include "render/image.h"
#include "ui/theme.h"

namespace vf {

// What the pointer should look like over a given pixel. Defined here rather than in the
// window layer so the UI can decide it (the UI is what knows what is under the pointer)
// without knowing that SDL exists; the window turns it into a real cursor.
enum class PointerCursor { Arrow, Hand, IBeam };

namespace ui {

enum class View { Home, Browse, MyList, Search };

struct InputEvent {
  enum class Type { None, MouseMove, MouseDown, MouseUp, Wheel, Key, Text, Back, Resize } type = Type::None;
  float x = 0, y = 0;
  float wheel = 0;      // vertical: +1 per notch down-and-away from the user
  float wheel_x = 0;    // horizontal wheel / trackpad swipe
  int key = 0;         // SDLK_* values, kept as int so this header stays toolkit-free
  std::string text;    // for Type::Text
  bool ctrl = false;
  bool from_pad = false;  // a two-finger drag, not a mouse wheel
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
    PadScroll,      // click or drag the touchpad speed track
    NudgePadScroll, // value = "+" or "-"
    FocusSearch,
    ClearSearch,
    ScanLibrary,
    AddFolder,      // value = the typed path
    BrowseForFolder,  // open the folder picker
    BrowseInto,       // value = child folder name (".." goes up)
    BrowseRoot,       // value = a drive/volume root
    BrowseUse,        // add the folder the picker is showing
    BrowseClose,
    Replay,
    ClosePlayer,
    Seek,           // x position of the click decides
    SeekRelative,   // value = +/- seconds
    TogglePause,
    Mute,
    Volume,
    CycleSubtitles,
    NudgeSync,      // value = +/- ms
    ToggleFullscreen,
    RemoveFolder,   // value = the folder
    UseSampleFolder,
    ScrollTo,       // value = scroll region; a click on the bar's track jumps there
    ScrollThumb,    // value = scroll region; the thumb is dragged
  } kind = Kind::None;
  int index = -1;
  std::string value;
};

class App {
 public:
  App(std::string media_dir, std::string data_dir);
  ~App();

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
  bool browser_open() const { return browser_open_; }
  // What the folder picker is showing, and what it found there. Read by the tests.
  const std::string& browser_path() const { return browser_path_; }
  const std::vector<std::string>& browser_folders() const { return browser_folders_; }
  const std::string& browser_error() const { return browser_error_; }
  // Opens the picker at `start` (or at a sensible place when the path is unusable).
  void open_folder_browser(const std::string& start);
  void browse_to(const std::string& path);
  const std::string& last_message() const { return last_message_; }
  std::string screen_name() const;

  // Scrolling. Every screen that can overflow declares a region while it draws; the
  // input path and the tests use the same names, so "it scrolls" is measurable.
  // Regions: "page" (Home, Browse, My List, Search), "detail", "settings", and
  // "rail:<id>" for each row on Home.
  float scroll_offset(const std::string& region) const;
  float scroll_limit(const std::string& region) const;
  std::string scroll_region_at(float x, float y) const;
  static const char* page_region() { return "page"; }

  // Where Home puts its two halves at a given scroll: the hero, and the rails below it.
  // Public because the scroll behaviour ("does the page move, or does it come apart") is
  // exactly what these two rectangles decide, and testing it needs to see them.
  Rect hero_rect(float scroll) const;
  Rect rails_rect(float scroll) const;

  // The hit areas produced by the last draw. Used by tests so they click the real
  // geometry instead of guessed pixel coordinates.
  const std::vector<std::pair<Rect, Action>>& hit_rects() const { return hits_; }

  // Hovering must say "this is clickable". The answer comes from the same hit areas the
  // click uses, so the cursor can never promise something a click will not do.
  PointerCursor pointer_cursor() const;
  float pointer_x() const { return pointer_x_; }
  float pointer_y() const { return pointer_y_; }
  void set_pointer(float x, float y) {
    pointer_x_ = x;
    pointer_y_ = y;
    pointer_known_ = true;
  }
  std::string hover_title() const;

  // Reported after a Play click, whether or not the player could be started. The
  // tests use it to see the request rather than the pixels.
  std::function<void(int index)> on_play_requested;
  // The player asks the window to go fullscreen; the window decides how.
  std::function<void()> on_toggle_fullscreen;

  /* ------------------------------- playback -------------------------------- */

  // Starts the film at `index` in the catalog. Returns false (and says why in
  // last_message) when there is nothing to play - a missing file, or an engine that
  // refused the format.
  bool start_playback(int index);
  void stop_playback();
  // Starts/stops the muted hover preview for `index` (-1 stops it). The engine
  // starts on a background thread so the window keeps answering Windows.
  void set_hover_preview(int index);
  bool preview_playing() const { return preview_index_ >= 0; }
  bool preview_has_picture() const { return preview_has_frame_; }
  bool playing() const { return player_open_; }
  const EngineStatus& player_status() const { return player_status_; }
  // Why the player gave up, if it did. Empty while it is healthy.
  const std::string& player_error() const { return player_error_; }
  const Presenter& presenter() const { return presenter_; }
  // The engine, for tests that want to watch what it really did (frames, seeks).
  Engine& engine() { return engine_; }

  // One UI frame: pulls decoded pictures, applies the sync plan, saves progress and
  // notices the end of a film. `now` is seconds on the same clock the presenter uses.
  // Returns true when the picture or the chrome changed.
  bool tick(double now);
  // Draws the player into its own canvas (the window presents this instead).
  void draw_player_frame(Canvas& canvas);
  double clock_now() const;
  void set_clock(std::function<double()> clock) { clock_ = std::move(clock); }

  // Subtitle state, also used by the tests.
  const std::vector<SubtitleTrack>& subtitle_tracks() const { return subtitle_tracks_; }
  int subtitle_track() const { return subtitle_track_; }
  std::string active_subtitle_text(double time) const;
  int subtitle_offset_ms() const { return subtitle_offset_ms_; }
  void set_subtitle_offset_ms(int offset);
  int sync_audio_offset_ms() const { return manual_sync_ms_; }
  void nudge_sync(int delta_ms);
  const SyncPlan& sync_plan() const { return sync_plan_; }
  bool player_chrome_visible() const { return chrome_visible_; }
  // Ends the film's progress as finished, which is the same rule the store applies
  // (A4): 92 % or more leaves Continue Watching.
  void finish_film();

  // Kept so a caller can redraw between items. The scan itself no longer runs on
  // the window thread: artwork workers have to be able to lose their place to the
  // film under the pointer, and that only works if the window is still reading the mouse.
  std::function<void()> pump;

  // Starts a scan on a background thread and returns 0. Returns -1 when there is
  // nothing to scan. The window stays usable; tick() copies the catalog in as
  // artwork lands.
  int scan_now();
  // Draws whatever posters and previews are still missing, without walking the
  // folders again. The film under the pointer is drawn next.
  void resume_missing_artwork();

  // Settings editing, persisted immediately. Both return false and set `error` when
  // the change was rejected (a folder that does not exist, for instance).
  bool add_library_folder(const std::string& folder, std::string* error);
  bool remove_library_folder(const std::string& folder, std::string* error);
  // Live while dragging; written when the drag ends or a step button is clicked.
  bool apply_pad_scroll(float value, bool persist);
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
  void draw_folder_browser(Canvas& canvas);
  void draw_toast(Canvas& canvas);
  void draw_footer(Canvas& canvas);

  void draw_poster_card(Canvas& canvas, const Rect& rect, const Item& item, bool wide,
                        float scroll, bool title_on_art = false);
  void draw_button(Canvas& canvas, const Rect& rect, const std::string& label, bool primary,
                   Action action, bool icon = true);
  // Buttons are sized to their label, so no label can be clipped by a fixed width
  // (the first screenshots had "Open Setting" cut off).
  float button_width(const std::string& label, bool primary, bool icon = true) const;
  void draw_backdrop_or_fallback(Canvas& canvas, const Rect& rect, const Item& item, float radius);
  // The hover card: the 16:9 preview still, the title, the meta line and a Play hint,
  // floating next to the hovered poster. No animation yet - motion needs a decoder,
  // which arrives with the player in turn 3 - but the still is real artwork.
  void draw_hover_preview(Canvas& canvas);
  const Image* backdrop_for(const Item& item);
  const Image* poster_for(const Item& item);

  void add_hit(const Rect& rect, Action action);
  // An area that belongs to something already drawn and must not pass the click on to
  // what is behind it (the body of a sheet over the page, or over the sheet's scrim).
  // add_hit() drops Action::Kind::None, which is why this exists: the sheets used to
  // call add_hit() with a None action and silently registered nothing at all.
  void swallow(const Rect& rect);
  Action hit_test(float x, float y) const;

  /* -------------------------------- scrolling ------------------------------ */
  struct ScrollState {
    float offset = 0;
    float max = 0;
    Rect viewport{0, 0, 0, 0};
    Rect thumb{0, 0, 0, 0};
    bool horizontal = false;
  };
  // Called by the drawing pass with what it just laid out, so the limit is never a
  // guess: the same number drives the bar, the wheel and the tests.
  void declare_scroll(const std::string& region, float max_offset, const Rect& viewport,
                      bool horizontal);
  void set_scroll(const std::string& region, float value);
  bool scroll_by(const std::string& region, float delta);
  // Draws a bar (only when the region actually overflows) and registers its hit areas.
  void draw_scrollbar(Canvas& canvas, const Rect& track, const std::string& region,
                      int thumb_alpha = 96, int track_alpha = 26);
  // Scrolls whatever contains `rect` just enough to show it - used when the keyboard
  // moves focus, and when a rail is scrolled.
  void reveal_rect(const Rect& rect);
  const std::string& rail_at(float x, float y) const;
  void notify(const std::string& message);
  bool handle_player(const InputEvent& event);
  void draw_player_back(Canvas& canvas);
  void draw_chrome(Canvas& canvas);
  void draw_end_card(Canvas& canvas);
  void apply_sync();
  int cycle_subtitle_track(int delta);
  std::string format_duration(double seconds) const;
  std::string meta_line(const Item& item) const;
  int percent_for(const Item& item) const;

  std::string media_dir_;
  std::string data_dir_;
  Store store_;

  /* playback */
  Engine engine_;
  // The hover preview gets its own engine: it plays while the browsing UI is on
  // screen, so it cannot share the film player (that one is a different view and a
  // different file). Muted, 640x360, stopped as soon as the pointer leaves.
  Engine preview_engine_;
  Frame preview_frame_;
  bool preview_has_frame_ = false;
  int preview_index_ = -1;
  double preview_started_ = 0;
  // The preview engine is only touched by preview_thread_. The window thread
  // queues a request and copies out a finished frame.
  struct PreviewRequest {
    int generation = 0;
    int index = -1;
    std::string path;
    std::string name;
    double seek_at = -1;
  };
  std::mutex preview_mu_;
  std::condition_variable preview_cv_;
  PreviewRequest preview_request_;
  bool preview_request_ready_ = false;
  bool preview_stop_thread_ = false;
  bool preview_published_ = false;
  Frame preview_published_frame_;
  int preview_failed_generation_ = 0;
  std::atomic<int> preview_generation_{0};
  std::thread preview_thread_;
  // Playing a whole film as a preview must not seek before the demuxer knows the
  // length; a seek into nothing leaves the engine sitting there with no picture, which
  // is exactly what "the preview does not work" looked like on the Windows machine.
  double preview_seek_at_ = -1;
  // Where the hovered card was drawn this pass, so the hover can grow it in place.
  Rect hover_card_rect_{};
  bool hover_card_valid_ = false;
  Rect focus_card_rect_{};
  bool focus_card_valid_ = false;
  bool preview_warned_ = false;
  void draw_hover_card(Canvas& canvas);
  void ensure_preview_thread();
  void preview_thread_main();
  // Copies a catalog the scan thread published. Called from tick, never from the
  // scan thread: the window thread is the only one that draws and saves.
  bool apply_scan_publish();
  void join_scan_thread();
  void flush_hover_priority(double now);
  Presenter presenter_;
  EngineStatus player_status_;
  bool player_open_ = false;       // a film is loaded in the player view
  bool player_chrome_ = false;     // the chrome is drawn over the picture
  bool chrome_visible_ = true;
  // The seek and volume tracks, so a click uses the bar that was drawn.
  Rect player_seek_track_{};
  Rect player_volume_track_{};
  bool player_finished_ = false;
  bool player_failed_ = false;
  double chrome_hidden_at_ = -1;   // clock time when the chrome last hid
  double last_input_at_ = 0;
  double playback_started_at_ = 0;
  double last_progress_save_ = 0;
  double resume_at_ = -1;          // pending "start here" seek, applied once length is known
  int playing_index_ = -1;
  std::vector<SubtitleTrack> subtitle_tracks_;
  int subtitle_track_ = -1;        // -1 = Off
  std::vector<Cue> subtitle_cues_;
  int subtitle_offset_ms_ = 0;
  int manual_sync_ms_ = 0;
  SyncPlan sync_plan_;
  std::function<double()> clock_;
  bool scrub_dragging_ = false;
  std::string player_error_;
  std::string folder_input_;
  bool folder_input_focused_ = false;
  bool scanning_ = false;
  int scan_done_ = 0;
  int scan_total_ = 0;
  std::string sample_folder_;
  // The scan thread only writes these. tick() copies them onto the fields above.
  std::mutex publish_mu_;
  bool publish_dirty_ = false;
  bool has_pending_catalog_ = false;
  Catalog pending_catalog_;
  bool has_pending_progress_ = false;
  int pending_done_ = 0;
  int pending_total_ = 0;
  std::string pending_label_;
  std::string pending_notice_;
  std::atomic<bool> scan_cancel_{false};
  std::atomic<bool> scan_running_{false};
  std::thread scan_thread_;
  // Debounce: scrubbing across ten cards must not jump ten films to the front.
  std::string hover_priority_id_;
  double hover_priority_at_ = -1;
  std::string preview_playing_path_;
  int preview_playing_version_ = -1;
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
  std::map<std::string, ScrollState> scrolls_;
  std::string drag_region_;      // the scroll bar being dragged, if any
  bool pad_scroll_drag_ = false;
  Rect pad_scroll_track_{};
  // The folder picker: what it is showing, what is in it, and why it may be empty.
  bool browser_open_ = false;
  std::string browser_path_;
  std::vector<std::string> browser_folders_;
  std::string browser_error_;
  bool browser_truncated_ = false;
  float drag_grab_ = 0;          // pointer offset inside the thumb when the drag began
  std::vector<std::pair<Rect, std::string>> rail_bands_;
  std::vector<Rect> order_rects_;  // drawn position of each card in order_
  int hover_item_ = -1;
  float pointer_x_ = 0;
  float pointer_y_ = 0;
  bool pointer_known_ = false;
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
