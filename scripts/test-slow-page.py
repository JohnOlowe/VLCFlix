"""A deliberately slow page, for exercising slow-loading capture.

Pair with scripts/chromium-driver.py:

    python3 scripts/test-slow-page.py 8130 &
    python3 scripts/chromium-driver.py http://127.0.0.1:8130/slow

(original docstring follows)

Streams HTML in chunks so the page is *committed* immediately but not *loaded*
for ~12 s, and reveals markers over time:

    #early   at 0 s
    #mid     at ~5 s
    #late    at ~10 s
    #ready   at ~12 s (and only then does document.readyState reach complete)

Usage: python3 /tmp/slowserver.py [port]
"""
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8130


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.startswith("/fast"):
            body = b"<!doctype html><title>fast</title><p id=early>fast</p>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def send(text, delay):
            time.sleep(delay)
            data = text.encode()
            chunk = b"%x\r\n%s\r\n" % (len(data), data)
            try:
                self.wfile.write(chunk)
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                raise SystemExit

        try:
            send("<!doctype html><html><head><title>slow page</title>"
                 "<style>body{font:16px sans-serif;padding:16px}"
                 ".step{margin:8px 0;padding:8px;border:1px solid #ccc}"
                 "#bar{height:14px;background:#4a7}</style></head><body>"
                 "<h1>Slow page</h1><div id=early class=step>step 1: shell arrived</div>", 0)
            for i in range(1, 13):
                label = {5: "id=mid class=step>step 2: markup half way",
                         10: "id=late class=step>step 3: more markup"}.get(
                             i, f"class=step>tick {i}")
                send(f"<div {label}</div>", 1)
            send("<div id=ready class=step>step 4: ready</div>"
                 "<script>document.title='slow page loaded'</script>"
                 "</body></html>", 1)
        except SystemExit:
            pass


if __name__ == "__main__":
    print(f"slow page on :{PORT}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
