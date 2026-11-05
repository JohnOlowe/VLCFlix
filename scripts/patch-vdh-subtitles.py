#!/usr/bin/env python3
"""Download subtitle renditions as sidecar .vtt files.

HLS exposes subtitles as EXT-X-MEDIA:TYPE=SUBTITLES renditions; DASH/MPD as text
AdaptationSets. The vendor bundle parses them (`Vm`) and, on the ffmpeg muxed
paths, embeds one track into the container -- but it never writes a separate
file, and it cannot re-fetch them later. This adds:

  service (new module `service/vdh-subtitles.js`, imported by service/main.js)
    - fetchSubtitleTrack(): rendition URI -> one stitched WebVTT document
      (handles multi-segment playlists, X-TIMESTAMP-MAP offsets, EXT-X-MAP,
      refuses encryption / byte ranges instead of writing a broken file)
    - message "vdh_subtitle_save": {url, language, basename, headers?} writes
      the document through chrome.downloads as
        <subdir><basename>.<language>.vtt
    - message "vdh_subtitle_save_all": same for every rendition of a download,
      using the rendition list recorded in the journal
  journal
    - the rendition list travels with the download record (written in Hy, where
      the serialized media payload is available), so the history page can offer
      the subtitles days later, after a crash, without the original tab
  history page
    - per-download rows grow a "Subtitles" section with one button per language
      when the record has renditions

Usage: scripts/patch-vdh-subtitles.py [tree]
"""
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "extension"
MARK = "vdhSubtitles"
SERVICE = ROOT / "service" / "main.js"
HISTORY = ROOT / "content" / "history.js"
PANEL_JS = ROOT / "content" / "panel.js"
MODULE = ROOT / "service" / "vdh-subtitles.js"


def die(msg):
    sys.exit("ABORT: " + msg)


def find_ws(haystack, needle):
    keep = [i for i, ch in enumerate(haystack) if not ch.isspace()]
    flat = "".join(haystack[i] for i in keep)
    target = "".join(needle.split())
    pos = flat.find(target)
    if pos < 0:
        return None, None, 0
    return keep[pos], keep[pos + len(target) - 1] + 1, flat.count(target)


def replace_once(text, old, new, label):
    start, end, n = find_ws(text, old)
    if n == 0:
        die(f"{label}: target not found:\n  {old[:160]}")
    if n > 1:
        die(f"{label}: {n} matches (need exactly 1):\n  {old[:160]}")
    return text[:start] + new + text[end:]


def check_module(path):
    """Module scripts must be parsed as modules; `node --check x.js` is CommonJS."""
    tmp = pathlib.Path("/tmp") / (path.parent.name + "-" + path.name + ".mjs")
    tmp.write_text(pathlib.Path(path).read_text(encoding="utf-8"), encoding="utf-8")
    r = subprocess.run(["node", "--check", str(tmp)], capture_output=True, text=True)
    if r.returncode != 0:
        die(f"{path.name}: does not parse: " + " | ".join((r.stderr or r.stdout).splitlines()[:3]))


if not MODULE.exists():
    die("service/vdh-subtitles.js is missing (copy it from extension/ first)")
check_module(MODULE)

svc = SERVICE.read_text(encoding="utf-8")
if MARK in svc:
    print("skip (already patched): service/main.js")
else:
    # 1. import the module. service/main.js is a module script (manifest says so).
    svc = replace_once(svc, 'var VDH_JOURNAL_KEY = "vdh_journal",',
                       '''import { applySubtitleFormat, fetchSubtitleTrack, probeSubtitleRendition, renditionsFromMedia, renditionsFromPlaylist, safeLanguage } from "./vdh-subtitles.js";

var VDH_JOURNAL_KEY = "vdh_journal",''', "service: import the subtitle module")

    # 2. helper: hand a WebVTT document to the browser's download manager
    svc = replace_once(svc, "async function vdhJournalPartialSize(download_id) {",
                       '''/* vdhSubtitles: the user's format and placement choices.
     format     "keep"  - exactly what the stream served, no conversion
                "smart" - SubRip only when nothing meaningful would be lost
                "srt"   - always SubRip
     placement  "file"  - save <name>.<lang>.vtt next to the movie
                "embed" - merge into the already downloaded movie file */
var VDH_SUBS_SETTINGS_KEY = "vdh_subtitle_settings",
  VDH_SUBS_DEFAULTS = { format: "keep", placement: "file" };

async function vdhSubtitleSettings() {
  try {
    let got = await Vt.default.storage.local.get(VDH_SUBS_SETTINGS_KEY),
      saved = got[VDH_SUBS_SETTINGS_KEY] || {};
    return Object.assign({}, VDH_SUBS_DEFAULTS, saved);
  } catch (vdhE) { return Object.assign({}, VDH_SUBS_DEFAULTS) }
}

/** Write one subtitle document as <subdir><basename>.<lang>.<vtt|srt> */
async function vdhSaveSubtitleFile(rendition, basename, subdir) {
  let track = await fetchSubtitleTrack({
      url: rendition.url,
      headers: rendition.headers || []
    }),
    settings = await vdhSubtitleSettings(),
    /* the format choice is applied here, at save time, so changing the setting
       also applies to renditions recorded long before it was changed */
    out = applySubtitleFormat(track.text, settings.format),
    name = (basename || "subtitles") + "." + safeLanguage(rendition.language) + "." + out.ext;
  console.log("[vdhSubtitles] format", settings.format, "->", out.ext,
              out.converted ? "(converted)" : "(as served)",
              out.reasons.length ? "kept vtt for: " + out.reasons.join(", ") : "");
  if (settings.placement === "embed" && !subdir) {
    /* no movie file to attach to yet (nothing downloaded): a sidecar is the
       only way to keep the result, so say so and still write the file */
    console.log("[vdhSubtitles] embed requested, writing sidecar as well");
  }
  /* No URL.createObjectURL here: this runs in an MV3 service worker, where that
     API does not exist. chrome.downloads accepts a data: URL, and a subtitle
     document is small, so the whole file travels inline.
     A deliberate generic MIME type: Chrome derives the saved file's kind from
     it, and "text/vtt" makes it force the name to .vtt (and reject .srt). The
     real extension is on the filename, which is what players act on. */
  let url = "data:application/octet-stream;charset=utf-8," + encodeURIComponent(out.text);
  let id = await Vt.default.downloads.download({
    url: url,
    filename: (subdir || "") + name,
    conflictAction: "uniquify",
    saveAs: false
  });
  console.log("[vdhSubtitles] wrote", name, "from", track.segments, "segment(s),",
              track.bytesIn, "bytes in");
  return { ok: !0, filename: (subdir || "") + name, browser_download_id: id,
           segments: track.segments };
}

async function vdhJournalPartialSize(download_id) {''', "service: subtitle writer")

    # 3. record the renditions with the download record, in Hy() where the
    #    serialized media payload is available
    svc = replace_once(svc, '''  try {  /* vdhJournal: durable record of the job, before any transfer */
    await vdhJournalPut("dl:" + n.download_id, {
      download_id: n.download_id, status: "running",''',
                       '''  try {  /* vdhJournal: durable record of the job, before any transfer */
    await vdhJournalPut("dl:" + n.download_id, {
      download_id: n.download_id, status: "running",
      /* vdhSubtitles: keep the rendition list so the history page can offer the
         subtitles later, even after a crash or a browser restart */
      subtitles: (r && r.subtitles && r.subtitles.isSome && r.subtitles.isSome())
        ? r.subtitles.value.map(vdhS => ({
            url: vdhS.url && vdhS.url.href ? vdhS.url.href : String(vdhS.url || ""),
            language: vdhS.language, type: vdhS.type
          }))
        /* fall back to the detected media payload: if the vendor parser missed
           the renditions, the history page can still recover them from the
           serialized media object that Retry already replays */
        : renditionsFromMedia(e.data.media),''', "service: journal keeps subtitle renditions")

    # 4. messages: save one rendition, or all of them for a recorded download
    svc = replace_once(svc, '''      if (a.name == "vdhJournal_retry") {  /* vdhJournal: retry after a crash */''',
                       '''      if (a.name == "vdh_subtitle_probe") {  /* vdhSubtitles */
        try {
          let got = await Vt.default.storage.local.get(VDH_JOURNAL_KEY),
            rec = (got[VDH_JOURNAL_KEY] || {})[a.data.key] || {},
            list = Array.isArray(rec.subtitles) ? rec.subtitles : [],
            unavailable = [];
          for (let i = 0; i < list.length; i++) {
            let vdhProbe = await probeSubtitleRendition({ url: (list[i] || {}).url });
            if (!vdhProbe.ok) {
              unavailable.push(i);
              console.warn("[vdhSubtitles] not offered:", list[i] && list[i].language,
                           "-", vdhProbe.reason);
            }
          }
          /* the verdict goes in the journal, so it is paid for once and the
             history page re-renders from it (storage.onChanged) */
          await vdhJournalPut(a.data.key, { subtitles_unavailable: unavailable });
          console.log("[vdhSubtitles] probed", a.data.key, "-", unavailable.length,
                      "of", list.length, "not savable");
        } catch (vdhE) { console.error("[vdhSubtitles] probe failed", vdhE) }
        return;
      }
      if (a.name == "vdh_subtitle_settings_get") {  /* vdhSubtitles */
        try {
          let settings = await vdhSubtitleSettings();
          await Vt.default.storage.local.set({ vdh_subtitle_settings_reply: settings });
          console.log("[vdhSubtitles] settings", JSON.stringify(settings));
        } catch (vdhE) { console.error("[vdhSubtitles] settings read failed", vdhE) }
        return;
      }
      if (a.name == "vdh_subtitle_settings_set") {  /* vdhSubtitles */
        try {
          let next = await vdhSubtitleSettings(),
            patch = a.data || {};
          if (["keep", "smart", "srt"].indexOf(patch.format) >= 0) next.format = patch.format;
          if (["file", "embed"].indexOf(patch.placement) >= 0) next.placement = patch.placement;
          await Vt.default.storage.local.set({ vdh_subtitle_settings: next });
          /* echo back through the same reply key the page listens on */
          await Vt.default.storage.local.set({ vdh_subtitle_settings_reply: next });
          console.log("[vdhSubtitles] settings saved", JSON.stringify(next));
        } catch (vdhE) { console.error("[vdhSubtitles] settings write failed", vdhE) }
        return;
      }
      if (a.name == "vdh_subtitle_save_url") {  /* vdhSubtitles
        Save a rendition for a stream that was never downloaded. Nothing about
        the movie file is needed: a sidecar subtitle comes from the subtitle
        playlist alone. */
        try {
          let settings = await vdhSubtitleSettings(),
            base = a.data.basename || "subtitles",
            out = await vdhSaveSubtitleFile({ url: a.data.url, language: a.data.language },
                                            base, a.data.subdir || "");
          await Vt.default.storage.local.set({ vdh_subtitle_reply: {
            ok: true, at: Date.now(), filename: out.filename,
            language: a.data.language, placement: settings.placement
          }});
          console.log("[vdhSubtitles] saved by url", out.filename);
        } catch (vdhE) {
          console.error("[vdhSubtitles] save by url failed", vdhE);
          await Vt.default.storage.local.set({ vdh_subtitle_reply: {
            ok: false, at: Date.now(), error: String(vdhE && vdhE.message || vdhE)
          }});
        }
        return;
      }
      if (a.name == "vdh_subtitle_renditions_from_url") {  /* vdhSubtitles */
        try {
          let res = await fetch(a.data.url, { credentials: "include" }),
            text = await res.text(),
            list = renditionsFromPlaylist(text, a.data.url);
          await Vt.default.storage.local.set({ vdh_subtitle_found: {
            at: Date.now(), url: a.data.url, renditions: list,
            error: res.ok ? null : "HTTP " + res.status
          }});
          console.log("[vdhSubtitles] found", list.length, "rendition(s) for", a.data.url);
        } catch (vdhE) {
          console.error("[vdhSubtitles] playlist lookup failed", vdhE);
          await Vt.default.storage.local.set({ vdh_subtitle_found: {
            at: Date.now(), url: a.data.url, renditions: [],
            error: String(vdhE && vdhE.message || vdhE)
          }});
        }
        return;
      }
      if (a.name == "vdh_subtitle_save") {  /* vdhSubtitles */
        try {
          let got = await Vt.default.storage.local.get(VDH_JOURNAL_KEY),
            rec = (got[VDH_JOURNAL_KEY] || {})[a.data.key] || {},
            rendition = (rec.subtitles || [])[a.data.index];
          if (!rendition || !rendition.url) {
            console.warn("[vdhSubtitles] no rendition", a.data.key, a.data.index);
            return;
          }
          let base = (rec.filename || "subtitles").replace(/\\.[A-Za-z0-9]{1,5}$/, "");
          let out = await vdhSaveSubtitleFile(rendition, base, rec.subdir || "");
          await vdhJournalPut(a.data.key, {
            subtitles_saved: [...new Set([...((rec.subtitles_saved) || []),
                                          safeLanguage(rendition.language)])]
          });
          console.log("[vdhSubtitles] saved", out.filename);
        } catch (vdhE) {
          console.error("[vdhSubtitles] save failed", vdhE);
        }
        return;
      }
      if (a.name == "vdh_subtitle_save_index") {  /* vdhSubtitles */
        try {
          let got = await Vt.default.storage.local.get(VDH_JOURNAL_KEY),
            rec = (got[VDH_JOURNAL_KEY] || {})[a.data.key] || {},
            rendition = (rec.subtitles || [])[a.data.index];
          if (!rendition || !rendition.url) {
            console.warn("[vdhSubtitles] no rendition for index", a.data.index);
            return;
          }
          await vdhSaveSubtitleFile(rendition, a.data.basename || "subtitles",
                                    rec.subdir || "");
        } catch (vdhE) {
          console.error("[vdhSubtitles] save failed", vdhE);
        }
        return;
      }
      if (a.name == "vdhJournal_retry") {  /* vdhJournal: retry after a crash */''',
                       "service: subtitle messages")

    SERVICE.write_text(svc, encoding="utf-8")
    check_module(SERVICE)
    print("patched: service/main.js (subtitle writer + messages + journal)")

hist = HISTORY.read_text(encoding="utf-8")
if MARK in hist:
    print("skip (already patched): content/history.js")
else:
    # one button per *savable* rendition, inside the existing per-download row.
    # The service probes each rendition once and records the verdict in the
    # journal, so an encrypted or unsupported track is never offered at all.
    hist = replace_once(hist, '''      let del = document.createElement("button");
      del.textContent = "Delete";''',
                        '''      /* vdhSubtitles: list only the renditions we can actually save */
      let renditions = Array.isArray(e.subtitles) ? e.subtitles : [],
        probed = Array.isArray(e.subtitles_unavailable),
        unavailable = probed ? e.subtitles_unavailable : [];
      for (let i = 0; i < renditions.length; i++) {
        let r = renditions[i];
        if (!r || !r.url) continue;
        if (unavailable.indexOf(i) >= 0) continue;  /* encrypted / unsupported */
        let sub = document.createElement("button");
        sub.textContent = "Subtitles: " + (r.language || "?");
        sub.onclick = async () => {
          sub.disabled = !0;
          sub.textContent = "Fetching\\u2026";
          try {
            await chrome.runtime.sendMessage({
              msg: { name: "vdh_subtitle_save", data: { key, index: i } },
              channel: 1  /* FromContentToService */
            });
            sub.textContent = "Subtitles: " + (r.language || "?") + " \\u2713";
          } catch (vdhE) {
            sub.textContent = "Subtitles: " + (r.language || "?") + " (failed)";
            console.warn("[vdhSubtitles] send failed", vdhE);
          }
          setTimeout(() => {
            sub.disabled = !1;
            sub.textContent = "Subtitles: " + (r.language || "?");
          }, 2500);
        };
        row.appendChild(sub);
      }
      if (renditions.length && !probed) {
        /* ask the service which of these can be saved; the answer comes back
           through storage.onChanged, which re-runs this render */
        chrome.runtime.sendMessage({
          msg: { name: "vdh_subtitle_probe", data: { key } },
          channel: 1  /* FromContentToService */
        }).catch(() => {});
      }
      let del = document.createElement("button");
      del.textContent = "Delete";''', "history: subtitle buttons")

    HISTORY.write_text(hist, encoding="utf-8")
    subprocess.run(["node", "--check", str(HISTORY)], check=False)
    print("patched: content/history.js (per-language subtitle buttons)")

# ------------------------------------------------- settings + url entry (UI) --

# ------------------------------------------------- subtitles UI in the panel --
# Both are the user's own cards in the panel's Settings view, which is the only
# place a settings-looking block belongs. history.html gets neither: that page is
# the download history, and the panel is where the user actually reads Settings.

SUBS_SETTINGS_BLOCK = '''<vbox id="vdh_subs_settings_box" class="roundedbox">
        <h2>Subtitles</h2>
        <p>Subtitles are fetched from the stream itself. Nothing about the movie file is needed, so you can save them without downloading (or re-downloading) anything.</p>
        <label><input type="radio" name="vdh_subs_format" value="keep"/> Keep the subtitle format exactly as it is in the stream</label>
        <label><input type="radio" name="vdh_subs_format" value="smart"/> Smart convert - use SRT only when no styling or positioning is lost, otherwise keep VTT</label>
        <label><input type="radio" name="vdh_subs_format" value="srt"/> Always convert to SRT, no matter what</label>
        <label><input type="checkbox" id="vdh_subs_embed"/> Embed the subtitles into the movie file instead of saving a separate file next to it (only possible while the extension still holds the movie, i.e. a download that is running or was interrupted. A movie that already finished belongs to the browser, and the extension cannot write into it, so a separate file is saved instead)</label>
        <p id="vdh_subs_settings_note">Nothing is downloaded for a separate file: the subtitles come from the stream's own subtitle track.</p>
      </vbox>'''

# One line collapsed, the rest only when opened. A <details> element does the
# opening natively, so it still works if the page's script has not run yet.
SUBS_URL_BLOCK = '''<vbox id="vdh_subs_url_box" class="roundedbox">
        <details id="vdh_subs_url_details">
          <summary><span class="vdh_subs_url_name">Get subtitles from a stream address</span></summary>
          <p>Paste the address of an HLS stream (a .m3u8). Subtitle languages found in it are listed below.</p>
          <input id="vdh_subs_url" placeholder="https://example.com/master.m3u8"/>
          <input id="vdh_subs_movie_name" placeholder="Movie file name (optional), e.g. Lecture 4.mp4"/>
          <hbox>
            <button id="vdh_subs_url_find">Find subtitles</button>
            <button id="vdh_subs_url_save_all">Save all languages</button>
          </hbox>
          <vbox id="vdh_subs_url_results"></vbox>
        </details>
      </vbox>'''

SUBS_CSS = '''<style>/* vdhSubtitles: one card for the settings, one collapsed line for the
   address box. Kept inside the panel's settings styling (same rounding, same
   spacing) so it does not look bolted on. */
#vdh_subs_settings_box, #vdh_subs_url_box {
  padding: 10px;
  margin-top: var(--spacing-medium);
}
#vdh_subs_settings_box h2, #vdh_subs_url_box h2 { margin: 0 0 6px; }
#vdh_subs_settings_box label {
  display: flex;
  gap: 10px;
  align-items: flex-start;
  margin: 6px 0;
  line-height: 1.35;
}
#vdh_subs_url_details > summary {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  font-weight: bold;
  font-size: 1.05em;
  list-style: none;
  padding: 2px 0;
  min-height: 34px;
}
#vdh_subs_url_details > summary::-webkit-details-marker { display: none; }
#vdh_subs_url_details > summary::after {
  content: "\\25BE";
  margin-left: auto;
  font-weight: normal;
  transition: transform .15s;
}
#vdh_subs_url_details[open] > summary::after { transform: rotate(180deg); }
/* the panel styles its own boxes, and an author rule such as vbox{display:flex}
   beats the browser's own "hide the body of a closed details", so say it here
   too: collapsed really is one line, nothing peeking out */
#vdh_subs_url_details:not([open]) > *:not(summary) {
  display: none !important;
}
#vdh_subs_url_details > *:not(summary) { margin-top: 8px; }
#vdh_subs_url_details p { margin: 0 0 6px; }
#vdh_subs_url_details[open] { padding-bottom: 2px; }
#vdh_subs_url, #vdh_subs_movie_name {
  width: 100%;
  max-width: 100%;
  box-sizing: border-box;
}
/* the panel lays its boxes out as flex containers, so content that is wider
   than the card is not squeezed into it: pin everything to the card instead of
   letting the fields run off the right edge */
#vdh_subs_settings_box, #vdh_subs_url_box, #vdh_subs_url_details {
  min-width: 0;
  max-width: 100%;
  box-sizing: border-box;
}
#vdh_subs_url_details { display: block; width: 100%; }
#vdh_subs_url_details > * { max-width: 100%; box-sizing: border-box; }
#vdh_subs_url_details hbox {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 8px;
}
#vdh_subs_settings_box p, #vdh_subs_url_box p, #vdh_subs_url_box label {
  max-width: 100%;
  overflow-wrap: anywhere;
}
</style>'''

for page in ("popup.html", "sidebar.html"):
    PAGE = ROOT / "content" / page
    text = PAGE.read_text(encoding="utf-8")
    if "vdh_subs_settings_box" in text:
        print(f"skip (already patched): content/{page} subtitles cards")
        continue
    anchor = '<h2 data-i18n="settings_history_title"></h2>'
    if text.count(anchor) != 1:
        die(f"{page}: expected exactly one settings_title heading to place them before")
    # Both cards sit together ahead of the app's own history card, and the style
    # ships inside the same tree: the settings view is a custom element with its
    # own shadow root, so a <style> in <head> would never reach these nodes.
    text = text.replace(anchor, SUBS_CSS + "\n\n      " + SUBS_SETTINGS_BLOCK +
                        "\n\n      " + SUBS_URL_BLOCK + "\n\n      " + anchor, 1)
    PAGE.write_text(text, encoding="utf-8")
    print(f"patched: content/{page} (subtitles card + collapsed address box)")

# history.html must not keep an older copy of the settings card
HIST_HTML = ROOT / "content" / "history.html"
html = HIST_HTML.read_text(encoding="utf-8")
if "vdh_subs_settings_box" in html:
    import re as _re
    html2 = _re.sub(r'\s*<vbox id="vdh_subs_settings_box".*?</vbox>\s*', "\n      ",
                    html, count=1, flags=_re.S)
    assert html2 != html, "history.html: could not remove the old subtitles box"
    HIST_HTML.write_text(html2, encoding="utf-8")
    print("moved out of content/history.html: subtitles settings card")

# ---------------------------------------------------------------- settings js --

JS_BLOCK = r'''
/* vdhSubtitles: settings panel and "subtitles from a stream address".
   Deliberately independent of any download: a sidecar .vtt/.srt is produced
   from the stream's subtitle playlist alone, so a finished movie is never
   needed. Results come back through storage, which keeps this page simple. */
var VDH_SUBS_UI = {
  keys: { settings: "vdh_subtitle_settings", reply: "vdh_subtitle_settings_reply",
          found: "vdh_subtitle_found", saved: "vdh_subtitle_reply" },
  found: [],
  /* the settings view is a custom element with its own shadow root, so a plain
     getElementById from this page never sees these controls */
  deep(id) {
    function walk(root) {
      if (root.getElementById) {
        let hit = root.getElementById(id);
        if (hit) return hit;
      }
      for (let el of root.querySelectorAll("*")) {
        if (el.shadowRoot) {
          let hit = walk(el.shadowRoot);
          if (hit) return hit;
        }
      }
      return null;
    }
    return walk(document);
  },
  async load_settings() {
    let got = await chrome.storage.local.get(this.keys.settings),
      s = Object.assign({ format: "keep", placement: "file" }, got[this.keys.settings] || {}),
      box = this.deep("vdh_subs_settings_box");
    for (let r of (box || document).querySelectorAll('input[name="vdh_subs_format"]')) {
      r.checked = r.value === s.format;
    }
    let embed = this.deep("vdh_subs_embed");
    if (embed) embed.checked = s.placement === "embed";
    this.note(s);
  },
  note(s) {
    let el = this.deep("vdh_subs_settings_note");
    if (!el) return;
    el.textContent = (s.placement === "embed")
      ? "Embedding needs the movie file itself. The extension can only do that while it still holds the movie (a running or interrupted download); for a movie that has finished, a separate file is saved next to it instead. A separate file never needs the movie at all."
      : "Nothing is downloaded for a separate file: the subtitles come from the stream's own subtitle track.";
  },
  send(msg) {
    return chrome.runtime.sendMessage({ msg: msg, channel: 1 /* FromContentToService */ });
  },
  async save_settings() {
    let format = "keep",
      box = this.deep("vdh_subs_settings_box"),
      embed = this.deep("vdh_subs_embed");
    for (let r of (box || document).querySelectorAll('input[name="vdh_subs_format"]')) {
      if (r.checked) format = r.value;
    }
    let next = { format: format, placement: (embed && embed.checked) ? "embed" : "file" };
    await this.send({ name: "vdh_subtitle_settings_set", data: next });
    this.note(next);
    console.log("[vdhSubtitles] settings:", JSON.stringify(next));
  },
  render_results() {
    let box = this.deep("vdh_subs_url_results");
    if (!box) return;
    box.textContent = "";
    if (!this.found.length) return;
    for (let i = 0; i < this.found.length; i++) {
      let r = this.found[i],
        row = document.createElement("div");
      row.className = "vdh_subs_lang";
      let label = document.createElement("span");
      label.textContent = (r.language || "?") + (r.name && r.name !== r.language ? " (" + r.name + ")" : "");
      let save = document.createElement("button");
      save.textContent = "Save " + (r.language || "?");
      save.dataset.lang = r.language || "";
      save.onclick = async () => {
        save.disabled = !0;
        save.textContent = "Saving\u2026";
        await VDH_SUBS_UI.save_one(r.language || "und", encodeURIComponent(r.url));
        save.textContent = "Save " + (r.language || "?");
        save.disabled = !1;
      };
      row.appendChild(label);
      row.appendChild(save);
      box.appendChild(row);
    }
  },
  movie_name: "",
  async find() {
    let name = this.deep("vdh_subs_movie_name");
    this.movie_name = ((name && name.value) || "").trim()
      .replace(/\.[A-Za-z0-9]{1,5}$/, "");  /* the extension is added per language */
    let input = this.deep("vdh_subs_url"),
      url = (input && input.value || "").trim();
    if (!url) return;
    let box = this.deep("vdh_subs_url_results");
    if (box) box.textContent = "Looking for subtitles\u2026";
    await this.send({ name: "vdh_subtitle_renditions_from_url", data: { url: url } });
  },
  async save_one(language, encodedUrl) {
    let url = decodeURIComponent(encodedUrl),
      base = this.movie_name || "subtitles";
    if (!this.movie_name) {
      /* fall back to the playlist file name, without its extension */
      let last = url.split("/").pop() || "subtitles";
      last = last.split("?")[0];
      let parts = last.split(".");
      if (parts.length > 1) parts.pop();
      base = parts.join(".") || "subtitles";
    }
    await this.send({ name: "vdh_subtitle_save_url",
                      data: { url: url, language: language, basename: base } });
  },
  async save_all() {
    for (let r of this.found) {
      await this.save_one(r.language || "und", encodeURIComponent(r.url));
    }
  },
  mounted: false,
  mount() {
    if (this.mounted) return true;
    let box = this.deep("vdh_subs_settings_box");
    if (!box) return false;
    this.mounted = true;
    box.hidden = false;
    for (let r of box.querySelectorAll('input[name="vdh_subs_format"]')) {
      r.onchange = () => VDH_SUBS_UI.save_settings();
    }
    let embed = this.deep("vdh_subs_embed");
    if (embed) embed.onchange = () => VDH_SUBS_UI.save_settings();
    let url = this.deep("vdh_subs_url_box");
    if (url) {
      /* the address box starts closed: one line until the user opens it */
      let det = this.deep("vdh_subs_url_details");
      if (det && !det.hasAttribute("open")) det.open = false;
    }
    let find = this.deep("vdh_subs_url_find"),
      all = this.deep("vdh_subs_url_save_all");
    if (find) find.onclick = () => VDH_SUBS_UI.find();
    if (all) all.onclick = () => VDH_SUBS_UI.save_all();
    this.load_settings();
    this.watch_storage();
    return true;
  },
  /* Settings is built the first time it is opened, so wait for the card to
     exist rather than assuming it is already in the page */
  watch_dom() {
    if (this.mount()) return;
    let obs = new MutationObserver(() => {
      if (VDH_SUBS_UI.mount()) obs.disconnect();
    });
    obs.observe(document.documentElement, { childList: true, subtree: true });
    document.addEventListener("click", () => { VDH_SUBS_UI.mount(); });
  },
  watch_storage() {
    if (this.storage_watched) return;
    this.storage_watched = true;
    chrome.storage.onChanged.addListener((ch) => {
      if (ch[VDH_SUBS_UI.keys.found]) {
        let v = ch[VDH_SUBS_UI.keys.found].newValue || {},
          box2 = VDH_SUBS_UI.deep("vdh_subs_url_results");
        VDH_SUBS_UI.found = Array.isArray(v.renditions) ? v.renditions : [];
        if (!VDH_SUBS_UI.found.length) {
          if (box2) box2.textContent = v.error
            ? "Could not read that address: " + v.error
            : "No subtitle track in that stream.";
        } else {
          VDH_SUBS_UI.render_results();
        }
      }
      if (ch[VDH_SUBS_UI.keys.saved]) {
        let v = ch[VDH_SUBS_UI.keys.saved].newValue || {},
          box3 = VDH_SUBS_UI.deep("vdh_subs_url_results");
        if (box3 && v.at) {
          let line = document.createElement("p");
          line.textContent = v.ok
            ? "Saved " + v.filename
            : "Could not save: " + v.error;
          box3.appendChild(line);
        }
      }
      if (ch[VDH_SUBS_UI.keys.settings]) VDH_SUBS_UI.load_settings();
    });
  }
};
try { VDH_SUBS_UI.watch_dom() } catch (vdhE) { console.warn("[vdhSubtitles] settings mount failed", vdhE) }
'''

# The cards are the panel's own settings view now, so the code that drives them
# has to be loaded by the panel. history.js is only loaded by history.html, which
# no longer carries either card.
panel = PANEL_JS.read_text(encoding="utf-8")
if "VDH_SUBS_UI" in panel:
    print("skip (already patched): content/panel.js settings ui")
else:
    panel = panel.rstrip("\n") + "\n" + JS_BLOCK
    PANEL_JS.write_text(panel, encoding="utf-8")
    check_module(PANEL_JS)
    print("patched: content/panel.js (settings ui + stream address)")

# when the panel embeds this page, its own Back banner would be a second Back
embedded = HISTORY.read_text(encoding="utf-8")
MARK = "vdh_embedded"
if MARK in embedded:
    print("skip (already patched): content/history.js embedded mode")
else:
    embedded = embedded.rstrip("\n") + "\n" + r'''
/* vdhHistoryPanel: when the panel shows this page inside itself, the overlay
   owns the Back button, so hide this page's own banner and title. */
try {
  if (location.search.indexOf("vdh_embedded=1") >= 0) {
    /* this page has no Back banner of its own; only its big title duplicates
       the overlay's bar, so hide just that */
    var vdhTitle = document.querySelector("#main > h1");
    if (vdhTitle) vdhTitle.setAttribute("hidden", "true");
    document.documentElement.setAttribute("vdh_embedded", "1");
  }
} catch (vdhE) {}
'''
    HISTORY.write_text(embedded, encoding="utf-8")
    check_module(HISTORY)
    print("patched: content/history.js (embedded mode hides its own Back)")

print("vendored: service/vdh-subtitles.js")
print("all patches applied.")
