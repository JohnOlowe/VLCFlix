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
