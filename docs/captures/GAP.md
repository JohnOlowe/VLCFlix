# Turn 2 — gap list

Both apps were run here on 2026-10-29 against the same eight sample films.
Nothing in `native/src` was edited. Every row cites a file opened this turn.

The rule for the next turns: a viewport shot is not the screen. The JS home
document is 2442 px tall in a 900 px window. Search results were below the
first screen. Settings has another 517 px under the fold. A C++ render that
stops at the window edge is not a match, even if the top looks close.

## How they were captured

| App | What ran | Proof |
|---|---|---|
| Node | `node server/index.js` on `0.0.0.0:4173`, library `/home/user/VLCFlix/samples`, scan finished 8/8 | `docs/captures/js/metrics.json` |
| Node shots | Chromium 156 via puppeteer-core, viewport 1600×900, plus `fullPage` | files under `docs/captures/js/` |
| C++ | `./build/vlcflix scan` then `render`, built this turn | `docs/captures/cpp/home.png` and the other renders |
| C++ window | Xvfb `:99` 1600×900, real `ui` process, `xdotool` wheel and Page Down, `import -window root` | `docs/captures/cpp/window-home-top.png`, `window-after-wheel.png` |

## Scroll, measured

This is the behaviour the user called out. It is not an opinion.

**JS home scrolls through the whole page.**

- `documentElement.scrollHeight` 2442, `innerHeight` 900, overflow 1542 px.
- `window.scrollTo` landed on 0, 700, 1400 and 1542. The wheel moved `scrollY` from 0 to 960.
- The full document, including the footer, is `docs/captures/js/home-full.png`.
- A viewport after scrolling 1400 px is `docs/captures/js/home-scroll-1400.png`. Thriller, Documentary and Everything in Your Library are on that screen. They are not on `home-viewport.png`.

**C++ home moves once, then sticks, with content still cut off.**

- The headless render has no scroll argument. `docs/captures/cpp/home.png` is one 1600×900 frame. The portrait row is cut by the bottom edge.
- The live window opened at 1536×810, not the 1600×900 it was asked for (`fit` against the 1600×900 Xvfb desktop).
- One wheel burst changed 53.70% of sampled pixels (stride 2, channel delta > 8). Page Down produced a frame identical to that wheel result.
- Four more wheel clicks changed 0.000 of a 160×90 sample. Those frames are identical to `window-after-wheel.png`.
- That stuck frame still clips the Mystery row. Drama and Documentary are visible. The rest of the page is not. File: `docs/captures/cpp/window-after-wheel.png`.

Integer wheel and Page Down are not dead. They are not enough. The scroll limit is short of the content. A precision touchpad was not tested. There is no touchpad here, so that path is not claimed either way.

**JS settings also scrolls.** Modal `scrollHeight` 1417, `clientHeight` 900, `scrollTop` reached 517, which is the maximum. `settings-top.png` does not contain Playback, hardware decoding, audio latency or Save. `settings-scrolled.png` does. The C++ settings render shows a scrollbar and stops at Scan library (`docs/captures/cpp/settings.png`). The sheet was not driven with the wheel this turn, so sheet-scroll is observed as clipped, not proven stuck.

## Screen by screen

| Screen | JS, opened | C++ , opened | Gap |
|---|---|---|---|
| Home, top | `js/home-viewport.png` | `cpp/home.png` and `cpp/window-home-top.png` | JS puts the title on the artwork, with an orange eyebrow, TV-14 and NEW chips, a white Play, More Info, and a circle-plus. Cards on Home are 16:9, title on the art, meta under the title. C++ puts the title under a separate art band, orange Play, My List, Details, and tall posters with the title underneath. Header: JS is logo, search icon, VLC pill, gear. C++ adds Home / Browse All / My List, a search field, a libVLC pill and a Settings button. |
| Home, rest of the page | `js/home-full.png`, `js/home-scroll-1400.png` | `cpp/window-after-wheel.png` | JS reaches a footer after genre rails of 16:9 cards. C++ stops while a portrait row is still cut off. See Scroll above. |
| Hover | `js/home-hover.png` | not driven | The JS hover did not attach a `<video>` in 1.8 s (`metrics.json` `hover.videos = 0`). Not claimed as a product bug. C++ hover was not captured. Turn 5. |
| Detail | `js/detail-top.png` | `cpp/detail.png` | Both open The Silent Coast on a green backdrop. JS is a large sheet: title on the art, Play, plus, Play in browser, genres, added date, file size, resolution, filename. C++ is a smaller card: title under a short art strip, Play and My List only. Play in browser stays out of scope. The metadata column does not. The JS sheet fitted this 900 px viewport (`bodyScrollH` 794), so detail-scroll was not exercised. |
| Browse | `js/browse-full.png` | `cpp/browse.png` | JS Browse is portrait posters, title and meta on the art, two rows, then a footer. `scrollHeight` 1203, scrolled to 303. C++ is one row of posters, titles under the art, and a large empty area. All eight fit, so this screen does not overflow. The card chrome still does not match. |
| Search | `js/search-full.png` | `cpp/search-ghost.png` | Both find Ghostlight, 1 match. The JS viewport shot missed it: Browse All is still in the document above the results, so the match is below the fold. The full-page file is the one that shows it. C++ shows the result immediately, as a small poster with the title underneath and no year line. |
| My List | `js/mylist.png` | `cpp/mylist.png` | JS, after a reload so bootstrap saw the save: one poster, title on the art, “1 saved title”. C++ render is the empty state. The C++ list was not given an item before the render, so emptiness is the setup, not a failed toggle. The empty-state copy and the filled-card chrome still have to match once an item is saved. |
| Settings | `js/settings-top.png`, `js/settings-scrolled.png` | `cpp/settings.png` | JS top shows the samples folder, Scan library, and the engine picker. The rest (hardware decoding, audio latency, Save, fullscreen) is only in the scrolled shot. C++ top shows “0 configured” because this render’s data dir has a catalog and no `settings.json`. It also shows a scrollbar, so the sheet is taller than the panel. Not the same form. |
| Player | `js/player.png` | `cpp/player.png` | Both show a decoded frame of The Silent Coast (JS `videoWidth` 1280, `currentTime` 1.14; C++ “2 frames decoded”). Chrome does not match. JS is a bottom transport: back, title, scrub, ±10 s, CC, volume. C++ is a centred −10 / pause / +10, a volume pill reading 100, Mute, CC, and a sync readout. The JS shot also shows a VLC-exit notice because the first play still tried the external player. That notice is not the target chrome. |

## What turn 3 should change first

Home, and the scroll limit that belongs to it. A billboard that matches the top
of `js/home-viewport.png` is not done until a scroll reaches the last rail the
way `js/home-full.png` does, without clipping a row and stopping.

Order after that stays the plan: detail, browse, search, My List, settings,
then hover and player chrome. No engine rewrite. Play in browser and the HTTP
dock stay deleted.

## Turn 3 result (2026-10-29) — home closed

The home rows above are the before. They stay, because the stuck frame is the
measurement. The after is a different set of files, opened this turn:

| What | File opened |
|---|---|
| JS top, the target | `docs/captures/js/home-viewport.png` |
| JS rest of the page, the target | `docs/captures/js/home-full.png` |
| C++ render, top, 1600×900 | `docs/captures/cpp/home-viewport.png` |
| C++ render, scrolled to the limit | `docs/captures/cpp/home-scrolled.png` |
| Live window, rest, 1536×810 | `docs/captures/cpp/window-home-rest.png` |
| Live window, 12 wheel clicks | `docs/captures/cpp/window-home-mid.png` |
| Live window, end of the page | `docs/captures/cpp/window-home-end.png` |

What the opened files show:

- Billboard: eyebrow, title on the still, TV-14 chip, NEW, year · 720p · genre, synopsis, white Play, More Info, circle-plus. Header at rest is logo, search icon, VLC pill, gear. Nav stays hidden until the page moves, then it appears (`window-home-mid.png`).
- Cards are 16:9, title and meta on the art, cut off at the right edge the way the JS row is.
- Scroll is no longer one notch. On the live window, wheels 0→6, 6→12 and 12→18 each changed the frame (97.7%, 65.3%, 44.5% of pixels). Wheel 21 and four more clicks after that changed 0.000% against `window-home-end.png`. That end frame shows Thriller, Everything in Your Library, and the footer. Mystery, the row the old window clipped, is fully on screen in `window-home-mid.png`.
- The limit no longer shrinks with the offset. `test_ui_home_scroll_reaches_the_last_rail` scrolls to the limit and requires the last card inside the window. `make test`: 598 checks, 0 failures.

Not the same pixels as Chromium. The gutter is still 48 px, not 4 vw, and the home rails have no hover arrows. Those are not the gap that made the screen read as a different page.

Turn 4 is the detail sheet, then browse, search, My List, settings. Home is not the next edit.

## Not claimed

- A precision touchpad. Not present.
- Scroll of the C++ detail sheet and settings sheet under the wheel. The settings
  scrollbar is visible. It was not dragged.
- Windows, audio, or a GPU.
- That the JS search route is supposed to leave Browse All above the results.
  That is what this capture did. The match itself is in `search-full.png`.
