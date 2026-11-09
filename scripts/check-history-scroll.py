"""The history page has to scroll to everything on it.

What was reported: with a handful of interrupted downloads the "Storage &
interrupted downloads" box filled the screen, and what sat below it could not be
scrolled to -- including the app's own "remember my download history" prompt and
the Clear history button.

The cause was not the box alone: `html` is `overflow: hidden`, and `#main` -- the
element meant to scroll -- is a flex child that will not shrink, so it grows to
its content height, its own `overflow-y: auto` never engages, and everything past
the bottom of the screen is simply clipped.

Part A  #main is a real scroll container: no taller than the space it has
Part B  with a dozen interrupted downloads the box stays a fraction of the
        screen, and scrolling reaches the very last thing on the page
Part C  the box folds away with one tap on its heading
Part D  the app's own "Enable History" prompt is reachable and works, so history
        can actually be turned on -- and the controls it reveals are reachable too
Part E  same in the overlay the phone really opens (the panel's history view),
        and at the smallest and largest text sizes
"""
import asyncio
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
RECORDS = 12
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def job(i):
    return {
        "id": f"dl:scroll_{i}", "download_id": f"scroll_{i}",
        "filename": f"Interrupted Movie Number {i} With A Long Title.mp4",
        "status": "running", "percent": 40 + i, "bytes": 400000000,
        "updated_at": 1000 - i, "media_hash": f"scroll_hash_{i}",
    }


async def seed(page, n):
    await page.evaluate("""async (recs) => {
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        for (const r of recs) j[r.id] = r;
        await chrome.storage.local.set({vdh_journal: j});
    }""", [job(i) for i in range(n)])
    await page.reload()
    await page.wait_for_timeout(1600)


async def geometry(page):
    return await page.evaluate("""() => {
        const main = document.getElementById('main');
        const box = document.getElementById('vdh_journal_box');
        const list = document.getElementById('vdh_journal_list');
        const vh = window.innerHeight;
        const r = el => el ? Math.round(el.getBoundingClientRect().height) : null;
        return {
            vh: vh,
            mainH: r(main),
            mainScrollable: main.scrollHeight > main.clientHeight + 2,
            mainScrollHeight: main.scrollHeight,
            mainClientHeight: main.clientHeight,
            boxH: r(box),
            listH: r(list),
            listScrollable: list ? list.scrollHeight > list.clientHeight + 2 : null,
            rows: document.querySelectorAll('div.vdh_journal_row').length,
        };
    }""")


async def scroll_main_to_bottom(page):
    """Scroll the page container to its end; is the storage box fully in view?"""
    return await page.evaluate("""async () => {
        const main = document.getElementById('main');
        main.scrollTop = main.scrollHeight;
        await new Promise(r => setTimeout(r, 350));
        const box = document.getElementById('vdh_journal_box');
        const rb = box.getBoundingClientRect();
        const vh = window.innerHeight;
        return {
            scrollTop: Math.round(main.scrollTop),
            maxScroll: Math.round(main.scrollHeight - main.clientHeight),
            boxTop: Math.round(rb.top), boxBottom: Math.round(rb.bottom),
            boxFullyVisible: rb.top >= -2 && rb.bottom <= vh + 2,
            vh: vh,
        };
    }""")


async def scroll_list_to_bottom(page):
    """Scroll the box's own list to its end; is the last row and the purge
    button actually on screen?"""
    return await page.evaluate("""async () => {
        const list = document.getElementById('vdh_journal_list');
        const box = document.getElementById('vdh_journal_box');
        const purge = document.getElementById('vdh_purge_blobs');
        list.scrollTop = list.scrollHeight;
        await new Promise(r => setTimeout(r, 350));
        const rows = list.querySelectorAll('div.vdh_journal_row');
        const last = rows[rows.length - 1];
        const vh = window.innerHeight;
        const lr = last ? last.getBoundingClientRect() : null;
        const pr = purge ? purge.getBoundingClientRect() : null;
        const rr = box.getBoundingClientRect();
        return {
            listScrollTop: Math.round(list.scrollTop),
            listMaxScroll: Math.round(list.scrollHeight - list.clientHeight),
            rows: rows.length,
            lastRowVisible: lr ? (lr.bottom <= vh + 2 && lr.bottom > 0) : null,
            lastRowBottom: lr ? Math.round(lr.bottom) : null,
            purgeVisible: pr ? (pr.bottom <= vh + 2 && pr.bottom > 0) : null,
            boxBottom: Math.round(rr.bottom),
            vh: vh,
        };
    }""")


async def part_a(page, label):
    print(f"\nA. #main is a real scroll container ({label})")
    g = await geometry(page)
    print(f"   {g}", flush=True)
    ok(g["rows"] == RECORDS, f"all {RECORDS} rows are listed ({g['rows']})")
    ok(g["mainH"] <= g["vh"] + 2,
       f"#main is no taller than the space it has ({g['mainH']} vs {g['vh']})")

    # now make the content genuinely taller than the screen and scroll it
    await page.set_viewport_size({"width": 412, "height": 420})
    await page.wait_for_timeout(600)
    g2 = await geometry(page)
    print(f"   short screen: {g2}", flush=True)
    ok(g2["mainH"] <= g2["vh"] + 2,
       f"#main still fits the short screen ({g2['mainH']} vs {g2['vh']})")
    ok(g2["mainScrollable"],
       f"and this time it has to scroll ({g2['mainScrollHeight']} > "
       f"{g2['mainClientHeight']})")
    bottom = await scroll_main_to_bottom(page)
    print(f"   bottom: {bottom}", flush=True)
    ok(bottom["boxFullyVisible"] is True,
       f"scrolling the page brings the whole storage box into view "
       f"(top={bottom['boxTop']} bottom={bottom['boxBottom']} vh={bottom['vh']})")
    await page.set_viewport_size({"width": 412, "height": 800})
    await page.wait_for_timeout(600)
    return g


async def part_b(page, label):
    print(f"\nB. the box cannot fill the screen ({label})")
    g = await geometry(page)
    share = g["boxH"] / g["vh"]
    print(f"   box {g['boxH']}px of a {g['vh']}px screen ({share:.0%}), "
          f"list {g['listH']}px, list scrolls: {g['listScrollable']}", flush=True)
    ok(share <= 0.60,
       f"the box takes at most 60% of the screen ({g['boxH']}px, {share:.0%})")
    ok(g["listScrollable"],
       "the list of interrupted downloads scrolls inside its own height")
    lb = await scroll_list_to_bottom(page)
    print(f"   list bottom: {lb}", flush=True)
    ok(lb["lastRowVisible"] is True,
       f"the last of the {lb['rows']} rows can be scrolled to "
       f"(bottom={lb['lastRowBottom']} vh={lb['vh']})")
    ok(lb["purgeVisible"] is True,
       f"and so can the Purge button below the list ({lb['boxBottom']})")
    return g


async def part_c(page, label):
    print(f"\nC. the box folds away ({label})")
    before = await page.evaluate(
        "() => Math.round(document.getElementById('vdh_journal_box')"
        ".getBoundingClientRect().height)")
    await page.evaluate("""() => {
        const d = document.getElementById('vdh_journal_details');
        d.querySelector('summary').click();
    }""")
    await page.wait_for_timeout(500)
    after = await page.evaluate(
        "() => Math.round(document.getElementById('vdh_journal_box')"
        ".getBoundingClientRect().height)")
    print(f"   {before}px -> {after}px", flush=True)
    ok(after < before * 0.5,
       f"tapping the heading collapses it ({before}px -> {after}px)")
    await page.evaluate("""() => {
        document.getElementById('vdh_journal_details').querySelector('summary').click();
    }""")
    await page.wait_for_timeout(400)
    reopened = await page.evaluate(
        "() => Math.round(document.getElementById('vdh_journal_box')"
        ".getBoundingClientRect().height)")
    ok(reopened > after * 1.5, f"and tapping again opens it ({after}px -> {reopened}px)")


async def part_d(page, label):
    print(f"\nD. history can be turned on ({label})")
    state = await page.evaluate("""() => {
        const btn = document.getElementById('button_enable_history');
        const dis = document.getElementById('history_disabled');
        return {
            hasButton: !!btn,
            disabledShown: dis ? getComputedStyle(dis).display !== 'none' : null,
            text: dis ? dis.innerText.slice(0, 60) : null,
        };
    }""")
    print(f"   {state}", flush=True)
    ok(state["hasButton"], "the app's 'remember my download history' prompt exists")
    clicked = await page.evaluate("""() => {
        const btn = document.getElementById('button_enable_history');
        if (!btn) return 'no button';
        btn.scrollIntoView({block: 'center'});
        const r = btn.getBoundingClientRect();
        if (r.top < 0 || r.bottom > window.innerHeight) return 'off screen';
        btn.click();
        return 'clicked';
    }""")
    ok(clicked == "clicked", f"the Enable History button is reachable ({clicked})")
    await page.wait_for_timeout(1200)
    await page.reload()
    await page.wait_for_timeout(1600)
    after = await page.evaluate("""() => {
        const dis = document.getElementById('history_disabled');
        const ctl = document.getElementById('history_controls');
        const clear = document.getElementById('button_clear_history');
        const vis = el => el ? getComputedStyle(el).display !== 'none' &&
                                el.getBoundingClientRect().height > 0 : null;
        return {disabledShown: vis(dis), controlsShown: vis(ctl), clearShown: vis(clear)};
    }""")
    print(f"   after enabling: {after}", flush=True)
    ok(after["disabledShown"] is False,
       "the 'we do not record your history' prompt is gone once enabled")
    ok(after["controlsShown"] or after["clearShown"],
       f"the history controls below the box are reachable and shown ({after})")
    # with the controls visible the page is taller: it still has to scroll
    await page.set_viewport_size({"width": 412, "height": 420})
    await page.wait_for_timeout(600)
    g = await geometry(page)
    bottom = await scroll_main_to_bottom(page)
    print(f"   bottom with history on, short screen: {bottom}", flush=True)
    ok(g["mainScrollable"], f"the page overflows and scrolls ({g['mainH']} vs {g['vh']})")
    reach = await page.evaluate("""async () => {
        const box = document.getElementById('vdh_journal_box');
        box.scrollIntoView({block: 'center'});
        await new Promise(r => setTimeout(r, 350));
        const r = box.getBoundingClientRect();
        const vh = window.innerHeight;
        return {top: Math.round(r.top), bottom: Math.round(r.bottom), vh: vh,
                visible: r.top >= -2 && r.bottom <= vh + 2,
                enough: r.height <= vh};
    }""")
    print(f"   box reachable: {reach}", flush=True)
    ok(reach["visible"] is True,
       f"and the storage box can be scrolled into view "
       f"(top={reach['top']} bottom={reach['bottom']} vh={reach['vh']})")
    ok(await page.evaluate("""async () => {
        const main = document.getElementById('main');
        main.scrollTop = main.scrollHeight;
        await new Promise(r => setTimeout(r, 300));
        const ctl = document.getElementById('history_controls');
        const r = ctl.getBoundingClientRect();
        return r.height > 0 && r.bottom <= window.innerHeight + 2 && r.bottom > 0;
    }"""), "the history controls below the box can be reached too")
    await page.set_viewport_size({"width": 412, "height": 800})
    await page.wait_for_timeout(500)
    return g


async def main():
    async with async_playwright() as p:
        sizes = [{"width": 412, "height": 800}, {"width": 360, "height": 640}]
        for vp in sizes:
            label = f"{vp['width']}x{vp['height']}"
            prof = f"/tmp/prof-hists-{vp['width']}"
            ctx = await p.chromium.launch_persistent_context(
                prof, headless=True, channel="chromium", viewport=vp,
                args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                      "--no-first-run"])
            await asyncio.sleep(2)
            if not ctx.service_workers:
                print("FAIL: extension did not load")
                sys.exit(1)
            extid = ctx.service_workers[0].url.split("/")[2]
            page = await ctx.new_page()
            await page.goto(f"chrome-extension://{extid}/content/history.html")
            await page.wait_for_timeout(1200)
            await seed(page, RECORDS)
            await part_a(page, label)
            await part_b(page, label)
            await part_c(page, label)
            await part_d(page, label)

            # --- the overlay the phone really opens -------------------------
            print(f"\nE. through the panel's history view ({label})")
            popup = await ctx.new_page()
            await popup.goto(f"chrome-extension://{extid}/content/popup.html")
            await popup.wait_for_timeout(1400)
            await popup.evaluate("() => window.vdhHistoryOpen()")
            frame = None
            for _ in range(40):
                await asyncio.sleep(0.25)
                frame = next((f for f in popup.frames
                              if "/content/history.html" in (f.url or "")), None)
                if frame:
                    break
            ok(frame is not None, "the history view opens inside the panel")
            if frame:
                await frame.wait_for_timeout(1400)
                g = await geometry(frame)
                print(f"   {g}", flush=True)
                ok(g["mainH"] <= g["vh"] + 2,
                   f"#main fits the overlay ({g['mainH']} vs {g['vh']})")
                share = g["boxH"] / g["vh"]
                ok(share <= 0.60,
                   f"the box stays a fraction of the overlay ({g['boxH']}px, "
                   f"{share:.0%})")
                bottom = await scroll_main_to_bottom(frame)
                lb = await scroll_list_to_bottom(frame)
                print(f"   bottom: {bottom} list: {lb}", flush=True)
                ok(bottom["boxFullyVisible"] is True,
                   f"the overlay page scrolls to the whole box ({bottom['boxBottom']})")
                ok(lb["lastRowVisible"] is True,
                   "and the last interrupted download can be reached inside it")

            # --- the smallest and largest text sizes ------------------------
            for px in (5, 15):
                print(f"\n   text size {px}px ({label})")
                await page.evaluate("""async (px) => {
                    await chrome.storage.local.set({vdh_ui_text_size: px});
                }""", px)
                await page.reload()
                await page.wait_for_timeout(1600)
                g = await geometry(page)
                lb = await scroll_list_to_bottom(page)
                print(f"   {g}", flush=True)
                ok(g["mainH"] <= g["vh"] + 2,
                   f"#main fits the screen at {px}px ({g['mainH']} vs {g['vh']})")
                ok(g["boxH"] <= g["vh"] * 0.6,
                   f"the box stays a fraction of the screen at {px}px "
                   f"({g['boxH']} of {g['vh']})")
                ok(lb["purgeVisible"] is True,
                   f"and the bottom of the box is reachable at {px}px")
            await ctx.close()

    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
