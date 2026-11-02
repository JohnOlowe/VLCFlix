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
