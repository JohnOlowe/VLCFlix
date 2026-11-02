#!/usr/bin/env python3
"""End-to-end proof of pause / resume, through the real extension and UI.

No fabricated state: this drives the same path a user does.

  1. serve a page whose <video> streams a 4 MB file slowly (~200 KB/s)
  2. load the built extension in Chromium, open the test page in a tab
  3. open the panel, find the discovered media row, click its Download action
  4. read progress from the downloading row (its shadow root shows percent /
     bytes), then click the new Pause button
  5. assert the byte count stops growing while paused, and continues after
     clicking Resume

    python3 scripts/check-pause-resume.py build/extension
Exit code 0 = paused and resumed for real.
"""
import asyncio
import http.server
import pathlib
import socketserver
import sys
import threading
import time

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
TOTAL = 6 * 1024 * 1024
CHUNK = 64 * 1024
DELAY = 0.35


class SlowHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.startswith("/slow.mp4"):
            self.send_response(200)
            self.send_header("Content-Type", "video/mp4")
            self.send_header("Content-Length", str(TOTAL))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            sent = 0
            try:
                while sent < TOTAL:
                    self.wfile.write(b"\x00" * min(CHUNK, TOTAL - sent))
                    self.wfile.flush()
                    sent += CHUNK
                    time.sleep(DELAY)
            except (BrokenPipeError, ConnectionResetError):
                pass
        else:
            body = (b"<!doctype html><html><head><title>pause test page</title></head>"
                    b"<body><h1>pause test</h1>"
                    b'<video id="v" width="640" height="360" controls '
                    b'src="/slow.mp4" preload="metadata"></video>'
                    b"<script>document.getElementById('v').load()</script>"
                    b"</body></html>")
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)


def serve():
    class S(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True
    srv = S(("127.0.0.1", 0), SlowHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv.server_address[1]


READ_ROW = """() => {
  const main = document.querySelector('com-main');
  const sr = main && main.shadowRoot;
  if (!sr) return null;
  const rows = [...sr.querySelectorAll('com-media-downloading')];
  if (!rows.length) return {rows: 0};
  const r = rows[0].shadowRoot;
  if (!r) return {rows: rows.length, shadow: false};
  const txt = (sel) => { const e = r.querySelector(sel); return e ? e.textContent.trim() : null; };
  const btn = r.querySelector('#button_pause');
  const box = btn ? btn.getBoundingClientRect() : null;
  return {rows: rows.length, shadow: true,
          percent: txt('#span_percent'), bitrate: txt('#span_bitrate'),
          pauseText: btn ? btn.textContent.trim() : null,
          buttonVisible: !!(box && box.width > 0)};
}"""

CLICK = """(sel) => {
  const main = document.querySelector('com-main');
  const r = main && main.shadowRoot;
  const row = r && r.querySelector('com-media-downloading');
  const b = row && row.shadowRoot && row.shadowRoot.querySelector(sel);
  if (!b) return false;
  b.click();
  return true;
}"""

CLICK_DOWNLOAD = """() => {
  const main = document.querySelector('com-main');
  const sr = main && main.shadowRoot;
  if (!sr) return 'no main shadow';
  const found = [];
  for (const host of sr.querySelectorAll('*')) {
    if (!host.shadowRoot) continue;
    for (const el of host.shadowRoot.querySelectorAll('button, [id^="action_"]')) {
      found.push(el.id || el.className || el.tagName);
      if (el.id === 'action_download' && !el.hasAttribute('hidden')) { el.click(); return 'clicked action_download'; }
    }
  }
  return 'actions seen: ' + found.slice(0, 12).join(',');
}"""


async def main():
    port = serve()
    ok = True
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-pause2", headless=True, channel="chromium",
            viewport={"width": 412, "height": 720},
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        site = await ctx.new_page()
        await site.goto(f"http://127.0.0.1:{port}/")
        await site.wait_for_timeout(3000)          # let the video start + detect
        if not ctx.service_workers:
            print("FAIL: extension did not load"); return 1
        extid = ctx.service_workers[0].url.split("/")[2]

        panel = await ctx.new_page()
        panel.on("console", lambda m: None)
        await panel.goto(f"chrome-extension://{extid}/content/popup.html")
        await panel.wait_for_timeout(2500)

        print("download click:", await panel.evaluate(CLICK_DOWNLOAD))
        await panel.wait_for_timeout(4000)

        row = await panel.evaluate(READ_ROW)
        print("downloading row:", row)
        if not row or not row.get("rows"):
            print("FAIL: no downloading row appeared (media not detected?)"); return 1
        if not row.get("buttonVisible"):
            print("FAIL: pause button is not rendered"); ok = False

        p1 = await panel.evaluate(READ_ROW)
        print("pause button says:", p1.get("pauseText"))
        clicked = await panel.evaluate(CLICK, "#button_pause")
        print("clicked pause:", clicked)
        await panel.wait_for_timeout(1500)
        a = await panel.evaluate(READ_ROW)
        await panel.wait_for_timeout(4000)
        b = await panel.evaluate(READ_ROW)
        print("paused  ->", a.get("percent"), a.get("pauseText"))
        print("4s later->", b.get("percent"), b.get("pauseText"))
        if a.get("percent") == b.get("percent") and b.get("pauseText") == "Resume":
            print("PASS: progress frozen while paused")
        else:
            print("FAIL: progress moved or button state wrong while paused"); ok = False

        await panel.evaluate(CLICK, "#button_pause")   # resume
        await panel.wait_for_timeout(500)
        c = await panel.evaluate(READ_ROW)
        await panel.wait_for_timeout(5000)
        d = await panel.evaluate(READ_ROW)
        print("after resume ->", c.get("percent"), c.get("pauseText"))
        print("5s later     ->", d.get("percent"), d.get("pauseText"))
        if d.get("percent") != c.get("percent"):
            print("PASS: progress continued after resume")
        else:
            print("FAIL: progress did not continue after resume"); ok = False
        await ctx.close()
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


sys.exit(asyncio.run(main()))
