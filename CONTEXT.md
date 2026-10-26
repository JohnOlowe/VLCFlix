# Project context

## Project
- Repository: `JohnOlowe/VLCFlix` (`https://github.com/JohnOlowe/VLCFlix.git`).
- Product direction confirmed by the user: modify VLC for Windows itself, adding features and changing its interface. The homepage should be Netflix-like and show previews for different videos.

## Current state
- A local clone exists at `/home/user/VLCFlix` on branch `main`.
- The initial setup commit was pushed to `origin/main`; verified remote SHA at that point was `913b93d8253e9a82335e5111ab37db3be4f456b8`.
- The pushed files are `.gitignore`, `AGENTS.md`, `CONTEXT.md`, `TURN_LOG.md`, and `checkpoint.json`. `.git-credentials` was excluded and remains ignored.
- No VLC source files, build system, or tests are present in the checkout yet.
- User-provided fine-grained PAT is stored in ignored, mode-600 `/home/user/VLCFlix/.git-credentials`; its value is not repeated in state files. Local Git credential storage uses `credential.useHttpPath=true`. Successful push verified authentication and push access.
- Commit identity configured as `John Olowe` and `121398818+JohnOlowe@users.noreply.github.com` (GitHub ID-based noreply format derived from public account ID, per user's request for generic email).

## Git workflow requested by user
- At the start of each repository-work turn, read these state files first, then fetch/check the remote. Pull/rebase remote changes when a branch exists and safe integration is possible.
- At the end of each turn that creates a meaningful change, commit and push it, then verify the remote SHA. No empty commit is needed when a turn makes no changes.
- Keep `.git-credentials` ignored and never print or copy its contents into tracked files or notes.

## Next steps
1. Obtain/populate the VLCFlix source in the local checkout and inspect the README, project structure, and build/test setup.
2. Define a verifiable first homepage-preview milestone before editing.

## RECOVERY
1. Read `AGENTS.md` for standing rules.
2. Read this `CONTEXT.md`, then `TURN_LOG.md`, then `checkpoint.json`.
3. Run `git status --short --branch` and `git remote -v`; then `git fetch origin` and inspect remote refs. `.git/config` may not survive a restored workspace snapshot.
4. Confirm `.git-credentials` still exists (do not display its contents) and remains ignored; it is excluded from workspace snapshots and may need re-provisioning after a restore.
5. Resume at the first item in **Next steps**.
