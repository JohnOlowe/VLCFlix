"""Verify the manual text-size control.

Part A  the default is still 21px everywhere (nothing moved on its own)
Part B  the control in Settings: a real drag, the steppers, reset
Part C  the choice survives a reload, in both storages
Part D  the history page follows the choice, live and on a fresh load
Part E  the app's own Large / Very large setting still stacks on top

Every part drives the real page: the Settings button is pressed, the slider is
dragged with the mouse, and sizes are read back with getComputedStyle.
"""
import asyncio
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
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

MEASURE = "(() => {" + DEEP + """
    const cs = (el) => el ? getComputedStyle(el).fontSize : null;
    const btn = deep(document, 'toolbar_button_settings');
    const box = btn ? btn.getBoundingClientRect() : null;
    return {root: cs(document.documentElement), body: cs(document.body),
            main: cs(deep(document, 'com-main') || document.body),
            toolbar: cs(deep(document, 'toolbar') || document.body),
            button: box ? {w: Math.round(box.width), h: Math.round(box.height),
                           font: cs(btn)} : null,
            innerWidth: window.innerWidth};
})()"""


async def open_settings(page):
    """Settings is a view inside the panel, built on demand, so press the real
    button and wait for the card the same way a finger would find it."""
    await page.evaluate("(() => {" + DEEP + """
        const b = deep(document, 'toolbar_button_settings');
        if (!b) throw new Error('settings button missing');
        b.click();
    })()""")
    for _ in range(60):
        await asyncio.sleep(0.1)
        if await page.evaluate("(() => {" + DEEP +
                               " return !!deep(document, 'vdh_text_size_box'); })()"):
            return True
    return False


async def state(page):
    return await page.evaluate("(() => {" + DEEP + """
        const box = deep(document, 'vdh_text_size_box');
        if (!box) return null;
        const r = deep(document, 'vdh_text_size_range'),
              v = deep(document, 'vdh_text_size_value'),
              p = deep(document, 'vdh_text_size_preview');
        const rect = r.getBoundingClientRect();
        return {value: Number(r.value), min: r.min, max: r.max,
                readout: v.textContent.trim(),
                previewFont: getComputedStyle(p).fontSize,
                rootFont: getComputedStyle(document.documentElement).fontSize,
                rect: {x: rect.x, y: rect.y, w: rect.width, h: rect.height},
                right: Math.round(box.getBoundingClientRect().right)};
    })()""")


async def part_a(ctx, extid):
    print("\nA. the default is the 15px ceiling")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1500)
    # start from a clean slate: nothing saved yet
    await page.evaluate("""async () => {
        await chrome.storage.local.remove('vdh_ui_text_size');
        localStorage.removeItem('vdh_ui_text_size');
    }""")
    await page.reload()
    await page.wait_for_timeout(1200)
    m = await page.evaluate(MEASURE)
    print(f"   {m}", flush=True)
    ok(m["root"] == "15px", f"root font-size is 15px by default ({m['root']})")
    ok(m["main"] == "15px", f"com-main is 15px ({m['main']})")
    ok(m["toolbar"] == "15px", f"the toolbar is 15px ({m['toolbar']})")
    ok(m["button"] and m["button"]["font"] == "15px",
       f"a toolbar button is 15px ({m['button']})")
    return page


async def part_b(page):
    print("\nB. the control in Settings")
    ok(await open_settings(page), "pressing Settings shows the text size card")
    s0 = await state(page)
    print(f"   initial: {s0}", flush=True)
    ok(s0 is not None, "the card is in the Settings view")
    ok(s0 and s0["min"] == "5" and s0["max"] == "15",
       f"the slider spans 5..15 px ({s0 and (s0['min'], s0['max'])})")
    ok(s0 and s0["value"] == 15 and s0["readout"] == "15 px",
       f"it starts on the 15px ceiling ({s0 and (s0['value'], s0['readout'])})")
    ok(s0 and s0["right"] <= 412, f"the card fits the panel width (right {s0 and s0['right']})")

    # a real drag: press on the slider near its right end, the way a thumb moves
    await page.evaluate("(() => {" + DEEP +
                        " deep(document, 'vdh_text_size_box').scrollIntoView({block: 'center'}); })()")
    await page.wait_for_timeout(300)
    s0 = await state(page)
    r = s0["rect"]
    await page.mouse.click(r["x"] + r["w"] * 0.15, r["y"] + r["h"] / 2)
    await page.wait_for_timeout(400)
    s1 = await state(page)
    print(f"   after drag: {s1}", flush=True)
    ok(s1 and s1["value"] < 15, f"dragging the slider lowers the size ({s1 and s1['value']})")
    ok(s1 and s1["readout"] == f"{s1['value']} px",
       f"the readout shows the size ({s1 and s1['readout']})")
    ok(s1 and s1["rootFont"] == f"{s1['value']}px",
       f"the page really is that size ({s1 and s1['rootFont']})")
    ok(s1 and s1["previewFont"] == f"{s1['value']}px",
       f"the preview line is that size ({s1 and s1['previewFont']})")

    m1 = await page.evaluate(MEASURE)
    print(f"   {m1}", flush=True)
    ok(m1["main"] == f"{s1['value']}px", f"com-main follows ({m1['main']})")
    ok(20 <= m1["button"]["h"] < 44,
       f"the toolbar shrinks with the text, floored at 20px ({m1['button']})")

    # the steppers move it one pixel at a time
    base = s1["value"]
    await page.evaluate("(() => {" + DEEP + " return deep(document, 'vdh_text_size_plus').click(); })()")
    await page.wait_for_timeout(250)
    up = await state(page)
    ok(up and up["value"] == base + 1, f"+ adds one pixel ({base} -> {up and up['value']})")
    await page.evaluate("(() => {" + DEEP + " return deep(document, 'vdh_text_size_minus').click(); })()")
    await page.evaluate("(() => {" + DEEP + " return deep(document, 'vdh_text_size_minus').click(); })()")
    await page.wait_for_timeout(250)
    down = await state(page)
    ok(down and down["value"] == base - 1, f"- removes one pixel ({base} -> {down and down['value']})")

    # reset returns to the default
    await page.evaluate("(() => {" + DEEP + " return deep(document, 'vdh_text_size_default').click(); })()")
    await page.wait_for_timeout(300)
    reset = await state(page)
    ok(reset and reset["value"] == 15 and reset["rootFont"] == "15px",
       f"Reset returns to 15px ({reset and (reset['value'], reset['rootFont'])})")
    return s1["value"]


async def part_c(page):
    print("\nC. the choice is remembered")
    await page.evaluate("(() => {" + DEEP + " return window.vdhTextSizeUI.set(11); })()")
    await page.wait_for_timeout(400)
    stored = await page.evaluate("""async () => {
        const got = await chrome.storage.local.get('vdh_ui_text_size');
        return {storage: got.vdh_ui_text_size, local: localStorage.getItem('vdh_ui_text_size')};
    }""")
    print(f"   stored: {stored}", flush=True)
    ok(stored["storage"] == 11, f"chrome.storage holds 11 ({stored['storage']})")
    ok(stored["local"] == "11", f"localStorage holds 11 ({stored['local']})")

    await page.reload()
    await page.wait_for_timeout(1400)
    m = await page.evaluate(MEASURE)
    ok(m["root"] == "11px", f"a fresh load comes back at 11px ({m['root']})")
    ok(await open_settings(page), "Settings still opens after the reload")
    s = await state(page)
    ok(s and s["value"] == 11 and s["readout"] == "11 px",
       f"the slider shows the saved size ({s and (s['value'], s['readout'])})")


async def part_d(ctx, extid, page):
    print("\nD. the history page follows")
    # fresh load: the history page on its own reads the same saved size
    hist = await ctx.new_page()
    await hist.goto(f"chrome-extension://{extid}/content/history.html")
    await hist.wait_for_timeout(1200)
    h = await hist.evaluate("""() => ({
        root: getComputedStyle(document.documentElement).fontSize,
        p: (() => { const el = document.querySelector('p');
                    return el ? getComputedStyle(el).fontSize : null; })()
    })""")
    print(f"   history page: {h}", flush=True)
    ok(h["root"] == "11px", f"the history page opens at 11px, not the vendor 12px ({h['root']})")

    # live: change the size in the panel, the open history page must follow
    await page.bring_to_front()
    await page.evaluate("(() => window.vdhTextSizeUI.set(7))()")
    await page.wait_for_timeout(500)
    live = await hist.evaluate(
        "() => getComputedStyle(document.documentElement).fontSize")
    ok(live == "7px", f"an already open history page resizes itself ({live})")

    # and the embedded history view inside the panel, which is an iframe
    await page.evaluate("(() => {" + DEEP + """
        const b = deep(document, 'toolbar_button_history');
        if (b) b.click(); else window.vdhHistoryOpen && window.vdhHistoryOpen();
    })()""")
    await page.wait_for_timeout(1800)
    frames = [f for f in page.frames if "history.html" in f.url]
    emb = None
    if frames:
        emb = await frames[0].evaluate(
            "() => getComputedStyle(document.documentElement).fontSize")
    print(f"   embedded history ({len(frames)} frame(s)): {emb}", flush=True)
    ok(emb == "7px", f"the history view inside the panel is 7px too ({emb})")
    await hist.close()


async def part_e(page):
    print("\nE. the app's own text-size setting still stacks on top")
    for attr, factor in (("large", 1.0952), ("verylarge", 1.1905)):
        got = await page.evaluate("""async (a) => {
            document.documentElement.setAttribute('font_size', a);
            await new Promise((r) => requestAnimationFrame(r));
            const v = parseFloat(getComputedStyle(document.documentElement).fontSize);
            document.documentElement.removeAttribute('font_size');
            return v;
        }""", attr)
        want = round(7 * factor, 1)
        ok(abs(got - want) < 0.6,
           f"font_size={attr} on a 7px base gives {got:.1f}px (want ~{want}px)")


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-textsize", headless=True, channel="chromium",
            viewport={"width": 412, "height": 900},
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)
        page = await part_a(ctx, extid)
        await part_b(page)
        await part_c(page)
        await part_d(ctx, extid, page)
        await part_e(page)
        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
