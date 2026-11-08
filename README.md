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

**Everything else follows it.** The side margins, the spacing unit, the video
thumbnail height and the toolbar (icons, targets, padding and gap) are all
proportional to the base size with small floors, so the whole layout shrinks
with the text instead of the text shrinking inside a fixed frame. Two of those
were pinned rather than proportional before, which is why nothing appeared to
move: the spacing floors were the old 21px values themselves, and the toolbar
was floored at a 44px target / 26px icon, so seven icons could never fit one
line. They now do -- on a 720px screen at every size, and on a 360px one from
11px down.

**The footer stays put.** `com-main` is the only child of the panel that grows
and shrinks (`min-height: 0`, without which a flex child refuses to shrink below
its content and pushes the page taller than the screen), and `com-toolbar` is
pinned with `position: sticky; bottom: 0`, so Settings, History and the rest are
always on screen while the media list scrolls. The page itself does not scroll.

Nothing is allowed to be wider than the screen: the history rows are built from
web components, so the rules that reach the Play / Retry / Delete buttons and
the media row's own `flex-shrink: 0` are injected into the component templates
(`patch_media_templates`). `scripts/check-fit.py` asserts it, with real seeded
rows, at 360px and 720px and at every text size.

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

## Subtitles: HLS and DASH

Both stream types are supported, and both end up as a valid `.vtt` (or `.srt`,
if that is the chosen format) named after the movie and the language.

- **HLS** (`.m3u8`): the renditions listed by `EXT-X-MEDIA` are read, the
  segments of the chosen one are fetched, and cues are shifted by each
  segment's timeline position so the file is continuous.
- **DASH** (`.mpd`): the manifest is parsed as XML. Text `AdaptationSet`s are
  recognised by `contentType`, `mimeType`, codecs (`stpp`, `wvtt`, `webvtt`) or
  a `subtitle`/`caption` Role; `BaseURL`s are concatenated up the tree; and a
  `SegmentTemplate` is walked, either through its `SegmentTimeline`
  (`S@t`/`S@d`/`S@r`, with `$Number$`/`$Time$`/`$RepresentationID$` filled in)
  or from a fixed `duration` bounded by the Period length. TTML, which DASH
  carries far more often than HLS does, is converted to WebVTT.

  Why this was missing for so long: the app's DASH parser records a subtitle as
  a Representation **id**, never a URL, because on desktop the track is muxed by
  ffmpeg rather than fetched. Anything that keeps only renditions with a URL
  drops every DASH track before it is ever shown. The id is now resolved
  against the manifest into a `dash:<manifest>#<id>` reference that can be
  fetched.

**Captions that are not in the stream.** Some sites never mention their
subtitles in the playlist or manifest; the page just downloads a subtitle file
of its own when captions are switched on. Tubi is the extreme case: its HLS
playlists carry no `EXT-X-MEDIA:SUBTITLES` at all, and the captions are one
plain `.srt` on another host (`s.adrise.tv`). No manifest parser can see that,
so the extension also watches the network: any `.vtt`, `.srt` or `.ttml`
response the page fetches is remembered with the tab it came from and offered
alongside the manifest's own renditions, marked as page-fetched. Where the URL
says nothing about the language (Tubi's does not), the page is asked which
caption track is showing.

Because the file only exists once the player has fetched it, **turn captions on
in the player before starting the download** — or start the download, turn them
on, then open History. Only the tracks the page actually loaded can be offered.

**One case is refused on purpose:** WebVTT wrapped in MP4 boxes (`wvtt`) is
binary — the cues live in mdats and reading them needs an ISOBMFF parser. It is
reported as *"WebVTT in an MP4 container (wvtt) cannot be read as plain text"*
instead of being written out as a `.vtt` that is really an MP4. A "save all"
still saves every other language and reports that one.

Subtitles can be saved from a detected download (the CC badge and the
**Subtitles: xx** buttons on its History row) or, for a stream that was never
downloaded, from the address box in **Settings → Subtitles**, which takes
either an `.m3u8` or an `.mpd` address.

## Partial files: why they used to disappear

The vendor's own startup routine deletes **every** file in the extension's
private (OPFS) storage each time the service worker starts:

```js
Promise.allSettled(n.map(r => (console.warn(`main::purging ${r}`), i.removeEntry(r))))
```

That is why a partial download never survived a crash, a browser restart or a
service-worker restart: the bytes were on disk, the worker started, the sweep
deleted them. The journal entry in `chrome.storage.local` survived, which is why
History kept offering a download there were no bytes left for.

The sweep now keeps any file whose name belongs to a download the journal still
has a live record for — those are exactly the ones that can be resumed — and
sweeps the orphans. `scripts/check-partials.py` proves it across a real browser
restart, and `navigator.storage.persist()` is requested on top of the
`unlimitedStorage` permission the manifest already declares.

Two things storage cannot survive: **uninstalling or reinstalling the
extension** (the browser deletes the origin's data) and, on Edge Canary
Android, a **browser restart that removes a side-loaded extension**. Nothing in
the extension can prevent either.
