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
