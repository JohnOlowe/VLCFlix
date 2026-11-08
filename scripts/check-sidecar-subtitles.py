"""Subtitles that are not in the stream at all.

Tubi is the case that forced this: its HLS playlists carry no
EXT-X-MEDIA:SUBTITLES, and the captions are a plain .srt on another host that
the player downloads when captions are switched on. A manifest parser can never
see it, so the extension has to watch the network instead.

Part A  the page's own subtitle files are noticed: one with a language in its
        URL, one named like Tubi's (a UUID, no language anywhere)
Part B  the language Tubi does not put in the URL is asked of the page, which
        knows which caption track is showing
Part C  the sidecars ride along with a download started from that page
Part D  saving one produces a real SubRip file, not SubRip relabelled WebVTT
"""
import asyncio
import pathlib
import re
import sys
import urllib.parse

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
FIXTURE = "http://127.0.0.1:8140"
PAGE = FIXTURE + "/sidecar/index.html"
PLAIN = "http://127.0.0.1:8123"
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-sidecar", headless=True, channel="chromium",
            viewport={"width": 412, "height": 900}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run", "--autoplay-policy=no-user-gesture-required"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)

        # --- the page the captions belong to -------------------------------
        print("\nA. subtitle files the page fetches for itself")
        site = await ctx.new_page()
        await site.goto(PAGE, wait_until="load", timeout=30000)
        await site.wait_for_timeout(3000)

        hist = await ctx.new_page()
        await hist.goto(f"chrome-extension://{extid}/content/history.html")
        await hist.wait_for_timeout(1200)

        async def sidecars():
            return await hist.evaluate("""async () => {
                const got = await chrome.storage.local.get('vdh_subtitle_sidecars');
                return got.vdh_subtitle_sidecars || [];
            }""")

        seen = await sidecars()
        for s in seen:
            print(f"   {s['language']:4} {s['format']:4} {s['url'].replace(FIXTURE, '')}", flush=True)
        urls = [s["url"] for s in seen]
        ok(any("21eb2f30" in u for u in urls),
           "the UUID-named .srt (no language in its URL) was noticed")
        ok(any("episode.fr.srt" in u for u in urls),
           "the .srt with a language in its URL was noticed")
        ok(all(s["format"] == "srt" for s in seen if "srt" in s["url"]),
           "and recognised as SubRip, not WebVTT")
        ok(all(s["origin"] == FIXTURE for s in seen),
           f"each is tied to the page it came from ({sorted(set(s['origin'] for s in seen))})")

        print("\nB. the language the URL does not say")
        by_url = {s["url"]: s for s in await sidecars()}
        tubi_like = next((s for u, s in by_url.items() if "21eb2f30" in u), None)
        named = next((s for u, s in by_url.items() if "episode.fr" in u), None)
        print(f"   uuid-named: {tubi_like and tubi_like['language']!r}"
              f"   named: {named and named['language']!r}", flush=True)
        ok(named and named["language"] == "fr",
           f"the language in the URL is used as-is ({named and named['language']!r})")
        ok(tubi_like and tubi_like["language"] == "en",
           f"for the one with no language, the page is asked which track is showing "
           f"({tubi_like and tubi_like['language']!r})")

        # --- a download from that page --------------------------------------
        print("\nC. the sidecars ride along with the download")
        P = lambda v: {"__serde_tag": "primitive", "__serde_val": v}
        U = lambda v: {"__serde_tag": "url", "__serde_val": v}
        O = lambda d: {"__serde_tag": "object", "__serde_val": d}
        A_ = lambda l: {"__serde_tag": "array", "__serde_val": l}
        NONE = {"__serde_tag": "none"}
        SOME = lambda v: {"__serde_tag": "some", "__serde_val": v}
        dl_args = O({
            "download_id": P("sidecar_probe"), "headers": A_([]),
            "good_basename": P("sidecarprobe"), "muxer": P("mp4"), "subdir": P(""),
            "will_use_jsfetch": P(False), "save_as": P(False),
            "strategy": P("http_audio_video_one_source"),
            "url": U(PLAIN + "/video.mp4"), "extension": P("mp4"),
            "is_youtube": P(False), "throttle": P(False),
            "carry_get_params": P(False), "cache": NONE})
        # the media Tubi's shape: a playlist with no subtitle renditions at all
        media = O({
            "hash": P("media_hash_sidecar"), "type": P("hls_playlist"),
            "has_drm": P(False), "is_youtube": P(False),
            "master_url": SOME(U(PLAIN + "/video.m3u8")),
            "url": SOME(U(PLAIN + "/video.m3u8")),
            "initiator": SOME(U(PAGE)), "sent_headers": A_([]),
            "availability": P("available"), "size": P(8192), "duration": P(1.0),
            "sources": A_([O({"url": U(PLAIN + "/video.mp4"),
                              "mime_type": P("video/mp4"), "is_audio": P(False),
                              "is_video": P(True), "codec": P("avc1"), "bitrate": P(0)})]),
            "cache": NONE, "subtitles": NONE})
        meta = O({"tab_id": P(1), "url": SOME(U(PAGE)), "favicon_url": NONE,
                  "title": SOME(P("Sidecar captions fixture")),
                  "default_action": P("download")})
        await hist.evaluate("""async (rec) => {
            const got = await chrome.storage.local.get('vdh_journal');
            const j = got.vdh_journal || {};
            j['dl:sidecar_probe'] = rec;
            await chrome.storage.local.set({vdh_journal: j});
        }""", {"id": "dl:sidecar_probe", "download_id": "sidecar_probe", "status": "failed",
               "filename": "sidecarprobe.mp4", "updated_at": 1,
               "download_args": dl_args, "media": media, "meta": meta})
        clicked = None
        for _ in range(40):
            await hist.wait_for_timeout(0.3)
            clicked = await hist.evaluate("""() => {
                const rows = [...document.querySelectorAll('#vdh_journal_list div.vdh_journal_row')];
                const row = rows.find(r => r.textContent.includes('sidecarprobe.mp4'));
                if (!row) return 'no row';
                const b = [...row.querySelectorAll('button')].find(x => x.textContent === 'Retry');
                if (!b) return 'no retry button';
                b.click(); return 'clicked';
            }""")
            if clicked == "clicked":
                break
        ok(clicked == "clicked", f"the download was started ({clicked})")

        record = None
        for _ in range(60):
            await asyncio.sleep(0.3)
            record = await hist.evaluate("""async () => {
                const got = await chrome.storage.local.get('vdh_journal');
                return (got.vdh_journal || {})['dl:sidecar_probe'] || null;
            }""")
            if record and record.get("subtitles"):
                break
        subs = (record or {}).get("subtitles") or []
        print(f"   record carries {len(subs)} rendition(s):", flush=True)
        for s in subs:
            print(f"     {s.get('language')!r:6} {s.get('type')!r:10} "
                  f"{str(s.get('url')).replace(FIXTURE, '')[:60]}", flush=True)
        ok(bool(subs), "a stream with no subtitle renditions of its own still gets some")
        ok(any("21eb2f30" in str(s.get("url")) for s in subs),
           "the UUID-named caption is on the record")
        ok(any("episode.fr" in str(s.get("url")) for s in subs),
           "so is the French one")
        ok(all(s.get("type") == "sidecar" for s in subs),
           f"and they are marked as page-fetched, not from the manifest "
           f"({sorted(set(s.get('type') for s in subs))})")

        # --- saving one ------------------------------------------------------
        print("\nD. saving it produces SubRip, not SubRip relabelled WebVTT")
        await hist.reload()
        await hist.wait_for_timeout(1500)
        buttons = await hist.evaluate("""() => {
            const rows = [...document.querySelectorAll('#vdh_journal_list div.vdh_journal_row')];
            const row = rows.find(r => r.textContent.includes('sidecarprobe.mp4'));
            return row ? [...row.querySelectorAll('button')].map(b => b.textContent) : null;
        }""")
        print(f"   buttons: {buttons}", flush=True)
        ok(bool([b for b in (buttons or []) if b == "Subtitles: en"]),
           f"English is offered on the row ({buttons})")
        ok(bool([b for b in (buttons or []) if b == "Subtitles: fr"]),
           f"and French ({buttons})")

        await hist.evaluate("""() => {
            const rows = [...document.querySelectorAll('#vdh_journal_list div.vdh_journal_row')];
            const row = rows.find(r => r.textContent.includes('sidecarprobe.mp4'));
            const b = [...row.querySelectorAll('button')]
              .find(x => x.textContent.trim() === 'Subtitles: en');
            b.click();
        }""")
        await asyncio.sleep(1.5)   # the tick is transient by design
        ticked = await hist.evaluate("""() => {
            const rows = [...document.querySelectorAll('#vdh_journal_list div.vdh_journal_row')];
            const row = rows.find(r => r.textContent.includes('sidecarprobe.mp4'));
            return row ? [...row.querySelectorAll('button')].map(b => b.textContent) : [];
        }""")
        ok(bool([t for t in ticked if t.startswith("Subtitles: en") and "\u2713" in t]),
           f"the row ticks the one it saved ({ticked})")

        body = None
        for _ in range(60):
            await asyncio.sleep(0.3)
            items = await hist.evaluate("""async () => (await chrome.downloads.search({}))
                .map(d => ({state: d.state, error: d.error, url: d.url}))""")
            for d in items:
                raw = (d["url"] or "").split(",", 1)
                text = urllib.parse.unquote(raw[1]) if len(raw) == 2 else ""
                if "eerie music" in text:
                    body = (d["state"], text)
            if body:
                break
        ok(body is not None, "the caption file was written")
        if body:
            state, text = body
            ok(state == "complete", f"and the download completed ({state})")
            ok(not text.startswith("WEBVTT"),
               "it is not a WebVTT file with SubRip timestamps inside")
            ok(re.match(r"^1\s*\n\d{2}:\d{2}:\d{2},\d{3}\s*-->\s*\d{2}:\d{2}:\d{2},\d{3}",
                        text.strip()) is not None,
               f"it starts like SubRip: index line, then comma timestamps "
               f"({text.strip()[:46]!r})")
            ok("[eerie music]" in text and "This is the sidecar caption file" in text,
               "with the cues intact")
        # a saved track is recorded on the record and ticked on the row
        saved = await hist.evaluate("""async () => {
            const got = await chrome.storage.local.get('vdh_journal');
            const rec = (got.vdh_journal || {})['dl:sidecar_probe'] || {};
            return rec.subtitles_saved || [];
        }""")
        print(f"   subtitles_saved: {saved}", flush=True)
        ok("en" in saved, f"the record says English was saved ({saved})")

        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
