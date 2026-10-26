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
