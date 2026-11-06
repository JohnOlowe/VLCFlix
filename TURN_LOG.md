# Turn log — android-port

## 2026-11-01 — Branch created, extension identified, port analysis written

- Cloned `JohnOlowe/VLCFlix` and `JohnOlowe/BuildTools` fresh. VLCFlix remote
  heads at time of clone: `main d4606a7`, `dev 2ef340f`, `cpp 5b86706`,
  `studio-ui 50fa816`.
- Read **every MD file on every branch**: `AGENTS.md`, `CONTEXT.md`,
  `README.md`, `TURN_LOG.md` (main/dev/cpp/studio-ui), `docs/ROADMAP.md`,
  `docs/TESTING.md`, `docs/preview-generation.md`, `docs/ci/README.md` (dev),
  `docs/AUDIT.md`, `docs/PLAN_4_TURNS.md`, `docs/PORT_PLAN.md`,
  `native/README.md` (cpp), `docs/PLAN_6_TURNS.md`, `docs/captures/GAP.md`,
  `preview-generation-guide.md` (studio-ui). Findings: the rules are the
  `AGENTS.md` that is byte-identical on `main`/`cpp`/`studio-ui` (13 sections
  + pre-flight); the `dev` file named `AGENTS.md` is a different, small
  "Development notes" file (checked by diff, not assumed). All other MD files
  are project history for the VLCFlix Windows/C++ work and contain no rules
  for this branch.
- Extracted `10.6.1.2_0.7z` (installed `py7zr`; no 7z binary in the sandbox).
  Verified by reading `manifest.json` and listing the tree: **Video
  DownloadHelper Pro 10.6.1.2**, Manifest V3, service-worker background,
  `offscreen` + `sidePanel`, 23 content scripts, per-site injected scripts,
  libav.js wasm download/mux worker. Details in `CONTEXT.md`.
- Web checks performed (results labelled in `docs/ANDROID_PORT_PLAN.md`):
  VDH ships an official add-on for Firefox for Android on AMO; Kiwi Browser
  (MV2+MV3 extension loading on Chromium) is reported archived as of Jan 2025
  with its extension engine continued in Microsoft Edge Canary (community
  sources — flagged as such, not verified on a device).
- Measured the sandbox instead of recalling it: 2 cores, ~2 GB RAM, ~20 GB
  free; JDK 11 present; no adb/gradle/Android SDK.
- Created **orphan branch `android-port`** (no shared files with other
  branches, per user instruction) containing only: `AGENTS.md` (verbatim
  rules copy), `README.md`, `CONTEXT.md`, `TURN_LOG.md`,
  `docs/ANDROID_PORT_PLAN.md` — the last four rewritten to apply only to this
  work. No `checkpoint.json` and no `.gitignore` on this branch, both by user
  instruction ("MD files only"); consequence recorded in `CONTEXT.md`.
- **Not pushed**: no credential exists in this session. Asked the user for a
  fine-grained PAT (VLCFlix-only, Contents: read/write), the route choice,
  branch-name confirmation, and the use scope (personal vs distribution).

## 2026-11-01 — Decisions received, branch pushed as `vdh-android`

- User answered the four questions: **route A+D**; branch renamed
  **`vdh-android`** (rename happened before the first push — the name
  `android-port` never existed on the remote); **personal use** of the user's
  own Pro copy; PAT supplied via the question UI.
- PAT stored at `.secrets/github-token` (mode 600, verified with `stat`;
  untracked — `git status` shows it only as `??`). It passed through chat;
  the user is advised to rotate it when this work concludes. It is never
  printed to files/commits and never placed in a remote URL.
- Token scope **probed, not assumed** (same token, same client; codes
  reported as returned): PUT ref on `JohnOlowe/BuildTools` → **403**; PUT ref
  on `JohnOlowe/VLCFlix` with an invalid SHA → **422** (authorized; rejected
  only for the bad SHA — no ref was created); branch-protection GET →
  **403**; Actions-secrets GET → **403**. Conclusion: Contents write on
  VLCFlix only, no admin.
- Built the side-load package `/home/user/vdh-pro-10.6.1.2-sideload.zip`:
  79 entries, `manifest.json` verified at the zip root, 4,042,921 bytes.
  Workspace file only — **not committed** (this branch is MD-only).
- Docs updated for the decision: `README.md` status, `CONTEXT.md`
  (decisions, credential handling, next steps), `docs/ANDROID_PORT_PLAN.md`
  §6 (decisions + the Firefox clarification: this Chrome archive cannot load
  in Firefox for Android; the Firefox-compatible VDH is the vendor's Gecko
  build on AMO; hosts for *this* archive are Chromium-based — Edge Canary
  first choice).
- First push attempt was blocked by `git diff --cached --check` (blank line
  at EOF in `README.md`); fixed and recommitted — the check did its job.
- **Pushed and verified**: `origin/vdh-android` =
  `0fb098451c9eec10fc5baa2a9e3ed51f1dcfa16f` = local HEAD (compared
  `git ls-remote` with `git rev-parse HEAD`). Branch contains exactly the
  five MD files (`git ls-tree -r HEAD --name-only`).
- **Unverified, awaiting the user's phone**: whether Edge Canary (or Kiwi)
  runs the side-loaded extension, Pro activation there, and the Termux
  merge/transcode glue. Nothing on-device is claimed yet.

## 2026-11-01 — Workspace trimmed below the 128 MB snapshot cap

- User instruction: end turns under 128 MB; delete what is easily restored,
  keep scripts for the fiddly parts.
- Measured before: workspace **266 MB** — `BuildTools` clone ≈ 237 MB
  (126 MB working tree + 111 MB `.git`), VLCFlix 21 MB, side-load zip 3.9 MB.
- Wrote `~/scripts/restore-vdh-workspace.sh` (idempotent): sparse-clones
  BuildTools with `--filter=blob:none --no-cone` fetching **only**
  `10.6.1.2_0.7z`, verifies sha256, extracts with py7zr, rebuilds the
  side-load zip, and re-clones VLCFlix single-branch if absent.
- Tested the script in a throwaway `/tmp/vdh-restore-test` root (AGENTS.md
  §5), twice: first run exposed a real bug — cone-mode sparse checkout
  materializes root-level files, pulling the 117 MB of tools; fixed with
  `--no-cone`. Second run: restore in 2.6 s, BuildTools dir 19 MB, sha256
  `OK`, rebuilt zip byte-identical in content (all 79 entry CRCs equal to
  the original). Test root deleted afterwards.
- Deleted `~/BuildTools` (clone + extraction). Measured after: workspace
  **30 MB** total (`.cache` 5.5 MB is snapshot-excluded anyway).
- Updated `CONTEXT.md` (assets now on-demand, size-discipline section,
  RECOVERY points at the script).
- **Not verified on device still**: Edge Canary/Kiwi running the extension,
  Termux glue — unchanged from the previous entry.

## 2026-11-02 — Edge Canary takes CRX only; extension re-packed as CRX3

- User reported on their phone: Edge Canary offers **no zip/unpacked
  option**, only CRX. Web check confirmed: the supported path is Settings →
  About Microsoft Edge → tap build number 5× → Developer Options →
  "Extension install by crx" (community-documented since 2024).
- Restored the extension with `scripts/restore-vdh-workspace.sh` (sha256
  `OK`), then packed a **CRX3** with the `crx3` npm package (v3 format,
  Chrome 64+): first attempt used a guessed JS API and failed (`Received an
  instance of Object`) — read the package README and used its CLI instead.
- Key handling: generated a persistent RSA key at `~/vdh-crx-key.pem` (mode
  600, workspace file, not a service credential) so re-packs keep extension
  ID `gmppgmnjccgklfklpibfkejdjijlnlnd`.
- Verified the CRX by parsing it: magic `Cr24`, version 3, 581-byte signed
  header (sha256-with-RSA proof present); ZIP payload = 79 entries,
  `manifest.json` at root, **every CRC equal** to the side-load zip.
  4,083,698 bytes.
- Size discipline held: BuildTools clone deleted again after packing;
  workspace 35 MB.
- **Not verified (needs the phone)**: that Edge Canary accepts a
  self-signed CRX3 (community reports say the crx path works for
  side-loaded packs, but nobody has run *this* file), that the MV3 service
  worker + sidePanel survive its Chromium build, and Pro sign-in.

## 2026-11-02 — CRX wrapper zip (desktop Chrome auto-install workaround)

- User hit `CRX_REQUIRED_PROOF_MISSING` downloading the `.crx` in desktop
  Chrome: Chrome intercepts loose crx downloads and tries to install them,
  requiring Web Store proof in the header; it then discards the file. This is
  desktop Chrome's install path, not a defect in the packed file (structure
  and CRCs were verified when built).
- Created `/home/user/vdh-pro-10.6.1.2-crx.zip` (single entry, the crx;
  inner sha256 verified equal to the original, 4,069,143 bytes) so the
  download survives desktop Chrome untouched; unzip happens on the phone.
- Implication recorded: desktop Chrome enforces store proof; whether **Edge
  Canary's** "Extension install by crx" enforces it for self-signed CRX3 is
  still **unverified** — first thing the phone test will answer. Fallback if
  it refuses: Kiwi (takes the plain zip) or the vendor's Firefox build.

## 2026-11-02 — Device-verified install + detection; Android UI patch v1

- **User verified on the phone (Edge Canary)**: the CRX installs and videos
  are detected — first device-verified result for this project. Two UI
  defects reported: (1) switching to side-panel mode closes the UI with no
  way to bring it back; (2) the default popup renders as a small box at the
  top-left instead of filling the screen.
- Root causes found by reading the bundle (not guessed): the Chrome build
  (`qe=ht=="mozilla"` → false) routes sidebar mode through
  `chrome.sidePanel.open()` — Edge Canary Android exposes the API but has no
  side-panel UI, so nothing renders; and popup mode uses fixed desktop sizes
  (480x320/460/600 px), wider than a phone viewport. "Window" mode is dead
  in this build (no `windows.create` anywhere).
- Wrote `scripts/patch-vdh-mobile-ui.py` (persisted; strict match-once,
  idempotent, aborts before writing on any mismatch — it did abort once when
  a target had been reconstructed from truncated grep output; the exact
  strings were then extracted from the file itself).
- Patches: sidebar mode → `chrome.tabs.create(content/sidebar.html)` on
  action click and on mode selection (listener cleaned up when switching
  back to popup); phone-viewport CSS override in `popup.html` and
  `sidebar.html`. Only the Chrome code path touched; the Mozilla branch is
  dead code in this build and was left alone.
- Verified after packing: `node --check` on the patched worker parses;
  markers `vdhMobileSidebar` x5 in the packaged `service/main.js` and
  `vdh-android-ui` in the packaged HTML; CRX payload CRCs == rebuilt
  sideload zip == wrapper zip (79 entries). Same signing key → same
  extension ID `gmppgmnjccgklfklpibfkejdjijlnlnd`, installs over the old one.
- **Unverified (phone)**: that the tab-based sidebar renders the full UI
  correctly, that the popup override actually enlarges Edge Canary's popup
  dialog (the dialog container may impose its own cap — if so, the tab mode
  is the fallback), and that mode-switching round-trips cleanly.

## 2026-11-02 — On-device UI screenshots: v1 CSS was wrong; patch v3

- User committed two phone screenshots (`screenshots/`, commit `90f5e9c`,
  pulled). Read them: popup mode opens a bottom sheet whose UI stops after
  one media row, the remaining ~70% of the sheet is white; side-panel mode
  (screenshot 2) is a tab on `extension://<id>/` and renders full width.
- Diagnosis, measured instead of guessed: `content/popup.html` only pins
  sizes on `html[dockmode="popup"][popup_size=...]` and `popup_size` is
  applied at runtime from settings (default `medium` = 460 px), so v1's
  `100vh` override on the same element made the sheet and the page agree on
  a height the UI does not fill. v1 also asserted `height:100%` on `#main`,
  which the app sizes through a flex chain (`#main flex:1`, toolbar last).
- Built `scripts/render-check-vdh-ui.py` (Playwright + `chrome.*` mock +
  local http server, since `file://` blocks the ES module). Baseline and
  patched renders agree that the lower area is NOT the page background -
  the app paints nothing because it has nothing to show; the white belongs
  to the browser container.
- Patch v3 (`scripts/patch-vdh-mobile-ui.py`, 4 worker replacements):
  popup mode no longer uses the toolbar popup on Android - selecting either
  mode opens `content/sidebar.html` in a full tab, and re-clicking the icon
  focuses that tab (`chrome.tabs.query` + `update`) instead of stacking
  copies; `chrome.action.openPopup` call removed for popup mode. CSS forces
  `100vw`/`100dvh` on `html` and `body` for both dock modes, including the
  `popup_size` variants, with no overrides on `#main`.
- Verified: renders at 412x915 -> `html`/`body` 915 px, `#main` 874 px,
  toolbar at y=874 (was 460 px html in a tab before v3); worker parses
  (`node --check`); patch idempotent; `scripts/pack-vdh-artifacts.sh` asserts
  CRX payload CRC table == sideload zip and markers inside the payload.
- Artefacts (v3): crx 4,084,185 B, crx wrapper zip 4,084,323 B, sideload zip
  4,062,912 B; same key, ID `gmppgmnjccgklfklpibfkejdjijlnlnd`.
- Unverified on phone: that Edge Canary renders the tab full height and that
  the icon re-click focuses rather than duplicates.

## 2026-11-02 — Branch now carries the VDH source (layout change)

- User clarified the "MD files only" rule: it governed what to COPY from the
  other branches, not what this branch may contain. Corrected layout: the
  branch holds VDH extension source + scripts; VLCFlix app source stays on
  `main`; the crx/zips stay out (rebuilt every change, force-push disabled,
  so history could never be pruned).
- Added `extension/` (pristine VDH tree, 79 files, 14 MB, wasm included),
  `scripts/` (patch, render check, packer, restore), `.gitignore`
  (`build/`, `dist/`, `.secrets/`); moved the two UI screenshots to
  `docs/screenshots/`.
- Scripts rewritten to be repo-relative: `patch-vdh-mobile-ui.py` stages
  `extension/` -> `build/extension` (vendor tree never mutated),
  `pack-vdh-artifacts.sh` writes `dist/` and self-checks (payload CRC table
  vs sideload zip, markers inside the packaged payload, extension ID from the
  signing key), `render-check-vdh-ui.py` asserts layout and exits non-zero on
  failure.
- Verified end-to-end from the repo: patch (4 worker replacements) -> render
  check PASS at 412x915 and 412x800 -> pack. Extension ID still
  `gmppgmnjccgklfklpibfkejdjijlnlnd`; build outputs ignored by git.
- Fixed while verifying: the packer's ID derivation hashed the PKCS8 private
  key instead of the public SPKI (reported a wrong ID); now mirrors node's
  `createPublicKey` and prints the expected ID.

## 2026-11-02 — Readability pass + secret-scanning false positive

- User: GitHub flags a secret in `extension/injected/youtube.js`, and the
  source is unreadable. Investigated the key instead of assuming: it is
  `AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8`, the public InnerTube web-client
  key that ships in youtube.com's own page source; the same value appears in
  yt-dlp's `INNERTUBE_CLIENTS`, NewPipeExtractor issue threads and every
  "innertube" client (3 occurrences here, all inside the WEB /
  WEB_EMBEDDED / WEB_CREATOR client descriptors). Not a credential of ours,
  nothing to rotate; flagged as a false positive in CONTEXT.md.
- `scripts/format-vdh-source.py`: reformats all 31 JS + 6 HTML files in
  place. First attempt used a hand-rolled string/regex-aware stripper for
  verification and rejected a correct beautify (regex literals were read as
  strings); replaced with an acorn token-stream comparison. HTML uses tag
  sequence + void/balance checks.
- `patch-vdh-mobile-ui.py` reworked for formatted input: whitespace-
  insensitive target matching that still enforces a single unique match,
  plus `node --check` on the patched worker before it is accepted.
- Verified after the pass: `--check` is a no-op (idempotent), patch applies
  (4 replacements), render check PASS at 412x915 and 412x800, packer reports
  payload CRC table == sideload zip and ID
  `gmppgmnjccgklfklpibfkejdjijlnlnd` (unchanged). Artefacts grew ~150 KB
  (crx 4,238,826 B) purely from indentation.

## 2026-11-02 — UI scale patch v4 (v3 reverted)

- User: v3 made it worse. The screenshots were sent to ask for a BIGGER UI,
  not for both modes to open a tab. Popup mode must stay a popup.
- Reverted in the worker: popup-mode reroute and the `chrome.action.openPopup`
  removal are gone; only the two sidebar-path replacements remain.
- Found the real cause in the stock CSS: `:root { font-size: 12px }` with a
  rem-based UI, plus a `@media (max-width: 400px)` rule that deliberately
  shrinks everything to 0.8rem (9.6px) with halved spacing. A phone sheet is
  right at that breakpoint.
- v4 CSS: 20px root (repeated in the 400px media query so it wins), spacing
  4/8/14/24, video-height 128px, `font_size` options stack at 22/24px, both
  modes fill viewport width + 100dvh, tighter row margins, ~44px controls.
- Harness rework: fabricated session/persistent state is rejected by the
  app's own schema validator, so the harness now measures the rendered shell
  (root font 20px, every rendered text >= 12px, page height == viewport) and
  reports the longest empty band. Verified PASS for both pages at 412px wide;
  screenshot checked by eye (text and buttons now scale, nothing clipped).
- Repacked: crx 4,240,006 B; ID unchanged. Row-level layout with real media is
  unverified and must be confirmed on the phone.

## 2026-11-02 — v5: real font scaling, premium UI removed, vendor links stripped

- User: still too small, a premium/free-account banner appeared that was not
  there before, and asked to remove everything that links back to the vendor.
- Why v4 failed: shadow DOM. Components carry `:root, :host { font-size:
  0.8rem }` in a <=400px media query; a page rule on `:root` cannot cross the
  shadow boundary. v5 also sets font-size on every component host (21px base)
  and reports the deepest rendered text node per component in the harness: all
  21px now.
- The banner was the stock `com-banner` markup, already present in
  `popup.html`; it only needed the ad behaviour from the worker to become
  visible. Hidden via its own rule (`M(this, !1)`).
- Vendor links: `scripts/strip-vendor-links.py`. Enumerated first — 15 base
  URLs, 13 `files.`, 9 help articles, 3 tos, 3 store listings, 2 vendor
  content-script matches. Then: inert host for all vendor hosts, `about:blank`
  for help, store/review literals blanked, `homepage_url` removed, activate.js
  and premium.js deleted with their manifest entries, manifest name +
  description + 33 locales neutralised, upsell strings replaced.
- Polarity audit caught two of my own first-pass mistakes before shipping:
  `D()` is hide (so the review edit would have kept it visible) and
  `I()` is show (so the premium buttons needed flipping to `D()`). The script
  now hard-fails if any of the three UI edits misses.
- Verified: patch (2 replacements) -> strip (asserts) -> render check PASS ->
  pack, 77 entries, ID unchanged, vendor references: none.

## 2026-11-02 — REGRESSION: vendor-host rewrite broke media detection

- User: only OK / get_premium_description / Version / Report on screen, no
  media rows at all.
- Diffed the v5 build against a patch-only build: the strip pass had rewritten
  `https://downloadhelper.net:443` -> `http://127.0.0.1:9:443` and the
  `files.downloadhelper.net` ruleset URL inside service/main.js, panel.js,
  global_persistent.js, smartnaming.js and every injected script. Those are
  the client base URLs and the detection path, not UI links.
- Reverted every JS/URL edit. strip-vendor-links.py now: panel.js visibility
  only, manifest (drop activate/premium content scripts + files, rename,
  drop homepage_url), locales. New gate: a full-tree byte diff against
  /tmp/vdh-prestrip must show "files changed beyond the allowed set: none",
  and the service worker must stay byte-identical.
- Verified: 6 representative JS files byte-identical to the patch-only build;
  render check PASS (21px everywhere); pack OK, ID unchanged.
- Lesson recorded in CONTEXT: cosmetic link removal is not worth breaking the
  download path; do not rewrite hosts in JS again.

## 2026-11-02 — ROOT CAUSE: unclosed <style> in my CSS patch (blank panel)

- User pushed back on the link theory with the correct timeline: it broke right
  after the size change. Bisected properly this time by loading the real
  extension into Chromium (`--load-extension`, three variants): pristine
  mounts, every patched variant throws `DOM not valid`.
- `DOM not valid` comes from `content/panel.js` when `com-main`,
  `com-settings` or `com-toolbar` are missing at script time. Dumping the
  parsed DOM showed the patched page had `body > vbox#box_main` instead of
  `com-settings/com-main/com-toolbar`: my injected `<style>` block had no
  closing tag, so the parser swallowed `<body>` and the components as CSS
  text. Stray text (OK / get_premium_description / Version / Review) was
  exactly what leaked out.
- Fixed: closed the block, and added `verify_document()` to the patch script
  (one added style, same tag sequence as the pristine file, same unclosed
  stack, components present) -- it aborts the build otherwise.
- Committed as a proper regression test: `scripts/check-real-extension.py`
  loads the built tree into Chromium and asserts components + zero page
  errors + 21px font. Current build passes.
- The earlier vendor-link revert stays (it was a second, independent mistake,
  but it was NOT the cause of this symptom).

## 2026-11-02 — v6: bigger toolbar icons, more padding, side margins

- User: enlarge the toolbar icons and their padding, and give the whole
  extension more margin to the screen edges.
- Icons live in the toolbar component's shadow template -> patched the
  template's own style block (page CSS cannot reach a shadow root). Icons
  16-18px -> 26px, buttons -> 48x44 with 7x9 padding, bar padding 10x12.
- Side margins: body padding 10px + 8px inner margins on scroll content.
- First attempt overflowed: 7 buttons at 46px + gaps are wider than a 412px
  phone and the stock bar clips rather than wraps, so the last button was cut
  off. Added `flex-wrap: wrap; justify-content: center`; verified
  `toolbarOverflow: 0`, screenshot checked by eye (two tidy rows, nothing
  clipped).
- Also hit my own trap twice: the inserted CSS comment originally contained a
  literal `<style>` string, which the document verifier counted as a real open
  tag and aborted the build. Text inside CSS comments must not look like tags.

## 2026-11-02 — download storage explained; ENOSPC handoff fix

- User's download completed, then the handoff to Edge's downloader failed with
  "No space left on the device", and the whole file had to be re-downloaded.
- Traced the pipeline: worker -> OPFS (`<download_id>.<ext>`) -> blob URL ->
  `chrome.downloads.download` -> browser writes to shared storage.
- Found the real defect: service/main.js deletes the OPFS entry and revokes the
  blob URL *before* checking whether `downloads.download` succeeded, so a
  failed handoff destroys a finished download.
- `scripts/patch-vdh-keep-on-failure.py`: keep the OPFS file + blob URL when
  the handoff fails (still clean up on user-cancel), and add storage
  usage/quota/free to the error message. Strict single-match, node --check,
  idempotent.
- Verified the extension still mounts with the patch (real Chromium run, 0 page
  errors); packed with the ID unchanged.

## 2026-11-02 — pause / resume feature (paused paths: progressive http)

- Traced the worker first: ids, message handler, dispatcher `Mo`, strategies,
  `Ce` streaming loop. Pause is feasible because `Ce` already has an offset and
  Range-based recovery.
- Implemented in three layers (worker control map + messages + pause wait,
  service forwarding, panel button + markup). Caught two of my own mistakes
  before shipping: the control object was not reaching `Ce` (the dispatcher
  calls the strategy, not `Ce`), and the pause handler was overwriting the
  progress object (would have blanked percentage/bytes).
- Dropped a no-op edit to the segmented loop rather than leave dead plumbing.
- Verified: `node --check` on all three files, extension mounts with 0 page
  errors, packed with ID unchanged.
- NOT verified end to end: synthetic `<video>` pages are not detected by the
  extension in headless Chromium, so `check-pause-resume.py` cannot drive a
  real download here. Phone test required.

## 2026-11-05 — round 8: the text size was not the panel, it was History

- User: "reduce the font back to the exact size it was before", naming `a02412f`
  as the good size, and "everywhere in the app".
- Measured instead of guessing: rendered `a02412f` (`git archive` into /tmp/base,
  rebuilt with the same patches) side by side with HEAD. The panel is identical
  -- root 21px, main 21px, toolbar 21px, toolbar button 21px in a 46x44 box, in
  both trees. The only font-size line added since `a02412f` is `1.05em` on the
  new subtitles summary.
- The real regression was History: at `a02412f` it rendered root/body/p at 21px;
  now it is an iframe of the vendor's stock `history.html`, which runs at 12px
  root / 16px body. `patch_html()` now patches `history.html` too, and
  `verify_document()` takes `required=()` because that page has no
  com-main / com-settings / com-toolbar (without it the build aborted).
- After: history root 21px, paragraph 21px. Suite green; `3adfd09`.

## 2026-11-05 — round 9: a manual text-size control

- User asked to be able to choose the size themselves.
- `scripts/patch-vdh-text-size.py` + `extension/content/vdh-text-size.js`:
  the stylesheet's three hard-coded 21px values (root, the <=400px media query,
  and the component-host list) became `var(--vdh-ui-base-font, 21px)`, so one
  property rescales everything else (all other sizes are rem). Spacing and the
  toolbar icons/tap targets scale too, each with the phone value as a floor
  (`max(44px, base * 2.09524)`), which leaves 21px bit-for-bit unchanged.
- Card is the first box in Settings: slider (15-32px), -/+ steppers, live px
  readout, preview line, reset to 21. Applied live while dragging.
- Two-tier storage: localStorage for a synchronous first paint (no flash of the
  default size), chrome.storage.local for durability and for the live update of
  the other open pages, including the embedded history iframe.
- `scripts/check-text-size.py`: default still 21px, a real mouse drag on the
  slider, steppers, reset, persistence across reload, history page follows both
  on a fresh load and live, and the app's own large/verylarge still stack on top.

## 2026-11-05 — round 10: the range is 5-15px, and 15px is the ceiling

- The user was right that my measurements had missed something. `getComputedStyle
  .fontSize` reports CSS pixels, so it reads 21px whether the page is laid out at
  ~980px and scaled down to the screen (no viewport meta) or at true device
  width. The apparent size is what changes, and that is the "twice the size"
  the user kept describing: before `15e176f` a 21px root reached the eye at
  roughly 15px.
- So the slider now runs 5-15px with 15px as both the default and the maximum:
  15 reproduces the size the panel used to have, and below it is for a screen
  where even that is too large. The stylesheet fallback moved to 15px as well,
  so an untouched install and the card can never disagree.
- Spacing, tap targets (44px) and toolbar icons (26px) keep their px floors, so
  shrinking the text to 5px never shrinks the things you press.
- Suite green: text-size, viewport, history-open, subtitles, subtitle-options,
  journal, resume, real-extension (rootFont now reports 15px).

## 2026-11-05 — round 11: margins, icons and rows all scale with the text

- The user: the side margins never reduced, the icons never got small enough to
  sit on one line, and the history rows (Storage & interrupted downloads, the
  Play buttons) had to be swiped sideways to be read.
- Root cause of the first two: two things were pinned to floors rather than
  proportional. The spacing variables had floors equal to their own 21px values
  (4/8/14/24), so below a 21px base nothing moved at all; and the toolbar was
  floored at a 44px target / 26px icon, so seven icons could never fit one line.
  Floors are now 1/2/3/5/40px for the spacing and thumbnail, and 20px (target)
  / 12px (icon) for the toolbar.
- Measured on a 720px screen (the user's width): icons 19 -> 14 -> 12 -> 12px
  across 15/11/8/5px, one line at every size. On 360px: two rows at 15px, one
  row from 11px down. Side margin 7.1px -> 2.4px across the range.
- History rows are web components, so page CSS cannot reach the Play / Retry /
  Delete buttons (sized by width="16" ATTRIBUTES) or the vendor's
  `flex-shrink: 0`, which is what pushed rows off the screen. New
  `patch_media_templates()` in patch-vdh-mobile-ui.py injects scaling CSS into
  the media templates' own <style>, the same way the toolbar is patched.
- One self-inflicted bug worth remembering: my new CSS mentions
  `#vdh_journal_box`, and patch-vdh-download-journal.py's idempotency guard
  greps for the bare string, so it silently skipped inserting the storage box
  markup. The guard now looks for `<vbox id="vdh_journal_box"`.
- scripts/check-fit.py (new): margins shrink, icons end up one line, and
  nothing is wider than the screen -- panel, settings, history page and the
  embedded history view, at 360px and 720px, at 15/11/8/5px, with real seeded
  journal rows and a media row built from the real component with a very long
  file name.

## 2026-11-05 — round 12: footer, Pause, the subtitle marker, and the partials

- Footer: `com-main { flex: 1 1 auto; min-height: 0 }` plus
  `com-toolbar { position: sticky; bottom: 0 }` and `html { overflow: hidden }`.
  Without min-height: 0 the flex child refuses to shrink below its content and
  pushes the page taller than the screen, which is what let the bar drift.
  Measured with a 30-item list at 15/11/8/5px: bottom edge always at the
  viewport bottom, page never scrolls.
- The Pause button was rendered but unreadable: #box_combo > * paints its
  children in --theme-colored-fg (near-white) and only #button_stop has a
  background of its own, so Pause came out white-on-white (measured colour
  rgb(245,245,245) on background rgb(245,245,245)). It now has a slate
  background; contrast 0.74. Pause/Cancel also keep a 20px tap-target floor.
- Third time the same trap bit me: CSS that mentions another patch's id trips
  that patch's idempotency guard. My #button_pause rule made
  patch-vdh-pause-resume.py skip inserting the button, so the component threw
  `Element "#button_pause" not found` -- the button was gone from the build
  entirely. Guards now match the element, not the bare id (same fix as
  #vdh_journal_box in round 11).
- Subtitle marker: a small "CC en, es" badge on a running download, drawn from
  the rendition list the worker already stores with the journal record (key
  `subtitles`), so no extra fetching and it survives a reload.
- Thumbnail scales with --video-height: 94 -> 69 -> 50 -> 32px tall across
  15/11/8/5px (floor lowered from 40 to 32).
- PARTIALS ANSWERED: the vendor's startup sweep deletes every OPFS file on
  every service-worker start -- that is why a partial never survived a crash or
  a restart. It now keeps files owned by a live journal record and sweeps
  orphans; proven across a real browser restart (64 KiB partial intact, orphan
  gone). Reinstalling the extension, or a browser restart that drops a
  side-loaded extension, still wipes it -- nothing in the extension can help.

## Round 13 - subtitles for DASH streams (.mpd)

You asked for the one case that was still missing: HLS subtitles worked, DASH
did not. Both halves of that are now done.

**Why DASH had nothing to download.** The vendor's HLS path records a subtitle
as a URL. Its DASH path does not: `TS()` in main.js records
`{language, type: "id", id}` where the id is the MPD Representation id, because
on desktop the track is muxed by ffmpeg rather than fetched. There is no ffmpeg
here, so my code -- which kept only renditions with a url -- dropped every DASH
track before anyone could see it. Two changes fix that: `renditionsFromMedia()`
now resolves a `type: "id"` rendition into `dash:<manifest>#<id>` using the
media's own `master_url`, and `vdh-subtitles.js` grew an MPD reader that turns
that reference into text.

**What the new code handles.**
- An MPD is XML, so it is parsed with a small XML reader written for this
  module. A service worker has no DOM and therefore no DOMParser, which is the
  first thing that broke: the manifest parsed fine in a page and silently
  returned nothing in the worker.
- AdaptationSets are recognised by `contentType="text"`, by a text mimeType,
  by codecs (`stpp`, `wvtt`, `webvtt`) or by a `subtitle`/`caption` Role. The
  video and audio sets are left alone. BaseURLs concatenate up the tree.
- Both segment shapes: `SegmentTemplate` + `SegmentTimeline`
  (`$Number$`/`$Time$`/`$RepresentationID$`, with `S@t`, `S@d`, `S@r` repeats
  walked in order) and fixed-`duration` templates, where the count comes from
  the Period length and is capped at MAX_SEGMENTS so a live stream cannot run
  away.
- TTML -> WebVTT, because DASH carries `application/ttml+xml` far more often
  than HLS does. Handles `begin`/`end`/`dur`, the frame (`120f`) and clock
  (`00:00:01.500`) time forms, `<br/>`, and nested spans.
- Segment cues are shifted by each segment's timeline position, exactly as the
  HLS path does, so a segmented track comes out continuous instead of restarting
  at 00:00 every segment.

**One case is refused on purpose.** WebVTT wrapped in MP4 boxes (`wvtt`) is
binary: the cues live in mdats and reading them needs an ISOBMFF parser this
module does not have. It is reported as "WebVTT in an MP4 container (wvtt)
cannot be read as plain text" in the panel rather than written out as a .vtt
that is really an MP4. A save-all still saves the other languages and reports
that one.

**Verified, not assumed** -- `scripts/check-dash-subtitles.py`, five parts
against a fixture MPD served over HTTP (`scripts/test-subtitle-server.py` grew
the DASH routes): the four AdaptationSets (en single file, es 3 segments, fr
TTML, de wvtt), each shape stitched into one valid WebVTT with the right
timings, wvtt refused, the panel path (paste the .mpd address -> Save
en/es/fr, names read from the panel's own "Saved ..." line and bytes read back
from the chrome.downloads record, since Playwright renames intercepted
downloads to a uuid), and the automatic path: a media payload shaped exactly
like the vendor's `mpd_playlist` one, offering Subtitles: en/es/fr on the
History row and saving a correctly-timed file from it. RESULT: PASS. All ten
earlier suites still PASS and the extension audit is clean.

Two bugs worth remembering, both found by the fixture rather than by reading:
comments in the XML made my reader skip to the *next* `-->` and swallow the
AdaptationSets between them; and `<br/>` inside a TTML cue lost its place
("Bonjourtout le monde") until text runs and child elements were recorded in
document order.

Artifacts rebuilt (same id `gmppgmnjccgklfklpibfkejdjijlnlnd`):
`vdh-pro-10.6.1.2.crx` 4,294,951 B sha256 1be1716a7f6ba1bd42be3b413785effea76999d566003cf5c3f90d71ba0ada61.
