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
tools/preflight.sh        # 28 checks, no compiler needed
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
| a crash left no log (requirement 2) | journey steps 12–14 | disable the crash handlers, or write the log after the window opens |
| `--json` output polluted by the banner | journey step 4 (`{"` must be the first byte of stdout) | print the banner to stdout unconditionally |

## 9. What is verified, and what is not

Verified on this machine (Linux, no display, no sound card), in this turn:

* 339 checks across layers 1, 2 and 4 — `0 failure(s)`
* 28/28 static preflight checks
* 15/15 journey steps through the real binary, run repeatedly, including three
  consecutive clean artwork passes
* `make asan`: 339 checks clean, and the pre-fix code failing under it
* CMake/CTest: 2/2 tests pass (`native-tests`, `journey`) from a build directory
  outside the tree
* `make verify` green from a fresh clone
* the Windows executable **builds** and its payload is complete — 48 imported
  libraries checked, 0 missing (§11). It has not been *run*.

**Not verified — and not claimed:**

| Area | Why not | How to check it on a machine that has it |
|---|---|---|
| **Running** the Windows build | the exe is built and payload-checked (§11), but never executed: Wine dies before creating a prefix in this sandbox, and there is no Windows machine here | run `scripts/windows-selfcheck.ps1` from the `win64-cross-1` release, or enable `docs/ci/windows.yml` |
| Every `_WIN32` branch | compiled by the cross build now, but not executed | build on Windows and run the checklist in §10 |
| A real display / window manager | no X11 or Wayland here | `./build/vlcflix` on a desktop; resize, full-screen, hover a card, press `f` |
| Audio | no sound card; libVLC prints `ALSA lib … cannot find card '0'` | play a sample with sound and check the `−/+` latency keys move the sound earlier/later |
| GPU video decode | no Vulkan video queue on this box; `-hwaccel auto` falls back to software (proven by its exit code and output file) | `hwDecode` on, scan, and check the log for a hardware device |
| Automatic device-latency compensation | libVLC 3 cannot report output latency | nothing to do — the UI now says so instead of offering a toggle that does nothing |
| Installer behaviour side by side with 1.2.0 | no NSIS, no Windows toolchain here; `scripts/build-win-native.sh --check` reports exactly what is missing and exits 3 | §10 |

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

## 11. The Windows build: what was actually done

This started as the largest "not verified" item. It is now half closed.

**Built.** `scripts/build-win-native.sh` produces a real `VLCFlixStudio.exe` from this
source tree on a Linux box; the result is published as the `win64-cross-1` release.
The recipe, all of which ran in this session:

| Piece | Where it comes from | Why that one |
|---|---|---|
| compiler | llvm-mingw 20261020 (clang + lld, UCRT) | no install, no root: a complete Windows toolchain in a tarball |
| SDL2 | official `SDL2-devel-2.32.10-mingw` package | the window and input layer |
| FreeType | built from source here with `--without-harfbuzz` | the MSYS2 build drags in glib, graphite2 and a GCC runtime DLL just to draw text |
| libpng, zlib, libjpeg-turbo | MSYS2 mingw64 packages (import libraries) | small, standard, their DLLs are shipped |
| libVLC | VideoLAN's own `vlc-3.0.24-win64.zip` | the app must ship the exact libVLC it linked against; the import library is generated from `libvlc.dll` with `gendef` + `dlltool` |

**Checked.** `scripts/check-win-payload.sh` walks every DLL imported by the exe, by
SDL2, libpng, libvlc and libvlccore, and requires each one to be in the payload or part
of Windows: **48 imports, 0 missing**. It also refuses to call the payload complete
without the plugins libVLC needs (`libavcodec_plugin.dll`, the mp4/mkv/avi demuxers,
the h264 packetizer, the vmem and d3d11 video outputs, the mmdevice and directsound
audio outputs). The PE is verified as x86-64, GUI subsystem, with the manifest (DPI
awareness) and version block embedded as resources.

**Three portability bugs the cross build found**, all fixed in the source: `std::atoi`,
`std::atof` and `std::strtoul` were used without `<cstdlib>` (libstdc++ on Linux pulls
it in transitively, libc++ does not), and the deliberate-crash command called
`RaiseException`/`EXCEPTION_ACCESS_VIOLATION` without `<windows.h>`. It also forced a
real decision: the exe is a **GUI** executable so a double-click does not open a
console, and `main()` calls `AttachConsole(ATTACH_PARENT_PROCESS)` so the same binary
typed into `cmd.exe` still prints.

**Not checked: running it.** Wine will not start in this sandbox — `wineboot` dies with
SIGSEGV before it creates a prefix, in both the 11.20 and the 11.20-staging builds, so
the exe has never been executed anywhere. That is stated on the release and here.

Two independent ways to close it:

1. `scripts/windows-selfcheck.ps1`, which ships inside the release zip: it runs the
   exe, scans a folder, renders every screen, plays a film, and packs the screenshots,
   the `status --json` document, the app's own log and a pass/fail report into
   `windows-selfcheck-evidence.zip`.
2. `docs/ci/windows.yml`: the same thing unattended on `windows-latest`, using MSYS2's
   mingw-w64 GCC — a *second, independent* toolchain — and attaching the exe and the
   screenshots to a release. It needs to live at `.github/workflows/windows.yml` and a
   token with the `workflow` scope; `docs/ci/README.md` has the two-minute version.

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
