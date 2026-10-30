# Development notes

- Keep application sources, tests, build files and packaging at the repository root.
- Make focused changes, run the relevant checks and report what remains unverified.
- Do not commit credentials, private configuration, build output or release binaries.
- Preserve executable permissions on scripts and keep user library data safe.
- Read `CONTEXT.md` and `TURN_LOG.md`, when present, before continuing a task.
- Treat playback, touchpad and installer behaviour on a user's Windows machine as
  unverified unless that person confirms it.

## Git pushes and the temporary secret

The GitHub secret is **not part of the repository** and must be assumed unavailable
in a new agent session. When authentication is needed, ask the repository owner to
supply a temporary token file **via this chat**; do not search for old copies or
assume an earlier token still works. Prefer a chat file attachment containing only
the token. If it is pasted as text, do not repeat it; if it cannot be saved without
putting it into a logged command or tool input, stop and request an attachment or
another safer delivery method.

Request a short-lived, fine-grained token limited to this repository, with only the
permissions needed for the task (normally Contents read/write). The user will rotate
it once the work is done; they may also revoke it. Remind them, and do not claim
rotation has happened until they confirm it.

Keep the token in the ignored `.secrets/github-token` file and use the ignored
`.secrets/git-credential.sh` helper to give it to Git. Before copying an attachment,
check that both paths are ignored and untracked. Create `.secrets/` with mode `700`
and the token file with mode `600`; do not overwrite an existing file without the
owner's permission. Never print the token, paste it into commands, tool inputs,
commit messages, a remote URL, Git configuration or logs, or commit the files.
Never run the helper directly: its output contains the secret for Git.

If the helper is also unavailable, an agent may create this token-free script at
`.secrets/git-credential.sh` (with mode `600`); Git runs it through `/bin/sh`:

```sh
#!/bin/sh
[ "${1:-}" = get ] || exit 0
token_file="$(dirname "$0")/github-token"
[ -s "$token_file" ] || exit 1
printf 'username=x-access-token\npassword='
tr -d '\r\n' < "$token_file"
printf '\n\n'
```

Push only the intended branch using the clean HTTPS URL and the helper. For `dev`:

```sh
export VLCFLIX_CREDENTIAL_HELPER="$(git rev-parse --show-toplevel)/.secrets/git-credential.sh"
GIT_TERMINAL_PROMPT=0 git -c credential.helper= \
  -c 'credential.helper=!/bin/sh "$VLCFLIX_CREDENTIAL_HELPER"' \
  push https://github.com/JohnOlowe/VLCFlix.git HEAD:refs/heads/dev
```

Check the staged paths before committing, use a normal fast-forward push, and compare
the remote branch SHA from `git ls-remote` with `git rev-parse HEAD`. Use the same
clean URL. Never force push or update a different branch without the owner's explicit
permission. If
credentials are missing, expired or rejected, ask the owner rather than putting a
secret in the URL or retrying in a way that exposes it. Afterward, remove temporary
copies created by the agent when permitted and remind the owner to rotate the token.

**Risk:** Chat messages, attachments, tool inputs, logs and workspace copies may be
retained or accessed depending on the platform; neither deletion nor confidentiality
can be guaranteed. `.gitignore` prevents ordinary Git additions, not disk access,
backups or accidental disclosure. A narrow, short-lived token and prompt rotation
reduce the exposure window but do not make sharing a credential through chat safe.
