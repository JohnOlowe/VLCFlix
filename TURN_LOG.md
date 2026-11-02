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
