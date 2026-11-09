"""A download that finished but never reached the browser.

What happened on the phone: two movies finished downloading, the browser died
while the extension was handing them to the download manager, and on reopening
both still said "running". Retry then fetched them again from the beginning and
overwrote the finished files, so the storage stayed full and nothing was saved.

This drives the real extension into exactly that state and checks it behaves:

Part A  the row tells the truth: "downloaded, waiting to be saved", not
        "running", and a Save button is offered
Part B  Save hands the finished file to the browser with no bytes fetched --
        proven by pointing the job at a URL that does not exist, so any
        re-fetch would fail
Part C  the space is given back, and the record says done
Part D  Retry on a complete file hands it over instead of downloading it again
Part E  a genuinely partial file still says interrupted, offers Retry, and does
        not claim to be finished
"""
import asyncio
import pathlib
import sys
import urllib.parse

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
PLAIN = "http://127.0.0.1:8123"
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


# the serde envelope the panel and the journal both use
P = lambda v: {"__serde_tag": "primitive", "__serde_val": v}
U = lambda v: {"__serde_tag": "url", "__serde_val": v}
O = lambda d: {"__serde_tag": "object", "__serde_val": d}
A_ = lambda l: {"__serde_tag": "array", "__serde_val": l}
NONE = {"__serde_tag": "none"}
SOME = lambda v: {"__serde_tag": "some", "__serde_val": v}


def job(download_id, basename, url, media_hash, strategy="m3u8_audio_video_one_source"):
    """A record for a download that was interrupted mid-flight."""
    return {
        "id": "dl:" + download_id, "download_id": download_id,
        "filename": basename + ".mp4", "updated_at": 1,
        "download_args": O({
            "download_id": P(download_id), "headers": A_([]),
            "good_basename": P(basename), "muxer": P("mp4"), "subdir": P(""),
            "will_use_jsfetch": P(False), "save_as": P(False),
            "strategy": P(strategy), "url": U(url), "extension": P("mp4"),
            "is_youtube": P(False), "throttle": P(False),
            "carry_get_params": P(False), "cache": NONE}),
        "media": O({"hash": P(media_hash), "type": P("hls_playlist"),
                    "has_drm": P(False), "is_youtube": P(False),
                    "initiator": NONE, "sent_headers": A_([]), "cache": NONE,
                    "subtitles": NONE}),
        "meta": O({"tab_id": P(1), "url": SOME(U(PLAIN + "/index.html")),
                   "favicon_url": NONE, "title": SOME(P("Stranded")),
                   "default_action": P("download")}),
    }


async def seed(page, record, status="running", percent=100, bytes_=65536):
    await page.evaluate("""async (args) => {
        const [rec, status, percent, bytes] = args;
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        j[rec.id] = Object.assign({}, rec, {status, percent, bytes});
        await chrome.storage.local.set({vdh_journal: j});
    }""", [record, status, percent, bytes_])


async def write_opfs(page, name, size, fill=b"S"):
    """Create a file in the extension's private storage, as the worker would."""
    return await page.evaluate("""async (args) => {
        const [name, size, fill] = args;
        const dir = await navigator.storage.getDirectory();
        const fh = await dir.getFileHandle(name, {create: true});
        const w = await fh.createWritable();
        const chunk = fill.repeat(4096);
        let left = size;
        while (left > 0) {
          const n = Math.min(left, chunk.length);
          await w.write(chunk.slice(0, n));
          left -= n;
        }
        await w.close();
        return (await (await dir.getFileHandle(name)).getFile()).size;
    }""", [name, size, fill.decode()])


async def opfs_names(page):
    return await page.evaluate("""async () => {
        const dir = await navigator.storage.getDirectory();
        const out = [];
        for await (const n of dir.keys()) out.push(n);
        return out;
    }""")


async def row_of(page, filename):
    return await page.evaluate("""(name) => {
        const rows = [...document.querySelectorAll('#vdh_journal_list div.vdh_journal_row')];
        const row = rows.find(r => r.textContent.includes(name));
        return row ? {text: row.textContent,
                      buttons: [...row.querySelectorAll('button')].map(b => b.textContent)}
                   : null;
    }""", filename)


async def click(page, filename, label):
    return await page.evaluate("""(args) => {
        const [name, label] = args;
        const rows = [...document.querySelectorAll('#vdh_journal_list div.vdh_journal_row')];
        const row = rows.find(r => r.textContent.includes(name));
        if (!row) return 'no row';
        const b = [...row.querySelectorAll('button')].find(x => x.textContent.trim() === label);
        if (!b) return 'no ' + label + ' button';
        b.click();
        return 'clicked';
    }""", [filename, label])


async def downloads_sized(page, size):
    # A handed-over file is a blob: URL, so its bytes cannot be read back out of
    # the download record the way a data: URL can, and Playwright renames the
    # files it intercepts to a uuid. So: match on the size the browser reports.
    # That is still the proof that matters -- the job's own URL is a 404, so a
    # download that had fetched anything could not be exactly this size.
    items = await page.evaluate("""async () => (await chrome.downloads.search({}))
        .map(d => ({state: d.state, error: d.error, name: d.filename,
                    fileSize: d.fileSize, bytesReceived: d.bytesReceived}))""")
    return [d for d in items if d["fileSize"] == size and d["state"] == "complete"]


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-stranded", headless=True, channel="chromium",
            viewport={"width": 412, "height": 900}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)

        page = await ctx.new_page()
        await page.goto(f"chrome-extension://{extid}/content/history.html")
        await page.wait_for_timeout(1500)

        # a page for the download to be attached to
        site = await ctx.new_page()
        await site.goto(PLAIN + "/index.html")
        await site.wait_for_timeout(600)

        # ------------------------------------------------------------------
        # the state after the crash: finished file on disk, record says running,
        # and the job points at a URL that no longer exists so that any
        # re-fetch would be obvious
        print("\nA. what the row says after the browser died")
        dead_url = PLAIN + "/this-file-does-not-exist.mp4"
        rec = job("stranded_1", "Stranded Movie", dead_url, "media_hash_stranded_1")
        await seed(page, rec, status="running", percent=100, bytes_=65536)
        size = await write_opfs(page, "stranded_1.mp4", 65536, fill=b"M")
        print(f"   seeded {size} bytes on disk as a finished download", flush=True)
        await page.reload()
        await page.wait_for_timeout(2000)
        row = await row_of(page, "Stranded Movie.mp4")
        print(f"   row: {row and row['text'][:90]!r}", flush=True)
        print(f"   buttons: {row and row['buttons']}", flush=True)
        ok(row is not None, "the stranded download shows its row")
        ok(row and "running" not in row["text"].lower(),
           f"it no longer claims to be running ({row and row['text'][:70]!r})")
        ok(row and "waiting to be saved" in row["text"],
           f"it says the download finished and is waiting to be saved "
           f"({row and row['text'][:70]!r})")
        ok(row and "Save" in row["buttons"],
           f"a Save button is offered ({row and row['buttons']})")

        print("\nB. Save hands over the file without fetching anything")
        got = await click(page, "Stranded Movie.mp4", "Save")
        ok(got == "clicked", f"Save was clicked ({got})")
        found = None
        for _ in range(60):
            await asyncio.sleep(0.4)
            hits = await downloads_sized(page, 65536)
            if hits:
                found = hits[0]
                break
        ok(found is not None, "the finished file reached the browser")
        if found:
            d = found
            ok(d["state"] == "complete",
               f"and completed, though the job's URL was a 404 ({d['state']} {d['error']})")
            ok(d["fileSize"] == 65536,
               f"with all 65536 bytes that were on disk, none re-fetched "
               f"({d['fileSize']})")
            ok(d["bytesReceived"] == 65536,
               f"and the browser received every byte ({d['bytesReceived']})")

        print("\nC. the space is given back")
        await page.wait_for_timeout(2500)
        names = await opfs_names(page)
        print(f"   files left: {names}", flush=True)
        ok("stranded_1.mp4" not in names,
           f"the file is gone from extension storage ({names})")
        rec_now = await page.evaluate("""async () => {
            const got = await chrome.storage.local.get('vdh_journal');
            return (got.vdh_journal || {})['dl:stranded_1'] || null;
        }""")
        print(f"   record: status={rec_now and rec_now.get('status')} "
              f"path={rec_now and rec_now.get('path')}", flush=True)
        ok(rec_now and rec_now.get("status") == "done",
           f"and the record now says done ({rec_now and rec_now.get('status')})")

        # ------------------------------------------------------------------
        print("\nD. Retry on a finished file hands it over instead of refetching")
        rec2 = job("stranded_2", "Second Movie", PLAIN + "/also-missing.mp4",
                   "media_hash_stranded_2")
        await seed(page, rec2, status="running", percent=100, bytes_=40960)
        await write_opfs(page, "stranded_2.mp4", 40960, fill=b"R")
        await page.reload()
        await page.wait_for_timeout(2000)
        got = await click(page, "Second Movie.mp4", "Retry")
        ok(got == "clicked", f"Retry was clicked ({got})")
        found2 = None
        for _ in range(60):
            await asyncio.sleep(0.4)
            hits = await downloads_sized(page, 40960)
            if hits:
                found2 = hits[0]
                break
        ok(found2 is not None,
           "Retry handed the finished file over instead of downloading it again")
        if found2:
            ok(found2["bytesReceived"] == 40960,
               f"with all 40960 bytes that were on disk ({found2['bytesReceived']})")
        await page.wait_for_timeout(2000)
        names = await opfs_names(page)
        ok("stranded_2.mp4" not in names, f"and its space was released ({names})")

        # ------------------------------------------------------------------
        print("\nE. a genuinely partial file is not called finished")
        rec3 = job("partial_1", "Half Movie", PLAIN + "/video.mp4", "media_hash_partial_1",
                   strategy="http_audio_video_one_source")
        await seed(page, rec3, status="running", percent=42, bytes_=8192)
        await write_opfs(page, "partial_1.mp4", 4000, fill=b"P")
        await page.reload()
        await page.wait_for_timeout(2000)
        row3 = await row_of(page, "Half Movie.mp4")
        print(f"   row: {row3 and row3['text'][:90]!r}", flush=True)
        print(f"   buttons: {row3 and row3['buttons']}", flush=True)
        ok(row3 and "interrupted" in row3["text"],
           f"it says interrupted, not running and not done ({row3 and row3['text'][:70]!r})")
        ok(row3 and "42%" in row3["text"],
           f"and shows how far it got ({row3 and row3['text'][:70]!r})")
        ok(row3 and "Retry" in row3["buttons"], f"Retry is offered ({row3 and row3['buttons']})")
        ok(row3 and "Save" not in row3["buttons"],
           f"and Save is not offered for a partial file ({row3 and row3['buttons']})")

        # ------------------------------------------------------------------
        print("\nF. a partial HLS download asks before throwing work away")
        rec4 = job("partial_2", "Hls Movie", PLAIN + "/video.mp4", "media_hash_partial_2",
                   strategy="m3u8_audio_video_one_source")
        await seed(page, rec4, status="running", percent=63, bytes_=90000)
        await write_opfs(page, "partial_2.mp4", 60000, fill=b"H")
        await page.reload()
        await page.wait_for_timeout(2000)
        row4 = await row_of(page, "Hls Movie.mp4")
        print(f"   row: {row4 and row4['text'][:90]!r}", flush=True)
        print(f"   buttons: {row4 and row4['buttons']}", flush=True)
        ok(row4 and "cannot resume" in row4["text"],
           f"the row says an HLS stream cannot be resumed "
           f"({row4 and row4['text'][:70]!r})")
        ok(row4 and "Start over" in row4["buttons"],
           f"the button is labelled Start over, not Retry ({row4 and row4['buttons']})")

        said = {}
        async def on_dialog(dlg):
            said["message"] = dlg.message
            await dlg.dismiss()          # the user changes their mind
        page.on("dialog", on_dialog)
        got = await click(page, "Hls Movie.mp4", "Start over")
        await page.wait_for_timeout(2000)
        print(f"   dialog: {str(said.get('message'))[:120]}", flush=True)
        ok(got == "clicked", f"Start over was clicked ({got})")
        ok("cannot be resumed" in str(said.get("message", "")),
           "it warns that the part already downloaded will be discarded")
        ok("60" in str(said.get("message", "")) or "KB" in str(said.get("message", "")),
           f"and says how much is at stake ({str(said.get('message'))[:80]})")
        names = await opfs_names(page)
        ok("partial_2.mp4" in names,
           f"dismissing the warning leaves the partial file alone ({names})")

        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
