import asyncio, pathlib, sys, shutil
from playwright.async_api import async_playwright

def variant(name):
    return pathlib.Path("/tmp/variants") / name

async def probe(p, name):
    ext = variant(name)
    prof = pathlib.Path("/tmp/prof-" + name)
    shutil.rmtree(prof, ignore_errors=True)
    ctx = await p.chromium.launch_persistent_context(str(prof), headless=True, channel="chromium",
        viewport={"width": 412, "height": 720},
        args=[f"--disable-extensions-except={ext}", f"--load-extension={ext}",
              "--no-first-run", "--disable-features=DialMediaRouteProvider"])
    try:
        page = await ctx.new_page()
        await page.goto("http://127.0.0.1:8123/index.html")
        await page.wait_for_timeout(3000)
        workers = [w.url for w in ctx.service_workers]
        if not workers:
            print(f"{name:16s} NO SERVICE WORKER"); return
        extid = workers[0].split("/")[2]
        panel = await ctx.new_page()
        errs = []
        panel.on("pageerror", lambda e: errs.append(str(e)[:90]))
        await panel.goto(f"chrome-extension://{extid}/content/popup.html")
        await panel.wait_for_timeout(4000)
        info = await panel.evaluate("""() => {
          const sr = (sel) => { const e = document.querySelector(sel); return e && e.shadowRoot; };
          const main = sr('com-main');
          const rows = document.querySelectorAll('com-media').length;
          const text = ((main && main.textContent) || document.body.innerText || '')
              .replace(/\\s+/g,' ').trim();
          return {rows, text: text.slice(0, 160)};
        }""")
        print(f"{name:16s} rows={info['rows']} text={info['text']!r} errs={errs[:2]}")
    finally:
        await ctx.close()

async def main():
    async with async_playwright() as p:
        for name in sys.argv[1:]:
            await probe(p, name)

asyncio.run(main())
