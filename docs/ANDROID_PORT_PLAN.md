# Port plan — Video DownloadHelper Pro 10.6.1.2 → Android

Question being answered: *how can this extension be ported to Android?*
Written under `AGENTS.md` — every factual claim below is labelled
**verified** (I ran/read it, or a named source says so), **assumed**, or
**unverified** (needs a test before anyone relies on it).

---

## 1. What we are porting (verified by extraction, 2026-11-01)

`10.6.1.2_0.7z` (sha256 `21cf3b2b…e37921`) unpacks to **Video DownloadHelper
Pro 10.6.1.2**, a **Manifest V3** Chrome extension:

| Part | What it is | Port consequence |
|---|---|---|
| `service/main.js` (557 KB, minified) + `emscripten-module.wasm` | background **service worker** | needs an MV3-capable engine; not patchable by hand casually — changes to it mean patching a minified bundle |
| `injected/*.js` (23 content scripts, ISOLATED + **MAIN** world; youtube, vimeo, facebook/instagram, vk, ok.ru, bilibili, kick, canva, …) | the actual value: per-site stream sniffing | needs `content_scripts` + MAIN-world injection support — this is what rules out plain Android WebView |
| `download_worker/` — **libav.js 6.5.7.1 (h264/aac/mp3)** wasm | in-browser muxing of separate video+audio (DASH) parts | the natural replacement point on Android: real `ffmpeg` with **MediaCodec** does this better (all codecs, hardware, no 4 GB wasm memory ceiling) |
| `offscreen` documents, `sidePanel` | Chromium-only APIs | **assumed** the main adaptation cost in any non-Chromium port (Gecko has `sidebar_action`, not `sidePanel`; `offscreen` does not exist there) |
| `webRequest` + `declarativeNetRequest`, `<all_urls>`, `downloads`, `scripting` | network sniffing and saving | supported in both Chromium-Android forks and Gecko, with behavioural differences (**unverified** for this extension specifically) |

There is no Android build in the archive — it is the desktop Chrome package.

## 2. What Android actually offers (facts, with sources)

- **Chrome for Android runs no extensions** — no extension API exists in the
  mobile build. (Well-established; Google has never shipped it.)
- **Firefox for Android runs WebExtensions**, and VDH publishes an official
  add-on there — *verified* on addons.mozilla.org, which advertises "works
  on both desktop and Android / Available on Firefox for Android".
- **Kiwi Browser** (Chromium fork) supported loading MV2 **and MV3**
  extensions from zip/crx; it is **reported archived since Jan 2025** and its
  extension engine is **reported** to live on in **Microsoft Edge Canary for
  Android**, which can load extensions from zip/crx/user.js. (Community
  sources — apkpure changelog, mobilism forum. **Unverified on a device.**)
- **Android WebView cannot run extensions** — there is no API for it in any
  form. Any "our own app" route therefore either embeds a real browser engine
  (GeckoView) or reimplements the extension's job natively.

## 3. The four routes

### Route A — run the extension unmodified in an extension-capable browser
*(no code; hours, not weeks)*

Install **Microsoft Edge Canary** (or Kiwi, unmaintained) on the phone, zip
the extracted `10.6.1.2_0/` folder, load it via the browser's
Extensions → Developer → *Load from zip* path.

- **Why it can work**: the archive is a standard unpacked MV3 extension;
  these browsers are Chromium and accept that format.
- **Risks (all unverified until tried on the phone)**: completeness of MV3
  support in Edge Canary's implementation (`offscreen`, `sidePanel`, service
  worker persistence); Pro/licence activation being bound to a recognised
  host; store-level gating of side-loaded extensions.
- **Verification plan**: user installs Edge Canary, side-loads the zip, we
  test one DASH site (YouTube) and one HLS site end to end; result goes in
  `TURN_LOG.md`.
- **Note**: Firefox for Android is *not* a target for **this** archive — its
  official VDH build is a Gecko-flavoured package, not the Chrome one we
  have. Route A means Chromium hosts.

### Route B — our own APK embedding GeckoView
*(the way VDH's official Android presence works; weeks; standalone app)*

Build a small Android app whose UI is a **GeckoView** surface, install the
extension as a built-in add-on via `WebExtensionController.ensureBuiltIn()`,
and adapt the extension where Chromium-only APIs are used.

- **Adaptation list (assumed complete until a Gecko run says otherwise)**:
  `sidePanel` → `sidebar_action` (or drop it — the phone UI can host the
  panel natively); `offscreen` document uses → move that work to the
  background script or to native code; `downloads` → Gecko downloads API
  differences; MV3 background semantics — Gecko's MV3 story centres on event
  pages; service-worker-as-background support must be checked against the
  Gecko version chosen before committing.
- **Patch target is a minified 557 KB bundle** — feasible (string-level
  shims, injected prelude scripts) but fragile across VDH updates.
- **The ffmpeg upgrade belongs here**: replace the libav.js wasm muxer with
  native ffmpeg — bundle an ffmpeg build (ffmpeg-kit was retired in Jan 2025;
  alternatives: maintained community builds, or our own NDK build) and mux
  with `-c copy`, transcode with `-hwaccel mediacodec` /
  `-c:v h264_mediacodec` when needed. An app can exec binaries from **its own
  data dir** — no root needed — which is exactly the Termux trick, in-app.
- **Costs**: Gecko runtime adds roughly 80–100 MB to the APK (**assumed**,
  to be measured); GeckoView API churn means pinning a version.
- **Build in this sandbox**: plausible without full Android SDK — JDK 11 +
  `android-35.jar` from BuildTools + downloaded `aapt2`/`d8`/`apksigner` +
  GeckoView AAR from Maven. **Unverified**; first task if this route is
  chosen. No device here — the user's phone is the only place it can be
  proven.

### Route C — native reimplementation (WebView + Kotlin)
*(months; not recommended)*

Own app, Android WebView, inject the sniffers with `evaluateJavascript`,
intercept requests, parse M3U8/MPD natively, download segments with OkHttp,
mux with bundled ffmpeg.

- WebView has **no MAIN-world injection** equivalent, and the per-site
  scripts are the extension's whole value — they hook page objects before the
  site's player does. Reimplementing 20+ site adapters that break monthly is
  a treadmill, and `shouldInterceptRequest` does not see everything
  `webRequest` sees (media fetched by the page's own JS/service workers).
- Only worth it if the goal is a product independent of VDH's code — which
  also solves the licence question in §5.

### Route D — hybrid: browser extension + Termux/MediaCodec pipeline
*(days; uses what the user already runs)*

Detection and download stay in a browser with extension support (Route A, or
official VDH on Firefox for Android). The **merge/convert** step — where the
desktop extension leans on its wasm muxer or companion app — moves to the
user's existing Termux ffmpeg:

```sh
# mux separate DASH video+audio, no re-encode:
ffmpeg -i video.mp4 -i audio.m4a -c copy out.mp4
# transcode with hardware MediaCodec, no root, Android 14:
ffmpeg -hwaccel mediacodec -i in.mkv -c:v h264_mediacodec -b:v 5M out.mp4
```

- Glue options, least to most work: manual (download to shared storage, run
  in Termux), `Termux:API`/`termux-open`, or `RUN_COMMAND` intents from a
  tiny helper app (requires Termux's `allow-external-apps` setting).
- **Verified** by the user's own report: MediaCodec hwaccel works in their
  Termux on Android 14 without root. **Unverified**: the automation glue on
  their exact device.

## 4. Recommendation

**Start with A, add D when merging matters; choose B only if a standalone
branded APK is the actual goal; skip C.**

- A is the only route that can be *proven on the phone this week* with zero
  new code, and its failure modes are cheap to discover.
- D turns the user's existing, already-working MediaCodec ffmpeg into the
  extension's backend — strictly better than the wasm muxer.
- B is the "real port" (it is architecturally what VDH's own Android app is),
  but it is a weeks-long project gated on Gecko MV3 verification, and it only
  makes sense once A/D has proven which sites and workflows actually matter
  to the user.

## 5. Licence — stated plainly, once

The archive is the **paid Pro** build of a commercial extension. Personal use
of the user's own licensed copy is the user's call and this plan assumes it.
**Redistributing** it — a repackaged APK, a public repo with the extracted
sources — would be redistribution of MiConvert's code and is out of scope for
this repository. If the goal is a distributable app, the honest path is Route
C (independent implementation) or a conversation with the vendor.

## 6. Decisions (answered by the user, 2026-11-01)

1. **Route: A + D** — side-load unmodified + Termux/MediaCodec pipeline.
2. PAT supplied via the question UI; stored untracked at
   `.secrets/github-token`; rotate when the work is done (it passed through
   chat).
3. Branch renamed to **`vdh-android`** before the first push.
4. **Personal use** of the user's own Pro copy — §5 redistribution limit
   stands.

One clarification the user's answer raised, answered here: this archive
**cannot** be loaded in Firefox for Android — `sidePanel`, `offscreen` and
the MV3 service-worker background are Chromium-only, and Gecko would refuse
or mis-run them. The VDH that runs on Firefox is the vendor's separate Gecko
build on AMO. For *this* 7z the hosts are Chromium-based: **Edge Canary**
(maintained, inherited Kiwi's extension loader), **Kiwi** (works, archived
since Jan 2025 — reported, unverified on device), or similar Chromium forks
(Mises, Lemur). The desktop "companion app" the user remembered is not
needed here: merging happens in-extension via the wasm muxer, and route D
replaces that step with Termux ffmpeg anyway.

## 7. Verification plan for whichever route is picked (`AGENTS.md` §4/§5)

1. Pick the route → verify: decision recorded in `CONTEXT.md`, pushed.
2. Route A/D: zip the extracted extension → verify: user side-loads on the
   phone and reports extension icon + detected streams on one DASH and one
   HLS site; Termux mux of a downloaded pair → file plays.
3. Route B: hello-world GeckoView APK builds in this sandbox (`aapt2`+`d8`
   against `android-35.jar`) → verify: APK installs on the user's phone and
   loads a page; only then start the extension adaptation.
4. Every "works on Android" claim in this repo gets the device, the date and
   the reporter attached, or it is labelled unverified.
