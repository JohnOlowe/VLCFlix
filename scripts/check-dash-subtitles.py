"""DASH subtitles: read an MPD manifest and save the tracks as files.

Part A  the manifest is parsed: four text AdaptationSets, three of them usable
Part B  each shape is fetched and stitched into one valid WebVTT document:
        a single file, a SegmentTemplate+SegmentTimeline (cues shifted by each
        segment's own start time), and TTML converted to WebVTT
Part C  WebVTT inside MP4 boxes (wvtt) is refused rather than written broken
Part D  end to end through the panel: paste the .mpd address, Find, save every
        language, and check the files that land in the download folder
"""
import asyncio
import pathlib
import re
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
FIXTURE = "http://127.0.0.1:8140"
MPD = FIXTURE + "/dash/manifest.mpd"
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


DEEP = """function deep(root, id) {
    if (root.getElementById) { const e = root.getElementById(id); if (e) return e; }
    for (const el of root.querySelectorAll('*'))
        if (el.shadowRoot) { const r = deep(el.shadowRoot, id); if (r) return r; }
    return null;
}"""


async def load_module(page, extid):
    await page.evaluate("""async (u) => { window.VDH_SUBS = await import(u) }""",
                        f"chrome-extension://{extid}/service/vdh-subtitles.js")


async def part_a(page):
    print("\nA. the manifest is parsed")
    renditions = await page.evaluate("""async (url) => {
        const res = await fetch(url);
        const text = await res.text();
        return {mpd: window.VDH_SUBS.looksLikeMpd(text),
                renditions: window.VDH_SUBS.renditionsFromMpd(text, url)};
    }""", MPD)
    print(f"   looksLikeMpd: {renditions['mpd']}", flush=True)
    for r in renditions["renditions"]:
        print(f"   - {r['language']:3} format={r.get('format'):5} "
              f"segments={len(r.get('segments') or [])} url={r['url'][-40:]}", flush=True)
    ok(renditions["mpd"], "an .mpd body is recognised as DASH, not as a playlist")
    by_lang = {r["language"]: r for r in renditions["renditions"]}
    ok(set(by_lang) >= {"en", "es", "fr", "de"},
       f"all four text AdaptationSets are found ({sorted(by_lang)})")
    ok("v0" not in [str(r["url"]).rsplit("#", 1)[-1] for r in renditions["renditions"]],
       "the video AdaptationSet is not offered as a subtitle")
    ok(by_lang.get("en") and by_lang["en"]["url"].endswith("en.vtt"),
       f"a single-file representation resolves to the file itself "
       f"({by_lang.get('en') and by_lang['en']['url']})")
    ok(by_lang.get("es") and len(by_lang["es"]["segments"]) == 3,
       f"the segmented representation enumerates 3 segments "
       f"({by_lang.get('es') and len(by_lang['es']['segments'])})")
    ok(by_lang.get("es") and by_lang["es"]["segments"][1]["startMs"] == 8000,
       f"the second segment starts at 8s, walked from the timeline "
       f"({by_lang.get('es') and by_lang['es']['segments'][1]})")
    ok(by_lang.get("fr") and by_lang["fr"]["format"] == "ttml",
       f"the TTML one is identified as TTML ({by_lang.get('fr') and by_lang['fr']['format']})")
    ok(by_lang.get("de") and by_lang["de"].get("unsupported"),
       f"wvtt is flagged as unsupported, not silently broken "
       f"({by_lang.get('de') and by_lang['de'].get('unsupported')})")


async def part_b(page):
    print("\nB. each shape becomes one valid WebVTT document")
    en = await page.evaluate("""async (args) => {
        const [url] = args;
        const got = await fetch(url);
        const list = window.VDH_SUBS.renditionsFromMpd(await got.text(), url);
        const r = list.find((x) => x.language === 'en');
        return await window.VDH_SUBS.fetchSubtitleTrack({url: r.url});
    }""", [MPD])
    print(f"   en: {en['segments']} segment(s), {len(en['text'])} chars", flush=True)
    ok(en["text"].startswith("WEBVTT"), "the single-file track starts with WEBVTT")
    ok("Hello from the DASH single file" in en["text"],
       "and keeps its cues")

    es = await page.evaluate("""async (args) => {
        const [url] = args;
        const got = await fetch(url);
        const list = window.VDH_SUBS.renditionsFromMpd(await got.text(), url);
        const r = list.find((x) => x.language === 'es');
        return await window.VDH_SUBS.fetchSubtitleTrack({url: r.url});
    }""", [MPD])
    print(f"   es: {es['segments']} segment(s)\n{es['text'][:220]}", flush=True)
    ok(es["segments"] == 3, f"all three segments were fetched ({es['segments']})")
    ok(es["text"].count("WEBVTT") == 1,
       f"the WEBVTT header appears once, not once per segment "
       f"({es['text'].count('WEBVTT')})")
    ok("00:00:04.000 --> 00:00:06.000" in es["text"],
       "the first segment's cues sit at its timeline position (4s)")
    ok("00:00:08.000 --> 00:00:10.000" in es["text"],
       "the second segment is shifted to 8s instead of restarting at zero")
    ok("00:00:12.000 --> 00:00:13.500" in es["text"],
       "the third segment is shifted to 12s")
    ok("Tercer segmento" in es["text"], "no cue is dropped")

    fr = await page.evaluate("""async (args) => {
        const [url] = args;
        const got = await fetch(url);
        const list = window.VDH_SUBS.renditionsFromMpd(await got.text(), url);
        const r = list.find((x) => x.language === 'fr');
        return await window.VDH_SUBS.fetchSubtitleTrack({url: r.url});
    }""", [MPD])
    print(f"   fr: {fr['segments']} segment(s)\n{fr['text'][:220]}", flush=True)
    ok(fr["text"].startswith("WEBVTT"), "TTML came out as WebVTT")
    ok("00:00:01.000 --> 00:00:03.000" in fr["text"],
       "TTML begin/end became cue timings")
    ok("Bonjour" in fr["text"] and "Deuxieme ligne" in fr["text"],
       "both TTML cues survived the conversion")


async def part_c(page):
    print("\nC. wvtt is refused, not written broken")
    out = await page.evaluate("""async (args) => {
        const [url] = args;
        const got = await fetch(url);
        const list = window.VDH_SUBS.renditionsFromMpd(await got.text(), url);
        const r = list.find((x) => x.language === 'de');
        try {
          await window.VDH_SUBS.fetchSubtitleTrack({url: r.url});
          return {threw: false};
        } catch (e) { return {threw: true, message: String(e.message || e)}; }
    }""", [MPD])
    print(f"   {out}", flush=True)
    ok(out.get("threw"), f"it raises instead of saving an MP4 as subtitles ({out})")
    ok("wvtt" in (out.get("message") or "").lower() or
       "container" in (out.get("message") or "").lower(),
       f"and the reason says why ({out.get('message')})")


async def part_d(page, extid):
    print("\nD. end to end: paste the .mpd address and save every language")
    await page.goto(f"chrome-extension://{extid}/content/popup.html")
    await page.wait_for_timeout(1400)
    await page.evaluate("(() => {" + DEEP + """
        const b = deep(document, 'toolbar_button_settings');
        if (b) b.click();
    })()""")
    for _ in range(60):
        await asyncio.sleep(0.1)
        if await page.evaluate("(() => {" + DEEP +
                               " return !!deep(document, 'vdh_subs_url_box'); })()"):
            break
    await page.evaluate("(() => {" + DEEP + """
        const det = deep(document, 'vdh_subs_url_details');
        if (det && !det.open) det.querySelector('summary').click();
    })()""")
    await page.wait_for_timeout(400)
    await page.evaluate("(() => {" + DEEP + """
        deep(document, 'vdh_subs_url').value = '%s';
        deep(document, 'vdh_subs_movie_name').value = 'DASH lecture.mp4';
        deep(document, 'vdh_subs_url_find').click();
    })()""" % MPD)
    for _ in range(60):
        await asyncio.sleep(0.2)
        labels = await page.evaluate("(() => {" + DEEP + """
            const box = deep(document, 'vdh_subs_url_results');
            return [...(box ? box.querySelectorAll('button') : [])].map((b) => b.textContent.trim());
        })()""")
        if labels:
            break
    print(f"   languages offered: {labels}", flush=True)
    ok(sorted(labels) == ["Save de", "Save en", "Save es", "Save fr"],
       f"every language in the manifest is offered ({labels})")

    # The service worker writes each file with chrome.downloads (a data: URL,
    # because MV3 has no createObjectURL). Playwright intercepts downloads and
    # renames them to a uuid, so the *name* is read from the panel's own
    # "Saved <file>" line and the *bytes* are read back from the download
    # record, matched by content.

    async def click(label):
        await page.evaluate("(() => {" + DEEP + """
            const box = deep(document, 'vdh_subs_url_results');
            const b = [...box.querySelectorAll('button')]
              .find((x) => x.textContent.trim() === `%s`);
            b.click();
        })()""" % label)

    async def box_text():
        return await page.evaluate("(() => {" + DEEP + """
            const box = deep(document, 'vdh_subs_url_results');
            return box ? box.textContent : '';
        })()""")

    async def wait_note(fragment, seconds=25):
        for _ in range(seconds * 4):
            if fragment in await box_text():
                return True
            await asyncio.sleep(0.25)
        return False

    async def files():
        """Every download record, with the saved document decoded from its URL."""
        import urllib.parse
        items = await page.evaluate("""async () => (await chrome.downloads.search({}))
            .map((d) => ({state: d.state, error: d.error, url: d.url}))""")
        out = []
        for d in items:
            raw = (d["url"] or "").split(",", 1)
            out.append({
                "state": d["state"],
                "error": d["error"],
                "body": urllib.parse.unquote(raw[1]) if len(raw) == 2 else "",
            })
        return out

    async def body_with(marker):
        for f in await files():
            if marker in f["body"]:
                return f
        return None

    # --- one language at a time ------------------------------------------
    for lang, marker in [("en", "Hello from the DASH single file"),
                         ("es", "Tercer segmento"),
                         ("fr", "Bonjour")]:
        await click(f"Save {lang}")
        shown = await wait_note(f"Saved DASH lecture.{lang}.vtt")
        ok(shown, f"saving {lang} reports the file it wrote ({(await box_text()).strip()[-70:]})")
        got = await body_with(marker)
        ok(got is not None, f"the {lang} file reached the browser as a download")
        if got:
            ok(got["state"] == "complete",
               f"the {lang} download completed ({got['state']} {got['error']})")
            ok(got["body"].startswith("WEBVTT"), f"the {lang} file starts with WEBVTT")
    es = await body_with("Tercer segmento")
    ok(es and "00:00:08.000 --> 00:00:10.000" in es["body"],
       "the Spanish file has continuous timings, not three 00:00 starts")
    ok(es and "00:00:12.000 --> 00:00:13.500" in es["body"],
       "and reaches its third segment at 12s")
    fr = await body_with("Bonjour")
    ok(fr and "00:00:01.000 --> 00:00:03.000" in fr["body"],
       "the French TTML file is converted WebVTT with real cue times")

    # --- the wvtt track: refused, and the panel says why ------------------
    await click("Save de")
    await wait_note("Could not save", seconds=15)
    note = await box_text()
    junk = [f for f in await files() if "ftyp" in f["body"]]
    print(f"   de note: {note.strip()[-80:]}", flush=True)
    ok(not junk, "the wvtt track writes no file rather than an MP4 named .vtt")
    ok("wvtt" in note.lower() or "container" in note.lower(),
       f"and the panel says why instead of failing silently ({note.strip()[-80:]})")

    # --- "Save all languages": one refusal must not stop the others -------
    await page.evaluate("(() => {" + DEEP + """
        deep(document, 'vdh_subs_movie_name').value = 'DASH all.mp4';
        deep(document, 'vdh_subs_url_find').click();
    })()""")
    await asyncio.sleep(2)
    await page.evaluate("(() => {" + DEEP + """
        const b = deep(document, 'vdh_subs_url_save_all'); if (b) b.click();
    })()""")
    await wait_note("Saved DASH all.fr.vtt", seconds=30)
    await asyncio.sleep(2)
    text = await box_text()
    names2 = sorted(set(re.findall(r"Saved (DASH all\.[a-z]+\.vtt)", text)))
    print(f"   save-all saved: {names2}", flush=True)
    ok(names2 == ["DASH all.en.vtt", "DASH all.es.vtt", "DASH all.fr.vtt"],
       f"save-all writes the three readable languages and skips wvtt ({names2})")
    ok("Could not save" in text,
       "save-all still reports the one it could not read, rather than hiding it")


async def part_e(page, extid):
    """The automatic path: a DASH download the extension detected itself.

    The vendor records DASH subtitles as a Representation id and never as a
    URL (desktop muxes them with ffmpeg), so nothing was ever offered. This
    checks that such a media payload now yields savable renditions."""
    print("\nE. a DASH download the extension detected on its own")
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await load_module(page, extid)   # part D left the page on the popup
    recovered = await page.evaluate("""async (mpd) => {
        const media = {__serde_tag: "object", __serde_val: {
            hash: {__serde_tag: "primitive", __serde_val: "media_hash_dash"},
            type: {__serde_tag: "primitive", __serde_val: "mpd_playlist"},
            master_url: {__serde_tag: "url", __serde_val: mpd},
            subtitles: {__serde_tag: "some", __serde_val: [
              {__serde_tag: "object", __serde_val: {
                  hash: {__serde_tag: "primitive", __serde_val: "subtitle_hash_en"},
                  language: {__serde_tag: "primitive", __serde_val: "en"},
                  type: {__serde_tag: "primitive", __serde_val: "id"},
                  id: {__serde_tag: "primitive", __serde_val: "en"}}},
              {__serde_tag: "object", __serde_val: {
                  language: {__serde_tag: "primitive", __serde_val: "es"},
                  type: {__serde_tag: "primitive", __serde_val: "id"},
                  id: {__serde_tag: "primitive", __serde_val: "es"}}},
              {__serde_tag: "object", __serde_val: {
                  language: {__serde_tag: "primitive", __serde_val: "fr"},
                  type: {__serde_tag: "primitive", __serde_val: "id"},
                  id: {__serde_tag: "primitive", __serde_val: "fr"}}}]}}};
        return window.VDH_SUBS.renditionsFromMedia(media);
    }""", MPD)
    for r in recovered:
        print(f"   {r['language']}: {r['url'].replace('http://127.0.0.1:8140/dash/', '')}", flush=True)
    ok(len(recovered) == 3, f"all three DASH tracks are offered ({len(recovered)})")
    ok(all(r["url"].startswith("dash:") for r in recovered),
       "each id is resolved against the manifest so it can actually be fetched")
    ok(sorted(r["language"] for r in recovered) == ["en", "es", "fr"],
       "and each keeps its language")

    probes = {}
    for r in recovered:
        probes[r["language"]] = await page.evaluate(
            """async (u) => window.VDH_SUBS.probeSubtitleRendition({url: u})""", r["url"])
    print(f"   probes: {probes}", flush=True)
    ok(all(v.get("ok") for v in probes.values()),
       f"every one of them passes the probe that decides what to offer ({probes})")
    ok(probes.get("es", {}).get("segments") == 3,
       f"the segmented one reports its three segments ({probes.get('es')})")

    # and it is savable from the history row the way a finished download shows it
    await page.evaluate("""async (rec) => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        j['dl:dash_probe'] = rec;
        await chrome.storage.local.set({vdh_journal: j});
    }""", {"id": "dl:dash_probe", "download_id": "dash_probe", "status": "done",
           "filename": "DASH Course.mp4", "subdir": "", "updated_at": 1,
           "subtitles": recovered})
    await page.reload()
    await page.wait_for_timeout(1500)
    buttons = await page.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const row = rows.find((r) => r.textContent.indexOf('DASH Course.mp4') >= 0);
        return row ? [...row.querySelectorAll('button')].map((b) => b.textContent) : null;
    }""")
    print(f"   history row buttons: {buttons}", flush=True)
    ok(buttons is not None, "the DASH download shows its row in History")
    ok(sorted(b for b in (buttons or []) if b.startswith("Subtitles:")) ==
       ["Subtitles: en", "Subtitles: es", "Subtitles: fr"],
       f"each language is offered on the row ({buttons})")

    await page.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const row = rows.find((r) => r.textContent.indexOf('DASH Course.mp4') >= 0);
        const b = [...row.querySelectorAll('button')]
          .find((x) => x.textContent.trim() === 'Subtitles: es');
        b.click();
    }""")
    got = None
    for _ in range(120):
        await asyncio.sleep(0.25)
        items = await page.evaluate("""async () => (await chrome.downloads.search({}))
            .map((d) => ({state: d.state, url: d.url}))""")
        import urllib.parse
        for d in items:
            raw = (d["url"] or "").split(",", 1)
            body = urllib.parse.unquote(raw[1]) if len(raw) == 2 else ""
            if "Tercer segmento" in body:
                got = (d["state"], body)
        if got:
            break
    print(f"   saved from the row: state={got and got[0]}", flush=True)
    ok(got is not None, "saving from the history row produced the subtitle file")
    if got:
        ok(got[0] == "complete", f"and it completed ({got[0]})")
        ok("00:00:08.000 --> 00:00:10.000" in got[1],
           "with the segment timings shifted, so the file is usable")

    await page.evaluate("""async () => {
        const j = (await chrome.storage.local.get('vdh_journal')).vdh_journal || {};
        delete j['dl:dash_probe'];
        await chrome.storage.local.set({vdh_journal: j});
    }""")


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-dash", headless=True, channel="chromium",
            viewport={"width": 412, "height": 900}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)
        page = await ctx.new_page()
        await page.goto(f"chrome-extension://{extid}/content/history.html")
        await load_module(page, extid)
        await part_a(page)
        await part_b(page)
        await part_c(page)
        await part_d(page, extid)
        await part_e(page, extid)
        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
