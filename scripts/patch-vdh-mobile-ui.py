#!/usr/bin/env python3
"""Apply the Android UI patch to a VDH Pro 10.6.1.2 tree.

The tracked `extension/` directory stays PRISTINE (a vendor drop, easy to
diff against upstream). This script copies it to a build directory and
patches the copy:

    scripts/patch-vdh-mobile-ui.py                 # -> build/extension
    python3 scripts/patch-vdh-mobile-ui.py DIR     # -> DIR

What it changes (see docs/ANDROID_PORT_PLAN.md and TURN_LOG.md for why):

  service/main.js
    - "side panel" mode: toolbar click opens content/sidebar.html in a TAB
      (Edge Canary Android exposes chrome.sidePanel but has no side-panel UI).
      A second click focuses the existing tab instead of stacking copies.
    - "popup" mode: same tab. The toolbar popup on Android is a bottom sheet
      sized from the page's fixed 320/460/600 px desktop heights, so it
      showed one media row in an oversized empty sheet.
    - switching modes inside the app's settings keeps both paths on the tab.

  content/sidebar.html, content/popup.html
    - fill the viewport (100vw / 100dvh), including the popup_size variants,
      without touching the app's own #main / #toolbar flex chain.

Strict: every target must match exactly once or the script aborts without
writing. Idempotent: reruns skip already-patched files, and older patch
generations are upgraded in place.
"""
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "extension"
BUILD = REPO / "build" / "extension"

MARKER = "vdhMobileSidebar"      # service worker helper
CSS_MARK = "vdh-android-ui-v3"   # page override
OLD_CSS_MARKS = ("vdh-android-ui-v2", "vdh-android-ui")

HELPER = ('var vdhMobileSidebar=function(){'
          'function o(){chrome.tabs.create('
          '{url:chrome.runtime.getURL("/content/sidebar.html")})}'
          'try{chrome.tabs.query({url:chrome.runtime.getURL("/content/sidebar.html")},'
          'function(t){t&&t.length?chrome.tabs.update(t[0].id,{active:!0}):o()})}'
          'catch(e){o()}};')

OPEN_TAB = ('chrome.action.onClicked.removeListener(vdhMobileSidebar),'
            'chrome.action.onClicked.addListener(vdhMobileSidebar),'
            'chrome.action.setPopup({popup:""})')


def find_ws(haystack: str, needle: str):
    """Find `needle` in `haystack` ignoring ALL whitespace.

    Returns (start, end, count) with real offsets into `haystack`, or
    (None, None, count). Works for the minified vendor file and for the
    formatted one, whose line breaks land between almost every token.
    """
    keep = [i for i, ch in enumerate(haystack) if not ch.isspace()]
    flat = "".join(haystack[i] for i in keep)
    target = "".join(needle.split())
    count = flat.count(target)
    pos = flat.find(target)
    if pos < 0:
        return None, None, 0
    return keep[pos], keep[pos + len(target) - 1] + 1, count


def replace_once(text: str, old: str, new: str, label: str) -> str:
    start, end, count = find_ws(text, old)
    if count == 0:
        sys.exit(f"ABORT: {label}: target not found:\n{old[:140]}")
    if count > 1:
        sys.exit(f"ABORT: {label}: target matched {count}x (need 1):\n{old[:140]}")
    return text[:start] + new + text[end:]


def stage(target: pathlib.Path) -> pathlib.Path:
    """Copy the pristine tree to target; reuse an existing patched copy."""
    if (target / "manifest.json").exists():
        print(f"reusing existing tree: {target}")
        return target
    if not (SRC / "manifest.json").exists():
        sys.exit(f"ABORT: no pristine extension tree at {SRC}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SRC, target)
    print(f"staged pristine tree -> {target}")
    return target


def patch_worker(root: pathlib.Path) -> None:
    sw = root / "service/main.js"
    s = sw.read_text(encoding="utf-8")
    if MARKER in s:
        print("skip (already patched): service/main.js")
        return
    pairs = [
        # $b(): sidebar mode -> toolbar click opens/focuses the panel tab
        ('chrome.sidePanel.setOptions({enabled:!0}),'
         'chrome.sidePanel.setPanelBehavior({openPanelOnActionClick:!0}),'
         'chrome.action.setPopup({popup:""})', OPEN_TAB),
        # $b(): popup mode -> same tab, no tiny bottom sheet
        ('chrome.sidePanel.setOptions({enabled:!1}),'
         'chrome.sidePanel.setPanelBehavior({openPanelOnActionClick:!1}),'
         'chrome.action.setPopup({popup:chrome.runtime.getURL("/content/popup.html")})',
         OPEN_TAB),
        # K0(): choosing a mode from the in-app settings opens the tab at once
        ('e=="sidebar"&&t.isSome()&&chrome.sidePanel?.open({windowId:t.value})',
         'e=="sidebar"&&vdhMobileSidebar()'),
        # K0(): popup mode no longer tries chrome.action.openPopup on Android
        ('if(e=="popup")try{t.isSome()?chrome.action.openPopup({windowId:t.value})'
         ':chrome.action.openPopup()}catch{}else e=="sidebar"',
         'if(e=="popup"){}else e=="sidebar"'),
    ]
    for old, new in pairs:
        s = replace_once(s, old, new, "service/main.js")
    s = HELPER + "\n" + s
    sw.write_text(s, encoding="utf-8")
    check = subprocess.run(["node", "--check", str(sw)], capture_output=True, text=True)
    if check.returncode != 0:
        sys.exit("ABORT: patched service/main.js does not parse: "
                 + (check.stderr or check.stdout).strip().splitlines()[-1][:160])
    print(f"patched: service/main.js ({len(pairs)} replacements + helper, parses)")


CSS = """<style>/*vdh-android-ui-v3*/
/* Phone UI: fill the viewport however the page is opened (tab or popup
   sheet). Sizes are forced on the elements that carry the fixed desktop
   size; the app's flex chain (#main flex:1, toolbar last) is left alone. */
html, html > body {
  width: 100vw !important;
  max-width: 100vw !important;
  height: 100dvh !important;
  min-height: 100dvh !important;
  max-height: 100dvh !important;
  background-color: light-dark(hsl(240 4.8% 95.9%), hsl(240 5.7% 18.2%)) !important;
}
html[dockmode="popup"][popup_size="small"],
html[dockmode="popup"][popup_size="medium"],
html[dockmode="popup"][popup_size="big"] {
  width: 100vw !important;
  height: 100dvh !important;
}
html[dockmode="popup"][popup_size="small"] > body,
html[dockmode="popup"][popup_size="medium"] > body,
html[dockmode="popup"][popup_size="big"] > body {
  width: 100vw !important;
  height: 100dvh !important;
}
</style>"""


def patch_html(root: pathlib.Path) -> None:
    for name in ("popup.html", "sidebar.html"):
        p = root / "content" / name
        s = p.read_text(encoding="utf-8")
        if CSS_MARK in s:
            print(f"skip (already patched): {name}")
            continue
        upgraded = False
        for old_mark in OLD_CSS_MARKS:
            m = re.search(r"<style>/\*" + re.escape(old_mark) + r"\*/.*?</style>", s, re.S)
            if m:
                s = s[:m.start()] + CSS + s[m.end():]
                upgraded = True
                break
        if upgraded:
            p.write_text(s, encoding="utf-8")
            print(f"upgraded: {name} (older CSS -> v3)")
            continue
        if s.count("</head>") != 1:
            sys.exit(f"ABORT: {name}: expected exactly one </head>")
        p.write_text(s.replace("</head>", CSS + "\n</head>"), encoding="utf-8")
        print(f"patched: {name} (viewport CSS v3)")


root = stage(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else BUILD)
patch_worker(root)
patch_html(root)
print("all patches applied.")
