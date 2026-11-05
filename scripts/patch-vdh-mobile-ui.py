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
     - root 21px, repeated inside the <=400px media query so the stock shrink
       rule can never win;
     - font-size forced on every component host (page styles on a host always
       beat that component's own :host rule), so everything inside inherits
       the new size;
     - roomier controls/spacing and a viewport-filling layout. */
:root, html {
  font-size: 21px !important;
  --spacing-small: 4px !important;
  --spacing-medium: 8px !important;
  --spacing-large: 14px !important;
  --spacing-x-large: 24px !important;
  --video-height: 132px !important;
}
@media (max-width: 400px) {
  :root, html {
    font-size: 21px !important;
    --spacing-small: 4px !important;
    --spacing-medium: 8px !important;
    --spacing-large: 14px !important;
    --spacing-x-large: 24px !important;
    --video-height: 132px !important;
  }
}
/* the user's own font-size setting still stacks on the new base */
html[font_size="large"] { font-size: 23px !important; }
html[font_size="verylarge"] { font-size: 25px !important; }

/* win over each component's shadow-DOM `:host` shrink rule */
com-main, com-toolbar, com-banner, com-notification, com-settings, com-media,
com-media-downloaded, com-dismiss-button, com-native-menu, com-progress,
com-debug, com-details, #main > *, #toolbar > *, #settings > * {
  font-size: 21px !important;
}
html[font_size="large"] com-main, html[font_size="large"] com-toolbar,
html[font_size="large"] #main > *, html[font_size="large"] #toolbar > * {
  font-size: 23px !important;
}
html[font_size="verylarge"] com-main, html[font_size="verylarge"] com-toolbar,
html[font_size="verylarge"] #main > *, html[font_size="verylarge"] #toolbar > * {
  font-size: 25px !important;
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

/* breathing room on the sides of the whole panel */
body { padding-left: 10px !important; padding-right: 10px !important; }
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
   targets and roomier padding, scaled to the 21px base font. */
#toolbar {
  /* wrap instead of clipping: 7 bigger buttons do not fit a 412px phone on
     one line (7 x 46 + 6 x 10 + padding > viewport), and the stock bar clips */
  flex-wrap: wrap !important;
  justify-content: center !important;
  padding: 10px 12px !important;
  gap: 10px !important;
  overflow-x: visible !important;
}
#toolbar button,
#toolbar com-native-menu {
  padding: 7px 9px !important;
  min-width: 44px !important;
  min-height: 44px !important;
  border-radius: var(--spacing-medium) !important;
}
#toolbar button > svg,
#toolbar com-native-menu > svg,
#toolbar svg {
  width: 26px !important;
  height: 26px !important;
  min-width: 26px !important;
  min-height: 26px !important;
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


VIEWPORT = ('<meta name="viewport" content="width=device-width, '
            'initial-scale=1, viewport-fit=cover">')


def patch_viewport(root: pathlib.Path) -> None:
    """Without a viewport meta tag Chromium lays these pages out at a default
    ~980px width and then boosts the text to compensate, which makes everything
    look roughly twice its intended size on a phone. Declaring the device width
    makes the 21px base mean 21 real pixels and keeps the layout consistent."""
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
patch_viewport(root)
patch_history_buttons(root)
print("all patches applied.")
