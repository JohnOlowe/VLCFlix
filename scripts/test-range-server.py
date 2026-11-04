"""Minimal static server with HTTP Range support, for testing byte-range resume.

scripts/check-resume.py needs two of these:

    python3 scripts/test-range-server.py 8124 /tmp/site --slow        # honours Range
    python3 scripts/test-range-server.py 8125 /tmp/site --no-range --slow   # ignores it

and a 4 MB file at <root>/big.mp4. Each writes /tmp/rangeserver-<port>.log.

Logs one line per request to /tmp/rangeserver.log:
    <method> <path> range=<header or -> status=<code> sent=<bytes>

Usage: python3 /tmp/rangeserver.py [port] [root] [--no-range]
"""
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8124
ROOT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/site"
NO_RANGE = "--no-range" in sys.argv
SLOW = "--slow" in sys.argv   # trickle the body so progress is observable
LOG = "/tmp/rangeserver-%d.log" % (int(sys.argv[1]) if len(sys.argv) > 1 else 8124)


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = os.path.join(ROOT, self.path.lstrip("/").split("?")[0])
        if not os.path.isfile(path):
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._log(404, 0, "-")
            return
        size = os.path.getsize(path)
        rng = self.headers.get("Range")
        start, end, status = 0, size - 1, 200
        if rng and not NO_RANGE:
            m = re.match(r"bytes=(\d+)-(\d*)", rng)
            if m:
                start = int(m.group(1))
                end = int(m.group(2)) if m.group(2) else size - 1
                if start >= size:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    self._log(416, 0, rng)
                    return
                status = 206
        length = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Accept-Ranges", "bytes" if not NO_RANGE else "none")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        with open(path, "rb") as f:
            f.seek(start)
            left = length
            while left > 0:
                chunk = f.read(min(65536 if not SLOW else 32768, left))
                if not chunk:
                    break
                try:
                    self.wfile.write(chunk)
                except BrokenPipeError:
                    break
                left -= len(chunk)
                if SLOW:
                    import time
                    time.sleep(0.08)
        self._log(status, length - left, rng or "-")

    def _log(self, status, sent, rng):
        with open(LOG, "a") as f:
            f.write(f"GET {self.path} range={rng} status={status} sent={sent}\n")


if __name__ == "__main__":
    open(LOG, "w").close()
    print(f"serving {ROOT} on :{PORT} range={'off' if NO_RANGE else 'on'} slow={SLOW}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
