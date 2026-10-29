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

## Not started

The C++ UI has not been edited. Turn 2 is the first code-adjacent work, and it
starts with the Node screenshots, not with a rewrite.
