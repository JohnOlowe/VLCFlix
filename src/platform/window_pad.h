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

// Scroll the Windows hook has already collected (ELAN WM_VSCROLL, pointer wheel).
// The hook cannot safely inject an SDL event from inside the window procedure, so
// the poll loop takes the notches from here. Returns false when nothing is waiting.
bool pad_take_scroll(float* out_x, float* out_y, float* mouse_x, float* mouse_y);

}  // namespace vf

#endif  // VLCFLIX_PLATFORM_WINDOW_PAD_H
