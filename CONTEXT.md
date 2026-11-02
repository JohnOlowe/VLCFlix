# Project context — vdh-android

## Goal

Port **Video DownloadHelper Pro 10.6.1.2** (the Chrome MV3 extension packaged
as `10.6.1.2_0.7z` in `JohnOlowe/BuildTools`) to Android. The analysis lives
in `docs/ANDROID_PORT_PLAN.md`.

## Decisions (user, 2026-11-01 — asked, not assumed)

- **Route A + D**: side-load the extension unmodified into an
  extension-capable Chromium browser on the phone (Edge Canary first choice;
  Kiwi archived but usable; **Firefox is not a target for this archive** —
  Chromium-only APIs; the Firefox-compatible VDH is the vendor's own Gecko
  build on AMO), and use Termux ffmpeg + MediaCodec for merging/converting.
- **Branch name**: `vdh-android` (renamed from the initial `android-port`
  before any push; the old name never existed on the remote).
- **Use scope**: personal use of the user's own Pro copy. No redistribution;
  `docs/ANDROID_PORT_PLAN.md` §5 stands.

## Assets (verified 2026-11-01 by extraction, not from memory)

- Source of truth: `JohnOlowe/BuildTools` on GitHub. Local clone is fetched
  on demand (sparse, 7z only — see "Workspace size discipline" below) and
  was **deleted** at end of turn after the extraction and zip were built.
  Contents: `10.6.1.2_0.7z` (2,440,677 bytes, sha256
  `21cf3b2bbb07cf9a27510506a83921953ed29ef9506dd681754163ebfae37921`),
  `apktool_3.0.3.jar`, `android-35.jar` (Android API 35 `android.jar`),
  `jadx-1.5.6.zip`.
- 7z extracted (py7zr) to `~/BuildTools/extracted/10.6.1.2_0` (14 MB, also
  deleted; rebuilt by the restore script).
  Measured structure: `manifest.json` = manifest_version **3**, name
  "Video DownloadHelper Pro", version 10.6.1.2; background = **service
  worker** `service/main.js` (557 KB, minified esbuild bundle) +
  `service/emscripten-module.wasm`; **23 content scripts**; per-site
  `injected/*.js` (youtube, vimeo, facebook/instagram, vk, ok.ru, bilibili,
  twitch-ish kick, canva, …) in ISOLATED and MAIN worlds; `download_worker/`
  with **libav.js 6.5.7.1 (h264-aac-mp3)** wasm muxer; `content/` side-panel
  UI; `_locales` for 30+ languages. Permissions include `offscreen`,
  `sidePanel`, `webRequest`, `declarativeNetRequest`, `scripting`,
  `downloads`, `<all_urls>` — several of these are **Chromium-only APIs**.

## User environment (as stated by the user, 2026-11-01 — not yet verified here)

- Android **14** phone, **no root**.
- **Termux** installed; `ffmpeg` hardware encoding/decoding via **MediaCodec**
  works there.

## Branch policy (user instruction, 2026-11-01)

- Branch `android-port` is an **orphan**: no files from `main`, `dev`, `cpp`
  or `studio-ui` except the MD files, and those rewritten to apply only to
  this work. `AGENTS.md` alone is verbatim, because it is the rules file.
- **MD files only.** No `checkpoint.json` on this branch by user instruction;
  `CONTEXT.md` + `TURN_LOG.md` are the state files (deviation from `AGENTS.md`
  §6 is deliberate and user-ordered).
- This branch has **no `.gitignore`** (that would be a non-MD file). Never
  `git add -A` here; add files explicitly by name.

## Git and credentials

- Remote: `https://github.com/JohnOlowe/VLCFlix.git`. It is recorded here
  because this MD-only branch cannot carry `.gitremote`, and workspace
  snapshots strip `.git/config`. If `git remote -v` is empty after a restore:
  `git remote add origin https://github.com/JohnOlowe/VLCFlix.git`.
- Commit identity (repo-local, matching the identity the user approved in the
  repo's history): `John Olowe <121398818+JohnOlowe@users.noreply.github.com>`.
- **No credential exists in this session.** Nothing has been pushed. A
  fine-grained PAT scoped to `JohnOlowe/VLCFlix` (Contents: read/write only)
  must be supplied by the user; store it at `.secrets/github-token` (mode 600,
  untracked, never printed, never in a remote URL) per `AGENTS.md` §8.
- No `--force`, ever (`AGENTS.md` §7). Push, then verify with
  `git ls-remote origin android-port`.

## Sandbox, measured 2026-11-01 (`df`, `free`, `nproc`, `which` — not recalled)

- 2 CPU cores, ~2 GB RAM, ~20 GB free disk.
- **JDK 11** present (`/usr/bin/java`, `/usr/bin/javac`).
- **No** `adb`, `gradle`, `aapt2`/`d8`, or Android SDK. A hand-rolled APK
  build (aapt2 + d8 + apksigner against `BuildTools/android-35.jar`) is
  plausible on JDK 11 but is **unverified** until attempted.
- No Android device in this sandbox: every on-device claim needs the user's
  phone, and must be labelled "verified on device" only after the user
  reports the result.

## Credential (2026-11-01)

- The user supplied a fine-grained PAT via the question UI. It is stored at
  `.secrets/github-token` (mode 600, **untracked**). It has not been printed
  into any file, commit, or remote URL. It **passed through chat**, so the
  user has been advised to rotate it when this work is done.
- Scope is verified by probing (status codes in `TURN_LOG.md`), not assumed.
- `.git-credentials` and `.git/config` are stripped by snapshots. To rebuild
  auth after a restore (no script on this MD-only branch):
  1. `git remote add origin https://github.com/JohnOlowe/VLCFlix.git` (if empty)
  2. `printf 'https://JohnOlowe:%s@github.com\n' "$(cat .secrets/github-token)" > .git-credentials && chmod 600 .git-credentials`
  3. `git config credential.helper "store --file $PWD/.git-credentials"`

## Next steps (route A+D)

1. **Edge Canary on Android installs CRX only** — there is no zip/unpacked
   path (confirmed by the user on-device 2026-11-02 and by community docs:
   Settings → About Microsoft Edge → tap build number 5× → Developer Options
   → "Extension install by crx"). Artefacts (workspace files, **not**
   committed — MD-only branch):
   - `/home/user/vdh-pro-10.6.1.2.crx` — CRX3, magic `Cr24` v3, signed with
     `/home/user/vdh-crx-key.pem` (kept; re-packs keep the same ID), payload
     CRC-identical to the zip. Extension ID:
     `gmppgmnjccgklfklpibfkejdjijlnlnd`.
   - `/home/user/vdh-pro-10.6.1.2-sideload.zip` — still valid for Kiwi-style
     browsers that take zips.
   - Re-pack recipe: `npm i crx3` then
     `crx3 -p ~/vdh-crx-key.pem -o ~/vdh-pro-10.6.1.2.crx <extracted-dir>`
     (extracted dir via `scripts/restore-vdh-workspace.sh`).
   - **Android UI patch (2026-11-02)**: `scripts/patch-vdh-mobile-ui.py`
     must run on the extracted dir **before** packing. It rewrites
     sidebar mode to open `content/sidebar.html` in a full tab (Edge Canary
     Android has the `chrome.sidePanel` API but no side-panel UI) and adds a
     phone-viewport CSS override to `popup.html`/`sidebar.html`. Strict
     (every target must match exactly once) and idempotent. Artefacts above
     are the **patched** builds; the patch markers are `vdhMobileSidebar`
     (service worker) and `vdh-android-ui-v3` (HTML).
   - **Android UI patch v3 (2026-11-02, after on-device screenshots)**: v1
     did not fix the phone UI. Measured cause: the toolbar popup is a bottom
     sheet whose height comes from the page's fixed desktop sizes
     (`popup_size` = 320/460/600 px, set at runtime), and the panel page keeps
     that height when opened in a tab, leaving a white void. v3: both modes
     now open `content/sidebar.html` in a full tab (clicking the icon again
     focuses it), and the CSS forces `100vw`/`100dvh` on `html` and `body`
     while leaving the app's `#main`/`#toolbar` flex chain alone.
   - **UI regression harness**: `scripts/render-check-vdh-ui.py` serves the
     unpacked tree over http (file:// blocks the ES module) with a tolerant
     `chrome.*` mock and measures/render-screenshots the panel at phone size
     with Playwright Chromium. Use it before every repack:
     `pip install playwright && python3 -m playwright install chromium`.
     Expect two harmless mount errors (proxy stub) and `#main` ending where
     `#toolbar` starts; that is the pass condition.
   - Packing: `scripts/pack-vdh-artifacts.sh` builds all three artefacts and
     asserts the CRX payload CRC table equals the sideload zip and that the
     patch markers are inside the packaged payload.
2. User loads the CRX in Edge Canary and reports: does it install, does the
   icon appear, does it detect a DASH stream (e.g. YouTube) and an HLS
   stream, does Pro sign-in work. Result → `TURN_LOG.md`, labelled
   "verified on device" only with the user's report.
3. Termux side: verify the merge command (`ffmpeg -i video -i audio -c copy
   out.mp4`) and a MediaCodec transcode on a downloaded pair; then decide the
   glue (manual / Termux:API / RUN_COMMAND intents).
4. If Edge Canary cannot run the extension, fall back order: Kiwi → official
   VDH Gecko build on Firefox for Android (different archive, vendor-built) →
   revisit route B.

## Workspace size discipline (user instruction, 2026-11-01)

The snapshot cap is **128 MB**; the workspace must end every turn below it.
Re-obtainable bulk (the `BuildTools` clone — ~237 MB full, and its
extraction) is **deleted after use** and rebuilt on demand by
**`/home/user/scripts/restore-vdh-workspace.sh`** (kept in the workspace, not
in this MD-only repo). The script sparse-clones **only** `10.6.1.2_0.7z`
(≈2.4 MB, `--no-cone` — cone mode would drag in the 117 MB of root-level
tools), verifies the sha256, extracts, and rebuilds
`vdh-pro-10.6.1.2-sideload.zip`; verified end-to-end in a throwaway
`/tmp` root (restore 2.6 s, 19 MB, zip CRCs identical). Other BuildTools
files when needed:
`git -C ~/BuildTools sparse-checkout add apktool_3.0.3.jar android-35.jar jadx-1.5.6.zip && git -C ~/BuildTools checkout`.
Measured after cleanup: workspace 30 MB total.

## RECOVERY

1. Read `AGENTS.md` (rules), then this file, then `TURN_LOG.md`.
2. `cd /home/user/VLCFlix`; if `git remote -v` is empty, re-add `origin` from
   the URL above; `git checkout vdh-android` if needed.
3. If `/home/user/BuildTools` or the side-load zip is missing, run
   `bash /home/user/scripts/restore-vdh-workspace.sh` — do not re-clone
   BuildTools in full.
4. Keep the end-of-turn workspace under 128 MB: delete the BuildTools clone
   and extraction again when a task no longer needs them.
5. Nothing is pushed without the PAT; verify the remote SHA after every push.
6. Resume at the newest entry in `TURN_LOG.md`.

## Branch layout decision (2026-11-02)

- The branch carries **VDH source, not VLCFlix app source**: this branch is
  the extension port; VLCFlix's `public/`, `server/`, `src/` stay on `main`.
- `extension/` is the pristine vendor tree and stays unmodified; the patch
  script copies it to `build/extension` and patches the copy, so `git diff`
  against upstream stays meaningful.
- Binaries that are part of the extension (the 4.9 MB libav wasm, bitmaps,
  `_locales`) are committed: they are inputs, they never change. The *build
  outputs* (`build/`, `dist/`: crx + two zips, ~4 MB each, rewritten on every
  rebuild) are ignored via `.gitignore` because force-push is unavailable and
  history could never be cleaned.
- The signing key never goes in the repo:
  `.secrets/vdh-crx-key.pem` (ignored) or `$VDH_CRX_KEY`. Without it the
  extension ID changes and the phone install becomes a separate extension.
- Scripts are path-independent: they locate the repo from their own path, so
  the same files work from a fresh clone on the phone.

## Readability pass (2026-11-02)

- `extension/` is a minified vendor drop: `service/main.js` was one 557 KB
  line. `scripts/format-vdh-source.py` reformats every `.js`/`.html` in place
  (js-beautify for JS, a style-block expander for HTML) and is the only
  writer of `extension/` besides the vendor drop itself.
- It is verified, not assumed: acorn token streams must be identical before
  and after (JS), tag sequences must be identical (HTML); the JS file is also
  checked with `node --check` afterwards, and any mismatch restores the
  original and exits non-zero. `--check` reports without writing.
- Cost: JS 6.9 MB -> 8.9 MB (indentation, one statement per line). `dist/`
  grows ~150 KB. Worth it: the four expressions the patch rewrites, and the
  CSS the UI patch touches, are now reviewable.
- Consequence for the patch: minified matching no longer works.
  `patch-vdh-mobile-ui.py` now matches targets whitespace-insensitively
  (`find_ws`: flatten both sides, map offsets back) and still requires exactly
  one match; it also runs `node --check` on the patched worker before writing.
- The API key in `injected/youtube.js` (`AIzaSyAO_FJ2SlqU8...`, 3x) is
  YouTube's public InnerTube web-client key: it ships inside youtube.com's own
  page source and appears in the same form in yt-dlp, NewPipeExtractor and
  every "innertube" client. It is not our credential and needs no rotation.
  GitHub secret scanning may still flag it; that is a false positive for this
  repository.

## UI scale, patch v4 (2026-11-02) — supersedes v3

v3 was wrong: it rerouted BOTH dock modes to a browser tab. User feedback:
keep popup mode as a popup; the actual problem was that everything is tiny.
Restored: the worker now only remaps the sidebar path (2 replacements);
popup mode is stock.

Measured cause of "tiny":
- `:root { font-size: 12px }` and the entire UI is rem-based, so 12px is the
  ceiling for body text.
- At `max-width: 400px` the stock sheet adds a rule commented "the sidebar is
  real small. Let's make everything small." -> `:root { font-size: 0.8rem;
  --spacing-*: half; --video-height: 70px }` = 9.6px text. A phone popup sheet
  sits right at that threshold.

v4 CSS (in `content/popup.html` + `content/sidebar.html`):
- `:root`/`:host` 20px + the same override repeated inside `@media
  (max-width:400px)` so the shrink can never win; spacing 4/8/14/24px,
  `--video-height: 128px`.
- `font_size=large` / `verylarge` still stack: 22px / 24px.
- popup sheet and sidebar tab both fill width and `100dvh`; `#main` keeps the
  app's own flex + `overflow-y:auto`, so extra height goes to the media list
  instead of an empty band.
- com-media margins tightened, toolbar padded, controls `min-height: 2.2rem`
  (~44px) for finger targets.

Harness: `scripts/render-check-vdh-ui.py` now reports root font size, real
element font sizes, page/main/toolbar geometry and the longest empty band;
it asserts root >= 19px, no rendered text < 12px, page height == viewport.
It cannot populate the media list: the app validates the state it receives
from the worker with a schema, and a fabricated state is rejected ("DOM not
valid" / valibot "Unreachable"). So row-level layout is unverified here and
must be judged on the phone.

## Patch v5 + vendor-link strip (2026-11-02)

- v4 was ineffective for a reason worth remembering: the UI is a set of web
  components, each with its own shadow root, and the stock sheet shrinks them
  with a `:root, :host { font-size: 0.8rem }` rule below 400px. Raising only
  `:root` does nothing, because inheritable font-size stops at the shadow
  boundary. v5 sets the size on `:root` **and on every component host**
  (page-level styles on a host beat that component's own `:host` rule):
  21px base, 23/25px for the app's large/verylarge font options.
- `scripts/strip-vendor-links.py` (new, runs after the UI patch):
  vendor hosts -> `http://127.0.0.1:9` (inert; every derived endpoint and the
  remote ruleset now fails fast instead of phoning home), help links ->
  `about:blank`, store/review URLs -> `""`, `homepage_url` dropped, the two
  vendor-only content scripts removed with their files, extension renamed to
  "Video Downloader" in manifest + all 33 `_locales` files, upsell strings
  neutralised.
- Polarity trap worth remembering: `M(el, v)` / `I(el)` / `D(el)` are
  show-if-truthy / show / hide. The banner rule was `M(this, advertize)`
  (hide by passing `!1`), the review prompt was
  `i.dont_ask_for_user_review && D(this.p_leave_review)` (dropping the
  condition would have *shown* it - only the condition is dropped), and the
  premium buttons were revealed with `I(...)` (flipped to `D(...)`).
  The script now asserts all three landed, so a silent no-op fails the build.
- Verified: patch -> strip -> render check (root 21px, component hosts and
  their shadow text 21px, page height == viewport) -> pack. ID unchanged,
  "remaining vendor references: none".

## Never rewrite vendor hosts in JS (2026-11-02, regression)

- The v5 vendor-link strip rewrote `https://downloadhelper.net:443` and the
  ruleset URL inside `service/main.js`, `content/panel.js`,
  `content/global_persistent.js` and every `injected/*.js`. Result on device:
  no media was detected and the panel showed only the empty shell.
- Cause: those strings are not decoration. `qe`/`nn`/`le` are the client
  base URLs the entitlement state and per-client ruleset fetch go through;
  the injected scripts are the detection path.
- Rule now enforced by the script itself: `strip-vendor-links.py` does not
  touch any URL or host in JavaScript. It only (1) changes visibility of
  upsell UI in `content/panel.js`, (2) edits the manifest (drop the two
  vendor-only content scripts, rename, drop `homepage_url`), (3) edits
  `_locales`. It then diffs the whole tree against a pre-strip reference copy
  (`/tmp/vdh-prestrip`) and fails if anything else changed.
- Consequence: vendor links reachable from the UI (support, premium, terms,
  store) stay in the code. Cosmetic link removal is not worth a broken
  detection pipeline; remove them only if a link is proven dead code.

## CSS patch must close its <style> block (2026-11-02, root cause of the blank panel)

- Symptom: from v4 on, the panel showed only stray text ("OK",
  "get_premium_description", "Version: Local build", "Report") and no media.
- Cause: `patch-vdh-mobile-ui.py`'s CSS string ended without `</style>`. An
  unclosed `<style>` makes the HTML parser treat everything after it as CSS
  text, so `<body>` is never created and `com-main`/`com-settings`/
  `com-toolbar` do not exist. `content/panel.js` then throws `DOM not valid`
  at startup and nothing renders.
- This is why every JS-side theory (vendor URL rewrite, deleted content
  scripts, shadow-DOM font size) failed to explain it: the HTML was broken.
- Guard added: after injecting CSS, `verify_document()` re-parses the patched
  file and requires (a) exactly one added `<style>`, (b) the same tag sequence
  as the pristine vendor file, (c) the same leftover unclosed-tag stack (the
  vendor markup is genuinely loose, so absolute balance is not the test), and
  (d) `com-main`/`com-settings`/`com-toolbar` present. The build aborts
  otherwise.
- Verification that would have caught it: `scripts/check-real-extension.py`
  loads the unpacked tree into Chromium (`--load-extension`), opens
  `content/popup.html` and asserts the components exist, no page errors, and
  the computed font size. Current build: all true, 0 errors, 21px root and
  toolbar. Use it, not the mock harness, whenever the panel "renders nothing".

## Toolbar icons + side margins, patch v6 (2026-11-02)

- The toolbar icons are inline SVGs inside `com-toolbar`'s shadow template, so
  page-level CSS cannot size them. `patch-vdh-mobile-ui.py` now also injects
  `TOOLBAR_CSS` just before the *first* `</style>` after
  `<template id="toolbar-template">` (balance-preserving; the inserted comment
  must not contain a literal `<style>` string or the balance check trips).
- Numbers (measured in the real extension at 412px wide): icons 26x26 (stock
  16-18), buttons 48x44 with 7x9 padding (stock ~30), bar padding 10x12, gap
  10, body side padding 10px, plus 8px inner margins on scroll content.
- Overflow lesson: 7 buttons at 46px + gaps do not fit 412px on one line, and
  the stock bar clips instead of wrapping, so the last button was cut off at
  the screen edge. `flex-wrap: wrap` + `justify-content: center` fixed it; the
  bar is now two rows. `scripts/check-real-extension.py` reports
  `toolbarOverflow` (scrollWidth - clientWidth) and it must be 0.
