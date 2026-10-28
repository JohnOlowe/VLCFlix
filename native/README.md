# VLCFlix Studio — the native port

The C++ rewrite of VLCFlix. Turn 1 of three (see `../docs/PLAN_3_TURNS.md`).

**What this is right now:** the whole non-graphical half of the product — logging,
settings, library scanning, metadata, artwork generation, subtitles, sync maths —
as one native binary. It scans a real library and produces the same three assets per
film the web version produced. The window and the player land in turns 2 and 3.

**What it is not yet:** there is no window and no playback in this turn. Nothing
here has been compiled for Windows. Both statements are deliberate, not omissions.

---

## Build

```bash
cd native
make -j2          # build/vlcflix, needs only g++ and make
make test         # 132 checks
```

or with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
ctest --test-dir build
```

C++17, no third-party headers. `ffmpeg`/`ffprobe` on `PATH` are used for probing
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
```

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

`make test` → **132 checks**. Beyond porting the JS cases (naming, sync maths,
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
- **The window and the player** — turns 2 and 3.
- Artwork rendering with libVLC instead of ffmpeg is not planned: ffmpeg remains the
  artwork engine, libVLC is the playback engine.
