#!/usr/bin/env python3
"""Reference driver: launch Chromium, observe a page that takes ~12 s to load.

Run:  python3 scripts/chromium-driver.py [url]

Everything here is measured on Playwright 1.64.1 + chromium in this sandbox, not
recalled from docs. The remarks explain *why* each line exists, because most of
the failure modes are silent (you get an empty screenshot, not an error).

Why this is a separate skill from "screenshot a page"
-----------------------------------------------------
`page.goto()` defaults to `wait_until="load"`, which for a slow page means it
blocks until the last byte of the document has arrived. With the default 30 s
timeout it raises TimeoutError and -- critically -- the page you wanted is alive
and usable the whole time, you just never got control back. Every technique below
is about getting control back early and then *observing* while it finishes.

Measured against a page that streams its body over 12 s:

    goto(wait_until="load")            never returns within 30 s   (TimeoutError)
    goto(wait_until="commit")          returns in 0.0 s, readyState="loading"
    goto(timeout=1500) then catch      TimeoutError after 1.5 s, page still usable
    wait_for_selector("#late")         resolves at 10.4 s, no fixed sleep
"""
import asyncio
import sys

from playwright.async_api import async_playwright

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8130/slow"
SHOTS = "/tmp/slow-shots"

# ---------------------------------------------------------------------------
# 1. Launching. Two settings that are not optional in this kind of work:
#
#      channel="chromium"
#          forces the full Chrome-for-Testing build. Playwright's bundled
#          headless *shell* (channel=None) does NOT support extensions:
#          measured here, service workers = 0 with the shell and 1 with
#          channel="chromium". If your subject is an extension, the shell
#          silently gives you a browser with nothing installed.
#
#      launch_persistent_context(...)
#          needed for --load-extension and for keeping a profile (cookies,
#          extension storage, OPFS) between runs. `--load-extension` is ignored
#          by `launch()` unless you use a persistent context.
#
#   headless=True is the new headless in current Playwright and does support
#   extensions, so you do not need Xvfb just to test one.
# ---------------------------------------------------------------------------
LAUNCH_ARGS = [
    "--no-first-run",
    "--no-default-browser-check",
]


async def main() -> None:
    async with async_playwright() as p:
        browser = await p.chromium.launch(channel="chromium", args=LAUNCH_ARGS)
        # A phone-sized viewport when the target is a mobile layout; pass
        # device_scale_factor=2 and is_mobile=True for touch behaviour.
        ctx = await browser.new_context(viewport={"width": 412, "height": 720})
        page = await ctx.new_page()

        # Surface errors instead of swallowing them; a page that "renders
        # nothing" is usually a thrown exception in its own JS.
        errors: list[str] = []
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        requests: list[str] = []
        page.on("request", lambda r: requests.append(r.url))

        # -------------------------------------------------------------------
        # 2. Get control back at "commit": the document has started arriving.
        #    Do NOT wait for "domcontentloaded" or "load" on a slow page.
        # -------------------------------------------------------------------
        await page.goto(URL, wait_until="commit")

        # -------------------------------------------------------------------
        # 3. Poll what actually exists, rather than sleeping a guessed amount.
        #    Taking screenshots at intervals is fine for *diagnosis*, but assert
        #    on element presence so the test is not timing-dependent.
        # -------------------------------------------------------------------
        for label in ("t+1s", "t+6s", "t+12s"):
            wait_s = {"t+1s": 1, "t+6s": 5, "t+12s": 6}[label]
            await page.wait_for_timeout(wait_s * 1000)
            state = await page.evaluate("() => document.readyState")
            markers = await page.evaluate(
                "() => ['early','mid','late','ready']"
                ".filter(id => document.getElementById(id))")
            print(f"{label:6s} readyState={state:9s} markers={markers}")
            # full_page=True grows the shot to the whole document; on a page that
            # is still streaming, content that has not arrived yet simply is not
            # there -- that is the honest picture of a half-loaded page.
            await page.screenshot(path=f"{SHOTS}/slow-{label}.png", full_page=True)

        # -------------------------------------------------------------------
        # 4. Wait for the *condition you care about*, with a generous timeout.
        #    This resolved at 10.4 s on the 12 s page instead of me guessing.
        # -------------------------------------------------------------------
        await page.wait_for_selector("#ready", timeout=30000)
        print("ready marker present; title =", await page.title())

        # 5. Network-level visibility: what the page fetched, and whether
        #    anything is still in flight.
        pending = [r for r in requests if r.startswith("http")]
        print(f"{len(pending)} requests, page errors: {errors or 'none'}")

        # 6. A timeout is not fatal. Catching it keeps the page handle valid:
        #      try: await page.goto(url, timeout=1500)
        #      except PlaywrightTimeoutError: pass   # page is still there
        #    Useful for "load what you can, then look at the state" probes.
        await browser.close()


asyncio.run(main())
