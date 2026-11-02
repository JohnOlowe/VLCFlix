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
        info = await panel.evaluate("""() => {
          const t = document.querySelector('com-toolbar');
          const sr = t && t.shadowRoot;
          const bar = sr && sr.querySelector('#toolbar');
          const icons = sr ? [...sr.querySelectorAll('#toolbar svg')] : [];
          const btns = sr ? [...sr.querySelectorAll('#toolbar button')] : [];
          const box = e => { if (!e) return null; const b = e.getBoundingClientRect();
            return [Math.round(b.width), Math.round(b.height)]; };
          return {
          iconSize: icons.length ? box(icons[0]) : null,
          iconCount: icons.length,
          buttonSize: btns.length ? box(btns[0]) : null,
          toolbarPadding: bar ? getComputedStyle(bar).padding : null,
          toolbarOverflow: bar ? (bar.scrollWidth - bar.clientWidth) : null,
          toolbarGap: bar ? getComputedStyle(bar).gap : null,
          bodyPad: getComputedStyle(document.body).paddingLeft,
          main: !!document.querySelector('com-main'),
          toolbar: !!document.querySelector('com-toolbar'),
          settings: !!document.querySelector('com-settings'),
          toolbarShadow: !!(document.querySelector('com-toolbar')||{}).shadowRoot,
          rootFont: getComputedStyle(document.documentElement).fontSize,
          toolbarFont: (() => { const t = document.querySelector('com-toolbar');
             return t ? getComputedStyle(t).fontSize : null; })(),
          bodyH: document.body.getBoundingClientRect().height,
          vp: innerHeight,
        };}""")
        print(info); print("errors:", errs)
        await ctx.close()
asyncio.run(main())
