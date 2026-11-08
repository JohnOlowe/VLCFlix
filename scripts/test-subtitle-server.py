"""HLS fixture with subtitle renditions, for scripts/check-subtitles.py.

Serves three subtitle shapes so the stitcher is exercised, not just the happy
path:

  /plain/master.m3u8            one subtitle rendition -> a single .vtt file
  /srtserved/master.m3u8        a rendition that is already SubRip (no conversion)
  /segmented/master.m3u8        two renditions, each a media playlist of
                                WebVTT segments that carry X-TIMESTAMP-MAP, so
                                the cue times must be offset to stay continuous
  /encrypted/master.m3u8        EXT-X-KEY METHOD=AES-128 -> must be refused

A Tubi-shaped fixture (`/sidecar/`), for scripts/check-sidecar-subtitles.py.
Tubi's captions are not in its stream at all: the HLS playlists carry no
EXT-X-MEDIA:SUBTITLES, and the page downloads a plain .srt from another host
(s.adrise.tv on the real site) when captions are switched on. So this page
fetches two .srt files the way such a player does -- one with a language in the
URL, one with a UUID name and no language anywhere -- and keeps a text track
showing so the extension can ask the page which language is on screen.

DASH fixtures (an .mpd manifest, parsed as XML rather than as tag lines):

  /dash/manifest.mpd            three text AdaptationSets:
      en  one complete file                     (Representation + BaseURL)
      es  SegmentTemplate + SegmentTimeline     (segments must be walked and
                                                 each shifted by its own start)
      fr  TTML                                  (converted to WebVTT)
      de  wvtt, i.e. WebVTT inside MP4 boxes    -> must be refused, not broken

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

MASTER_SRT = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="subs/en.srt"
#EXT-X-STREAM-INF:BANDWIDTH=800000,SUBTITLES="subs"
video.m3u8
"""

SRT_EN = """1
00:00:01,000 --> 00:00:02,500
Already SubRip, served as SubRip

2
00:00:03,000 --> 00:00:04,000
Second cue
"""

ROUTES = {
    "/plain/master.m3u8": ("application/vnd.apple.mpegurl", MASTER_PLAIN),
    "/plain/subs/en.vtt": ("text/vtt", VTT_ONE),
    "/srtserved/master.m3u8": ("application/vnd.apple.mpegurl", MASTER_SRT),
    "/srtserved/subs/en.srt": ("application/x-subrip", SRT_EN),
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



# ------------------------------------------------------------------ DASH ----
# One manifest, four shapes. The segment timeline starts at 4 s of media time
# and the segments are 4 s long, so a naive concatenation would leave every
# segment's cues beginning at 00:00 -- the same bug the HLS fixture catches.

MPD = """<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static"
     mediaPresentationDuration="PT20S" minBufferTime="PT4S">
  <Period id="0" duration="PT20S">
    <AdaptationSet mimeType="video/mp4">
      <Representation id="v0" bandwidth="800000" codecs="avc1.4d401f">
        <BaseURL>video.mp4</BaseURL>
      </Representation>
    </AdaptationSet>

    <!-- one complete file: the simplest case -->
    <AdaptationSet contentType="text" mimeType="text/vtt" lang="en">
      <Role schemeIdUri="urn:mpeg:dash:role:2011" value="subtitle"/>
      <Representation id="en" bandwidth="256">
        <BaseURL>en.vtt</BaseURL>
      </Representation>
    </AdaptationSet>

    <!-- segmented: SegmentTemplate + SegmentTimeline -->
    <AdaptationSet contentType="text" mimeType="text/vtt" lang="es" label="Espanol">
      <Role schemeIdUri="urn:mpeg:dash:role:2011" value="subtitle"/>
      <Representation id="es" bandwidth="256">
        <SegmentTemplate timescale="1000" media="es-$Number$.vtt">
          <SegmentTimeline>
            <S t="4000" d="4000"/>
            <S d="4000"/>
            <S d="4000"/>
          </SegmentTimeline>
        </SegmentTemplate>
      </Representation>
    </AdaptationSet>

    <!-- TTML, which DASH uses far more often than HLS does -->
    <AdaptationSet contentType="text" mimeType="application/ttml+xml" lang="fr">
      <Role schemeIdUri="urn:mpeg:dash:role:2011" value="subtitle"/>
      <Representation id="fr" bandwidth="256">
        <BaseURL>fr.ttml</BaseURL>
      </Representation>
    </AdaptationSet>

    <!-- WebVTT inside MP4 boxes: not plain text, must be refused -->
    <AdaptationSet contentType="text" mimeType="application/mp4" lang="de">
      <Role schemeIdUri="urn:mpeg:dash:role:2011" value="subtitle"/>
      <Representation id="de" bandwidth="256" codecs="wvtt">
        <BaseURL>de.m4s</BaseURL>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
"""

DASH_VTT_EN = """WEBVTT

1
00:00:01.000 --> 00:00:03.000
Hello from the DASH single file

2
00:00:04.000 --> 00:00:06.500
Second line
"""

# three 4s segments starting at 4s of media time; cues are relative to the
# segment, so the second cue of the result must be at 00:00:08.000
DASH_SEG_ES_1 = """WEBVTT

00:00:00.000 --> 00:00:02.000
Empieza aqui

00:00:02.000 --> 00:00:04.000
y sigue
"""

DASH_SEG_ES_2 = """WEBVTT

00:00:00.000 --> 00:00:02.000
Segundo segmento

00:00:02.500 --> 00:00:03.500
mas texto
"""

DASH_SEG_ES_3 = """WEBVTT

00:00:00.000 --> 00:00:01.500
Tercer segmento
"""

DASH_TTML_FR = """<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="fr">
  <body>
    <div>
      <p begin="00:00:01.000" end="00:00:03.000">Bonjour<br/>tout le monde</p>
      <p begin="00:00:04.000" end="00:00:06.000">Deuxieme ligne</p>
    </div>
  </body>
</tt>
"""

DASH_WVTT_DE = "\x00\x00\x00\x18ftypmp42"

ROUTES.update({
    "/dash/manifest.mpd": ("application/dash+xml", MPD),
    "/dash/en.vtt": ("text/vtt", DASH_VTT_EN),
    "/dash/es-1.vtt": ("text/vtt", DASH_SEG_ES_1),
    "/dash/es-2.vtt": ("text/vtt", DASH_SEG_ES_2),
    "/dash/es-3.vtt": ("text/vtt", DASH_SEG_ES_3),
    "/dash/fr.ttml": ("application/ttml+xml", DASH_TTML_FR),
    "/dash/de.m4s": ("application/mp4", DASH_WVTT_DE),
})

# --------------------------------------------------------------- sidecar ----
# named like Tubi's: a UUID, no language in the URL
SIDECAR_SRT = """1
00:00:07,073 --> 00:00:10,377
[eerie music]

2
00:00:18,918 --> 00:00:21,821
Bonjour tout le monde

3
00:00:47,080 --> 00:00:49,149
This is the sidecar caption file
"""

SIDECAR_SRT_FR = """1
00:00:05,000 --> 00:00:08,000
Sous-titre francais

2
00:00:12,000 --> 00:00:15,000
Deuxieme ligne
"""

SIDECAR_PAGE = """<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>Sidecar captions fixture</title></head>
<body>
<video id="v" muted playsinline width="320" height="180">
  <track id="tr" kind="subtitles" srclang="en" label="English" default>
</video>
<script>
  /* a player that fetches its captions itself, as Tubi's does */
  fetch('/sidecar/21eb2f30-70a3-4999-af4e-535f09e56a42.srt');
  fetch('/sidecar/episode.fr.srt');
  const tr = document.getElementById('tr');
  try { tr.mode = 'showing'; } catch (e) {}
</script>
</body>
</html>
"""

ROUTES.update({
    "/sidecar/index.html": ("text/html", SIDECAR_PAGE),
    "/sidecar/21eb2f30-70a3-4999-af4e-535f09e56a42.srt": ("application/x-subrip", SIDECAR_SRT),
    "/sidecar/episode.fr.srt": ("application/x-subrip", SIDECAR_SRT_FR),
})


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
