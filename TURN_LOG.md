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
