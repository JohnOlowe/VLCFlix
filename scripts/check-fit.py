"""Does the layout follow the text size, and does anything force a sideways swipe?

Part A  the side margins shrink with the text instead of sitting at a floor
Part B  the toolbar's seven icons end up on one line at a small enough size
Part C  nothing is wider than the screen -- panel, settings, history page and
        the history view embedded in the panel -- at 360px and at 720px

The history page is checked with real content: journal rows are seeded the way
check-journal.py seeds them, and a media row with a very long file name is
built from the real component, because that is the row that used to run off
the side of the screen.
"""
import asyncio
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
SIZES = [15, 11, 8, 5]
WIDTHS = [360, 720]
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

# Every rendered element, light DOM and shadow DOM alike: the worst thing that
# sticks out past the right edge of the screen, plus the page's own scroll span.
OVERFLOW = "(() => {" + DEEP + """
    let worst = null;
    const walk = (root) => {
        for (const el of root.querySelectorAll('*')) {
            const r = el.getBoundingClientRect();
            if (r.width > 0 && r.height > 0 && r.right > innerWidth + 1) {
                if (!worst || r.right > worst.right)
                    worst = {tag: el.tagName, id: el.id, cls: el.className && String(el.className).slice(0, 40),
                             right: Math.round(r.right), w: Math.round(r.width)};
            }
            if (el.shadowRoot) walk(el.shadowRoot);
        }
    };
    walk(document);
    return {scrollW: document.documentElement.scrollWidth, inner: innerWidth,
            bodyScroll: document.body ? document.body.scrollWidth : 0,
            worst: worst};
})()"""

MARGINS = """() => {
    const cs = getComputedStyle(document.body);
    /* a custom property is returned as its token stream, not as pixels, so
       measure the used value instead: put it on a probe and read the margin */
    const probe = document.createElement('div');
    probe.style.marginLeft = 'var(--spacing-medium)';
    probe.style.position = 'absolute';
    document.body.appendChild(probe);
    const spacing = parseFloat(getComputedStyle(probe).marginLeft);
    probe.remove();
    return {padL: parseFloat(cs.paddingLeft), padR: parseFloat(cs.paddingRight),
            spacing: spacing};
}"""

TOOLBAR_ROWS = "(() => {" + DEEP + """
    const bar = deep(document, 'toolbar');
    if (!bar) return null;
    const row = bar.shadowRoot ? bar.shadowRoot.getElementById('toolbar')
                               : bar.querySelector('#toolbar');
    const target = row || bar;
    const kids = [...target.children].filter((el) => {
        const r = el.getBoundingClientRect();
        return r.width > 0 && r.height > 0;
    });
    /* items on one line overlap vertically but do not share a top: a taller
       button starts higher, so counting distinct tops over-counts the rows */
    const spans = kids.map((el) => {
        const r = el.getBoundingClientRect();
        return [r.top, r.bottom];
    }).sort((a, b) => a[0] - b[0]);
    let rows = 0, lineEnd = -Infinity;
    for (const [top, bottom] of spans) {
        if (top > lineEnd - 2) { rows++; lineEnd = bottom; } else { lineEnd = Math.max(lineEnd, bottom); }
    }
    const tops = spans.map((s) => Math.round(s[0]));
    const rect = target.getBoundingClientRect();
    return {count: kids.length, rows: rows,
            tops: tops,
            firstIcon: (() => {
                const svg = target.querySelector('svg');
                if (!svg) return null;
                const r = svg.getBoundingClientRect();
                return {w: Math.round(r.width), h: Math.round(r.height)};
            })(),
            barW: Math.round(rect.width), barH: Math.round(rect.height),
            barScroll: target.scrollWidth, barClient: target.clientWidth};
})()"""

LONG_NAME = ("My Very Long Downloaded Lecture File Name That Should Wrap "
             "Instead Of Running Off The Side Of The Screen - Part 4 (2026).mp4")

SEED = """async (args) => {
    const [name, longName] = args;
    /* the journal rows the history page renders for interrupted downloads */
    /* the same fields the app writes for a download it is tracking */
    await chrome.storage.local.set({vdh_journal: {
        'dl:crashed_dl_1': {id: 'dl:crashed_dl_1', download_id: 'crashed_dl_1',
            status: 'running', filename: name, bytes: 1234567, percent: 37.5,
            media_hash: 'media_hash_1', updated_at: Date.now() - 60000,
            download_args: {download_id: 'crashed_dl_1'}},
        'dl:lost_one': {id: 'dl:lost_one', download_id: 'lost_one',
            status: 'running', filename: name, bytes: 12345, percent: 5,
            updated_at: Date.now() - 120000}
    }});
    /* a real media row, with a file name long enough to be the worst case */
    const box = document.getElementById('downloaded_container');
    if (box && !box.querySelector('com-media-downloaded')) {
        const row = document.createElement('com-media-downloaded');
        box.appendChild(row);
        await new Promise((r) => setTimeout(r, 300));
        const p = row.shadowRoot && row.shadowRoot.querySelector('.media_filename');
        if (p) p.textContent = longName;
    }
    return true;
}"""


async def set_size(page, size):
    await page.evaluate("(s) => localStorage.setItem('vdh_ui_text_size', String(s))", size)
    await page.reload()
    await page.wait_for_timeout(900)


async def open_settings(page):
    await page.evaluate("(() => {" + DEEP +
                        " deep(document, 'toolbar_button_settings').click(); })()")
    for _ in range(60):
        await asyncio.sleep(0.1)
        if await page.evaluate("(() => {" + DEEP +
                               " return !!deep(document, 'vdh_text_size_box'); })()"):
            return True
    return False


async def part_a(ctx, extid):
    print("\nA. the side margins follow the text size")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1200)
    got = {}
    for size in (15, 5):
        await set_size(page, size)
        got[size] = await page.evaluate(MARGINS)
    print(f"   15px: {got[15]}\n    5px: {got[5]}", flush=True)
    ok(got[5]["padL"] < got[15]["padL"] - 1,
       f"the panel's side margin shrinks ({got[15]['padL']:.1f}px -> {got[5]['padL']:.1f}px)")
    ok(got[5]["spacing"] < got[15]["spacing"] - 0.5,
       f"the spacing unit shrinks ({got[15]['spacing']:.1f}px -> {got[5]['spacing']:.1f}px)")
    await page.close()


async def part_b(ctx, extid):
    print("\nB. the toolbar scales, and fits one line")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1200)
    for width in WIDTHS:
        await page.set_viewport_size({"width": width, "height": 780})
        for size in SIZES:
            await set_size(page, size)
            bar = await page.evaluate(TOOLBAR_ROWS)
            print(f"   {width}px @{size}px: {bar}", flush=True)
            assert bar, "no toolbar found"
            # "at a small enough size, they are all on the same line": on a
            # 720px screen (the user's) that holds at every size; on a 360px
            # one it holds from 11px down, which is what the range is for

            want_one_line = (width >= 720) or size <= 11
            if want_one_line:
                ok(bar["rows"] == 1,
                   f"{width}px screen at {size}px text: all {bar['count']} icons on "
                   f"one line (rows {bar['rows']}, tops {bar['tops']})")
            else:
                ok(bar["rows"] <= 2,
                   f"{width}px screen at {size}px text: at most two rows "
                   f"(rows {bar['rows']})")
            ok(bar["barScroll"] <= bar["barClient"] + 1,
               f"{width}px at {size}px: the bar does not scroll sideways "
               f"({bar['barScroll']} vs {bar['barClient']})")
    # icons really scale: 15px must give a bigger icon than 5px
    await page.set_viewport_size({"width": 360, "height": 780})
    await set_size(page, 15)
    big = await page.evaluate(TOOLBAR_ROWS)
    await set_size(page, 5)
    small = await page.evaluate(TOOLBAR_ROWS)
    ok(big["firstIcon"]["w"] > small["firstIcon"]["w"] + 3,
       f"the icons scale with the text ({big['firstIcon']} -> {small['firstIcon']})")
    await page.close()


async def part_c(ctx, extid):
    print("\nC. nothing is wider than the screen")
    for width in WIDTHS:
        for size in SIZES:
            page = await ctx.new_page()
            await page.set_viewport_size({"width": width, "height": 780})
            await page.goto(f"chrome-extension://{extid}/content/popup.html")
            await page.wait_for_timeout(900)
            await set_size(page, size)

            main = await page.evaluate(OVERFLOW)
            ok(main["scrollW"] <= main["inner"] + 1 and main["worst"] is None,
               f"{width}px @{size}px panel: fits ({main['scrollW']} vs "
               f"{main['inner']}, worst {main['worst']})")

            assert await open_settings(page), f"settings did not open ({width}/{size})"
            await page.wait_for_timeout(400)
            settings = await page.evaluate(OVERFLOW)
            ok(settings["scrollW"] <= settings["inner"] + 1 and settings["worst"] is None,
               f"{width}px @{size}px settings: fits ({settings['scrollW']} vs "
               f"{settings['inner']}, worst {settings['worst']})")
            await page.close()

            # the history page, with real rows in it
            hist = await ctx.new_page()
            await hist.set_viewport_size({"width": width, "height": 780})
            await hist.goto(f"chrome-extension://{extid}/content/history.html")
            await hist.wait_for_timeout(900)
            await set_size(hist, size)
            await hist.evaluate(SEED, [LONG_NAME, LONG_NAME])
            await hist.wait_for_timeout(1500)
            rows = await hist.evaluate(
                "() => document.querySelectorAll('.vdh_journal_row').length")
            over = await hist.evaluate(OVERFLOW)
            print(f"   {width}px @{size}px history: {rows} journal rows, {over}", flush=True)
            ok(rows >= 2, f"{width}px @{size}px: the storage rows are there ({rows})")
            ok(over["scrollW"] <= over["inner"] + 1 and over["worst"] is None,
               f"{width}px @{size}px history: fits, no sideways swipe "
               f"({over['scrollW']} vs {over['inner']}, worst {over['worst']})")
            await hist.close()


async def part_d(ctx, extid):
    print("\nD. the history view inside the panel fits too")
    page = await ctx.new_page()
    for width in WIDTHS:
        await page.set_viewport_size({"width": width, "height": 780})
        await page.goto(f"chrome-extension://{extid}/content/popup.html")
        await page.wait_for_timeout(900)
        await set_size(page, 8)
        await page.evaluate("(() => {" + DEEP + """
            const b = deep(document, 'toolbar_button_history');
            if (b) b.click(); else window.vdhHistoryOpen && window.vdhHistoryOpen();
        })()""")
        await page.wait_for_timeout(1800)
        frames = [f for f in page.frames if "history.html" in f.url]
        ok(bool(frames), f"{width}px: the history view is embedded in the panel")
        if not frames:
            continue
        await frames[0].evaluate(SEED, [LONG_NAME, LONG_NAME])
        await frames[0].wait_for_timeout(600)
        over = await frames[0].evaluate(OVERFLOW)
        print(f"   {width}px embedded: {over}", flush=True)
        ok(over["scrollW"] <= over["inner"] + 1 and over["worst"] is None,
           f"{width}px embedded history: fits ({over['scrollW']} vs "
           f"{over['inner']}, worst {over['worst']})")
    await page.close()


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-fit", headless=True, channel="chromium",
            viewport={"width": 360, "height": 780},
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
