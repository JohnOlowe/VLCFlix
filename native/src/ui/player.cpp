// The player view: picture, chrome, subtitles, sync, resume and the finished state.
//
// Part of ui::App but kept in its own file: the browsing screens and the player have
// almost nothing in common beyond the canvas and the theme, and a 2 000-line file is
// harder to reason about than two that each fit in a head.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/log.h"
#include "core/paths.h"
#include "core/sync.h"
#include "ui/app.h"

namespace vf {
namespace ui {
namespace {
constexpr int kKeyEscape = 27;
constexpr int kKeySpace = ' ';
constexpr int kKeyLeft = 1073741904;
constexpr int kKeyRight = 1073741903;
constexpr int kKeyUp = 1073741906;
constexpr int kKeyDown = 1073741905;
constexpr int kKeyM = 'm';
constexpr int kKeyF = 'f';
constexpr int kKeyS = 's';
constexpr int kKeyLeftBracket = '[';
constexpr int kKeyRightBracket = ']';
constexpr double kProgressSaveInterval = 5.0;
constexpr double kChromeHideAfter = 3.0;

std::string clock_text(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
  const int total = static_cast<int>(seconds + 0.5);
  char buffer[32];
  if (total >= 3600) {
    std::snprintf(buffer, sizeof buffer, "%d:%02d:%02d", total / 3600, (total / 60) % 60,
                  total % 60);
  } else {
    std::snprintf(buffer, sizeof buffer, "%d:%02d", total / 60, total % 60);
  }
  return buffer;
}
}  // namespace

double App::clock_now() const {
  if (clock_) return clock_();
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

/* ------------------------------ starting it ------------------------------ */

bool App::start_playback(int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= catalog_.items.size()) {
    // Nothing to play. Saying so is better than opening a black window.
    notify(catalog_.items.empty() ? std::string("The library is empty")
                                  : std::string("That film is no longer in the library"));
    return false;
  }
  const Item& item = catalog_.items[static_cast<std::size_t>(index)];
  if (on_play_requested) on_play_requested(index);

  stop_playback();
  player_error_.clear();
  player_finished_ = false;
  player_failed_ = false;
  subtitle_tracks_.clear();
  subtitle_cues_.clear();
  subtitle_track_ = -1;
  subtitle_offset_ms_ = 0;

  if (item.unreadable) {
    // Known bad before we even open it: a partial download or a container the prober
    // rejected. Saying so beats a black window and a spinner.
    player_error_ = "This file could not be read when the library was scanned: " + item.filename;
    notify(player_error_);
    LOG_ERROR("player: refusing unreadable item %s", item.filename.c_str());
    return false;
  }

  const std::string absolute = item.path;
  if (!file_exists(absolute)) {
    player_error_ = "The file is gone: " + absolute;
    notify(player_error_);
    LOG_ERROR("player: missing file %s", absolute.c_str());
    return false;
  }

  // Decode at the size the picture will actually be shown at, capped: bigger frames
  // cost memory for detail nobody can see, and the presenter has to hold them.
  const int decode_width = std::min(1280, std::max(320, width_));
  engine_.configure(decode_width, decode_width * 9 / 16);
  engine_.set_clock([this] { return clock_now(); });
  std::string error;
  if (!engine_.open(absolute, &error)) {
    player_error_ = error;
    notify(error);
    return false;
  }

  subtitle_tracks_ = item.subtitles;
  presenter_.clear();

  // Resume where the film was left, but never in the last few seconds: resuming at
  // 91 % only to hit the end is not a courtesy.
  auto saved = progress_.find(item.id);
  resume_at_ = -1;
  if (saved != progress_.end() && saved->second.duration > 0) {
    const double fraction = saved->second.position / saved->second.duration;
    if (fraction > 0.02 && fraction < 0.92) resume_at_ = saved->second.position;
  }

  engine_.play();
  playback_started_at_ = clock_now();
  player_open_ = true;
  playing_index_ = index;
  chrome_visible_ = true;
  last_input_at_ = clock_now();
  last_progress_save_ = last_input_at_;
  player_status_ = engine_.status();
  apply_sync();  // set the initial plan before the first frame is presented
  LOG_INFO("player: playing %s (resume %s)", item.title.c_str(),
           resume_at_ >= 0 ? std::to_string(static_cast<int>(resume_at_)).c_str() : "no");
  notify("Playing “" + item.title + "”");
  return true;
}

void App::stop_playback() {
  if (!player_open_ && !engine_.has_media()) return;
  const EngineStatus status = engine_.status();
  const int index = playing_index_;
  if (index >= 0 && static_cast<std::size_t>(index) < catalog_.items.size() && status.length > 0) {
    // Leaving mid-film is exactly what Continue Watching is for, and the store applies
    // the same 2 %/92 % rule it applies to everything else.
    store_.set_progress(catalog_.items[static_cast<std::size_t>(index)].id, status.time,
                        status.length);
    progress_ = store_.load_progress();
  }
  engine_.stop();
  presenter_.clear();
  player_open_ = false;
  player_chrome_ = false;
  playback_started_at_ = 0;
  playing_index_ = -1;
  subtitle_cues_.clear();
  LOG_INFO("player: stopped at %.1f s", status.time);
}

void App::finish_film() {
  player_finished_ = true;
  const int index = playing_index_;
  if (index >= 0 && static_cast<std::size_t>(index) < catalog_.items.size()) {
    const Item& item = catalog_.items[static_cast<std::size_t>(index)];
    const double length = player_status_.length > 0 ? player_status_.length : 1;
    // Writing it as "at the end" is what makes the store drop the entry: a finished
    // film leaves Continue Watching instead of sitting at 99 % forever (A4).
    store_.set_progress(item.id, length, length);
    progress_ = store_.load_progress();
    LOG_INFO("player: finished %s", item.title.c_str());
    notify("Finished “" + item.title + "”");
  }
}

/* -------------------------------- sync ----------------------------------- */

void App::apply_sync() {
  // libVLC cannot be told what the output device's latency is, so the device term is
  // zero here and the manual offset is what the maths works on. The plan still decides
  // which of the two corrections pays: audio when sound must wait, picture when the
  // picture must. That is the same arithmetic the browser used.
  sync_plan_ = compute_sync(0, manual_sync_ms_);
  engine_.set_audio_delay_ms(sync_plan_.audio_delay_ms);
  presenter_.configure(sync_plan_.video_delay_ms);
}

void App::nudge_sync(int delta_ms) {
  manual_sync_ms_ = std::clamp(manual_sync_ms_ + delta_ms, -kMaxVideoDelayMs, kMaxVideoDelayMs);
  apply_sync();
  notify("Sync " + describe_delay(manual_sync_ms_));
  LOG_INFO("player: sync offset %d ms -> video %d ms, audio %d ms", manual_sync_ms_,
           sync_plan_.video_delay_ms, sync_plan_.audio_delay_ms);
}

void App::set_subtitle_offset_ms(int offset) {
  subtitle_offset_ms_ = std::clamp(offset, -20000, 20000);
  notify("Subtitles " + describe_delay(subtitle_offset_ms_));
}

/* ------------------------------- subtitles -------------------------------- */

std::string App::active_subtitle_text(double time) const {
  if (subtitle_track_ < 0 || subtitle_cues_.empty()) return "";
  const double at = time + subtitle_offset_ms_ / 1000.0;
  for (const auto& cue : subtitle_cues_) {
    if (at >= cue.start && at <= cue.end) return cue.text;
  }
  return "";
}

int App::cycle_subtitle_track(int delta) {
  if (subtitle_tracks_.empty()) {
    notify("No subtitle files next to this film");
    return -1;
  }
  const int count = static_cast<int>(subtitle_tracks_.size()) + 1;  // + Off
  int next = (subtitle_track_ + 1 + delta + count) % count;         // -1 (=Off) is index 0
  subtitle_track_ = next - 1;
  subtitle_cues_.clear();
  if (subtitle_track_ >= 0) {
    const SubtitleTrack& track = subtitle_tracks_[static_cast<std::size_t>(subtitle_track_)];
    // The catalog stores the subtitle's name beside the film (that is what the web
    // build's JSON held), so it has to be joined with the film's folder here. Reading
    // it relative to the process's working directory silently found nothing.
    std::string path = track.name;
    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos &&
        playing_index_ >= 0 && static_cast<std::size_t>(playing_index_) < catalog_.items.size()) {
      path = join(dirname(catalog_.items[static_cast<std::size_t>(playing_index_)].path), path);
    }
    std::string text;
    if (read_file(path, &text)) {
      subtitle_cues_ = parse_cues(text);
      notify(track.label.empty() ? "Subtitles on" : track.label);
      LOG_INFO("player: subtitles %s (%zu cues)", path.c_str(), subtitle_cues_.size());
    } else {
      subtitle_track_ = -1;
      notify("Could not read " + path);
      LOG_ERROR("player: cannot read subtitle file %s", path.c_str());
    }
  } else {
    notify("Subtitles off");
  }
  return subtitle_track_;
}

/* --------------------------------- ticking -------------------------------- */

bool App::tick(double now) {
  const bool published = apply_scan_publish();
  flush_hover_priority(now);
  if (!player_open_) {
    // The hover preview's engine runs on its own thread. This thread only copies a
    // finished frame, so a slow file open cannot freeze the window.
    if (preview_index_ < 0) return published;
    bool changed = false;
    {
      std::lock_guard<std::mutex> lock(preview_mu_);
      if (preview_failed_generation_ != 0 && preview_failed_generation_ == preview_generation_.load()) {
        preview_index_ = -1;
        preview_has_frame_ = false;
        preview_failed_generation_ = 0;
        return true;
      }
      if (preview_published_) {
        preview_frame_ = std::move(preview_published_frame_);
        preview_has_frame_ = true;
        preview_published_ = false;
        changed = true;
      }
    }
    return changed || published;
  }
  bool changed = false;

  Frame frame;
  if (engine_.take_frame(&frame)) {
    presenter_.push(std::move(frame), now);
  }
  if (presenter_.advance(now)) changed = true;

  const EngineStatus status = engine_.status();
  const PlayState previous_state = player_status_.state;
  const double previous_time = player_status_.time;
  const std::string previous_cue = active_subtitle_text(previous_time);
  player_status_ = status;
  // The clock and the subtitle line change between pictures. Redraw for those, and
  // only those, so a held frame does not get scaled again every few milliseconds.
  if (chrome_visible_ && static_cast<int>(status.time) != static_cast<int>(previous_time)) {
    changed = true;
  }
  if (active_subtitle_text(status.time) != previous_cue) changed = true;

  // Resume, once the length is known: a seek before the demuxer reports a duration is
  // a seek into nothing.
  if (resume_at_ >= 0 && status.length > 0) {
    engine_.seek(resume_at_);
    LOG_INFO("player: resumed at %.1f s of %.1f s", resume_at_, status.length);
    resume_at_ = -1;
    changed = true;
  }

  if (status.state == PlayState::Ended && !player_finished_ && !player_failed_) {
    if (status.frames_decoded == 0) {
      // A file libVLC "plays" and immediately ends without one decoded picture is a
      // broken file, not a finished film. Finishing it would write it as watched and
      // drop it from Continue Watching - the film would vanish because it was corrupt.
      player_error_ = "This file ended without producing a single picture: " +
                      (playing_index_ >= 0 &&
                               static_cast<std::size_t>(playing_index_) < catalog_.items.size()
                           ? catalog_.items[static_cast<std::size_t>(playing_index_)].filename
                           : std::string("this file"));
      player_failed_ = true;
      notify(player_error_);
      LOG_ERROR("player: ended with no frames, treating as failure: %s", player_error_.c_str());
    } else {
      finish_film();
    }
    changed = true;
  }
  if (status.state == PlayState::Error && player_error_.empty()) {
    player_error_ = status.error.empty() ? "the engine reported an error" : status.error;
    player_failed_ = true;
    notify(player_error_);
    LOG_ERROR("player: %s", player_error_.c_str());
    changed = true;
  }

  // A file libVLC accepts but cannot decode never reports an error on its own: it
  // sits in "playing" with no pictures. Six seconds of that is a failure, and saying
  // so is the difference between a bug report and a mystery.
  if (status.state == PlayState::Playing && status.frames_decoded == 0 && player_error_.empty() &&
      playback_started_at_ > 0 && now - playback_started_at_ > 6.0) {
    player_error_ = "The engine produced no picture for 6 seconds: " +
                      (playing_index_ >= 0 &&
                               static_cast<std::size_t>(playing_index_) < catalog_.items.size()
                           ? catalog_.items[static_cast<std::size_t>(playing_index_)].filename
                           : std::string("this file"));
    notify(player_error_);
    LOG_ERROR("player: no picture after 6 s on %s", player_error_.c_str());
    player_failed_ = true;
    engine_.pause();
    changed = true;
  }

  // Progress, every few seconds and not while paused at the very start.
  if (status.state == PlayState::Playing && status.length > 0 &&
      now - last_progress_save_ >= kProgressSaveInterval &&
      playing_index_ >= 0 && static_cast<std::size_t>(playing_index_) < catalog_.items.size()) {
    last_progress_save_ = now;
    const Item& item = catalog_.items[static_cast<std::size_t>(playing_index_)];
    store_.set_progress(item.id, status.time, status.length);
    progress_ = store_.load_progress();
  }

  // The chrome hides itself while the film plays and comes back on any input.
  if (chrome_visible_ && status.state == PlayState::Playing && !scrub_dragging_ &&
      now - last_input_at_ > kChromeHideAfter) {
    chrome_visible_ = false;
    changed = true;
  }
  if (!chrome_visible_ && status.state != PlayState::Playing) {
    chrome_visible_ = true;
    changed = true;
  }
  if (player_failed_ && player_status_.state != PlayState::Error) {
    // The engine may still claim it is playing; from the user's point of view the
    // player has failed, and the state machine has to say the same thing as the
    // message on screen.
    player_status_.state = PlayState::Error;
    player_status_.error = player_error_;
  }
  if (previous_state != player_status_.state) changed = true;
  return changed || published;
}

/* --------------------------------- drawing -------------------------------- */

void App::draw_player_frame(Canvas& canvas) {
  canvas.clear(rgb(0x000000));
  if (!player_open_) return;

  // The picture, letterboxed into the window: aspect is preserved, never stretched.
  const Frame& frame = presenter_.current();
  const Rect area{0, 0, static_cast<float>(canvas.width()), static_cast<float>(canvas.height())};
  if (presenter_.has_frame() && frame.width > 0 && frame.height > 0) {
    const float video_aspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
    const float area_aspect = area.w / area.h;
    float w = area.w;
    float h = area.h;
    if (video_aspect > area_aspect) {
      h = area.w / video_aspect;
    } else {
      w = area.h * video_aspect;
    }
    const Rect picture{(area.w - w) / 2, (area.h - h) / 2, w, h};
    canvas.blit_raw(frame.pixels.data(), frame.width, frame.height, picture, false);
  } else if (player_error_.empty()) {
    regular_.draw_centered(canvas, "Buffering…", area, theme::kFontBody, theme::faint());
  }

  // Subtitles: drawn by us, timed off the engine's clock, which is the same clock the
  // picture came from. No <video>, no ::cue, no second timeline to drift away from.
  const std::string cue_text = active_subtitle_text(player_status_.time);
  if (!cue_text.empty()) {
    // Centre a box sized to the text, in the lower third but clear of the chrome.
    const float size = theme::kFontBody + 4;
    const float max_width = area.w * 0.72f;
    const std::vector<std::string> lines = regular_.wrap(cue_text, size, max_width);
    float widest = 0;
    for (const auto& line : lines) {
      widest = std::max(widest, regular_.measure(line, size).width);
    }
    const float line_height = regular_.line_height(size);
    const float box_height = line_height * static_cast<float>(lines.size()) + 18;
    const float box_width = widest + 32;
    const Rect box{(area.w - box_width) / 2, area.h * 0.78f - box_height / 2, box_width,
                   box_height};
    canvas.fill_round_rect(box, 6, rgba(0x000000, 165));
    float line_y = box.y + 9;
    for (const auto& line : lines) {
      regular_.draw_centered(canvas, line,
                             Rect{box.x, line_y, box.w, line_height}, size, rgb(0xffffff));
      line_y += line_height;
    }
  }

  if (player_finished_) {
    draw_end_card(canvas);
    return;
  }
  // The way out stays on screen after the rest of the chrome fades, the same rule
  // as the web player.
  draw_player_back(canvas);
  if (chrome_visible_) draw_chrome(canvas);
}

void App::draw_end_card(Canvas& canvas) {
  const float width = static_cast<float>(width_);
  const float height = static_cast<float>(height_);
  // Two layers: dim the last picture, then a solid plate behind the text. One layer
  // of 190 alpha left the old chrome legible underneath, which looked like a bug.
  canvas.fill_rect(Rect{0, 0, width, height}, rgba(0x000000, 170));
  const float plate_h = 220;
  const Rect plate{0, height / 2 - 120, width, plate_h};
  canvas.fill_gradient_h(Rect{0, plate.y, width * 0.25f, plate.h}, rgba(0x000000, 0),
                         rgba(0x000000, 220));
  canvas.fill_rect(Rect{width * 0.25f, plate.y, width * 0.5f, plate.h}, rgba(0x000000, 220));
  canvas.fill_gradient_h(Rect{width * 0.75f, plate.y, width * 0.25f, plate.h},
                         rgba(0x000000, 220), rgba(0x000000, 0));
  const Item* item = playing_index_ >= 0 && static_cast<std::size_t>(playing_index_) < catalog_.items.size()
                         ? &catalog_.items[static_cast<std::size_t>(playing_index_)]
                         : nullptr;
  bold_.draw_centered(canvas, item ? item->title : "Finished",
                      Rect{0, height / 2 - 90, width, 48}, theme::kFontLabel, theme::text());
  regular_.draw_centered(canvas, "Finished — it has left Continue Watching",
                         Rect{0, height / 2 - 40, width, 24}, theme::kFontBody, theme::dim());
  const float button_w = button_width("Play again", true);
  const float gap = 12;
  const float total = button_w * 2 + gap;
  const float x = (width - total) / 2;
  const Rect again{x, height / 2 + 10, button_w, 44};
  draw_button(canvas, again, "Play again", true, Action{Action::Kind::Replay, -1, ""});
  const Rect close{x + button_w + gap, height / 2 + 10, button_width("Back to browse", false), 44};
  draw_button(canvas, close, "Back to browse", false, Action{Action::Kind::ClosePlayer, -1, ""});
}

void App::draw_player_back(Canvas& canvas) {
  const Rect back{22, 22, 42, 42};
  canvas.fill_circle(back.x + 21, back.y + 21, 21, rgba(0x000000, 140));
  regular_.draw_centered(canvas, "\xE2\x86\x90", back, 20, theme::text());
  add_hit(back, Action{Action::Kind::ClosePlayer, -1, ""});
}

void App::draw_chrome(Canvas& canvas) {
  const float width = static_cast<float>(width_);
  const float height = static_cast<float>(height_);
  const Item* item = playing_index_ >= 0 && static_cast<std::size_t>(playing_index_) < catalog_.items.size()
                         ? &catalog_.items[static_cast<std::size_t>(playing_index_)]
                         : nullptr;
  const bool playing = player_status_.state == PlayState::Playing;

  // The web chrome is a fade, not a bar: dark at the top and the bottom, clear
  // across the picture.
  canvas.fill_gradient_v(Rect{0, 0, width, height * 0.22f}, rgba(0x000000, 184), rgba(0x000000, 0));
  canvas.fill_gradient_v(Rect{0, height * 0.62f, width, height * 0.38f}, rgba(0x000000, 0),
                         rgba(0x000000, 224));

  if (item) {
    bold_.draw(canvas, bold_.ellipsize(item->title, 15.0f, width - 280), 80, 30, 15.0f, theme::text());
  }

  // Sync stays, because the native player still needs it, but it is a small pair of
  // buttons rather than the old labelled chips. The web player has no equivalent.
  const float margin = 22.0f;
  const Rect sync_plus{width - margin - 28, 26, 28, 28};
  const std::string sync_text = describe_delay(manual_sync_ms_);
  const float sync_w = std::max(64.0f, regular_.measure(sync_text, 11.5f).width + 16);
  const Rect sync_chip{sync_plus.x - 6 - sync_w, 26, sync_w, 28};
  const Rect sync_minus{sync_chip.x - 6 - 28, 26, 28, 28};
  canvas.fill_round_rect(sync_minus, 14, rgba(0xffffff, 28));
  regular_.draw_centered(canvas, "\xE2\x88\x92", sync_minus, 14, theme::text());
  add_hit(sync_minus, Action{Action::Kind::NudgeSync, -1, "-20"});
  canvas.fill_round_rect(sync_chip, 14, rgba(0xffffff, 18));
  regular_.draw_centered(canvas, sync_text, sync_chip, 11.5f, theme::dim());
  canvas.fill_round_rect(sync_plus, 14, rgba(0xffffff, 28));
  regular_.draw_centered(canvas, "+", sync_plus, 14, theme::text());
  add_hit(sync_plus, Action{Action::Kind::NudgeSync, -1, "+20"});

  // Centre icon: pause bars while playing, a play triangle while paused. Only this
  // box is the button, not the whole picture.
  const Rect big{(width - 84.0f) / 2.0f, (height - 84.0f) / 2.0f, 84, 84};
  const int icon_alpha = playing ? 128 : 235;
  if (playing) {
    canvas.fill_round_rect(Rect{big.x + 24, big.y + 18, 12, 48}, 2, rgba(0xffffff, icon_alpha));
    canvas.fill_round_rect(Rect{big.x + 48, big.y + 18, 12, 48}, 2, rgba(0xffffff, icon_alpha));
  } else {
    canvas.fill_triangle_right(Rect{big.x + 30, big.y + 20, 28, 44}, rgba(0xffffff, icon_alpha));
  }
  add_hit(big, Action{Action::Kind::TogglePause, -1, ""});

  const float edge = 26.0f;
  const float controls_y = height - 20.0f - 36.0f;
  const Rect track{edge, controls_y - 18.0f, width - 2 * edge, 4};
  player_seek_track_ = track;
  canvas.fill_round_rect(track, 2, rgba(0xffffff, 70));
  const double length = player_status_.length > 0 ? player_status_.length : 0;
  const double fraction = length > 0 ? std::clamp(player_status_.time / length, 0.0, 1.0) : 0.0;
  if (fraction > 0) {
    canvas.fill_round_rect(Rect{track.x, track.y, track.w * static_cast<float>(fraction), track.h}, 2,
                           theme::accent());
  }
  const float thumb_x = track.x + track.w * static_cast<float>(fraction);
  canvas.fill_circle(thumb_x, track.y + 2, 6.5f, rgb(0xffffff));
  add_hit(Rect{track.x, track.y - 10, track.w, 24}, Action{Action::Kind::Seek, -1, ""});

  auto pause_mark = [&](const Rect& box, int alpha) {
    if (playing) {
      canvas.fill_rect(Rect{box.x + 11, box.y + 10, 4, 16}, rgba(0xffffff, alpha));
      canvas.fill_rect(Rect{box.x + 19, box.y + 10, 4, 16}, rgba(0xffffff, alpha));
    } else {
      canvas.fill_triangle_right(Rect{box.x + 12, box.y + 10, 12, 16}, rgba(0xffffff, alpha));
    }
  };
  const Rect toggle{edge, controls_y, 36, 36};
  pause_mark(toggle, 255);
  add_hit(toggle, Action{Action::Kind::TogglePause, -1, ""});

  const Rect back10{toggle.right() + 4, controls_y, 42, 36};
  regular_.draw_centered(canvas, "\xE2\x88\x92" "10", back10, 13, theme::text());
  add_hit(back10, Action{Action::Kind::SeekRelative, -1, "-10"});
  const Rect fwd10{back10.right() + 2, controls_y, 42, 36};
  regular_.draw_centered(canvas, "+10", fwd10, 13, theme::text());
  add_hit(fwd10, Action{Action::Kind::SeekRelative, -1, "10"});

  const std::string clock = clock_text(player_status_.time) + " / " + clock_text(length);
  regular_.draw(canvas, clock, fwd10.right() + 10, controls_y + 10, 12.5f, rgb(0xe6e6ea));

  // Right cluster, from the edge inwards: fullscreen, volume, speaker, CC.
  const Rect full{width - edge - 36, controls_y, 36, 36};
  const float fx = full.x + 9;
  const float fy = full.y + 9;
  canvas.fill_rect(Rect{fx, fy, 8, 2}, theme::text());
  canvas.fill_rect(Rect{fx, fy, 2, 8}, theme::text());
  canvas.fill_rect(Rect{fx + 10, fy, 8, 2}, theme::text());
  canvas.fill_rect(Rect{fx + 16, fy, 2, 8}, theme::text());
  canvas.fill_rect(Rect{fx, fy + 16, 8, 2}, theme::text());
  canvas.fill_rect(Rect{fx, fy + 10, 2, 8}, theme::text());
  canvas.fill_rect(Rect{fx + 10, fy + 16, 8, 2}, theme::text());
  canvas.fill_rect(Rect{fx + 16, fy + 10, 2, 8}, theme::text());
  add_hit(full, Action{Action::Kind::ToggleFullscreen, -1, ""});

  const Rect vol_track{full.x - 12 - 92, controls_y + 16, 92, 4};
  player_volume_track_ = vol_track;
  canvas.fill_round_rect(vol_track, 2, rgba(0xffffff, 80));
  const float level = engine_.muted() ? 0.0f : std::clamp(engine_.volume() / 100.0f, 0.0f, 1.0f);
  if (level > 0) {
    canvas.fill_round_rect(Rect{vol_track.x, vol_track.y, vol_track.w * level, vol_track.h}, 2,
                           rgb(0xffffff));
  }
  canvas.fill_circle(vol_track.x + vol_track.w * level, vol_track.y + 2, 5.5f, rgb(0xffffff));
  add_hit(Rect{vol_track.x - 4, vol_track.y - 8, vol_track.w + 8, 20},
          Action{Action::Kind::Volume, -1, ""});

  const Rect speaker{vol_track.x - 8 - 36, controls_y, 36, 36};
  const float sx = speaker.x + 8;
  const float sy = speaker.y + 11;
  canvas.fill_rect(Rect{sx, sy + 4, 5, 8}, theme::text());
  canvas.fill_triangle_right(Rect{sx + 5, sy, 8, 16}, theme::text());
  if (!engine_.muted()) {
    // Only the right half of the rings, so they read as sound waves rather than a target.
    canvas.push_clip(Rect{sx + 13, sy - 2, 16, 22});
    canvas.stroke_circle(sx + 12, sy + 8, 5, 1.4f, theme::text());
    canvas.stroke_circle(sx + 12, sy + 8, 8, 1.4f, theme::text());
    canvas.pop_clip();
  }
  add_hit(speaker, Action{Action::Kind::Mute, -1, ""});

  const bool subs_on = subtitle_track_ >= 0;
  const Rect cc{speaker.x - 8 - 36, controls_y + 4, 34, 28};
  if (subs_on) canvas.fill_round_rect(cc, 4, rgb(0xe50914));
  else canvas.stroke_round_rect(cc, 4, 1.0f, rgba(0xffffff, 90));
  bold_.draw_centered(canvas, "CC", cc, 11, theme::text());
  add_hit(cc, Action{Action::Kind::CycleSubtitles, -1, ""});

  if (!player_error_.empty()) {
    const float text_w = regular_.measure(player_error_, 12.5f).width;
    const Rect pill{(width - std::min(text_w + 36, width - 80)) / 2, height - 118,
                    std::min(text_w + 36, width - 80), 36};
    canvas.fill_round_rect(pill, 18, rgba(0xffe4e4, 230));
    regular_.draw_centered(canvas, regular_.ellipsize(player_error_, 12.5f, pill.w - 24), pill, 12.5f,
                           rgb(0x8a1f1f));
  }
}

/* --------------------------------- input ---------------------------------- */

bool App::handle_player(const InputEvent& event) {
  if (!player_open_) return false;
  last_input_at_ = clock_now();
  if (!chrome_visible_) {
    chrome_visible_ = true;
    // A pointer moving only brings the chrome back; a key should also do what it
    // says, or pressing space to pause would need two presses.
    if (event.type == InputEvent::Type::MouseMove) return true;
  }

  switch (event.type) {
    case InputEvent::Type::MouseMove:
      return false;
    case InputEvent::Type::MouseDown: {
      const Action action = hit_test(event.x, event.y);
      switch (action.kind) {
        case Action::Kind::Seek: {
          const double length = player_status_.length;
          if (length <= 0 || player_seek_track_.w <= 1) return false;
          const double target =
              std::clamp(static_cast<double>((event.x - player_seek_track_.x) / player_seek_track_.w), 0.0,
                         1.0) *
              length;
          engine_.seek(target);
          return true;
        }
        case Action::Kind::SeekRelative:
          engine_.seek_relative(std::atof(action.value.c_str()));
          return true;
        case Action::Kind::TogglePause:
          engine_.toggle_pause();
          player_status_ = engine_.status();
          return true;
        case Action::Kind::Mute:
          engine_.set_muted(!engine_.muted());
          return true;
        case Action::Kind::Volume:
          if (player_volume_track_.w <= 1) return false;
          engine_.set_volume(static_cast<int>(
              std::clamp((event.x - player_volume_track_.x) / player_volume_track_.w, 0.0f, 1.0f) * 100.0f));
          return true;
        case Action::Kind::ToggleFullscreen:
          if (on_toggle_fullscreen) on_toggle_fullscreen();
          return true;
        case Action::Kind::CycleSubtitles:
          cycle_subtitle_track(1);
          return true;
        case Action::Kind::NudgeSync:
          nudge_sync(action.value == "-20" ? -20 : 20);
          return true;
        case Action::Kind::ClosePlayer:
          stop_playback();
          return true;
        case Action::Kind::Replay: {
          const int index = playing_index_;
          if (index >= 0) {
            store_.set_progress(catalog_.items[static_cast<std::size_t>(index)].id, 0, 1);
            progress_ = store_.load_progress();
            start_playback(index);
          }
          return true;
        }
        case Action::Kind::OpenDetail:
          stop_playback();
          open_item_ = playing_index_;
          return true;
        default:
          return false;
      }
    }
    case InputEvent::Type::Key: {
      switch (event.key) {
        case kKeyEscape:
          stop_playback();
          return true;
        case kKeySpace:
          engine_.toggle_pause();
          player_status_ = engine_.status();
          return true;
        case kKeyLeft:
          engine_.seek_relative(-10);
          return true;
        case kKeyRight:
          engine_.seek_relative(10);
          return true;
        case kKeyUp:
          engine_.set_volume(engine_.volume() + 5);
          return true;
        case kKeyDown:
          engine_.set_volume(engine_.volume() - 5);
          return true;
        case kKeyM:
          engine_.set_muted(!engine_.muted());
          return true;
        case kKeyS:
          cycle_subtitle_track(1);
          return true;
        case kKeyLeftBracket:
          nudge_sync(-20);
          return true;
        case kKeyRightBracket:
          nudge_sync(20);
          return true;
        case kKeyF: {
          // Fullscreen is the window's business; the UI only asks for it.
          if (on_toggle_fullscreen) on_toggle_fullscreen();
          return true;
        }
        default:
          return false;
      }
    }
    case InputEvent::Type::Wheel:
      engine_.set_volume(engine_.volume() + (event.wheel > 0 ? 5 : -5));
      return true;
    default:
      return false;
  }
}

}  // namespace ui
}  // namespace vf
