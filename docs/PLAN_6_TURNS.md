# Six turns — `studio-ui`

Turn 1 is the plan. No UI code in this commit. Decisions below were confirmed by
the user on 2026-10-29, not assumed.

## Locked decisions

| Decision | Choice |
|---|---|
| Base | `cpp` at `5b86706`. Engine, tests, and playback stay. |
| Branch | `studio-ui`. `cpp` and `main` are not moved. |
| Scope | **UI only.** Rewrite the C++ surface so it matches the JS UI. |
| Bar | Screenshots taken here of the running Node app, then C++ screens built one after another until they match. Checked-in screenshots are a hint, not the oracle. |
| Out of scope unless asked | Play in browser, the VLC-over-HTTP dock, deleting `server/` or `public/`. |
| Identity | Stays VLCFlix Studio. Must not install over 1.2.0 (`docs/PORT_PLAN.md` §7.1). |

## What “match” means

A screen matches only when both of these are true:

1. I opened the Node screenshot and the C++ render in this session (`AGENTS.md` §5).
2. The difference is measured, not described. Layout, type, chips, buttons, and
   artwork placement are compared. A blank or letter-block card is a failure.
   Pixel-identical output is not required across FreeType and Chromium, and will
   not be claimed. A screen that still reads as a different product does not pass.

Order, one screen at a time, Node capture first:

1. Home, at rest (hero is the backdrop, title on the art).
2. Home, scrolled (rails, no gap, no letter-block cards when art exists).
3. Hovered card.
4. Detail sheet.
5. Browse, search, My List, empty library.
6. Settings.
7. Player chrome over a real decoded frame.

Behaviour that is part of the look (scroll, hover growth, what a click opens)
is in turns 4–5. Playback internals are not rewritten unless a screenshot shows
the chrome is wrong.

## Turns

| Turn | Work | Done when |
|---|---|---|
| 1 (this commit) | Tool plan, token stored off git, branch created. | This file. Remote SHA checked. No UI diff. |
| 2 | Install the Node app. Run it. Capture every screen above. Render the current C++ UI the same way. Write a gap row per screen, each citing files I opened. | Gap list. Both apps started, or the failure pasted. |
| 3 | Home billboard and cards. | Node capture and C++ render opened side by side. |
| 4 | Detail, browse, search, My List, settings, empty. | Same, one screen at a time. |
| 5 | Hover, scroll, player chrome. Window captures, not only still renders. | Two captures of a hovered card differ inside the card. |
| 6 | Corners: resize, keyboard, empty, fault paths that show in the UI, ASan, honest not-verified list. | `make verify` output pasted. Remote SHA matches HEAD. |

## Tools (the only ones)

- `bash` — git, apt, `make`, tests, pixel stats, one-shot captures. Not servers.
- `start_process` — Node reference on `0.0.0.0`, and a gallery that shows the
  latest Node capture next to the latest C++ PNG. Relative URLs only.
- `read_file` — source, and every image I claim to have seen.
- `present_file` — the screen under discussion.
- `write_file` / `edit_file` — the UI and these state files.
- `fetch_page` — remote SHA check after a push.
- `generate_image` — not used. A generated picture is not a render of the app.

SDL has no browser. The preview the user watches is the gallery of real PNGs.

## Machine, measured 2026-10-29

21 GB free, 1982 MB RAM. Present: `g++`, `git`, `node` v20.20.2, FreeType, libpng,
libjpeg, ImageMagick. Missing until turn 2 installs them (none survive a snapshot):
`cmake`, `ninja`, `ffmpeg`, `libsdl2-dev`, `libvlc-dev`, `vlc-bin`, `xvfb`, `xdotool`.
No display is attached. A real Windows desktop is not claimed.

## Credential

Stored at `.secrets/github-token`, mode 600, git-ignored. Never printed.
`scripts/setup-git-auth.sh` rebuilds the helper after a snapshot. Probe results
are in `TURN_LOG.md` for 2026-10-29. Do not infer scope from `GET /user/repos`.

## Turn 6 result (2026-10-29)

Empty library matches the web empty state: centred “Your shelf is empty”, the
folder copy, “Choose library folders”, and “Load the demo library”. Opened
`docs/captures/js/empty.png` and `docs/captures/cpp/empty.png`. A 640×420 window
scrolls far enough to show the footer (`cpp-empty` proof is the 1600×900 still).
Keyboard focus grows the focused card the same way a hover does
(`docs/captures/cpp/focus.png`).

### Not verified

These were not checked, or were checked only as far as the note says. Do not
read a green `make verify` as covering them.

- A Windows desktop, audio output, and a GPU. This machine has none.
- Pixel-identical output against Chromium. FreeType and the browser do not match.
- Play in browser, and the VLC-over-HTTP dock. Out of scope.
- Settings fields the native player does not use: audio latency, VLC path, control
  port, password, and fullscreen-on-play. The playback segment is drawn, and only
  Real VLC is the engine. FFmpeg and Built-in player are not buttons.
- The player sync control. The web player has no equivalent. It stays because
  this player still applies a manual offset, and the tests require the hits.
- The footer brand is one white “VLCFLIX”, not the orange/white split in the web
  footer. Home was already accepted with that footer, so it was not restyled.
- The gutter is 48 px, not 4 vw. Home rails have no hover arrows.
- A precision touchpad. Not present. Wheel and Page Down are what the tests drive.
- A live window resize by dragging. Short and wide sizes were rendered
  (640×420 empty, 900×520 browse) and opened. The window was not dragged.
- A live keypress in the window. Keyboard focus was a `--focus` render, plus the
  existing key tests. It was not an xdotool key capture.
- Dragging the settings scrollbar in the live window. The unit test scrolls that
  sheet to Scan. The bar was not dragged by hand this turn.

## Turn 2 result (2026-10-29)

Captures are in `docs/captures/`. The gap list is `docs/captures/GAP.md`.

## Turn 3 result (2026-10-29)

Home only. Billboard and 16:9 cards match the opened JS shots
(`js/home-viewport.png`, `js/home-full.png`). The live window on Xvfb kept
moving through 18 wheel clicks and stopped on the footer
(`cpp/window-home-end.png`), not on a clipped Mystery row. `make test` 598/598.
Detail is next. No engine rewrite.
No UI code was edited.

Turn 3 starts with the home billboard **and** the scroll limit. A top-of-page
match is not done until a scroll reaches the last rail the way
`docs/captures/js/home-full.png` does.
