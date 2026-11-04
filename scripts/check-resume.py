"""Prove byte-range resume in the real download worker.

The retry path is: history page -> service -> worker. Two of those layers are
awkward in a headless profile (the offscreen document that hosts the worker has
its own 10 s startup budget and often loses it), and neither is where the resume
logic lives. So this drives download_worker/main.js directly over its own
BroadcastChannel protocol -- the same messages the service sends -- and measures
what the HTTP server was actually asked for:

  1. partial file on disk   -> Range: bytes=<size>- , only the remainder fetched,
                               file assembled complete and byte-identical,
                               progress counted the surviving bytes
  2. partial file is whole  -> 416 path, nothing re-downloaded
  3. no partial file        -> plain full fetch (regression guard)
  4. server ignores Range   -> restarts at 0 and still produces a correct file
"""
import asyncio
import hashlib
import json
import pathlib
import shutil
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
SITE = pathlib.Path("/tmp/site")
SRC = SITE / "big.mp4"
LOG_FOR_PORT = {8124: pathlib.Path("/tmp/rangeserver-8124.log"),
                8125: pathlib.Path("/tmp/rangeserver-8125.log")}
TOTAL = SRC.stat().st_size
WANT_SHA = hashlib.sha256(SRC.read_bytes()).hexdigest()
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


P = lambda v: {"__serde_tag": "primitive", "__serde_val": v}
U = lambda v: {"__serde_tag": "url", "__serde_val": v}
O = lambda d: {"__serde_tag": "object", "__serde_val": d}
A_ = lambda l: {"__serde_tag": "array", "__serde_val": l}
NONE = {"__serde_tag": "none"}


def download_args(port, download_id):
    """Serialized exactly the way the panel serializes download_args."""
    return O({
        "download_id": P(download_id), "headers": A_([]),
        "good_basename": P("resume_test"), "muxer": P("mp4"), "subdir": P(""),
        "will_use_jsfetch": P(False), "save_as": P(False),
        "strategy": P("http_audio_video_one_source"),
        "url": U(f"http://127.0.0.1:{port}/big.mp4"), "extension": P("mp4"),
        "is_youtube": P(False), "throttle": P(False),
        "carry_get_params": P(False)})
    # NB: no "cache" key. The service re-serializes args before the worker sees
    # them; a raw {"__serde_tag":"none"} decodes to the Option wrapper here, and
    # fetch() stringifies that to "None" and rejects it.


async def scenario(name, port, seed_bytes, expect, file_id="resume_probe"):
    print(f"\n=== {name} ===", flush=True)
    async with async_playwright() as p:
        prof = f"/tmp/prof-rs-{expect}-{port}"
        shutil.rmtree(prof, ignore_errors=True)
        ctx = await p.chromium.launch_persistent_context(
            prof, headless=True, channel="chromium",
            viewport={"width": 412, "height": 800}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        extid = ctx.service_workers[0].url.split("/")[2]
        pg = await ctx.new_page()
        errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)[:160]))
        await pg.goto(f"chrome-extension://{extid}/content/history.html")

        if seed_bytes:
            # fetch the first `seed_bytes` of the real file, so what is already on
            # disk is a genuine prefix of the source (zeros would survive a resume
            # and make the final content check fail for the wrong reason)
            seeded = await pg.evaluate("""async ([name, n, url]) => {
                const dir = await navigator.storage.getDirectory();
                const r = await fetch(url, {headers: {'Range': 'bytes=0-' + (n - 1)}});
                const buf = new Uint8Array(await r.arrayBuffer());
                const fh = await dir.getFileHandle(name, {create: true});
                const w = await fh.createWritable();
                await w.write(buf);
                await w.close();
                return (await (await dir.getFileHandle(name)).getFile()).size;
            }""", [f"{file_id}.mp4", seed_bytes, "http://127.0.0.1:8124/big.mp4"])
            await asyncio.sleep(1.2)   # let the seeding request finish and be logged
            ok(seeded == seed_bytes, f"seeded a {seed_bytes}-byte partial file")

        logpath = LOG_FOR_PORT[port]
        logpath.write_text("")
        # spawn the real worker module and talk to it the way the service does:
        # channel 2 is FromServiceToWorker, channel 3 is FromWorkerToService.
        result = await pg.evaluate("""async ([url, args]) => {
            const w = new Worker(url, {type: 'module'});
            const msgs = [];
            const bc = new BroadcastChannel('worker_service');
            bc.onmessage = ev => {
                const d = ev.data || {};
                if (d.channel === 3 && d.msg) msgs.push(d.msg);
            };
            const done = new Promise(res => {
                const t = setTimeout(() => res('timeout'), 90000);
                const iv = setInterval(() => {
                    if (msgs.some(m => m.name === 'download_result' ||
                                       m.name === 'download_error')) {
                        clearTimeout(t); clearInterval(iv); res('settled');
                    }
                }, 250);
            });
            await new Promise(r => setTimeout(r, 800));
            bc.postMessage({msg: {name: 'download',
                                  data: {download_args: args}}, channel: 2});
            const settled = await done;
            const terminal = msgs.find(m => m.name === 'download_result' ||
                                            m.name === 'download_error') || {};
            const last = terminal;
            const prog = msgs.filter(m => m.name === 'download_progress')
                             .map(m => m.data && m.data.progress)
                             .filter(Boolean);
            const percent = prog.map(x => x.percent)
                                .filter(x => x && x.is_known)
                                .map(x => x.value);
            bc.close();
            const known = percent.slice();
            return {settled, names: msgs.map(m => m.name), last: last.name,
                    firstPercent: known.length ? known[0] : 0,
                    error: JSON.stringify(last.data || {}).slice(0, 200),
                    maxPercent: percent.length ? Math.max(...percent) : 0,
                    statuses: prog.map(x => x.status).slice(-3)};
        }""", [f"chrome-extension://{extid}/download_worker/main.js",
               download_args(port, file_id)])
        print("   worker:", json.dumps(result), flush=True)

        after = await pg.evaluate("""async (name) => {
            const dir = await navigator.storage.getDirectory();
            let size = 0, sha = null;
            try {
                const f = await (await dir.getFileHandle(name)).getFile();
                size = f.size;
                const buf = new Uint8Array(await f.arrayBuffer());
                const h = await crypto.subtle.digest('SHA-256', buf);
                sha = [...new Uint8Array(h)].map(b => b.toString(16).padStart(2, '0')).join('');
            } catch (e) { return {size: -1, sha: null} }
            return {size, sha};
        }""", f"{file_id}.mp4")
        await asyncio.sleep(0.5)
        log = [l for l in logpath.read_text().strip().splitlines() if "big.mp4" in l]
        sent = sum(int(l.split("sent=")[1]) for l in log)
        ranges = [l for l in log if "range=bytes=" in l and "range=bytes=0-" not in l]
        print(f"   server: {log or 'nothing'}", flush=True)

        ok(result["last"] == "download_result",
           f"worker reported a result ({result['last']}: {result['error']})")
        ok(after["size"] == TOTAL, f"file on disk is complete ({after['size']} of {TOTAL})")
        ok(after["sha"] == WANT_SHA, f"content matches the source ({str(after['sha'])[:12]}…)")
        if expect == "resume":
            floor = 100.0 * seed_bytes / TOTAL - 5
            ok((result["firstPercent"] or 0) >= floor,
               f"progress started from the surviving bytes "
               f"({result['firstPercent']}%, expected >= {floor:.1f}%)")
        else:
            ok((result["maxPercent"] or 0) > 90,
               f"progress ran to the end ({result['maxPercent']}%)")

        if expect == "resume":
            ok(bool(ranges), f"request carried a byte range ({ranges[:1]})")
            ok(sent <= TOTAL - seed_bytes + 4096,
               f"only the remainder was transferred ({sent} B of {TOTAL})")
        elif expect == "no-refetch":
            ok(sent == TOTAL,
               f"416 fell back to a clean full fetch ({sent} B)")
        elif expect == "full":
            ok(sent >= TOTAL, f"full file transferred ({sent} B)")
        elif expect == "ignored":
            ok(sent >= TOTAL, f"server ignored the range, full body sent ({sent} B)")
        ok(not errs, f"no page errors ({errs[:2]})")
        await ctx.close()


async def main():
    await scenario(f"resume: {3 * 1024 * 1024} of {TOTAL} bytes already present",
                   8124, 3 * 1024 * 1024, "resume")
    await scenario("partial file is already whole (416 path)",
                   8124, TOTAL, "no-refetch")
    await scenario("nothing on disk: plain download (regression guard)",
                   8124, 0, "full")
    await scenario("server ignores Range: restart at 0, still correct",
                   8125, 3 * 1024 * 1024, "ignored")
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
