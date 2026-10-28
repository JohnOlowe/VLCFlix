# VLCFlix Studio — the native port

The C++ rewrite of VLCFlix. All four turns are done (see `../docs/PLAN_4_TURNS.md`);
how it is tested is in `../docs/TESTING.md`.

**What this is right now:** the whole product, in one native binary — the library
engine (scan, probe, artwork, subtitles, sync maths, crash-proof logging), the window
and every browsing screen drawn by our own software renderer, and **real playback**:
libVLC decodes the picture in-process into our buffer, we present it ourselves, draw
the subtitles ourselves off the engine's clock, and apply the sync plan from
`sync-math.js` by delaying whichever of the two senses has to wait.

**What is not verified:** the Windows build (written and packagable, never compiled or
run), the window on a real display (it has only ever run headless), and the audio path
(there is no sound card here). See "Not verified, on purpose" at the bottom; it is kept
current, and `../docs/TESTING.md` §10 is the checklist for the machine that can.

---

## Build

```bash
sudo apt install build-essential pkg-config libsdl2-dev libfreetype-dev libpng-dev libjpeg-dev
cd native
make -j2          # build/vlcflix
make verify       # the gate: static checks, 529 test checks, 15 journey steps
make test         # just the tests (unit, golden image, fault injection)
make asan         # the same tests under AddressSanitizer
make preflight    # just the static checks
```

or with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
ctest --test-dir build
```

C++17. Five system libraries, no vendored code and no third-party headers: SDL2
(window and input), FreeType (text), libpng and libjpeg (artwork), and libVLC from
turn 3. `ffmpeg`/`ffprobe` on `PATH` are used for probing
and artwork; without them the app still scans and browses, it just has no
durations and no thumbnails (and it says so).

## Use

```bash
./build/vlcflix scan --folders /path/to/movies      # walk, probe, draw artwork
./build/vlcflix scan --folders … --no-art           # metadata only
./build/vlcflix status [--json]                     # what is stored
./build/vlcflix settings [--set hwDecode=true]       # validated partial update
./build/vlcflix naming "1917.2019.2160p.WEB-DL.mkv"  # what the parser makes of it
./build/vlcflix doctor                              # paths, tools, log location
./build/vlcflix crash                               # tests the crash log (really crashes)
./build/vlcflix ui                                  # the window
./build/vlcflix ui --play 3                         # the window, playing a film
./build/vlcflix play 3 --wait 5                     # play headlessly, then report
./build/vlcflix play ../samples/ghostlight.mp4 --seek 5 --subtitles --wait 4
./build/vlcflix render --all out/ --size 1600x900    # every screen to PNG, no display
./build/vlcflix render --screen detail --hover 2 --png d.png
./build/vlcflix render --screen browse --focus 3 --png b.png
./build/vlcflix render --play 3 --seek 5 --subtitles --png player.png
```

`ui --frames N` draws N frames and exits, which is how the window loop is tested on a
machine with no display (`SDL_VIDEODRIVER=dummy`). `render` needs no display at all:
every screen is drawn into a pixel buffer and written out, so a layout can be checked
by looking at a file rather than by describing it.

## Installing on Windows

One file: **`releases/VLCFlixStudio-Setup.exe`**, built by
`scripts/build-win-native.sh --setup` (git-ignored, ~70 MB). It carries the program,
SDL2, FreeType, libpng, zlib, libjpeg, `libvlc.dll`, `libvlccore.dll` and all 29 of
libVLC's plugin modules, so nothing else has to be downloaded and there is nothing to
unzip first.

Double-click it. It unpacks into `%LOCALAPPDATA%\Programs\VLCFlixStudio`, adds a
Desktop icon and a Start Menu entry, registers itself in **Apps & features**, and starts
the app. Every step is written to `%LOCALAPPDATA%\VLCFlixStudio\logs\setup.log` —
an installer that fails silently is the worst kind, so the log is opened, appended and
closed for each line.

```
VLCFlixStudio-Setup.exe                 install (if needed), then start the app
VLCFlixStudio-Setup.exe --install-only  install, do not start
VLCFlixStudio-Setup.exe --verify        report state, exit 0 only if complete
VLCFlixStudio-Setup.exe --uninstall     remove program, shortcuts, registry entry
VLCFlixStudio-Setup.exe --dir <path>    install somewhere else
                                        (anything else is taken as "install, then run")
```

Why the app is not a single file: libVLC loads its codecs from `plugins/` at runtime, so
a self-contained binary is impossible without replacing libVLC itself. The honest way to
hand someone one file is to put the directory *inside* it: the stub executable has the
zip appended to it, followed by a 24-byte footer (`VFSUZIP1`, offset, length), and
miniz inflates it on first run. Uninstalling runs the copy the installer kept in the
install folder; it can delete the folder it lives in because it relaunches itself from
`%TEMP%` first, and it retries for five seconds because Windows often keeps a handle
open. Your library, settings and artwork are **not** deleted — uninstalling a player
should not throw away the collection.

VLCFlix 1.x, if present, is left exactly where it is: this is a different application
with its own folder, data directory and uninstall key, and the installer refuses to be
pointed at 1.x's folder rather than migrating it.

**The window's renderer** is chosen by name and defaults to SDL's *software* renderer:
the UI rasterizes every pixel itself, so the GPU only saves a memcpy — and on at least one
Intel machine SDL's default order fell through to Direct3D 9 and crashed inside
`igdumdim64` before the first frame (2.0.2). Ask for the accelerated path if you want it:
`set VLCFLIX_RENDER_DRIVER=direct3d11`. `VLCFlixStudio.exe doctor` prints the policy.

**Adding a library folder** no longer means typing a path: in **Settings** the field has
a **Browse...** button beside it which opens a folder picker — the current path, one row
per folder, ".. up one level", and on Windows the other drives. It scrolls, and *Use this
folder* adds the folder shown through the same validation as a typed path.

**On a small screen** the window shrinks to fit the desktop (96 % of the width, 90 % of
the height, never below 640x420) instead of opening wider than the screen and putting its
own title bar out of reach. Anything clickable under the pointer turns the cursor into a
hand, text fields into an I-beam, and dead space stays an arrow — the cursor is decided
by the same hit areas the click uses, so it cannot promise something a click will not do.

The same build also writes a bare `releases/VLCFlixStudio.exe` and
`dist/win-native/VLCFlixStudio-win64.zip` for running without installing. Put
`ffmpeg.exe` and `ffprobe.exe` in the install folder (or on `PATH`) if you want
durations for files with unusual headers; `doctor` says when they are missing.

## The window

| Screen | What it does |
|---|---|
| Home | Billboard of the newest film with its synopsis, Play / My List / Details, then rails: Continue Watching, Recently Added, My List, one per genre |
| Browse All | Auto-filling grid, wheel, drag and page-key scrolling, with a draggable bar |
| Search | Opens on `/` or a click, filters on title, genre and year as you type |
| My List | The saved films, from `myList.json` |
| Detail sheet | Artwork, title, meta line, the whole synopsis, Play, My List, progress - scrolls when the text is longer than the sheet |
| Settings | Library folders (add by typing a path, remove, or take the bundled samples), the two toggles, and **Scan library**, which runs the real scan |

Keyboard: `/` search, arrows move the card focus (Enter opens it), Page Up / Page Down
scroll a screen at a time, Home and End jump to the top and the bottom, Escape backs
out one layer at a time, Tab-free by design. Mouse: hover lifts a card and shows a
preview panel with the still and the meta line.

**Scrolling is everywhere it can be needed**, because a place you cannot scroll is a
place you cannot reach:

| Where | How it moves |
|---|---|
| Home, Browse All, My List, Search | the wheel, Page keys, Home/End, or dragging the bar on the right edge |
| Each rail on Home | the wheel over that rail moves it sideways; at its end the wheel carries on down the page instead of doing nothing. Arrow keys scroll the rail to keep the focused card visible |
| Detail sheet | the wheel, the page keys or its own bar: a long synopsis scrolls instead of hiding the Play button |
| Settings | the same - with many folders the Scan button is reachable by scrolling rather than being cut off |
| Player | the wheel is volume, which is what a player is for |

A bar is drawn only when there is something to scroll, and it is draggable: press it,
move, release. Hit areas for anything that has scrolled out of view are dropped, so
what is off-screen is never clickable.

Every hit area is produced by the drawing pass that drew it, so a control cannot be
painted somewhere it cannot be clicked. The UI tests use that list, not guessed
coordinates.

## The player

libVLC runs in this process and hands every decoded picture to a callback
(`libvlc_video_set_callbacks`); we copy it out and present it on our own canvas. That
is what the browser build could not do, and it is why three of the ten audited bugs
simply stop existing: there is no separate `vlc.exe` to drive, no HTTP bridge, and
no `<video>` element fighting us for the picture.

| Control | Mouse | Key |
|---|---|---|
| Play / pause | the big button | `Space` |
| Seek | click the scrub bar | `←` `→` (10 s) |
| Volume | the pill | `↑` `↓`, wheel |
| Mute | the Mute button | `M` |
| Subtitles on/off | CC | `S` |
| Sync earlier / later | `−` / `+` top right | `[` `]` (20 ms) |
| Fullscreen | — | `F` |
| Back to browsing | ← | `Esc` |

- **Resume:** a film with saved progress between 2 % and 92 % starts there, once the
  engine reports its length. Below 2 % counts as not started; at 92 % the film is
  finished and leaves Continue Watching (audit A4).
- **Progress** is written every five seconds and when the player closes, through the
  same `Store` the CLI uses. The end card writes the film as finished, which is the
  same rule, so a watched film never sits in Continue Watching at 99 %.
- **Subtitles** come from the sidecar files the scanner found, are parsed by the same
  SRT/WebVTT parser as before, and are drawn by our compositor against the engine's
  clock with an adjustable offset. `samples/ghostlight.en.srt` exists so this path has
  something real to run against.
- **Sync** is `compute_sync()` from turn 1, unchanged: a manual offset says how much
  later the sound should be than the picture, and the plan decides which side pays.
  Positive → libVLC delays the audio (`libvlc_audio_set_delay`). Negative → the sound
  cannot play early, so the **picture** waits: the presenter holds frames for that
  long. With no offset there is no queue and no added latency.
- **Hover preview** plays the generated preview montage muted in the preview panel,
  on its own engine, and stops the moment the pointer leaves.
- **A file that is gone** says so and leaves the browser on screen; it does not show
  a black player and pretend.

### What the sync does *not* do yet

There is no device-latency measurement. The browser could ask WebAudio for
`outputLatency`; libVLC 3 has no equivalent, so `compute_sync(0, manual)` is called
and the *automatic* half of latency compensation is inert. The `autoLatency` setting
is still stored and still toggles, but it currently changes nothing. That is a
limitation, not a bug, and it is the first thing turn 4 should either fix or say
plainly in the UI.

`--data <dir>` puts settings/catalog/media somewhere else; `--log <file>` and
`--console` control logging.

## Where things live

| What | Where |
|---|---|
| Data directory | Linux `~/.local/share/VLCFlixStudio`, Windows `%LOCALAPPDATA%\VLCFlixStudio`, macOS `~/Library/Application Support/VLCFlixStudio`; `--data` or `VLCFLIX_STUDIO_DATA` override it |
| Log | `~/.local/state/VLCFlixStudio/logs/vlcflix-studio.log` (Windows: `%LOCALAPPDATA%\VLCFlixStudio\logs`), rotated at 2 MB × 5 |
| Artwork | `<data>/media/<id>-poster.jpg`, `-backdrop.jpg`, `-preview.mp4` |

The name `VLCFlixStudio` is why this never touches a VLCFlix 1.2.0 install: different
data directory, different executable, different uninstall key. See
`../docs/PORT_PLAN.md` §7.1.

## Logging, because a crash must still leave evidence

`log::init()` is the first statement of `main()`; nothing runs before it. It tries
three directories in order (user data, beside the executable, temp), writes a
header with version/build/argv/PID, and flushes every line. Crash handlers are
installed immediately afterwards: `std::terminate`, SIGSEGV/ABRT/FPE/ILL/BUS/INT,
and on Windows an unhandled-exception filter plus a first-chance vectored handler.
Each writes a `CRASH` record with a stack trace and flushes before the process dies.

A run that dies leaves no `clean exit` record, so the *next* launch reports
"previous run: ENDED UNEXPECTEDLY" and can print the tail of that run
(`vlcflix doctor`). `vlcflix crash` exists to test that path.

## What the tests prove

`make verify` runs four things and they are not interchangeable:
`../tools/preflight.sh` (33 static checks a compiler here cannot make), 529 test
checks, and the 15-step journey through the real binary. `make asan` runs the same 529
checks under AddressSanitizer. The reasoning behind each layer, the negative control
for every layer, and the honest list of what none of them can reach are in
`../docs/TESTING.md`.

**529 checks** (unit + golden image + fault injection):

- every bug from `../docs/AUDIT.md` has a regression test: **A1** year-titled films,
  **A4** a film past 92 % leaves Continue Watching, **A5** `nv12` is not 10-bit,
  **A6** `vlcPassword` must be a string, **A9** a still from an older format is
  missing even though the file exists;
- **seven golden baselines** (`tests/golden/*.png`) compare every screen pixel by
  pixel — per-channel tolerance 6/255, at most 0.15 % of pixels — and the comparator
  self-checks: an identical image reports 0 changed pixels and a 4×4 nudge reports
  exactly 16;
- the UI checks are measurements, not assertions about intent: every screen including
  the empty library draws real content (with a cleared canvas as the negative
  control), clicking the nav, a card, the search field, Play, My List, Settings and
  the close button each does what it looks like, hit areas do not overlap, arrow keys
  move focus and Enter opens the focused film, and 640×420 draws without a crash;
- playback is measured against a real decoder: libVLC decodes `samples/ghostlight.mp4`
  into our buffer at 320×180 with more than one shade, the length comes from the engine,
  seeking the single-keyframe file behind A2 keeps delivering pictures, the presenter
  honours the plan in both directions, pause is confirmed through libVLC's asynchronous
  state, the sidecar subtitle is timed off the engine clock, and stopping saves progress
  while finishing removes the film;
- fault injection: corrupt catalog/settings/JSON/subtitles, a read-only data directory,
  a scan with no ffmpeg, junk video, corrupt artwork, a file deleted mid-playback and
  nonsense UI input all end in a message rather than a crash, a hang or silence.

## Verified end-to-end in this session

- Fresh scan of the eight bundled clips: **8 items, 0 errors, 13.7 s** total,
  including all 24 artwork files; posters are 400×600, backdrops 1280×720, previews
  640×360 × 24 s (five snippets); a poster was opened and looked at.
- Second scan: **0.12 s**, nothing redrawn, nothing re-probed.
- Killed mid-artwork (`kill -9`, 16 of 24 files written): the next scan drew
  exactly the 8 missing files, **rewrote none**, left no temp files.
- Crash test: exit 139, log holds `[CRASH] SIGSEGV` plus nine stack frames, no
  clean-exit record, and the next run reports the previous one died.
- Unwritable log directory: falls back to the directory beside the executable and
  records why in the log that did open.
- Bad settings (`hwDecode="yes"`, port 70000, object password): all rejected with
  exit 4 and nothing written.

## Not verified, on purpose

- **Windows.** No toolchain or machine here: `scripts/build-win-native.sh --check`
  lists exactly what is missing (mingw-w64, NSIS, a Windows VLC, and mingw builds of
  the five libraries) and exits 3 rather than pretending. `platform/proc.cpp` has a
  `CreateProcess` path, `paths.cpp` a `%LOCALAPPDATA%` path, `log.cpp` a DbgHelp
  stack-trace path, and `packaging/` has the installer and manifest for a *new*
  application (own AppId, install dir, uninstall key, data dir; refuses to install
  over 1.x and never touches 1.x's data). All of it is unproven until a Windows build
  actually runs; `../docs/TESTING.md` §10 is the checklist for that day.
- **The window on a real display.** The window, its event loop, its renderer and the
  player inside it were exercised with `SDL_VIDEODRIVER=dummy` (opens, plays a film,
  presents frames, closes, exit 0), and every screen and the player were rendered and
  looked at as PNGs at 1600×900, 1280×720 and 1920×1080 — but nobody has watched it on
  a monitor with a mouse in hand.
- **Audio.** There is no sound card here (libVLC prints `ALSA lib … cannot find card
  '0'` on every attempt, which is the machine, not the app). Playback ran with the default output and
  with `--aout=dummy`; the picture, the clock and the state machine are verified, the
  sound is not. `libvlc_audio_set_delay` is called and the plan is computed, but
  whether the sound actually lands where the maths says has not been heard.
- **Automatic latency compensation.** libVLC 3 cannot report output latency, so the
  device term is always zero. This used to be an `autoLatency` toggle that changed
  nothing; it is now removed from the setting, the settings file, the CLI and the UI,
  which says "not available with libVLC 3 — use − / + in the player" instead.
- Artwork rendering with libVLC instead of ffmpeg is not planned: ffmpeg remains the
  artwork engine, libVLC is the playback engine.
