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

## Windows desktop app

VLCFlix ships as a normal Windows application: **`VLCFlix.exe`**, an installer, and a
desktop icon. There is no bundled browser.

**Download:** `release/VLCFlix-Setup-1.0.0.exe` — or build it yourself with
`scripts/build-win.sh` (see [Building the Windows app](#building-the-windows-app)).

Run the installer and it will:

1. Install to `%LOCALAPPDATA%\Programs\VLCFlix` — **per-user, so no UAC prompt**.
2. Create a **desktop icon** and a Start Menu entry.
3. Register an entry in *Add or remove programs*, including an uninstaller.

Double-click the icon. A native window opens and the app is there.

### Why it is small

| Part | Size | Notes |
|---|---|---|
| `VLCFlix.exe` | 670 KB | Win32 shell: window, server lifecycle, WebView2 host |
| `node.exe` | 67 MB | the bundled runtime — the app's backend is your existing Node server |
| `WebView2Loader.dll` | 165 KB | Microsoft's redistributable loader |
| app + dependencies | 4 MB | server, front-end, demo clips |
| **Installer** | **21 MB** | LZMA-compressed |

The UI renders in **WebView2** — the Chromium-based engine that is already part of
Windows 10 and 11. Nothing Chromium-shaped is bundled, which is the difference between
21 MB and the ~200 MB an Electron app would cost.

If WebView2 is somehow absent (rare; it is a Windows component and ships with Edge),
the app says so plainly instead of failing silently.

### What the shell actually does

1. Picks a free loopback port.
2. Starts `resources\node.exe resources\app\server\index.js` with
   `HOST=127.0.0.1` and `VLCFLIX_DATA_DIR=%LOCALAPPDATA%\VLCFlix`.
3. Polls `/api/health` until it answers, then shows the window.
4. On close, kills the server — enforced with a **job object**, so the background
   process cannot outlive the window even if the app is killed from Task Manager.

Nothing is written into *Program Files*. Settings, watch progress and generated
artwork live in `%LOCALAPPDATA%\VLCFlix`, and survive an uninstall.

### If the window stays black

From version 1.0.1 the app no longer fails silently. Every step writes to
`%LOCALAPPDATA%\VLCFlix\launcher.log`:

```
[launch] port    = 58619
[launch] node.exe started, pid=14744
[health] server answered on port 58619 after 800 ms
[webview2] loader = ...\resources\WebView2Loader.dll
[webview2] CreateCoreWebView2EnvironmentWithOptions -> 0x80070002 (ERROR_FILE_NOT_FOUND)
```

If the embedded browser cannot start, the window shows a **native panel** instead of a
black rectangle: the reason, the local address to open in any browser, a button to do
that, a Retry button, and the log path. The app stays open rather than closing itself.

![Fallback panel](docs/screenshots/webview2-fallback.png)

Most common cause: **the WebView2 runtime is missing** (unusual on Windows 10/11, but it
happens on stripped or LTSC images and some managed machines). Install the
*Microsoft Edge WebView2 Runtime* from Microsoft, then press Retry — no reinstall of
VLCFlix needed.

Logs, both in `%LOCALAPPDATA%\VLCFlix`:

- **`launcher.log`** — paths, port, whether `node.exe` started, the health check, and
  every WebView2 HRESULT.
- **`server.log`** — the Node server's own stdout and stderr.

### Building the Windows app

On Linux or in WSL, with `mingw-w64`, `nsis` and `zip`:

```bash
VERSION=1.0.0 bash scripts/build-win.sh
```

That fetches `node.exe` and the WebView2 SDK into `.cache/`, cross-compiles
`src/win/main.cpp`, assembles `dist/win/VLCFlix/`, and produces the installer and a
portable zip. It is reproducible from a clean checkout.

### Uninstalling

*Settings → Apps → VLCFlix → Uninstall*, or run `Uninstall.exe` in the install folder.
Your library settings are kept; the uninstaller offers to remove them too.

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
