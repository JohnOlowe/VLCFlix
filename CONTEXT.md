# Project context — android-port

## Goal

Port **Video DownloadHelper Pro 10.6.1.2** (the Chrome MV3 extension packaged
as `10.6.1.2_0.7z` in `JohnOlowe/BuildTools`) to Android. The user asked, on
2026-11-01, *how* it can be ported; the analysis lives in
`docs/ANDROID_PORT_PLAN.md`. The route has **not** been chosen yet.

## Assets (verified 2026-11-01 by extraction, not from memory)

- `JohnOlowe/BuildTools` cloned to `/home/user/BuildTools`. Contents:
  `10.6.1.2_0.7z` (2,440,677 bytes, sha256
  `21cf3b2bbb07cf9a27510506a83921953ed29ef9506dd681754163ebfae37921`),
  `apktool_3.0.3.jar`, `android-35.jar` (Android API 35 `android.jar`),
  `jadx-1.5.6.zip`.
- 7z extracted (py7zr) to `/home/user/BuildTools/extracted/10.6.1.2_0`, 14 MB.
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

## Open questions (asked via UI on 2026-11-01)

1. **Route** — A (run the extension unmodified in an extension-capable
   Android browser), B (own GeckoView-based APK), C (native WebView
   reimplementation), D (browser + Termux/MediaCodec ffmpeg pipeline).
   See `docs/ANDROID_PORT_PLAN.md` for the tradeoffs; A(+D) recommended.
2. **PAT** to push this branch.
3. **Branch name** — currently `android-port`; rename before push if wanted.
4. **Use scope** — personal use of the user's own Pro copy, or distribution.
   The archive is the paid Pro build; redistribution of repackaged commercial
   code is out of scope for this repo and changes the advice.

## RECOVERY

1. Read `AGENTS.md` (rules), then this file, then `TURN_LOG.md`.
2. `cd /home/user/VLCFlix`; if `git remote -v` is empty, re-add `origin` from
   the URL above; `git checkout android-port` if needed.
3. If `/home/user/BuildTools` is gone, re-clone it and re-extract the 7z
   (`pip install py7zr`, then extract; verify the sha256 above).
4. Nothing is pushed until a PAT exists. Say so at the end of every turn
   until the remote SHA is verified.
5. Resume at the newest entry in `TURN_LOG.md`.
