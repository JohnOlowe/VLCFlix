# VLCFlix

A Netflix-style browsing front-end for your own video library that plays through
**the real VLC media player**.

VLC is a superb engine with a functional, unglamorous interface. VLCFlix leaves the
engine alone and replaces the browsing experience: a hero billboard with an autoplay
preview, horizontally scrolling rails, hover-to-preview cards, a detail sheet, search,
My List and resume-watching — while playback is handed to VLC itself over its
documented HTTP control interface.

![VLCFlix home](docs/screenshots/home.jpg)

---

## Quick start

Requires **Node.js 18+** and, for full functionality, **VLC** and **ffmpeg**.

```bash
npm install
npm start            # http://localhost:4173
```

On Windows, double-click **`start-windows.bat`** instead — it checks for Node,
installs dependencies on first run, and opens the browser.

Then, in VLCFlix → **Settings**:

1. Add one or more **library folders** (e.g. `D:\Movies`).
2. Press **Scan library**.
3. Press **Play** on anything.

No videos handy? Press **Use the bundled demo library** — eight short procedural
clips ship with the repo (2 MB total) so the UI has something to show immediately.

---

## How playback works

VLC ships an HTTP control interface. Launching it with

```
vlc --extraintf http --http-host 127.0.0.1 --http-port 8080 --http-password <pw>
```

exposes a small REST-ish API that VLCFlix drives:

| Endpoint | Used for |
|---|---|
| `GET /requests/status.xml` | player state, position, length, volume, current title |
| `GET /requests/playlist.xml` | the queue |
| `GET /requests/status.xml?command=…` | `in_play`, `pl_pause`, `pl_stop`, `seek`, `volume` |

When you press play, VLCFlix checks whether VLC is already listening. If it is, it
hands the file to the running instance. If not, it spawns VLC detached with the
control interface enabled, waits for it to answer, and only then reports success.

While VLC plays, a **remote dock** appears in the corner with transport controls,
a scrub bar and live volume — all round-tripping through VLC's own state, so the UI
cannot drift away from what is actually happening in the player.

Every `vlc.exe` call goes through the server. The browser never talks to VLC
directly, which is why the app works when the server runs on a different machine
from the browser.

### The built-in player

VLC is the intended engine, but it is not the only one. **Settings → Playback →
Built-in player** plays media inside the page using a range-aware local stream
(`GET /api/stream/:id`, HTTP 206). It cannot decode every codec VLC can — notably
MKV/HEVC — but it guarantees a working player on a machine without VLC.

If VLC is missing or refuses to start, VLCFlix falls back to the built-in player
and tells you why, rather than leaving a dead button.

---

## What happens on a scan

`POST /api/library/scan` walks every configured folder and, for each video:

1. **Reads metadata** with `ffprobe` (duration, resolution).
2. **Generates three assets** with `ffmpeg`:
   - `poster.jpg` — 400×600 crop, for grid views
   - `backdrop.jpg` — 1280×720 crop, for the hero and cards
   - `preview.mp4` — an 8-second muted clip for hover previews
3. **Grades each still** (contrast, saturation, vignette) and then **measures its
   mean luma** using ffmpeg's `signalstats`, correcting exposure with a gamma curve
   if the frame is too dark or too flat for white text to sit on. A dark opening
   scene would otherwise produce an unreadable card.
4. **Parses a title and year** out of the filename, stripping release tags
   (`1080p`, `x265`, `WEB-DL`, …).

Unchanged files are skipped on later scans via an id/size/mtime check, so
re-scanning a large library is cheap.

Artwork is written to `data/media/`. VLCFlix reads your videos and never moves,
renames or deletes them.

### Sidecar metadata

Filenames are a poor place for real metadata. Drop a JSON file beside any video
with the same base name to override everything:

```json
// The.Long.Harrow.2019.mkv  ->  The.Long.Harrow.2019.json
{
  "title": "The Long Harrow",
  "year": 2019,
  "rating": "TV-MA",
  "genres": ["Western", "Drama"],
  "accent": "#b5651d",
  "synopsis": "Two brothers split a failing ranch down the fence line..."
}
```

`accent` is used for fallback artwork when ffmpeg is unavailable.

---

## Configuration

Stored in `data/settings.json`, editable from the Settings sheet:

| Key | Meaning |
|---|---|
| `libraryFolders` | absolute paths scanned for videos |
| `vlcPath` | VLC executable; empty = auto-detect |
| `vlcHttpPort` | port for VLC's control interface (default 8080) |
| `vlcPassword` | required, because VLC refuses to open the interface without one |
| `playMode` | `vlc` or `browser` |
| `vlcFullscreen` | fullscreen when VLC starts |

Auto-detection covers `C:\Program Files\VideoLAN\VLC\vlc.exe`,
`C:\Program Files (x86)\...`, `/Applications/VLC.app/Contents/MacOS/VLC`, and
`vlc` on `PATH`. `VLCFLIX_VLC_PATH` overrides it.

---

## API

| Method | Route | Purpose |
|---|---|---|
| GET | `/api/bootstrap` | everything the UI needs on load |
| GET | `/api/catalog` | items with progress and list state |
| POST | `/api/library/scan` | start a scan (202) |
| GET | `/api/scan/status` | scan progress, polled by the UI |
| POST | `/api/settings` | validated partial update |
| GET | `/api/system` | platform, ffmpeg availability, VLC detection |
| POST | `/api/player/play` | play an item in VLC or the browser |
| GET | `/api/player/status` | live VLC state |
| POST | `/api/player/command` | pause, resume, toggle, stop, seek, seekBy, volume |
| POST | `/api/progress` | save a resume point |
| POST | `/api/mylist` | toggle an item in My List |
| GET | `/api/stream/:id` | range-aware local stream |

---

## Tests

```bash
npm test
```

23 tests over the parsers and the HTTP surface. The parser fixtures in
`tests/fixtures/` are **real captured output from VLC 3.0.24**, not hand-written
XML — so the tests fail if VLC's format and the parser ever disagree. The API
tests run against a throwaway data directory and never touch your library.

---

## Verified against

- VLC **3.0.24 Vetinari** (Debian build): `status.xml`, `playlist.xml` parsing;
  `in_play`, `pl_pause`, `seek`, `volume` commands; password rejection (401).
- Node.js 20.
- Chromium: full UI, built-in player, hover previews, detail sheet.

## Known limitations

Stated plainly rather than discovered later:

- **The UI is a companion app, not a patched `vlc.exe`.** This sandbox cannot
  build VLC for Windows (2 cores, 2 GB RAM, no mingw-w64 toolchain) — and a
  cross-compiled Windows binary could not be run or verified here. VLCFlix
  therefore drives VLC rather than replacing its Qt/QML interface. The product
  behaviour — Netflix-style browsing with playback in VLC — is real and tested.
- **The built-in player depends on browser codecs.** Chromium builds without
  proprietary codecs cannot play H.264; use VLC for those files.
- **Playback state is polled, not pushed.** VLC's HTTP interface has no event
  stream, so the remote dock refreshes every 2 seconds. Seeks can take up to that
  long to appear.
- **One VLC instance per port.** Two VLCFlix windows pointed at the same port share
  one player.
- **Artwork comes from the video itself.** There is no online metadata lookup, so
  a film with no sidecar file is titled from its filename.
- **No authentication.** VLCFlix binds to `0.0.0.0` and has no login. Run it on a
  trusted network, or change `app.listen` to `127.0.0.1` if that matters.
