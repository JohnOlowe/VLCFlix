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
    - "popup" mode is left exactly as shipped: it stays a toolbar popup.
      Patch v3 rerouted it to a tab; that was wrong (user feedback).

  content/sidebar.html, content/popup.html
    - scale the whole UI up for a phone: 20px root (stock is 12px, and
      0.8rem = 9.6px below 400px wide), bigger spacing/video-height, thumb-
      sized controls, and it beats the stock "make everything small" rule.
    - fill viewport width and height, including the popup_size variants,
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
CSS_MARK = "vdh-android-ui-v4"   # page override
OLD_CSS_MARKS = ("vdh-android-ui-v3", "vdh-android-ui-v2", "vdh-android-ui")

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
        # $b(): sidebar mode -> toolbar click opens/focuses the sidebar page in
        # a tab (Edge Canary Android has chrome.sidePanel but no side-panel
        # UI). Popup mode is left exactly as shipped.
        ('chrome.sidePanel.setOptions({enabled:!0}),'
         'chrome.sidePanel.setPanelBehavior({openPanelOnActionClick:!0}),'
         'chrome.action.setPopup({popup:""})', OPEN_TAB),
        # K0(): choosing sidebar mode from the in-app settings opens the tab
        ('e=="sidebar"&&t.isSome()&&chrome.sidePanel?.open({windowId:t.value})',
         'e=="sidebar"&&vdhMobileSidebar()'),
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


CSS = """<style>/*vdh-android-ui-v4*/
/* Phone UI. Two problems on Android, both fixed here:

   1. everything is drawn at desktop density -- :root is 12px and the whole
      app is rem-based, worse, at <=400px wide the stock sheet applies a
      "the sidebar is real small, let's make everything small" rule
      (:root font-size 0.8rem => 9.6px, spacing halved). A phone popup sheet
      sits right at that threshold, which is why the UI looked tiny.
   2. the sheet was sized for a 480x600 desktop popup, so it either clipped
      or left a big empty area on a 412px-wide phone.

   So: one scale-up rule that beats the <=400px shrink, generous tap targets,
   and the page fills whatever box the browser gives it. #main keeps the
   app's own flex/scroll chain, so extra height is used by the media list
   instead of leaving a void. */
:root, :host {
  font-size: 20px !important;
  --spacing-small: 4px !important;
  --spacing-medium: 8px !important;
  --spacing-large: 14px !important;
  --spacing-x-large: 24px !important;
  --video-height: 128px !important;
}
/* neutralise the stock "real small" shrink (same specificity, later wins) */
@media (max-width: 400px) {
  :root, :host {
    font-size: 20px !important;
    --spacing-small: 4px !important;
    --spacing-medium: 8px !important;
    --spacing-large: 14px !important;
    --spacing-x-large: 24px !important;
    --video-height: 128px !important;
  }
}
/* the user's own font-size choice still stacks on top of the new base */
html[font_size="large"] { font-size: 22px !important; }
html[font_size="verylarge"] { font-size: 24px !important; }

html, html > body {
  width: 100vw !important;
  max-width: 100vw !important;
  height: 100dvh !important;
  min-height: 100dvh !important;
  max-height: 100dvh !important;
}
html[dockmode="popup"][popup_size="small"],
html[dockmode="popup"][popup_size="medium"],
html[dockmode="popup"][popup_size="big"],
html[dockmode="popup"][popup_size="small"] > body,
html[dockmode="popup"][popup_size="medium"] > body,
html[dockmode="popup"][popup_size="big"] > body {
  width: 100vw !important;
  height: 100dvh !important;
}

/* use the width: tighter side margins, full-bleed rows, list takes the rest */
#main, #settings { overflow-y: auto !important; }
com-media, com-debug, com-banner, #nomedia {
  margin-left: var(--spacing-medium) !important;
  margin-right: var(--spacing-medium) !important;
}
com-toolbar, #toolbar { padding: var(--spacing-medium) !important; min-height: 3rem !important; }

/* thumb-sized controls (Android guideline: ~48px / 2.4rem at this scale) */
button, input, select, [role="button"] { min-height: 2.2rem !important; }
button { padding: 0 var(--spacing-medium) !important; }
"""


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
            print(f"upgraded: {name} (older CSS -> v4)")
            continue
        if s.count("</head>") != 1:
            sys.exit(f"ABORT: {name}: expected exactly one </head>")
        p.write_text(s.replace("</head>", CSS + "\n</head>"), encoding="utf-8")
        print(f"patched: {name} (phone scale CSS v4)")


root = stage(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else BUILD)
patch_worker(root)
patch_html(root)
print("all patches applied.")
