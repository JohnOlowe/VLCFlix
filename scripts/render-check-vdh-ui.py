#!/usr/bin/env python3
"""Measure the patched UI in a real Chromium at phone viewport size.

`file://` blocks the ES module (CORS), so the tree is served over http and a
tolerant chrome.* stub is injected; the app then mounts far enough to lay
out. Pass condition: `html`/`body` == viewport height and `#main` ends
exactly where `#toolbar` starts. Two mount errors from the stub are expected.

    pip install playwright && python3 -m playwright install chromium
    scripts/render-check-vdh-ui.py [tree] [--shot DIR]

`tree` defaults to build/extension (i.e. after patch-vdh-mobile-ui.py).
"""
import asyncio
import functools
import http.server
import pathlib
import socketserver
import sys
import threading

from playwright.async_api import async_playwright

REPO = pathlib.Path(__file__).resolve().parent.parent
args = [a for a in sys.argv[1:] if not a.startswith("--")]
TREE = pathlib.Path(args[0]) if args else REPO / "build" / "extension"
SHOTS = None
if "--shot" in sys.argv:
    SHOTS = pathlib.Path(sys.argv[sys.argv.index("--shot") + 1])
PORT = 8912

MOCK = r"""
(() => {
  const mk = (path) => new Proxy(function(){}, {
    get(t, k) {
      if (k === 'then') return undefined;
      if (k === Symbol.toPrimitive) return () => '';
      if (k === 'getMessage') return (k2) => k2;
      if (k === 'getURL') return (p) => 'chrome-extension://mock' + (p.startsWith('/') ? p : '/' + p);
      if (k === 'getPlatformInfo') return async () => ({os: 'android', arch: 'arm64'});
      if (k === 'isAllowedIncognitoAccess') return async () => false;
      if (k === 'getManifest') return () => ({version: '10.6.1.2'});
      if (k === 'get' || k === 'getKey' || k === 'getAll') return async () => ({});
      if (k === 'onMessage' || k === 'onConnect' || k.endsWith('Listener') || k.startsWith('on'))
        return { addListener(){}, removeListener(){}, hasListener(){ return false; } };
      if (k === 'id') return 'mock';
      if (k === 'local' || k === 'sync' || k === 'session' || k === 'managed')
        return mk(path + '.' + String(k));
      return mk(path + '.' + String(k));
    },
    apply() { return Promise.resolve(undefined); }
  });
  const chrome = {};
  for (const ns of ['management','runtime','storage','i18n','tabs','downloads','notifications',
                    'webRequest','webNavigation','scripting','declarativeNetRequest','offscreen',
                    'sidePanel','action','contextMenus','browsingData','permissions','alarms',
                    'windows','commands'])
    chrome[ns] = mk(ns);
  window.chrome = chrome;
  window.browser = chrome;
  class FakeWS { constructor(){ setTimeout(() => this.onerror && this.onerror(new Event('error')), 0); }
    send(){} close(){} addEventListener(){} removeEventListener(){} }
  window.WebSocket = FakeWS;
})();
"""

VIEWPORTS = (("sidebar.html", 412, 915), ("popup.html", 412, 800))


def serve(directory: str):
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=directory)
    handler.log_message = lambda *a, **k: None
    srv = socketserver.TCPServer(("127.0.0.1", PORT), handler)
    srv.allow_reuse_address = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


async def main():
    if not (TREE / "manifest.json").exists():
        sys.exit(f"missing tree: {TREE} (run scripts/patch-vdh-mobile-ui.py first)")
    serve(str(TREE))
    if SHOTS:
        SHOTS.mkdir(parents=True, exist_ok=True)
    ok = True
    async with async_playwright() as p:
        b = await p.chromium.launch()
        for name, w, h in VIEWPORTS:
            ctx = await b.new_context(viewport={"width": w, "height": h})
            await ctx.add_init_script(MOCK)
            pg = await ctx.new_page()
            errors = []
            pg.on("pageerror", lambda e: errors.append(str(e)[:120]))
            await pg.goto(f"http://127.0.0.1:{PORT}/content/{name}")
            await pg.wait_for_timeout(2500)
            info = await pg.evaluate("""() => {
              const r = (el) => { if (!el) return null; const b = el.getBoundingClientRect();
                return {w: Math.round(b.width), h: Math.round(b.height), t: Math.round(b.top)}; };
              const tb = document.querySelector('#toolbar');
              return {vp: [innerWidth, innerHeight],
                      html: r(document.documentElement), body: r(document.body),
                      main: r(document.querySelector('#main')), toolbar: r(tb)};
            }""")
            good = (info["html"]["h"] == info["vp"][1]
                    and info["body"]["h"] == info["vp"][1]
                    and info["main"] and info["toolbar"]
                    and abs(info["main"]["h"] + info["toolbar"]["h"] - info["vp"][1]) <= 2)
            ok = ok and good
            print(f"{'PASS' if good else 'FAIL'} {name:13s} viewport {info['vp']}, "
                  f"html {info['html']['h']}px, main {info['main']['h']}px, "
                  f"toolbar at y={info['toolbar']['t']}")
            if SHOTS:
                await pg.screenshot(path=str(SHOTS / f"{name.replace('.html', '')}.png"),
                                    full_page=False)
            await ctx.close()
        await b.close()
    print("OK" if ok else "layout check FAILED")
    sys.exit(0 if ok else 1)


asyncio.run(main())
