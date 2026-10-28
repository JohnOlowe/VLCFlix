// Which SDL renderer to ask for, and in what order.
//
// This exists because of a real crash on a real machine:
//
//   [CRASH] unhandled exception code=0xc0000005
//     ...
//     #8  igdumdim64!OpenAdapter            (Intel graphics driver)
//     #9  D3D9!Direct3DShaderValidatorCreate9
//     #11 D3D9!Direct3DCreate9On12Ex
//     #13 SDL2!...
//     #16 VLCFlixStudio ...
//
// SDL's default driver order (direct3d11, then direct3d, then ...) fell through to
// Direct3D 9 on that machine, and D3D9-on-12 inside the Intel driver raised an access
// violation during renderer creation. No amount of try/catch can recover from that: the
// crash is inside the driver, on SDL's call, before it can return an error. The only
// fix is not to ask that driver to do anything.
//
// The policy that follows from it: ask for the **software** renderer by default. This
// UI draws every pixel itself into a pixel buffer and uploads one texture per frame, so
// the GPU contributes a memcpy and nothing else - while a driver bug costs the whole
// application. Anyone who wants the accelerated path can ask for it by name:
//
//     VLCFLIX_RENDER_DRIVER=direct3d11     (or opengl, direct3d, ...)
//     SDL_RENDER_DRIVER=direct3d11         (SDL's own variable also works)
//
// and the plan always ends in software, so an explicit choice that cannot be honoured
// degrades to something that works instead of to a window that never appears.
#pragma once
#include <string>
#include <vector>

namespace vf {

struct RenderDriverPlan {
  std::vector<std::string> order;  // driver names to try, first working one wins
  std::string requested;           // what the environment asked for (empty = default)
  bool from_environment = false;
  // True when the plan knowingly tries a driver that has been seen to crash in the
  // wild (Direct3D 9, the D3D9-on-12 shim). Honoured, because an explicit request is
  // the user's decision, but logged as such.
  bool risky = false;
};

// `requested` is the value of VLCFLIX_RENDER_DRIVER (or SDL_RENDER_DRIVER); empty means
// "no opinion". The result is never empty and always ends with "software".
inline RenderDriverPlan render_driver_plan(const std::string& requested) {
  RenderDriverPlan plan;

  // Trim: an environment variable with a stray space in it is a typo, not a driver.
  std::size_t begin = 0;
  while (begin < requested.size() && (requested[begin] == ' ' || requested[begin] == '\t')) ++begin;
  std::size_t end = requested.size();
  while (end > begin && (requested[end - 1] == ' ' || requested[end - 1] == '\t')) --end;
  plan.requested = requested.substr(begin, end - begin);

  if (plan.requested.empty()) {
    plan.order.push_back("software");
    return plan;
  }

  plan.from_environment = true;
  plan.risky = plan.requested == "direct3d" || plan.requested == "direct3d9";
  plan.order.push_back(plan.requested);
  if (plan.requested != "software") plan.order.push_back("software");
  return plan;
}

// How the choice should be described in a log or in `doctor`.
inline std::string render_driver_description(const RenderDriverPlan& plan) {
  if (!plan.from_environment) return "the safe default; ask for another with VLCFLIX_RENDER_DRIVER";
  std::string text = plan.requested + " requested, falling back to software";
  if (plan.risky) text += " (warning: this driver has crashed on some Intel machines)";
  return text;
}

}  // namespace vf
