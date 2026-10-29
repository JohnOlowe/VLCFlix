// Lenovo ELAN / precision-touchpad two-finger drag, which does not arrive as a wheel.
//
// A physical mouse wheel is WM_MOUSEWHEEL, and SDL turns that into SDL_MOUSEWHEEL.
// "Drag two fingers to scroll" on an ELAN pad is a different path, and this app was
// only listening to the first one. The JS UI scrolls because Chromium handles all of
// them. The details are in window_pad.cpp. Everywhere else these functions do nothing.
#ifndef VLCFLIX_PLATFORM_WINDOW_PAD_H
#define VLCFLIX_PLATFORM_WINDOW_PAD_H

struct SDL_Window;
union SDL_Event;

namespace vf {

void pad_install(SDL_Window* window);
void pad_uninstall(SDL_Window* window);

// A wheel message just arrived. A two-finger drag that the driver also reports as
// touch contacts must not scroll a second time.
void pad_note_wheel();
bool pad_drop_wheel();

// True while a two-finger drag is in progress, so the touch-synthesized mouse
// events that SDL emits alongside it do not also click whatever is under the cursor.
bool pad_ignore_touch_mouse(unsigned int which);

// True when this finger event is a two-finger drag worth scrolling. Writes notches
// in the same units as SDL_MOUSEWHEEL's preciseX/preciseY.
bool pad_finger_to_wheel(const SDL_Event& event, int window_w, int window_h, float* out_x,
                         float* out_y);

}  // namespace vf

#endif  // VLCFLIX_PLATFORM_WINDOW_PAD_H
