# Three turns, defined

Constraint: three turns to a **working C++ application**, pushed to a second
branch that this session creates. Every turn ends with something running and
something I looked at (`AGENTS.md` §5), a commit, and a verified remote SHA.

Decisions taken because they were not overridden (rename/redirect is cheap, say
the word):

| Decision | Choice | Why |
|---|---|---|
| Branch | **`cpp`**, cut from `main` at `d4606a7` | already created locally; pushes become the remote branch |
| Where the code lives | **`native/`** inside this repo | non-destructive: `main`'s Node app stays readable as the reference and nothing is deleted without asking (§12). When the port is complete, `native/` becomes the root and `server/`, `public/`, `src/win/` go — as its own approved step |
| UI | **SDL2 + our own renderer** | portable, one dependency, and the renderer targets a plain RGBA buffer so every screen is verifiable headlessly here |
| Playback | **libVLC in-process only** | verified spike; deletes the HTTP client, the remote dock and the transcode engine, and with them the black-screen family of bugs |
| Build | **CMake** (Ninja here) + a `Makefile` fallback | installed packages do not survive a snapshot; the fallback needs only `g++` + `pkg-config` |
| Tests | own 60-line assert harness | no dependency to reinstall; `make test` must work from a bare clone |

---

## Turn 1 — foundation that runs (this turn, after the token)

**Build:** `native/CMakeLists.txt`, `native/Makefile`, `native/tests/` harness,
`.gitignore` already covers `build/`.

**Port (logic, no pixels):**

| Module | Replaces | Note |
|---|---|---|
| `core/json` | `JSON.parse/stringify` | reader + writer, same files on disk |
| `core/store` | `server/store.js` | same defaults, temp+rename, `vlcPassword` type-checked (A6) |
| `core/naming` | `library.parseName` | **A1 fixed**: year chosen from the release-tag boundary, tags parsed from the right |
| `core/sync` | `sync-math.js` | same arithmetic, the 14 JS cases ported one-for-one |
| `core/catalog` | `decorate`/`buildRails` | **A4 fixed**: finished films leave Continue Watching |
| `media/subtitles` | `findSubtitles`/`listFolderSubtitles`/`parseVtt` | cues in C++, no WebVTT-for-a-browser detour |
| `media/walk`,`media/probe`,`media/mime` | `library.walk/probeVideo/mimeFor` | ffprobe JSON parsed by our own reader |
| `media/artwork` | `renderArt`/`missingArt`/`sweepTempArt`/`renderMissingArtwork` | same ffmpeg command lines, temp+rename, attempt accounting, stills versioned too (A9) |
| `platform/proc` | `execFile` | `fork/exec` + `CreateProcess` behind one call |
| `main.cpp` | `server/index.js` entry | subcommands: `--scan`, `--status`, `--version` |

**Verify (all pasted, not asserted):**
1. `make -j2 && make test` → every unit test green, and a deliberately broken
   assertion shown failing first as the negative control.
2. Regression test per audit bug: `1917.2019.2160p` → `"1917"`,`2019`;
   `2012.2009.1080p` → `"2012"`,`2009`; `Blade.Runner.2049.2017` →
   `"Blade Runner 2049"`,`2017`; and the old behaviour must fail all three.
3. `./build/vlcflix --scan --data /tmp/d1 --folders samples` → the same eight
   titles, years and durations the Node app reported (audit §1 run), artwork
   written to `/tmp/d1/media`; I open one poster and one preview clip and look.
4. Kill a scan mid-artwork, re-run, confirm it resumes and does not redraw.
5. Push `cpp`; `git ls-remote` shows the branch at my HEAD.

**Exit criteria:** a binary that scans a real library and makes real artwork,
started from a clean clone with one `make`.

## Turn 2 — the window and the browsing UI

**Build:** `render/canvas` (blend, clip, rounded rect, gradient, bilinear blit,
glyph runs via FreeType), `render/image` (JPEG/PNG via libjpeg/libpng),
`platform/window_sdl`, `ui/theme` (the CSS design tokens ported to code),
`ui/widgets` (rail, card, hero, sheet, chip, toast, focus/scroll).

**Screens:** home (hero billboard + rails + hover preview playback), browse grid,
search, My List, detail sheet, settings sheet with the scan chip, empty state.

**Verify:**
1. `./build/vlcflix --render home --png out/home.png` (and one per screen), then
   **I open every PNG and look at it** — layouts, text overflow, poster crops.
2. Negative control: `--render` of an empty catalogue must produce a visibly empty
   state, not a plausible-looking frame.
3. Headless interaction tests driving the real event queue: click card → detail
   opens with that film's title; type "neon" → one result; toggle My List →
   rail gains the item.
4. Push; confirm remote SHA.

**Exit criteria:** a browsable library with every view from README's screenshots,
each one rendered by our code, no browser involved.

## Turn 3 — playback, sync, and the honest finish

**Build:** `player/engine` (libVLC instance, media, `set_callbacks` frame sink,
state/seek/volume/rate, duration from the engine), `player/presenter` (frame queue
for the audio offset in both directions, latency smoothing), `player/overlay`
(subtitle cues drawn with the presentation clock, delay/size/height), player view
(chrome, scrub, gestures per the README table, keyboard G/H/J/K/V, resume,
progress).

**Verify:**
1. Headless playback of a sample: frame count, mean luma, `set_time` accuracy on
   the single-keyframe file the pipe engine could not seek (A2).
2. `sync` end-to-end: ±200 ms offsets and the latency smoothing, with the queue
   depth the maths predicts.
3. A screenshot of the player chrome over a real decoded frame, opened and read.
4. Full `make test`; then push and verify.
5. README/PORT docs rewritten for the native app; `CONTEXT.md`, `TURN_LOG.md`,
   `checkpoint.json` current; explicit list of what is **not** done.

**Deliberately not in these three turns** (named so nobody discovers it later):
Windows build and packaging (P8 — needs a machine I can run it on), the GPU
decode ladder and `--hw-probe` (P7 — it now only serves artwork/previews), hover
preview *audio*, multi-part/season grouping, online metadata. If Turn 1 lands
early, P7's probe moves into Turn 2.

---

## What I need from you now

A **fine-grained PAT** for `JohnOlowe/VLCFlix` with **Contents: read/write** only
(no Administration, no Actions, no Secrets — so a token leak cannot change repo
settings or CI). Paste it in the next message. I will:

1. store it at `.secrets/github-token` (mode 600, git-ignored, never printed, and
   confirmed absent from the remote afterwards),
2. run `bash scripts/setup-git-auth.sh` to rebuild origin, identity and the
   credential helper (that script is what keeps this working after a snapshot),
3. `git push -u origin cpp` to create the branch, and confirm with
   `git ls-remote` and the GitHub API that `cpp` is at my HEAD,
4. then start Turn 1's build.

The token passes through chat, so rotate it when the three turns are done — that
is a hygiene note, not a suspicion: the scope you grant is the scope it will have.
Until the token arrives, work continues locally and nothing is pushed.
