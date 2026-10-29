#include "platform/window_pad.h"

#include "platform/wheel.h"

#ifndef _WIN32

namespace vf {

void pad_install(SDL_Window*) {}
void pad_uninstall(SDL_Window*) {}
void pad_note_wheel() {}
bool pad_drop_wheel() { return false; }
bool pad_ignore_touch_mouse(unsigned int) { return false; }
bool pad_finger_to_wheel(const SDL_Event&, int, int, float*, float*) { return false; }

}  // namespace vf

#else

#include <SDL.h>
#include <SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/log.h"

namespace vf {
namespace {

// Messages the SDK names, written out so a header that predates them still builds.
constexpr UINT kPointerWheel = 0x024E;                   // WM_POINTERWHEEL
constexpr UINT kPointerHWheel = 0x024F;                  // WM_POINTERHWHEEL
constexpr UINT kTabletQueryGesture = 0x02CC;             // WM_TABLET_QUERYSYSTEMGESTURESTATUS
constexpr UINT kGesture = 0x0119;                        // WM_GESTURE
constexpr DWORD kGesturePan = 4;                         // GID_PAN
constexpr DWORD kGestureBegin = 0x00000001;              // GF_BEGIN
// Press-and-hold stays off (it delays a click and turns it into a right click).
// Smooth scrolling and flicks stay ON. SDL's own answer to this message turns both
// off, and an ELAN driver that asks the window which gestures to honour then never
// generates the two-finger drag. A mouse wheel never asks, which is why the wheel
// worked and the gesture did not.
constexpr LRESULT kGestureStatus = 0x00000001 | 0x00000008 | 0x00000010;

constexpr Uint32 kDuplicateMs = 80;

struct Pad {
  HWND hwnd = nullptr;
  WNDPROC old_proc = nullptr;
  SDL_Window* window = nullptr;
  bool down_scrolls_up = true;
  struct Finger {
    SDL_FingerID id = 0;
    bool down = false;
  };
  Finger fingers[8];
  int finger_count = 0;
  Uint32 last_wheel_ms = 0;
  Uint32 last_finger_ms = 0;
  Uint32 suppress_touch_mouse_until = 0;
  bool panning = false;
  int pan_x = 0;
  int pan_y = 0;
};

Pad g_pad;

int signed_word(LPARAM value, bool high) {
  const DWORD word = high ? HIWORD(value) : LOWORD(value);
  return static_cast<short>(word);
}

int primary_finger() {
  for (int i = 0; i < 8; ++i) {
    if (g_pad.fingers[i].down) return i;
  }
  return -1;
}

int find_finger(SDL_FingerID id) {
  for (int i = 0; i < 8; ++i) {
    if (g_pad.fingers[i].down && g_pad.fingers[i].id == id) return i;
  }
  return -1;
}

void add_finger(SDL_FingerID id) {
  if (find_finger(id) >= 0) return;
  for (int i = 0; i < 8; ++i) {
    if (!g_pad.fingers[i].down) {
      g_pad.fingers[i].down = true;
      g_pad.fingers[i].id = id;
      ++g_pad.finger_count;
      return;
    }
  }
}

void remove_finger(SDL_FingerID id) {
  const int slot = find_finger(id);
  if (slot < 0) return;
  g_pad.fingers[slot].down = false;
  if (g_pad.finger_count > 0) --g_pad.finger_count;
}

bool recent(Uint32 then, Uint32 now) { return then != 0 && now - then < kDuplicateMs; }

void push_wheel(float dx, float dy, int mouse_x, int mouse_y) {
  if (!g_pad.window || (dx == 0.0f && dy == 0.0f)) return;
  SDL_Event ev{};
  ev.type = SDL_MOUSEWHEEL;
  ev.wheel.timestamp = SDL_GetTicks();
  ev.wheel.windowID = SDL_GetWindowID(g_pad.window);
  ev.wheel.preciseX = dx;
  ev.wheel.preciseY = dy;
  ev.wheel.x = dx >= 1.0f ? 1 : dx <= -1.0f ? -1 : 0;
  ev.wheel.y = dy >= 1.0f ? 1 : dy <= -1.0f ? -1 : 0;
  ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
  ev.wheel.mouseX = mouse_x;
  ev.wheel.mouseY = mouse_y;
  SDL_PushEvent(&ev);
  g_pad.last_wheel_ms = ev.wheel.timestamp;
}

void read_scroll_direction() {
  // 0 is "Down motion scrolls up", the Windows default. 0xFFFFFFFF is the other way.
  // A legacy ELAN pad has no such key; the default is what Chrome uses too.
  g_pad.down_scrolls_up = true;
  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER,
                    "Software\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad", 0, KEY_READ,
                    &key) != ERROR_SUCCESS) {
    return;
  }
  DWORD value = 0;
  DWORD size = sizeof value;
  if (RegQueryValueExA(key, "ScrollDirection", nullptr, nullptr, reinterpret_cast<BYTE*>(&value),
                       &size) == ERROR_SUCCESS) {
    g_pad.down_scrolls_up = value == 0;
  }
  RegCloseKey(key);
}

using GetGestureInfoFn = BOOL(WINAPI*)(HGESTUREINFO, PGESTUREINFO);
using CloseGestureFn = BOOL(WINAPI*)(HGESTUREINFO);

bool handle_pan(LPARAM lp) {
  HMODULE user = GetModuleHandleA("user32.dll");
  if (!user) return false;
  auto get_info = reinterpret_cast<GetGestureInfoFn>(GetProcAddress(user, "GetGestureInfo"));
  auto close_info = reinterpret_cast<CloseGestureFn>(GetProcAddress(user, "CloseGestureInfoHandle"));
  if (!get_info || !close_info) return false;
  GESTUREINFO info{};
  info.cbSize = sizeof info;
  const auto handle = reinterpret_cast<HGESTUREINFO>(lp);
  if (!get_info(handle, &info)) return false;
  // Not our gesture: leave the handle for DefWindowProc. Closing it here and then
  // forwarding the message frees it twice.
  if (info.dwID != kGesturePan) return false;
  {
    if (info.dwFlags & kGestureBegin) {
      g_pad.panning = true;
      g_pad.pan_x = info.ptsLocation.x;
      g_pad.pan_y = info.ptsLocation.y;
    } else if (g_pad.panning && g_pad.window) {
      int w = 0, h = 0;
      SDL_GetWindowSize(g_pad.window, &w, &h);
      const float dx = static_cast<float>(info.ptsLocation.x - g_pad.pan_x);
      const float dy = static_cast<float>(info.ptsLocation.y - g_pad.pan_y);
      g_pad.pan_x = info.ptsLocation.x;
      g_pad.pan_y = info.ptsLocation.y;
      if (w > 0 && h > 0 && (dx != 0.0f || dy != 0.0f)) {
        float ox = 0.0f, oy = 0.0f;
        touch_drag_to_wheel(dx / static_cast<float>(w), dy / static_cast<float>(h),
                            static_cast<float>(w), static_cast<float>(h), kTouchDragPixelsPerNotch,
                            g_pad.down_scrolls_up, &ox, &oy);
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(g_pad.hwnd, &cursor);
        push_wheel(ox, oy, cursor.x, cursor.y);
      }
    }
  }
  close_info(handle);
  return true;
}

LRESULT CALLBACK pad_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kTabletQueryGesture) return kGestureStatus;
  if (msg == kPointerWheel || msg == kPointerHWheel) {
    // Returning 0 tells Windows we took it, so DefWindowProc does not also
    // synthesize WM_MOUSEWHEEL. Doing both would scroll twice.
    const float notches = static_cast<float>(signed_word(static_cast<LPARAM>(wp), true)) / 120.0f;
    POINT pt{signed_word(lp, false), signed_word(lp, true)};
    ScreenToClient(hwnd, &pt);
    if (msg == kPointerWheel) push_wheel(0.0f, notches, pt.x, pt.y);
    else push_wheel(notches, 0.0f, pt.x, pt.y);
    return 0;
  }
  if (msg == kGesture && handle_pan(lp)) return 0;
  if (g_pad.old_proc) return CallWindowProc(g_pad.old_proc, hwnd, msg, wp, lp);
  return DefWindowProc(hwnd, msg, wp, lp);
}

}  // namespace

void pad_install(SDL_Window* window) {
  if (!window || g_pad.hwnd) return;
  SDL_SysWMinfo info{};
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
  g_pad.window = window;
  g_pad.hwnd = info.info.win.window;
  read_scroll_direction();
  g_pad.old_proc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtr(g_pad.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(pad_proc)));
  LOG_INFO("touchpad: two-finger drag hooked (scroll direction %s)",
           g_pad.down_scrolls_up ? "down-scrolls-up" : "down-scrolls-down");
}

void pad_uninstall(SDL_Window*) {
  if (g_pad.hwnd && g_pad.old_proc) {
    SetWindowLongPtr(g_pad.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_pad.old_proc));
  }
  g_pad = Pad{};
}

void pad_note_wheel() { g_pad.last_wheel_ms = SDL_GetTicks(); }

bool pad_drop_wheel() { return recent(g_pad.last_finger_ms, SDL_GetTicks()); }

bool pad_ignore_touch_mouse(unsigned int which) {
  if (which != SDL_TOUCH_MOUSEID) return false;
  if (g_pad.finger_count >= 2) return true;
  return SDL_GetTicks() < g_pad.suppress_touch_mouse_until;
}

bool pad_finger_to_wheel(const SDL_Event& event, int window_w, int window_h, float* out_x,
                         float* out_y) {
  if (event.type == SDL_FINGERDOWN) {
    add_finger(event.tfinger.fingerId);
    return false;
  }
  if (event.type == SDL_FINGERUP) {
    remove_finger(event.tfinger.fingerId);
    if (g_pad.finger_count < 2) g_pad.suppress_touch_mouse_until = SDL_GetTicks() + 120;
    return false;
  }
  if (event.type != SDL_FINGERMOTION) return false;
  add_finger(event.tfinger.fingerId);  // a move can arrive before the down
  if (g_pad.finger_count != 2) return false;
  if (find_finger(event.tfinger.fingerId) != primary_finger()) return false;
  if (recent(g_pad.last_wheel_ms, SDL_GetTicks())) return false;
  // The driver already queued a wheel for this same drag. Prefer that: its sign is
  // the one the user set, and scrolling both would move the page twice.
  SDL_Event queued{};
  if (SDL_PeepEvents(&queued, 1, SDL_PEEKEVENT, SDL_MOUSEWHEEL, SDL_MOUSEWHEEL) > 0) return false;
  if (window_w <= 0 || window_h <= 0) return false;
  float ox = 0.0f, oy = 0.0f;
  touch_drag_to_wheel(event.tfinger.dx, event.tfinger.dy, static_cast<float>(window_w),
                      static_cast<float>(window_h), kTouchDragPixelsPerNotch, g_pad.down_scrolls_up,
                      &ox, &oy);
  if (ox == 0.0f && oy == 0.0f) return false;
  if (out_x) *out_x = ox;
  if (out_y) *out_y = oy;
  g_pad.last_finger_ms = SDL_GetTicks();
  g_pad.suppress_touch_mouse_until = g_pad.last_finger_ms + 160;
  return true;
}

}  // namespace vf

#endif
