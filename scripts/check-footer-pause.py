"""The footer stays put, the Pause button is readable, the thumbnail scales,
and a running download shows that subtitles were found.

Part A  the toolbar is stuck to the bottom at every text size, with a full list
        to scroll, and the page itself does not scroll
Part B  the Pause button is actually readable (it was white on white)
Part C  the thumbnail scales with the text size
Part D  the CC marker appears on a download whose stream carries subtitles,
        and does not appear when the stream has none
"""
import asyncio
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
SIZES = [15, 11, 8, 5]
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


DEEP = """function deep(root, id) {
    if (root.getElementById) { const e = root.getElementById(id); if (e) return e; }
    for (const el of root.querySelectorAll('*'))
        if (el.shadowRoot) { const r = deep(el.shadowRoot, id); if (r) return r; }
    return null;
}"""

FILL = """() => {
    const host = document.querySelector('com-main');
    const root = host && host.shadowRoot ? host.shadowRoot : document;
    const media = root.getElementById('media');
    if (media && !media.querySelector('.filler')) {
        for (let i = 0; i < 30; i++) {
            const d = document.createElement('div');
            d.className = 'filler';
            d.style.cssText = 'min-height:60px;background:#ccc;margin:4px 0';
            d.textContent = 'filler ' + i;
            media.appendChild(d);
        }
    }
    return true;
}"""

FOOTER = """() => {
    const bar = document.querySelector('com-toolbar');
    const host = document.querySelector('com-main');
    const root = host && host.shadowRoot ? host.shadowRoot : document;
    const box = root.getElementById('box_main');
    if (box) box.scrollTop = box.scrollHeight;
    const r = bar.getBoundingClientRect();
    const cs = getComputedStyle(bar);
    return {top: Math.round(r.top), bottom: Math.round(r.bottom), innerH: innerHeight,
            scrolled: box ? box.scrollTop : 0,
            canScroll: box ? box.scrollHeight > box.clientHeight + 4 : false,
            position: cs.position,
            docScrolls: document.documentElement.scrollHeight >
                        document.documentElement.clientHeight + 1};
}"""

PAUSE = "(() => {" + DEEP + """
    const host = document.querySelector('com-main');
    const root = host && host.shadowRoot ? host.shadowRoot : document;
    const media = root.getElementById('media') || root.getElementById('box_main');
    let row = media.querySelector('com-media-downloading');
    if (!row) { row = document.createElement('com-media-downloading'); media.appendChild(row); }
    const sr = row.shadowRoot;
    const btn = sr && sr.getElementById('button_pause');
    if (!btn) return null;
    const cs = getComputedStyle(btn);
    const rect = btn.getBoundingClientRect();
    return {text: btn.textContent.trim(), w: Math.round(rect.width),
            h: Math.round(rect.height), color: cs.color, bg: cs.backgroundColor};
})()"""


def lum(rgb):
    nums = [int(x) for x in rgb.replace("rgba(", "").replace("rgb(", "")
            .rstrip(")").split(",")[:3]]
    def ch(v):
        v = v / 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(v) for v in nums)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


THUMB = "(() => {" + DEEP + """
    const host = document.querySelector('com-main');
    const root = host && host.shadowRoot ? host.shadowRoot : document;
    const media = root.getElementById('media') || root.getElementById('box_main');
    let row = media && media.querySelector('com-media');
    if (!row && media) { row = document.createElement('com-media'); media.appendChild(row); }
    const sr = row && row.shadowRoot;
    const stack = sr ? sr.getElementById('stack_top') : null;
    const preview = sr ? sr.querySelector('com-media-preview') : null;
    const vid = preview && preview.shadowRoot ? preview.shadowRoot.querySelector('video') : null;
    const rect = (el) => { if (!el) return null; const b = el.getBoundingClientRect();
        return {w: Math.round(b.width), h: Math.round(b.height)}; };
    return {stack: rect(stack), video: rect(vid)};
})()"""

BADGE = """async (args) => {
    const [id, withSubs] = args;
    const host = document.querySelector('com-main');
    const root = host && host.shadowRoot ? host.shadowRoot : document;
    const media = root.getElementById('media') || root.getElementById('box_main');
    let row = media.querySelector('com-media-downloading');
    if (!row) { row = document.createElement('com-media-downloading'); media.appendChild(row); }
    row.download_id = id;
    await chrome.storage.local.set({vdh_journal: {
        ['dl:' + id]: {id: 'dl:' + id, download_id: id, status: 'running',
            filename: 'Lecture 4.mp4', bytes: 10, percent: 1,
            subtitles: withSubs ? [{url: 'https://example.com/en.vtt', language: 'en'},
                                   {url: 'https://example.com/es.vtt', language: 'es'}]
                                : []}
    }});
    await new Promise((r) => setTimeout(r, 600));
    const sr = row.shadowRoot;
    const badge = sr ? sr.getElementById('vdh_subs_badge') : null;
    return badge ? {hidden: badge.hidden, text: badge.textContent.trim(),
                    title: badge.title, w: Math.round(badge.getBoundingClientRect().width)}
                 : null;
}"""


async def set_size(page, size):
    await page.evaluate("(s) => localStorage.setItem('vdh_ui_text_size', String(s))", size)
    await page.reload()
    await page.wait_for_timeout(1000)


async def part_a(ctx, extid):
    print("\nA. the footer stays at the bottom")
    page = await ctx.new_page()
    await page.set_viewport_size({"width": 720, "height": 800})
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1200)
    for size in SIZES:
        await set_size(page, size)
        await page.evaluate(FILL)
        await page.wait_for_timeout(300)
        f = await page.evaluate(FOOTER)
        print(f"   {size}px: {f}", flush=True)
        ok(f["canScroll"], f"{size}px: the list is long enough to scroll")
        ok(f["bottom"] <= f["innerH"] + 1 and f["top"] >= 0,
           f"{size}px: the toolbar is fully on screen (top {f['top']}, "
           f"bottom {f['bottom']}, viewport {f['innerH']})")
        ok(f["bottom"] >= f["innerH"] - 2,
           f"{size}px: it is stuck to the bottom edge, not floating ({f['bottom']})")
        ok(f["position"] == "sticky", f"{size}px: it is pinned ({f['position']})")
        ok(not f["docScrolls"], f"{size}px: the page itself does not scroll")
    await page.close()


async def part_b(ctx, extid):
    print("\nB. the Pause button is readable")
    page = await ctx.new_page()
    await page.set_viewport_size({"width": 720, "height": 800})
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1200)
    for size in SIZES:
        await set_size(page, size)
        p = await page.evaluate(PAUSE)
        print(f"   {size}px: {p}", flush=True)
        assert p, "no pause button"
        ok(p["text"] == "Pause", f"{size}px: it is labelled ({p['text']!r})")
        ok(p["w"] > 20 and p["h"] > 10,
           f"{size}px: it has a real size ({p['w']}x{p['h']})")
        contrast = abs(lum(p["color"]) - lum(p["bg"]))
        ok(contrast > 0.15,
           f"{size}px: text and background are distinguishable "
           f"({p['color']} on {p['bg']}, contrast {contrast:.2f})")
    await page.close()


async def part_c(ctx, extid):
    print("\nC. the thumbnail scales")
    page = await ctx.new_page()
    await page.set_viewport_size({"width": 720, "height": 800})
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1200)
    got = {}
    for size in SIZES:
        await set_size(page, size)
        got[size] = await page.evaluate(THUMB)
        print(f"   {size}px: {got[size]}", flush=True)
    ok(got[15]["stack"]["h"] > got[8]["stack"]["h"] > got[5]["stack"]["h"],
       f"the thumbnail shrinks with the text "
       f"({got[15]['stack']['h']} -> {got[8]['stack']['h']} -> {got[5]['stack']['h']}px tall)")
    ok(got[15]["video"]["w"] > got[5]["video"]["w"] * 1.5,
       f"the video inside it shrinks too "
       f"({got[15]['video']['w']} -> {got[5]['video']['w']}px wide)")
    await page.close()


async def part_d(ctx, extid):
    print("\nD. the subtitle marker")
    page = await ctx.new_page()
    await page.set_viewport_size({"width": 720, "height": 800})
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1200)
    with_subs = await page.evaluate(BADGE, ["subs_yes_1", True])
    print(f"   with subtitles: {with_subs}", flush=True)
    ok(with_subs is not None and not with_subs["hidden"],
       f"a stream with subtitles shows a marker ({with_subs})")
    ok(with_subs and "en" in with_subs["text"] and "es" in with_subs["text"],
       f"the marker names the languages ({with_subs and with_subs['text']!r})")
    ok(with_subs and with_subs["w"] > 10,
       f"the marker is actually drawn ({with_subs and with_subs['w']}px wide)")
    without = await page.evaluate(BADGE, ["subs_no_1", False])
    print(f"   without subtitles: {without}", flush=True)
    ok(without is None or without["hidden"],
       f"a stream without subtitles shows nothing ({without})")
    await page.close()


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-footer", headless=True, channel="chromium",
            viewport={"width": 720, "height": 800},
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
