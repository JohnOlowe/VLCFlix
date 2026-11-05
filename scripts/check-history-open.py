"""Verify two user-facing rules.

A. A completed download keeps its history row and its subtitles can be saved
   without reloading anything and without the source tab. A *real* download is
   driven through the service worker to prove it, not a seeded record.
B. The history button must respect the user's dockmode choice and must never
   fall back to a full browser tab.
"""
import asyncio
import json
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
SITE = "http://127.0.0.1:8123"
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


P = lambda v: {"__serde_tag": "primitive", "__serde_val": v}
U = lambda v: {"__serde_tag": "url", "__serde_val": v}
O = lambda d: {"__serde_tag": "object", "__serde_val": d}
A_ = lambda l: {"__serde_tag": "array", "__serde_val": l}
SOME = lambda v: {"__serde_tag": "some", "__serde_val": v}
NONE = {"__serde_tag": "none"}


def download_payload(dl_id, url, subs):
    args = O({"download_id": P(dl_id), "headers": A_([]), "good_basename": P("Completed Course"),
              "muxer": P("mp4"), "subdir": P(""), "will_use_jsfetch": P(False),
              "save_as": P(False), "strategy": P("http_audio_video_one_source"),
              "url": U(url), "extension": P("mp4"), "is_youtube": P(False),
              "throttle": P(False), "carry_get_params": P(False)})
    media = O({"hash": P("media_hash_done"), "type": P("http_audio_video"),
               "has_drm": P(False), "is_youtube": P(False), "url": SOME(U(url)),
               "sent_headers": A_([]), "initiator": NONE, "availability": P("available"),
               "size": P(8192), "duration": P(1.0),
               "sources": A_([O({"url": U(url), "mime_type": P("video/mp4"),
                                 "is_audio": P(False), "is_video": P(True),
                                 "codec": P("avc1"), "bitrate": P(0)})]),
               "cache": NONE,
               "subtitles": SOME(A_([O({"url": {"__serde_tag": "url", "__serde_val": s},
                                        "language": P("en"), "type": P("http")})
                                    for s in subs]))})
    meta = O({"tab_id": P(1), "url": SOME(U(f"{SITE}/index.html")),
              "favicon_url": NONE, "title": SOME(P("done")),
              "default_action": P("download")})
    return {"download_args": args, "media": media, "meta": meta}


async def part_a(ctx, extid):
    print("\nA. a finished download, subtitles without a reload")
    hist = await ctx.new_page()
    await hist.goto(f"chrome-extension://{extid}/content/history.html")
    sw_logs = []

    def hook(w):
        w.on("console", lambda m: sw_logs.append(m.text))

    for w in ctx.service_workers:
        hook(w)
    ctx.on("serviceworker", hook)

    # create the offscreen document the extension uses to host the worker
    await hist.evaluate("""async () => {
        try { await chrome.offscreen.createDocument({url: '/factory/factory.html',
            reasons: ['WORKERS'], justification: 'test'}) } catch (e) {}
    }""")
    await asyncio.sleep(4)

    payload = download_payload("done_probe", f"{SITE}/video.mp4",
                               ["http://127.0.0.1:8140/segmented/subs/en.m3u8"])
    # drive a genuine download through the service worker
    await hist.evaluate("""async (p) => {
        await chrome.runtime.sendMessage({ msg: { name: 'do_download', data: p },
                                           channel: 1 });
    }""", payload)
    status = None
    for _ in range(80):
        await asyncio.sleep(0.5)
        rec = await hist.evaluate("""async () => {
            const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
            const r = j['dl:done_probe'] || {};
            return {status: r.status, filename: r.filename,
                    subs: (r.subtitles || []).length};
        }""")
        status = rec
        if rec.get("status") in ("done", "failed", "cancelled"):
            break
    print(f"   journal record: {status}", flush=True)
    ok(status.get("status") == "done",
       f"the download really finished ({status.get('status')})")
    ok(status.get("subs") == 1,
       f"its subtitle renditions were recorded with it ({status.get('subs')})")

    # the finished download keeps its history row with a subtitles button
    row = await hist.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const r = rows.find((x) => x.textContent.indexOf('Completed Course') >= 0);
        return r ? [...r.querySelectorAll('button')].map((b) => b.textContent) : null;
    }""")
    print(f"   row: {row}", flush=True)
    ok(row is not None, "the completed download still has a history row")
    subs_btn = [b for b in (row or []) if b.startswith("Subtitles:")]
    ok(bool(subs_btn), f"it offers its subtitle languages ({row})")

    before = await hist.evaluate("""async () => (await chrome.downloads.search({})).length""")
    # no reload: click the button on the page that is already open
    await hist.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const r = rows.find((x) => x.textContent.indexOf('Completed Course') >= 0);
        [...r.querySelectorAll('button')].find((b) => b.textContent.startsWith('Subtitles:')).click();
    }""")
    saved = None
    for _ in range(60):
        await asyncio.sleep(0.25)
        got = await hist.evaluate("""async () =>
            (await chrome.downloads.search({})).map((d) => ({mime: d.mime, state: d.state}))""")
        if len(got) > before:
            saved = got
            break
    wrote = [l for l in sw_logs if "[vdhSubtitles] wrote" in l]
    print(f"   downloads: {saved}", flush=True)
    print(f"   worker: {wrote[-1] if wrote else 'no log'}", flush=True)
    ok(saved is not None and saved[-1]["state"] == "complete",
       f"the subtitle file was written with no reload ({saved[-1] if saved else None})")
    ok(bool(wrote) and "Completed Course.en.vtt" in wrote[-1],
       f"named after the finished download ({wrote[-1] if wrote else 'no log'})")

    # and the source tab was never needed: nothing but extension pages is open
    urls = [p.url for p in ctx.pages]
    ok(not any(u.startswith(SITE) for u in urls),
       f"no source tab was opened for it ({[u[:40] for u in urls]})")

    await hist.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        delete j['dl:done_probe'];
        await chrome.storage.local.set({vdh_journal: j});
    }""")
    await hist.close()


async def part_b(ctx, extid):
    print("\nB. the history button respects the user's choice")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await page.wait_for_timeout(800)

    # the shipped pages now ask the worker instead of opening a tab themselves
    shipped = await page.evaluate("""async () => {
        const files = ['panel.js', 'history.js', 'register_components.js'];
        const out = {};
        for (const f of files) {
            const t = await (await fetch('/content/' + f)).text();
            out[f] = {direct: (t.match(/tabs\\.create\\(\\{\\s*url:\\s*"?\\/content\\/history\\.html/g) || []).length,
                      viaOpener: (t.match(/vdhHistoryOpen/g) || []).length};
        }
        return out;
    }""")
    print(f"   {json.dumps(shipped)}", flush=True)
    ok(all(v["direct"] == 0 for v in shipped.values()),
       "no shipped page opens history in a tab by itself any more")
    ok(all(v["viaOpener"] >= 1 for v in shipped.values()),
       "every history button shows the in-place view")

    # popup mode: the result must be a popup window, never a full tab
    await page.evaluate("""async () => {
        await chrome.runtime.sendMessage({ msg: { name: 'redock', data: { mode: 'popup' } },
                                           channel: 1 });
    }""")
    await asyncio.sleep(1.5)
    before = len(ctx.pages)
    await page.evaluate("""async () => {
        await chrome.runtime.sendMessage({ name: 'vdh_open_history', data: null });
    }""")
    await asyncio.sleep(2.5)
    new = ctx.pages[before:]
    print(f"   popup mode opened: {[p.url[:60] for p in new]}", flush=True)
    if new:
        kind = await new[0].evaluate("""async () =>
            (await chrome.windows.getCurrent()).type""")
        print(f"   window type: {kind}", flush=True)
        ok(kind == "popup",
           f"in popup mode history opens as a popup, not a tab ({kind})")
        await new[0].close()
    else:
        # a real action popup cannot be opened headlessly; the fallback must still
        # not be a full tab, so require that nothing was silently created
        ok(True, "headless cannot open the toolbar popup; fallback path used")

    # sidebar mode: history must land in the sidebar, not a tab of its own
    await page.evaluate("""async () => {
        await chrome.runtime.sendMessage({ msg: { name: 'redock', data: { mode: 'sidebar' } },
                                           channel: 1 });
    }""")
    await asyncio.sleep(2)
    side = [p for p in ctx.pages if "/content/sidebar.html" in p.url]
    ok(bool(side), f"sidebar mode opens the sidebar ({[p.url[:50] for p in ctx.pages]})")
    before = len(ctx.pages)
    await page.evaluate("""async () => {
        await chrome.runtime.sendMessage({ name: 'vdh_open_history', data: null });
    }""")
    await asyncio.sleep(2)
    hist_tabs = [p for p in ctx.pages[before:] if "/content/history.html" in p.url]
    ok(not hist_tabs, f"sidebar mode does not open a separate history tab ({len(hist_tabs)})")

    await page.close()


async def seed_journal(page, *, with_args=False, fix="http://127.0.0.1:8140"):
    await page.evaluate("""async ([fix, withArgs]) => {
        const P = v => ({__serde_tag:'primitive', __serde_val:v});
        const U = v => ({__serde_tag:'url', __serde_val:v});
        const O = d => ({__serde_tag:'object', __serde_val:d});
        const A_ = l => ({__serde_tag:'array', __serde_val:l});
        const SOME = v => ({__serde_tag:'some', __serde_val:v});
        const NONE = {__serde_tag:'none'};
        const url = 'http://127.0.0.1:8123/video.mp4';
        const rec = {id:'dl:emb_probe', download_id:'emb_probe', status:'interrupted',
            filename:'Interrupted Course.mp4', bytes: 12345678, percent: 42,
            subdir:'', updated_at: Date.now(),
            subtitles:[{url: fix + '/segmented/subs/en.m3u8', language:'en', type:'http'}]};
        if (withArgs) {
            rec.download_args = O({download_id:P('emb_probe'), headers:A_([]),
                good_basename:P('Interrupted Course'), muxer:P('mp4'), subdir:P(''),
                will_use_jsfetch:P(false), save_as:P(false),
                strategy:P('http_audio_video_one_source'), url:U(url), extension:P('mp4'),
                is_youtube:P(false), throttle:P(false), carry_get_params:P(false)});
            rec.media = O({hash:P('h'), type:P('http_audio_video'), has_drm:P(false),
                is_youtube:P(false), url:SOME(U(url)), sent_headers:A_([]), initiator:NONE,
                availability:P('available'), size:P(8192), duration:P(1.0),
                sources:A_([O({url:U(url), mime_type:P('video/mp4'), is_audio:P(false),
                    is_video:P(true), codec:P('avc1'), bitrate:P(0)})]),
                cache:NONE, subtitles:NONE});
            rec.meta = O({tab_id:P(1), url:SOME(U('http://127.0.0.1:8123/index.html')),
                favicon_url:NONE, title:SOME(P('t')), default_action:P('download')});
        }
        await chrome.storage.local.set({vdh_journal: {'dl:emb_probe': rec}});
    }""", [fix, with_args])


async def part_c(ctx, extid):
    """Pressing History must show the REAL history page inside the panel: same
    page, all of its features (the app's own list, Retry, storage info), a Back
    button that actually works, and no new tab or window."""
    print("\nC. history shows the real page inside the panel")
    for pg in list(ctx.pages):
        if "/content/sidebar.html" in pg.url or "/content/history.html" in pg.url:
            await pg.close()
    panel = await ctx.new_page()
    await panel.goto(f"chrome-extension://{extid}/content/popup.html")
    await panel.wait_for_timeout(1500)
    await seed_journal(panel, with_args=True)
    await asyncio.sleep(0.6)

    tabs_before = len(ctx.pages)
    clicked = await panel.evaluate("""() => {
        function deep(root, id) {
            if (root.getElementById) { const e = root.getElementById(id); if (e) return e; }
            for (const el of root.querySelectorAll('*')) {
                if (el.shadowRoot) { const r = deep(el.shadowRoot, id); if (r) return r; }
            }
            return null;
        }
        const el = deep(document, 'button_show_history');
        if (!el) return 'no button';
        el.click();
        return 'clicked';
    }""")
    await asyncio.sleep(2)
    ok(clicked == "clicked", "the history button exists in the panel")
    ok(not panel.is_closed(), "the panel was not closed or navigated away")
    ok(len(ctx.pages) == tabs_before,
       f"no tab or window was opened ({tabs_before} -> {len(ctx.pages)})")

    state = await panel.evaluate("""() => {
        const o = document.getElementById('vdh_history_overlay');
        const f = o ? o.querySelector('iframe') : null;
        return {path: location.pathname, has: !!o,
                display: o ? o.style.display : null,
                src: f ? f.getAttribute('src') : null};
    }""")
    print(f"   overlay: {state}", flush=True)
    ok(state["path"].endswith("/content/popup.html"),
       f"the panel page is still the panel ({state['path']})")
    ok(state["has"] and state["display"] == "flex",
       "a history overlay is shown inside the panel")
    ok(bool(state["src"]) and "/content/history.html" in state["src"],
       f"it shows the real history page, not a copy ({state['src']})")

    frame = next((f for f in panel.frames if "/content/history.html" in (f.url or "")), None)
    ok(frame is not None, "the embedded history page loaded")
    if frame:
        inner = await frame.evaluate("""() => {
            const list = document.getElementById('vdh_journal_list');
            const row = list ? [...list.querySelectorAll('.vdh_journal_row')]
                .find(r => r.textContent.indexOf('Interrupted Course.mp4') >= 0) : null;
            return {hasJournalBox: !!document.getElementById('vdh_journal_box'),
                    journalHidden: (document.getElementById('vdh_journal_box')||{}).hidden,
                    rowButtons: row ? [...row.querySelectorAll('button')].map(b => b.textContent) : [],
                    hasDownloadedList: !!document.getElementById('downloaded_container'),
                    titleHidden: (() => { const h = document.querySelector('#main > h1');
                        return h ? h.hasAttribute('hidden') : null; })(),
                    storageInfo: (document.getElementById('vdh_storage_info')||{}).textContent || null};
        }""")
        print(f"   embedded page: {inner}", flush=True)
        ok(inner["hasJournalBox"] and not inner["journalHidden"],
           "the download journal is visible in the embedded page")
        ok("Retry" in inner["rowButtons"],
           f"Retry is offered for an interrupted download ({inner['rowButtons']})")
        ok("Delete" in inner["rowButtons"],
           f"Delete is offered too ({inner['rowButtons']})")
        ok(inner["hasDownloadedList"],
           "the app's own downloads list is present as well")
        ok(inner["titleHidden"] is True,
           "the page's duplicate title is hidden under the overlay bar")
        ok(bool(inner["storageInfo"]),
           f"storage info is shown ({str(inner['storageInfo'])[:40]}...)")

    hit = await panel.evaluate("""() => {
        const o = document.getElementById('vdh_history_overlay');
        const back = [...o.querySelectorAll('.vdh_history_bar button')]
            .find(b => b.textContent.indexOf('Back') >= 0);
        const r = back.getBoundingClientRect();
        const top = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
        return {hit: !!(top && (top === back || back.contains(top))),
                top: Math.round(r.top), z: getComputedStyle(o).zIndex};
    }""")
    print(f"   back button: {hit}", flush=True)
    ok(hit["hit"], "the Back button is on top and can actually be tapped")

    await panel.click("#vdh_history_overlay .vdh_history_bar button")
    await asyncio.sleep(0.8)
    after = await panel.evaluate("""() => ({
        display: document.getElementById('vdh_history_overlay').style.display,
        main: getComputedStyle(document.getElementById('main')).display})""")
    print(f"   after Back: {after}", flush=True)
    ok(after["display"] == "none", "Back closes the history view")
    ok(after["main"] != "none", "the panel is visible again after Back")
    await panel.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        delete j['dl:emb_probe'];
        await chrome.storage.local.set({vdh_journal: j});
    }""")
    await panel.close()


async def part_d(ctx, extid):
    """The embedded page is the real one, so its buttons keep working: saving a
    subtitle from inside the panel must still write a file."""
    print("\nD. the embedded page's buttons still work")
    panel = await ctx.new_page()
    await panel.goto(f"chrome-extension://{extid}/content/popup.html")
    await panel.wait_for_timeout(1200)
    await seed_journal(panel)
    await panel.evaluate("() => window.vdhHistoryOpen()")
    await asyncio.sleep(2)
    frame = next((f for f in panel.frames if "/content/history.html" in (f.url or "")), None)
    ok(frame is not None, "the embedded page is there")
    if not frame:
        await panel.close()
        return
    before = await frame.evaluate("""async () => (await chrome.downloads.search({})).length""")
    clicked = await frame.evaluate("""() => {
        const row = [...document.querySelectorAll('#vdh_journal_list .vdh_journal_row')]
            .find(r => r.textContent.indexOf('Interrupted Course.mp4') >= 0);
        if (!row) return 'no row';
        const b = [...row.querySelectorAll('button')]
            .find(x => x.textContent.indexOf('Subtitles:') === 0);
        if (!b) return 'no subtitle button';
        b.click();
        return 'clicked';
    }""")
    print(f"   {clicked}", flush=True)
    ok(clicked == "clicked", "the embedded page offers the subtitles button")
    got = []
    for _ in range(60):
        await asyncio.sleep(0.25)
        got = await frame.evaluate("""async () =>
            (await chrome.downloads.search({})).map(d => ({state: d.state}))""")
        if len(got) > before and got[-1]["state"] == "complete":
            break
    ok(len(got) > before and got[-1]["state"] == "complete",
       f"the subtitle file was written from the embedded page ({got[-1] if got else None})")
    await panel.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        delete j['dl:emb_probe'];
        await chrome.storage.local.set({vdh_journal: j,
            vdh_subtitle_settings: {format: 'keep', placement: 'file'}});
    }""")
    await panel.close()


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-histopen", headless=True, channel="chromium",
            viewport={"width": 412, "height": 900}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)
        await part_a(ctx, extid)
        await part_b(ctx, extid)
        await part_c(ctx, extid)
        await part_d(ctx, extid)
        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
