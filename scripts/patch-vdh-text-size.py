#!/usr/bin/env python3
"""A text-size control the user drives: one base font size, in pixels, for the
whole extension (panel, sidebar, history).

Why a variable and not a number: the phone UI stylesheet hard-codes
`font-size: 21px` on :root, on html inside the <=400px media query, and on every
component host (that last one is what beats the vendor's own `:host` shrink
rules). Rewriting those to `var(--vdh-ui-base-font, 21px)` makes one property
the single knob, and because every other size in the app is expressed in rem,
that one property rescales the entire UI.

The default stays 21px, so nothing moves until the user moves it.

What this script does:

  content/vdh-text-size.js (new, copied in with the pristine tree)
    - read/apply/save the size, localStorage for a synchronous first paint and
      chrome.storage.local for durability + live updates in the other pages
  content/popup.html, content/sidebar.html
    - a "Text size" card in the Settings view: slider, -/+ steppers, a live
      readout, a preview line, and a reset to the 21px default
  content/panel.js, content/history.js
    - bootstrap: apply the saved size as the module is evaluated

Usage: scripts/patch-vdh-text-size.py [tree]
"""
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "extension"
MODULE = ROOT / "content" / "vdh-text-size.js"
PANEL_JS = ROOT / "content" / "panel.js"
HISTORY_JS = ROOT / "content" / "history.js"
MARK = "vdh_text_size"
ANCHOR = '<vbox id="box_main">'


def die(msg):
    sys.exit("ABORT: " + msg)


def check_module(path):
    """Module scripts must be parsed as modules; `node --check x.js` is CommonJS."""
    tmp = pathlib.Path("/tmp") / (path.parent.name + "-" + path.name + ".mjs")
    tmp.write_text(pathlib.Path(path).read_text(encoding="utf-8"), encoding="utf-8")
    r = subprocess.run(["node", "--check", str(tmp)], capture_output=True, text=True)
    if r.returncode != 0:
        die(f"{path.name}: does not parse: " + " | ".join((r.stderr or r.stdout).splitlines()[:3]))


if not MODULE.exists():
    die("content/vdh-text-size.js is missing (copy it from extension/ first)")
check_module(MODULE)

# ------------------------------------------------------------- settings card --

CARD = '''<vbox id="vdh_text_size_box" class="roundedbox">
        <h2>Text size</h2>
        <p>Drag the slider to set the base text size for the whole app. It is remembered on this device.</p>
        <hbox id="vdh_text_size_row">
          <button id="vdh_text_size_minus" aria-label="Smaller text">&#8722;</button>
          <input id="vdh_text_size_range" type="range" min="15" max="32" step="1" value="21" aria-label="Text size in pixels"/>
          <button id="vdh_text_size_plus" aria-label="Bigger text">+</button>
          <span id="vdh_text_size_value">21 px</span>
        </hbox>
        <p id="vdh_text_size_preview">The quick brown fox jumps over the lazy dog. 0123456789</p>
        <hbox id="vdh_text_size_row2">
          <button id="vdh_text_size_default">Reset to 21 px</button>
        </hbox>
      </vbox>'''

CARD_CSS = '''<style>/* vdhTextSize: the size card. Same card styling as the rest of the
   settings view; the row is pinned to the card's width so a wide slider cannot
   run off the right edge. */
#vdh_text_size_box {
  padding: 10px;
  margin-top: var(--spacing-medium);
  min-width: 0;
  max-width: 100%;
  box-sizing: border-box;
}
#vdh_text_size_box h2 { margin: 0 0 6px; }
#vdh_text_size_box p { margin: 0 0 8px; line-height: 1.35; max-width: 100%; overflow-wrap: anywhere; }
#vdh_text_size_row, #vdh_text_size_row2 {
  display: flex !important;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  min-width: 0;
  max-width: 100%;
  box-sizing: border-box;
}
#vdh_text_size_row2 { margin-top: 8px; }
#vdh_text_size_range {
  flex: 1 1 120px;
  min-width: 96px;
  max-width: 100%;
  box-sizing: border-box;
  min-height: 2.2rem;
  accent-color: currentColor;
}
#vdh_text_size_row button { min-width: 46px; min-height: 2.2rem; padding: 0 12px; }
#vdh_text_size_value {
  font-weight: bold;
  min-width: 4.2em;
  text-align: right;
  font-variant-numeric: tabular-nums;
}
#vdh_text_size_preview {
  margin: 10px 0 0 !important;
  padding: 8px 10px;
  border-radius: var(--spacing-medium);
  border: 1px solid var(--theme-box-border, rgba(128, 128, 128, .35));
  background-color: var(--theme-control-bg, rgba(128, 128, 128, .12));
  line-height: 1.3;
  overflow-wrap: anywhere;
}
</style>'''

for page in ("popup.html", "sidebar.html"):
    PAGE = ROOT / "content" / page
    text = PAGE.read_text(encoding="utf-8")
    if 'id="vdh_text_size_box"' in text:
        print(f"skip (already patched): content/{page} text size card")
        continue
    if text.count(ANCHOR) != 1:
        die(f"{page}: expected exactly one settings main box to place the card in")
    # The card is the FIRST box inside the settings view, right under the Back
    # bar, so it is on screen the moment Settings opens instead of several
    # screens of scrolling down. Its style ships inside the same tree: the
    # settings view is a custom element with its own shadow root, so a <style>
    # in <head> would never reach these nodes.
    text = text.replace(ANCHOR, ANCHOR + "\n\n      " + CARD_CSS +
                        "\n\n      " + CARD, 1)
    PAGE.write_text(text, encoding="utf-8")
    print(f"patched: content/{page} (text size card)")

# ---------------------------------------------------------------- bootstrap --

PANEL_BOOT = ('import { initTextSize, installTextSizeUI } from "./vdh-text-size.js";\n'
              'initTextSize();\n'
              'installTextSizeUI();\n')
HISTORY_BOOT = ('import { initTextSize } from "./vdh-text-size.js";\n'
                'initTextSize();\n')

for js, boot in ((PANEL_JS, PANEL_BOOT), (HISTORY_JS, HISTORY_BOOT)):
    if not js.exists():
        die(f"{js.name} is missing")
    text = js.read_text(encoding="utf-8")
    if "vdh-text-size.js" in text:
        print(f"skip (already patched): content/{js.name} text size bootstrap")
        continue
    # The import has to come first: module evaluation order means the imported
    # module runs before this file's own body, which is what lets the size be
    # applied before the panel paints anything.
    js.write_text(boot + text, encoding="utf-8")
    check_module(js)
    print(f"patched: content/{js.name} (applies the saved size on load)")
