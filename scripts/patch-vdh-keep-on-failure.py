#!/usr/bin/env python3
"""Stop throwing away a finished download when the browser handoff fails.

Pipeline (VDH Pro 10.6.1.2), for the record:

  1. download_worker/main.js streams the media into **OPFS**
     (Origin Private File System -- not a normal folder, see CONTEXT.md) under
     `<download_id>.<ext>`, plus `<download_id>_audio` / `<download_id>_video`
     for split streams, and muxes with the bundled libav wasm.
  2. It hands the service worker a blob URL (`createObjectURL` of an OPFS
     `File`, i.e. still backed by OPFS, not a copy).
  3. The service worker calls `chrome.downloads.download({url: <blob url>})`,
     i.e. hands the blob to the browser's download manager.

Stock code in step 3 (service/main.js):

    if (await navigator.storage.getDirectory().removeEntry(r),   // delete OPFS file
        wr({name:"revoke_blob_url", ...}),                       // revoke blob url
        d.isErr()) { ... return d }                              // only THEN check result

The OPFS file and the blob URL are destroyed before the result is inspected,
so when `downloads.download` fails -- e.g. "No space left on the device" while
the browser copies the blob to shared storage -- the local copy is already
gone and the media has to be fetched again from the network.

This patch keeps the local copy when the handoff failed:

  - delete the OPFS file and revoke the blob URL only if the handoff succeeded
  - on failure, report what the user can free up and where the bytes are, and
    how much OPFS space is still available

Nothing else changes: same objects, same error type on the failure path, so
the UI's retry button keeps working. Verified by `node --check` and by
`scripts/check-real-extension.py` (the extension must still mount).
"""
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "extension"
SW = ROOT / "service/main.js"
MARK = "vdhKeepOnFail"

if not SW.exists():
    sys.exit(f"missing {SW}")

OLD = '''if (await (await navigator.storage.getDirectory()).removeEntry(r), wr({
        name: "revoke_blob_url",
        data: {
          blob_url: o
        }
      }), d.isErr()) {
      let h = d.error;
      return h.interrupt_reason == "USER_CANCELED" ? L({
        aborted_no_partial: !0,
        ending_reason: kr()
      }) : (h.details, d)
    }'''

NEW = '''if (d.isErr()) {
      /* vdhKeepOnFail: the browser handoff failed (typically ENOSPC while the
         browser copies the blob into shared storage). Stock code deleted the
         OPFS copy and revoked the blob URL first, so the finished download was
         lost and had to be fetched again. Keep both and report. */
      let h = d.error;
      if (h.interrupt_reason == "USER_CANCELED") {
        await (await navigator.storage.getDirectory()).removeEntry(r).catch(() => {});
        wr({ name: "revoke_blob_url", data: { blob_url: o } });
        return L({ aborted_no_partial: !0, ending_reason: kr() })
      }
      let q = "unknown", free = "unknown";
      try {
        let s = await navigator.storage.estimate();
        q = ((s.usage || 0) / 1048576).toFixed(1) + " MB used of " +
            ((s.quota || 0) / 1048576).toFixed(0) + " MB quota";
        free = (((s.quota || 0) - (s.usage || 0)) / 1048576).toFixed(1) + " MB";
      } catch {}
      console.warn("[vdhKeepOnFail] handoff failed; keeping OPFS copy", r,
                   "blob", o, q);
      return I({ details: (h.details || h) + " | handoff failed after the media " +
                 "was fully downloaded. Free some storage on the device and retry: " +
                 "the finished file is kept in the extension's private storage " +
                 "(" + r + ") instead of being discarded. Extension storage: " +
                 q + ", free " + free + "." })
    }
    await (await navigator.storage.getDirectory()).removeEntry(r).catch(() => {}),
      wr({ name: "revoke_blob_url", data: { blob_url: o } })'''


def find_ws(haystack: str, needle: str):
    keep = [i for i, ch in enumerate(haystack) if not ch.isspace()]
    flat = "".join(haystack[i] for i in keep)
    target = "".join(needle.split())
    n = flat.count(target)
    pos = flat.find(target)
    if pos < 0:
        return None, None, 0
    return keep[pos], keep[pos + len(target) - 1] + 1, n


text = SW.read_text(encoding="utf-8")
if MARK in text:
    print("skip (already patched): service/main.js")
    sys.exit(0)

start, end, n = find_ws(text, OLD)
if n != 1:
    sys.exit(f"ABORT: handoff block matched {n}x (need 1) -- minified layout changed?")
text = text[:start] + NEW + text[end:]

SW.write_text(text, encoding="utf-8")
r = subprocess.run(["node", "--check", str(SW)], capture_output=True, text=True)
if r.returncode != 0:
    SW.write_text(text[:start] + OLD + text[end:], encoding="utf-8")
    sys.exit("ABORT: patched service worker does not parse; reverted")

print("patched: service/main.js (keep OPFS copy + blob URL when the handoff fails)")
