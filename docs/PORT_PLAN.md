# PORT PLAN — VLCFlix in pure C++

Target: one native application, no browser, no Node, no WebView2, no external
`vlc.exe`. VLC's *engine* stays (libVLC); VLC's *interface* and VLCFlix's web
surface are both replaced by our own C++ rendering and input.

Rules this plan is written under: `AGENTS.md`. In particular §4 (every step gets a
verification), §5 (nothing is claimed that was not run) and §12 (what is
unverified is labelled).

Status of the two load-bearing assumptions: **verified**
(`docs/AUDIT.md` §3 — libVLC hands decoded frames to our buffer, and seeks the file
the ffmpeg pipe engine could not). Not yet verified: the desktop window and input
on a real machine; nothing here has been compiled for Windows.

---

## 1. Architecture

```
                    ┌───────────────────────────────────────────────┐
   ~/, settings,    │  app/            (shell: window, input, loop)  │
   data/*.json  ───▶│  ui/             (retained widget tree, layout)│
                    │  render/         (RGBA canvas, text, images)   │──▶ SDL2 window
                    │  player/         (libVLC engine + our chrome)  │──▶ libVLC (decode)
                    │  media/          (scan, probe, artwork, subs)  │──▶ ffprobe/ffmpeg (CLI)
                    │  core/           (store, json, naming, sync)   │
                    └───────────────────────────────────────────────┘
```

Four rules that make it one program instead of a stack of layers:

1. **The renderer owns a pixel buffer, nothing else.** `render::Canvas` is an RGBA
   surface with blending, clipping, rounded rects, gradients, bilinear scaled
   blits, and text via FreeType. It has no window dependency, so every screen can
   be rendered to a PNG in a test — that is how the UI gets verified here.
2. **The player is a frame source, not a widget.** `player::Engine` exposes
   `state()`, `seek()`, `set_volume()`, and a callback that receives each decoded
   frame already in the right pixel format. Presentation timing (the audio-delay
   queue) lives in `player::Presenter`, which is where `sync-math` ends up.
3. **Refresh is immediate-mode over a retained tree.** Widgets are retained nodes
   (so state survives a frame); layout and painting are recomputed only when
   something is marked dirty. No diffing, no reactivity, no template language.
4. **One thread per concern, no locks in the draw path.** Scan and artwork run on
   worker threads and publish immutable snapshots; the UI thread swaps a pointer.

### Dependencies (all present in this sandbox, all cross-platform)

| Purpose | Library | Why not something else |
|---|---|---|
| Window, input, audio device | **SDL2** | only portable choice that is one dependency and needs no toolkit |
| Decoding + demuxing | **libVLC ≥ 3.0** (`-lvlc`) | it *is* the product's engine, and it decodes everything |
| Text | **FreeType** | already installed; no font stack to bundle |
| Still images | **libjpeg**, **libpng** (+zlib) | artwork is JPEG; PNG for screenshots |
| Process spawning | POSIX `fork/exec` + Win32 `CreateProcess` behind one `proc::run()` | ffprobe/ffmpeg calls stay identical to today's |
| JSON | own 200-line reader/writer | the data files are ours; no dependency for `{}` |
| HTTP range serving | **removed** | nothing streams over HTTP any more |
| XML parsing | **removed** | was only for VLC's control interface |

Build: **CMake** (generator-agnostic: Ninja here, MSVC/mingw on Windows) with a
`Makefile` fallback that only needs `pkg-config` and `g++`, because installed
packages do not survive a sandbox snapshot. `make test` runs the suite.

---

## 2. Module map (JS → C++)

| Today | Lines | Becomes | Fidelity target |
|---|---|---|---|
| `server/store.js` | 135 | `core/store.cpp` | identical JSON files, same defaults, atomic temp+rename |
| `server/library.js` (parse/probe/walk) | ~300 | `media/naming.cpp`, `media/probe.cpp`, `media/walk.cpp` | same results, *plus* the A1 fix |
| `server/library.js` (artwork) | ~300 | `media/artwork.cpp` | same ffmpeg commands, same temp-file discipline, stills versioned (A9) |
| `server/library.js` (subtitles) | ~100 | `media/subtitles.cpp` | same naming rules; SRT/VTT parsed to cues, not converted to WebVTT for a browser |
| `public/js/sync-math.js` | 86 | `core/sync.cpp` | identical arithmetic, same 14 test cases ported |
| `server/index.js` (rails, decorate, progress) | ~250 | `core/catalog.cpp` | same rails order, plus A4's finished rule |
| `server/vlc.js` (client + XML) | 329 | **deleted** | replaced by libVLC; parsers kept only as a test fixture source |
| `server/index.js` (HTTP API) | ~400 | **deleted** | no client to serve |
| `public/js/app.js` (UI) | 2 523 | `ui/*.cpp` (view per screen) | same look and gestures, rebuilt |
| `public/css/styles.css` | 1 565 | `ui/theme.cpp` | design tokens ported, not the CSS text |
| `src/win/main.cpp` (WebView2 shell) | 1 000 | `app/main.cpp` | window, single instance, loop |
| `scripts/build-win.sh`, `installer.nsi` | — | phase 7 | rebuild for a JIT-free binary; installer becomes optional |

---

## 3. Phases, each with a way to prove it

Every phase ends with: tests green, a screenshot or log I looked at (§5), state
files updated (§6), commit (§7).

**P0 — skeleton (this branch).** `cmake` + `Makefile`, `make test` harness, empty
module dirs, `.gitignore` for `build/`. *Verify:* `make && make test` runs and one
deliberately failing test fails.

**P1 — core without pixels.** `json`, `store`, `naming` (with A1 fixed by parsing
release tags from the right), `subtitles` (SRT/VTT → cues), `sync` (port the 14
`sync.test.js` cases), `walk`, `mime`, `previewSegments`. *Verify:* ported tests
pass, **including a regression test per bug** — e.g. `1917.2019.2160p` →
`{"1917", 2019}`, `2012.2009` → `{"2012", 2009}`, `Blade.Runner.2049.2017` →
`{"Blade Runner 2049", 2017}`; negative control: the old parser must fail them.

**P2 — scan and artwork.** `proc::run` around ffprobe/ffmpeg, two-phase scan with
worker threads, artwork pipeline (poster/backdrop/preview montage, `signalstats`
exposure correction, temp+rename, attempt accounting, versioning for stills).
*Verify:* run the real scan against `samples/`, print the same eight titles and
durations the Node version printed (audit §1 run), then compare the generated
`data/media` byte-for-byte in count and dimensions; kill a scan mid-way and confirm
it resumes.

**P3 — renderer and shell.** `Canvas` (blend/clip/rect/round-rect/gradient/blit),
FreeType text, JPEG/PNG load, SDL2 window + input, `--render-frame <screen> <png>`
headless mode. *Verify:* every screen renders to a PNG **and I open it**; a
negative control that draws nothing must produce a visibly empty image, not a
"success".

**P4 — browsing UI.** Hero, rails, cards with hover preview, detail sheet, search,
My List, progress, settings sheet, scan chip, toasts. *Verify:* PNG per screen, and
interaction tests that drive the event queue headlessly (click a card → detail
opens; type in search → filtered count).

**P5 — player.** libVLC inline, our chrome, gestures (tap zones, double-tap skip,
per the README table), keyboard, scrub bar, volume, resume, progress saving,
subtitle overlay with delay/size/height. *Verify:* play the samples headlessly,
assert frames arrive and the picture changes between two timestamps; screenshot the
chrome over a frame.

**P6 — sync and latency.** `Presenter` frame queue; manual audio offset both
directions; device latency from the audio path, smoothed; readout in the CC panel.
*Verify:* unit tests on `sync.cpp`; a harness that measures picture-vs-sound offset
at ±200 ms and asserts the queue depth follows `computeSync`.

**P7 — hardware decode + previews.** The GPU ladder now only feeds the *artwork*
and *preview-probe* paths, because playback never transcodes. *Verify:* the
`-hwaccels` probe, a plan ladder with `hwdownload`, and the 2.5 s trial/fallback,
with a log line per attempt; keep `tools/hwdecode-probe` as a native `--hw-probe`
mode.

**P8 — platform.** Windows build (mingw-w64 or MSVC), packaging decision, and the
"it really runs" proof on the user's laptop. *Verify:* a binary that runs on the
laptop and a screenshot of it — **not** a cross-compiled binary I cannot run.

---

## 4. Deliberate differences from the Node version

- **No HTTP API, no VLC-over-HTTP.** Everything that talked to VLC over HTTP is
  gone, including its 401 handling, polling, dock and password on the command line
  (A8). `playMode` collapses from three engines to one.
- **No transcode for playback.** The `hwDecode` switch stops being a playback
  option; it survives only where it still makes sense (rendering previews).
- **Seeking is exact** (A2), **ranges are not a thing any more** (A3), **finished
  films leave Continue Watching** (A4), **the 10-bit test is on real bit depth**
  (A5), **settings are validated per key** (A6).
- **Subtitles are drawn by the compositor** with the presentation clock, so the
  cue timing cannot drift from the picture the way a `requestAnimationFrame` loop
  can.

## 5. Risks

| Risk | Handling |
|---|---|
| libVLC not installed on the user's machine | detect at startup, say so plainly, offer the artwork-only mode (browsing still works). Never a black window. |
| libVLC 3 smem API has no per-frame PTS | the legacy `set_callbacks` API gives a display callback in presentation order; delay is applied by queueing in the callback. VLC 4's `set_output_callbacks` (with PTS) is a drop-in later. Not claimed to be frame-exact at >2 s offsets until measured. |
| SDL2 window/input unverified here | no display in this sandbox. Everything up to presentation is verified headlessly; the window step is called out as unverified until someone runs it. |
| 2 cores / 2 GB here | the artwork pool stays at 2 workers, as today (measured: 8 clips, ~9 s). |
| Windows build not verifiable here | P8, and it is the user's machine that closes it. |

## 6. Open decisions (asked before P1 code)

1. **Branch name** — the repo has only `main`. Assumed `cpp`; rename is trivial.
2. **UI toolkit** — SDL2 + our own renderer (recommended: portable, headless
   screenshots, matches the CSS look) vs Win32/GDI (Windows-only, unverifiable
   here) vs Dear ImGui (less work, but a different look and a big vendored
   dependency).
3. **VLC's own window** — does anyone want the old "open real VLC and drive it"
   mode kept as an option, or is inline playback the only engine?
