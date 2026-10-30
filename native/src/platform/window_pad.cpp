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
bool pad_take_scroll(float*, float*, float*, float*) { return false; }

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

constexpr UINT kVScroll = 0x0115;  // WM_VSCROLL
constexpr UINT kHScroll = 0x0114;  // WM_HSCROLL
constexpr UINT kMouseWheel = 0x020A;
constexpr UINT kMouseHWheel = 0x020E;
constexpr UINT kPointerUpdate = 0x0245;
constexpr UINT kPointerDown = 0x0246;
constexpr UINT kPointerUp = 0x0247;
constexpr DWORD kPointerFlagWheel = 0x00080000;
constexpr DWORD kPointerFlagHWheel = 0x00100000;
constexpr DWORD kPointerFlagInContact = 0x00000004;

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
  Uint32 last_consumed_ms = 0;
  Uint32 suppress_touch_mouse_until = 0;
  bool panning = false;
  int pan_x = 0;
  int pan_y = 0;
  float pending_x = 0.0f;
  float pending_y = 0.0f;
  float pending_mx = 0.0f;
  float pending_my = 0.0f;
  bool pending = false;
  bool logged_elan = false;
  struct Contact {
    UINT32 id = 0;
    int x = 0;
    int y = 0;
    bool down = false;
    bool seen = false;
  };
  Contact contacts[8];
  int contact_count = 0;
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

void remember_cursor(HWND hwnd, int* x, int* y) {
  POINT pt{};
  if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) return;
  *x = pt.x;
  *y = pt.y;
}

// Held here, not pushed into SDL. SDL_PushEvent from inside the window procedure
// (which is running because SDL is already pumping) drops the event on the floor,
// and returning 0 from WM_POINTERWHEEL also stops Windows from synthesizing a wheel.
// The poll loop collects this instead.
void hold_scroll(float dx, float dy, int mouse_x, int mouse_y) {
  if (dx == 0.0f && dy == 0.0f) return;
  g_pad.pending_x += dx;
  g_pad.pending_y += dy;
  g_pad.pending_mx = static_cast<float>(mouse_x);
  g_pad.pending_my = static_cast<float>(mouse_y);
  g_pad.pending = true;
  g_pad.last_wheel_ms = SDL_GetTicks();
  g_pad.last_consumed_ms = g_pad.last_wheel_ms;
}

float wheel_lines() {
  UINT lines = 3;
  if (!SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0) || lines == 0 || lines > 100) {
    return 3.0f;
  }
  return static_cast<float>(lines);
}

float page_notches() {
  int h = 0;
  if (g_pad.window) SDL_GetWindowSize(g_pad.window, nullptr, &h);
  // One notch moves the page by the UI's wheel step (96px). A page request should
  // move about one screen, not one line.
  return h > 96 ? static_cast<float>(h) / 96.0f : 4.0f;
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
        int cx = 0, cy = 0;
        remember_cursor(g_pad.hwnd, &cx, &cy);
        hold_scroll(ox, oy, cx, cy);
      }
    }
  }
  close_info(handle);
  return true;
}

#ifndef POINTER_FLAG_WHEEL
#define POINTER_FLAG_WHEEL 0x00080000
#endif
#ifndef POINTER_FLAG_HWHEEL
#define POINTER_FLAG_HWHEEL 0x00100000
#endif

// The official structure. Defined here so a header that predates pointer input still
// builds; the field order is the one GetPointerInfo writes.
struct PointerInfo {
  UINT32 pointerType;
  UINT32 pointerId;
  UINT32 frameId;
  UINT32 pointerFlags;
  HANDLE sourceDevice;
  HWND hwndTarget;
  POINT ptPixelLocation;
  POINT ptHimetricLocation;
  POINT ptPixelLocationRaw;
  POINT ptHimetricLocationRaw;
  DWORD dwTime;
  UINT32 historyCount;
  INT32 inputData;
  DWORD dwKeyStates;
  UINT64 performanceCount;
  UINT32 buttonChangeType;
};
#ifdef POINTER_FLAG_WHEEL
static_assert(POINTER_FLAG_WHEEL == 0x00080000, "wheel flag value");
static_assert(sizeof(PointerInfo) == sizeof(POINTER_INFO), "pointer info layout");
#endif

using GetPointerInfoFn = BOOL(WINAPI*)(UINT32, PointerInfo*);

GetPointerInfoFn pointer_info_fn() {
  static GetPointerInfoFn fn = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (HMODULE user = GetModuleHandleA("user32.dll")) {
      fn = reinterpret_cast<GetPointerInfoFn>(GetProcAddress(user, "GetPointerInfo"));
    }
  }
  return fn;
}

int contact_slot(UINT32 id, bool create) {
  for (int i = 0; i < 8; ++i) {
    if (g_pad.contacts[i].down && g_pad.contacts[i].id == id) return i;
  }
  if (!create) return -1;
  for (int i = 0; i < 8; ++i) {
    if (!g_pad.contacts[i].down) {
      g_pad.contacts[i].down = true;
      g_pad.contacts[i].seen = false;
      g_pad.contacts[i].id = id;
      ++g_pad.contact_count;
      return i;
    }
  }
  return -1;
}

void drop_contact(UINT32 id) {
  const int slot = contact_slot(id, false);
  if (slot < 0) return;
  g_pad.contacts[slot].down = false;
  g_pad.contacts[slot].seen = false;
  if (g_pad.contact_count > 0) --g_pad.contact_count;
}

int primary_contact() {
  for (int i = 0; i < 8; ++i) {
    if (g_pad.contacts[i].down) return i;
  }
  return -1;
}

// Precision touchpads put the wheel delta on the pointer update, in InputData, and
// never send WM_MOUSEWHEEL. Two contacts with no wheel flag are the same gesture
// reported as fingers. Either one has to become a scroll or the drag does nothing.
void consume_pointer(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const UINT32 id = static_cast<UINT32>(LOWORD(wp));
  int cx = signed_word(lp, false);
  int cy = signed_word(lp, true);
  POINT pt{cx, cy};
  ScreenToClient(hwnd, &pt);
  cx = pt.x;
  cy = pt.y;

  PointerInfo info{};
  const bool have = pointer_info_fn() && pointer_info_fn()(id, &info);
  if (have && (info.pointerFlags & (kPointerFlagWheel | kPointerFlagHWheel))) {
    const float notches = static_cast<float>(info.inputData) / 120.0f;
    if (info.pointerFlags & kPointerFlagWheel) hold_scroll(0.0f, notches, cx, cy);
    if (info.pointerFlags & kPointerFlagHWheel) hold_scroll(notches, 0.0f, cx, cy);
    return;
  }
  if (msg == kPointerWheel || msg == kPointerHWheel) {
    const float notches = static_cast<float>(signed_word(static_cast<LPARAM>(wp), true)) / 120.0f;
    if (msg == kPointerWheel) hold_scroll(0.0f, notches, cx, cy);
    else hold_scroll(notches, 0.0f, cx, cy);
    return;
  }

  const bool up = msg == kPointerUp || (have && (info.pointerFlags & 0x00040000));
  if (up) {
    drop_contact(id);
    return;
  }
  const bool in_contact = msg == kPointerDown || !have || (info.pointerFlags & kPointerFlagInContact);
  if (!in_contact) return;
  const int slot = contact_slot(id, true);
  if (slot < 0) return;
  int nx = cx;
  int ny = cy;
  if (have) {
    POINT raw = info.ptPixelLocation;
    ScreenToClient(hwnd, &raw);
    nx = raw.x;
    ny = raw.y;
  }
  const bool had = g_pad.contacts[slot].seen;
  const int prev_x = g_pad.contacts[slot].x;
  const int prev_y = g_pad.contacts[slot].y;
  g_pad.contacts[slot].x = nx;
  g_pad.contacts[slot].y = ny;
  g_pad.contacts[slot].seen = true;
  if (!had || g_pad.contact_count != 2 || slot != primary_contact()) return;
  if (recent(g_pad.last_consumed_ms, SDL_GetTicks())) return;
  int w = 0, h = 0;
  if (g_pad.window) SDL_GetWindowSize(g_pad.window, &w, &h);
  if (w <= 0 || h <= 0) return;
  const float dx = static_cast<float>(nx - prev_x);
  const float dy = static_cast<float>(ny - prev_y);
  float ox = 0.0f, oy = 0.0f;
  touch_drag_to_wheel(dx / static_cast<float>(w), dy / static_cast<float>(h),
                      static_cast<float>(w), static_cast<float>(h), kTouchDragPixelsPerNotch,
                      g_pad.down_scrolls_up, &ox, &oy);
  hold_scroll(ox, oy, cx, cy);
}

bool consume_elan_scroll(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const LegacyScroll scroll = legacy_scroll_message(
      msg, static_cast<unsigned>(wp), static_cast<long long>(lp),
      reinterpret_cast<long long>(hwnd), wheel_lines(), page_notches());
  if (!scroll.from_driver || (scroll.x == 0.0f && scroll.y == 0.0f)) return false;
  int cx = 0, cy = 0;
  remember_cursor(hwnd, &cx, &cy);
  hold_scroll(scroll.x, scroll.y, cx, cy);
  if (!g_pad.logged_elan) {
    g_pad.logged_elan = true;
    LOG_INFO("touchpad: ELAN scroll message (WM_%sSCROLL)", msg == kVScroll ? "V" : "H");
  }
  return true;
}

// Elantech checks the window under the cursor for WS_VSCROLL / WS_HSCROLL before it
// will generate a two-finger drag at all. No style, no message: not a wheel, not
// WM_VSCROLL, nothing. Chrome sets these styles for that reason and then hides the
// native bar. A mouse wheel never looks at the style, which is why that already worked.
void mark_scrollable(HWND hwnd) {
  if (!hwnd) return;
  LONG style = GetWindowLong(hwnd, GWL_STYLE);
  if ((style & (WS_VSCROLL | WS_HSCROLL)) == (WS_VSCROLL | WS_HSCROLL)) return;
  SetWindowLong(hwnd, GWL_STYLE, style | WS_VSCROLL | WS_HSCROLL);
  SCROLLINFO info{};
  info.cbSize = sizeof info;
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  info.nMin = 0;
  info.nMax = 10000;
  info.nPage = 100;
  info.nPos = 5000;  // stay mid-range so the driver does not decide we are already at the end
  SetScrollInfo(hwnd, SB_VERT, &info, FALSE);
  SetScrollInfo(hwnd, SB_HORZ, &info, FALSE);
  ShowScrollBar(hwnd, SB_BOTH, FALSE);
  style = GetWindowLong(hwnd, GWL_STYLE);
  SetWindowLong(hwnd, GWL_STYLE, style | WS_VSCROLL | WS_HSCROLL);
}

// The native bar must not steal client pixels. The style bits stay set.
LRESULT keep_client_full(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (!wp) {
    return g_pad.old_proc ? CallWindowProc(g_pad.old_proc, hwnd, msg, wp, lp)
                          : DefWindowProc(hwnd, msg, wp, lp);
  }
  auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
  const RECT proposed = params->rgrc[0];
  const LRESULT result = g_pad.old_proc ? CallWindowProc(g_pad.old_proc, hwnd, msg, wp, lp)
                                        : DefWindowProc(hwnd, msg, wp, lp);
  const int left_inset = params->rgrc[0].left - proposed.left;
  const int right_inset = proposed.right - params->rgrc[0].right;
  const int bottom_inset = proposed.bottom - params->rgrc[0].bottom;
  if (right_inset > left_inset) params->rgrc[0].right = proposed.right - left_inset;
  if (bottom_inset > left_inset) params->rgrc[0].bottom = proposed.bottom - left_inset;
  return result;
}

LRESULT CALLBACK pad_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kTabletQueryGesture) return kGestureStatus;
  if (msg == WM_NCCALCSIZE) return keep_client_full(hwnd, msg, wp, lp);
  if (msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED) mark_scrollable(hwnd);
  // Elantech "drag two fingers" is a scrollbar message, not a mouse wheel. Taking it
  // here is what makes the gesture scroll. A mouse wheel never arrives as this.
  if ((msg == kVScroll || msg == kHScroll) && consume_elan_scroll(hwnd, msg, wp, lp)) return 0;
  if (msg == kPointerUpdate || msg == kPointerDown || msg == kPointerUp || msg == kPointerWheel ||
      msg == kPointerHWheel) {
    consume_pointer(hwnd, msg, wp, lp);
    // A dedicated wheel message must not also be turned into WM_MOUSEWHEEL by
    // DefWindowProc, or the page moves twice. Pointer moves still have to reach SDL
    // so the cursor follows one finger.
    if (msg == kPointerWheel || msg == kPointerHWheel) return 0;
  }
  // The driver sometimes follows its own scroll message with a wheel. That wheel is
  // the same gesture. A physical wheel has no scroll message in front of it.
  if ((msg == kMouseWheel || msg == kMouseHWheel) && recent(g_pad.last_consumed_ms, SDL_GetTicks())) {
    return 0;
  }
  if (msg == kGesture && handle_pan(lp)) return 0;
  if (g_pad.old_proc) return CallWindowProc(g_pad.old_proc, hwnd, msg, wp, lp);
  return DefWindowProc(hwnd, msg, wp, lp);
}

void SDLCALL on_sdl_message(void*, void* hwnd, unsigned message, Uint64 wparam, Sint64 lparam) {
  // A gesture aimed at some other window in this thread (a driver quirk) never
  // reaches our procedure. The pump still sees it.
  if (!hwnd || hwnd == g_pad.hwnd) return;
  if (message == kVScroll || message == kHScroll) {
    consume_elan_scroll(static_cast<HWND>(hwnd), message, static_cast<WPARAM>(wparam),
                        static_cast<LPARAM>(lparam));
  }
}

}  // namespace

void pad_ensure_hook() {
  if (!g_pad.hwnd) return;
  const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtr(g_pad.hwnd, GWLP_WNDPROC));
  if (current != pad_proc) {
    // SDL installs its own procedure at creation and can put it back. If ours is no
    // longer the one Windows calls, the ELAN messages never reach us.
    g_pad.old_proc = current;
    SetWindowLongPtr(g_pad.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(pad_proc));
  }
  mark_scrollable(g_pad.hwnd);
}

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
  // SDL registered the window for raw touch, which tells the driver not to synthesize
  // a wheel. Undo that. The scrollbar styles are what make an ELAN pad send the
  // gesture in the first place.
  if (HMODULE user = GetModuleHandleA("user32.dll")) {
    using UnregisterFn = BOOL(WINAPI*)(HWND);
    if (auto unregister = reinterpret_cast<UnregisterFn>(GetProcAddress(user, "UnregisterTouchWindow"))) {
      unregister(g_pad.hwnd);
    }
  }
  mark_scrollable(g_pad.hwnd);
  SDL_SetWindowsMessageHook(on_sdl_message, nullptr);
  LOG_INFO("touchpad: two-finger drag hooked (scroll direction %s)",
           g_pad.down_scrolls_up ? "down-scrolls-up" : "down-scrolls-down");
}

void pad_uninstall(SDL_Window*) {
  SDL_SetWindowsMessageHook(nullptr, nullptr);
  if (g_pad.hwnd && g_pad.old_proc) {
    SetWindowLongPtr(g_pad.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_pad.old_proc));
  }
  g_pad = Pad{};
}

void pad_note_wheel() { g_pad.last_wheel_ms = SDL_GetTicks(); }

bool pad_drop_wheel() { return recent(g_pad.last_finger_ms, SDL_GetTicks()); }

bool pad_take_scroll(float* out_x, float* out_y, float* mouse_x, float* mouse_y) {
  pad_ensure_hook();
  if (!g_pad.pending) return false;
  if (out_x) *out_x = g_pad.pending_x;
  if (out_y) *out_y = g_pad.pending_y;
  if (mouse_x) *mouse_x = g_pad.pending_mx;
  if (mouse_y) *mouse_y = g_pad.pending_my;
  g_pad.pending_x = 0.0f;
  g_pad.pending_y = 0.0f;
  g_pad.pending = false;
  return true;
}

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
