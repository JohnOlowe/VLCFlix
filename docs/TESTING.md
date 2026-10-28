# Testing VLCFlix Studio

This is the answer to "how do we know it works?" — the layers, what each one can and
cannot prove, how to run them, and the honest list of what no automated test on this
machine can reach.

Everything below is run from the repository root unless a command says otherwise.

```bash
cd native
make verify        # the gate: static checks, all tests, the journey   (~1 min)
make asan          # the same tests under AddressSanitizer              (~1.5 min)
make preflight     # only the static checks                             (instant)
```

`make verify` is the exit criterion for turn 4: from a fresh clone with a C++
compiler, `pkg-config`, `ffmpeg` and `libVLC` installed, it must go green without any
setup step, any bundled state, or any network access.

---

## 1. Why six layers and not one

A test suite that only checks the happy path through a library that already exists on
the developer's machine proves that *this machine* can run the app today. Each layer
below answers a question the others cannot:

| Layer | Question it answers | Failure it catches that nothing else does |
|---|---|---|
| 1. Unit | Do the rules hold in isolation? | Parsing, naming, sync maths, progress rules |
| 2. Golden image | Does the window still look like the window? | A layout change that silently pushes a label off the card, or makes a screen blank |
| 3. End-to-end journey | Do the modules agree, on files nobody prepared? | Scan → catalog → render → play → resume disagreeing about an id or a path |
| 4. Fault injection | What happens when things are wrong? | Crashes, silence, a black window with no explanation |
| 5. Sanitizer | Is there memory the code should not touch? | The *intermittent* bug: one that passes half the time |
| 6. Static preflight | Do the repository's rules hold? | A source missing from one build system, an identity that would make the Windows build an upgrade, a promised `make` target that does not exist |
| Machine checklist | What cannot run here? | Windows, a real display, real sound, a GPU |

The layers are deliberately not substitutes: a golden image cannot tell you the file
was unreadable, and a unit test cannot tell you the app opens at all.

---

## 2. Layer 1 — unit tests

`native/tests/run_tests.cpp` · part of `make test`

Pure logic, no window, no display, ~1 s. One `TEST(...)` line per rule, each `CHECK`
counting as one check. Notable groups:

* `naming:` — release filenames, including the audit-A1 case where the title *is* a
  year (`Blade.Runner.2049.2017.mkv` must not become `Blade Runner 2049` with year
  `2049`).
* `sync:` — all 14 cases ported from the original `sync-math.js`, so the port can be
  compared to the thing it replaces.
* `store:` — the finish rule (A4: crossing 92 % removes the film from Continue
  Watching, not just clamps it) and settings validation (A6: `vlcPassword` of any JSON
  type is refused rather than stored).
* `subtitles:` — naming convention, labels, SRT and WebVTT cue parsing.

## 3. Layer 2 — golden images, with pixel diffs

`native/tests/golden_tests.cpp` + `native/tests/golden/*.png` · part of `make test`

Every screen is rendered at 1280×720 with the *same* catalog, into a canvas, and
compared to a committed baseline. The comparison is per-channel with a tolerance:

* `kThreshold = 6` — a channel must move by more than 6/255 to count as changed.
* `kAllowed = 0.0015` — 0.15 % of pixels may differ before the test fails.

The tolerance exists because text antialiasing is not bit-identical across FreeType
and libpng versions, and a test that fails on a library upgrade teaches people to
ignore it. Layout does not move by 0.15 % of a 1280×720 screen; a broken card does.

Baselines: `home`, `browse`, `mylist`, `search`, `detail`, `settings`, `empty` (a
library with no items, so the empty state is covered too).

When the test fails it names the worst pixel:

```
browse: 2.401% pixels changed, worst channel delta 188 at (742,318)
```

The layer tests itself first — an identical image must report exactly 0 changed
pixels, and a 4×4 magenta nudge must report exactly 16. Without those two self-checks
a broken comparator would "pass" everything.

**Deliberately changing the UI?** Re-baseline and *look at the new PNGs* before
committing them:

```bash
VLCFLIX_UPDATE_GOLDEN=1 ./build/native-tests   # rewrites native/tests/golden/*.png
```

One screen, one baseline name. (A duplicated `check_screen("empty")` call once diffed
the Browse screen against the Empty baseline: 61 % of pixels changed, which looked
like a layout catastrophe and was a typo in the test.)

## 4. Layer 3 — the end-to-end journey

`native/tests/journey.sh` · part of `make verify` and CTest

One continuous user story, driven through the **real binary** with real command-line
arguments, on a library built during the run by `ffmpeg` — different titles, different
lengths, a 10-bit file, a sidecar JSON, a sidecar `.srt`, and a file that is not a
video at all. Nothing from `samples/` is used, so the test proves the app works on
files it has never seen.

The 15 steps:

1. preconditions (ffmpeg, binary present)
2. build the synthetic library
3. scan it; the broken file is named in the output, not just counted
4. sidecar metadata wins over the filename (title, year, genres)
5. the 10-bit file is reported as 10-bit and the 8-bit one is not (A5)
6. every screen renders with no display, and the PNGs are not blank
7. play with `--seek` and `--subtitles`: frames decoded, subtitle cue on screen,
   length came from the engine
8. progress was written by playing
9. relaunching the binary resumes at the saved position
10. the broken file is refused **with a reason**, not a black window
11. a second scan draws nothing (incremental work, not a full redraw)
12. a deliberate crash leaves a log with a `CRASH` record and a stack location
13. the next run says the previous one ended unexpectedly (plain text and `--json`)
14. a window that refuses to open still writes a log, and says why (exit 5)
15. the play and resume runs logged their start, the film, and their own clean exit
    - and a clean run is *not* mistaken for a crash

```bash
tests/journey.sh                     # against build/vlcflix
tests/journey.sh --keep              # keep the temp directory and print its path
tests/journey.sh --binary /path/bin  # test a build made elsewhere (CTest does this)
```

A failing step prints the step number, the command output and the work directory, and
sets `--keep` itself: a failure is exactly when the evidence has to survive.

## 5. Layer 4 — fault injection

`native/tests/fault_tests.cpp` · part of `make test`

Every case asserts two things: **no crash or hang**, and **the program says what
happened** — in the message the user sees, in the log, or both. Silence is the failure
mode being tested for.

* adversarial filenames, JSON, subtitles and settings (control characters, 4 kB names,
  truncated documents, wrong types)
* a corrupt catalog and a corrupt settings file are survived and reported
* a settings patch with the wrong JSON type is refused and the old file is kept
* a read-only data directory is reported instead of silently lost
* a scan with no usable tools, a missing folder, and a junk file
* corrupt artwork on disk, and a still deleted after the catalog recorded it
* a non-video file and a missing file are refused with a reason
* the UI survives nonsense input and an absurd window size
* a film deleted while it is playing does not crash the player

## 6. Layer 5 — the sanitizer (what proves an intermittent bug is gone)

```bash
cd native && make asan     # builds build/native-tests-asan, runs the same 339 checks
```

This is not a fifth copy of the same tests: it is the layer that converts a bug that
passes four times in five into a deterministic failure with a file and a line.

It earned its place this turn. The artwork preview job captured its segment list by
reference while the list lived only inside an `if` block that closed before the jobs
were joined. The preview thread therefore read a destroyed vector, and ffmpeg received
things like:

```
[warn] preview failed for Second.Feature.2019.mp4: Invalid duration for option t: 3219238484152785303456914394985
```

Roughly half of all scans drew no preview at all. The fix is one word in the capture
list (`[&, segments]`). Two things keep it fixed:

* `faults: a full artwork pass draws all three pieces, every single time` — three full
  poster/backdrop/preview passes and byte-size checks, because *one* pass passed by
  luck;
* the ASan build, which failed on the old code with
  `AddressSanitizer: stack-use-after-scope` on the first pass and is clean on the new
  code. That negative control — break it, watch ASan name it, fix it, watch it go —
  is what makes this a regression test rather than a hope.

## 7. Layer 6 — static preflight (the part of the checklist a machine can hold)

```bash
tools/preflight.sh        # 33 checks, no compiler needed
```

Some rules cannot be checked by building on this machine, but they *can* be checked by
reading the tree. `tools/preflight.sh` does that:

* every `.cpp` appears in **both** build systems (a source listed in neither is how a
  test suite silently stops testing a file);
* every `#if`/`#ifdef` has its `#endif` — a broken conditional compiles fine until the
  platform that is not built here;
* the Windows identity is a *new* application: product, slug, data directory, install
  directory, uninstall key, AppId, manifest assembly identity all distinct from
  VLCFlix 1.2.0, and the installer names 1.2.0's directory **only** in order to refuse
  it;
* the house rules from `AGENTS.md`: `main.cpp` includes no SDL header, no credential
  file is tracked, no tracked file is over 5 MB, no token-shaped string exists anywhere
  in the tree;
* the documentation's promises exist: `make verify`, `make test`, `make asan`, the
  executable scripts and the seven golden baselines.

Its own negative control: change the installer's `APP_EXE` back to `VLCFlix.exe` and
the check fails with `installer executable name is not the new product's`.

## 8. A regression test for each known bug, and its negative control

"Fixed" is a claim. Each row below has a test that fails on the old behaviour; the
negative control is what was broken on purpose to see the test go red.

| Bug (from `docs/AUDIT.md`) | Test that holds it fixed | Negative control |
|---|---|---|
| A1 title that is a year is eaten by the parser | `naming: titles that are years (audit A1)` | restore the "last 4 digits are the year" branch |
| A2 sparse-keyframe files could not seek | `engine: seeks a single-keyframe file the ffmpeg pipe engine could not (A2)` | point the engine at the ffmpeg pipe |
| A4 finished films were never finished | `store: progress finish rule (A4)`, `player: … finishing removes it from Continue Watching (A4)` | clamp the position instead of erasing the entry |
| A5 10-bit detection matched `nv12` | `artwork: … deep colour (A5) …`, journey step 5 | match on `"12"` in the format name |
| A6 any JSON type accepted for a secret | `store: … settings validation (A6)`, `faults: a settings patch with the wrong type is refused` | drop the type check |
| A9 stills were not versioned | `artwork: … versioning (A9)` | trust the file's existence instead of its version |
| Unreadable file kept with no marker (found turn 4) | `faults: a non-video file and a missing file are refused with a reason`, journey step 10 | remove `Item::unreadable` |
| Zero-decode file reported as `Ended` and written as watched (found turn 4) | `faults: a film deleted while it is playing…`, `faults: the UI survives nonsense input…` neighbours, journey step 10 | treat `Ended` as success |
| Lost artwork preview (found turn 4) | `faults: a full artwork pass draws all three pieces, every single time` + `make asan` | capture `[&]` instead of `[&, segments]` |
| `autoLatency` toggle that changed nothing | settings golden baseline + `settings` sheet render | re-add the row and watch the golden diff |
| `run_process` returned empty output for every Windows child (found by running the exe, §11) | `processes: eight children started at once each keep their own output` | the old `PeekNamedPipe` drain: every child came back with `out=0 err=0` |
| concurrent children inherited each other's pipes (found by running the exe, §11) | same test — 8 children at once, each must return *its own* text | `bInheritHandles = TRUE` without a handle list: under Wine, 3 failures + 3 timeouts out of 8 |
| `write_png` wrote zero bytes across a CRT boundary (found by running the exe, §11) | `png: written bytes are real, complete, and decode to the same size` | pass a `FILE*` to `png_init_io` instead of a callback |
| a crash left no log (requirement 2) | journey steps 12–14 | disable the crash handlers, or write the log after the window opens |
| `--json` output polluted by the banner | journey step 4 (`{"` must be the first byte of stdout) | print the banner to stdout unconditionally |

### Scrolling: the screen that fits is as important as the one that does not

Scrolling is tested as a *state*, not as a picture, because "the wheel does nothing"
looks exactly like "the wheel worked" in a screenshot. Six tests in
`native/tests/ui_tests.cpp` drive the real input path and read back the offset the app
itself reports:

| Test | What it proves | Its negative control |
|---|---|---|
| the wheel scrolls the page on all four list screens | Home, Browse, My List and Search all move and stop exactly at their last pixel | scrolling back up returns to 0 and does not go negative |
| a rail scrolls sideways | the wheel over a rail moves that rail and does **not** move the page | at the rail's end the same wheel moves the page, so the input is never swallowed |
| the detail sheet scrolls to the end of a long synopsis | a 14-sentence synopsis scrolls, and the Play button is unreachable until it is on screen — then it appears and is clickable | the rendered frame before and after must differ in ≥5% of pixels |
| the settings sheet scrolls to its Scan button | eight library folders push the button below the fold, and scrolling reaches it | the button has no hit area until it is visible |
| Page Down / End / Home | keyboard scrolling moves a page, jumps to the end, returns to the top | a short synopsis in a large window reports **no** scroll limit and the wheel changes nothing |
| clicking and dragging the bar | press, move, release scrolls; movement after release does not | offsets are compared before and after, so a stuck drag fails |

Two layout bugs came out of writing these: the detail sheet claimed it could scroll by
~90 px when it fitted perfectly (its content height counted the art twice), and its
buttons were drawn at a fixed height instead of moving with the text — which is the
difference between "the Play button exists" and "the Play button can be reached".

## 9. What is verified, and what is not

Verified on this machine (Linux, no display, no sound card), in this turn:

* 412 checks across layers 1, 2 and 4 — `0 failure(s)`
* 33/33 static preflight checks
* 15/15 journey steps through the real binary, run repeatedly, including three
  consecutive clean artwork passes
* `make asan`: 412 checks clean, and the pre-fix code failing under it
* CMake/CTest: 2/2 tests pass (`native-tests`, `journey`) from a build directory
  outside the tree
* `make verify` green from a fresh clone
* the Windows executable **builds**, its payload is complete (136 imported libraries
  checked, 0 missing), and it **runs**: under Wine 11.19 it scans a real folder,
  draws artwork, plays a film, renders all six screens and logs a crash — §11

**Not verified — and not claimed:**

| Area | Why not | How to check it on a machine that has it |
|---|---|---|
| **Windows 10/11 itself** | the exe is executed under Wine 11.19 (§11), which is an impression of Windows, not Windows | run `scripts/windows-selfcheck.ps1` (it ships in the zip) on a real machine, or enable `docs/ci/windows.yml` |
| The window on a display | no X11/Wayland here, and Wine's `ui` command correctly refuses to open one | `./build/vlcflix` on a desktop; resize, full-screen, hover a card, press `f` |
| Audio | no sound card; libVLC prints `ALSA lib … cannot find card '0'`. Playback itself is verified (decoded/presented advance) | play a sample with sound and check the `−/+` latency keys move the sound earlier/later |
| GPU video decode | no Vulkan video queue on this box; `-hwaccel auto` falls back to software (proven by its exit code and output file) | `hwDecode` on, scan, and check the log for a hardware device |
| Automatic device-latency compensation | libVLC 3 cannot report output latency | nothing to do — the UI now says so instead of offering a toggle that does nothing |
| Installer behaviour side by side with 1.2.0 | no NSIS, no Windows machine; `scripts/build-win-native.sh --check` reports exactly what is missing and exits 3 | §10 |
| Code signing / SmartScreen | out of scope | — |

The rule for the future is the one in `AGENTS.md`: if it was not run, it is not
verified, and it gets listed here rather than described as working.

## 10. The machine checklist for what cannot run here

Run these on the target machine and paste the results into a release note. Each line
is pass/fail; "looks fine" is not a result.

Every line below is manual *today*, and that is deliberate: none of it can be faked
by a Linux build. The files that make it possible — `native/packaging/installer.nsi`,
`native/packaging/app.manifest`, `scripts/build-win-native.sh` — are in the tree and
are checked statically by `tools/preflight.sh`.

**Windows install identity (requirement: a new application, not an upgrade)**

1. `VLCFlixStudio-Setup-<version>.exe` installs to
   `%LOCALAPPDATA%\Programs\VLCFlixStudio`, and 1.2.0 stays in
   `…\Programs\VLCFlix` with its `VLCFlix.exe` untouched.
2. Settings → Apps shows **two** entries: `VLCFlix` and `VLCFlix Studio`.
3. `%LOCALAPPDATA%\VLCFlix` (1.2.0's library, settings, logs) is byte-identical after
   the install — compare `dir /s` before and after.
4. Both apps run at the same time; the single-instance mutex stops the *second copy of
   the new app*, not 1.2.0.
5. Uninstalling `VLCFlix Studio` leaves `VLCFlix` runnable and its data intact.
6. Uninstalling `VLCFlix` leaves `VLCFlix Studio` runnable.

**Logs survive a crash on open (requirement: logs always exist)**

7. Start the app, kill it with Task Manager, start it again: the new log says the
   previous run ended unexpectedly.
8. Force a crash: `VLCFlixStudio.exe crash` writes a `CRASH` record with a stack
   trace, and the file is on disk after the process is gone.
9. Make the log directory unwritable: the app still writes a log next to the exe or in
   `%TEMP%`, and says in that log why it moved.

**A real display**

10. Window opens at 1280×720, all six screens draw text at 100 % and at 150 % display
    scaling.
11. Full-screen (`f`), then Escape, returns to the same place in the library.
12. Hover a card: the preview clip plays in the card and stops when the pointer
    leaves.
13. Resize to the minimum size, then maximise: nothing overlaps or is clipped.

**Playback with sound**

14. A sample plays with sound; `−` and `+` move the sound earlier and later and the
    offset survives a restart.
15. `CC` turns the sidecar subtitles on, and they stay in sync after a seek.
16. Closing the window mid-film and reopening continues where it stopped; letting a
    film reach the end removes it from Continue Watching.
17. Pull the network drive / rename the folder while a film is playing: the player
    says the file is gone and does not hang.

## 11. The Windows build: built, executed, and the five bugs that found

This was the largest "not verified" item in section 9. It is now closed for everything
that can run without a Windows desktop: the executable has been **run**, repeatedly,
and every command below is a transcript of a real execution.

**Built.** `scripts/build-win-native.sh` produces a real `VLCFlixStudio.exe` from this
source tree on a Linux box. The recipe, all of which ran in this session:

| Piece | Where it comes from | Why that one |
|---|---|---|
| compiler | llvm-mingw 20261020 (clang + lld, UCRT) | no install, no root: a complete Windows toolchain in a tarball |
| SDL2 | official `SDL2-devel-2.32.10-mingw` package | the window and input layer |
| FreeType | built from source here with `--without-harfbuzz` | the MSYS2 build drags in glib, graphite2 and a GCC runtime DLL just to draw text |
| libpng, zlib, libjpeg-turbo | MSYS2 mingw64 packages (import libraries) | small, standard, their DLLs are shipped |
| libVLC | VideoLAN's own `vlc-3.0.24-win64.zip` | the app must ship the exact libVLC it linked against; the import library is generated from `libvlc.dll` with `gendef` + `dlltool` |

**Checked.** `scripts/check-win-payload.sh` walks every DLL imported by the exe, by
SDL2, libpng, libpng16, libjpeg, libvlc, libvlccore *and* by any `ffmpeg.exe` /
`ffprobe.exe` staged next to it, and requires each one to be in the payload or part of
Windows: **136 imports, 0 missing**. It also refuses to call the payload complete
without the plugins libVLC needs (`libavcodec_plugin.dll`, the mp4/mkv/avi demuxers, the
h264 packetizer, the vmem and d3d11 video outputs, the mmdevice and directsound audio
outputs). The PE is verified as x86-64, GUI subsystem, with the manifest (DPI awareness)
and version block embedded as resources.

### How it is executed here, without Windows

Wine, one specific version, in one specific way — this is written down because the
first attempt failed and cost most of a turn:

```
# Kron4ek Wine 11.19 amd64-wow64 (11.20 is BROKEN: see below)
export WINEPREFIX=$PWD/wp19 WINEARCH=win64 WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml="
mkdir -p "$WINEPREFIX" && ./bin/wineboot -u
cd .../dist/win-native/payload
wine ./VLCFlixStudio.exe scan --folders 'Z:\home\user\VLCFlix\samples'
```

*Wine 11.20 and 11.20-staging die before creating a prefix*, with
`virtual_init host addr space limit: 0xffffffffffff0000` followed by a SIGSEGV in
`create_view → map_view → virtual_alloc_first_thread_data`. That is an upstream
regression, not this sandbox: it reproduces in Bottles (bottlesdevs/Bottles#4903) and
in Paint.NET's Wine tracker (paintdotnet/Paint.NET-on-Wine#58), both on exactly those
two versions. A fixed `mmap` at both `0x7ffe0000` and `0x7ff000000000`, `mmap_min_addr
= 4096`, `max_map_count = 65530` and an 8 MB stack were all checked and are fine. **Wine
11.19 works**, and that is what the evidence below was produced with.

### What was executed, and what it printed

| Command | Result |
|---|---|
| `version` | `VLCFlix Studio 2.0.0-turn1` |
| `doctor` | log path, data dir, media dir, exe dir, `ffmpeg found`, `ffprobe found`, `catalog 8 item(s)`, `previous run clean` |
| `scan --folders Z:\…\samples --no-art` | **8 item(s), 0 error(s)** — every film with its real length (`0:16`) and resolution (`1280x720`) |
| `scan` (with artwork, 8 ffmpeg children at once) | **8 of 8 artwork set(s) drawn**, `poster backdrop preview` on every row |
| `render --screen home|browse|mylist|search|detail|settings` | six PNGs written, 25 kB – 361 kB each (`docs/evidence/windows-browse.png`, `windows-detail.png`) |
| `play 1 --wait 4` | `state=playing time=3.15 s length=16.00 s decoded=99 presented=99 sync(video=0 audio=0)` |
| `crash` | process dies with the deliberate access violation, **and the log has the `CRASH` record with a 21-frame stack** — the requirement that a crash still leaves a log, proved on Windows |
| `ui --frames 3` | `could not create a window` → exit 5. Correct: Wine here has no display, and the app reports it instead of vanishing |
| `render` ×6, rebuilt with the scrolling UI | all six screens still render; `docs/evidence/windows-home-scrolling.png` is the Windows binary drawing a rail with its own scroll bar |

The rendered screens are the real UI, drawn by the Windows binary: real fonts from
FreeType, real posters decoded from the JPEG/PNG files ffmpeg had just produced on
Windows, real text layout. That is a much stronger statement than "it compiles".

### The five bugs that only running it could find

None of these could be seen from Linux, from the cross build, or from the tests:

1. **`run_process` returned empty output for every child.** The Windows branch polled
   the pipes with `PeekNamedPipe` and drained nothing, so `ffprobe` looked like it
   printed nothing, every film was "unreadable video", and `have_binary()` said
   "ffmpeg found" for a binary that could have been anything. Fixed by reading each
   pipe with a blocking `ReadFile` on its own thread; EOF then happens exactly when the
   child and every copy of its write handle are gone.
2. **Concurrent children stole each other's pipes.** `CreateProcess` was called with
   `bInheritHandles = TRUE`, which inherits *every* inheritable handle in the process:
   with a scan running eight probes at once, each child also received another child's
   pipe write end, so pipes never reached EOF and Wine refused launches outright
   (`CreateProcess failed, error 1359`, and three timeouts). Fixed with an explicit
   `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, and process *creation* is serialised (children
   still run in parallel, so a scan is no slower). Measured: eight concurrent probes
   went from 3 failures + 3 timeouts out of 8, to 8/8 in 10.5 s.
3. **`write_png` could not write a single byte.** Our `libpng` on Windows is built
   against a different C runtime from the app, so passing it a `FILE*` from our CRT is
   undefined behaviour between two runtimes: libpng's `fwrite` failed
   (`libpng error: Write Error`), screenshots stayed at zero bytes, and `render` died
   with an integer divide-by-zero inside `msvcrt`. Fixed by writing through a libpng
   callback into memory and writing the file ourselves, then reading it back to prove
   the size matches.
4. **Double-clicking the installed app did nothing at all.** The CLI defaulted to
   `help`, so the Desktop and Start Menu shortcuts launched a GUI-subsystem program
   that printed a usage text to a console nobody could see and exited. Found by
   installing it and launching the shortcut the way a user would; with no arguments the
   app now opens its window, and if the window cannot be created it shows a message box
   pointing at the log instead of disappearing silently.
5. **A one-line honesty bug**: `doctor` claimed the tools were present because a process
   had started. `have_binary()` now requires `exit code 0` *and* non-empty output.

Bugs 1 and 3 were found only because the app's own log recorded the failure honestly
(`ffprobe failed … : `, `cannot open … for writing`) instead of swallowing it.

### The artifact, and the executable on its own

`scripts/build-win-native.sh --zip` packs the whole runnable set into
`dist/win-native/VLCFlixStudio-win64.zip` (69 MB: the exe, SDL2, libpng, zlib, libjpeg,
libVLC 3.0.24 and its 29 plugin modules). Both the payload and the zip are git-ignored,
and so is the copy kept for handing out: **`native/releases/VLCFlixStudio.exe`**,
written by the same script on every build. The bare exe is 5.6 MB and is *not*
self-contained - it needs the DLLs from the zip beside it - which is why the two exist
together.

### The installer: one file to hand somebody

`scripts/build-win-native.sh --setup` also produces **one** `.exe` —
`native/releases/VLCFlixStudio-Setup.exe` (71 MB, git-ignored) — that carries
everything: the program, SDL2, FreeType, libpng, zlib, libjpeg, `libvlc.dll`,
`libvlccore.dll` and all 29 plugin modules. It is `native/src/setup/setup.cpp` linked
as a GUI-subsystem stub with the payload zip appended to it and a 24-byte footer after
that (`VFSUZIP1`, offset, length); miniz inflates it on first run.

| Action | Executed under Wine 11.19 | Result |
|---|---|---|
| `VLCFlixStudio-Setup.exe` with no arguments (the double-click path) | installs **372 files**, then starts the app | folder appears at `drive_c/users/<user>/AppData/Local/Programs/VLCFlixStudio`, `setup.log` names every step |
| `--verify` | exit **0** | registry entry, exe, `libvlc.dll`, >20 plugin files and both shortcuts all present |
| shortcuts | `Desktop\VLCFlix Studio.lnk`, `Start Menu\VLCFlix Studio.lnk`, `Start Menu\Uninstall VLCFlix Studio.lnk` | 972 B / 972 B / 1028 B; `strings -el` on the `.lnk` shows the target `…\Programs\VLCFlixStudio\VLCFlixStudio.exe` |
| Apps & features entry | `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\VLCFlixStudio` | `DisplayName`, `DisplayVersion 2.0.0`, `DisplayIcon`, `InstallLocation`, `UninstallString`, `QuietUninstallString`, `EstimatedSize`, `NoModify`, `NoRepair` |
| the installed app | `version` → `2.0.0-turn1`; `scan --folders …` → **8 item(s), 0 errors**; `play 1 --wait 3` → `state=playing … decoded=70 presented=70`; `render --screen settings` → 58,923 B PNG | the installed copy is the working application, not a launcher shell |
| reinstall over itself | removes the previous copy first, then installs | no stale plugins, and the library/settings are kept |
| launch with no arguments | prints `could not create a window …` and exits 5 | correct here (no display under Wine); on a desktop it opens the window — that is what the shortcut does |
| `--uninstall --quiet` | install folder **gone**, registry key gone, all three shortcuts gone | user data (`catalog.json`, `media/`, `progress.json`) deliberately kept; the log ends `uninstall: files removed` |
| 1.x side by side | a fake `…\Programs\VLCFlix\VLCFlix.exe` was left in place during an install | untouched (2 files before, 2 after); `--dir` pointed at that folder **refuses** and exits 1 |

Two things this proves that a test cannot: an uninstaller can delete the directory it is
running from (it copies itself to `%TEMP%`, relaunches with `--finish-uninstall`, and
retries the delete for five seconds because Windows keeps handles open), and no step
depends on a file the user has to remember to run first.

### The manifest bug: the one failure that happens before `main()`

The first run of the installer on a real Windows machine ended before any of this code
ran:

> The application has failed to start because its side-by-side configuration is
> incorrect. Please see the application event log or use the command-line sxstrace.exe
> tool for more detail.

That is `ERROR_SXS_CANT_GEN_ACTCTX` (14001), raised by the loader while reading the
application manifest. **Cause:** `native/packaging/app.manifest` carried a
`description` *attribute* on `<assemblyIdentity>`. Microsoft's manifest schema defines
exactly six attributes there — `type`, `name`, `language`, `processorArchitecture`,
`version`, `publicKeyToken` — and a description is a **child element** of `<assembly>`,
not an attribute. An out-of-schema manifest is rejected whole, so the process never
starts. Nothing here could see it: Wine does not validate manifests, and the app cannot
log a failure that happens before `log::init()`.

**Fixed in two independent ways, because one fix is not enough for a failure with no
log:**

| Change | Why |
|---|---|
| `description` moved to a `<description>` child element | the actual defect |
| every `<dependency>` removed | nothing in the app or the installer imports `comctl32.dll`; a dependent assembly is the only thing side-by-side has to *resolve*, so an empty list removes the failure mode instead of trusting it |
| `processorArchitecture="*"` | the documented "all platforms" value, instead of naming one |
| `scripts/check-win-manifest.py` | enforces the attribute whitelist, the root/namespace/version, `requestedExecutionLevel asInvoker`, no comments containing `--`, the 64 KB resource limit, and — given the built exe — that the manifest is embedded **exactly once, byte for byte** |
| `tools/preflight.sh` runs it, with a negative control | injecting `description="…"` back into a temp copy must make the check **fail**; if the check passes it, the test fails instead |
| `scripts/build-win-native.sh` runs it twice | once on the file before `windres`, once against the linked `VLCFlixStudio.exe` and `setup-stub.exe`, and refuses to publish either if it fails |

`make verify` therefore now fails on the exact defect that stopped the Windows build.
What it still cannot do is run Microsoft's own parser: Wine ignores manifests. The
manifest is schema-clean and has no dependent assemblies to resolve, which is the
strongest statement available without a Windows machine — and `sxstrace.exe` on the
target machine is the arbiter if anything remains.

`ffmpeg.exe` and `ffprobe.exe` are **not** bundled (166 MB each, and they are the
user's choice of build). Without them the scan still runs and lists the films, but
there are no durations and no artwork, and `doctor` says exactly that:
`ffmpeg MISSING (no artwork)` / `ffprobe MISSING (no durations)`.

### Still not verified, and still not claimed

| Area | Why not |
|---|---|
| Real Windows 10/11 itself | Wine is a very good impression of Windows, not Windows; `kernel32` behaviour under Wine is not proof about the real thing |
| The window on screen, and therefore *interactive* scrolling on Windows | Wine here has no display; `ui` is exercised on Linux with SDL's dummy driver, and the drawing is exercised by `render` on Windows. The wheel plumbing (`SDL_MOUSEWHEEL`, both axes, `MOUSEBUTTONUP` for drag release) compiles and is exercised on Linux, but a wheel turn on Windows has not been performed |
| Audio output | no sound card under Wine (`ALSA lib … Unknown PCM default`); playback itself is verified (decoded/presented counters advance) |
| A real Explorer, Start Menu and Add/Remove Programs | Wine implements shortcuts and `HKCU` convincingly, and the installer was executed there, but a real Windows shell has not drawn these icons or run this uninstaller. The registry key, the `.lnk` targets and the delete were all verified; what is unproven is the shell's rendering of them |
| Side by side with a real 1.2.0 | verified against a stand-in directory (`%LOCALAPPDATA%\Programs\VLCFlix\VLCFlix.exe`): the installer ignores it and refuses to be pointed at it. A machine with the real 1.x installed has not been used |
| Code signing, SmartScreen | out of scope, and stated on the download page |

Two independent ways to close the rest: `scripts/windows-selfcheck.ps1` (ships inside
the release zip: it runs the exe, scans, renders every screen, plays a film and packs
the screenshots, `status --json`, the app log and a pass/fail report into
`windows-selfcheck-evidence.zip`), and `docs/ci/windows.yml` (the same, unattended on
`windows-latest` with MSYS2's mingw-w64 GCC — a second, independent toolchain). The
workflow is parked under `docs/` because GitHub refused the push without the `workflow`
scope; it needs to be copied to `.github/workflows/windows.yml` to go live.

## 12. How to add a test

* **A rule with no pixels** → a `TEST(...)` block in `run_tests.cpp`. One test per
  rule; assert the exact expected value, not "not empty".
* **A screen that must not change** → `check_screen("name")` in `golden_tests.cpp`
  with a single, unique baseline name.
* **A failure mode** → `fault_tests.cpp`, and assert the *message*, not just the exit
  code. Silence is the bug.
* **Things agreeing with each other** → a step in `journey.sh`; if it needs new CLI
  output, make the binary print it (a test must not read the store's JSON and infer
  what the app would have said).
* **Anything with shared state** → check it under `make asan` before believing it.

## 13. What the suite deliberately does not do

* It does not assert on log *text* beyond a few stable phrases, because log wording is
  for humans and should be free to improve.
* It does not sleep to "make sure" something finished: waits are bounded and poll a
  condition (frame present, state changed), so a slow machine slows the test down
  rather than breaking it.
* It does not test the old Node app. That app is the reference the audit describes,
  not part of this build; its behaviour is recorded in `docs/AUDIT.md`.
* It does not claim the interfaces are pixel-identical to 1.2.0. This is a rewrite
  with its own look; the golden baselines pin *this* app's appearance.
