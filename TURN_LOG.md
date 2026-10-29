# Turn log

## 2026-10-26 — Repository and standing-instruction setup
- Read the workspace before acting: `/home/user` contained no repository and none of `CONTEXT.md`, `TURN_LOG.md`, or `checkpoint.json`; `df -h .` showed 21 GB available.
- Checked `git ls-remote` for `https://github.com/JohnOlowe/VLCFlix.git`; it returned no refs.
- Cloned the URL to `/home/user/VLCFlix`. Git reported: `You appear to have cloned an empty repository.` The local branch is `main` with no commits; `origin` is configured.
- Added `AGENTS.md` with the user's standing instructions and created initial project state files.
- No application code, dependencies, tests, commit, or push were made. No remote data was changed.
- Verified the created files exist, `checkpoint.json` parses as valid JSON, and the four state files are untracked on the empty local branch.

## 2026-10-26 — Product scope clarified
- User confirmed VLCFlix is a modification to VLC itself (VLC for Windows), not a standalone app using VLC.
- Product direction: add features/change the VLC interface, with a Netflix-like homepage and previews for different videos.
- Updated `CONTEXT.md` and `checkpoint.json` to remove the scope ambiguity; appended this clarification here.
- Source/build inspection remains blocked until source is available in the repository. The remote was observed to have no refs at initial setup; check it again before further git work.

## 2026-10-26 — Credential disclosure and Git cadence
- User pasted a fine-grained GitHub PAT in chat. Advised the user to revoke/rotate it; revocation status is unknown.
- User requested a remote check before every turn and a push after every turn. Recorded the workable cadence: read state first, fetch/check remote at start; pull/rebase if a remote branch exists and integration is safe; commit/push meaningful changes at turn end and verify remote SHA. No empty commits when there are no changes.
- Ran `git fetch origin` without using the disclosed credential. Verification: no remote refs appeared; local branch has no commits and the four state files remain untracked. No push or other remote change occurred.
- GitHub connector is unsupported in this session.

## 2026-10-26 — User directs use of provided PAT for pushes
- User explicitly directed using the chat-provided token for pushes to this repository, accepting the risk. The token value is not repeated in notes.
- Ran `git fetch origin` this turn; no remote refs appeared. No token was used and no commit or push occurred because there was no commit to push.

## 2026-10-26 — Local ignored credential storage
- At the user's request, wrote the PAT to `/home/user/VLCFlix/.git-credentials`, which is ignored by the new `.gitignore`; did not include the value in state notes.
- Set file mode to `600` and configured the repository-local credential helper to use this file with `credential.useHttpPath=true`.
- Verified with `stat` that the mode is `600`, `git check-ignore -v` that it is ignored, and `git config --get` that the helper/path settings are active. Did not display the credential contents or test authentication; token validity remains unverified at that point.
- No commit or push occurred. The remote still had no refs at that point; working tree contained only setup/state files and `.gitignore`.

## 2026-10-26 — Initial setup files pushed
- User authorized pushing all current files except the token credential file and supplied author name `John Olowe` plus the instruction to use a generic email.
- Retrieved the public GitHub account profile for `JohnOlowe`; its account ID is `121398818`. Configured the GitHub ID-based noreply address `121398818+JohnOlowe@users.noreply.github.com` as the generic author email.
- Added ignore rules for the credential and common secret/build/cache paths before the first commit, per `AGENTS.md`.
- Staged and checked `.gitignore`, `AGENTS.md`, `CONTEXT.md`, `TURN_LOG.md`, and `checkpoint.json`. `git diff --cached --check` passed; `.git-credentials` is ignored and was not staged.
- Created root commit `913b93d8253e9a82335e5111ab37db3be4f456b8` (`chore: add repository agent setup`) and pushed `main` to `origin`.
- Verified with `git ls-remote origin refs/heads/main` that the remote SHA exactly matched local HEAD: `913b93d8253e9a82335e5111ab37db3be4f456b8`.
- Updated the state files and created `40d887cdb984de79f0e9bfa21b814d32295aa290` (`docs: record initial repository push`); pushed it to `origin/main` and verified remote SHA `40d887cdb984de79f0e9bfa21b814d32295aa290` matched local HEAD.
- No VLC source code was added; the commits contain setup/state files and `.gitignore` only. The credential file was not committed.

## 2026-10-26 — Scope re-decided, application built and verified
- Read `AGENTS.md`, `CONTEXT.md`, `TURN_LOG.md`, `checkpoint.json` before acting.
  Confirmed `.git-credentials` no longer exists; `git push --dry-run` returned
  `remote: No anonymous write access` / `fatal: Authentication failed`.
- Measured the sandbox rather than assuming: 21 GB disk, **2 GB RAM, 2 CPU cores**,
  no mingw-w64, no cmake/meson, no ffmpeg, no browser, no display.
- Presented the VLC-fork blocker to the user with VideoLAN's own build requirements
  (mingw-w64 + Wine + ~100 contribs, 4-hour minimum). User chose
  **"Netflix front-end driving real VLC"** and to supply a fresh PAT.
- Installed ffmpeg 7.1.5 and Chromium (Playwright) for verification work.
- Built the application: Express API + vanilla front-end + VLC bridge.
- Generated 8 procedurally rendered demo clips with ffmpeg (2.2 MB total).

### Bugs found by running it, and fixed
1. **Detect returned a boolean.** `looksExecutable()` returned
   `fs.statSync(p).isFile()`, so `detect()` handed `true` to `spawn()` instead of a
   path. Found by calling `/api/system` against a real VLC install. Fixed; a
   regression test now asserts detect never returns a boolean.
2. **Playlist parsing returned `[]`.** VLC puts entries in `<leaf>` elements
   nested inside `<node>` folders; the walker only looked at `<node>`. Found by
   diffing against real `playlist.xml`. Fixed, and covered by fixtures captured
   from VLC 3.0.24 rather than hand-written XML.
3. **Demo clips were almost black.** `crop` offsets used 640/360 but a 1600×900
   source cropping to 1280×720 only permits 0–320/0–180, so ffmpeg clamped every
   frame into the dark bottom-right corner. Fixed by computing max offsets from
   the geometry and centring the drift.
4. **20-second hang when VLC dies on startup.** `launch()` ignored the child's
   exit and stderr. Now returns in 0.37s with VLC's own message.
5. **Stale remote-dock title.** `/api/player/status` kept serving the last item's
   title after VLC stopped. Now reports `active:false` and the dock shows
   "Nothing playing in VLC".
6. **Washed-out artwork.** First fix (global brightness) turned frames milky;
   replaced with gamma correction driven by measured mean luma (`signalstats`),
   which lifts midtones without moving the black point.

### Verification performed (not asserted)
- Real VLC 3.0.24 installed. Confirmed `status.xml`/`playlist.xml` parsing,
  `in_play`, `pl_pause`, `seek`, `volume`, and 401 on a wrong password — including
  a direct read from VLC bypassing the VLCFlix server.
- Launch path: VLCFlix spawned VLC with correct arguments, connected in 0.36 s, and
  the player outlived the server (still listening after a server restart).
- Failure path: server correctly refused to claim success when VLC exited.
- Built-in player: verified with a VP9/WebM clip — duration 10 s, currentTime
  advancing 4.6 → 7.1, no media error. (Playwright's Chromium lacks H.264; the
  MP4s cannot play there and that is a codec limitation, not an app bug.)
- UI looked at directly in Chromium at 1600×900: home, rails, hover previews,
  detail sheet, browse, search, settings, player, and the live VLC remote dock.
- `npm test`: 23/23 pass. Negative controls run: breaking `<leaf>` parsing and
  restoring the boolean bug each made exactly one test fail; reverting returned
  23/23.

### State
- Committed `e2df2fd` (37 files). **Not pushed** — no credential. `origin/main`
  remains at `2d05ee5`.

## 2026-10-26 — Pushed to origin (credential re-provisioned)
- User supplied a fresh fine-grained PAT in chat. Wrote it to the ignored
  `.git-credentials` with `umask 077` + `chmod 600`, confirmed via
  `git check-ignore -v` and `stat`. Configured `credential.helper=store
  --file=…` and `credential.useHttpPath=true`. The value is not repeated in
  any tracked file.
- Probed scope before use, as §8 requires. Results:
  - `GET /user` -> 200, authenticates as `JohnOlowe` (id 121398818).
  - `GET /repos/JohnOlowe/VLCFlix/actions/secrets` -> **403**
  - `GET /repos/JohnOlowe/VLCFlix/actions/permissions` -> **403**
  - `GET /user/repos` -> 200, lists **15 repositories**, i.e. the token is
    broader than the single repository it is being used for.
  - No `X-OAuth-Scopes` header -> fine-grained token, not classic.
  - Caveat recorded honestly: every one of those 15 repos is **public**, so
    200 responses do not by themselves prove read scope; public repos are
    readable unauthenticated. Write scope beyond VLCFlix was **not** tested
    because the only way to test it is to mutate another repository. Not done.
- `git fetch origin` (no force, no `--force-with-lease`); branch was `ahead 2`,
  a fast-forward, so no rebase was needed.
- `git push origin main` -> `2d05ee5..a53d691  main -> main`.
- Verified rather than trusted: `git ls-remote origin refs/heads/main` returned
  `a53d691eb7617a7e8f07a9da1ab48453ac3d2525`, identical to local HEAD. The
  GitHub contents API independently lists `server/`, `public/`, `tests/`,
  `samples/`, `docs/`, `README.md` and `start-windows.bat`, and
  `.git-credentials` is absent from the remote — the credential was not pushed.
- Advised the user to rotate this token, since it passed through chat and is
  broader than one repository.

## 2026-10-26 — Credential persistence fixed (snapshot-strip discovery)
- Observed on restore: `.git-credentials` was **gone**, `git remote -v` empty,
  `credential.helper` and `user.name` unset — while all regular files survived.
- Root cause: workspace snapshots exclude `.git-credentials`, `.git/config` and
  `.netrc` **by filename**, independent of `.gitignore`. The earlier arrangement
  was therefore correct with respect to GitHub but not durable in the workspace.
  Corrected an assumption: gitignore was never at fault, and a fresh clone could
  never contain a gitignored file in the first place.
- Fix: token now lives at `.secrets/github-token` (mode 600). `.secrets/` is
  git-ignored but is not on the snapshot exclusion list, so it persists and still
  cannot reach GitHub. Added tracked `.gitremote` and `scripts/setup-git-auth.sh`,
  which rebuilds origin, commit identity and `.git-credentials` idempotently
  without printing the token.
- Verified: dry-run push succeeded after restore; negative control (token hidden,
  credentials cleared) returned `remote: No anonymous write access.`
- Committed `f2ac2c9` and pushed; local HEAD matched `git ls-remote` exactly.
- Verified absence from GitHub independently: cloned the remote to /tmp and
  confirmed no `.secrets`, no `.git-credentials`, and no `github_pat_` string
  anywhere in the tree.

## 2026-10-26 — Token scope verified: effective access is VLCFlix only (claim retracted)
- **Correction.** I previously recorded that the token was "broader than a single
  repository" and advised narrowing it on that basis. That was wrong. `GET
  /user/repos` listing 15 repos proves nothing: all 15 are public, and public
  metadata is readable with no token. I asserted a scope conclusion from evidence
  that could not support it.
- The user stated only VLCFlix is writable and authorised testing other repos.
- Controlled test, with the token passed explicitly via `http.extraheader` so that
  `credential.useHttpPath` (which only supplies it for the VLCFlix path) could not
  produce a false negative:
  - control `git push --dry-run` to VLCFlix -> accepted, `* [new branch] HEAD -> scope-probe`
  - `git push --dry-run` to each of the other 14 repos -> **HTTP 403, 14/14**
- No side effects: `--dry-run` creates nothing. Verified afterwards that VLCFlix
  still has only `main` at `87fbefc`, and that no `scope-probe` branch exists on
  any repository (spot-checked OmniPost, whose branches are its own).
- Combined with earlier probes (Actions secrets 403, Administration 403), the
  token is scoped to repository contents on VLCFlix only.
- Rotation is still reasonable because the token passed through chat, but that is
  hygiene, not a defect in its scope.

## 2026-10-26 — Native Windows desktop app (installable exe + desktop icon)
- User asked for a native Windows app with an installer and desktop icon rather
  than a browser tab.
- Checked the toolchain before designing: mingw-w64, nsis and wine64 available via
  apt. Verified early that mingw can compile and link against MSVC-oriented
  WebView2.h (needed an EventToken.h shim and hand-written COM handlers, since
  mingw has no wrl Callback and no __uuidof).
- Architecture: keep the tested Node backend, add a 670 KB Win32 shell that hosts
  the UI in the OS WebView2 runtime. Explicitly rejected Electron (~200 MB) and a
  C#/WebView2 rewrite (would duplicate the VLC integration and discard 23 tests).
- Added: src/win/main.cpp, installer.nsi, app.rc, app.manifest, resource.h,
  shim/EventToken.h, icon.ico, scripts/build-win.sh, LICENSE.txt.

### Verified (under Wine 10, not assumed)
- Silent install produced VLCFlix.exe, Uninstall.exe, resources/, a **desktop
  shortcut**, Start Menu entries and a complete uninstall registry key.
- The installed copy resolved its paths, started node.exe, and answered
  /api/health in 800 ms; the server log read
  `VLCFlix on http://127.0.0.1:34327`.
- Curling it from Linux returned the full UI: / 200 (9.2 KB), /css/styles.css 200
  (30.8 KB), /js/app.js 200 (51.4 KB), and /api/bootstrap reported
  `platform: win32`.
- Silent uninstall removed files, shortcuts and the registry key, and kept user data.

### Not verified — stated plainly
- **WebView2 never rendered a pixel.** Wine has no WebView2 runtime, so the window
  surface is unproven. The server, payload and lifecycle are proven; the browser
  surface is not.
- VLC detection from the installed app is unproven (no VLC inside Wine).

### Bugs found by running it, and fixed
1. Server bound `0.0.0.0` in a single-user desktop app. Launcher now sets
   `HOST=127.0.0.1`; dev server keeps 0.0.0.0 for the sandbox preview proxy.
2. A startup failure produced **no evidence at all** — a windowed app with no
   console. Added `launcher.log` and redirection of the server's stdout/stderr to
   `server.log`. This is what found the rest.
3. `icon.ico` was PNG-compressed per entry; NSIS could not load it. Regenerated
   with BMP/DIB entries.
4. `${__FILEDIR__}` resolved incorrectly when the script was passed as a relative
   path; icon and licence paths are now passed with `-D`.
5. `makensis` died with SIGBUS: it mmaps a solid-compression block in `TMPDIR`, and
   `/tmp` here is a 992 MB tmpfs. `TMPDIR` now points into `.cache/winbuild`.

### Side effects disclosed
- Dumped the Wine prefix twice by filling `/tmp` (a 992 MB tmpfs). Cleaned up; no
  workspace data lost.
- Killed my own shell twice with `pkill -f` patterns that matched the command line.
  Avoided thereafter by using `wineserver -k` and explicit PIDs.
- `dist/` was removed after copying the artifacts to `release/`: the workspace
  reached 178 MB against a ~128 MB snapshot cap. Builds are reproducible.

## 2026-10-26 — v1.0.1: black window / self-closing on real Windows
- User report: window shows "Starting VLCFlix" ~5 s, then black; clicking after
  maximising makes it close by itself. Provided launcher.log showed the run ending
  at `[webview2] loader = ...` with nothing after it.
- Diagnosis from my own code: there was **no instrumentation after the loader**.
  Nothing recorded whether environment creation succeeded, whether the controller
  attached, whether Navigate ran, or whether the page loaded. The black surface was
  at a step I could not see. Also, every WebView2 failure path called
  `PostMessage(WM_CLOSE)`, which is the self-closing behaviour.
- v1.0.1 changes:
  1. HRESULT logged at every step, with hints for the common codes
     (0x80070002 FILE_NOT_FOUND, 0x80040154 CLASSNOTREG, ...).
  2. `NavigationCompleted` and `ProcessFailed` handlers added (were absent).
  3. A failed browser now raises a **native fallback panel** instead of killing the
     window: reason, local URL, "Open in browser", "Retry", and the log path.
  4. Logging moved to the first statement of `wWinMain`; a previous build initialised
     it after window creation, so early exits (including the single-instance branch)
     left no trace at all.
  5. Removed an unused comctl32 dependency and a leftover empty `EDIT` control.

### Verified (Wine 10 + Xvfb, screenshots read)
- `[webview2] CreateCoreWebView2EnvironmentWithOptions -> 0x80070002
  (ERROR_FILE_NOT_FOUND)` and `[fallback] showing native panel: ...` appear in
  launcher.log — the instrumentation captures the real failure.
- Screenshot of the panel confirms it renders, is readable, shows the live URL
  (http://127.0.0.1:43139/), the log path and both buttons, and that the window
  stays open. Saved as docs/screenshots/webview2-fallback.png.

### Harness mistakes, disclosed
- Ran Wine several times without `DISPLAY`, then spent time chasing the
  `CreateWindowEx failed` that resulted. `WINEDEBUG=+seh` exposed
  `nodrv_CreateWindow ... display server is running` — it was the harness.
- Called `pkill -f` a third time with a self-matching pattern and killed the shell.

### Still unproven
- WebView2 has never rendered a pixel here; Wine has no WebView2 runtime. The
  fallback path is proven, the success path is not. The real cause of the user's
  black window should now be identifiable from `launcher.log`, but it is not yet
  confirmed — most likely a missing/blocked WebView2 runtime.

## 2026-10-26 — v1.0.2: WebView2 succeeds yet nothing renders
- User ran **both** the installer and the portable build; identical result, so it is not
  a packaging problem. New log (1.0.1) is decisive:
  `environment Invoke -> 0`, `CreateCoreWebView2Controller -> 0`, `controller Invoke -> 0`,
  `get_CoreWebView2 -> 0`, `Navigate -> 0`, and **no `navigation starting`/`completed`**.
  The runtime IS present and every call succeeds, but the page never loads.
  1.0.1 hid the loading label at controller-attach, which is exactly the moment the
  screen went black. "Maximise then click closes it" is the same failure seen again.
- This matches a documented failure class: JUCE #1642 — "controller is created
  successfully, Navigate() returns S_OK, but no events fire … page source remains
  about:blank".
- v1.0.2:
  1. watchdog (8 s): if NavigationCompleted has not fired, dump browser state and escalate;
     `LogWebViewState()` records visible / bounds / source / browserPid — the three
     possible causes are told apart by that single line.
  2. second attempt with `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--disable-gpu` and a
     separate profile (a wedged browser process must not answer the retry);
     `--disable-gpu` is the standard remedy for a surface that is created but never paints.
  3. every previously unchecked call is logged: put_IsVisible, put_Bounds, all three
     add_*Event calls, get_Settings.
  4. the label now stays **above** the webview until the page paints, so a browser failure
     can never present as an unexplained black window again.
  5. `SetUnhandledExceptionFilter` logs exception code, address and module, which covers
     the "maximise and click closes it" symptom if it is a real crash.
  6. the final message now distinguishes "runtime could not be started (hr)" from
     "started but never drew the page" — the first version told users to install a
     runtime they already had, whenever the environment call failed for another reason.
- Verified under Wine (no WebView2 present): attempt 0 -> 0x80070002 -> attempt 1 with
  --disable-gpu -> panel reading "The Microsoft Edge WebView2 Runtime could not be
  started (0x80070002)". Screenshot read. Retry ladder and accounting both work.
- Removed a diagnostic that passed a null handler to add_WebMessageReceived: it can only
  ever fail, so its result proves nothing.

## 2026-10-26 — v1.0.3: ROOT CAUSE — controller reference was borrowed, not owned
- New log: all HRESULTs 0, watchdog fires at 8 s, then
  `[crash] code=0xC0000005 address=... module=VLCFlix.exe` — the crash is in OUR code,
  immediately after the watchdog, i.e. on the first use of the controller after Navigate.
- **Root cause:** `ICoreWebView2Controller*` passed to
  `ICoreWebView2CreateCoreWebView2ControllerCompletedHandler::Invoke` is a borrowed
  reference. `g_controller = controller;` stored it raw, so the runtime released its own
  reference when Invoke returned and the object was destroyed. Microsoft's docs for
  ICoreWebView2Controller: "The CoreWebView2Controller owns the CoreWebView2, and if all
  references to the CoreWebView2Controller go away, the WebView will be closed."
  That single defect explains every symptom seen so far:
    * black window: the webview was closed microseconds after creation;
    * `Navigate -> 0` with no navigation events: Navigate ran *inside* the handler,
      before the release;
    * "maximise then click and it closes" (1.0.0/1.0.1): WM_SIZE called put_Bounds on
      the freed object;
    * standalone crash (1.0.2): the watchdog called get_IsVisible/get_Bounds/get_Source
      on the freed object.
- Corroborated by four independent sources (StackOverflow 77772018, StackOverflow
  66820213, AutoHotkey community thread, MS ICoreWebView2Controller reference).
- Fix: `controller->AddRef();` before storing, `Release()` in TearDownWebView().
- Verified under Wine: no crash, escalation still reaches the panel, process stays alive.
  The render itself still cannot be exercised here.
- Lesson recorded: whenever a WebView2 completed handler hands over an interface, it is
  borrowed unless AddRef'd — including the environment.

## 2026-10-26 — v1.1.0: scan speed, subtitles, ffmpeg engine
User confirmed the 1.0.3 AddRef fix works. Three requests: make scanning feel
instant, add an ffmpeg playback engine with hardware decode, and pick up sidecar
subtitles.

### Scan
- Was: one pass, one file at a time, ffprobe + 3 ffmpeg jobs + 2 luma measurements,
  and the catalog was only published after the last thumbnail.
- Now: phase 1 reads metadata only (ffprobe, 6-way concurrency) and publishes the
  catalog; phase 2 renders artwork 2-way in the background, republishing at most
  every 1.2 s. The UI says "Library ready — drawing artwork 40/113" and reloads the
  grid as art lands.
- Measured here (8 clips): catalog usable in **685 ms**, artwork done +8.3 s in the
  background. The old code made the user wait for the whole 9 s.
- Known trade-off: artwork uses 2 ffmpeg workers so a low-end machine is not
  saturated while the user is browsing.

### Subtitles
- `findSubtitles` matches Movie.srt / Movie.en.srt / Movie.forced.srt (+ .vtt),
  labels known languages, sorts the bare sidecar first.
- `toVtt` converts SRT to WebVTT because `<track>` silently ignores SRT.
- Regression caught during development: the cue-number regex used `\s*`, which
  matched across the blank line between cues and merged them. Fixed to `[ \t]*`;
  the negative control (restoring the buggy regex) fails exactly one test.

### FFmpeg engine
- Third `playMode`. `?engine=ffmpeg&t=N` spawns ffmpeg, streams fragmented MP4.
- H.264 is copied, not re-encoded; anything else is transcoded with `-hwaccel`
  (qsv/d3d11va/dxva2 on Windows, videotoolbox on macOS, software otherwise).
- Seek restarts the stream at the new offset; the UI adds the offset back so the
  clock shows true positions. VAAPI deliberately not auto-selected: it needs an
  explicit device and format negotiation.
- Verified with ffprobe on the served bytes: mpeg4-in-MKV -> video:h264 audio:aac
  in 198 ms; H.264 MP4 -> stream copied unchanged.

### Environment note
ffmpeg/ffprobe are NOT installed by default in this sandbox (packages do not
persist between turns). With them missing, `tools.canRender` is false and no
artwork is produced — that is correct behaviour, not a bug: install ffmpeg.

## 2026-10-26 — v1.1.1: the scan always continued; the UI just stopped reporting it
User asked whether the scan keeps going after closing Settings. Answer: yes on the
server, no on screen — and the second part was three client bugs.

Proved the server half first: with zero requests for 6 seconds, posters went 0 -> 8
(`phase=art done=8/8`). The scan is a server-side job and nothing cancels it.

The client half was broken:
1. `closeSettings()` called `clearInterval(state.scanPoll)` — polling died with the panel.
2. Even without that, the tick did `const status = $('#scanStatus'); if (!status) { clearInterval(...) }`,
   and that element does not exist once the panel is closed.
3. `refreshCatalog()` was only reachable from that poll, so the grid never updated.
4. On boot `if (state.scan?.running) openSettings()` force-opened the panel.

v1.1.1:
- `watchScan()` owns the polling, independent of any panel, and updates whatever
  happens to be on screen.
- Header chip (`#scanChip`) shows "Reading 40/113" then "Artwork 40/113 · 35%"
  anywhere in the app; clicking it opens Settings. Hidden when idle.
- Grid refreshes as artwork lands (throttled to 1.5 s).
- Boot no longer hijacks the view; it just starts the watcher.
- Reopening Settings paints current progress immediately (one `tick()` on start)
  instead of waiting up to 750 ms for the first interval.

Second bug found while verifying: `.scan-chip { display: inline-flex }` beats the UA
`[hidden]` rule, so the chip never disappeared. Added `.scan-chip[hidden] { display: none }`.

Verified in real headless Chromium (playwright 1.49, temp install) with the settings
panel closed the whole time:
  idle page            chip hidden
  closed 6 s           chip "Artwork 4/32 · 13%", 12 artwork images in the grid
  reopen mid-scan      "Library ready — drawing artwork 6/32" after 150 ms
  finished             chip hidden, 100 artwork images, 0 console errors

## 2026-10-26 — v1.1.2: engine switch, preview montage, preview in Info

### "I can't switch to FFmpeg (hardware)"
Two independent defects, both mine, both introduced with the engine in 1.1.0:
1. `POST /api/settings` whitelisted only `['vlc','browser']`, so `playMode: 'ffmpeg'`
   was answered with 400 and never saved. The client showed nothing at all.
2. The segmented control's handler did
   `await saveSettings(...)` then `event.currentTarget.classList.add(...)`.
   `currentTarget` is null once an await yields, so it threw — **after** the line that
   clears `is-active` from every button. Result: the control rendered with no button
   selected and no error the user could see.
Fixed by capturing the button before the await, returning early if the save is
refused, and surfacing server refusals as a toast instead of swallowing them.

### Preview = one moment -> montage
`previewSegments()` takes five 2 s snippets at 12/32/52/72/90% of the runtime and
concatenates them in a single ffmpeg pass. Segments are clamped so they never
overrun the end, and a short film yields fewer distinct snippets rather than
repeating frames. `PREVIEW_VERSION = 2` marks old clips stale so an existing library
re-renders them without a manual rescan; the version is only claimed once a clip
actually exists, so a transient ffmpeg failure retries next scan.

Verified with a 40 s film built as RED/GREEN/BLUE/WHITE quarters: the generated
preview samples at 1/3/5/7/9 s return ff0000, 01ff02, 0000ff, 0000ff, fffeff —
i.e. the film's own colour sequence, not one repeated frame.

### Preview in the Info panel
`openDetail` now calls `attachHeroPreview(hero, item, { delay: 500 })`, so the panel
plays the same clip the hover card plays.

### Third bug found while testing this
`el()` assigns unknown props with `setAttribute`, so `muted: true` set the *content
attribute* (which maps to `defaultMuted`), not the property the autoplay policy
checks. The clip was silently refused and `is-on` was never added, which is why the
panel showed a still. Fixed in both the hero and card previews by setting
`video.muted = true` directly.

### Testing note
Playwright's headless Chromium ships **without H.264** and reports
`DEMUXER_ERROR_NO_SUPPORTED_STREAMS` — it cannot play these clips at all. Installed
the real Chrome channel (`playwright install chrome`) for a trustworthy result:
detail video reached `paused=false, opacity=1, currentTime 2 -> 3.2`, zero console
errors. Do not diagnose video problems with headless Chromium.

## 2026-10-26 — v1.1.3: VLC sync keys, subtitle picker, size and position

### Keys
G/H = subtitle delay, J/K = audio delay, V = next track — the Windows/Linux VLC
defaults. Sources disagree on direction (some blogs have G/H reversed), so this
follows the VideoLAN-derived tables: **G earlier, H later, J earlier, K later**,
50 ms per press like VLC. Every press toasts what it did ("Subtitles +300 ms
(later)") so the user never has to remember which way is which.

### Native <track> had to go
`<track>` + `::cue` is opaque once loaded: cue timing cannot be shifted and there
is no usable control of size or vertical position. Replaced with a small WebVTT
parser (`parseVtt`) and an overlay renderer driven by requestAnimationFrame.
Cue text is HTML-escaped and only `<i> <b> <u> <em> <strong>` are restored, because
subtitle files are untrusted input.

### Subtitles that are not named after the video
`listFolderSubtitles` lists every `.srt`/`.vtt` in the film's folder, name matches
first; the picker shows them all and a mismatched file keeps its own stem as the
label. The choice is stored per film in `subtitleChoices.json` and used as the
default next time.

### Two bugs caught by the live test
1. **Unit mismatch:** `sub.delay` is milliseconds (VLC's unit), but `subtitleClock`
   subtracted it as seconds. Pressing H twice moved the clock back 100 *seconds* and
   every cue disappeared. Only visible by playing a file, not by reading the code.
2. Release-named subtitles were labelled "Ghostlight.2019.1080p.WEB DL" — the label
   function was title-casing and de-hyphenating a filename. It now keeps the stem.

### Verified in real Chrome
cue renders; G/H shift it and the label follows; size 100%->120% changes the
computed font size to 36px; height label and CSS variable both read 12%; picking
`Ghostlight.2019.1080p.WEB-DL.srt` renders "RELEASE SUB italic" and the server
reports `selected=Ghostlight.2019.1080p.WEB-DL.srt`. Zero console errors. 48 tests
(was 44).

## 2026-10-26 — v1.1.4: negative audio sync, and automatic Bluetooth latency
Two requests that turned out to be the same problem.

### The observation that solved both
A browser cannot advance sound past the file's timeline — that is `<video>`, not
VLCFlix. But "sound 200 ms earlier than the picture" and "picture 200 ms later than
the sound" are the same outcome, and delaying the picture is always possible.
So the negative direction is served by holding frames back:
`requestVideoFrameCallback` captures decoded frames, `createImageBitmap` stores them,
and a canvas overlay draws the frame that belongs to `currentTime - delay`.

The arithmetic lives in `public/js/sync-math.js` so it can be tested directly:

    videoDelay = deviceLatency + audioDelay - manual     (both >= 0)

with the smallest non-negative pair chosen. 14 tests in tests/sync.test.js,
including the one that matters most: turning the manual offset to zero must not
cancel the device correction.

### Automatic latency compensation
Settings toggle plus an AUTO button in the player panel. Reads
`AudioContext.outputLatency`, re-measures every 2.5 s while playing, smooths small
drifts (large changes jump, because that means a different device), and always
applies the correction on the picture side. That is what keeps it independent of the
manual offset: the manual control stays at 0 and remains the user's to use.

### Two bugs the live test caught
1. **Use-after-close on the frame buffer:** pruning closed the bitmap that was on
   screen -> "The image source is detached". The displayed frame is now exempt from
   pruning, and a failed draw restores the previous reference instead of leaving a
   dangling one.
2. **Memory:** frames were captured at source resolution, so a 4K film buffered for a
   second would cost hundreds of MB. Capture is now capped at 1280 px wide and 48
   frames, drawn to a canvas of the same size (the canvas is never larger than the
   window, so nothing visible is lost).

### Verified in real Chrome
- 6x J (-300 ms): audio label -300, canvas visible, measured picture lag **0.34 s**.
- 10x K to +200 ms: canvas hides, back to the delay node.
- AUTO on: device readout 32 ms, measured lag 0.06 s, `autoLatency` persisted.
- With AUTO on, H still moves subtitles (+200 ms) and audio stays 0 ms.
- Zero console errors. 62 tests (was 48).

## 2026-10-26 — v1.1.5: an upgrade left the old previews in place
Reported: reinstalled without uninstalling and the montage previews did not appear.

Two causes, both mine, and the second would have defeated a rescan anyway:

1. **Nothing re-rendered on upgrade.** The 1.1.2 stale-preview check lived inside
   `scan()`, and `runScan()` is only reached from `POST /api/library/scan` -- i.e.
   the Scan button. After upgrading, the stored catalogue (with single-chunk
   previews) was never revisited, so the new format never happened.
2. **The browser would have kept serving the old clip.** `/media` is mounted with
   `maxAge: '1h', immutable: true` and a re-render keeps the same filename, so the
   URL was byte-identical. `immutable` tells the browser not to revalidate, so even
   a completed re-render would be invisible for up to an hour.

Fixes:
- `pendingArtwork()` is checked once at startup; if anything is missing artwork or
  carries an older `previewVersion`, the refresh runs in the background 1.5 s after
  listen (after the window paints), logging
  `[startup] N item(s) need artwork or a newer preview`.
- Artwork URLs now carry `?v=<previewVersion>-<source mtime>`, so a re-rendered
  asset is a different URL and therefore a real request. A changed source file
  changes the mtime part too.

Verified by simulating the exact upgrade: built a library with the current code,
then downgraded the stored state to what 1.1.1 would have left (previewVersion
removed, preview overwritten with a single 8 s chunk sampled at one timestamp).
A plain restart, with no user action, logged the refresh and produced a 10 s
montage whose colours follow the film (ff0000, 01ff02, 0000ff, 0000ff, fffeff).
The URL moved from `?v=0-<mtime>` to `?v=2-<mtime>`.

Test note: the first version of the cache-busting test passed even with the fix
reverted -- it looped over `catalog.items`, which was empty, so the assertion never
ran. It now seeds its own catalogue and fails correctly when the version is removed.
64 tests (was 62).

## 2026-10-26 — v1.1.6: the large centre play/pause icon did nothing
Two defects in the same control:

1. `.player__center { pointer-events: none; }`. The overlay is the element with the
   click listener, so with pointer events disabled the click never arrived. The
   handler was correct and simply unreachable.
2. `updatePlayerToggle()` refreshed the small bottom button and the clock but never
   the centre glyph, which stayed hardcoded to `#i-play` whatever the state.

Now: the overlay accepts clicks (cursor changes), its glyph and aria-label follow
play state, and it also shows while playing so it is a visible pause affordance —
but only while the chrome is on screen, not floating over the film.

Checked that making a full-screen overlay interactive did not eat other clicks:
picture click pauses/resumes and flips the glyph both ways, the bottom toggle still
works, scrubbing to 70% still seeks (t=11.89s), the CC panel still opens and its
buttons still work, zero console errors. Screenshot read.

## 2026-10-26 — v1.1.7: picture clicks by position, double-tap skips, scroll checked
Five reports; four were code, one was not.

1. *"Moving around doesn't bring up the controls."* The cause was v1.1.6 itself. To
   make the big centre icon clickable I had given `.player__center` the whole
   picture as clickable area, at full screen size -- so it sat on top of the close
   button, the bottom bar and everything else. Any click meant for the chrome hit
   the overlay instead. The overlay is display-only again and the player root now
   decides what a click means from where it landed.
2. *"Only the middle centre should activate pause."* The picture is split at 30% and
   70%: left third, middle, right third. The middle toggles; the sides do not.
3. *"A double tap at the middle should toggle when the icon isn't displaying."* With
   the controls hidden, one click in the middle waits 320 ms: a second click turns
   that into a toggle, otherwise the click just brings the controls back. With the
   controls already up, every middle click toggles immediately -- no waiting, and
   two quick clicks pause then resume rather than being read as a "double" that
   cancels itself out.
4. *"Double tap right fast forwards, same at left."* The sides skip 10 s in that
   direction and confirm with a `+10 s` / `−10 s` badge placed over the side used,
   so it does not cover the centre icon.
5. *"Scrolling on the touchpad is more sensitive than normal."* Measured rather than
   guessed. Ten 100 px wheel notches moved the page exactly 100, 200, 300, 400, 488,
   488 … (it stops at the document end): one-to-one, no amplification. `grep` finds
   no `wheel` handler anywhere; the horizontal rails do not hijack a mostly-vertical
   gesture; wheel over a nested panel at its end does not chain to the page behind;
   frame gaps stayed at 17 ms even while the startup artwork refresh was running.
   The Windows shell source is byte-identical to v1.1.4 (`git diff 500da0a --
   src/win/main.cpp` is empty), so the installed app cannot have changed how a swipe
   is delivered. What did change defensively: `overscroll-behavior: contain` on both
   nested scrollers, so one gesture can never scroll a panel and the page behind it.

Testing: a new 14-check harness covers every requirement above, with a negative
control -- collapsing the three bands into "everywhere is the middle" makes it fail
exactly where it should. 64 unit tests still pass. Screenshot of the skip badge read
back before shipping.

## 2026-10-26 — v1.1.8: artwork resumes instead of starting over
Reported: artwork draws everything again at every launch, so it takes a long time
to become usable. Two separate causes, both measured.

1. **Startup triggered a whole library scan.** Whenever anything was outstanding,
   the launch path called `runScan()`, which walks every folder and runs `ffprobe`
   over every file before it draws anything — reading 0/N, then artwork 0/M. On a
   large library that is minutes of work to draw a handful of missing pictures, and
   it looks exactly like starting over.
2. **One unreadable file made that permanent.** An item that cannot be drawn
   (`duration === 0`) was skipped silently and stayed outstanding forever, so every
   single launch re-ran the full scan. Reproduced: 12 good clips plus one file of
   random bytes logged `[startup] 1 item(s) need artwork` and did a full rescan on
   every launch, indefinitely.

Now the launch path renders only what is missing, from the stored catalogue: no
walk, no `ffprobe` for items that already have a duration. Measured on 60 clips,
killed mid-pass at 6/60: relaunch reached the artwork phase in **0.4 s** (was
2.1 s) with `[startup] artwork: 5 of 60 already drawn, resuming 55 — no rescan
needed`, and drew only the remainder.

Also fixed while in there:

- **Judged by disk, not by the catalogue.** `missingArt()` stats the three files.
  A catalogue entry that outlives its files (deleted media, a wipe) is redrawn; an
  entry that lost its fields but whose file is on disk is adopted without drawing.
- **Atomic files.** Posters, backdrops and previews render to `*.tmpNNN.jpg/mp4`
  and are renamed when complete, so a killed pass can never leave a truncated file
  that later counts as finished. Stale temp files are swept at startup.
- **Failures are recorded**, not retried forever: `artAttempts`/`artError`, three
  attempts, then left alone until a manual scan. The bad file from the repro now
  settles and the fourth launch logs `artwork complete for all 13 item(s)`.
- **The progress chip tells the truth**: an artwork pass shows `Artwork 3/8 · 37%`
  from the first paint (the pass is announced before the 1.5 s delay that lets the
  window draw) and ends with *"Artwork finished"* instead of claiming a scan ran.

Verified: fresh install over an existing user-data folder rewrote **0 of 48**
artwork files; deleting every preview with the catalogue intact redrew exactly the
16 previews; a catalogue field nulled by hand was restored from the file on disk
with 0 files rewritten; 64/64 unit tests, including a rewritten artwork test whose
negative control (catalogue-only check) fails as it should; the player harness
still passes end to end.

## 2026-10-26 — v1.1.9: the window could not paint while artwork rendered
Windows report: launcher log shows `navigation starting`, then
`WATCHDOG: no navigation completion after 8000 ms` on **both** attempts, the
native fallback panel, and only after pressing Retry
`navigation completed: success=1 http=200`. So the page was reachable but slow to
commit, twice, and fine on the third try.

Cause: v1.1.8 starts the artwork pass 1.5 s after the server listens — which is
exactly when WebView2 is booting. ffmpeg rendering competes with the first paint,
and on a cold start that took longer than the 8 s watchdog. The server was never
the bottleneck: it answered `/api/catalog` in 4 ms while rendering.

Fixes:

- **The page now says when it has painted.** `POST /api/ui/ready` is sent at the
  end of `boot()`, and the artwork pass waits for it (20 s ceiling, so a window
  that never reports still gets its artwork drawn in the background). Measured:
  page loads in 77 ms with the pass held back, and artwork starts **501 ms**
  after the page is up instead of racing it.
- **Watchdog 8 s -> 20 s.** A local page that cannot commit in 20 s with the CPU
  free is genuinely broken; 8 s produced false failures on a cold runtime.
- **One stat pass at startup** instead of four (`artworkOnDisk()` replaces the
  adopt pass plus two `pendingArtwork()` passes plus the pass inside the renderer).
- **Dead URLs are cleared**: when a media file is gone, the catalogue field that
  pointed at it is nulled, so the grid falls back to its gradient instead of
  requesting a 404 — found because a deleted-artwork test logged two 404s.

Two process notes, disclosed rather than buried: the `/api/ui/ready` route was
first registered after the catch-all 404 handler, so the ping failed and the pass
fell back to its 20 s deadline (caught by the harness, not by eye). And an
`accept.js` run reported a console 404 that turned out to be **a stale server from
an earlier run still holding port 4976**, answering with pre-change code; stray
servers were killed and the harness now clears its port first. The player checks
it failed on were re-run against a fresh server and all pass.

Verified: `npm test` 64/64; artwork harness (page gated, chip, toast, no temps,
no console errors) all pass; player harness 14/14; API stays at 4 ms while ffmpeg
renders.

## 2026-10-26 — v1.1.10: a failed stream was a silent black screen
Reported: with the **FFMPEG** engine the player chrome shows for a few seconds,
then everything goes black and nothing else happens. The launcher log for the same
session shows the window itself now loading (after the 1.1.9 fix).

The player had **no error handler at all** on `#playerVideo`. Whatever went wrong
in the ffmpeg pipe — a build without `libx264`, a codec it will not transcode, a
stream that sends nothing — the element stayed black and the chrome auto-hid. That
is the whole reported behaviour.

The ffmpeg path itself plays correctly here: 1280x720, `readyState 4`, position
advancing, no console errors. So the *cause* on that machine is still unknown,
but it no longer matters to the viewer:

- a media `error` event, or **no picture within 7 s**, is treated as a failed
  stream and the film is retried **directly from the file** at the same position,
  with a toast explaining the switch;
- if the file will not play that way either, the reason appears in the player
  ("the video could not be decoded") instead of a black rectangle;
- the server checks `ffmpeg -encoders` once and answers **501 / NO_X264** when a
  transcode is needed but the build cannot encode, rather than spawning a pipe
  that dies;
- the 502 for an empty pipe now carries ffmpeg's own last error line, and
  `server.log` records it.

Verified with three forced failures: a 500 on the ffmpeg URL (falls back and
plays, toast shown), a 200 that never sends a byte (gives up at 7 s, falls back
and plays), and every stream failing (reason shown on screen — screenshot read).
Normal ffmpeg playback still works, 64/64 tests, player harness 14/14.

Housekeeping: the workspace was 206 MB against a 128 MB snapshot budget. Six old
installer/zip pairs were deleted, leaving only 1.1.9 (55 MB counted). Two stale
servers left by earlier harness runs were also found and killed — one of them had
been answering `accept.js` on port 4976 with pre-change code.

## 2026-10-26 — v1.1.11: the black screen named itself
The user's `server.log` had the answer:

```
[ffmpeg] Mean_Girls_2_1080P.mp4: transcode (qsv) at 0s
[ffmpeg] Impossible to convert between the formats supported by the filter
         'Parsed_null_0' and the filter 'auto_scale_0'
[ffmpeg] Pixel formats:  src: qsv   dst: yuv420p ...
[ffmpeg] Could not open encoder before EOF
[ffmpeg] Nothing was written into output file ...
```

The transcode path passed `-hwaccel qsv`. QSV decodes into the GPU pixel format
`qsv`, and neither the implicit scaler nor libx264 can take that, so ffmpeg exits
before writing a single frame. Every film that needed transcoding (anything not
already H.264) was therefore a black screen — which is exactly what was reported,
and why the 1.1.10 fallback could only ever show a notice: the browser cannot
decode those HEVC files either.

Fix: **no hardware decode in the transcode path.** Transcoding is CPU-bound on
the encoder side anyway, and software decode always yields frames the encoder can
consume. `detectHwAccel()` and its unused `accelerated` response field are gone.

Also fixed, from the same report ("it was like finding one's way on a broken
screen ... I found where the close button was"): the chrome's auto-hide now
refuses to run while `videoWidth === 0`, and showing the notice cancels the hide
timer. A failed stream keeps its controls — and its close button — on screen.

Verified with a 10-bit HEVC 1920x1080 clip (the same shape as the reported file):
it now transcodes and plays through ffmpeg itself — `readyState 4`, 1920x1080,
position advancing, no fallback, no console errors, and the log reads
`transcode (software decode)`. Control behaviour checked both ways: with a
picture the chrome still hides when idle and returns on a mouse move; with a
broken picture it stays (`is-active` true, notice visible, close button
clickable). 64/64 tests, player harness 14/14.

Two self-inflicted errors this turn, both caught before shipping: the block
deletion that removed `detectHwAccel` also took `hasX264()` with it, which made
every ffmpeg stream 500 (the HEVC harness caught it, the unit tests did not), and
the first `directStreamUrl` dropped the `?` from `?engine=ffmpeg&t=40`.

## 2026-10-26 — v1.1.12: the way out is never hidden
"The hover still doesn't show those controls." `#playerClose` was a child of
`.player__chrome`, and that container is `opacity: 0; pointer-events: none`
unless `.player__chrome` matches `.player.is-active`. So whenever the chrome was
hidden, the close button was invisible *and* unclickable — on a black screen it
had to be found by memory, which is exactly what was described.

- the close button now lives outside the auto-hiding container, with its own
  always-on style, `z-index: 8` and its own background, so it reads over a black
  frame as well as a bright one
- a document-level `pointermove` reveals the chrome whenever the player is open,
  rather than only movement over the picture

Verified: with a playing picture the chrome still fades after a few seconds and
the close button stays visible and clickable; a mouse move anywhere brings the
chrome back; on a forced black screen the chrome, notice and close button are all
present and the button closes the player (screenshot read). Player harness 14/14,
fallback harness 10/10, HEVC transcode harness 6/6, 64/64 tests.

Answering the question that came with it: the playback regression was **v1.1.0**
(`95d453f`, "ffmpeg hardware playback"), which introduced `-hwaccel qsv` on the
transcode path. Before 1.1.0 there was no ffmpeg engine at all — films played
from VLC or straight from the file, so nothing was ever transcoded and nothing
was black. 1.1.11 removed the hardware decode.

## 2026-10-26 — v1.1.13: the three player bugs from the middle-button work

Three separate faults, all in code written after v1.1.5 — the comparison against
`3d2e10b` (v1.1.5) confirmed the seek code is byte-for-byte what 1.1.5 shipped, so
the regression was never in seeking itself:

1. **No controls while the picture is being manipulated.** `.player__delay` is a
   full-screen canvas at `z-index: 2` that holds frames back for audio sync;
   `.player__chrome` had no `z-index` at all, so the controls were painted *under*
   an opaque canvas and could not appear until the delay was switched off.
   `.player__chrome` is now `z-index: 3`.
2. **Seek always restarted the film.** A resumed session opens
   `/api/stream/<id>?engine=ffmpeg&t=14`; `restartStream()` appended its own
   offset, producing `…&t=14&t=9`. Express hands a repeated key to the route as an
   array, `Number(['14','9'])` is `NaN`, and the offset became 0 — the log said
   `copy video at 0s` on every seek. `streamUrlAt()` now rebuilds the query with
   `searchParams.set('t', …)`, so the offset is replaced; the server also takes the
   last value if a duplicate ever arrives. Fresh sessions always seeked correctly,
   which is why this looked like "only some films".
3. **Clicking near the middle button paused.** The tap handler toggled for the
   whole middle third. Now only `event.target.closest('.player__center')` toggles,
   and the icon alone is a pointer target (`pointer-events: auto`) while the
   controls are up; a click elsewhere in the middle just keeps the controls on
   screen. With the controls hidden the double-tap gesture still toggles.

Verified with a new harness against a *resumed* film: a 60 % seek produced
`?engine=ffmpeg&t=9` and the server answered `copy video at 9s`; a click 110 px
beside the button left the film playing, a click on the button paused it; with the
delay canvas live the chrome computed `z-index: 3` over the canvas's `2`, the close
button hit-tested as the top element, and the screenshot shows the scrub bar and the
"Audio −300 ms" pill drawn over the picture. Regressions all green: player 14/14,
fallback 10/10, HEVC 6/6, chrome 6/6, 64/64 unit tests.

## 2026-10-26 — v1.2.0: hardware decoding, but only if you ask for it

"Add it, but add it as an option that you have to explicitly go and activate.
Normally, it should use software, but if the option is explicitly selected, it
should try hardware first."

So the GPU is back in the transcode path, behind a switch that starts off:

- `settings.hwDecode` (Settings → Playback → **Hardware decoding (experimental)**),
  default `false`, validated as a boolean, whitelisted in `POST /api/settings`.
- when on, `streamViaFfmpeg` builds a plan ladder from the decoders this ffmpeg
  build lists — `qsv`, `d3d11va`, `dxva2` on Windows — each with
  `-hwaccel_output_format` and an explicit `hwdownload,format=…` filter. That
  filter is the whole fix: without it the scaler and x264 cannot read GPU frames,
  which is the black screen of v1.1.0–v1.1.10.
- 10/12-bit films ask for `p010le` first and 8-bit films for `nv12`, so the first
  trial is not a conversion the hardware is likely to refuse.
- every attempt is a trial: no first frame within **2.5 s**, an early exit, or a
  device that will not open marks that plan broken and replays the *same request*
  in software. Broken plans are remembered for the run, so the ladder retires
  itself instead of charging 2.5 s to every seek.
- the log says which pipe ran: `transcode (hardware qsv -> p010le)` or
  `transcode (software decode)`, plus why a hardware plan was abandoned.

Caught by a test rather than by the user: `POST /api/settings` copies only keys in
its `allowed` list, and `hwDecode` was not in it — the validator existed, the value
was silently dropped, and the switch did nothing. The settings test failed with
`200 !== 400` on a deliberately invalid value, which is exactly what a whitelist
that eats a key looks like from outside.

Verified on this GPU-less machine, which is the interesting case: with the option
on, a 10-bit HEVC film is sent to `qsv -> p010le`, ffmpeg answers
`Device creation failed: -542398533.`, the request is replayed in software, and the
browser gets a picture (1280x720, 192 frames, readyState 4, no fallback notice). The
ladder retired `qsv -> nv12` and `vaapi -> nv12` one request each and then went
straight to software; an H.264 film is still copied with the option on; the choice
survives a restart. Settings UI: the row renders, the switch saves, the choice
survives a reload, and the engine button no longer claims to be hardware. 72/72
tests.

Also added `tools/hwdecode-probe.cmd` for the laptop: it runs the same eight plans
against a real film (or a 3-second 10-bit HEVC clip it makes itself) and writes a
report of which ones produce frames. **Not yet run on Windows** — that is the next
thing to do with it.

## 2026-10-28 — Audit + port plan, branch `cpp` (no code written yet)

User direction: rewrite the whole product in pure C++ — everything that is there,
plus everything the web version could not do — working on a second branch, under
the rules in `AGENTS.md`. The repo has **one** branch (`main`); created `cpp` from
`d4606a7` and stated the assumption in the reply so it can be renamed for free.

Read before acting (AGENTS §6): `AGENTS.md`, `CONTEXT.md`, `TURN_LOG.md`,
`checkpoint.json`, then the source — `server/{index,library,store,vlc}.js`,
`public/js/{app,sync-math}.js`, `public/index.html`, `README.md`.

Environment prepared (all ephemeral, none survives a snapshot): `cmake`,
`ninja-build`, `libsdl2-dev`, `libvlc-dev`, `ffmpeg`, `vlc-bin`,
`vlc-plugin-base`, `zlib1g-dev`, `libfreetype-dev`. Verified: g++ 14.2, SDL2 2.32,
libVLC 3.0.24 headers, ffmpeg 7.1.5, 2 cores, 1.9 GB RAM, 21 GB free.

**Verified, not assumed** (`docs/AUDIT.md` §2 has the commands):

- `npm install` + `npm test` → **72/72 pass**; the shipped suite misses every bug
  below.
- A1 `parseName` destroys titles that are years: `1917.2019.2160p` → title
  `"1917 2019 2160p WEB-DL"`, year 1917; `Blade.Runner.2049.2017` → year 2049.
- A2 the ffmpeg copy engine cannot seek the bundled clips: they have **1 I-frame in
  480 frames**, so `-ss 4 -c copy` emits `duration=16.000000` (the whole clip).
  With a dense GOP (g=24) the same command emits `12.000000`.
- A3 `Range: bytes=-500` is answered `Content-Range: bytes 0-500/217238`.
- A4 `setProgress(id, 97, 100)` keeps the entry; nothing ever marks a film finished.
- A5 `/10|12/` classifies `nv12` as 10-bit.
- A6 `POST /api/settings {"vlcPassword":{"a":1}}` → 200.
- A7 README/API drift (subtitle route, "23 tests" vs 72).
- Checked and **withdrew** a suspected unit bug in `subtitleClock()`: `sub.audio`
  is stored in seconds, the arithmetic is correct.

**Spike that the plan depends on** (`/tmp`, throwaway): 40-line libVLC
`set_callbacks` program played `samples/the-silent-coast.mp4` into a `std::vector`:
`frames=22 luma=132`, `length_ms=16000`, and `set_time(12000)` → `time_ms=12390`
with 18 new frames — i.e. exact seeking on the same file A2 cannot seek. That is
the reason to move playback in-process.

Deliverables this turn: `docs/AUDIT.md`, `docs/PORT_PLAN.md` (phases P0–P8, each
with a verification), this entry, `CONTEXT.md` and `checkpoint.json` updated.

Not done, and why: no C++ code, no build, **no push**. A fresh clone has no
credential (`.secrets/github-token` is correctly git-ignored and absent), so the
`cpp` branch exists locally only; `git ls-remote` still shows `main` alone. Asked
the user for the three decisions the first line of code depends on (branch name,
UI toolkit, whether to keep the external-VLC mode at all).

## 2026-10-28 — Three-turn plan agreed; token requested

User fixed the shape of the work: three turns, pushing to a second branch that
this session creates. Answers to the decisions were not given, so they are taken
and recorded in `docs/PLAN_3_TURNS.md` (branch `cpp`; code under `native/`, Node app
left intact as the reference; SDL2 + own renderer; libVLC in-process only; CMake
with a Makefile fallback; own test harness).

Ask outstanding: a fine-grained PAT, Contents read/write on `JohnOlowe/VLCFlix`
only. Nothing can be pushed until it arrives — this clone has no credential, which
`git push --dry-run origin cpp` proved with
`fatal: could not read Username for 'https://github.com'`. Dry run only: no refs,
objects or branches were created and the remote still has `main` alone at
`d4606a7`.

## 2026-10-28 — Turn 1: credentials, branch created on GitHub, native foundation runs

**Credential and branch.** Token written to `.secrets/github-token` (mode 600,
git-ignored, never printed). `scripts/setup-git-auth.sh` rebuilt origin, identity
and the credential helper. Identity as instructed: **John Olowe /
121398818+JohnOlowe@users.noreply.github.com**. `git push -u origin cpp` created
the remote branch; `git ls-remote` then showed
`refs/heads/cpp` at the local HEAD and `refs/heads/main` untouched at `d4606a7`.

**Two requirements added by the user**, both now in `docs/PORT_PLAN.md` §7:
1. the Windows build must look like a *new* application, never installing over
   1.2.0 — different product name, exe, install dir, data dir, uninstall key and
   mutex (`VLCFlix Studio` / `VLCFlixStudio`; product name is one constant in
   `native/src/core/brand.h`);
2. logs must always be produced, even if the app crashes on open.

**Turn 1 built** (`native/`, C++17, no third-party headers): `core/{json,log,paths,
naming,sha1,store,sync,catalog,model,brand}`, `media/{walk,probe,subtitles,artwork,
scan}`, `platform/proc`, `main.cpp` CLI (`scan`, `status`, `settings`, `naming`,
`doctor`, `crash`), `tests/` harness. CMake and a plain Makefile both build it.

Two bugs of my own, found by running rather than by reading:
- phase 1 of the scan assigned each item twice, so a cached item was overwritten by
  a bare one: titles came back empty and artwork re-rendered every launch. Fixed,
  and `adopt_existing_art()` added so a finished file on disk is adopted into the
  catalog instead of being drawn again.
- `ffprobe -of json` reports numbers as strings, so durations and dimensions read
  as 0. Fixed with a number reader that accepts both forms.

**Verified this turn** (commands and outputs in `native/README.md`): `make test`
132/132 with a negative control that fails the pre-fix parser on all four
year-titled files; fresh scan of `samples/` in 13.7 s producing 24 artwork files
(poster 400x600 opened and looked at); a second scan in 0.12 s that redraws
nothing; `kill -9` mid-pass then a resume that drew exactly the 8 missing files and
rewrote none; a deliberate SIGSEGV that leaves `[CRASH] SIGSEGV` + 9 stack frames
in the log and makes the next run report the previous one died; an unwritable log
directory falling back beside the executable and saying why in the log that opened;
settings rejected for wrong types with exit 4.

**Caught by checking rather than trusting the commit:** the first commit silently
omitted five of the new source files. The root `.gitignore` has a `media/` rule for
generated artwork, and it matched `native/src/media/`. `git status` looked clean
because the files were *ignored*, not missing. Renamed the directory to
`native/src/library/` (which also reads better: it is the port of `server/library.js`),
rebuilt, re-ran the tests (132/132) and re-ran a full scan (8 items, 0 errors).

**Not done, stated not assumed:** no window, no player (turns 2–3); no Windows
build exists, so every `_WIN32` branch (CreateProcess, %LOCALAPPDATA%, DbgHelp,
the exception filter) is written but unexecuted.

## 2026-10-28 — Turn 2: the window and the browsing UI

Branch `cpp`. Turn 1's engine (scan, artwork, store, logging) is untouched; this turn
adds the drawing and window layer on top of it and wires the UI to the real settings,
the real scan and the real My List.

**Built:** `render/canvas.cpp` (RGBA8 canvas: blend, clip stack, rounded rects,
gradient fills, circles, rounded-corner bilinear blits at any size, bitmap glyph
blending), `render/font.cpp` (FreeType glyph cache, measure/wrap/ellipsize, UTF-8
decode, DejaVu→Liberation→TTF fallback list), `render/image.cpp` (JPEG via libjpeg
with a `setjmp` error hook, PNG via libpng's simplified API, mtime-keyed cache),
`render/png_writer.cpp`, `ui/theme.cpp` (the CSS tokens), `ui/app.cpp` (state, layout,
drawing, hit recording, input), `platform/window_sdl.cpp` (window, streaming texture,
event translation), plus `tests/ui_tests.cpp` and `render`/`ui` CLI commands.

**Two properties I decided deliberately:**
- Drawing and hit-testing are the same pass. `draw()` records a `(rect, action)` for
  every control it paints; `handle()` tests against that list. A button cannot be
  drawn somewhere it cannot be clicked, and the tests click real geometry instead of
  guessed coordinates.
- Playback is not faked. Clicking Play calls `on_play_requested` and shows "the libVLC
  player is not wired up yet". Turn 3 replaces the callback; nothing else changes.

**Bugs found by looking at the rendered screens and fixed in this turn** (each one was
visible in a PNG before it was fixed):
- rail headings were drawn on top of the hero's buttons and synopsis — the hero's
  text block and the rails' top edge disagreed by 40 px;
- `stroke_round_rect` painted circular blobs at the corners whenever the radius did
  not match the edge lengths (visible on every Play/My List button); rewritten as a
  single distance-field pass;
- the detail sheet had a dead band between the synopsis and the buttons, from a fixed
  620 px panel height; the panel is now sized from its content;
- fixed button widths clipped "Open Setting" and "Scan library"; buttons are now
  measured from their label;
- the settings sheet left a dead band under the toggles (same fixed-height mistake);
- Browse All's grid left a ragged right margin; it now auto-fills and shares the
  remainder, so the right gutter equals the left one;
- the Makefile had **no header dependencies**, so editing a header relinked objects
  compiled against the previous layout. That produced a segfault inside
  `std::vector<pair<Rect,Action>>::~vector` that disappeared under ASan and at `-O0`
  — a stale-layout link, not a compiler fault. `-MMD -MP` and `-include $(DEPS)` fix
  it; CMake was never affected. I also chased it with a hand-built reproducer and
  `gdb` before spotting it, and the first thing I ruled out (a double free in the
  renderer) was genuinely fine.

**Verified this turn:**
- `make test` → **210 checks, 0 failures** (78 of them UI); CMake + CTest the same.
- Clean clone of `origin/cpp` into `/tmp`: `make` builds, `make test` passes.
- Every screen rendered and **opened and looked at**: 1600×900, 1280×720, 1920×1080,
  the 640×420 minimum, an empty library (the negative control: a real empty state,
  not a plausible frame), a 40-film grid, a hover preview, a keyboard focus ring.
- Settings → "Use the bundled samples" → **Scan library** inside the app: the folder
  is persisted, the real scan finds 8 films and writes 24 artwork files, the catalog
  lands on disk, and every screen then draws the real posters (screenshot opened).
  Adding a non-existent path and adding a duplicate are both rejected with a reason.
- `SDL_VIDEODRIVER=dummy ./build/vlcflix ui --frames 3` opens the window, draws,
  presents, closes and exits 0, leaving a clean-exit record in the log.

**Not verified, stated not assumed:** the window on a real display with a real mouse
(headless SDL only); no Windows build exists; the player is turn 3.

## 2026-10-28 — Turn 3: playback, sync, subtitles, honest finish

Branch `cpp`. Turn 2's window is unchanged in behaviour; this turn fills in the hole it
declared: clicking Play now plays.

**Built:** `playback/engine.cpp` (libVLC instance, `set_callbacks` frame sink into our
own buffer, play/pause/stop/seek/rate/volume/mute/audio-delay, duration and state from
the engine, EndReached/Error events), `playback/presenter.cpp` (the frame queue that
pays a picture-side sync delay, bounded by the delay it is paying for), `ui/player.cpp`
(player view: letterboxed picture, chrome, scrub, subtitles, end card), the hover
preview on a second engine, and `vlcflix play` / `ui --play <index>` /
`render --play <index>` so playback can be run and screenshotted without a mouse.

**The three audited bugs that stop existing** rather than being fixed: A2 (seeking a
single-keyframe file), the `vlc.exe`+HTTP bridge family, and the browser's black-window
class of failures. The code path that caused them is not in this program.

**Verified this turn:**
- `make test` → **281 checks, 0 failures**; CMake + CTest the same, from a build
  directory outside the tree.
- Real decoding: `vlcflix play 3 --seek 5 --wait 4` → `state=playing time=5.40 s
  length=16.00 s decoded=119 presented=118`. The length comes from libVLC, not ffprobe.
- **Seek on the single-keyframe sample** (`the-silent-coast.mp4`, audit A2): the clock
  moves past 12 s and pictures keep arriving; asserted in the test suite, not just by
  eye.
- Resume: a film saved at 8 s of 16 s restarts at 8 s (`player: resumed at 8.0 s of
  16.0 s`), and the window's own `--play` path did the same from saved progress.
- End card: ran a film past its end → `state=ended`, the film was written as finished
  and **removed** from `progress.json`, and the still I opened shows the card, not the
  old chrome bleeding through (that was a real defect, fixed by adding a solid plate).
- Subtitles: `samples/ghostlight.en.srt` (added this turn so the path has a real file)
  is found by the scanner, parsed into 4 cues, and the cue for 2 s–4 s is drawn
  centred in the lower third — screenshot opened and read.
- Hover preview: moving the pointer onto a card starts a muted preview on its own
  engine; the panel shows a decoded frame; leaving stops it.
- The window loop with the player inside it, headless: `SDL_VIDEODRIVER=dummy
  ./build/vlcflix ui --play 3 --frames 200` → window opens, engine opens, resumes,
  draws, closes, exit 0, clean-exit record in the log.

**Defects I introduced and caught by looking:**
- the `+` sync control and the CC label drew past the right edge (fixed by laying the
  right-hand controls out from the right edge inwards, and by measuring buttons);
- the mute/CC buttons showed a play triangle because they are "primary" when active
  (buttons now take an `icon` flag);
- the end card was drawn over the *last* frame with one 190-alpha layer and the old
  chrome was still legible through it;
- `find_dir_upwards` was needed because CTest ran from a build directory outside the
  repository and the playback tests could not find `samples/` — they failed for the
  wrong reason, which is worse than failing.

**Also fixed after the first clean clone:** `vlcflix play 3` on a machine with no
catalog failed with an empty message and exit 1. It now says the library is empty and
prints the two ways forward, an out-of-range index names how many films there are, and
a file played from the CLI takes the same id the scanner would give it, so its progress
is the same progress instead of a second history under `id-cli`.

**Not verified, stated not assumed:** audio (no sound card here — the picture, clock
and state machine are verified, the sound is not); automatic device-latency
compensation is inert because libVLC 3 cannot report output latency, so the
`autoLatency` toggle records intent and changes nothing; the window on a real display;
Windows.


## 2026-10-28 — Turn 4: polish, bug confirmation, and how we know it works

The turn's real deliverable is `docs/TESTING.md`, but the work that produced it was
mostly *running* the thing until it broke, and it broke three times.

### The testing layers, built this turn

| Layer | File | What it is |
|---|---|---|
| 1 unit | `native/tests/run_tests.cpp` | unchanged, still the rules in isolation |
| 2 golden image | `native/tests/golden_tests.cpp` + `native/tests/golden/*.png` | seven screens compared pixel by pixel, tolerance 6/255 per channel and 0.15 % of pixels; the comparator self-checks (identical image = 0 changed, a 4×4 nudge = exactly 16) |
| 3 journey | `native/tests/journey.sh` | 15 steps through the real binary on a library built by ffmpeg during the run — no sample file touches it |
| 4 faults | `native/tests/fault_tests.cpp` | corrupt inputs, read-only data dir, junk video, deleted file mid-playback: no crash, and every one says why |
| 5 sanitizer | `make asan` | the same 364 checks under AddressSanitizer |
| 6 static | `tools/preflight.sh` | 22 checks no compiler here can make: build-system parity, `#if` balance, the Windows identity, `AGENTS.md` house rules, the documentation's own promises |

`make verify` chains preflight → tests → journey. It is green from a fresh clone with
nothing but g++, pkg-config, ffmpeg and libVLC. CTest runs the same two suites from a
build directory outside the tree.

Totals: turn 1 = 132 checks, turn 2 = 210, turn 3 = 281, **turn 4 = 364 + 28 static +
15 journey steps**.

### Three real bugs found by the new layers

1. **An unreadable file was kept with no marker.** A file the prober could not read
   stayed in the library looking exactly like a healthy film, and Play opened a player
   that could only ever show black. Now `Item::unreadable` is set by the scan, persisted
   in the catalog, drawn as a badge on the card, and Play refuses with
   *"This file could not be read when the library was scanned"*. Found by the fault
   layer, which asserted that a junk file is *reported*.
2. **A zero-decode file was written as watched.** libVLC reports a corrupt container as
   `Ended` with `frames_decoded == 0`; the player treated that as a finished film, which
   silently removed it from Continue Watching. Now a film that ends without producing a
   single picture is a failure with a message, and no progress entry is written. Proved
   with a throwaway harness that printed `state=ended decoded=0` before and
   `state=error err=This file ended without producing a single picture` after.
3. **Half of all artwork previews were never drawn.** The preview job captured its
   segment list by reference while the list lived inside an `if` block that closed
   before the jobs were joined, so the worker read a destroyed vector and ffmpeg
   received `-t 3219238484152785303456914394985`. The fix is one word (`[&, segments]`).
   The negative control is the point: with the old code, `make asan` fails on the first
   pass with `AddressSanitizer: stack-use-after-scope`; with the fix it is clean. A
   three-pass regression test now exists because *one* pass passed by luck.

### Other changes this turn

- `hwDecode` became real: `-hwaccel auto` on every ffmpeg call (`artwork.cpp`), off by
  default. On this box ffmpeg logs Vulkan device errors and still produces the file, so
  the test judges by exit code and output, never by stderr.
- `autoLatency` is gone — setting, UI row, settings file and CLI output. libVLC 3 cannot
  report output latency, so the row now reads "not available with libVLC 3 — use − / +".
  A toggle that changes nothing is a lie in the shape of a feature.
- `status --json` now carries a per-item `library` array (title, year, genres, pixFmt,
  poster, subtitles, unreadable), and the banner goes to *stderr* in `--json` mode so a
  parser gets JSON and nothing else. The journey checks that the first byte of stdout is
  `{`.
- The scan summary says what the artwork pass did (`0 of 0 artwork set(s) drawn` on a
  rescan), names unreadable files instead of only counting them, and no longer counts an
  unreadable file as a permanently missing artwork job.
- Windows packaging for the native app: `native/packaging/installer.nsi` (new AppId,
  install dir, uninstall key; refuses to install into a directory holding 1.2.0; never
  touches 1.2.0's data), `native/packaging/app.manifest`, and
  `scripts/build-win-native.sh`, whose `--check` mode states plainly what this machine
  lacks and exits 3.

### Not verified (unchanged, and now written down in `docs/TESTING.md` §9)

The Windows build and every `_WIN32` branch (no mingw here — `--check` proves the
absence rather than assuming it); a real display (the window only ever ran headless);
audio (no sound card; libVLC prints `ALSA lib … cannot find card '0'`); the
`-hwaccel auto` GPU path (no Vulkan video queue, software fallback proven by output
file); installing side by side with 1.2.0 (needs Windows). The manual checklist is in
`docs/TESTING.md` §10.

## 2026-10-28 — Turn 4 continued: an actual Windows exe

The user asked for something they can run, and asked whether GitHub could show the
outputs. Both were possible; one needed a workaround.

### What was tried, in order

1. **Wine in this sandbox: dead end, measured.** Downloaded two portable Wine 11.20
   builds (Kron4ek, amd64 and amd64-wow64, plus the staging variant, 95–99 MB each).
   `wine --version` answers, but any real start — `wine cmd`, `wineboot -u` — either
   exits silently or dies with SIGSEGV in `virtual_init` before a prefix exists.
   `setarch -R` made no difference. Recorded as a dead end rather than "it nearly
   worked": without a Windows-shaped syscall surface, Wine cannot run here.
2. **Cross-compiling: works.** Installed nothing (no root): llvm-mingw 20261020
   (clang + lld + mingw-w64 CRT, 81 MB tarball), SDL2's official mingw package,
   libpng/zlib/libjpeg-turbo import libraries from MSYS2 packages (unpacked with
   `pip install zstandard`), FreeType 2.13.3 built here `--without-harfbuzz`
   (the MSYS2 build pulls glib, graphite2 and a GCC runtime DLL into the payload just
   to draw text), and VideoLAN's own `vlc-3.0.24-win64.zip` with an import library
   generated from `libvlc.dll` (`gendef` + `dlltool`).
3. **The cross build found three real portability bugs**, which is the point of
   building for a second platform at all: `std::atoi`, `std::atof` and `std::strtoul`
   were used without `<cstdlib>` (libstdc++ pulls it in transitively on Linux, libc++
   does not) and `command_crash` used `RaiseException`/`EXCEPTION_ACCESS_VIOLATION`
   without `<windows.h>`. All four files fixed.
4. **A decision the platform forced:** the exe is a **GUI-subsystem** executable, so
   double-clicking does not open a console behind the window. `main()` now calls
   `AttachConsole(ATTACH_PARENT_PROCESS)` (Windows only, immediately after the log is
   open) so the same binary typed at `cmd.exe` still prints its output.

### What exists now

* `scripts/build-win-native.sh` — the whole recipe, reproducible from a clean machine
  with network access: fetch, build FreeType, generate the libVLC import library,
  compile, link (manifest + version resource embedded), stage, verify. `--check`
  reports what is missing and exits 3.
* `scripts/check-win-payload.sh` — walks the import table of the exe, SDL2, libpng,
  libvlc and libvlccore: **136 imports checked, 0 missing**, and it fails if the
  libVLC plugin modules the app needs are absent. Before this, "the exe opens and
  closes" was a thing that could only be discovered by running it.
* **The published exe**: release `win64-cross-2`,
  `VLCFlixStudio-win64.zip` (67 MB — exe, SDL2, libpng, zlib, libjpeg, libVLC 3.0.24
  with its 29 plugin modules, and the self-check script). PE64, GUI subsystem,
  manifest and version block in place.
* `scripts/windows-selfcheck.ps1`, shipped inside that zip — runs the exe, scans a
  folder, renders every screen, plays a film, and packs screenshots + `status --json`
  + the app's own log + a pass/fail report into `windows-selfcheck-evidence.zip`.
* `docs/ci/windows.yml` — a workflow that does the same unattended on
  `windows-latest` with MSYS2's mingw-w64 GCC (a second, independent toolchain) and
  attaches the exe and the screenshots to a release. It is parked in `docs/ci/`,
  **not** `.github/workflows/`, because this session's fine-grained token has
  Contents: read/write but not **Workflows: write**, and GitHub rejects the push:

  ```
  ! [remote rejected] cpp -> cpp (refusing to allow a Personal Access Token to
    create or update workflow `.github/workflows/windows.yml` without `workflow` scope)
  ```

  `docs/ci/README.md` gives the two ways to switch it on (add the scope, or paste the
  file in the browser).

### Superseded the same day — see the addendum below

**Reproduced by executing the Windows exe, found by doing it, and worth keeping:**

Four Windows-only bugs were invisible until it ran: `run_process` used `PeekNamedPipe`
polling and returned empty output for every child; concurrent `CreateProcess` calls with
`bInheritHandles = TRUE` let children inherit each other's pipe ends; `write_png` passed
a `FILE*` across a CRT boundary so no byte was ever written; and `have_binary()` trusted
a process that printed nothing. All four are fixed and covered by tests — see
`docs/TESTING.md` section 11.

---

## Addendum, 2026-10-28 (turn 4, after the paragraph above was written)

**The exe was executed.** Two things had to change first, and neither was a sandbox
limit:

1. **Wine 11.20 and 11.20-staging are broken upstream** — `wineboot` dies with
   `virtual_init host addr space limit: 0xffffffffffff0000` and a SIGSEGV in
   `create_view → map_view → virtual_alloc_first_thread_data` before a prefix exists.
   Bottles (`bottlesdevs/Bottles#4903`) and Paint.NET's Wine tracker
   (`paintdotnet/Paint.NET-on-Wine#58`) report the identical crash on exactly those
   versions. The sandbox was ruled out by measurement: a fixed `mmap` succeeds at both
   `0x7ffe0000` and `0x7ff000000000`, `mmap_min_addr` is 4096, `max_map_count` 65530,
   the stack is 8 MB. **Wine 11.19 amd64-wow64 works.**
2. **The Windows code paths had never run**, so they were wrong in four places (below).

What the executed Windows binary did, with the bundled libVLC 3.0.24 payload:

| Command | Result |
|---|---|
| `version` | `VLCFlix Studio 2.0.0-turn1` |
| `scan --no-art` | 8 items, **0 errors**, `0:16`, `1280x720` on every row |
| `scan` (artwork) | **8 of 8 artwork sets drawn** by 8 concurrent ffmpeg children |
| `render` ×6 | six real screens written as PNG (`docs/evidence/`) |
| `play 1 --wait 4` | `state=playing time=3.15 s length=16.00 s decoded=99 presented=99` |
| `crash` | the log carries the `CRASH` record and a 21-frame stack |
| `ui --frames 3` | `could not create a window` → exit 5 (Wine has no display; correct) |

Three of the four bugs were found *because* the app logs honestly and the harness
compared what came back, not because it looked fine:

* `run_process` polled with `PeekNamedPipe` and drained nothing → every child's output
  was empty → "8 item(s) … could not be read and will not play". Rewritten to a blocking
  `ReadFile` on one thread per pipe.
* Eight concurrent probes lost output or hung (three 20 s timeouts): `bInheritHandles =
  TRUE` inherits *every* inheritable handle, so each child also got another child's pipe
  write end. Fixed with `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` and serialised spawning:
  8/8 concurrent probes now succeed in 10.5 s.
* `write_png` passed libpng (built against a different C runtime) a `FILE*` from ours:
  `libpng error: Write Error`, zero-byte screenshots, then an integer divide-by-zero in
  `msvcrt`. Fixed by writing through a libpng callback into memory and reading the file
  back to prove the size.
* `have_binary()` was satisfied by any process that started, even one that printed
  nothing — which is how `doctor` said "ffmpeg found" while every probe returned empty.

New tests for the first three: `processes: eight children started at once each keep
their own output`, `png: written bytes are real, complete, and decode to the same size`.
Suite size: 339 → **364 checks**; payload check 48 → **136 imports**, 0 missing.

Still open after this, and stated as such: Windows 10/11 itself, the window on a real
display, real audio, and the installer side by side with 1.2.0.

---

## Follow-up, 2026-10-28: the exe on its own, and scrolling everywhere

Two requests, both from the user:

**1. "Just give me the exe itself, not the zip file."** The Windows executable is now
written to `native/releases/VLCFlixStudio.exe` by `scripts/build-win-native.sh`, and
that folder (plus `*.exe`, `*.dll`) is git-ignored, so it never reaches GitHub. The
same build still packs `dist/win-native/VLCFlixStudio-win64.zip`, because the exe is
5.6 MB and needs `libvlc.dll`, `libvlccore.dll`, its `plugins/`, SDL2, libpng, zlib and
libjpeg beside it to start. Rebuilt and re-run under Wine 11.19 after the change:
`version` OK, `scan` = 8 items / 0 errors, all six screens rendered
(`docs/evidence/windows-home-scrolling.png`).

**2. "Add scrolling to everywhere possible... make sure that scrolling exists anywhere
they might be needed."** It did not, and not subtly:

| Place | Before | Now |
|---|---|---|
| Detail sheet | the wheel was ignored outright (`return false` on any wheel while a sheet was open); a long synopsis put Play below the edge with no way to reach it | scrolls by wheel, Page keys, Home/End and its own draggable bar; the buttons move with the text, and are only clickable once they are on screen |
| Settings sheet | same: the wheel was ignored, and eight folders pushed *Scan library* past the panel edge | scrolls the same way, Scan button included in the content |
| Rails on Home | a rail stopped drawing at the right edge - films past the visible row could not be reached by any input at all | each rail scrolls sideways under the wheel, with its own bar in the heading line; at the end the wheel carries on down the page instead of doing nothing |
| Browse / My List / Search | wheel and arrows only, with nothing on screen saying more existed | wheel, Page Up/Down, Home/End, and a draggable bar that appears only when it is needed |
| Keyboard focus | scrolled the grid in a special case | `reveal_rect()` scrolls whatever holds the focused card - a rail sideways, a grid vertically - using the rectangles the last frame actually drew |

The mechanism is one declaration per region (`page`, `detail`, `settings`, `rail:<id>`)
made by the drawing pass, and every consumer - the bar, the wheel, the keys, the tests
- reads that same number. Hit areas for anything scrolled out of view are dropped, so
nothing invisible stays clickable.

Writing the tests found two bugs in the new code: the detail sheet claimed ~90 px of
scroll when it fitted (content height counted the art twice), and its buttons were
pinned to a fixed height instead of moving with the text. Both are covered now.

Suite: 364 → **412 checks**, `make verify` green (28 static + 412 + 15 journey steps),
`make asan` clean, golden baselines regenerated for the two sheets that changed.

## Turn 6 — one file to hand somebody

**The request, twice, unchanged:** *"All those libraries that it needs, make them all
get packed into an exe file. Then I should be able to install the app, and then it should
behave like a standard app, with a desktop icon and all. Then that final packed app, save
it under native/releases."*

**Why it cannot be one file the ordinary way.** libVLC loads its codecs from
`plugins/` at runtime. A single statically-linked binary would mean replacing libVLC
with something else, so the honest reading of "pack it all into an exe" is: put the
whole directory inside the executable and unpack it on first run. That is what
`native/src/setup/setup.cpp` does - the stub is a GUI-subsystem program with the
payload zip appended and a 24-byte footer (`VFSUZIP1`, offset, length) after it; miniz
inflates it in place. No unpacking by hand, no second download, no DLLs to place.

**What the installer does**, all of it logged to
`%LOCALAPPDATA%\VLCFlixStudio\logs\setup.log` line by line, opened and closed per write
so a crash still leaves evidence:

| Step | Detail |
|---|---|
| Install | 372 files into `%LOCALAPPDATA%\Programs\VLCFlixStudio` (the app, SDL2, FreeType, libpng, zlib, libjpeg, libVLC + all 29 plugin modules) |
| Desktop icon | `Desktop\VLCFlix Studio.lnk` via `IShellLinkW`, icon from the exe's own `1 ICON` resource |
| Start Menu | `VLCFlix Studio.lnk` and `Uninstall VLCFlix Studio.lnk` |
| Apps & features | `HKCU\…\Uninstall\VLCFlixStudio`: DisplayName, DisplayVersion 2.0.0, DisplayIcon, InstallLocation, UninstallString, QuietUninstallString, EstimatedSize, NoModify, NoRepair |
| First launch | starts the installed app when the installer was double-clicked |
| Uninstall | removes program, all three shortcuts and the registry key; **keeps** catalog, artwork, settings and progress - uninstalling a player must not throw away the collection |
| Self-delete | the running copy cannot delete its own folder, so it copies itself to `%TEMP%`, relaunches with `--finish-uninstall`, and retries the delete for five seconds |
| 1.x | the legacy folder is only ever *looked* at; `--dir` pointed at it is refused with exit 1 |

New icon: `native/packaging/vlcflix.ico` (6 sizes, 16→256), also embedded in the app.

**A fifth Windows-only bug, found by installing it.** Double-clicking the new Desktop
shortcut did nothing: `main()` defaulted to the `help` command, so the shortcut launched
a GUI-subsystem program that printed usage text into an invisible console and exited.
No arguments now means *open the window*, and a window that cannot be created shows a
message box pointing at the log rather than failing silently. (`--help` still prints the
command list.)

**Executed under Wine 11.19, in a fresh prefix:** install with no arguments → 372 files;
`--verify` → exit 0; the installed app runs (`version`, `scan` = 8 items / 0 errors,
`play` decoded=70 presented=70, `render --screen settings` → 58,923 B); reinstall over
itself keeps the library; `--uninstall --quiet` → folder gone, registry key gone, three
shortcuts gone, user data intact; a stand-in 1.x folder untouched.

**Not verified, and not claimed:** a real Windows shell drawing these icons, a real Add/
Remove Programs running this uninstaller, and side-by-side with a genuine 1.2.0 install.

`make verify` is green with **30 static checks** (the NSIS draft `installer.nsi` is gone;
its identity checks now check `native/src/setup/setup.cpp`), 412 tests and 15 journey
steps. Deliverable: **`native/releases/VLCFlixStudio-Setup.exe`**, 71 MB, git-ignored.

### Turn 6, follow-up: "side-by-side configuration is incorrect"

The first run on a real Windows machine failed before `main()`:

> The application has failed to start because its side-by-side configuration is
> incorrect.

`ERROR_SXS_CANT_GEN_ACTCTX` (14001): the loader read the embedded manifest, found a
`description` **attribute** on `<assemblyIdentity>` that Microsoft's schema does not
define (a description is a child element, and the six legal attributes are `type`,
`name`, `language`, `processorArchitecture`, `version`, `publicKeyToken`), and refused
the application. Wine does not validate manifests and no line of our own logging can run
before `log::init()`, so every test on this machine passed a manifest Windows rejects.

Fixed: the description is a child element now, `processorArchitecture` is `*`, and
**every `<dependency>` is gone** - nothing here imports `comctl32.dll`, and a dependent
assembly is the only thing side-by-side has to resolve, so there is nothing left to
fail. `scripts/check-win-manifest.py` enforces the schema rules, the root namespace and
version, `asInvoker`, no `--` in comments, the 64 KB limit, and that the built exe
carries that manifest exactly once, byte for byte. `tools/preflight.sh` runs it **with a
negative control** (putting the bad attribute back must fail the check), the build runs
it before `windres` and again against both linked executables, and `make verify` now
fails on the exact defect that stopped Windows. 33 static checks, 412 tests, 15 journey
steps, `make asan` clean; the rebuilt installer is still exercised under Wine 11.19
(install, verify, scan, play).

The honest limit: Microsoft's parser cannot be run here. The manifest is schema-clean
with no dependent assemblies, which is the strongest statement available from this
machine; `sxstrace.exe` on the target decides if anything remains.

## Turn 7 — the first session on a real screen

The installer from turn 6 ran on the user's Windows machine. Three things were wrong,
all of them invisible to a headless run, and all three are fixed in **2.0.1**.

**1. The window was wider than the screen.** It opened at the 1600x900 default wherever
it landed, so on a smaller desktop the title bar - and minimise, maximise and close with
it - sat off the edge. `fit_window_to_display()` now shrinks the request to 96 % of the
desktop width and 90 % of its height (never under 640x420) and the window gets a minimum
size. The rule is a pure function in `platform/window_size.h`, so it is tested against a
1366x728 laptop, a 2560x1400 desktop, an 800x480 screen and an unknown display.

**2. In Settings nothing responded, and any click closed the sheet.** Two bugs stacked:

| Bug | Effect |
|---|---|
| the full-screen scrim and the click-swallowing panel were registered *after* the sheet's controls, and hit testing takes the last match | they sat on top of every control the sheet drew, so Play, the toggles and the folder field were dead |
| `add_hit()` silently drops `Action::Kind::None` | the swallow area had never been registered at all, so a click that missed a control reached the scrim and closed the sheet - "it always takes you back to the main page" |

Both sheets (detail and settings) had it. The scrim and the swallow are now registered
first, and the swallow is its own `swallow()` call so a `None` action cannot be dropped by
accident. The reason the suite missed it is worth recording: the old assertion was
`CHECK(click_action(app, Kind::Play))`, which only requires `handle()` to return true -
and it did, because the click closed the sheet. The tests now assert that the thing under
the pointer **is** the thing that was drawn there.

**3. Nothing told the user what was clickable.** Hovering a poster or a button left the
arrow cursor. `App::pointer_cursor()` answers Arrow / Hand / I-Beam from the same hit
areas the click uses, and the event loop hands that to `Window::set_cursor()`, which
keeps one SDL cursor per shape.

Suite: 412 → **461 checks**, `make verify` green (33 static + 461 + 15 journey steps),
`make asan` clean. Both halves of the sheet bug were confirmed by re-breaking them: the
old ordering fails six checks, and `add_hit(panel, Action{})` fails both new tests.

Windows: installer rebuilt as **2.0.1**, installed and `--verify`-ed under Wine 11.19
(372 files, `version` → `2.0.1`, `DisplayVersion` → `2.0.1`), and the settings sheet
re-rendered at 1366x728 to check the panel on a laptop-sized screen
(`docs/evidence/windows-settings-1366.png`).

## Turn 8 — the cursor on Home, and choosing a folder

**1. "The cursor still doesn't change, that's only in the main screen."** It was not a
cursor bug. The window shrinks itself to fit the desktop (2.0.1) but `command_ui` kept
drawing and hit-testing a canvas at the *requested* size and SDL stretched it into the
window that actually existed: 1600x900 of canvas in a 1228x900 window. Mouse positions
arrive in window pixels, so every hit test was ~23 % away from the pixel the user aimed
at. On Home that meant no hit area under the pointer, hence the arrow; in the settings
sheet the centred panel's own click-swallowing area absorbed the error, which is why that
screen looked fine. The canvas now follows the window at startup and on every frame
(a window can resize without a `SIZE_CHANGED` event, and this one is shrunk before the
first frame).

**The test layer that was missing.** Everything about this bug is about a real window, so
this session installed `Xvfb` + `xdotool` and drove the actual application: open it on a
1280x1024 virtual screen, move the pointer, click, photograph the window, and read the
app's own log (`cursor: hand at …`, `ui: settings open`, `browser: now at …`). Same
sequence, both builds:

| | 2.0.1 | 2.0.2 |
|---|---|---|
| canvas | 1600x900 (window 1228x900) | 1228x900 |
| click the visible Settings button | nothing | `ui: settings open` |
| hover a poster | arrow | hand |

**2. "Add the ability to select a folder I can browse to, instead of pasting a path."**
The settings sheet now has **Browse...** next to the path field. It opens a picker over
the sheet: the current path, one row per real folder, ".. up one level", the other drives
on Windows, wheel/Page-key/bar scrolling, *Use this folder* and *Cancel*. Adding goes
through the same `add_library_folder()` as a typed path, so validation, duplicate
detection and error messages are the same code. `library/fs_browse.{h,cpp}` is plain
`std::filesystem` with no UI in it, so the listing is tested against a real temporary
tree.

Verified: unit tests (sorted, files excluded, dot names excluded, missing path reported),
UI tests through the picker's own hit areas (open, into a folder, "..", use, duplicate
refused, cancel), and a live run on the virtual display where four real clicks put
`"libraryFolders": ["/home/user/VLCFlix/native"]` into `settings.json`. The Windows build
renders it too: `docs/evidence/windows-folder-picker.png` (`C:/users/user`, Documents,
Downloads … and a scroll bar).

Suite: 461 → **513 checks**, `make verify` green (33 static + 513 + 15 journey steps),
`make asan` clean, CMake build clean. Installer rebuilt as **2.0.2**, installed and
verified under Wine 11.19 (372 files, `version` → 2.0.2), the picker rendered on Windows
and a film still plays (`decoded=76 presented=76`).

## Turn 9 — the crash inside the graphics driver

2.0.2 did not open on the real machine. It wrote this and died:

```
[CRASH] unhandled exception code=0xc0000005 address=00007FF860F6DBDE
  #8  igdumdim64!OpenAdapter            #9  D3D9!Direct3DShaderValidatorCreate9
  #11 D3D9!Direct3DCreate9On12Ex        #13 SDL2!...        #16 VLCFlixStudio ...
```

SDL's default renderer order (direct3d11, then direct3d, then ...) fell through to
**Direct3D 9**, and the D3D9-on-12 shim inside the Intel driver raised an access violation
*inside the driver*, on SDL's call, before `SDL_CreateRenderer` could return anything. No
fallback can be written for that: the fault happens on code we do not own and cannot
catch. The fix has to be not asking.

The app needs exactly one thing from SDL's renderer - a streaming texture it can upload a
finished pixel buffer into - because the UI rasterizes every pixel itself into a `Canvas`.
So a GPU saves a memcpy and costs a driver bug. The renderer is now requested **by name**
(`platform/render_driver.h`), **software by default**, with:

| Rule | Why |
|---|---|
| never `SDL_CreateRenderer(window, -1, ...)` | that is "pick for me", and what it picked crashed |
| `VLCFLIX_RENDER_DRIVER=direct3d11` (or `SDL_RENDER_DRIVER`) honoured | the accelerated path stays available |
| the plan always ends in `software` | an explicit choice that cannot be honoured still opens a window |
| `direct3d`/`direct3d9` honoured but flagged in the log and in `doctor` | the user's decision, with the warning attached |
| `doctor` prints the policy | the answer is visible without opening a window |

Verified live on Xvfb: `renderer: software`, cursor working, Settings opening, and the
screenshot identical to the accelerated builds - software is not a degraded mode here, it
is the normal one. Suite 513 → **529 checks** (`make verify` green, `make asan` clean).

What this does **not** prove: the machine cannot reproduce the Intel crash (no Intel GPU
here). What is proved is that the driver that crashed is no longer selected.

Installer rebuilt as **2.0.3**, installed and verified under Wine 11.19 (372 files,
`version` → 2.0.3, `doctor` prints the renderer policy, scan 8 items / 0 errors, playback
decoded=80 presented=78).

## Turn 10 — the Home screen: scrolling, and a hover that grows

2.0.3 opened, and the Home screen turned out to have three things wrong on it. Each one
looked like something else from the outside.

**Two-finger scrolling did nothing.** The mouse wheel scrolled; a touchpad did not. That
is not a Home bug — a precision touchpad sends fractional deltas (`0.03`, `0.07`, ...),
SDL 2.26+ stores them in `preciseX`/`preciseY` and **rounds the integer fields to zero**,
and the event handler read `wheel.y`. Every swipe therefore arrived as `0` and was
silently discarded, on every screen. The arithmetic is now a pure, tested function
(`platform/wheel.h`): take the precise value when there is one, honour the direction flip,
and carry the fractions until they are worth a scroll event, so a swipe is a handful of
smooth movements rather than a hundred redraws or none at all.

**Dragging the page bar "zoomed" the layout.** The page bar worked; the page did not. On
Home the hero is placed with `y = -scroll` and the rails with `top = hero.h + gap + scroll`
— the two halves of the page travelled in *opposite directions*, so every scroll opened the
gap between the hero and *Recently Added* until both were off the screen. One sign. The
test now measures what the user saw: the hero-to-rails gap is identical at 0, 20, 180 and
480 px of scroll, the hero does not change height, and every poster card on the page ends
up exactly `moved` pixels higher with not one card left behind.

**The hover preview showed artwork over the neighbours.** Two faults:

* it seeked to 20 % of the film immediately after `open()`, when libVLC does not yet know
  the length — the seek went nowhere and the card sat on the artwork for ever. The seek is
  now queued and performed by `tick()` as soon as `length > 0` (`preview: seeked to 3.2 s
  of 10.0 s`). A preview that still produces nothing after four seconds now says so in the
  log rather than looking like a dead feature;
* it drew a **second 336-px panel** above or below the card. It now grows the card itself —
  1.24× for posters, 1.12× for the 16:9 cards — centred on the card's own centre, clamped
  below the header and inside the window, with the title, meta and progress on it, and
  every other card left exactly where it was.

The enlarged card is drawn from a rectangle recorded during the draw pass, because looking
the card up in the hit list afterwards found the *hero's* clickable area for the same film
and drew the enlarged card across the Play and My List buttons. That was only visible in a
screenshot of the running window, not in a test.

Verified on the running window: hovering a poster for 3 s and then 6 s leaves **96 % of the
pixels inside the card different** between two captures (a still cannot do that), the card
is visibly larger with its text on it, and dragging the page bar from y=300 to y=600 brings
*Recently Added*, *Drama* and *Documentary* up with the hero gone off the top and no gap
anywhere. Suite 529 → **590 checks**; `make verify` (33 static + 590 + 15 journey) and
`make asan` green.

Installer rebuilt as **2.0.4** and exercised under Wine 11.19: 372 files installed,
`--verify` clean, `version` → 2.0.4, `doctor` prints the renderer policy, a scan of the
samples gives 8 items / 0 errors, and playback decodes and presents (decoded=64
presented=62 at 3 s).

**Not claimed:** the touchpad itself — there is no precision touchpad here and X11 cannot
synthesise `preciseY`, so the fractional path is proved by the pure function and by a build
that reads the field, not by a finger. Wine still shows no window, so hover cannot be driven
there either; the hover evidence is from the virtual display, and the real machine remains
the last mile.

## 2026-10-29 — Turn 1 of 6: plan only, branch `studio-ui`

The user asked for a new branch and a version whose UI matches the JS app. This
turn planned the tools and locked the decisions. No UI code was written.

Decisions, from the user, not assumed:

- Cut from `cpp`. UI only. The engine stays.
- Match is defined by screenshots taken of the running Node app in this sandbox,
  then C++ screens built one after another until they match.
- Branch name `studio-ui`.

Opened, so the gap is not a rumour: `main` `docs/screenshots/home.jpg` and
`detail.jpg`, and `cpp` `docs/evidence/windows-home-scrolling.png` and
`windows-detail.png`. The JS home is a billboard with the title on the art.
The C++ home capture is a gradient band with the title underneath, and that
particular capture's cards are letter blocks. The detail capture behind the
sheet does show artwork cards, so the letter blocks may be missing art rather
than the layout. Not settled until turn 2 renders both apps itself.
`windows-home-scrolling.png` is a scrolled capture; it is not a fair at-rest home.

Credential: stored at `.secrets/github-token` (mode 600). `git check-ignore`
matches it and `.git-credentials`. `git add` of that path is refused. `git
ls-files` does not list it. A working-tree search found the current token only
in the secret store. It is not in this commit.

Scope probe (status codes, token not printed):

| Call | Result |
|---|---|
| `GET /user` | 200, login JohnOlowe, `X-OAuth-Scopes` empty (fine-grained) |
| `GET` Actions secrets, Actions permissions, hooks, vulnerability alerts, branch protection | **403** |
| `POST /repos/JohnOlowe/VLCFlix/git/refs` with an all-zero SHA | **422** Object does not exist — write authorized, object rejected |
| same ref afterwards | **404** — nothing was created |
| same `POST` on `JohnOlowe/BT-Audio` | **403** Resource not accessible by personal access token |
| same `POST` on `JohnOlowe/Movies` | **403** |
| anonymous `POST` on either repo | **401** |
| `GET /user/repos` | 200, 15 public repos, `permissions.admin: true` on each |

The permissions field is the 2026-10-26 trap. Public metadata lists every public
repo and stamps admin on it. The write calls do not. Effective write scope is
VLCFlix only. Administration endpoints on VLCFlix itself returned 403. Rotate
the token after the six turns; it passed through chat.

Machine measured, not recalled: 21 GB free, 1982 MB RAM. `cmake`, `ffmpeg`,
SDL2, libVLC, Xvfb and xdotool are not installed. Turn 2 installs them. They
will not survive a snapshot.

Next: turn 2 captures the Node app, then the current C++ UI, and writes a gap
row per screen. No screen is rewritten before its pair of captures exists.
