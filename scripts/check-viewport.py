"""Every extension page must declare the device width.

Without a viewport meta tag Chromium lays these pages out at a default ~980px
and then boosts the text, which is why the panel looked about twice its intended
size on a phone. This checks the tag is present on each page and that a real
device-width viewport actually produces a device-width layout.
"""
import asyncio
import json
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
PAGES = ("popup.html", "sidebar.html", "history.html")
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


async def main():
    print("A. the tag is present in every page")
    for name in PAGES:
        text = (EXT / "content" / name).read_text(encoding="utf-8")
        ok('name="viewport"' in text and "width=device-width" in text,
           f"{name} declares width=device-width")

    print("\nB. a phone-width device gets a phone-width layout")
    async with async_playwright() as p:
        for width in (412, 360):
            ctx = await p.chromium.launch_persistent_context(
                f"/tmp/prof-vp-{width}", headless=True, channel="chromium",
                viewport={"width": width, "height": 760}, is_mobile=True,
                has_touch=True,
                args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                      "--no-first-run"])
            await asyncio.sleep(2)
            if not ctx.service_workers:
                print("FAIL: extension did not load")
                sys.exit(1)
            extid = ctx.service_workers[0].url.split("/")[2]
            page = await ctx.new_page()
            await page.goto(f"chrome-extension://{extid}/content/popup.html")
            await page.wait_for_timeout(1200)
            info = await page.evaluate("""() => ({
                inner: innerWidth,
                layout: document.documentElement.clientWidth,
                font: getComputedStyle(document.documentElement).fontSize,
                overflow: document.documentElement.scrollWidth > innerWidth + 1
            })""")
            print(f"   {width}px device: {json.dumps(info)}", flush=True)
            ok(info["layout"] == width,
               f"{width}px device lays out at {width}px, not 980 ({info['layout']})")
            ok(not info["overflow"],
               f"nothing overflows the {width}px screen ({info['overflow']})")
            ok(info["font"] == "21px",
               f"the 21px base really is 21px ({info['font']})")

            # the in-panel history view must follow the user's own text size
            await page.evaluate("""() => document.documentElement.setAttribute(
                'font_size', 'verylarge')""")
            await page.evaluate("() => window.vdhHistoryOpen()")
            await page.wait_for_timeout(1000)
            scale = await page.evaluate("""() => {
                const v = document.getElementById('vdh_history_view');
                return {html: getComputedStyle(document.documentElement).fontSize,
                        view: v ? getComputedStyle(v).fontSize : null,
                        overflow: document.documentElement.scrollWidth > innerWidth + 1};
            }""")
            print(f"   history view: {json.dumps(scale)}", flush=True)
            ok(scale["view"] == scale["html"],
               f"the history view follows the text size ({scale['view']} vs {scale['html']})")
            ok(not scale["overflow"],
               f"the history view does not overflow ({scale['overflow']})")
            await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
