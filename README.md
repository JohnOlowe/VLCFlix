# vdh-android — Video DownloadHelper, ported to Android

Working branch for porting the browser extension packaged as
`10.6.1.2_0.7z` in [`JohnOlowe/BuildTools`](https://github.com/JohnOlowe/BuildTools)
— **Video DownloadHelper Pro 10.6.1.2**, a Manifest V3 Chrome extension — to
Android.


## What is on this branch

This branch is the VDH (Video DownloadHelper Pro 10.6.1.2) Android port. It
holds source only:

| path | what |
| --- | --- |
| `extension/` | pristine unpacked VDH Pro 10.6.1.2 tree (vendor drop, unmodified, diffable against upstream) |
| `scripts/format-vdh-source.py` | reformats every `.js`/`.html` in `extension/`; verified behaviour-neutral (identical token stream / tag sequence) |
| `scripts/patch-vdh-mobile-ui.py` | copies `extension/` to `build/extension` and applies the Android UI patch |
| `scripts/patch-vdh-text-size.py` | adds the manual text-size control (Settings -> Text size, 5-15px) |
| `scripts/render-check-vdh-ui.py` | renders the patched panel in headless Chromium at phone size and asserts the layout |
| `scripts/strip-vendor-links.py` | makes the build self-contained: vendor links → inert host, premium/account/review UI hidden, store links and vendor content scripts removed, extension renamed |
| `scripts/pack-vdh-artifacts.sh` | packs `build/extension` into `dist/` (crx + two zips) with integrity checks |
| `docs/` | port plan, screenshots of the on-device UI |

`build/`, `dist/` and `.secrets/` are ignored: the crx/zip are rebuilt on
every change and would otherwise sit in git history forever (force-push is
unavailable here). The signing key is never committed.

```bash
python3 scripts/format-vdh-source.py     # optional; in-place, verified, --check to dry run
python3 scripts/patch-vdh-mobile-ui.py   # vendor tree -> build/extension
python3 scripts/patch-vdh-text-size.py   # manual base text size (default 21px)
python3 scripts/strip-vendor-links.py    # no vendor links, no upsell UI
python3 scripts/render-check-vdh-ui.py   # layout assertion (needs playwright)
bash    scripts/pack-vdh-artifacts.sh    # -> dist/*.crx, dist/*.zip
```

The signing key keeps the extension ID stable
(`gmppgmnjccgklfklpibfkejdjijlnlnd`), so installing a new build replaces the
old one in place.

## Text size

The phone UI is built around one number: `--vdh-ui-base-font`, the root font
size in CSS pixels. Every other size in the stylesheet is expressed in `rem`,
so changing that one property rescales the whole panel, the sidebar and the
history page. **Settings -> Text size** sets it (slider, steppers, or Reset):
the range is **5-15px and 15px is the default and the maximum**.

15px is the ceiling on purpose. It is the size that looks the way the panel did
before `15e176f` gave the pages a viewport meta tag: without one, a phone laid
the panel out at ~980px and scaled that down to the screen, so 21 CSS px reached
the eye at roughly 15. Once the pages declared `width=device-width` that scaling
disappeared and everything got correspondingly bigger. Going below 15 is for
screens where even that is too large.

The chosen size lives in `localStorage` (read synchronously, so a saved size is
applied before the first paint) and in `chrome.storage.local` (durable, and the
channel that resizes already-open pages such as the embedded history view).

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
