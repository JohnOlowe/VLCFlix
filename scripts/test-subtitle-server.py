"""HLS fixture with subtitle renditions, for scripts/check-subtitles.py.

Serves three subtitle shapes so the stitcher is exercised, not just the happy
path:

  /plain/master.m3u8            one subtitle rendition -> a single .vtt file
  /segmented/master.m3u8        two renditions, each a media playlist of
                                WebVTT segments that carry X-TIMESTAMP-MAP, so
                                the cue times must be offset to stay continuous
  /encrypted/master.m3u8        EXT-X-KEY METHOD=AES-128 -> must be refused

Segments of the segmented variant are deliberately offset in MPEGTS (90000 ticks
per second): the first starts at 10 s of media time, the second 5 s later. A
naive concatenation would leave both starting at 00:00, which is exactly the bug
this fixture is here to catch.

Usage: python3 scripts/test-subtitle-server.py [port]
"""
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8140

VTT_ONE = """WEBVTT

1
00:00:01.000 --> 00:00:03.000
Hello from the single file

2
00:00:04.000 --> 00:00:06.500
Second line
"""

# two segments, media time starting at 10s, second segment 5s later
SEG_1 = """WEBVTT
X-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000

4294967295
00:00:00.200 --> 00:00:02.000
First cue of segment one

00:00:03.000 --> 00:00:04.500
Second cue of segment one
"""

SEG_2 = """WEBVTT
X-TIMESTAMP-MAP=MPEGTS:1350000,LOCAL:00:00:00.000

00:00:00.500 --> 00:00:01.500
First cue of segment two

00:00:02.000 --> 00:00:03.000
Second cue of segment two
"""

SEG_1_ES = """WEBVTT
X-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000

00:00:00.200 --> 00:00:02.000
Primera se\u00f1al del segmento uno
"""

SEG_2_ES = """WEBVTT
X-TIMESTAMP-MAP=MPEGTS:1350000,LOCAL:00:00:00.000

00:00:00.500 --> 00:00:01.500
Primera se\u00f1al del segmento dos
"""

MASTER_PLAIN = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="subs/en.vtt"
#EXT-X-STREAM-INF:BANDWIDTH=800000,SUBTITLES="subs"
video.m3u8
"""

MASTER_SEGMENTED = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="subs/en.m3u8"
#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="Espa\u00f1ol",LANGUAGE="es",URI="subs/es.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=800000,SUBTITLES="subs"
video.m3u8
"""

SUBS_EN = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:5
#EXT-X-MEDIA-SEQUENCE:0
#EXTINF:5.0,
en-1.vtt
#EXTINF:5.0,
en-2.vtt
#EXT-X-ENDLIST
"""

SUBS_ES = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:5
#EXT-X-MEDIA-SEQUENCE:0
#EXTINF:5.0,
es-1.vtt
#EXTINF:5.0,
es-2.vtt
#EXT-X-ENDLIST
"""

MASTER_ENCRYPTED = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",URI="subs/enc.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=800000,SUBTITLES="subs"
video.m3u8
"""

SUBS_ENCRYPTED = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-KEY:METHOD=AES-128,URI="key.bin"
#EXTINF:5.0,
en-1.vtt
#EXT-X-ENDLIST
"""

ROUTES = {
    "/plain/master.m3u8": ("application/vnd.apple.mpegurl", MASTER_PLAIN),
    "/plain/subs/en.vtt": ("text/vtt", VTT_ONE),
    "/segmented/master.m3u8": ("application/vnd.apple.mpegurl", MASTER_SEGMENTED),
    "/segmented/subs/en.m3u8": ("application/vnd.apple.mpegurl", SUBS_EN),
    "/segmented/subs/es.m3u8": ("application/vnd.apple.mpegurl", SUBS_ES),
    "/segmented/subs/en-1.vtt": ("text/vtt", SEG_1),
    "/segmented/subs/en-2.vtt": ("text/vtt", SEG_2),
    "/segmented/subs/es-1.vtt": ("text/vtt", SEG_1_ES),
    "/segmented/subs/es-2.vtt": ("text/vtt", SEG_2_ES),
    "/encrypted/master.m3u8": ("application/vnd.apple.mpegurl", MASTER_ENCRYPTED),
    "/encrypted/subs/enc.m3u8": ("application/vnd.apple.mpegurl", SUBS_ENCRYPTED),
    # a tiny media playlist so the video side of the fixture is not a 404
    "/plain/video.m3u8": ("application/vnd.apple.mpegurl",
                          "#EXTM3U\n#EXT-X-TARGETDURATION:5\n#EXTINF:5.0,\nv-1.ts\n#EXT-X-ENDLIST\n"),
    "/segmented/video.m3u8": ("application/vnd.apple.mpegurl",
                              "#EXTM3U\n#EXT-X-TARGETDURATION:5\n#EXTINF:5.0,\nv-1.ts\n#EXT-X-ENDLIST\n"),
}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path.split("?")[0]
        route = ROUTES.get(path)
        if not route:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        ctype, body = route
        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


if __name__ == "__main__":
    print(f"subtitle fixture on :{PORT}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
