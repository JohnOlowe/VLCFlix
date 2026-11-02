import asyncio, pathlib, sys, shutil
from playwright.async_api import async_playwright
async def main():
    ext = pathlib.Path(sys.argv[1]).resolve()
    prof = "/tmp/prof-domcheck"; shutil.rmtree(prof, ignore_errors=True)
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(prof, headless=True, channel="chromium",
            viewport={"width":412,"height":720},
            args=[f"--disable-extensions-except={ext}", f"--load-extension={ext}","--no-first-run"])
        page = await ctx.new_page(); await page.goto("http://127.0.0.1:8123/index.html"); await asyncio.sleep(3)
        extid = ctx.service_workers[0].url.split("/")[2]
        panel = await ctx.new_page(); errs=[]
        panel.on("pageerror", lambda e: errs.append(str(e)[:100]))
        await panel.goto(f"chrome-extension://{extid}/content/popup.html"); await panel.wait_for_timeout(3500)
        info = await panel.evaluate("""() => ({
          main: !!document.querySelector('com-main'),
          toolbar: !!document.querySelector('com-toolbar'),
          settings: !!document.querySelector('com-settings'),
          toolbarShadow: !!(document.querySelector('com-toolbar')||{}).shadowRoot,
          rootFont: getComputedStyle(document.documentElement).fontSize,
          toolbarFont: (() => { const t = document.querySelector('com-toolbar');
             return t ? getComputedStyle(t).fontSize : null; })(),
          bodyH: document.body.getBoundingClientRect().height,
          vp: innerHeight,
        })""")
        print(info); print("errors:", errs)
        await ctx.close()
asyncio.run(main())
