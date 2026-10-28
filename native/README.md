# VLCFlix Studio — the native port

The C++ rewrite of VLCFlix. Turn 2 of four (see `../docs/PLAN_4_TURNS.md`).

**What this is right now:** a native window with the whole browsing experience —
home billboard and rails, Browse All, search, My List, the detail sheet, the settings
sheet with a working scan, keyboard browsing and the hover preview — drawn by our own
software renderer, above the turn-1 library engine (scan, probe, artwork, subtitles,
sync maths, crash-proof logging).

**What it is not yet:** playback. Clicking Play says so out loud instead of doing
nothing; libVLC, synchronisation and subtitles are turn 3. Nothing here has been
compiled for Windows. Both statements are deliberate, not omissions.

---

## Build

```bash
sudo apt install build-essential pkg-config libsdl2-dev libfreetype-dev libpng-dev libjpeg-dev
cd native
make -j2          # build/vlcflix
make test         # 210 checks, includes the UI tests
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
./build/vlcflix render --all out/ --size 1600x900    # every screen to PNG, no display
./build/vlcflix render --screen detail --hover 2 --png d.png
./build/vlcflix render --screen browse --focus 3 --png b.png
```

`ui --frames N` draws N frames and exits, which is how the window loop is tested on a
machine with no display (`SDL_VIDEODRIVER=dummy`). `render` needs no display at all:
every screen is drawn into a pixel buffer and written out, so a layout can be checked
by looking at a file rather than by describing it.

## The window

| Screen | What it does |
|---|---|
| Home | Billboard of the newest film with its synopsis, Play / My List / Details, then rails: Continue Watching, Recently Added, My List, one per genre |
| Browse All | Auto-filling grid, wheel and arrow-key scrolling |
| Search | Opens on `/` or a click, filters on title, genre and year as you type |
| My List | The saved films, from `myList.json` |
| Detail sheet | Artwork, title, meta line, synopsis, Play, My List, progress |
| Settings | Library folders (add by typing a path, remove, or take the bundled samples), the two toggles, and **Scan library**, which runs the real scan |

Keyboard: `/` search, arrows move the card focus (Enter opens it), Escape backs out
one layer at a time, Tab-free by design. Mouse: hover lifts a card and shows a preview
panel with the still and the meta line.

Every hit area is produced by the drawing pass that drew it, so a control cannot be
painted somewhere it cannot be clicked. The UI tests use that list, not guessed
coordinates.

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

`make test` → **210 checks**. Beyond porting the JS cases (naming, sync maths,
subtitle conventions, rails), each bug from `../docs/AUDIT.md` has a regression
test, and the naming one carries a **negative control**: a copy of the old
algorithm is run against the same filenames and asserted to be wrong on all four,
so the test cannot pass by testing nothing.

- **A1** year-titled films: `1917.2019.2160p` → `1917` / 2019,
  `Blade.Runner.2049.2017` → `Blade Runner 2049` / 2017.
- **A4** a film watched past 92 % is removed from progress, so it leaves
  Continue Watching.
- **A5** `nv12` is not 10-bit, `yuv420p10le` is.
- **A6** `vlcPassword` must be a string; `hwDecode` must be a boolean.
- **A9** a still from an older format is treated as missing even though the file
  exists.

The 78 UI checks are measurements, not assertions about intent:

- every screen, including the empty library, draws at least a floor fraction of
  non-background pixels, with a **negative control** (a cleared canvas must measure
  zero content, or the measurement means nothing);
- clicking the nav, a card, the search field, Play, My List, Settings and the close
  button each does the thing it looks like, driven through the same `handle()` the
  window calls;
- Play reports the request and says the player is not wired up yet — turn 2 does not
  pretend to play;
- hit areas do not overlap, so one click is always one action;
- arrow keys move the focus and Enter opens the focused film;
- the minimum window size (640×420) draws every screen without a crash.

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

- **Windows.** No toolchain or machine here. `platform/proc.cpp` has a
  `CreateProcess` path, `paths.cpp` a `%LOCALAPPDATA%` path, and `log.cpp` a DbgHelp
  stack-trace path; all three are written but never executed. Treat them as
  unproven until the first Windows build runs.
- **The window on a real display.** The window, its event loop and its renderer were
  exercised with `SDL_VIDEODRIVER=dummy` (opens, draws, presents, closes, exit 0), and
  every screen was rendered and looked at as a PNG at 1600×900, 1280×720 and
  1920×1080 — but nobody has watched it on a monitor with a mouse in hand. The player
  is turn 3.
- Artwork rendering with libVLC instead of ffmpeg is not planned: ffmpeg remains the
  artwork engine, libVLC is the playback engine.
