# Project context

## Project
- Repository: `JohnOlowe/VLCFlix` (`https://github.com/JohnOlowe/VLCFlix.git`).
- Product: a Netflix-style browsing front-end for a local video library that plays
  through the **real VLC media player** via its HTTP control interface.

## Scope decision (confirmed by the user, 2026-10-26)
The user first asked to modify VLC for Windows itself. Blocked, and the blocker was
reported with evidence: this sandbox has 2 CPU cores, 2 GB RAM and no mingw-w64
toolchain; VideoLAN's own docs require mingw-w64 + Wine + ~100 contrib libraries and
state a 4-hour minimum build. A cross-compiled Windows binary could not be run or
verified here, so every "it works" claim would have been unverifiable.

The user chose **"Netflix front-end driving real VLC"** instead. VLC remains the
playback engine; VLCFlix owns the entire browsing surface.

## Current state
- Local clone at `/home/user/VLCFlix`, branch `main`, tracking `origin/main`.
- Application is **complete, working and pushed**. Local HEAD = remote
  `origin/main` = `a53d691`, verified with `git ls-remote` and independently with
  the GitHub contents API.
- A fresh fine-grained PAT was supplied by the user and stored in the ignored,
  mode-600 `.git-credentials`. It is **broader than this one repository**: scope
  probes showed it sees 15 repos (all public, so that alone proves little), while
  Actions secrets and Administration endpoints return 403. Write scope beyond
  VLCFlix was deliberately **not** tested — proving it would mean mutating another
  repo. The user has been asked to rotate it and narrow it.
- `data/` and `media/` are gitignored: runtime state, generated artwork, nothing
  that cannot be recreated by a scan.
- `samples/` **is** committed (8 clips, 2.2 MB) so a fresh clone is usable at once;
  regenerable with `npm run demo`.

## What exists
- `server/` — Express API, library scanner, ffmpeg artwork + exposure correction,
  VLC bridge (detect / launch / status / command), range-aware streaming.
- `public/` — vanilla ES-module front-end: hero, rails, hover previews, detail
  sheet, search, My List, built-in player, live VLC remote dock.
- `tests/` — 23 tests; VLC fixtures are real captured VLC 3.0.24 output.
- `scripts/make-demo-media.js` — procedurally renders the demo library.
- `docs/screenshots/` — current UI captures.

## Environment notes for this sandbox
- `ffmpeg` 7.1.5 and `vlc` 3.0.24 are installed via apt **in this session only**;
  neither survives a snapshot restore. Reinstall with:
  `sudo apt-get install -y --no-install-recommends ffmpeg vlc-bin vlc-plugin-base`
- Chromium (Playwright, at `~/.cache/ms-playwright`) is also ephemeral. Needs
  `libnss3 libnspr4 libatk1.0-0 libatk-bridge2.0-0 libatspi2.0-0 libcups2
  libxdamage1 libxcomposite1 libxrandr2 libxkbcommon0 libgbm1 libpango-1.0-0
  libasound2`. `npx playwright install --with-deps` fails here on missing
  `ttf-unifont` / `ttf-ubuntu-font-family`; install the browser alone and add the
  libs by hand.
- **This box has no Qt/display interface module**, so plain `vlc` exits with
  "no suitable interface module". To test VLC headlessly, run it with `-I dummy`
  (see `tests/` procedure) or point `vlcPath` at a wrapper:
  `#!/bin/sh`
  `exec /usr/bin/vlc -I dummy "$@"`
- Playwright's Chromium **has no H.264**, so the built-in player cannot play the
  demo `.mp4` files in tests. Use a VP9/WebM clip to verify the player.
- Never `pkill -f <pattern>` here when the pattern also appears in the current
  command line — the shell kills itself. The bash tool and the process tool are
  separate processes; a server started with `start_process` survives and must be
  stopped with `stop_process`.

## Next steps
1. Ask the user to rotate the chat-exposed PAT and narrow it to
   Contents: read/write on `JohnOlowe/VLCFlix` only.
2. Ask the user to test `start-windows.bat` against their real VLC install and
   report failures with the exact VLC error text now surfaced by `launch()`.
3. Likely follow-ups, not yet requested: subtitle handling, multi-part/season
   grouping, an online metadata provider, player-in-VLC seek from the dock while
   VLC is fullscreen.

## RECOVERY
1. Read `AGENTS.md` for standing rules.
2. Read this `CONTEXT.md`, then `TURN_LOG.md`, then `checkpoint.json`.
3. `cd /home/user/VLCFlix && git log --oneline -3 && git status -sb`. `.git/config`
   may not survive a restore — if `git remote -v` is empty, re-add
   `origin https://github.com/JohnOlowe/VLCFlix.git`.
   **`.git-credentials` does not survive a restore either.** Everything up to
   `a53d691` is on the remote, so a fresh clone loses nothing; only pushing again
   needs a new token.
4. `node_modules` does not survive; run `npm install`.
5. Reinstall ffmpeg/VLC if the VLC tests or scanning need them (commands above).
6. Start the app with the **process tool**, not `bash`:
   `cd /home/user/VLCFlix && node server/index.js`, then open port 4173.
7. Resume at the first item in **Next steps**.
