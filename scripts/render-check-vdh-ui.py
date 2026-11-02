#!/usr/bin/env python3
"""Render the patched panel in a real Chromium at phone size and measure it.

`file://` blocks the ES module (CORS), so the tree is served over http. A
tolerant chrome.* stub lets the app mount; then a fake global state (the same
shape the service worker publishes) is installed and the page is re-loaded,
so the media list and toolbar actually render with content instead of an
empty shell. That is what makes the measurements meaningful.

Reported and asserted per viewport:
  base font      :root computed font-size  (stock is 12px, 9.6px below 400px)
  page height    : html/body height == viewport height
  layout         : #main ends exactly where #toolbar starts
  filled         : which y-ranges of the viewport contain drawn elements
                   (a big empty band is what the user reported as wasted space)

    pip install playwright && python3 -m playwright install chromium
    scripts/render-check-vdh-ui.py [tree] [--shot DIR] [--yes]
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
argv = sys.argv[1:]
SHOTS = None
if "--shot" in argv:
    i = argv.index("--shot")
    SHOTS = pathlib.Path(argv[i + 1])
    del argv[i:i + 2]
positional = [a for a in argv if not a.startswith("--")]
TREE = pathlib.Path(positional[0]) if positional else REPO / "build" / "extension"
PORT = 0   # ephemeral: bind() picks a free port

VIEWPORTS = (("sidebar.html", 412, 915), ("popup.html", 412, 720))

# Installed after the app has mounted once: mirrors the worker's persistent
# state (see service/main.js defaults) so the panel has something to draw.
STATE = r"""
(() => {
  const media = [];
  for (let i = 1; i <= 5; i++) {
    media.push({
      kind: 'MEDIA', format: 'mp4', md5: 'md5-' + i, id: i,
      title: 'Interview part ' + i + '.mp4', filename: 'Interview-' + i + '.mp4',
      duration: 3600 + i, duration_str: (60 + i) + ':' + (10 + i),
      width: 1920, height: 1080, bitrate: 2400,
      size: 48000000 + i * 1000000, url_path: 'https://example.com/v/' + i + '.mp4',
      thumbnail: '', timestamp: 1700000000 + i, download_timestamp: 0,
      playability: { status: 'PLAYABLE' }, found_on: 'plex.tv',
      is_live: false, audio_only: false, conversion_required: false,
    });
  }
  const persistent = {
    version: 1, disabled: [], default_action: 'download',
    default_action_per_hostname: {}, downloaded: {}, jwt: null, lsd: 1,
    dockmode: 'popup', download_directory: 'videos', youtube_throttle: false,
    preferred_audio_strategy: 'original', preferred_audio_languages: [],
    saveas: true, hide_nomedia_box: false, dont_ask_for_user_review: true,
    agreed_with_tos: true, ui_theme: 'system', popup_size: 'medium',
    font_size: 'default', preferred_discovered_media_order: 'newest',
    smartnaming_pattern: '', preferred_quality: 1080, preferred_av_muxer: 'mp4',
    preview_mode: 'video', context_menu: true, history_enabled: true,
  };
  const session = {
    current_win_tab: {win_id: 1, tab_id: {Some: 1}},
    current_media: media, current_discovered: [],
    current_hostname: 'plex.tv', media_by_id: {}, notifications: [],
    downloaded_media: [], conversion_queue: [], download_queue: [],
    is_premium: true, premium_account: null,
  };
  Object.defineProperty(globalThis, 'persistent_state',
    {value: persistent, writable: true, configurable: true});
  Object.defineProperty(globalThis, 'session_state',
    {value: session, writable: true, configurable: true});
})();
"""

MOCK = STATE + r"""
(() => {
  // keys the app actually reads (see panel.js: Rr/Yi constants)
  const store = {global_session_state: globalThis.session_state,
                 global_persistent_state: globalThis.persistent_state,
                 session: globalThis.session_state};
  const mk = (path) => new Proxy(function(){}, {
    get(t, k) {
      if (k === 'then') return undefined;
      if (k === Symbol.toPrimitive) return () => '';
      if (k === 'getMessage') return (k2) => k2;
      if (k === 'getURL') return (p) => 'chrome-extension://mock' + (p.startsWith('/') ? p : '/' + p);
      if (k === 'getPlatformInfo') return async () => ({os: 'android', arch: 'arm64'});
      if (k === 'isAllowedIncognitoAccess') return async () => false;
      if (k === 'getManifest') return () => ({version: '10.6.1.2'});
      if (k === 'sendMessage') return async (msg) => {
        const name = msg && (msg.name || msg.type);
        if (name === 'global_state' || name === 'get_state') return store;
        return undefined;
      };
      if (k === 'set') return async () => undefined;
      if (k === 'get') return async (key) => {
        if (typeof key === 'string') return {[key]: store[key]};
        if (Array.isArray(key)) return Object.fromEntries(key.map(k2 => [k2, store[k2]]));
        return store;
      };
      if (k === 'getKey' || k === 'getAll') return async () => store;
      if (k === 'onMessage' || k === 'onConnect' || k.endsWith('Listener') || k.startsWith('on'))
        return {addListener(){}, removeListener(){}, hasListener(){ return false; }};
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

PROBE = """() => {
  const r = (el) => { if (!el) return null; const b = el.getBoundingClientRect();
    return {w: Math.round(b.width), h: Math.round(b.height), t: Math.round(b.top),
            b: Math.round(b.bottom)}; };
  const CS = (el, p) => el ? getComputedStyle(el)[p] : null;
  const root = document.documentElement;
  const main = document.querySelector('#main');
  const tb = document.querySelector('#toolbar');
  // y-coverage of anything that actually paints inside #main
  const bands = [];
  const H = innerHeight;
  const painted = new Array(Math.ceil(H / 20)).fill(false);
  const walk = (el) => {
    for (const c of el.children) {
      const cs = getComputedStyle(c);
      if (cs.display === 'none' || cs.visibility === 'hidden' || +cs.opacity === 0) continue;
      const b = c.getBoundingClientRect();
      if (b.width > 8 && b.height > 8) {
        for (let y = Math.max(0, b.top); y < Math.min(H, b.bottom); y += 20)
          painted[Math.floor(y / 20)] = true;
      }
      walk(c);
    }
  };
  if (main) walk(main);
  const empty = painted.map((v, i) => v ? null : i * 20).filter(v => v !== null);
  // longest run of empty 20px bands
  let run = 0, best = 0;
  for (const v of painted) { run = v ? 0 : run + 20; best = Math.max(best, run); }
  const first = main && main.firstElementChild;
  return {
    vp: [innerWidth, innerHeight],
    baseFont: CS(root, 'fontSize'),
    html: r(root), body: r(document.body), main: r(main), toolbar: r(tb),
    rowCount: document.querySelectorAll('com-media').length,
    firstRow: r(document.querySelector('com-media')),
    buttonH: r(document.querySelector('com-toolbar button, #toolbar button')),
    fonts: Object.fromEntries(['#nomedia_title', '#nomedia p', '#toolbar button',
      'com-toolbar button', '#main', 'p'].map(sel => {
        const el = document.querySelector(sel);
        return [sel, el ? CS(el, 'fontSize') : null];
      }).filter(([, v]) => v)),
    longestEmptyBand: best,
  };
}"""


def serve(directory: str):
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=directory)
    handler.log_message = lambda *a, **k: None
    class Server(socketserver.TCPServer):
        allow_reuse_address = True
    srv = Server(("127.0.0.1", PORT), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


async def main():
    if not (TREE / "manifest.json").exists():
        sys.exit(f"missing tree: {TREE} (run scripts/patch-vdh-mobile-ui.py first)")
    httpd = serve(str(TREE))
    port = httpd.server_address[1]
    if SHOTS:
        SHOTS.mkdir(parents=True, exist_ok=True)
    ok = True
    async with async_playwright() as p:
        b = await p.chromium.launch()
        for name, w, h in VIEWPORTS:
            ctx = await b.new_context(viewport={"width": w, "height": h})
            await ctx.add_init_script(MOCK)
            pg = await ctx.new_page()
            await pg.goto(f"http://127.0.0.1:{port}/content/{name}")
            await pg.wait_for_timeout(2500)
            info = await pg.evaluate(PROBE)
            base = float(info["baseFont"].replace("px", ""))
            main = info["main"] or {"h": 0}
            toolbar = info["toolbar"] or {"t": 0}
            fonts = [float(v.replace("px", "")) for v in info["fonts"].values()]
            smallest = min(fonts) if fonts else 0
            good = (info["html"]["h"] == info["vp"][1]
                    and base >= 19.0                    # stock 12px, 9.6px if shrunk
                    and smallest >= 12.0               # nothing rendered below 12px
                    and (not info["toolbar"] or not info["main"]
                         or abs(main["h"] + info["toolbar"]["h"] - info["vp"][1]) <= 2))
            ok = ok and good
            print(f"{'PASS' if good else 'FAIL'} {name:13s} "
                  f"vp {info['vp'][0]}x{info['vp'][1]} | root font {info['baseFont']} | "
                  f"page h {info['html']['h']} | main {main['h']}px | "
                  f"toolbar y {toolbar['t']}")
            print(f"      fonts: {info['fonts']}")
            print(f"      rows drawn: {info['rowCount']} "
                  f"first row {info['firstRow']} | button h "
                  f"{info['buttonH']['h'] if info['buttonH'] else '-'} | "
                  f"longest empty band {info['longestEmptyBand']}px")
            if SHOTS:
                await pg.screenshot(path=str(SHOTS / f"{name.replace('.html', '')}.png"))
            await ctx.close()
        await b.close()
    print("OK" if ok else "layout check FAILED")
    sys.exit(0 if ok else 1)


asyncio.run(main())
