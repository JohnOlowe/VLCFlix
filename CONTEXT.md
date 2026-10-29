# Project context

## This session (2026-10-29) — read this first

Turn 2 of 6 is the captures. No UI code has been written. Branch **`studio-ui`**
is cut from `cpp` at `5b86706` and is the only branch this session commits to.
`cpp` and `main` are not moved.

The gap list is `docs/captures/GAP.md`. Every row cites a file opened this turn.
Home scroll is the first fix: the JS document is 2442 px and scrolls to the
footer; the C++ window moves once and then sticks while a row is still clipped.

User decisions, locked this turn (not assumed):

- **UI only.** Keep the `cpp` engine, tests, and playback. Rewrite the surface so
  the C++ UI matches the JS UI. Do not bring back Play in browser or the
  VLC-over-HTTP dock unless a later message says so.
- **Match by screenshots I take myself.** Install and run the Node app, capture
  every screen, then change the C++ renderer one screen at a time until those
  captures match. Repo screenshots are a hint, not the bar.
- **Branch name:** `studio-ui`.

The token the user pasted is in `.secrets/github-token` (mode 600). `.secrets/`
and `.git-credentials` are git-ignored. `git add` of that path is refused.
`git ls-files` does not list it. It must never be printed, committed, or put in
a remote URL. Scope was probed, not assumed: creating a ref on this repo was
authorized (HTTP 422 on an invalid SHA, and the ref was confirmed absent
afterwards); the same call on `BT-Audio` and `Movies` returned **403**; Actions
secrets, hooks, vulnerability alerts and branch protection returned **403**;
an anonymous call returned **401**. `GET /user/repos` lists 15 public
repositories with `permissions.admin: true` — that field is not evidence of
token scope (the same mistake was retracted on 2026-10-26). Rotate the token
when the six turns are done; it passed through chat.

## RECOVERY (this session)

1. Read `AGENTS.md`, then this section, then `docs/PLAN_6_TURNS.md`.
2. `cd /home/user/VLCFlix`. If `git remote -v` is empty, run
   `bash scripts/setup-git-auth.sh`. Do not print the token file.
3. `git status -sb` and `git log --oneline -3`. The branch must be `studio-ui`.
4. Packages and `node_modules` do not survive a snapshot. Reinstall before
   claiming a build or a screenshot.
5. Resume at the first unchecked item in `docs/PLAN_6_TURNS.md`.

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

## Current state (2026-10-28) — native port, all 4 turns done

Product: **VLCFlix Studio 2.0.0**. Branch **`cpp`** exists on GitHub (`origin/cpp`)
and is the working branch. All four turns — 1 foundation, 2 window + browsing UI,
3 player/sync/subtitles, 4 polish + bug confirmation + test strategy — are committed,
pushed and verified from a clean clone.

- **Code lives in `native/`.** The Node app (`server/`, `public/`, `src/win/`) is
  untouched and still works; it is the reference behaviour for the port and is
  deleted only when the rewrite is complete and the user approves it.
- `native/` is C++17 with **no third-party headers** but five system libraries (SDL2,
  FreeType, libpng, libjpeg, and libVLC from turn 3): `core/{json,log,paths,naming,
  sha1,store,sync,catalog,brand}`, `library/{walk,probe,subtitles,artwork,scan}`,
  `render/{canvas,font,image,png_writer}`, `ui/{theme,app}`, `platform/{proc,window_sdl}`,
  a CLI (`scan/status/settings/naming/doctor/crash/ui/render`) and a test harness.
  Builds with CMake **and** with plain `make`, because installed tooling does not
  survive a snapshot.
- **Playback is libVLC in-process**: `playback/engine` takes every decoded frame into
  our own buffer, `playback/presenter` pays a picture-side sync delay when the plan
  says the picture must wait, `ui/player` draws the picture, chrome and subtitles. No
  `vlc.exe`, no HTTP bridge, no `<video>`: the three audited bugs that lived in that
  machinery no longer have a code path.
- **The UI draws with its own software renderer**, so `vlcflix render --all <dir>`
  writes every screen to a PNG with no display, and the UI tests measure pixels
  instead of describing intent. Hit areas come from the drawing pass, so what is
  painted is what is clickable.
- **Verified from a clean clone on GitHub**: `make verify` = 33 static checks + 590
  test checks + 15 end-to-end journey steps, all green; `make asan` runs the same 590
  checks under AddressSanitizer; CTest runs both suites.
- Turn 1 from a clean clone: `./build/vlcflix scan --folders ../samples` = 8 items,
  0 errors, all artwork.
- Identity/product rules: the Windows build must look like a new application
  (`VLCFlix Studio`, its own data dir, uninstall key and mutex, never over 1.2.0) —
  `docs/PORT_PLAN.md` §7.1, constant in `native/src/core/brand.h`.
- Logging requirement: `log::init()` is the first statement of `main()`, three
  directory fallbacks, per-line flush, crash handlers (exit 139 test leaves
  `[CRASH] SIGSEGV` + 9 frames in the log), and a run marker so the next launch
  reports an unexpected death — `docs/PORT_PLAN.md` §7.2, verified.
- Bugs from `docs/AUDIT.md` fixed and covered by regression tests in the native
  code: A1 (year-titled films), A4 (finished films), A5 (`nv12`), A6 (password
  type), A9 (still versioning). A2/A3 die with the code that caused them
  (transcoding by pipe; HTTP range serving).
- **The test strategy is `docs/TESTING.md`**: six layers (unit → golden image with
  pixel diffs → end-to-end journey on a library built during the run → fault injection
  → AddressSanitizer → static preflight), a regression test per known bug with its
  negative control, the honest "not verified" table, and the manual checklist for what
  cannot run here. `make verify` is the gate and needs nothing but a compiler,
  pkg-config, ffmpeg and libVLC.
- **Turn 4 found and fixed three real bugs** (none of them cosmetic): an unreadable
  file was kept with no marker and no refusal; a zero-decode file arrived from libVLC
  as `Ended` and was written as *watched*, silently removing it from Continue Watching;
  and the artwork preview job captured its segment list by reference after the owning
  block had closed, so roughly half of all scans drew no preview at all (ffmpeg got an
  absurd `-t`). All three have regression tests; the third is proved by ASan reporting
  `stack-use-after-scope` on the old code and nothing on the new.
- **Scrolling is now everywhere it can be needed** (the user's report: places where you
  simply cannot scroll). Every screen declares its own scroll region while it draws -
  `page`, `detail`, `settings`, `rail:<id>` - and the same declaration drives the bar,
  the wheel, the keyboard and the tests, so a limit can never be a guess. The wheel
  works over the open sheets (it used to be ignored there), over a rail it moves the
  rail sideways and then falls through to the page instead of doing nothing, Page
  Up/Down/Home/End scroll whatever is in front, bars are draggable, and hit areas for
  anything scrolled out of view are dropped so nothing invisible is clickable. Six new
  UI tests cover it, including a negative control (a sheet that fits must report no
  limit) and a pixel-diff proof that the sheet really moved.
- **2.0.1 fixes the three things the first real-Windows session found** (all invisible
  to a headless run): the window now shrinks to fit the screen it opens on
  (`fit_window_to_display()`, 96 %/90 %, floor 640x420) instead of hanging its own title
  bar off the desktop; the detail and settings sheets are clickable again (their scrim
  and click-swallowing panel were registered *last*, and hit testing takes the last
  match, while `add_hit()` silently dropped the `None` swallow - so controls were dead
  and any other click closed the sheet); and the pointer finally says what is clickable
  (`App::pointer_cursor()` -> Arrow/Hand/I-Beam from the same hit areas the click uses).
  The old test only asserted `handle()` returned true, which the scrim satisfied - the
  tests now assert what is under the pointer.
- **2.0.2 fixes the pointer, and adds a folder browser.** The cursor was right in the
  settings sheet but not on Home, because that was never a cursor bug: the window shrank
  to fit the desktop while the canvas stayed at the requested 1600x900 and SDL stretched
  it into a 1228x900 window, so every hit test was ~23 % away from the pixel aimed at.
  The canvas now follows the window on every frame (a window can resize without an event).
  This session also added the missing test layer: **Xvfb + xdotool**, so the real window
  is opened, clicked and photographed on a virtual display — that is how the fix was
  proved (2.0.1: clicking the visible Settings button did nothing; 2.0.2: it opens).
  Library folders can now be **chosen** instead of typed: **Browse...** opens a picker
  (`library/fs_browse.{h,cpp}`, plain std::filesystem) with the current path, the real
  subdirectories, ".. up one level" and, on Windows, every drive; *Use this folder* goes
  through the same `add_library_folder()` validation as a typed path. Verified in unit
  tests on a temp tree, through the UI's own hit areas, and by four real clicks on the
  virtual display that put the folder in `settings.json`.
- **2.0.3 stops asking Direct3D 9 to do anything.** 2.0.2 crashed on the real machine
  with `0xc0000005` and a stack full of `igdumdim64` / `D3D9!Direct3DCreate9On12Ex`:
  SDL's default driver order fell through to Direct3D 9 and the D3D9-on-12 shim inside the
  Intel driver faulted before the first frame, which no return value or try/catch can
  recover from. The renderer is now requested **by name** (`platform/render_driver.h`),
  defaults to **software** (the UI rasterizes every pixel itself, so the GPU only saves a
  memcpy), always falls back to software, honours `VLCFLIX_RENDER_DRIVER`/`SDL_RENDER_DRIVER`,
  flags `direct3d9` as the driver that crashed, and is reported by `doctor`. Tested live on
  Xvfb: `renderer: software`, identical pixels. The crash itself cannot be reproduced on
  this machine (no Intel GPU) and is not claimed to be.
- **2.0.4 is the Home screen, three times.** (a) Two-finger scrolling did nothing at all:
  a precision touchpad sends fractional deltas, SDL 2.26+ keeps them in `preciseY` and
  rounds the integer field to zero, and the handler read the integer field — so every
  touchpad swipe arrived as `0`. `platform/wheel.h` now carries the arithmetic (precise
  value, direction flip, fractions accumulated) as a pure, tested function.
  (b) Dragging the page bar "opened a gap" between the hero and the rails rather than
  scrolling: `hero_rect()` used `y = -scroll` while `rails_rect()` used `+scroll`, so the
  two halves of the page travelled in opposite directions. One sign, and a test that the
  gap is identical at 0/20/180/480 px of scroll and every card moves by exactly the scroll.
  (c) The hover preview showed artwork because it seeked to 20 % before libVLC knew the
  length (the seek is now queued until `length > 0`), and it drew a *second panel* over its
  neighbours instead of growing the card: the card itself now scales in place, with its
  rectangle recorded during the draw pass (looking it up in the hit list found the hero's
  clickable area and drew the card over the Play button).
- **The Windows build has now been RUN**, under Wine 11.19 (Wine 11.20 and
  11.20-staging are broken upstream: `create_view`/`virtual_alloc_first_thread_data`
  SEGV before a prefix exists). Executed and captured: `version`, `doctor`, `status`,
  `scan --no-art` (8 items, 0 errors, real 0:16 / 1280x720), a full artwork scan
  (8 of 8 sets drawn by 8 concurrent ffmpeg children), `render` for all six screens
  (`docs/evidence/windows-browse.png`, `windows-detail.png`), `play 1 --wait 4`
  (`state=playing … decoded=99 presented=99`), and `crash` (log carries the `CRASH`
  record and a 21-frame stack). Evidence and the exact recipe: `docs/TESTING.md` §11.
- **Executing it found five Windows-only bugs the cross build could not see**, all
  fixed: (1) `run_process` drained its pipes with `PeekNamedPipe` and returned nothing,
  so every film was "unreadable"; (2) `CreateProcess(bInheritHandles=TRUE)` let eight
  concurrent probes inherit each other's pipe ends — pipes never reached EOF and Wine
  refused launches with error 1359, fixed with an explicit
  `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` plus serialised spawning; (3) `write_png` handed
  libpng a `FILE*` from a different C runtime, so every byte failed to write and
  `render` died with an integer divide-by-zero in `msvcrt`, fixed by writing through a
  libpng callback into memory and reading the file back to check it; (4) the installed
  app's shortcut did nothing because no arguments meant `help`; (5) `have_binary()`
  trusted a process that printed nothing.
- **There is now one file to hand out: `native/releases/VLCFlixStudio-Setup.exe`**
  (71 MB, git-ignored), built by `scripts/build-win-native.sh --setup` from
  `native/src/setup/setup.cpp`. It carries the program, SDL2, FreeType, libpng, zlib,
  libjpeg, libVLC and all 29 plugin modules inside itself (zip appended to a
  GUI-subsystem stub, miniz footer `VFSUZIP1`), so nothing is unzipped by hand.
  Double-clicking installs into `%LOCALAPPDATA%\Programs\VLCFlixStudio`, adds a Desktop
  icon and two Start Menu entries, registers in Apps & features, and starts the app;
  `--uninstall` removes program, shortcuts and registry key but keeps the user's
  library. Identity is a new application throughout (own folder, data dir, uninstall
  key, AppId, mutex); 1.x's folder is only read, and `--dir` pointed at it is refused.
  The old `native/packaging/installer.nsi` draft is deleted and `tools/preflight.sh`
  checks the real installer source instead (**30 static checks**).
- **A fifth Windows-only bug came out of installing it:** double-clicking the shortcut
  did nothing, because `main()` defaulted to `help` and the GUI-subsystem binary printed
  usage into a console nobody could see. No arguments now opens the window, and a
  window that cannot open shows a message box naming the log.
- **Verified under Wine 11.19 in a fresh prefix:** install (no args) → 372 files,
  `--verify` → 0, the installed app scans (8 items, 0 errors), plays
  (decoded=70 presented=70) and renders (58,923 B PNG); reinstall keeps the library;
  uninstall removes folder + key + 3 shortcuts and leaves `catalog.json`, `media/` and
  `progress.json`; a stand-in 1.x folder is untouched.
- **Still not verified:** Windows 10/11 itself (Wine is an impression, not the real
  thing), a real Explorer/Start Menu/Add-Remove Programs drawing these shortcuts and
  running this uninstaller, the window on a real display (only headless SDL and
  off-screen renders), audio (no sound card), the `-hwaccel auto` GPU path (no Vulkan
  video queue here, software fallback proven), and side by side with a genuine 1.2.0.
  Automatic device-latency compensation is not implemented because libVLC 3 cannot
  report output latency — the UI says so instead of offering a toggle that did
  nothing (`autoLatency` is gone from the code, the settings file and the CLI). Do not
  claim otherwise.

## Historical state (2026-10-26)
— the Node/WebView2 application, v1.2.0
- Local clone at `/home/user/VLCFlix`, branch `main`, tracking `origin/main`.
- Application is **complete, working and pushed**. Local HEAD = remote
  `origin/main` = `a53d691`, verified with `git ls-remote` and independently with
  the GitHub contents API.
- A fresh fine-grained PAT was supplied by the user. It is stored **durably** at
  `.secrets/github-token` (mode 600, git-ignored). `scripts/setup-git-auth.sh`
  regenerates `.git-credentials` and the git config from it on demand. Earlier
  this token was kept only in `.git-credentials`, which **disappeared** after the
  next snapshot — see the note below. **Its effective scope is VLCFlix only — verified, not assumed.** An earlier
statement in this file that the token was "broader than a single repository" was
**wrong and is retracted**. The `GET /user/repos` call returns 15 repositories
because they are *all public*; public metadata is readable by anyone with no
token at all, so that list proved nothing about permissions.

Controlled test (2026-10-26), using the token sent explicitly so that
`credential.useHttpPath` could not mask the result:

- Control — `git push --dry-run` to `JohnOlowe/VLCFlix` -> **accepted**
  (`* [new branch] HEAD -> scope-probe`).
- `git push --dry-run` to each of the **other 14 repositories** -> **HTTP 403**
  in every case.

Same credential, same command, same client; the only variable is the repository.
`--dry-run` means no refs or objects were created — confirmed afterwards:
VLCFlix has only `main`, and no `scope-probe` branch exists on any repo.

The same probes show Actions secrets and Administration return **403**, so the
token cannot alter repository settings or CI secrets.

Residual advice that still stands: the token has passed through chat twice, so
rotating it is prudent hygiene — but **not** because it is over-scoped. It isn't.

## Credential persistence — read this before debugging auth
Workspace snapshots strip these paths **by filename**, regardless of `.gitignore`:
`.git-credentials`, `.git/config`, `.netrc`. Verified the hard way: after the
turn that pushed successfully, `.git-credentials` was gone, `git remote -v` was
empty, and `credential.helper` / `user.name` were unset — while every regular
file survived.

`gitignore` is not the problem and never was. A gitignored file is simply never
pushed, so a fresh clone can never contain one; that is expected behaviour.

The working arrangement:
1. The token lives in **`.secrets/github-token`** — git ignores `.secrets/`, but
   the path is not on the snapshot exclusion list, so it persists.
2. **`.gitremote`** (tracked, non-secret) holds the remote URL, because
   `.git/config` does not survive.
3. **`scripts/setup-git-auth.sh`** rebuilds `origin`, the commit identity and
   `.git-credentials` from those two files. Run it first whenever git looks
   unconfigured. It is idempotent and never prints the token.

Negative control confirming the token is what authorises pushes: hiding
`.secrets/github-token` and clearing `.git-credentials` produced
`remote: No anonymous write access.`; restoring it made `git push --dry-run`
report `Everything up-to-date`.

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

## Next steps (C++ port)
1. Three decisions asked on 2026-10-28 and needed before P1: branch name (`cpp`),
   UI toolkit (SDL2 + own renderer recommended), and whether the external-VLC
   mode is kept at all.
2. P0/P1: build skeleton (`cmake` + `make`), then `json`, `store`, `naming`,
   `subtitles`, `sync`, with a regression test per audit bug and a negative
   control that the old behaviour fails it.
3. A credential is required to push the `cpp` branch; until then work stays local
   and that must be said at the end of every turn.

## Superseded next steps (Node app, v1.2.0)
1. Ask the user to rotate the chat-exposed PAT and narrow it to
   Contents: read/write on `JohnOlowe/VLCFlix` only.
2. ~~Ask the user to test `start-windows.bat`~~ — irrelevant once the C++ app
   replaces the shell; the Windows proof now belongs to P8.

## RECOVERY
1. Read `AGENTS.md` for standing rules.
2. Read this `CONTEXT.md`, then `TURN_LOG.md`, then `checkpoint.json`.
3. `cd /home/user/VLCFlix && git log --oneline -3 && git status -sb`. `.git/config`
   may not survive a restore — if `git remote -v` is empty, re-add
   `origin https://github.com/JohnOlowe/VLCFlix.git`.
   **Both `.git-credentials` and `.git/config` are stripped from snapshots.**
   Run `bash scripts/setup-git-auth.sh` to rebuild origin, identity and the
   credential helper from `.gitremote` and `.secrets/github-token`.
4. `node_modules` does not survive; run `npm install` (needed only to run/compare
   the reference Node app, not to build the C++ port).
5. Reinstall ffmpeg/VLC if the VLC tests or scanning need them (commands above).
6. Start the reference app with the **process tool**, not `bash`:
   `cd /home/user/VLCFlix && node server/index.js`, then open port 4173.
7. For the C++ port, reinstall `cmake ninja-build libsdl2-dev libvlc-dev ffmpeg
   vlc-bin vlc-plugin-base zlib1g-dev libfreetype-dev` (all ephemeral).
8. Resume at the first item in **Next steps (C++ port)**.
