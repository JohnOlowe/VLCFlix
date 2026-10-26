#!/usr/bin/env bash
# Restore Git identity, remote and push credentials for VLCFlix.
#
# Runs automatically at the start of a session, or on demand. It exists because
# workspace snapshots strip these credential paths BY NAME:
#
#   .git-credentials   .git/config   .netrc
#
# ...so after a restore the repo has no `origin`, no credential helper and no
# token, even though none of that was ever committed anywhere. The token lives
# in .secrets/github-token instead: git ignores that directory (so it cannot
# reach GitHub) but snapshots keep it (so it survives a restart).
#
# Safe to run repeatedly. Never prints the token.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR"

TOKEN_FILE=".secrets/github-token"
REMOTE_FILE=".gitremote"
AUTHOR_NAME="John Olowe"
AUTHOR_EMAIL="121398818+JohnOlowe@users.noreply.github.com"
DEFAULT_REMOTE="https://github.com/JohnOlowe/VLCFlix.git"

# --- remote -----------------------------------------------------------------
REMOTE_URL="$(cat "$REMOTE_FILE" 2>/dev/null || echo "$DEFAULT_REMOTE")"

if ! git remote get-url origin >/dev/null 2>&1; then
  git remote add origin "$REMOTE_URL"
  echo "added remote origin -> $REMOTE_URL"
else
  echo "remote origin present"
fi

# --- identity ---------------------------------------------------------------
git config user.name "$AUTHOR_NAME"
git config user.email "$AUTHOR_EMAIL"

# --- token ------------------------------------------------------------------
if [[ ! -f "$TOKEN_FILE" ]]; then
  echo "no $TOKEN_FILE — commits will work, pushes will not." >&2
  echo "Write a fine-grained PAT (Contents: read/write on this repo) into $TOKEN_FILE." >&2
  exit 0
fi

chmod 600 "$TOKEN_FILE"

# Build the https://user:token@host/owner/repo.git line that `store` expects.
USER_PART="${REMOTE_URL#https://}"      # github.com/JohnOlowe/VLCFlix.git
HOST_PATH="${USER_PART%%/*}/$(echo "$USER_PART" | cut -d/ -f2)/$(echo "$USER_PART" | cut -d/ -f3)"

umask 077
CRED_FILE=".git-credentials"
printf 'https://%s:%s@%s\n' "$(echo "$USER_PART" | cut -d/ -f2)" \
  "$(tr -d '\n' < "$TOKEN_FILE")" "$HOST_PATH" > "$CRED_FILE"
chmod 600 "$CRED_FILE"

git config credential.helper "store --file=$REPO_DIR/$CRED_FILE"
git config credential.useHttpPath true

echo "credentials restored from $TOKEN_FILE (mode 600, git-ignored)"
