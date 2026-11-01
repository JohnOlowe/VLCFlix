# vdh-android — Video DownloadHelper, ported to Android

Working branch for porting the browser extension packaged as
`10.6.1.2_0.7z` in [`JohnOlowe/BuildTools`](https://github.com/JohnOlowe/BuildTools)
— **Video DownloadHelper Pro 10.6.1.2**, a Manifest V3 Chrome extension — to
Android.

## Branch policy (user instruction, 2026-11-01)

This branch is an **orphan**: it shares no history and no files with `main`,
`dev`, `cpp` or `studio-ui`. It contains **Markdown files only** — no code, no
samples, no binaries from any other branch. Anything that is not a `.md` file
belongs in `BuildTools` or in a later, explicitly approved change.

## What is here

| File | Purpose |
|---|---|
| `AGENTS.md` | Standing instructions for AI coding agents — copied verbatim from the repo's rules file (identical on `main`, `cpp`, `studio-ui`; the `dev` file of the same name is unrelated notes, not the rules) |
| `CONTEXT.md` | Current state, decisions, environment, RECOVERY |
| `TURN_LOG.md` | Append-only log of what actually happened, per turn |
| `docs/ANDROID_PORT_PLAN.md` | The porting analysis: what the extension is, the four candidate routes to Android, recommendation, verification plan |

## Where the work happens

- **Assets**: `JohnOlowe/BuildTools` — the `10.6.1.2_0.7z` extension archive,
  plus `apktool_3.0.3.jar`, `android-35.jar` and `jadx-1.5.6.zip`.
- **This branch**: analysis, plans and state (Markdown only).
- **The phone**: final verification. The user's device is Android 14 with
  Termux and a working `ffmpeg` + MediaCodec hardware encode/decode setup, no
  root. Nothing in this project claims "it works on Android" until it has been
  run on that phone.

## Status

Route chosen **2026-11-01**: **A + D** — side-load the extension unmodified
into an extension-capable Chromium browser on the phone (Edge Canary first
choice; Kiwi is archived; **Firefox cannot load this archive** — it is the
Chrome build and uses Chromium-only APIs), and use the user's Termux ffmpeg
(MediaCodec hardware encode/decode) for merging and converting. Personal use
of the user's own Pro copy; no redistribution. See
`docs/ANDROID_PORT_PLAN.md` §3A/§3D and the verification plan in §7.
