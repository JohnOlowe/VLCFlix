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
