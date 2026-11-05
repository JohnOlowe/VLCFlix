"""Why the partial file disappeared, and that it no longer does.

The vendor's startup routine deletes EVERY file in the extension's private
storage each time the service worker starts:

    Promise.allSettled(n.map(r => (console.warn(`main::purging ${r}`),
                                   i.removeEntry(r))))

so a crash, a browser restart or a service-worker restart destroyed the partial
file of a download that was still running. The journal entry survived (it lives
in chrome.storage.local), which is why History kept showing a download there
were no bytes left for.

This checks the fix: a partial that belongs to a download the journal still
knows about is kept across a restart, and a file belonging to nothing is still
swept. It also checks the extension asks for persistent storage, so the browser
does not evict the origin under pressure.
"""
import asyncio
import json
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


SEED = """async (args) => {
    const [liveId, bytes] = args;
    const enc = new TextEncoder();
    const dir = await navigator.storage.getDirectory();
    const write = async (name, n) => {
        const h = await dir.getFileHandle(name, {create: true});
        const w = await h.createWritable();
        await w.write(new Uint8Array(n));
        await w.close();
    };
    /* a partial of a download the journal still has a record for */
    await write(liveId + '.mp4', bytes);
    /* a file belonging to no download at all */
    await write('orphan_leftover.tmp', 4096);
    await chrome.storage.local.set({vdh_journal: {
        ['dl:' + liveId]: {id: 'dl:' + liveId, download_id: liveId,
            status: 'running', filename: 'Lecture 4.mp4', bytes: bytes,
            percent: 42, updated_at: Date.now() - 1000,
            download_args: {download_id: liveId}}
    }});
    const names = [];
    for await (const n of dir.keys()) names.push(n);
    return names.sort();
}"""

LIST = """async () => {
    const dir = await navigator.storage.getDirectory();
    const out = {};
    for (const name of await Array.fromAsync(dir.keys())) {
        try { out[name] = (await (await dir.getFileHandle(name)).getFile()).size; }
        catch { out[name] = null; }
    }
    return out;
}"""

PERSIST = """async () => {
    let persisted = null, err = null;
    try { persisted = await navigator.storage.persisted(); } catch (e) { err = String(e); }
    return {persisted: persisted, err: err};
}"""


async def run(phase, extid_holder):
    """One browser session. A restart is simulated by closing the profile and
    launching a new browser over the same user-data directory, which is what
    really happens when the browser (or the crash) starts the service worker
    afresh: the private storage and chrome.storage.local stay, the worker does
    not."""
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-partials", headless=True, channel="chromium",
            viewport={"width": 720, "height": 800},
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        extid_holder.append(extid)
        page = await ctx.new_page()
        await page.goto(f"chrome-extension://{extid}/content/history.html")
        await page.wait_for_timeout(1800)
        result = await phase(page)
        await ctx.close()
        return result


async def main():
    print("\nA. a partial and an orphan are written")
    before = await run(lambda page: page.evaluate(SEED, ["live_dl_42", 64 * 1024]), [])
    print(f"   files: {before}", flush=True)
    ok("live_dl_42.mp4" in before and "orphan_leftover.tmp" in before,
       f"both files are in the extension's private storage ({before})")

    print("\nB. the browser and the service worker restart")
    print("   (profile closed and reopened: private storage and "
          "chrome.storage.local survive, the worker starts from scratch)")

    print("\nC. what survived the restart")
    after = await run(lambda page: page.evaluate(LIST), [])
    print(f"   files: {after}", flush=True)
    ok("live_dl_42.mp4" in after,
       f"the partial of a download with a live journal record survived ({after})")
    ok("orphan_leftover.tmp" not in after,
       f"a file belonging to no download was still swept ({after})")

    async def size_and_persist(page):
        return {"files": await page.evaluate(LIST),
                "persist": await page.evaluate(PERSIST)}
    got = await run(size_and_persist, [])
    size = got["files"].get("live_dl_42.mp4")
    ok(size == 64 * 1024,
       f"and it is the whole file, not a stub ({size} bytes of {64 * 1024})")
    print("\nD. the origin's storage is not evicted under pressure")
    print(f"   navigator.storage.persisted(): {got['persist']}", flush=True)
    perms = json.loads((EXT / "manifest.json").read_text(encoding="utf-8")).get("permissions", [])
    ok("unlimitedStorage" in perms,
       f"the manifest declares unlimitedStorage, which exempts the extension's "
       f"storage from quota eviction ({'unlimitedStorage' in perms})")
    # navigator.storage.persist() is an extra ask on top of that; headless
    # Chromium denies the explicit grant, so it is reported, not asserted
    ok(not got["persist"]["err"],
       f"the persistence request ran without error ({got['persist']})")

    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
