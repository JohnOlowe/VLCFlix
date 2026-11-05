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
    - scale the whole UI for a phone: the root is the user's base size, 15px
      by default (stock is 12px, and 0.8rem = 9.6px below 400px wide), bigger
      spacing/video-height, thumb-
      sized controls, and it beats the stock "make everything small" rule.
    - fill viewport width and height, including the popup_size variants,
      without touching the app's own #main / #toolbar flex chain.

Strict: every target must match exactly once or the script aborts without
writing. Idempotent: reruns skip already-patched files, and older patch
generations are upgraded in place.
"""
import pathlib
import re
from html.parser import HTMLParser
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "extension"
BUILD = REPO / "build" / "extension"

MARKER = "vdhMobileSidebar"      # service worker helper
HISTORY_SRC = (pathlib.Path(__file__).resolve().parent.parent
               / "extension" / "service" / "vdh-history-opener.js").read_text(encoding="utf-8")      # service worker helper
CSS_MARK = "vdh-android-ui-v6"   # page override
OLD_CSS_MARKS = ("vdh-android-ui-v5", "vdh-android-ui-v4", "vdh-android-ui-v3",
                 "vdh-android-ui-v2", "vdh-android-ui")

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
    # listen for the history request from any content page
    LISTEN = ('try{chrome.runtime.onMessage.addListener(function(m,s){'
              'var a=m&&m.msg?m.msg:m;'
              'if(a&&a.name==="vdh_open_history"){'
              'vdhOpenHistoryOnce.open(s&&s.tab?s.tab.id:undefined)}})}catch(e){}')
    s = HELPER + "\n" + HISTORY_SRC + "\n" + LISTEN + "\n" + s
    sw.write_text(s, encoding="utf-8")
    check = subprocess.run(["node", "--check", str(sw)], capture_output=True, text=True)
    if check.returncode != 0:
        sys.exit("ABORT: patched service/main.js does not parse: "
                 + (check.stderr or check.stdout).strip().splitlines()[-1][:160])
    print(f"patched: service/main.js ({len(pairs)} replacements + helper, parses)")


CSS = """<style>/*vdh-android-ui-v6*/
/* Phone UI. The app is a set of web components, each with its own shadow
   root; the stock CSS pins :root at 12px and, below 400px wide, shrinks the
   components again via `:root, :host { font-size: 0.8rem }` (9.6px). v4 raised
   :root but never the shadow HOSTS, so the components kept shrinking.

   v5 therefore:
     - root at the user's base size (15px by default), repeated inside the
       <=400px media query so the stock shrink rule can never win;
     - font-size forced on every component host (page styles on a host always
       beat that component's own :host rule), so everything inside inherits
       the new size;
     - roomier controls/spacing and a viewport-filling layout. */
:root, html {
  /* --vdh-ui-base-font is the user's text-size setting (see
     content/vdh-text-size.js); 15px is the default and it is what the whole UI
     scales from, because every other size here is expressed in rem. The user
     chooses 5-15px in Settings; 15px reproduces the size the panel had before
     the viewport fix (15e176f) made the phone lay it out at true device width. */
  --vdh-ui-base-font: 15px;
  font-size: var(--vdh-ui-base-font) !important;
  /* Spacing is proportional to the text size. The floors used to be the 21px
     values themselves (4/8/14/24), which is why the margins looked welded to
     the sides of the screen however small the text got: below a 21px base
     every one of them was pinned at its floor and nothing moved. The floors
     are now small enough that the whole layout shrinks with the text. */
  --spacing-small: max(1px, calc(var(--vdh-ui-base-font) * 0.19048)) !important;
  --spacing-medium: max(2px, calc(var(--vdh-ui-base-font) * 0.38095)) !important;
  --spacing-large: max(3px, calc(var(--vdh-ui-base-font) * 0.66667)) !important;
  --spacing-x-large: max(5px, calc(var(--vdh-ui-base-font) * 1.14286)) !important;
  --video-height: max(32px, calc(var(--vdh-ui-base-font) * 6.28571)) !important;
  --dismiss-button-width: calc(max(7px, calc(var(--vdh-ui-base-font) * 0.7619))
    + var(--spacing-medium) + 2 * var(--hardware-pixel)) !important;
  --dismiss-button-height: calc(max(7px, calc(var(--vdh-ui-base-font) * 0.7619))
    + 2 * var(--hardware-pixel)) !important;
}
@media (max-width: 400px) {
  :root, html {
    font-size: var(--vdh-ui-base-font) !important;
    --spacing-small: max(1px, calc(var(--vdh-ui-base-font) * 0.19048)) !important;
    --spacing-medium: max(2px, calc(var(--vdh-ui-base-font) * 0.38095)) !important;
    --spacing-large: max(3px, calc(var(--vdh-ui-base-font) * 0.66667)) !important;
    --spacing-x-large: max(5px, calc(var(--vdh-ui-base-font) * 1.14286)) !important;
    --video-height: max(32px, calc(var(--vdh-ui-base-font) * 6.28571)) !important;
  }
}
/* the app's own font-size setting still stacks on the (possibly user-set) base:
   large and verylarge were 23px and 25px on top of the old 21px base, so keep
   those ratios instead of pinning absolute px values that would shrink a
   bigger base */
html[font_size="large"] { font-size: calc(var(--vdh-ui-base-font) * 1.0952) !important; }
html[font_size="verylarge"] { font-size: calc(var(--vdh-ui-base-font) * 1.1905) !important; }

/* win over each component's shadow-DOM `:host` shrink rule */
com-main, com-toolbar, com-banner, com-notification, com-settings, com-media,
com-media-downloaded, com-dismiss-button, com-native-menu, com-progress,
com-debug, com-details, #main > *, #toolbar > *, #settings > * {
  font-size: var(--vdh-ui-base-font) !important;
}
html[font_size="large"] com-main, html[font_size="large"] com-toolbar,
html[font_size="large"] #main > *, html[font_size="large"] #toolbar > * {
  font-size: calc(var(--vdh-ui-base-font) * 1.0952) !important;
}
html[font_size="verylarge"] com-main, html[font_size="verylarge"] com-toolbar,
html[font_size="verylarge"] #main > *, html[font_size="verylarge"] #toolbar > * {
  font-size: calc(var(--vdh-ui-base-font) * 1.1905) !important;
}

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

/* The footer (the toolbar: Settings, History and the rest) has to stay put at
   the bottom of the panel while the media list scrolls. That needs two things:
   com-main must be the only child that grows and shrinks (min-height: 0, or a
   flex child refuses to shrink below its content and pushes the page taller
   than the screen), and the toolbar must be pinned rather than left to be
   pushed. The page itself must not scroll -- its scrollable child does. */
com-main { flex: 1 1 auto !important; min-height: 0 !important; }
com-toolbar, com-banner {
  flex: 0 0 auto !important;
  position: sticky !important;
  z-index: 5 !important;
}
com-toolbar { bottom: 0 !important; }
com-banner { top: 0 !important; }
html { overflow: hidden !important; }

/* breathing room on the sides of the whole panel: proportional to the text,
   so a smaller size also means a smaller margin (10px at the old 21px base) */
body {
  padding-left: max(2px, calc(var(--vdh-ui-base-font) * 0.47619)) !important;
  padding-right: max(2px, calc(var(--vdh-ui-base-font) * 0.47619)) !important;
}
#main > *, #settings > * { margin-left: var(--spacing-medium) !important;
                           margin-right: var(--spacing-medium) !important; }
#main, #settings { overflow-y: auto !important; }
com-media, com-debug, com-banner, #nomedia {
  margin-left: var(--spacing-medium) !important;
  margin-right: var(--spacing-medium) !important;
}
com-toolbar, #toolbar { padding: var(--spacing-medium) !important; min-height: 3.2rem !important; }
button, input, select, [role="button"] { min-height: 2.2rem !important; }
button { padding: 0 var(--spacing-medium) !important; }
/* --------------------------------------------------------------------------
   History page. It is a full page of its own, so nothing in it may be wider
   than the screen: the user should never have to swipe sideways to read a row.
   The media rows are web components (shadow roots), so the rules that reach
   inside them are injected into their templates further down; these are the
   page-level ones.
   -------------------------------------------------------------------------- */
#main { max-width: min(600px, 100%) !important; }
#downloaded_container, #downloaded_container > com-media-downloaded,
#history_empty, #history_controls, #vdh_journal_box {
  max-width: 100% !important;
  min-width: 0 !important;
}
/* the search box and the two buttons share one row; without wrapping they
   push the row past the right edge on a narrow screen */
#history_controls { flex-wrap: wrap !important; }
#history_controls > input { flex: 1 1 40% !important; min-width: 0 !important; }
#history_controls > spacer { flex-basis: 100% !important; height: 0 !important; }
h1 > img {
  width: max(12px, calc(var(--vdh-ui-base-font) * 1.14286)) !important;
  height: max(12px, calc(var(--vdh-ui-base-font) * 1.14286)) !important;
  margin-right: max(4px, calc(var(--vdh-ui-base-font) * 0.95238)) !important;
}
/* the journal box ("Storage & interrupted downloads") and its rows */
#vdh_journal_box {
  padding: max(4px, calc(var(--vdh-ui-base-font) * 0.47619)) !important;
}
#vdh_journal_box button { max-width: 100% !important; }
.vdh_journal_row {
  gap: max(2px, calc(var(--vdh-ui-base-font) * 0.38095)) !important;
  padding: max(2px, calc(var(--vdh-ui-base-font) * 0.28571)) 0 !important;
}
.vdh_journal_label {
  flex: 1 1 45% !important;
  min-width: 0 !important;
  overflow-wrap: anywhere !important;
}
</style>"""


class TagCounter(HTMLParser):
    """Tag balance for the patched document. An unclosed <style> makes the
    parser treat the rest of the file as CSS text, so <body> and its components
    disappear -- the app then throws "DOM not valid" and the panel is blank."""

    VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input",
            "link", "meta", "param", "source", "track", "wbr"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.tags, self.stack = [], []

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, dict(attrs).get("id")))
        if tag not in self.VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if self.stack and self.stack[-1] == tag:
            self.stack.pop()


def parsed_shape(text: str):
    tc = TagCounter()
    tc.feed(text)
    tc.close()
    return tc


def verify_document(name: str, text: str, required=("com-main", "com-settings", "com-toolbar")) -> None:
    """Compare the patched document's parsed shape with the pristine one.

    The vendor markup is genuinely loose (unclosed <template>/<vbox> at EOF), so
    absolute balance is not the test. What must hold is that the patch changed
    nothing structural: same tag sequence, same leftover stack, and the three
    components still present. An unclosed <style> breaks all three -- the rest
    of the file becomes CSS text and this check fails the build.
    """
    before = parsed_shape((SRC / "content" / name).read_text(encoding="utf-8"))
    after = parsed_shape(text)
    # the patch adds exactly one <style> block of its own
    extra_styles = [t for t in after.tags if t[0] == "style"].__len__() - \
                   [t for t in before.tags if t[0] == "style"].__len__()
    stripped = [t for i, t in enumerate(after.tags)
                if not (t[0] == "style" and i == after.tags.index(t))]
    problems = []
    if stripped != before.tags:
        problems.append("tag sequence differs from the pristine document")
    if extra_styles != 1:
        problems.append(f"expected 1 added <style>, found {extra_styles}")
    if after.stack != before.stack:
        problems.append(f"unclosed stack changed: {after.stack}")
    if text.count("<style") != text.count("</style>"):
        problems.append(f"<style> {text.count('<style')} vs </style> {text.count('</style>')}")
    for needed in required:
        if needed not in [t for t, _ in after.tags]:
            problems.append(f"{needed} missing from parsed DOM")
    if problems:
        sys.exit(f"ABORT: {name}: patched document is broken: {'; '.join(problems)}")
    print(f"  verified: {name} parses" + ("" if "history" not in name else " (history page)") + " with the components intact")


TOOLBAR_MARK = "vdh-android-toolbar"

TOOLBAR_CSS = """/*vdh-android-toolbar*/
/* Lives INSIDE the com-toolbar shadow template's style block: icons and the
   bar's own padding are only reachable from there. Bigger icons, real tap
   targets and roomier padding, scaled to the base font size. */
#toolbar {
  /* wrap instead of clipping: 7 bigger buttons do not fit a 412px phone on
     one line (7 x 46 + 6 x 10 + padding > viewport), and the stock bar clips */
  flex-wrap: wrap !important;
  justify-content: center !important;
  padding: max(2px, calc(var(--vdh-ui-base-font) * 0.47619))
           max(3px, calc(var(--vdh-ui-base-font) * 0.57143)) !important;
  gap: max(2px, calc(var(--vdh-ui-base-font) * 0.47619)) !important;
  overflow-x: visible !important;
}
/* Icons and tap targets scale with the text all the way down: the floors are
   20px for a target and 12px for an icon, small enough that the bar shrinks
   with the text and the seven buttons end up on ONE line instead of wrapping.
   They never did before, because every size in here was pinned at 44/26 no
   matter how small the text was. At the 15px default this is a 31px target
   and a 19px icon: 7 x 31 + 6 gaps + padding = 280px, one line on 360px. */
#toolbar button,
#toolbar com-native-menu {
  padding: max(1px, calc(var(--vdh-ui-base-font) * 0.33333))
           max(2px, calc(var(--vdh-ui-base-font) * 0.42857)) !important;
  min-width: max(20px, calc(var(--vdh-ui-base-font) * 2.09524)) !important;
  min-height: max(20px, calc(var(--vdh-ui-base-font) * 2.09524)) !important;
  border-radius: var(--spacing-medium) !important;
}
#toolbar button > svg,
#toolbar com-native-menu > svg,
#toolbar svg {
  width: max(12px, calc(var(--vdh-ui-base-font) * 1.2381)) !important;
  height: max(12px, calc(var(--vdh-ui-base-font) * 1.2381)) !important;
  min-width: max(12px, calc(var(--vdh-ui-base-font) * 1.2381)) !important;
  min-height: max(12px, calc(var(--vdh-ui-base-font) * 1.2381)) !important;
  max-width: max(12px, calc(var(--vdh-ui-base-font) * 1.2381)) !important;
  max-height: max(12px, calc(var(--vdh-ui-base-font) * 1.2381)) !important;
}
"""


def patch_toolbar_template(root: pathlib.Path) -> None:
    """Insert TOOLBAR_CSS before the first </style> of the toolbar template.

    The page-level stylesheet cannot reach into a shadow root, so the toolbar's
    icon sizes and padding have to be edited in the template that component
    clones. Inserting before an existing </style> keeps the block balanced --
    an unclosed <style> here would swallow the toolbar markup.
    """
    for name in ("popup.html", "sidebar.html"):
        p = root / "content" / name
        text = p.read_text(encoding="utf-8")
        if TOOLBAR_MARK in text:
            print(f"skip (toolbar already patched): {name}")
            continue
        start = text.find('<template id="toolbar-template">')
        if start < 0:
            sys.exit(f"ABORT: {name}: toolbar template not found")
        close = text.find("</style>", start)
        end_tpl = text.find("</template>", start)
        if close < 0 or (0 <= end_tpl < close):
            sys.exit(f"ABORT: {name}: toolbar template has no <style> block")
        text = text[:close] + TOOLBAR_CSS + text[close:]
        verify_document(name, text)
        p.write_text(text, encoding="utf-8")
        print(f"patched: {name} (toolbar icons + padding, inside shadow template)")


MEDIA_MARK = "vdh-android-media-scale"

MEDIA_TEMPLATES = ("media-template", "media-preview-template",
                   "media-discovered-template", "media-downloading-template",
                   "media-downloaded-template", "media-selector-template")

MEDIA_CSS = """/*vdh-android-media-scale*/
/* Inside a media row's own shadow root, which no page stylesheet can reach.
   Three things in here were fixed no matter what the text size was:
     - the icons are sized by width/height ATTRIBUTES (16), not by CSS, so they
       never scaled and the row of buttons stayed as wide as it ever was;
     - the vendor sets `flex-shrink: 0` on every box, hbox and vbox, so a row
       pushes itself off the right edge of the screen instead of wrapping;
     - --video-height and --dismiss-button-* are re-declared by each template,
       which beats the page's inherited value, so they have to be redone here.
   All three are now proportional to the base text size. */
:host {
  --video-height: max(32px, calc(var(--vdh-ui-base-font) * 6.28571)) !important;
  --dismiss-button-width: calc(max(7px, calc(var(--vdh-ui-base-font) * 0.7619))
    + var(--spacing-medium) + 2 * var(--hardware-pixel)) !important;
  --dismiss-button-height: calc(max(7px, calc(var(--vdh-ui-base-font) * 0.7619))
    + 2 * var(--hardware-pixel)) !important;
}
/* The Pause button was rendered but unreadable: #box_combo > * paints its
   children in --theme-colored-fg (near-white), and only #button_stop is given
   a background of its own, so Pause came out white text on a near-white
   button. Give it a background, in slate so it still reads as a different
   action from the red Cancel beside it. */
#button_pause {
  --theme-control-bg: light-dark(var(--slate-500), var(--slate-600)) !important;
  --theme-control-bg-hover: light-dark(var(--slate-400), var(--slate-700)) !important;
  --theme-control-border: light-dark(var(--slate-700), var(--slate-800)) !important;
}
/* Pause and Cancel are the buttons you press mid-download, so they keep a
   finger-sized floor (20px, the same one the toolbar uses) however small the
   text gets -- at 5px a proportional button is 10px tall and unhittable. */
#box_combo > button {
  min-height: max(20px, calc(var(--vdh-ui-base-font) * 2.09524)) !important;
}
button > svg, [tooltip] > svg {
  width: max(10px, calc(var(--vdh-ui-base-font) * 0.7619)) !important;
  height: max(10px, calc(var(--vdh-ui-base-font) * 0.7619)) !important;
}
box, hbox, vbox, stack, p, button, select, input, com-native-menu, img {
  max-width: 100% !important;
}
hbox, vbox, box, stack, .media_hbox, .media_vbox, #box_combo {
  flex-shrink: 1 !important;
  min-width: 0 !important;
}
.media_hbox { flex-wrap: wrap !important; }
.media_filename, p, span {
  overflow-wrap: anywhere !important;
  word-break: break-word !important;
}
button, com-native-menu, select {
  padding: max(1px, calc(var(--vdh-ui-base-font) * 0.19048))
           max(2px, calc(var(--vdh-ui-base-font) * 0.38095)) !important;
}
"""


def patch_media_templates(root: pathlib.Path) -> None:
    """Scale the media rows from the inside.

    The toolbar trick, generalised: a component's own shadow root is only
    reachable from the template it clones, so the CSS goes in before that
    template's first </style>. Inserting rather than appending keeps the block
    balanced -- an unclosed <style> here would swallow the component markup.
    """
    for name in ("popup.html", "sidebar.html", "history.html"):
        path = root / "content" / name
        text = path.read_text(encoding="utf-8")
        changed = False
        for tpl in MEDIA_TEMPLATES:
            anchor = f'<template id="{tpl}">'
            start = text.find(anchor)
            if start < 0:
                continue
            if MEDIA_MARK in text[start:text.find("</template>", start)]:
                continue
            close = text.find("</style>", start)
            end_tpl = text.find("</template>", start)
            if close < 0 or (0 <= end_tpl < close):
                sys.exit(f"ABORT: {name}: {tpl} has no <style> block")
            text = text[:close] + MEDIA_CSS + text[close:]
            changed = True
        if not changed:
            print(f"skip (media rows already scaled): {name}")
            continue
        verify_document(name, text,
                        required=() if name == "history.html"
                        else ("com-main", "com-settings", "com-toolbar"))
        path.write_text(text, encoding="utf-8")
        print(f"patched: {name} (media rows scale and fit the width)")



VIEWPORT = ('<meta name="viewport" content="width=device-width, '
            'initial-scale=1, viewport-fit=cover">')


def patch_viewport(root: pathlib.Path) -> None:
    """Without a viewport meta tag Chromium lays these pages out at a default
    ~980px width and then boosts the text to compensate, which makes everything
    look roughly twice its intended size on a phone. Declaring the device width
    makes the base size mean real pixels and keeps the layout consistent."""
    for name in ("popup.html", "sidebar.html", "history.html"):
        p = root / "content" / name
        s = p.read_text(encoding="utf-8")
        if 'name="viewport"' in s:
            print(f"skip (already patched): {name} viewport")
            continue
        if s.count("</head>") != 1:
            sys.exit(f"ABORT: {name}: expected exactly one </head>")
        head = s.split("</head>")[0]
        out = head + VIEWPORT + "\n</head>" + s.split("</head>", 1)[1]
        # earlier passes have already added their own tags, so compare with the
        # document as it stands rather than with the pristine one
        was = parsed_shape(s)
        now = parsed_shape(out)
        if ([t for t in now.tags if t[0] != "meta"] !=
                [t for t in was.tags if t[0] != "meta"]):
            sys.exit(f"ABORT: {name}: the viewport edit changed the document shape")
        if now.stack != was.stack or s.count("<style") != out.count("<style"):
            sys.exit(f"ABORT: {name}: the viewport edit unbalanced the document")
        p.write_text(out, encoding="utf-8")
        print(f"patched: {name} (viewport meta, device width)")


def patch_html(root: pathlib.Path) -> None:
    """history.html is included because the panel shows that page inside itself:
    on its own it keeps the vendor's 12px root, which would make the history view
    a different size from the panel it sits in."""
    for name in ("popup.html", "sidebar.html", "history.html"):
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
            verify_document(name, s)
            p.write_text(s, encoding="utf-8")
            print(f"upgraded: {name} (older CSS -> v6)")
            continue
        if s.count("</head>") != 1:
            sys.exit(f"ABORT: {name}: expected exactly one </head>")
        out = s.replace("</head>", CSS + "\n</head>")
        verify_document(name, out, required=() if name == "history.html"
                        else ("com-main", "com-settings", "com-toolbar"))
        p.write_text(out, encoding="utf-8")
        print(f"patched: {name} (phone scale CSS v6)")


HIST_MARK = "vdhOpenHistoryOnce()"


PANEL_BRIDGE = """
/* vdhHistoryPanel: history as a view inside the panel, exactly like Settings.
   The view swaps in place and its Back button restores the panel; nothing is
   opened in a tab or a window. Loaded from here because both popup.html and
   sidebar.html already load this module, so no markup or extra script tag (and
   therefore no CSP change) is needed. */
window.vdhHistoryOpen = function () {
  function view() {
    if (window.vdhHistoryPanel) { window.vdhHistoryPanel.open(); return true; }
    return false;
  }
  try {
    if (view()) return;
    import(chrome.runtime.getURL("/content/vdh-history-panel.js")).then(function () {
      if (!view()) { location.href = "/content/history.html"; }
    }).catch(function () {
      /* last resort, and still the same extension page */
      location.href = "/content/history.html";
    });
  } catch (e) {
    location.href = "/content/history.html";
  }
};
"""


def patch_history_buttons(root: pathlib.Path) -> None:
    """The history button used to always open a full tab, ignoring the user's
    dockmode choice. Route every occurrence through the opener helper."""
    total = 0
    for name in ("panel.js", "history.js", "register_components.js"):
        p = root / "content" / name
        s = p.read_text(encoding="utf-8")
        if HIST_MARK in s:
            print(f"skip (already patched): content/{name} history button")
            continue
        out, n = re.subn(r"[A-Za-z_$][\w$]*\.tabs\.create\(\{\s*url:\s*\"/content/history\.html\"\s*\}\)",
                         "@@OPEN@@", s)
        if not n:
            sys.exit(f"ABORT: content/{name}: no history tab creation found")
        # the helper lives in the worker; ask it rather than duplicating logic.
        # The extension only dispatches {msg, channel} envelopes, so the request
        # must be wrapped the same way or the worker never sees it.
        # consume the "namespace." that preceded the call, otherwise the site
        # reads `Wr.chrome.runtime...` (Wr has no such member) and throws.
        out, n2 = re.subn(r"[A-Za-z_$][\w$]*\.@@OPEN@@", "@@CALL@@", out)
        if n2 != n:
            sys.exit(f"ABORT: content/{name}: {n} call(s) but {n2} namespace(s)")
        # In place first (like Settings), and if that is not possible navigate
        # this very page, which also keeps it inside the panel.
        out = out.replace("@@CALL@@", "vdhHistoryOpen()")
        # the original handler closed the popup after opening history in a tab.
        # The view now swaps in place, so closing the window would destroy it.
        out, closed = re.subn(
            r",\s*this\.(?:persistent\(\)\.)?dockmode\s*==\s*\"popup\"\s*&&\s*window\.close\(\)",
            "", out)
        if not closed:
            sys.exit(f"ABORT: content/{name}: the window.close() after history "
                     "was not found, so history would close itself")
        if name == "panel.js":
            # the bridge must exist BEFORE the module body runs: the click handler
            # references it, and a page error can abort the module before it loads
            out = PANEL_BRIDGE + "\n" + out
        p.write_text(out, encoding="utf-8")
        total += n
        print(f"patched: content/{name} ({n} history button(s))")
    if not total:
        sys.exit("ABORT: no history buttons patched")



root = stage(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else BUILD)
patch_worker(root)
patch_html(root)
patch_toolbar_template(root)
patch_media_templates(root)
patch_viewport(root)
patch_history_buttons(root)
print("all patches applied.")
