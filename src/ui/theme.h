#pragma once
#include "render/canvas.h"

namespace vf {
namespace theme {

inline constexpr uint32_t kBg = 0x0b0b0f;
inline constexpr uint32_t kBgRaised = 0x0d0d12;
inline constexpr uint32_t kPanel = 0x15151b;
inline constexpr uint32_t kCard = 0x15151d;
inline constexpr uint32_t kText = 0xf2f2f5;
inline constexpr uint32_t kTextDim = 0x9c9caa;
inline constexpr uint32_t kTextFaint = 0x6b6b78;
inline constexpr uint32_t kAccent = 0xff8800;   // VLC orange
inline constexpr uint32_t kAccentAlt = 0xe26300;
inline constexpr uint32_t kDanger = 0xff4d4f;
inline constexpr uint32_t kWhite = 0xffffff;
inline constexpr uint32_t kBlack = 0x000000;

inline Color bg() { return rgb(kBg); }
inline Color panel() { return rgb(kPanel); }
inline Color card() { return rgb(kCard); }
inline Color text() { return rgb(kText); }
inline Color dim() { return rgb(kTextDim); }
inline Color faint() { return rgb(kTextFaint); }
inline Color accent() { return rgb(kAccent); }

// Type scale.
inline constexpr float kFontTiny = 11.0f;
inline constexpr float kFontSmall = 13.0f;
inline constexpr float kFontBody = 15.0f;
inline constexpr float kFontLabel = 17.0f;
inline constexpr float kFontTitle = 22.0f;
inline constexpr float kFontHero = 52.0f;

// Layout.
inline constexpr float kGutter = 48.0f;      // page padding
inline constexpr float kRadius = 6.0f;
inline constexpr float kCardRadius = 8.0f;
inline constexpr float kHeaderHeight = 68.0f;
inline constexpr float kRailGap = 14.0f;

// A poster card is 2:3, a backdrop 16:9, exactly as the artwork pipeline crops them.
inline constexpr float kPosterAspect = 2.0f / 3.0f;
inline constexpr float kBackdropAspect = 16.0f / 9.0f;

// Deterministic fallback colour for a film with no still, keyed on its title.
Color poster_fallback(const std::string& title, bool darken);

}  // namespace theme
}  // namespace vf
