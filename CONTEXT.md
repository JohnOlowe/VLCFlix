# Project context

## Project
- Repository: `JohnOlowe/VLCFlix` (`https://github.com/JohnOlowe/VLCFlix.git`).
- Product direction confirmed by the user: modify VLC for Windows itself, adding features and changing its interface. The homepage should be Netflix-like and show previews for different videos.

## Current state
- A local clone exists at `/home/user/VLCFlix` on branch `main`.
- Fetch on 2026-10-26 found no remote branches/commits; the checkout has no local commit and no VLC source tree.
- `AGENTS.md` contains the user's standing instructions. No build system or tests have been found.
- User-provided fine-grained PAT is stored in ignored, mode-600 `/home/user/VLCFlix/.git-credentials`; its value is not repeated in state files. `.gitignore` excludes it, and local Git credential storage is configured with `credential.useHttpPath=true`.
- Commit identity set per user request: `John Olowe` and GitHub's ID-based noreply address `121398818+JohnOlowe@users.noreply.github.com`, derived from the public GitHub account ID. User authorized pushing all current files except the ignored credential file.
- Current push consists of `.gitignore`, `AGENTS.md`, `CONTEXT.md`, `TURN_LOG.md`, and `checkpoint.json`; there is no VLC source yet.

## Git workflow requested by user
- At the start of each repository-work turn, read these state files first, then fetch/check the remote. Pull/rebase remote changes when a branch exists and safe integration is possible.
- At the end of each turn that creates a meaningful change, commit and push it, then verify the remote SHA. No empty commit is needed when a turn makes no changes.
- Keep `.git-credentials` ignored and never print or copy its contents into tracked files or notes.

## Next steps
1. Stage and inspect the approved files, ensuring `.git-credentials` is excluded.
2. Commit, push to `main`, and verify the remote SHA.
3. Obtain/populate the VLCFlix source and inspect its build/test setup before implementing the homepage.

## RECOVERY
1. Read `AGENTS.md` for standing rules.
2. Read this `CONTEXT.md`, then `TURN_LOG.md`, then `checkpoint.json`.
3. Run `git status --short --branch` and `git remote -v`; then `git fetch origin` and inspect remote refs. `.git/config` may not survive a restored workspace snapshot.
4. Confirm `.git-credentials` still exists (do not display its contents) and remains ignored; it is excluded from workspace snapshots and may need re-provisioning after a restore.
5. Resume at the first item in **Next steps**.
