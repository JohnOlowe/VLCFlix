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
                       '''import { fetchSubtitleTrack, probeSubtitleRendition, renditionsFromMedia, safeLanguage } from "./vdh-subtitles.js";

var VDH_JOURNAL_KEY = "vdh_journal",''', "service: import the subtitle module")

    # 2. helper: hand a WebVTT document to the browser's download manager
    svc = replace_once(svc, "async function vdhJournalPartialSize(download_id) {",
                       '''/** Write one subtitle document as <subdir><basename>.<lang>.vtt */
async function vdhSaveSubtitleFile(rendition, basename, subdir) {
  let track = await fetchSubtitleTrack({
    url: rendition.url,
    headers: rendition.headers || []
  });
  let name = (basename || "subtitles") + "." + safeLanguage(rendition.language) + ".vtt";
  /* No URL.createObjectURL here: this runs in an MV3 service worker, where that
     API does not exist. chrome.downloads accepts a data: URL, and a subtitle
     document is small, so the whole file travels inline. */
  let url = "data:text/vtt;charset=utf-8," + encodeURIComponent(track.text);
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

print("all patches applied.")
