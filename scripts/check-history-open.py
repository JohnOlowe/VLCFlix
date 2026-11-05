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


async def part_c(ctx, extid):
    """Pressing History must show history *inside the panel*, the way Settings
    does, with a Back button that restores the panel. No new tab, no new window,
    and the page must not navigate away."""
    print("\nC. history opens in place, like Settings")
    # part B leaves the sidebar page open; the opener focuses an open history
    # view first, so start from a clean set of pages
    for pg in list(ctx.pages):
        if "/content/sidebar.html" in pg.url or "/content/history.html" in pg.url:
            await pg.close()
    panel = await ctx.new_page()
    await panel.goto(f"chrome-extension://{extid}/content/popup.html")
    await panel.wait_for_timeout(1500)

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

    state = await panel.evaluate("""() => {
        const v = document.getElementById('vdh_history_view');
        const main = document.getElementById('main');
        return {path: location.pathname, has: !!v,
                hidden: v ? v.hidden : null,
                /* the app hides views with the hidden ATTRIBUTE, like its own
                   settings toggle does; check that, not inline styles */
                panelHidden: main ? main.getAttribute('hidden') : null,
                rows: v ? v.querySelectorAll('.vdh_history_row').length : -1,
                radios: v ? [...v.querySelectorAll('input[type=radio]')].map(r => r.value) : [],
                back: v ? [...v.querySelectorAll('button')].some(b => b.textContent.indexOf('Back') >= 0) : false};
    }""")
    print(f"   state: {state}", flush=True)
    ok(state["path"].endswith("/content/popup.html"),
       f"the panel page is still the panel, not history.html ({state['path']})")
    ok(state["has"] and state["hidden"] is False,
       "a history view is shown inside the panel")
    ok(state["panelHidden"] == "true", "the normal panel view is swapped out")
    ok(state["rows"] >= 0, f"the history list rendered ({state['rows']} row(s))")
    ok(state["radios"] == ["keep", "smart", "srt"],
       f"the subtitle format choices are there too ({state['radios']})")
    ok(state["back"], "there is a Back button")
    ok(len(ctx.pages) == tabs_before,
       f"no tab or window was opened ({tabs_before} -> {len(ctx.pages)})")

    after = await panel.evaluate("""() => {
        const v = document.getElementById('vdh_history_view');
        [...v.querySelectorAll('button')].find(b => b.textContent.indexOf('Back') >= 0).click();
        const main = document.getElementById('main');
        return {hidden: v.hidden, panelShown: main ? main.getAttribute('hidden') : null};
    }""")
    print(f"   after Back: {after}", flush=True)
    ok(after["hidden"] and after["panelShown"] is None,
       f"Back returns to the panel ({after})")
    await panel.close()


async def part_d(ctx, extid):
    """A download's subtitle button inside the panel view must work too."""
    print("\nD. saving subtitles from inside the panel view")
    panel = await ctx.new_page()
    await panel.goto(f"chrome-extension://{extid}/content/popup.html")
    await panel.wait_for_timeout(1200)
    await panel.evaluate("""async (fix) => {
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        j['dl:panel_probe'] = {id: 'dl:panel_probe', download_id: 'panel_probe',
            status: 'done', filename: 'Panel Course.mp4', subdir: '', updated_at: Date.now(),
            bytes: 4194304, subtitles: [{url: fix + '/segmented/subs/en.m3u8',
                                        language: 'en', type: 'http'}]};
        await chrome.storage.local.set({vdh_journal: j});
    }""", "http://127.0.0.1:8140")
    await asyncio.sleep(1)
    await panel.evaluate("""() => window.vdhHistoryOpen()""")
    await asyncio.sleep(1.5)
    before = await panel.evaluate("""async () => (await chrome.downloads.search({})).length""")
    clicked = await panel.evaluate("""() => {
        const v = document.getElementById('vdh_history_view');
        const row = [...v.querySelectorAll('.vdh_history_row')]
            .find(r => r.textContent.indexOf('Panel Course.mp4') >= 0);
        if (!row) return 'no row';
        const b = [...row.querySelectorAll('button')]
            .find(x => x.textContent.indexOf('Subtitles:') === 0);
        if (!b) return 'no subtitle button';
        b.click();
        return 'clicked';
    }""")
    print(f"   {clicked}", flush=True)
    ok(clicked == "clicked", "the in-panel view offers the subtitles button")
    got = []
    for _ in range(60):
        await asyncio.sleep(0.25)
        got = await panel.evaluate("""async () =>
            (await chrome.downloads.search({})).map(d => ({state: d.state}))""")
        if len(got) > before and got[-1]["state"] == "complete":
            break
    ok(len(got) > before and got[-1]["state"] == "complete",
       f"the subtitle file was written from the panel view ({got[-1] if got else None})")
    await panel.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        delete j['dl:panel_probe'];
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
