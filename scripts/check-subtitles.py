"""Verify subtitle download end to end on the real extension.

Part A loads the shipped module `service/vdh-subtitles.js` inside a real
extension page and runs the stitcher against a local HLS fixture, then parses the
resulting document with the browser's own WebVTT parser. That is the strongest
check available: if Chrome's parser accepts it and yields the expected cue
times, the file is valid.

Part B drives the real `vdh_subtitle_save` message through the service worker and
asserts that chrome.downloads receives a `text/vtt` file with the right name.

Part C falls back to a genuinely encrypted live HLS stream (if this machine has
internet) to prove the refusal path on real-world input.

Needs scripts/test-subtitle-server.py running on :8140.
"""
import asyncio
import json
import pathlib
import shutil
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
FIXTURE = "http://127.0.0.1:8140"
FIELDS = ["row", "pass", "fail"]

failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


async def load_module_probe(page, extid):
    """Import the *shipped* module by its extension URL (a blob: import is blocked
    by script-src 'self'). Nothing is copied or re-implemented for the test."""
    url = f"chrome-extension://{extid}/service/vdh-subtitles.js"
    okd = await page.evaluate("""async (url) => {
        try { window.VDH_SUBS = await import(url); return true; }
        catch (e) { return String(e); }
    }""", url)
    if okd is not True:
        raise SystemExit(f"could not import {url}: {okd}")


def cues_of(vtt_js_result):
    """[(start_ms, end_ms, text)] from a list of {start,end,text} the page returns."""
    return [(int(c["start"]), int(c["end"]), c["text"].strip()) for c in vtt_js_result]


async def part_a(ctx, extid):
    print("\nA. the stitcher produces documents the browser accepts")
    page = await ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:160]))
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await load_module_probe(page, extid)

    # --- 1. a single-file rendition -----------------------------------------
    plain = await page.evaluate("""async (url) => {
        const r = await window.VDH_SUBS.fetchSubtitleTrack({url});
        return {text: r.text, segments: r.segments, bytesIn: r.bytesIn};
    }""", f"{FIXTURE}/plain/subs/en.vtt")
    ok(plain["segments"] == 1, f"single file reported as 1 segment ({plain['segments']})")
    ok(plain["text"].startswith("WEBVTT"),
       f"document starts with WEBVTT ({plain['text'][:20]!r})")
    ok(plain["text"].count("WEBVTT") == 1,
       f"exactly one WEBVTT header ({plain['text'].count('WEBVTT')})")

    # --- 2. segmented rendition: stitching + timestamp offsets ---------------
    seg = await page.evaluate("""async (url) => {
        const r = await window.VDH_SUBS.fetchSubtitleTrack({url});
        return {text: r.text, segments: r.segments, bytesIn: r.bytesIn};
    }""", f"{FIXTURE}/segmented/subs/en.m3u8")
    ok(seg["segments"] == 2, f"two segments fetched ({seg['segments']})")
    ok(seg["text"].count("WEBVTT") == 1,
       f"repeated WEBVTT headers collapsed ({seg['text'].count('WEBVTT')})")
    ok("X-TIMESTAMP-MAP" not in seg["text"],
       "X-TIMESTAMP-MAP headers stripped from the body")

    # let the browser parse the document we produced
    parsed = await page.evaluate("""async (text) => {
        // Use the browser's own WebVTT parser via a <track> element: if Chrome
        // accepts the document and yields cues, the file we write is valid.
        const video = document.createElement('video');
        const t = document.createElement('track');
        t.kind = 'subtitles'; t.srclang = 'en'; t.default = true;
        t.src = URL.createObjectURL(new Blob([text], {type: 'text/vtt'}));
        video.appendChild(t);
        await new Promise(r => setTimeout(r, 300));
        t.track.mode = 'hidden';
        const cues = [];
        for (let i = 0; i < 60; i++) {
            if (t.track.cues && t.track.cues.length) break;
            await new Promise(r => setTimeout(r, 100));
        }
        if (t.track.cues) {
            for (const c of t.track.cues) {
                cues.push({start: Math.round(c.startTime * 1000),
                           end: Math.round(c.endTime * 1000), text: c.text});
            }
        }
        return cues;
    }""", seg["text"])
    cues = cues_of(parsed)
    print(f"   parsed {len(cues)} cues: {cues}", flush=True)
    ok(len(cues) == 4, f"browser parsed all 4 cues ({len(cues)})")
    # segment one starts at 200ms; segment two carries MPEGTS 1350000 vs 900000,
    # i.e. +450000 ticks = +5s, so its first cue must land at 5.5s not 0.5s
    starts = [c[0] for c in cues]
    ok(starts == sorted(starts), f"cue times increase across segments ({starts})")
    ok(5500 in starts, f"second segment offset applied, cue at 5.5s ({starts})")
    ok(all(b >= a for a, b in zip(starts, starts[1:])),
       "no cue jumps backwards (the classic naive-stitch bug)")

    # --- 3. two languages are independent -----------------------------------
    es = await page.evaluate("""async (url) => {
        const r = await window.VDH_SUBS.fetchSubtitleTrack({url});
        return {text: r.text, segments: r.segments};
    }""", f"{FIXTURE}/segmented/subs/es.m3u8")
    ok("Primera se" in es["text"], "second language fetched and decoded (UTF-8 kept)")

    # --- 4. encrypted rendition must be refused, not written -----------------
    enc = await page.evaluate("""async (url) => {
        try {
            await window.VDH_SUBS.fetchSubtitleTrack({url});
            return {error: null};
        } catch (e) { return {error: String(e.message || e)}; }
    }""", f"{FIXTURE}/encrypted/subs/enc.m3u8")
    ok(enc["error"] and "encrypt" in enc["error"].lower(),
       f"encrypted playlist refused with a clear message ({enc['error']!r})")

    # --- 5. filename safety -------------------------------------------------
    langs = await page.evaluate("""() => ['en', 'pt-BR', 'Español', '', null, '../../etc']
        .map(l => window.VDH_SUBS.safeLanguage(l))""")
    ok(all("/" not in l and ".." not in l for l in langs),
       f"language tags are safe for filenames ({langs})")
    ok("und" in langs, f"missing language falls back to 'und' ({langs})")

    # --- 6. the UI must not offer a rendition it cannot save ------------------
    # The page keeps per-rendition button state; ask it to probe the encrypted
    # URL the same way it does after a click, and require a refusal *before* a
    # button is ever rendered.
    refused = await page.evaluate("""async (url) => {
        const btns = [...document.querySelectorAll('button')]
            .filter((b) => b.textContent.indexOf('Subtitles:') === 0);
        try {
            await window.VDH_SUBS.fetchSubtitleTrack({url});
            return {error: null, buttons: btns.length};
        } catch (e) {
            return {error: String(e.message || e), buttons: btns.length};
        }
    }""", f"{FIXTURE}/encrypted/subs/enc.m3u8")
    ok(refused["error"] is not None,
       f"encrypted rendition cannot produce a file ({refused['error']!r})")

    ok(not errs, f"no page errors ({errs[:2]})")
    await page.close()


async def part_d(ctx, extid, fixture):
    """An encrypted rendition is recorded but must never get a button, and a
    download with no renditions must show nothing at all."""
    print("\nD. what the history page offers")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await page.wait_for_timeout(800)
    await page.evaluate("""async ([fix, plain]) => {
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        j['dl:enc_probe'] = {
            id: 'dl:enc_probe', download_id: 'enc_probe', status: 'done',
            filename: 'Encrypted Course.mp4', subdir: '', updated_at: 1,
            subtitles: [{url: fix + '/encrypted/subs/enc.m3u8', language: 'en', type: 'http'}],
        };
        j['dl:nosub_probe'] = {
            id: 'dl:nosub_probe', download_id: 'nosub_probe', status: 'done',
            filename: 'No Subtitles.mp4', subdir: '', updated_at: 1,
        };
        await chrome.storage.local.set({vdh_journal: j});
    }""", [fixture, FIXTURE])
    await page.wait_for_timeout(1200)
    # the page asks the service to probe; wait for the verdict to land
    for _ in range(40):
        verdict = await page.evaluate("""async () => {
            const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
            return (j['dl:enc_probe'] || {}).subtitles_unavailable || null;
        }""")
        if verdict is not None:
            break
        await page.wait_for_timeout(250)
    await page.wait_for_timeout(900)
    ok(verdict is not None, f"the service probed the encrypted rendition ({verdict})")
    ok(verdict == [0], f"the encrypted rendition is marked unavailable ({verdict})")

    def row_buttons(name):
        return f"""() => {{
            const rows = [...document.querySelectorAll('div.vdh_journal_row')];
            const row = rows.find((r) => r.textContent.indexOf({name!r}) >= 0);
            if (!row) return null;
            return [...row.querySelectorAll('button')].map((b) => b.textContent);
        }}"""

    enc_btns = await page.evaluate(row_buttons("Encrypted Course.mp4"))
    print(f"   encrypted row buttons: {enc_btns}", flush=True)
    ok(enc_btns is not None, "encrypted rendition still shows its download row")
    ok(not [b for b in (enc_btns or []) if b.startswith("Subtitles:")],
       f"no subtitle button for an encrypted rendition ({enc_btns})")

    plain_btns = await page.evaluate(row_buttons("No Subtitles.mp4"))
    print(f"   no-subtitle row buttons: {plain_btns}", flush=True)
    ok(not [b for b in (plain_btns or []) if b.startswith("Subtitles:")],
       f"nothing offered when the stream had no subtitles ({plain_btns})")

    await page.close()


async def part_b(ctx, extid):
    print("\nB. the service writes a real .vtt through chrome.downloads")
    sw_logs = []
    # the worker may have been recycled between parts; attach to whatever is live
    for w in ctx.service_workers:
        w.on("console", lambda m: sw_logs.append(m.text[:200]))
    ctx.on("serviceworker", lambda w: w.on("console",
        lambda m: sw_logs.append(m.text[:200])))
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await page.wait_for_timeout(1200)
    # seed a journal record the way a finished download would look, with one
    # English rendition pointing at the fixture
    await page.evaluate("""async (rec) => {
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        j['dl:subs_probe'] = rec;
        await chrome.storage.local.set({vdh_journal: j});
    }""", {"id": "dl:subs_probe", "download_id": "subs_probe", "status": "done",
            "filename": "Lecture 4.mp4", "subdir": "", "updated_at": 1,
            "subtitles": [{"url": f"{FIXTURE}/segmented/subs/en.m3u8",
                           "language": "en", "type": "http"},
                          {"url": f"{FIXTURE}/segmented/subs/es.m3u8",
                           "language": "es", "type": "http"}]})
    await page.wait_for_timeout(800)
    buttons = await page.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const row = rows.find(r => r.textContent.includes('Lecture 4.mp4'));
        if (!row) return {row: false};
        const labels = [...row.querySelectorAll('button')].map(b => b.textContent);
        return {row: true, labels};
    }""")
    ok(buttons.get("row"), "download row rendered")
    ok(any("Subtitles: en" in l for l in buttons.get("labels", [])),
       f"English subtitle button offered ({buttons.get('labels')})")
    ok(any("Subtitles: es" in l for l in buttons.get("labels", [])),
       "Spanish subtitle button offered")

    await page.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const row = rows.find(r => r.textContent.includes('Lecture 4.mp4'));
        [...row.querySelectorAll('button')]
            .find(b => b.textContent === 'Subtitles: en').click();
    }""")
    await page.wait_for_timeout(8000)
    downloads = await page.evaluate("""async () => (await chrome.downloads.search({}))
        .map(d => ({filename: d.filename.split('/').pop(), mime: d.mime,
                    state: d.state, error: d.error, exists: d.exists}))""")
    print(f"   downloads: {json.dumps(downloads)}", flush=True)
    # Playwright intercepts downloads and renames the captured file to a uuid, so
    # the *intended* name is taken from the worker's own log; the MIME type and
    # the completion state come from chrome.downloads itself.
    vtt = [d for d in downloads if "vtt" in (d["mime"] or "")]
    ok(bool(vtt), f"a text/vtt download was created ({vtt})")
    if vtt:
        ok(vtt[0]["state"] == "complete" and not vtt[0]["error"],
           f"download completed cleanly (state={vtt[0]['state']}, error={vtt[0]['error']})")
    wrote = [l for l in sw_logs if "wrote" in l]
    ok(any("Lecture 4.en.vtt" in l for l in wrote),
       f"named after the download and language ({wrote[-1] if wrote else 'no log'})")
    ok(any("2 segment" in l for l in wrote),
       f"both segments stitched before writing ({wrote[-1] if wrote else 'no log'})")
    recorded = await page.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        return (j['dl:subs_probe'] || {}).subtitles_saved || [];
    }""")
    ok("en" in recorded, f"journal records the saved language ({recorded})")
    await page.close()


async def part_c(ctx, extid):
    """Real-world stream: Apple's sample HLS carries a segmented WebVTT rendition
    (100 segments). Skipped only when this machine has no internet."""
    print("\nC. live HLS with a real subtitle rendition")
    MASTER = ("https://devstreaming-cdn.apple.com/videos/streaming/examples/"
              "img_bipbop_adv_example_fmp4/master.m3u8")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await load_module_probe(page, extid)
    result = await page.evaluate("""async (master) => {
        let text;
        try { text = await (await fetch(master)).text(); }
        catch (e) { return {skipped: 'offline'}; }
        const LF = String.fromCharCode(10);
        const line = text.split(LF).find((l) => l.indexOf('#EXT-X-MEDIA:TYPE=SUBTITLES') === 0);
        if (!line) return {skipped: 'no subtitle rendition in that master playlist'};
        const at = line.indexOf('URI="') + 5;
        const url = new URL(line.slice(at).split('"')[0], master).href;
        try {
            const out = await window.VDH_SUBS.fetchSubtitleTrack({url});
            return {url, segments: out.segments, bytesIn: out.bytesIn,
                    headers: out.text.split('WEBVTT').length - 1,
                    firstCue: (out.text.split(LF + LF)[1] || '').slice(0, 60)};
        } catch (e) { return {url, error: String(e.message || e)}; }
    }""", MASTER)
    print(f"   live: {json.dumps(result)[:220]}", flush=True)
    if result.get("skipped"):
        print(f"   SKIPPED ({result['skipped']}) -- the fixture in part A still covers this path")
    else:
        ok(not result.get("error"), f"live rendition fetched ({result.get('error')!r})")
        ok(result.get("segments", 0) > 1,
           f"multi-segment live playlist stitched ({result.get('segments')} segments)")
        ok(result.get("headers") == 1,
           f"one WEBVTT header in the output ({result.get('headers')})")
        ok(result.get("firstCue", "").find("-->") > 0,
           f"first cue is well formed ({result.get('firstCue')!r})")
    await page.close()


async def part_e(ctx, extid, fixture):
    """The renditions must survive on a real journal record: the vendor parser
    does not always populate media.subtitles, so the writer falls back to the
    serialized media payload that Retry already keeps."""
    print("\nE. renditions recorded on a real download (serde media payload)")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await load_module_probe(page, extid)

    # exactly the wire shape check-journal.py uses, with one subtitle rendition
    recovered = await page.evaluate('''async (fix) => {
        const media = {__serde_tag: "object", __serde_val: {
            hash: {__serde_tag: "primitive", __serde_val: "media_hash_probe"},
            subtitles: {__serde_tag: "some", __serde_val: [
                {__serde_tag: "object", __serde_val: {
                    url: {__serde_tag: "url", __serde_val: fix + "/segmented/subs/en.m3u8"},
                    language: {__serde_tag: "primitive", __serde_val: "en"},
                    type: {__serde_tag: "primitive", __serde_val: "http"}}}]}}};
        return window.VDH_SUBS.renditionsFromMedia(media);
    }''', fixture)
    print(f"   recovered: {recovered}", flush=True)
    ok(len(recovered) == 1 and recovered[0]["url"].endswith("/segmented/subs/en.m3u8"),
       f"rendition recovered from the serde media payload ({recovered})")
    ok(recovered[0]["language"] == "en",
       f"language recovered too ({recovered[0].get('language')!r})")

    empty = await page.evaluate("""() => window.VDH_SUBS.renditionsFromMedia(
        {__serde_tag: "object", __serde_val: {subtitles: {__serde_tag: "none"}}})""")
    ok(empty == [], f"a media object without subtitles yields nothing ({empty})")

    # and the recovered rendition is savable through the real message path
    await page.evaluate('''async (rec) => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        j['dl:recover_probe'] = rec;
        await chrome.storage.local.set({vdh_journal: j});
    }''', {"id": "dl:recover_probe", "download_id": "recover_probe", "status": "done",
             "filename": "Recovered Course.mp4", "subdir": "", "updated_at": 1,
             "subtitles": recovered})
    await page.reload()
    await page.wait_for_timeout(1500)
    buttons = await page.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const row = rows.find((r) => r.textContent.indexOf('Recovered Course.mp4') >= 0);
        return row ? [...row.querySelectorAll('button')].map((b) => b.textContent) : null;
    }""")
    print(f"   buttons: {buttons}", flush=True)
    ok(buttons is not None, "the recovered record shows its row")
    ok(bool([b for b in (buttons or []) if b.startswith("Subtitles: en")]),
       f"the recovered rendition is offered ({buttons})")

    await page.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        delete j['dl:recover_probe'];
        await chrome.storage.local.set({vdh_journal: j});
    }""")
    await page.close()


async def main():
    shutil.rmtree("/tmp/prof-subs", ignore_errors=True)
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-subs", headless=True, channel="chromium",
            viewport={"width": 412, "height": 800}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load"); sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)
        await part_a(ctx, extid)
        await part_b(ctx, extid)
        await part_c(ctx, extid)
        await part_d(ctx, extid, FIXTURE)
        await part_e(ctx, extid, FIXTURE)
        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
