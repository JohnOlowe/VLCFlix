#!/usr/bin/env python3
"""Crash recovery + blob cleanup for the history page.

The problem, concretely
-----------------------
1. The history list (`transient_history`) lives in the extension's *in-memory*
   session state. Every service-worker start clears it, so after a crash or a
   phone OOM kill there is no record of what was downloading: nothing to retry.
2. Partial files live in OPFS (the extension's private storage). They are only
   removed on clean paths; nothing in the UI can free them. Several hundred MB
   can silently sit in there.
3. There is no space report anywhere in the UI.

What this patch adds
--------------------
Service (`service/main.js`)
  * `vdh_journal` (in `chrome.storage.local`, durable across crashes):
        "dl:<download_id>": { download_id, download_args, filename, status,
                              bytes, percent, media_hash, created_at, updated_at }
    written when a download starts, updated (throttled, <=1 per 2 s) while it
    progresses, marked done/failed/cancelled when it ends.
  * `vdh_last_purge`: bytes reclaimed by the startup OPFS sweep, so the UI can
    say how much space recovering from the last crash freed.
  * `vdhJournal_retry` message: rebuilds the download from the stored args and
    re-runs the existing pipeline (`iu`) with a fresh download id.

History page (`content/history.js` + `content/history.html`)
  * A "Storage" box listing every journal entry with size/status and
    Retry / Delete buttons, a "Purge leftover files" button, and a live report
    of OPFS usage vs device quota.
  * The page reads `chrome.storage.local` and OPFS *directly* (same origin, same
    extension id) and redraws on `chrome.storage.onChanged`, so no new
    request/response plumbing is needed. Delete/purge act on
    `chrome.downloads` and OPFS directly; only Retry needs the service worker.

Usage: scripts/patch-vdh-download-journal.py [tree]
"""
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "extension"
MARK = "vdhJournal"
SERVICE = ROOT / "service" / "main.js"
WORKER_FILE = ROOT / "download_worker" / "main.js"
HISTORY = ROOT / "content" / "history.js"
HISTORY_HTML = ROOT / "content" / "history.html"


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
        die(f"{label}: target not found:\n  {old[:160]}")
    if n > 1:
        die(f"{label}: {n} matches (need exactly 1):\n  {old[:160]}")
    return text[:start] + new + text[end:]


def check_js(path):
    """Parse the file the way the browser will.

    `node --check foo.js` treats the file as CommonJS, which hides real module
    errors (a stray closing comment marker produced a SyntaxError that CommonJS
    parsing never reached). Module scripts are therefore checked as .mjs.
    """
    path = pathlib.Path(path)
    if "module" in path.read_text(encoding="utf-8", errors="ignore").split("export {")[0][-40:] \
            or path.name == "main.js":
        tmp = pathlib.Path("/tmp") / (path.parent.name + "-" + path.name + ".mjs")
        tmp.write_text(path.read_text(encoding="utf-8"), encoding="utf-8")
        r = subprocess.run(["node", "--check", str(tmp)], capture_output=True, text=True)
    else:
        r = subprocess.run(["node", "--check", str(path)], capture_output=True, text=True)
    if r.returncode != 0:
        detail = (r.stderr or r.stdout).strip().splitlines()
        die(f"{path.name}: does not parse: " + " | ".join(detail[:3])[-260:])


# ------------------------------------------------------------------ service --

svc = SERVICE.read_text(encoding="utf-8")
if MARK in svc:
    print("skip (already patched): service/main.js")
else:
    HELPERS = '''
/* ===================== vdhJournal (patch-vdh-download-journal) =============
   Durable download journal. The app's own history is in-memory session state
   and dies with the service worker, which is why a crashed download used to
   leave no trace. This one lives in storage.local. */
var VDH_JOURNAL_KEY = "vdh_journal",
  VDH_PURGE_KEY = "vdh_last_purge",
  vdhJournalLastWrite = 0;

async function vdhJournalPut(key, patch) {
  let got = await Vt.default.storage.local.get(VDH_JOURNAL_KEY),
    journal = got[VDH_JOURNAL_KEY] || {},
    rec = Object.assign({}, journal[key] || { id: key, created_at: Date.now() }, patch,
                        { updated_at: Date.now() });
  journal[key] = rec;
  await Vt.default.storage.local.set({ [VDH_JOURNAL_KEY]: journal });
  return rec;
}
/* how many bytes of this download survived on disk (0 when nothing did) */
async function vdhJournalPartialSize(download_id) {
  if (!download_id) return 0;
  let total = 0;
  try {
    let dir = await navigator.storage.getDirectory();
    for await (let name of dir.keys()) {
      if (name !== download_id && !name.startsWith(download_id + ".") &&
          !name.startsWith(download_id + "_")) continue;
      try { total += (await (await dir.getFileHandle(name)).getFile()).size || 0 } catch {}
    }
  } catch (vdhE) {}
  return total;
}

async function vdhJournalDropFiles(download_id) {
  if (!download_id) return 0;
  let freed = 0;
  try {
    let dir = await navigator.storage.getDirectory();
    for await (let name of dir.keys()) {
      if (name !== download_id && !name.startsWith(download_id + ".") &&
          !name.startsWith(download_id + "_")) continue;
      try { freed += (await (await dir.getFileHandle(name)).getFile()).size || 0 } catch {}
      await dir.removeEntry(name).catch(() => {});
    }
  } catch (vdhE) {}
  return freed;
}
/* Re-create a queued download from a journal record. Runs in its own async
   context so the job keeps going after the message handler returns; the task
   scheduler is internal to that context, so it is built fresh here. */
async function vdhJournalRetry(rec) {
  let meta = JSON.parse(JSON.stringify(rec.meta));
  /* a crash may have taken the tab with it: retarget to a live tab */
  let tabs = await Vt.default.tabs.query({});
  let want = meta && meta.__serde_val && meta.__serde_val.tab_id &&
             meta.__serde_val.tab_id.__serde_val;
  let usable = t => t && t.url && !/^(about:|chrome|edge|devtools)/.test(t.url);
  let hit = tabs.find(t => t.id === want && usable(t)) ||
            tabs.find(usable) ||
            tabs.find(t => t.id === want) || tabs[0];
  if (!hit) throw new Error("no tab to attach the download to");
  if (meta && meta.__serde_val && meta.__serde_val.tab_id) {
    meta.__serde_val.tab_id = { __serde_tag: "primitive", __serde_val: hit.id };
  }
  /* Keep whatever survived on disk and tell the worker to continue from its
     size. The download id is reused so that file is picked up as-is. */
  let args = JSON.parse(JSON.stringify(rec.download_args));
  let kept = await vdhJournalPartialSize(rec.download_id);
  if (kept) console.log("[vdhJournal] resuming", rec.download_id,
                        "from byte", kept);
  else console.log("[vdhJournal] no partial file left, starting fresh");
  console.log("[vdhJournal] retrying", rec.download_id, rec.filename);
  let n = Math.max(1, U().max_concurrent_downloads || 1);
  /* Hy(payload, <unused>, scheduler): the scheduler is the third argument */
  await Hy({ data: {
    download_args: args, media: rec.media, meta: meta
  } }, null, new In(n, n, 0));
}
/* ===================== end vdhJournal helpers ============================ */
'''
    svc = replace_once(svc, "function mp(e) {", HELPERS.strip() + "\n\nfunction mp(e) {",
                       "service: journal helpers")

    # (a) durable record of the job, taken inside Hy(): that is where the
    #     serialized download_args/media/meta payloads exist, so Retry can replay
    #     the original request instead of rebuilding (and re-serializing) it.
    svc = replace_once(svc, '''let n = ze(e.data.download_args),
    r = ze(e.data.media),
    o = ze(e.data.meta);''', '''let n = ze(e.data.download_args),
    r = ze(e.data.media),
    o = ze(e.data.meta);
  try {  /* vdhJournal: durable record of the job, before any transfer */
    await vdhJournalPut("dl:" + n.download_id, {
      download_id: n.download_id, status: "running",
      download_args: e.data.download_args,  /* replayed verbatim by Retry */
      media: e.data.media, meta: e.data.meta,
      filename: (n.subdir || "") + n.good_basename + "." + n.extension,
      media_hash: r && r.hash, url: n.url && n.url.href, bytes: 0, percent: 0
    });
  } catch (vdhE) { console.warn("[vdhJournal] start write failed", vdhE) }''',
                       "service: journal start")

    # (b) throttled progress updates, so a crash leaves a size to report
    svc = replace_once(svc, '''      let H = R.data.progress;''',
                       '''      let H = R.data.progress;
        /* vdhJournal: keep the durable record roughly current */
        try {
          let vdhNow = Date.now();
          /* module scope, not window: this is a service worker */
          if (vdhNow - vdhJournalLastWrite > 2000 && H.status == "downloading") {
            vdhJournalLastWrite = vdhNow;
            vdhJournalPut("dl:" + n.download_id, {
              status: "running", percent: H.percent,
              bytes: H.fetched_bytes_count
            });
          }
        } catch (vdhE) {}''', "service: journal progress")

    # (c) final state, with the file path once the browser owns it
    svc = replace_once(svc, '''    $(), w();
    let W = await x;''', '''    $(), w();
    try {  /* vdhJournal: terminal state */
      let vdhDone = j.isOk() ? (j.value.aborted_no_partial ? "cancelled" : "done")
                             : "failed";
      await vdhJournalPut("dl:" + n.download_id, {
        status: vdhDone,
        path: j.isOk() ? j.value.path || null : null,
        browser_download_id: j.isOk() ? j.value.browser_download_id || null : null,
        error: j.isOk() ? null : String(j.error && (j.error.details || j.error))
      });
    } catch (vdhE) { console.warn("[vdhJournal] final write failed", vdhE) }
    let W = await x;''', "service: journal terminal state")

    # (d) remember how much the startup sweep reclaimed (crash cleanup report)
    svc = replace_once(svc, '''    Promise.allSettled(n.map(r => (console.warn(`main::purging ${r}`), i.removeEntry(r))))''',
                       '''    let vdhFreed = 0;
    for (let vdhName of n) {
      try {
        vdhFreed += (await (await i.getFileHandle(vdhName)).getFile()).size || 0;
      } catch {}
    }
    if (vdhFreed > 0) {
      console.warn("[vdhJournal] startup sweep reclaiming", vdhFreed, "bytes");
      try {
        await Vt.default.storage.local.set({
          [VDH_PURGE_KEY]: { bytes: vdhFreed, at: Date.now() }
        });
      } catch {}
    }
    Promise.allSettled(n.map(r => (console.warn(`main::purging ${r}`), i.removeEntry(r))))''',
                       "service: startup sweep accounting")

    # (e) retry: replay the recorded payload through the normal download entry
    #     point Hy() -- the same path the panel's Download button uses
    svc = replace_once(svc, '''      (a.name == "pause_download" ||
        a.name == "resume_download") && vdhPause(a.data.download_id, a.name == "pause_download");''',
                       '''      (a.name == "pause_download" ||
        a.name == "resume_download") && vdhPause(a.data.download_id, a.name == "pause_download");
      if (a.name == "vdhJournal_retry") {  /* vdhJournal: retry after a crash */
        try {
          let vdhGot = await Vt.default.storage.local.get(VDH_JOURNAL_KEY),
            vdhRec = (vdhGot[VDH_JOURNAL_KEY] || {})[a.data.key];
          if (!vdhRec || !vdhRec.download_args || !vdhRec.media || !vdhRec.meta) {
            console.warn("[vdhJournal] cannot retry, source request missing", a.data.key);
            return;
          }
          /* not awaited: the download outlives this message */
          vdhJournalRetry(vdhRec).catch(async vdhE => {
            console.error("[vdhJournal] retry failed", vdhE);
            try {
              await vdhJournalPut(a.data.key, {
                status: "failed", error: String(vdhE && vdhE.message || vdhE)
              });
            } catch (vdhE2) {}
          });
        } catch (vdhE) {
          console.error("[vdhJournal] retry dispatch failed", vdhE);
        }
        return;
      }''', "service: retry message")

    SERVICE.write_text(svc, encoding="utf-8")
    check_js(SERVICE)
    print("patched: service/main.js (journal, retry, sweep report)")

# ------------------------------------------------------------------- worker --

worker = WORKER_FILE.read_text(encoding="utf-8")
if "vdhResume" in worker:
    print("skip (already patched): download_worker/main.js")
else:
    # Ce() resumes a stalled stream with `Range: bytes=f-` already, but it starts
    # every job at offset 0 and opens its output file empty. A job interrupted by
    # a crash therefore had to be fetched again from the beginning. Let it start
    # at a byte offset instead, appending to whatever survived:
    #
    #   * `resume_offset` is added to the args object (Ce already destructures
    #     {headers, download_id, cache}); reading it off the destructured value
    #     leaves the existing call sites untouched.
    #   * the first request asks for `Range: bytes=<offset>-`. A 200 answer means
    #     the server ignored Range, so the offset is dropped and it writes from
    #     zero (correct, just no saving). A 206 keeps the partial file.
    #   * byte counts count from the start of the file, not from the request, so
    #     progress and the `f < d * 0.9` completeness check stay right.
    #   * a 416 with Content-Range "bytes */<total>" means the file on disk is
    #     already whole: report it complete instead of re-downloading.
    worker = replace_once(worker, """  cache: s
}, vdhC) {""", """  cache: s,
  resume_offset: vdhResumeStart
}, vdhC) {""", "worker: Ce reads resume_offset")

    # One small loop decides how the transfer begins: honour the partial file,
    # fall back to a clean start when the server will not cooperate. Everything
    # after this point is the stock streaming loop.
    worker = replace_once(worker, """  baseHdrs.delete("range");
  let l = await ne(i, {
    headers: baseHdrs,
    signal: n,
    cache: s
  });""", """  baseHdrs.delete("range");
  /* vdhResume: continue a file left behind by an interrupted run */
  let vdhOff = (vdhResumeStart && vdhResumeStart.offset) || 0,
    vdhCan = !!(vdhResumeStart && vdhResumeStart.can_resume),
    l;
  for (let vdhTry = 0;; vdhTry++) {
    if (vdhCan && vdhOff > 0) baseHdrs.set("Range", `bytes=${vdhOff}-`);
    else baseHdrs.delete("Range");
    l = await ne(i, {
      headers: baseHdrs,
      signal: n,
      cache: s
    });
    let vdhStatus = l.isOk() ? l.value.status : l.error && l.error.status;
    if (vdhCan && vdhOff > 0 && vdhStatus === 200) {
      /* server ignored Range: the whole file is coming, so write from 0 */
      console.warn("[vdhResume] server ignored Range, restarting at 0");
      vdhOff = 0;
      continue;
    }
    if (vdhCan && vdhOff > 0 && vdhStatus === 416 && vdhTry === 0) {
      /* Range Not Satisfiable: what survived does not line up with the resource,
         so a resume would corrupt it. Start over on a fresh file. */
      console.warn("[vdhResume] cannot continue the partial file, restarting");
      try { await t.remove(e) } catch {}
      try { await t.open(e) } catch {}
      vdhOff = 0, vdhCan = !1;
      continue;
    }
    break;
  }""", "worker: Ce decides how to start (resume vs clean)")
    worker = replace_once(worker, """    u = Nr(l.value),
    d;
  u.isSome() && (d = u.value);
  let f = 0,
    resumes = 0;""", """    u = Nr(l.value),
    d;
  u.isSome() && (d = u.value);
  /* a 206 body is only the remainder, so the file size is offset + length */
  if (d !== void 0 && vdhOff > 0 && l.value.status === 206) d += vdhOff;
  /* bytes already on disk count towards progress and completeness */
  let f = vdhOff,
    resumes = 0;""", "worker: sizes and offsets account for the partial file")

    worker = replace_once(worker, """      aborted_no_partial: f < 102400,""",
                          """      aborted_no_partial: f < 102400 && vdhOff === 0,""",
                          "worker: do not discard a resumed partial file")

    # L.open(): record the size already on disk and keep appends from being
    # swallowed when the surviving file is longer than what gets written back
    worker = replace_once(worker, """      n = await i.createSyncAccessHandle();
    this.filename_writable_map.set(e, {
      writable: n,
      handle: i,
      size: 0
    })""", """      n = await i.createSyncAccessHandle(),
      vdhExisting = 0;
    /* vdhResume: a file left by a crashed run is appended to, not truncated */
    try { vdhExisting = n.getSize() } catch {}
    this.filename_writable_map.set(e, {
      writable: n,
      handle: i,
      size: vdhExisting
    })""", "worker: open() reports the surviving size")

    # Ur(): decide the offset from the file that is actually there. The offset
    # has to live on the object Ce destructures, so anchor on the line after
    # open() and add it to `t` before the call.
    if "async function Ur(t, e, vdhC) {" not in worker:
        die("worker: Ur is not the pause-patched signature")
    worker = replace_once(worker, """  await i.open(r);
  let n = new q(t.download_id),""", """  await i.open(r);
  /* vdhResume: if a file with this download id is already on disk, it belongs to
     this same job (ids are unique per download) because the previous run was
     interrupted -- so continue from its size instead of starting over. No new
     field has to travel: download_args is decoded against a strict schema, so
     adding one there would make the worker reject the whole message. */
  {
    let vdhSize = i.filename_writable_map.get(r)?.size || 0;
    if (vdhSize > 0) t.resume_offset = { offset: vdhSize, can_resume: !0 };
  }
  let n = new q(t.download_id),""", "worker: Ur picks the resume offset")

    WORKER_FILE.write_text(worker, encoding="utf-8")
    check_js(WORKER_FILE)
    # a comment that closes early is invisible to a CJS parse; assert the marker
    assert "bytes */" not in worker, "comment contains a stray block-comment terminator"
    print("patched: download_worker/main.js (byte-range resume)")

# ------------------------------------------------------------------ history --

hist = HISTORY.read_text(encoding="utf-8")
if MARK in hist:
    print("skip (already patched): content/history.js")
else:
    # Page-level, not inside <com-main>: the history list itself is only created
    # once the user opts in to history, but crash recovery and blob cleanup must
    # work from a fresh profile too.
    PANEL = r"""
/* ===================== vdhJournal (write to "vdh_journal") ================
   Crash recovery + blob cleanup for the history page. Reads the durable journal
   and the extension's OPFS directory directly (same origin), so it needs no
   service round-trip; storage.onChanged keeps the list live. */
var VDH_STORAGE = {
  journal_key: "vdh_journal",
  fmt(n) {
    return n >= 1073741824 ? (n / 1073741824).toFixed(2) + " GB"
      : n >= 1048576 ? (n / 1048576).toFixed(1) + " MB"
      : n >= 1024 ? (n / 1024).toFixed(0) + " KB" : (n || 0) + " B";
  },
  async opfs() {
    let out = new Map();
    try {
      let dir = await navigator.storage.getDirectory();
      for await (let name of dir.keys()) {
        let size = 0;
        try { size = (await (await dir.getFileHandle(name)).getFile()).size || 0 } catch {}
        out.set(name, size);
      }
    } catch (vdhE) { console.warn("[vdhJournal] opfs read failed", vdhE) }
    return out;
  },
  owns(name, id) {
    return !!id && (name === id || name.indexOf(id + ".") === 0 ||
                    name.indexOf(id + "_") === 0);
  },
  async records() {
    try {
      let got = await chrome.storage.local.get([this.journal_key, "vdh_last_purge"]);
      return { journal: got[this.journal_key] || {}, purge: got.vdh_last_purge || null };
    } catch { return { journal: {}, purge: null } }
  },
  async refresh() {
    let box = document.getElementById("vdh_journal_list"),
      info = document.getElementById("vdh_storage_info");
    if (!box || !info) return;
    let { journal, purge } = await this.records(),
      files = await this.opfs(),
      bytes = 0;
    for (let size of files.values()) bytes += size;
    let est = {};
    try { est = await navigator.storage.estimate() } catch {}
    let text = "Extension storage: " + files.size + " file(s), " + this.fmt(bytes) +
      " - device " + this.fmt(est.usage || 0) + " of " + this.fmt(est.quota || 0) + " used";
    if (purge && purge.bytes) text += " - last restart reclaimed " + this.fmt(purge.bytes);
    info.textContent = text;

    let rows = Object.entries(journal).filter(([, r]) => r && r.download_id);
    rows.sort((a, b) => (b[1].updated_at || 0) - (a[1].updated_at || 0));
    box.textContent = "";
    if (!rows.length) {
      let p = document.createElement("p");
      p.textContent = "Nothing interrupted or pending.";
      box.appendChild(p);
      return;
    }
    for (let [key, e] of rows) {
      let ids = [...files.keys()].filter(n => this.owns(n, e.download_id)),
        record = journal[key],
        on_disk = ids.reduce((sum, n) => sum + (files.get(n) || 0), 0),
        row = document.createElement("div");
      row.className = "vdh_journal_row";
      let label = document.createElement("span");
      label.className = "vdh_journal_label";
      let size = e.bytes ? this.fmt(e.bytes) : (on_disk ? this.fmt(on_disk) : "0 B");
      let pct = e.percent ? " - " + Math.round(e.percent) + "%" : "";
      let state = e.status || "unknown";
      if (state === "running" && !ids.length) state = "interrupted (partial file gone)";
      label.textContent = (e.filename || e.download_id) + " - " + state +
        " - " + size + pct;
      row.appendChild(label);

      if (e.download_args && e.media && e.meta) {
        let retry = document.createElement("button");
        retry.textContent = "Retry";
        retry.onclick = async () => {
          retry.disabled = !0;
          retry.textContent = "Starting...";
          try {
            await chrome.runtime.sendMessage({
              msg: { name: "vdhJournal_retry", data: { key } },
              channel: 1  /* FromContentToService */
            });
          } catch (vdhE) { console.warn("[vdhJournal] retry send failed", vdhE) }
          setTimeout(() => {
            retry.disabled = !1;
            retry.textContent = "Retry";
            VDH_STORAGE.refresh();
          }, 2000);
        };
        row.appendChild(retry);
      }
      let del = document.createElement("button");
      del.textContent = "Delete";
      del.onclick = async () => {
        del.disabled = !0;
        del.textContent = "Deleting...";
        try {
          if (e.browser_download_id) {
            try { await chrome.downloads.removeFile(e.browser_download_id) } catch {}
            try { await chrome.downloads.erase({ id: e.browser_download_id }) } catch {}
          }
          let dir = await navigator.storage.getDirectory();
          for (let name of [...files.keys()]) {
            if (VDH_STORAGE.owns(name, e.download_id)) {
              await dir.removeEntry(name).catch(() => {});
            }
          }
          let got = await chrome.storage.local.get(VDH_STORAGE.journal_key),
            j = got[VDH_STORAGE.journal_key] || {};
          delete j[key];
          await chrome.storage.local.set({ [VDH_STORAGE.journal_key]: j });
        } catch (vdhE) { console.warn("[vdhJournal] delete failed", vdhE) }
        VDH_STORAGE.refresh();
      };
      row.appendChild(del);
      box.appendChild(row);
    }
  },
  async purge(button) {
    button.disabled = !0;
    button.textContent = "Purging...";
    let freed = 0;
    try {
      let { journal } = await this.records(),
        keep = new Set();
      for (let r of Object.values(journal)) {
        if (r && r.status === "running" && r.download_id) keep.add(r.download_id);
      }
      let dir = await navigator.storage.getDirectory(),
        names = [];
      for await (let name of dir.keys()) names.push(name);
      for (let name of names) {
        if ([...keep].some(id => VDH_STORAGE.owns(name, id))) continue;
        try { freed += (await (await dir.getFileHandle(name)).getFile()).size || 0 } catch {}
        await dir.removeEntry(name).catch(() => {});
      }
      /* records pointing at files that no longer exist are of no further use */
      let { journal: all } = await this.records(),
        changed = false;
      for (let [k, r] of Object.entries(all)) {
        let still = [...(await this.opfs()).keys()].some(n => VDH_STORAGE.owns(n, r.download_id));
        if (!still && r.status === "running") {
          all[k] = Object.assign({}, r, { status: "interrupted", updated_at: Date.now() });
          changed = true;
        }
      }
      if (changed) await chrome.storage.local.set({ [this.journal_key]: all });
    } catch (vdhE) { console.warn("[vdhJournal] purge failed", vdhE) }
    button.disabled = !1;
    button.textContent = "Purge leftover files" +
      (freed ? " - freed " + this.fmt(freed) : " - nothing to purge");
    this.refresh();
  },
  mount() {
    let box = document.getElementById("vdh_journal_box"),
      purge = document.getElementById("vdh_purge_blobs");
    if (!box) return;
    I(box, !0);
    this.refresh();
    setInterval(() => this.refresh(), 5000);
    try {
      chrome.storage.onChanged.addListener(ch => {
        if (ch.vdh_journal || ch.vdh_last_purge) VDH_STORAGE.refresh();
      });
    } catch (vdhE) {}
    if (purge) purge.onclick = () => VDH_STORAGE.purge(purge);
  }
};
try { VDH_STORAGE.mount() } catch (vdhE) { console.warn("[vdhJournal] mount failed", vdhE) }
/* ===================== end vdhJournal =================================== */
"""
    hist = replace_once(hist, "Ye.button_enable_history.onclick = () => {",
                        PANEL.strip() + "\nYe.button_enable_history.onclick = () => {",
                        "history: storage panel")
    HISTORY.write_text(hist, encoding="utf-8")
    check_js(HISTORY)
    print("patched: content/history.js (storage panel, retry, delete, purge)")

# ------------------------------------------------------------------- markup --

html = HISTORY_HTML.read_text(encoding="utf-8")
# the guard looks for the ELEMENT, not the bare id: the phone stylesheet ships
# rules for #vdh_journal_box too, and matching the bare string made this patch
# skip the markup on a tree where it had never been inserted
if '<vbox id="vdh_journal_box"' in html:
    print("skip (already patched): content/history.html")
else:
    BLOCK = '''<vbox id="vdh_journal_box" class="roundedbox" hidden>
        <h2>Storage &amp; interrupted downloads</h2>
        <p id="vdh_storage_info">Checking...</p>
        <vbox id="vdh_journal_list"></vbox>
        <button id="vdh_purge_blobs">Purge leftover files</button>
      </vbox>
      '''
    anchor = '<hbox id="history_controls"'
    if html.count(anchor) != 1:
        die(f"history.html: expected exactly one {anchor}")
    html = html.replace(anchor, BLOCK + anchor)
    if "vdh_journal_row" not in html:
        style_anchor = "#history_controls, #history_empty {"
        if html.count(style_anchor) != 1:
            die("history.html: style anchor missing")
        html = html.replace(style_anchor, """#vdh_journal_row, .vdh_journal_row {
display: flex;
flex-wrap: wrap;
align-items: center;
gap: 8px;
padding: 6px 0;
border-top: 1px solid var(--theme-box-border);
}
.vdh_journal_label {
flex: 1 1 240px;
word-break: break-word;
}
#vdh_journal_box {
padding: 10px;
margin-top: var(--spacing-medium);
}
""" + style_anchor)
    HISTORY_HTML.write_text(html, encoding="utf-8")
    print("patched: content/history.html (storage box markup)")

print("all patches applied.")
