#include "playback/engine.h"

#include <vlc/vlc.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "core/log.h"

namespace vf {

const char* play_state_name(PlayState state) {
  switch (state) {
    case PlayState::Empty: return "empty";
    case PlayState::Opening: return "opening";
    case PlayState::Playing: return "playing";
    case PlayState::Paused: return "paused";
    case PlayState::Ended: return "ended";
    case PlayState::Error: return "error";
  }
  return "?";
}

namespace {
double steady_now() {
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

struct Engine::Impl {
  libvlc_instance_t* instance = nullptr;
  libvlc_media_t* media = nullptr;
  libvlc_media_player_t* player = nullptr;
  libvlc_event_manager_t* events = nullptr;

  // The buffer libVLC writes into. Only valid between lock() and unlock().
  std::vector<uint8_t> write_buffer;
  int width = 0;
  int height = 0;
  std::size_t pitch = 0;

  mutable std::mutex frame_mutex;
  Frame pending;         // decoded but not yet taken by the UI
  bool has_pending = false;
  std::atomic<long long> frames_decoded{0};
  std::atomic<long long> frames_delivered{0};

  std::atomic<PlayState> state{PlayState::Empty};
  std::atomic<bool> ended{false};
  std::atomic<bool> had_error{false};
  mutable std::mutex error_mutex;
  std::string error_text;
  Engine::Clock clock;
};

namespace {

void* lock_cb(void* opaque, void** planes) {
  auto* impl = static_cast<Engine::Impl*>(opaque);
  planes[0] = impl->write_buffer.data();
  return impl->write_buffer.data();
}

void unlock_cb(void* opaque, void* /*picture*/, void* const* /*planes*/) {
  auto* impl = static_cast<Engine::Impl*>(opaque);
  // Copy out of libVLC's buffer: after unlock it may be reused at any moment.
  std::lock_guard<std::mutex> guard(impl->frame_mutex);
  impl->pending.pixels.assign(impl->write_buffer.begin(), impl->write_buffer.end());
  impl->pending.width = impl->width;
  impl->pending.height = impl->height;
  impl->pending.arrival = impl->clock ? impl->clock() : steady_now();
  impl->pending.media_time = 0;  // filled in by Engine::take_frame from the engine clock
  impl->has_pending = true;
  impl->frames_decoded.fetch_add(1);
}

void display_cb(void* /*opaque*/, void* /*picture*/) {}

void on_event(const libvlc_event_t* event, void* opaque) {
  auto* impl = static_cast<Engine::Impl*>(opaque);
  switch (event->type) {
    case libvlc_MediaPlayerEndReached:
      impl->state.store(PlayState::Ended);
      impl->ended.store(true);
      break;
    case libvlc_MediaPlayerEncounteredError:
      impl->had_error.store(true);
      impl->state.store(PlayState::Error);
      break;
    case libvlc_MediaPlayerStopped:
      if (impl->state.load() != PlayState::Ended) impl->state.store(PlayState::Empty);
      break;
    default:
      break;
  }
}

}  // namespace

Engine::Engine() {
  impl_ = new Impl();
  impl_->clock = [] { return steady_now(); };
}

void Engine::set_clock(Clock clock) {
  clock_ = std::move(clock);
  if (impl_) impl_->clock = [this] { return clock_ ? clock_() : steady_now(); };
}

Engine::~Engine() {
  stop();
  if (!impl_) return;
  if (impl_->player) {
    libvlc_media_player_release(impl_->player);
    impl_->player = nullptr;
  }
  if (impl_->media) {
    libvlc_media_release(impl_->media);
    impl_->media = nullptr;
  }
  if (impl_->instance) {
    libvlc_release(impl_->instance);
    impl_->instance = nullptr;
  }
  delete impl_;
  impl_ = nullptr;
}

void Engine::configure(int width, int height) {
  decode_width_ = std::max(160, width & ~1);
  decode_height_ = std::max(120, height & ~1);
}

void Engine::add_argument(const std::string& argument) { arguments_.push_back(argument); }

bool Engine::open(const std::string& path, std::string* error) {
  stop();
  path_ = path;
  media_open_ = false;

  if (!impl_->instance) {
    // No window and no video output: we present the frames ourselves, so libVLC must
    // not try to open one (which would fail on a headless machine).
    std::vector<std::string> args = {
        "--no-video-title-show", "--no-osd", "--no-snapshot-preview", "--quiet",
        "--intf=dummy", "--vout=dummy"};
    for (const auto& extra : arguments_) args.push_back(extra);
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) argv.push_back(arg.c_str());
    impl_->instance = libvlc_new(static_cast<int>(argv.size()), argv.data());
    if (!impl_->instance) {
      if (error) *error = "libvlc_new failed (is libvlc installed?)";
      LOG_ERROR("engine: libvlc_new failed");
      return false;
    }
    LOG_INFO("engine: libVLC %s", libvlc_get_version());
  }

  impl_->media = libvlc_media_new_path(impl_->instance, path.c_str());
  if (!impl_->media) {
    if (error) *error = "libVLC could not open " + path;
    LOG_ERROR("engine: libvlc_media_new_path failed for %s", path.c_str());
    return false;
  }
  // Local files only: no network access, no metadata scraping, nothing that can hang
  // the window on a slow disk behind a remote path.
  libvlc_media_add_option(impl_->media, ":no-sub-autodetect-file");

  impl_->player = libvlc_media_player_new_from_media(impl_->media);
  if (!impl_->player) {
    if (error) *error = "libVLC could not create a media player";
    return false;
  }

  impl_->width = decode_width_;
  impl_->height = decode_height_;
  impl_->pitch = static_cast<std::size_t>(impl_->width) * 4;
  impl_->write_buffer.assign(impl_->pitch * static_cast<std::size_t>(impl_->height), 0);

  libvlc_video_set_callbacks(impl_->player, lock_cb, unlock_cb, display_cb, impl_);
  libvlc_video_set_format(impl_->player, "RV32", static_cast<unsigned>(impl_->width),
                          static_cast<unsigned>(impl_->height),
                          static_cast<unsigned>(impl_->pitch));

  impl_->events = libvlc_media_player_event_manager(impl_->player);
  if (impl_->events) {
    libvlc_event_attach(impl_->events, libvlc_MediaPlayerEndReached, on_event, impl_);
    libvlc_event_attach(impl_->events, libvlc_MediaPlayerEncounteredError, on_event, impl_);
    libvlc_event_attach(impl_->events, libvlc_MediaPlayerStopped, on_event, impl_);
  }

  impl_->state.store(PlayState::Opening);
  impl_->ended.store(false);
  impl_->had_error.store(false);
  impl_->frames_decoded.store(0);
  impl_->frames_delivered.store(0);
  {
    std::lock_guard<std::mutex> guard(impl_->frame_mutex);
    impl_->has_pending = false;
  }
  media_open_ = true;
  LOG_INFO("engine: opened %s at %dx%d", path.c_str(), impl_->width, impl_->height);
  return true;
}

void Engine::play() {
  if (!impl_ || !impl_->player || !media_open_) return;
  if (libvlc_media_player_play(impl_->player) != 0) {
    impl_->state.store(PlayState::Error);
    LOG_ERROR("engine: play failed for %s", path_.c_str());
    return;
  }
  impl_->state.store(PlayState::Playing);
  libvlc_audio_set_volume(impl_->player, volume_);
  libvlc_media_player_set_rate(impl_->player, static_cast<float>(rate_));
  if (audio_delay_ms_ != 0) {
    libvlc_audio_set_delay(impl_->player, static_cast<int64_t>(audio_delay_ms_) * 1000);
  }
  if (muted_) libvlc_audio_set_mute(impl_->player, 1);
}

void Engine::pause() {
  if (!impl_ || !impl_->player) return;
  libvlc_media_player_set_pause(impl_->player, 1);
  impl_->state.store(PlayState::Paused);
}

void Engine::toggle_pause() {
  const PlayState state = impl_ ? impl_->state.load() : PlayState::Empty;
  if (state == PlayState::Paused) {
    libvlc_media_player_set_pause(impl_->player, 0);
    impl_->state.store(PlayState::Playing);
  } else if (state == PlayState::Playing) {
    pause();
  }
}

void Engine::stop() {
  if (!impl_) return;
  if (impl_->player) {
    libvlc_media_player_stop(impl_->player);
    if (impl_->events) {
      libvlc_event_detach(impl_->events, libvlc_MediaPlayerEndReached, on_event, impl_);
      libvlc_event_detach(impl_->events, libvlc_MediaPlayerEncounteredError, on_event, impl_);
      libvlc_event_detach(impl_->events, libvlc_MediaPlayerStopped, on_event, impl_);
      impl_->events = nullptr;
    }
    libvlc_media_player_release(impl_->player);
    impl_->player = nullptr;
  }
  if (impl_->media) {
    libvlc_media_release(impl_->media);
    impl_->media = nullptr;
  }
  {
    std::lock_guard<std::mutex> guard(impl_->frame_mutex);
    impl_->has_pending = false;
    impl_->pending.pixels.clear();
  }
  impl_->write_buffer.clear();
  impl_->write_buffer.shrink_to_fit();
  impl_->state.store(PlayState::Empty);
  media_open_ = false;
}

void Engine::seek(double seconds) {
  if (!impl_ || !impl_->player || !media_open_) return;
  const double length = status().length;
  const double target = std::max(0.0, length > 0 ? std::min(seconds, length - 0.25) : seconds);
  libvlc_media_player_set_time(impl_->player, static_cast<libvlc_time_t>(target * 1000));
  LOG_INFO("engine: seek to %.2f s", target);
}

void Engine::seek_relative(double delta) { seek(status().time + delta); }

void Engine::set_rate(double rate) {
  rate_ = std::clamp(rate, 0.25, 4.0);
  if (impl_ && impl_->player) libvlc_media_player_set_rate(impl_->player, static_cast<float>(rate_));
}

void Engine::set_volume(int volume) {
  volume_ = std::clamp(volume, 0, 100);
  if (impl_ && impl_->player) libvlc_audio_set_volume(impl_->player, volume_);
}

void Engine::set_muted(bool muted) {
  muted_ = muted;
  if (impl_ && impl_->player) libvlc_audio_set_mute(impl_->player, muted ? 1 : 0);
}

void Engine::set_audio_delay_ms(int delay_ms) {
  audio_delay_ms_ = delay_ms;
  if (impl_ && impl_->player) {
    libvlc_audio_set_delay(impl_->player, static_cast<int64_t>(delay_ms) * 1000);
  }
}

bool Engine::take_frame(Frame* out) {
  if (!impl_ || !out) return false;
  std::lock_guard<std::mutex> guard(impl_->frame_mutex);
  if (!impl_->has_pending) return false;
  out->pixels.swap(impl_->pending.pixels);
  out->width = impl_->pending.width;
  out->height = impl_->pending.height;
  out->arrival = impl_->pending.arrival;
  impl_->pending.pixels.clear();
  impl_->has_pending = false;
  impl_->frames_delivered.fetch_add(1);
  return true;
}

EngineStatus Engine::status() const {
  EngineStatus status;
  if (!impl_) return status;
  status.state = impl_->state.load();
  status.frames_decoded = impl_->frames_decoded.load();
  status.frames_delivered = impl_->frames_delivered.load();
  if (impl_->width) {
    status.video_width = impl_->width;
    status.video_height = impl_->height;
  }
  if (impl_ && impl_->player) {
    const libvlc_time_t time_ms = libvlc_media_player_get_time(impl_->player);
    const libvlc_time_t length_ms = libvlc_media_player_get_length(impl_->player);
    status.time = time_ms > 0 ? time_ms / 1000.0 : 0;
    status.length = length_ms > 0 ? length_ms / 1000.0 : 0;
    const libvlc_state_t state = libvlc_media_player_get_state(impl_->player);
    if (status.state != PlayState::Error) {
      switch (state) {
        case libvlc_Ended: status.state = PlayState::Ended; break;
        case libvlc_Playing: status.state = PlayState::Playing; break;
        case libvlc_Paused: status.state = PlayState::Paused; break;
        case libvlc_Error:
          status.state = PlayState::Error;
          status.error = "the engine could not decode this file";
          break;
        default: break;
      }
    }
  }
  if (impl_->had_error.load() && status.error.empty()) status.error = "playback error";
  return status;
}

}  // namespace vf
