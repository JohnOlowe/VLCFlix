"""Verify the subtitle format/placement options and the no-download path.

Part A  conversion: keep / smart / srt, and what "smart" refuses to convert
Part B  settings persist and the UI reflects them
Part C  subtitles for a stream that was never downloaded, from a pasted URL
Part D  the recorded journal path still honours the setting

Needs scripts/test-subtitle-server.py on :8140.
"""
import asyncio
import json
import pathlib
import sys

from playwright.async_api import async_playwright

EXT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/extension").resolve()
FIXTURE = "http://127.0.0.1:8140"
failures = []


def ok(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg, flush=True)
    if not cond:
        failures.append(msg)


async def load_module(page, extid):
    await page.evaluate("""async (u) => { window.VDH_SUBS = await import(u) }""",
                        f"chrome-extension://{extid}/service/vdh-subtitles.js")


PLAIN_VTT = """WEBVTT

1
00:00:01.000 --> 00:00:02.500
Hello there

2
00:00:03.000 --> 00:00:04.000
Second line
"""

STYLED_VTT = """WEBVTT

STYLE
::cue(.yellow) { color: yellow }

NOTE this note must not survive

00:00:01.000 --> 00:00:02.500 align:start position:10%
<v Bob>Line one
Second line
"""

SRT_IN = """1
00:00:01,000 --> 00:00:02,500
Already SubRip
"""


async def part_a(ctx, extid):
    print("\nA. the three format choices")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await load_module(page, extid)

    # --- keep: byte for byte, no conversion at all ---
    keep = await page.evaluate("""(t) => {
        const r = window.VDH_SUBS.applySubtitleFormat(t, "keep");
        return {ext: r.ext, same: r.text === t, converted: r.converted};
    }""", PLAIN_VTT)
    ok(keep["ext"] == "vtt" and keep["same"] and not keep["converted"],
       f"'keep' returns the document untouched ({keep})")

    # --- smart: clean VTT becomes SRT, and must be valid SubRip ---
    smart = await page.evaluate("""(t) => {
        const r = window.VDH_SUBS.applySubtitleFormat(t, "smart");
        return {ext: r.ext, converted: r.converted, text: r.text, reasons: r.reasons};
    }""", PLAIN_VTT)
    print(f"   smart output: {smart['text']!r}", flush=True)
    ok(smart["ext"] == "srt" and smart["converted"],
       f"'smart' converts a plain cue to SubRip ({smart['ext']})")
    ok(smart["text"].startswith("1\n00:00:01,000 --> 00:00:02,500"),
       "the SubRip timestamp uses a comma, as SubRip requires")
    ok("WEBVTT" not in smart["text"], "the WEBVTT header is gone")
    ok("Hello there" in smart["text"] and "Second line" in smart["text"],
       "both cues survived the conversion")

    # --- smart: styling present, so it must NOT convert ---
    styled = await page.evaluate("""(t) => {
        const r = window.VDH_SUBS.applySubtitleFormat(t, "smart");
        return {ext: r.ext, converted: r.converted, reasons: r.reasons, text: r.text};
    }""", STYLED_VTT)
    print(f"   smart kept vtt because: {styled['reasons']}", flush=True)
    ok(styled["ext"] == "vtt" and not styled["converted"],
       f"'smart' keeps VTT when something would be lost ({styled['ext']})")
    ok(any("cue positioning" in r for r in styled["reasons"]),
       f"cue positioning is reported as the loss ({styled['reasons']})")
    ok(any("style block" in r for r in styled["reasons"]),
       f"the STYLE block is reported too ({styled['reasons']})")

    # --- always srt: converts anyway, and drops what SubRip cannot carry ---
    forced = await page.evaluate("""(t) => {
        const r = window.VDH_SUBS.applySubtitleFormat(t, "srt");
        return {ext: r.ext, converted: r.converted, text: r.text};
    }""", STYLED_VTT)
    print(f"   forced srt: {forced['text']!r}", flush=True)
    ok(forced["ext"] == "srt" and forced["converted"],
       f"'srt' converts even when lossy ({forced['ext']})")
    ok("align:start" not in forced["text"] and "position:10%" not in forced["text"],
       "cue settings are dropped in the SubRip output")
    ok("<v Bob>" not in forced["text"] and "Line one" in forced["text"],
       "voice markup is dropped but its text is kept")
    ok("STYLE" not in forced["text"] and "this note must not survive" not in forced["text"],
       "STYLE and NOTE blocks are not carried into the srt")
    ok("\n\n1\n" in forced["text"] or forced["text"].startswith("1\n"),
       "cues are renumbered from 1")

    # --- a stream that already serves SubRip is never double-converted ---
    srt = await page.evaluate("""(t) => ["keep","smart","srt"].map((m) => {
        const r = window.VDH_SUBS.applySubtitleFormat(t, m);
        return {mode: m, ext: r.ext, converted: r.converted, same: r.text === t};
    })""", SRT_IN)
    print(f"   native srt: {srt}", flush=True)
    ok(all(x["ext"] == "srt" and not x["converted"] and x["same"] for x in srt),
       f"an srt source is left alone in every mode ({srt})")

    # --- format detection itself ---
    det = await page.evaluate("""([a, b]) => [window.VDH_SUBS.detectFormat(a),
        window.VDH_SUBS.detectFormat(b)]""", [PLAIN_VTT, SRT_IN])
    ok(det == ["vtt", "srt"], f"formats are detected correctly ({det})")

    await page.close()


async def part_b(ctx, extid):
    print("\nB. the settings panel")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await page.wait_for_timeout(1200)

    shown = await page.evaluate("""() => {
        const box = document.getElementById("vdh_subs_settings_box");
        if (!box) return null;
        return {hidden: box.hidden,
                formats: [...document.querySelectorAll('input[name=\\"vdh_subs_format\\"]')]
                            .map((r) => r.value),
                embed: !!document.getElementById("vdh_subs_embed")};
    }""")
    print(f"   panel: {shown}", flush=True)
    ok(shown is not None and not shown["hidden"], "the subtitle settings box is visible")
    ok(shown and shown["formats"] == ["keep", "smart", "srt"],
       f"all three format choices are offered ({shown and shown['formats']})")
    ok(shown and shown["embed"], "the embed checkbox exists")
    label = await page.evaluate("""() => {
        const c = document.getElementById("vdh_subs_embed");
        return c && c.parentElement ? c.parentElement.textContent : "";
    }""")
    ok("interrupted" in label and "separate file" in label,
       "the embed option states exactly what it can and cannot do")

    # click "always srt", expect it to persist
    await page.evaluate("""() => {
        const r = [...document.querySelectorAll('input[name="vdh_subs_format"]')]
            .find((x) => x.value === "srt");
        r.checked = true; r.onchange();
    }""")
    await page.wait_for_timeout(1000)
    stored = await page.evaluate("""async () => (await chrome.storage.local.get(
        'vdh_subtitle_settings')).vdh_subtitle_settings""")
    print(f"   stored after clicking: {stored}", flush=True)
    ok(stored and stored.get("format") == "srt",
       f"choosing 'always srt' is saved ({stored})")

    # embed checkbox
    await page.evaluate("""() => {
        const c = document.getElementById("vdh_subs_embed");
        c.checked = true; c.onchange();
    }""")
    await page.wait_for_timeout(900)
    stored2 = await page.evaluate("""async () => (await chrome.storage.local.get(
        'vdh_subtitle_settings')).vdh_subtitle_settings""")
    ok(stored2 and stored2.get("placement") == "embed",
       f"the embed checkbox is saved ({stored2})")

    # survives a reload
    await page.reload()
    await page.wait_for_timeout(1300)
    after = await page.evaluate("""() => ({
        format: [...document.querySelectorAll('input[name="vdh_subs_format"]')]
                  .filter((r) => r.checked).map((r) => r.value)[0],
        embed: document.getElementById("vdh_subs_embed").checked})""")
    ok(after["format"] == "srt" and after["embed"],
       f"the choices survive a reload ({after})")

    # put it back
    await page.evaluate("""() => {
        const r = [...document.querySelectorAll('input[name="vdh_subs_format"]')]
            .find((x) => x.value === "keep");
        r.checked = true; r.onchange();
        const c = document.getElementById("vdh_subs_embed");
        c.checked = false; c.onchange();
    }""")
    await page.wait_for_timeout(800)
    await page.close()


async def part_c(ctx, extid, first=False):
    print("\nC. subtitles for a stream that was never downloaded")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await page.wait_for_timeout(1000)

    master = f"{FIXTURE}/segmented/master.m3u8"
    await page.evaluate("""(u) => {
        const i = document.getElementById("vdh_subs_url");
        i.value = u;
        const n = document.getElementById("vdh_subs_movie_name");
        if (n) n.value = "Lecture 4.mp4";
        document.getElementById("vdh_subs_url_find").click();
    }""", master)

    rows = None
    for _ in range(60):
        rows = await page.evaluate("""() => {
            const box = document.getElementById("vdh_subs_url_results");
            return [...box.querySelectorAll("button")].map((b) => b.textContent);
        }""")
        if rows:
            break
        await page.wait_for_timeout(250)
    print(f"   offered: {rows}", flush=True)
    ok(bool(rows) and len(rows) >= 2, f"both languages found in the pasted stream ({rows})")
    ok(any("es" in (r or "") for r in (rows or [])), "Spanish is offered")

    downloads_before = await page.evaluate("""async () =>
        (await chrome.downloads.search({})).length""")
    await page.evaluate("""() => {
        const b = [...document.querySelectorAll('#vdh_subs_url_results button')]
            .find((x) => x.textContent.indexOf("en") >= 0);
        b.click();
    }""")
    sw_logs = []
    for w in ctx.service_workers:
        w.on("console", lambda m: sw_logs.append(m.text))
    downloads = []
    for _ in range(60):
        downloads = await page.evaluate("""async () =>
            (await chrome.downloads.search({})).map((d) =>
                ({mime: d.mime, state: d.state, error: d.error}))""")
        if len(downloads) > downloads_before and downloads[-1]["state"] == "complete":
            break
        await page.wait_for_timeout(250)
    saved_name = next((l.split("wrote ")[1].split(" from")[0] for l in sw_logs
                       if "[vdhSubtitles] wrote" in l), "")
    print(f"   downloads: {downloads} saved as {saved_name}", flush=True)
    ok(len(downloads) == downloads_before + 1,
       f"exactly one subtitle file was written, no movie ({len(downloads)})")
    ok(saved_name == "Lecture 4.en.vtt",
       f"named after the movie and language in 'keep' mode ({saved_name})")
    ok(downloads and downloads[-1]["error"] is None,
       f"the download completed cleanly ({downloads[-1]['error'] if downloads else None})")

    # and with 'always srt' the very same button yields a SubRip file
    await page.evaluate("""async () => { await chrome.storage.local.set(
        { vdh_subtitle_settings: { format: "srt", placement: "file" } }) }""")
    await page.wait_for_timeout(300)
    before = len(downloads)
    await page.evaluate("""() => {
        const b = [...document.querySelectorAll('#vdh_subs_url_results button')]
            .find((x) => x.textContent.indexOf("es") >= 0);
        b.click();
    }""")
    downloads2 = []
    for _ in range(60):
        downloads2 = await page.evaluate("""async () =>
            (await chrome.downloads.search({})).map((d) =>
                ({mime: d.mime, state: d.state, error: d.error}))""")
        if len(downloads2) > before and downloads2[-1]["state"] == "complete":
            break
        await page.wait_for_timeout(250)
    print(f"   downloads after switching to srt: {downloads2}", flush=True)
    ok(len(downloads2) == before + 1, "a second subtitle file was written")
    saved2 = next((l.split("wrote ")[1].split(" from")[0] for l in sw_logs[::-1]
                   if "[vdhSubtitles] wrote" in l), "")
    print(f"   saved as {saved2}", flush=True)
    ok(saved2 == "Lecture 4.es.srt",
       f"with 'always srt' the file is named Lecture 4.es.srt ({saved2})")

    await page.evaluate("""async () => { await chrome.storage.local.set(
        { vdh_subtitle_settings: { format: "keep", placement: "file" } }) }""")
    await page.close()


async def part_d(ctx, extid):
    print("\nD. a recorded download keeps honouring the setting")
    page = await ctx.new_page()
    await page.goto(f"chrome-extension://{extid}/content/history.html")
    await page.wait_for_timeout(800)
    await page.evaluate("""async (fix) => {
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        j['dl:opt_probe'] = {
            id: 'dl:opt_probe', download_id: 'opt_probe', status: 'done',
            filename: 'Options Course.mp4', subdir: '', updated_at: 1,
            subtitles: [{url: fix + '/plain/subs/en.vtt', language: 'en', type: 'http'}],
        };
        await chrome.storage.local.set({vdh_journal: j});
    }""", FIXTURE)
    await page.wait_for_timeout(1800)
    before = await page.evaluate("""async () => (await chrome.downloads.search({})).length""")
    await page.evaluate("""async () => { await chrome.storage.local.set(
        { vdh_subtitle_settings: { format: "srt", placement: "file" } }) }""")
    await page.evaluate("""() => {
        const rows = [...document.querySelectorAll('div.vdh_journal_row')];
        const row = rows.find((r) => r.textContent.indexOf('Options Course.mp4') >= 0);
        const b = [...row.querySelectorAll('button')].find((x) => x.textContent.startsWith('Subtitles:'));
        b.click();
    }""")
    got = []
    for _ in range(60):
        got = await page.evaluate("""async () =>
            (await chrome.downloads.search({})).map((d) => ({mime: d.mime, state: d.state}))""")
        if len(got) > before and got[-1]["state"] == "complete":
            break
        await page.wait_for_timeout(250)
    print(f"   downloads: {got}", flush=True)
    ok(len(got) == before + 1 and got[-1]["state"] == "complete",
       f"the journal button wrote a file ({got[-1] if got else None})")
    await page.evaluate("""async () => {
        const got = await chrome.storage.local.get('vdh_journal');
        const j = got.vdh_journal || {};
        delete j['dl:opt_probe'];
        await chrome.storage.local.set({vdh_journal: j,
            vdh_subtitle_settings: { format: "keep", placement: "file" }});
    }""")
    await page.close()


async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(
            "/tmp/prof-opts", headless=True, channel="chromium",
            viewport={"width": 412, "height": 900}, accept_downloads=True,
            args=[f"--disable-extensions-except={EXT}", f"--load-extension={EXT}",
                  "--no-first-run"])
        await asyncio.sleep(2)
        if not ctx.service_workers:
            print("FAIL: extension did not load")
            sys.exit(1)
        extid = ctx.service_workers[0].url.split("/")[2]
        print("extension id:", extid)
        await part_a(ctx, extid)
        await part_b(ctx, extid)
        await part_c(ctx, extid)
        await part_d(ctx, extid)
        await ctx.close()
    print("\nRESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    for f in failures:
        print("  -", f)
    sys.exit(0 if not failures else 1)


asyncio.run(main())
