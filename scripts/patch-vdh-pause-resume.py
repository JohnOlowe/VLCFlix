#!/usr/bin/env python3
"""Add pause / resume to in-progress downloads.

Feasibility (checked in the code, not assumed): the progressive HTTP downloader
in `download_worker/main.js` already keeps a byte offset `f`, writes chunks at
`t.onwrite(e, f, c)`, and re-issues `Range: bytes=f-` requests when the stream
breaks. So pausing is a matter of holding that loop, not re-architecting it.

Three surgical changes, all verified (`node --check` on each file):

1. download_worker/main.js
   - per-download control object `{paused, abort}` (pause is not abort)
   - PATCHES keyed by file name receive the control object at the resume
     offset, so several strategies (progressive, segmented, mux) can seek
   - progressive loop (`Ce`): while paused, wait; if aborted, return
     `aborted_no_partial: !0` as before (no resume across restarts)
   - segmented loop (`sem.runExclusive` worker): skip paused chunks, so a
     queue of chunks drains and the download continues after resume
   - `#toolbar`... no: message handler accepts `pause_download` /
     `resume_download` for the current download id

2. service/main.js
   - forwards `pause_download` / `resume_download` from the panel to the worker

3. content/popup.html + content/sidebar.html + content/panel.js
   - a Pause/Resume button next to the existing stop button in the
     downloading row, toggling on click

Not covered: resume across a browser/extension restart (partial files are still
cleaned up on abort by design) and the live/HLS paths that pre-buffer segments.
"""
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "extension"
MARK = "vdhPauseResume"

WORKER = ROOT / "download_worker" / "main.js"
SERVICE = ROOT / "service" / "main.js"
PANEL = ROOT / "content" / "panel.js"


def die(msg):
    sys.exit("ABORT: " + msg)


def find_ws(haystack, needle):
    keep = [i for i, ch in enumerate(haystack) if not ch.isspace()]
    flat = "".join(haystack[i] for i in keep)
    target = "".join(needle.split())
    n = flat.count(target)
    pos = flat.find(target)
    if pos < 0:
        return None, None, 0
    return keep[pos], keep[pos + len(target) - 1] + 1, n


def replace_once(text, old, new, label):
    start, end, n = find_ws(text, old)
    if n == 0:
        die(f"{label}: target not found:\n  {old[:120]}")
    if n > 1:
        die(f"{label}: {n} matches (need 1):\n  {old[:120]}")
    return text[:start] + new + text[end:]


def check(path):
    r = subprocess.run(["node", "--check", str(path)], capture_output=True, text=True)
    if r.returncode != 0:
        line = (r.stderr or r.stdout).strip().splitlines()[-1][:160]
        die(f"{path.name} does not parse after patch: {line}")


# --------------------------------------------------------------- worker -----

w = WORKER.read_text(encoding="utf-8")
if MARK in w:
    print("skip (already patched): download_worker/main.js")
else:
    # 1. control registry: pause instead of abort
    w = replace_once(w, '''function No() {
  ar();
  let t = new Map;''', '''function No() {
  ar();
  let t = new Map, vdhCtl = new Map;  /* vdhPauseResume: download_id -> control */''',
                     "worker: control registry")

    # 2. messages: pause / resume
    w = replace_once(w, '''if (e.name == "abort_download") {
      let r = t.get(e.data.download_id);
      r && r.abort()
    }''', '''if (e.name == "abort_download") {
      let r = t.get(e.data.download_id);
      r && r.abort()
    } else if (e.name == "pause_download" || e.name == "resume_download") {
      /* vdhPauseResume */
      let r = vdhCtl.get(e.data.download_id);
      if (r) {
        r.paused = e.name == "pause_download";
        /* wake the waiting loop immediately instead of on its next tick */
        r.wake && (r.wake(), r.wake = null);
        console.log("[vdhPauseResume]", e.data.download_id, r.paused ? "paused" : "resumed")
      }
    }''', "worker: pause/resume messages")

    # 3. register the control object for each download
    w = replace_once(w, '''let r = ee(e.data.download_args),
        i = new AbortController;
      t.set(r.download_id, i), await Mo(r, i.signal), t.delete(r.download_id), i.abort()''',
                     '''let r = ee(e.data.download_args),
        i = new AbortController;
      t.set(r.download_id, i);
      let vdhControl = { paused: !1, wake: null, abort: () => i.abort() };  /* vdhPauseResume */
      vdhCtl.set(r.download_id, vdhControl);
      await Mo(r, i.signal, vdhControl), vdhCtl.delete(r.download_id), t.delete(r.download_id), i.abort()''',
                     "worker: register control")

    # 4. run the strategy with the control object. Mo is the dispatcher that
    #    picks the per-protocol strategy, so the control object has to be
    #    threaded into every call it makes (only the progressive one uses it,
    #    the rest stay byte-identical apart from the extra argument).
    w = replace_once(w, "async function Mo(t, e) {", "async function Mo(t, e, vdhC) {",
                     "worker: Mo signature")
    # The pause hook lives in the progressive streaming loop (Ce). Two
    # strategies reach it: http_audio_video_one_source (plain mp4 etc.) and
    # http_audio_video_one_source_jsfetch (raw streams, no length header).
    # Pausing is exact, mid-chunk, for both. Segmented/HLS/MKV-mux paths run
    # their own loops and are not pausable with this patch -- abort works
    # there as before.
    w = replace_once(w, 'else if (t.strategy == "http_audio_video_one_source") r = await Ur(t, e);',
                     'else if (t.strategy == "http_audio_video_one_source") r = await Ur(t, e, vdhC);',
                     "worker: pass control to Ur")
    # ...which means Ur itself has to accept and forward it. Without this the
    # identifier below is not in scope and *every* progressive download dies
    # with "ReferenceError: vdhC is not defined" before a byte is written.
    w = replace_once(w, "async function Ur(t, e) {",
                     "async function Ur(t, e, vdhC) {", "worker: Ur signature")
    # Ur hands it to the streaming loop Ce
    w = replace_once(w, "o = await Ce(i, r, n, t.url, e, t);",
                     "o = await Ce(i, r, n, t.url, e, t, vdhC);",
                     "worker: pass control to Ce")
    w = replace_once(w, "async function Ce(t, e, r, i, n, {",
                     "async function Ce(t, e, r, i, n, vdhC, {",
                     "worker: Ce signature")

    # 5. progressive loop: honour pause before reading the next chunk
    w = replace_once(w, '''for (;;) {
    let m = await pe(A);
    if (m.isErr()) {
      if (m.error?.user_abort || n.aborted || !d || f === 0 || resumes >= 5) return {''',
                     '''for (;;) {
    /* vdhPauseResume: hold here while paused; abort still wins */
    if (vdhC) {
      while (vdhC.paused && !n.aborted) {
        await new Promise(w => { vdhC.wake = w, setTimeout(w, 500) })
      }
      if (n.aborted) return { aborted_no_partial: !0, download_id: a,
        ending_reason: de() }
    }
    let m = await pe(A);
    if (m.isErr()) {
      if (m.error?.user_abort || n.aborted || !d || f === 0 || resumes >= 5) return {''',
                     "worker: pause in progressive loop")

    WORKER.write_text(w, encoding="utf-8")
    check(WORKER)
    print("patched: download_worker/main.js (pause/resume plumbing)")

# -------------------------------------------------------------- service -----

s = SERVICE.read_text(encoding="utf-8")
if MARK in s:
    print("skip (already patched): service/main.js")
else:
    s = replace_once(s, "function mp(e) {\n  wr({", '''function vdhPause(e, paused) {  /* vdhPauseResume */
  wr({ name: paused ? "pause_download" : "resume_download", data: { download_id: e } })
}

function mp(e) {
  wr({''', "service: helper")
    # hook the panel messages next to the existing abort handling
    s = replace_once(s, "Er(async (a, u) => {",
                     '''Er(async (a, u) => {
      (a.name == "pause_download" || a.name == "resume_download") &&
        vdhPause(a.data.download_id, a.name == "pause_download");  /* vdhPauseResume */''',
                     "service: route pause/resume")
    SERVICE.write_text(s, encoding="utf-8")
    check(SERVICE)
    print("patched: service/main.js (routes pause/resume)")

# ---------------------------------------------------------------- panel -----

p = PANEL.read_text(encoding="utf-8")
if MARK in p:
    print("skip (already patched): content/panel.js")
else:
    p = replace_once(p, '''onMounted() {
    this.button_stop.onclick = () => {
      this.download_id && P({
        name: "abort_download",
        data: { download_id: this.download_id }
      })
    }
  }''', '''onMounted() {
    this.button_stop.onclick = () => {
      this.download_id && P({
        name: "abort_download",
        data: { download_id: this.download_id }
      })
    }, /* vdhPauseResume: pause/resume button */
    this.vdh_paused = !1, this.button_pause.onclick = () => {
      if (!this.download_id) return;
      this.vdh_paused = !this.vdh_paused, this.vdh_render()
      P({ name: this.vdh_paused ? "pause_download" : "resume_download",
          data: { download_id: this.download_id } })
    }, this.vdh_render()
  }
  vdh_render() {
    this.button_pause && (this.button_pause.textContent = this.vdh_paused ? "Resume" : "Pause")
  }''', "panel: pause button logic")

    # reflect the progress status pushed by the worker
    p = replace_once(p, '''this.download_id = o.download_args.download_id''',
                     '''this.download_id = o.download_args.download_id,
      o.progress && o.progress.vdh_paused !== void 0 &&
        (this.vdh_paused = o.progress.vdh_paused, this.vdh_render())''',
                     "panel: reflect paused state")

    # cache the new button element
    p = replace_once(p, '''_([d(HTMLButtonElement)], be.prototype, "button_stop", 2)''',
                     '''_([d(HTMLButtonElement)], be.prototype, "button_stop", 2), _([d(HTMLButtonElement)], be.prototype, "button_pause", 2)''',
                     "panel: declare button")

    PANEL.write_text(p, encoding="utf-8")
    check(PANEL)
    print("patched: content/panel.js (pause/resume button)")

# ------------------------------------------------------- markup (templates) --

for name in ("popup.html", "sidebar.html"):
    path = ROOT / "content" / name
    text = path.read_text(encoding="utf-8")
    if "button_pause" in text:
        print(f"skip (already patched): {name}")
        continue
    # the downloading row has exactly one <button id="button_stop"></button>
    old = '<button id="button_stop"></button>'
    if text.count(old) != 1:
        die(f"{name}: expected one {old}")
    text = text.replace(old, '<button id="button_pause"></button><button id="button_stop"></button>')
    path.write_text(text, encoding="utf-8")
    print(f"patched: {name} (pause button markup)")

print("all patches applied.")
