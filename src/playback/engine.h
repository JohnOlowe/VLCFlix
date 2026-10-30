#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace vf {

enum class PlayState { Empty, Opening, Playing, Paused, Ended, Error };

const char* play_state_name(PlayState state);

struct EngineStatus {
  PlayState state = PlayState::Empty;
  double time = 0;      // seconds, from the engine's clock
  double length = 0;    // seconds, 0 until the engine knows
  int video_width = 0;
  int video_height = 0;
  long long frames_decoded = 0;
  long long frames_delivered = 0;
  std::string error;
};

// A single decoded picture. Frames are copied out of libVLC's buffer, because the
// buffer is only ours between lock and unlock.
struct Frame {
  std::vector<uint8_t> pixels;  // RV32, i.e. B,G,R,X in memory
  int width = 0;
  int height = 0;
  double arrival = 0;  // seconds on the injected clock
  double media_time = 0;
};

class Engine {
 public:
  struct Impl;  // defined in the .cpp; public so the C callbacks can reach it

  // `width`/`height` is the decode size we ask libVLC for. Frames are scaled to it by
  // libVLC's converter, which keeps our presenter's memory bounded and predictable.
  Engine();
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void configure(int width, int height);
  // Extra libVLC arguments, used by tests to keep the machine quiet (--aout=dummy).
  void add_argument(const std::string& argument);
  // Creates the libVLC instance without opening a file. The first creation loads
  // every plugin and can take seconds; doing it off the window thread is what keeps
  // the first hover from being reported as Not Responding.
  bool warm();

  bool open(const std::string& path, std::string* error);
  void play();
  void pause();
  void toggle_pause();
  void stop();
  void seek(double seconds);
  void seek_relative(double delta);
  void set_rate(double rate);
  double rate() const { return rate_; }
  void set_volume(int volume);  // 0..100
  int volume() const { return volume_; }
  void set_muted(bool muted);
  bool muted() const { return muted_; }
  // Audio delay in milliseconds, positive = sound later than the picture. This is the
  // half of the sync plan libVLC can apply itself.
  void set_audio_delay_ms(int delay_ms);
  int audio_delay_ms() const { return audio_delay_ms_; }

  // Pulls the newest decoded picture, if one has arrived since the last call.
  // Returns false when there is nothing new to show.
  bool take_frame(Frame* out);

  EngineStatus status() const;
  bool has_media() const { return media_open_; }
  const std::string& path() const { return path_; }

  // Steady clock, seconds. Injectable so the presenter and the tests can share one
  // notion of time and a test does not have to sleep.
  using Clock = std::function<double()>;
  void set_clock(Clock clock);

 private:
  Impl* impl_ = nullptr;
  Clock clock_;
  std::string path_;
  int decode_width_ = 960;
  int decode_height_ = 540;
  int volume_ = 100;
  int audio_delay_ms_ = 0;
  double rate_ = 1.0;
  bool muted_ = false;
  bool media_open_ = false;
  std::vector<std::string> arguments_;
};

}  // namespace vf
