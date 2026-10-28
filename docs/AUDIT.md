# AUDIT — what VLCFlix is, and what is actually wrong with it

Read-only audit of `main` at `d4606a7` (v1.2.0), done before writing any C++.
Everything marked **VERIFIED** was run in this sandbox on 2026-10-28 and the
command or output is quoted. Everything marked **READ** is from reading the source
and was not exercised. Nothing here is from memory.

Node test suite as shipped: **72/72 pass** (`npm test`). The audit is not about
the tests failing — it is about the places where the tests do not look.

---

## 1. The product, measured

| Piece | Files | Lines |
|---|---|---|
| Server (Express) | `server/index.js`, `library.js`, `vlc.js`, `store.js` | 2 150 |
| Front-end (vanilla ES modules, no build step) | `public/js/app.js`, `sync-math.js`, `index.html`, `styles.css` | 2 825 |
| Windows shell (Win32 + WebView2) | `src/win/main.cpp` + `.rc`/manifest/icon | 1 000 |
| Tests | `tests/*.test.js` | 679 |
| Demo generator | `scripts/make-demo-media.js` | 218 |

Three playback "engines" plus a browser surface:

1. **`vlc`** — spawn `vlc.exe --extraintf=http --http-port … --http-password …`,
   then drive it over HTTP (`/requests/status.xml`, `playlist.xml`,
   `?command=in_play|pl_pause|seek|volume`). The player window is VLC's own.
2. **`ffmpeg`** — pipe the film through `ffmpeg` into an in-app `<video>`; copy
   H.264, transcode everything else, optional GPU decode ladder.
3. **`browser`** — range-aware HTTP 206 of the raw file into `<video>`.

Feature inventory that the port must reproduce (from README + source):
two-phase scan, ffprobe metadata, filename parsing, sidecar JSON metadata,
three generated assets per film (poster/backdrop/preview montage), exposure
correction via `signalstats`, artwork resume/atomic writes/retry accounting,
filename-matched + folder-wide sidecar subtitles, SRT→WebVTT, custom cue
renderer with delay/size/height, audio delay in both directions via a delayed
picture, automatic device-latency compensation with smoothing, hero billboard
with preview, rails, hover previews, detail sheet, search, My List, resume,
progress, VLC remote dock, gestures (tap zones, double-tap skip), settings sheet,
header scan chip, Windows installer.

---

## 2. Bugs found — with evidence

### A1. Filename parsing destroys titles that *are* years — VERIFIED

`server/library.js:parseName` takes the **first** `19xx|20xx` token as the year
and `break`s the title loop on it.

```
{"title":"2012 2009 1080p Bluray x264","year":2012}   <- 2012.2009.1080p.BluRay.x264.mkv
{"title":"1917 2019 2160p WEB-DL",     "year":1917}   <- 1917.2019.2160p.WEB-DL.mkv
{"title":"Blade Runner",             "year":2049}     <- Blade.Runner.2049.2017.1080p.mkv
```

Three separate faults: the year *and* the real year are both plausible, the title
is truncated at the first one, and `Blade Runner 2049` gets the wrong release year
entirely. The tests only cover filenames with one year in them.

### A2. The ffmpeg "copy" engine cannot seek in files with sparse keyframes — VERIFIED

`-ss` before `-i` with `-c:v copy` can only start at a keyframe. The bundled demo
clips contain **one I-frame in 480 frames**, so a seek to 4 s re-streams the whole
file:

```
$ ffprobe -show_frames sample … | grep -c "I,"      -> 1
$ ffmpeg -ss 4 -i the-silent-coast.mp4 -c:v copy …  -> duration=16.000000  (whole clip)
$ ffmpeg -ss 4 -i dense.mp4 (g=24)   -c:v copy …    -> duration=12.000000  (correct)
```

The UI models an ffmpeg stream as `offset + elapsed`, so on those files the
timecode says 0:04–0:16 while the picture is at 0:00–0:12, and a 90-minute film is
re-piped from byte 0 on every seek. The 1.1.13 fix (`t=` replacement) addressed a
different fault and does not help here. **libVLC seeks the same file correctly**
(see §3), which is one concrete reason to move playback into the process.

### A3. HTTP Range: suffix ranges are served from byte 0 — VERIFIED

```
$ curl -H 'Range: bytes=-500' /api/stream/<id>   (file is 217 238 bytes)
HTTP/1.1 206 Partial Content
Content-Range: bytes 0-500/217238        <- RFC 9110 says 216738-217237
```

`server/index.js` strips `bytes=` with `/(\d*)-(\d*)/` and defaults a missing start
to 0; the `-N` form means "the last N bytes". Low impact for `<video>` (which
sends `0-` or explicit ranges) but it is a broken contract, and any resumable
download or seek-backwards probe gets wrong bytes.

### A4. Finished films are never finished — VERIFIED

`server/store.js:setProgress` documents "above 92% it is finished" and implements
only the `< 2 %` half:

```
setProgress('a', 97, 100) -> {"a":{"position":97,"duration":100,…}}   (kept)
```

There is no removal path at all, so a completed film stays in **Continue
Watching** forever at 97 %, and `POST /api/progress` has no way to say "done, drop
it". The port needs an explicit finished rule and a cleared resume point.

### A5. The GPU ladder's 10-bit detection matches `nv12` — VERIFIED (latent)

`hwCandidates` decides what to ask the decoder for with `/10|12/.test(item.pixFmt)`:

```
deep? nv12 = true      yuv420p10le = true
```

`nv12` is an **8-bit** format whose name contains "12", so an 8-bit source whose
`pix_fmt` is reported as `nv12` is sent down the 10-bit `p010le` path first — the
conversion the code's own comment says hardware usually refuses. It currently
survives only because ffprobe usually reports `yuv420p` for 8-bit sources.

### A6. `vlcPassword` accepts any JSON type — VERIFIED

```
POST /api/settings {"vlcPassword":{"a":1}}  -> 200, stored
```

`playMode`, `vlcHttpPort`, `autoLatency` and `hwDecode` are validated; the
password is not. It is later interpolated into `Basic base64(":" + value)`, so a
non-string silently produces `: [object Object]` and a 401 from VLC that explains
nothing.

### A7. Docs and API have drifted apart — VERIFIED

- README documents `GET /api/subtitles/:id/:index`; the route is
  `/api/subtitles/:id/file?name=…`. `/api/subtitles/:id/tracks` and
  `/api/subtitles/:id/track` are not documented at all.
- README says "23 tests"; `npm test` reports **72**.
- `CONTEXT.md` says the token lives at `.secrets/github-token`; that path is not in
  a fresh clone (correctly — it is git-ignored), so a new session has no credential
  and cannot push. Recorded here because it is exactly the failure §7 of AGENTS.md
  warns about.

### A8. Secrets on a command line — READ

`vlc.launch()` passes `--http-password=<pw>` in `argv`. On Windows any process can
read another process's command line, so the control password (default
`"vlcflix"`) is readable by any user on the box for the lifetime of the player.
The HTTP interface itself binds `127.0.0.1`, which limits the damage, but the
default password is a shared constant.

### A9. Stills are not versioned, only the preview clip is — READ

`library.missingArt` re-renders the preview when `previewVersion !== PREVIEW_VERSION`
but judges poster/backdrop purely by "file exists and is non-empty". The 1.1.2
class of bug ("format changed, nothing re-rendered") is still live for the two
JPEGs.

### A10. Minor / cosmetic — READ

- `probeTools()` spawns `ffprobe -version` and `ffmpeg -version` on every
  `/api/system` call, which the Settings panel polls.
- `liveStatus()` can report the previous scan's `error` while an artwork pass is
  running.
- `parseName("no-year-here.mp4")` → "No-year-here" (hyphens are not word breaks).
- Multi-range requests return a single 206 without `multipart/byteranges`;
  browsers do not use them, so this is noise rather than a defect.

### Deliberately not called a bug

`subtitleClock()` looks like a unit error (`video.currentTime - sub.audio`) but
`sub.audio` is stored in **seconds** (`adjustSubtitle` steps ±0.05, range ±2), so
the arithmetic is right. Checked before reporting.

---

## 3. The two assumptions the port depends on — both VERIFIED

The rewrite plan rests on "we own the picture". A throwaway spike
(`g++ -lvlc`, libVLC 3.0.24) played `samples/the-silent-coast.mp4` with
`libvlc_video_set_callbacks` writing into a plain `std::vector<uint8_t>`:

```
frames=22  luma=132  state=3            <- real decoded pictures in our buffer
length_ms=16000  time_ms=650            <- duration from the engine, no ffprobe
after seek to 12000: time_ms=12390  new_frames=18   <- seeks the single-keyframe file A2 fails on
```

Consequences for the plan:

- No `vlc.exe`, no HTTP polling, no remote dock drift: the engine reports state in
  process, and we present every frame ourselves.
- No ffmpeg transcode is needed for local playback at all — VLC decodes MKV, HEVC,
  AV1, AC3, DTS, 10-bit, and whatever else. The whole black-screen family
  (v1.1.0–v1.1.11) disappears because the code path stops existing.
- Seeking is exact regardless of keyframe spacing.
- The audio-delay problem the browser solved with a delayed-canvas hack becomes a
  frame queue we own, with the same maths (`sync-math.js`) but no `<video>` limits.

Second assumption, not yet tested: SDL2 window + input on the user's actual
machine. Rendering itself does not need a display — the renderer draws into an RGBA
buffer and can write a PNG — so the UI can be verified headlessly here and only the
window/input layer is machine-specific. **Do not claim the desktop window works
until someone runs it** (§5, §12).

---

## 4. What "everything it could not work with" means concretely

| Blocked in the web version | Why | In C++ |
|---|---|---|
| Own picture control for A/V sync | `<video>` cannot play sound early, so the picture is buffered on a canvas | frames are ours; the queue is the feature, not a workaround |
| Browser codecs | Chromium builds ship no H.264/HEVC/AC3 | libVLC decodes everything |
| Real seeking on the ffmpeg engine | pipes are not seekable; `t=` restarts the stream | `libvlc_media_player_set_time` (verified) |
| Frame-exact subtitles | `::cue` is opaque; a `requestAnimationFrame` overlay is a hand-rolled renderer anyway | draw cues in the same compositor as the picture, timed off the presentation clock |
| One player window budget | VLC's own window is separate from the chrome | inline video inside the Netflix UI |
| WebView2 black-window class of bugs | borrowed COM lifetimes, watchdog, `--disable-gpu` retries | no webview in the product path |
| Artwork/preview pipelines tied to Node | `execFile` + JSON on stdout | same ffmpeg commands, driven from C++ with a real process API |

---

## 5. Scope note for the port

The UI is ~1 500 lines of CSS and ~2 500 lines of DOM code; that is the largest
single chunk of work and it is *replaced*, not ported. The server's API surface,
the HTTP control client and the WebView2 shell all disappear. What survives as
logic — parsing, scan, artwork, subtitles, sync maths, store — is roughly 1 200
lines of the JS and is what the test suite actually covers, so it is also what can
be ported with proofs.
