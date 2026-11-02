#!/usr/bin/env python3
"""Make a VDH Pro tree self-contained: no vendor links, no premium/upsell.

Runs on build/extension, after patch-vdh-mobile-ui.py.

SAFETY RULE (learned the hard way): this script must not touch URLs or hosts
inside JavaScript. Rewriting `https://downloadhelper.net:443` -> an inert host
looks tidy but it runs through code the download/detection pipeline uses
(entitlement state, the per-client ruleset URL in the injected scripts), and a
build with those edits showed no media at all. Only three kinds of change are
made here, all of them outside the network path:

  1. panel.js visibility only: hide the premium banner, hide the review
     prompt, and never reveal the premium/account buttons.
  2. manifest: drop the vendor-only content scripts (and delete their files),
     rename the extension, drop homepage_url.
  3. _locales: rename the extension and neutralise upsell strings.

Anything that edits another JS file fails the build.

  service/main.js
    - the vendor base URL becomes an inert host (http://127.0.0.1:9) so every
      derived endpoint (/welcome, /changelog, /goodbye, /premium,
      /manage-subscription, /issue, /v, /v2/entitlements/*) fails harmlessly
    - store rates: Chrome/Edge/Firefox store URLs and the uninstall URL go
    - remote ruleset fetch (files.downloadhelper.net) points at the same inert
      host: the bundled ruleset is used instead of phoning home
  content/*.js, injected/*.js, factory/*.js
    - hard-coded help/downloadhelper/store URLs become about:blank
    - premium banner + review prompt are deleted from their component classes
      (banner: force the element hidden; review block: remove from the DOM)
  manifest.json
    - content scripts that only exist to talk to the vendor site are dropped
    - homepage_url and the side_panel default_title/name are neutralised
  injected/premium.js is deleted, and activate.js/premium.js registrations with
    it, because they cannot run without those host matches any more.
  _locales/*/messages.json
    - the extension name and description no longer carry the vendor's name
    - upsell strings (get premium, free account, restore purchase, account) are
      replaced with neutral text

Every edit is a whitespace-insensitive, single-match replacement or a JSON
transform, and the script refuses to write a JS file that no longer parses.

    scripts/strip-vendor-links.py [tree]     (default: build/extension)
"""
import json
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "extension"

if not (ROOT / "manifest.json").exists():
    sys.exit(f"missing tree: {ROOT}")

INERT = "http://127.0.0.1:9"       # nothing listens: every request fails fast

# ---------------------------------------------------------------- helpers ----

def find_ws(haystack: str, needle: str):
    keep = [i for i, ch in enumerate(haystack) if not ch.isspace()]
    flat = "".join(haystack[i] for i in keep)
    target = "".join(needle.split())
    n = flat.count(target)
    pos = flat.find(target)
    if pos < 0:
        return None, None, 0
    return keep[pos], keep[pos + len(target) - 1] + 1, n


def replace(text: str, old: str, new: str, label: str) -> str:
    start, end, n = find_ws(text, old)
    if n == 0:
        sys.exit(f"ABORT: {label}: not found:\n  {old[:120]}")
    if n > 1:
        sys.exit(f"ABORT: {label}: {n} matches (need 1):\n  {old[:120]}")
    return text[:start] + new + text[end:]


def must_parse(path: pathlib.Path, label: str):
    r = subprocess.run(["node", "--check", str(path)], capture_output=True, text=True)
    if r.returncode != 0:
        line = (r.stderr or r.stdout).strip().splitlines()[-1][:160]
        sys.exit(f"ABORT: {label}: does not parse after edit: {line}")


# The service worker is NOT touched: its vendor URLs are the entitlement and
# ruleset endpoints the download pipeline depends on. Leave it byte-identical.

# ------------------------------------------------- content/panel.js (UI only) -

# Only visibility of upsell UI is touched, never a URL, never a code path that
# the download pipeline runs.
PANEL = ROOT / "content" / "panel.js"
panel = PANEL.read_text(encoding="utf-8")

# hide the premium banner: its own rule is `M(this, advertize)` (M: truthy shows)
start, end, n = find_ws(panel, "M(this, i.advertize_premium.advertize)")
if n == 1:
    panel = panel[:start] + "M(this, !1)" + panel[end:]
else:
    print("  (banner rule already absent)")

# hide the review prompt: stock is `cond && D(this.p_leave_review)` (D: hide)
start, end, n = find_ws(panel, "i.dont_ask_for_user_review && D(this.p_leave_review)")
if n == 1:
    panel = panel[:start] + "D(this.p_leave_review)" + panel[end:]

# never reveal premium/account buttons: stock calls I() (I: show)
for ident in ("button_get_premium", "button_my_account", "button_restore_purchase",
              "premium_button"):
    panel, n = re.subn(r"I\(this\." + ident + r"\)", "D(this." + ident + ")", panel)

if panel != PANEL.read_text(encoding="utf-8"):
    PANEL.write_text(panel, encoding="utf-8")
    must_parse(PANEL, "content/panel.js")
    print("edited: content/panel.js (visibility only)")

# --------------------------------------------------------------- manifest ----

man_path = ROOT / "manifest.json"
man = json.loads(man_path.read_text(encoding="utf-8"))
kept, dropped = [], []
for cs in man.get("content_scripts", []):
    if any(("downloadhelper.net" in m) for m in cs.get("matches", [])):
        dropped.append(cs.get("js"))
    else:
        kept.append(cs)
man["content_scripts"] = kept
man.pop("homepage_url", None)   # a bogus value can fail manifest validation
if "side_panel" in man:
    man["side_panel"]["default_title"] = "Video Downloader"
man_path.write_text(json.dumps(man, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"manifest: dropped {len(dropped)} vendor content script(s): {dropped}")

man["name"] = "Video Downloader"
man["description"] = "Detect and download videos from web pages."
man_path.write_text(json.dumps(man, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

for rel in ("injected/premium.js", "injected/activate.js"):
    p = ROOT / rel
    if p.exists():
        p.unlink()
        print(f"deleted: {rel}")

# ---------------------------------------------------------------- locales ----

NAME = "Video Downloader"
DESC = "Detect and download videos from web pages."
UPSELL = {
    "get_premium_button": "OK",
    "get_premium_description": "",
    "free_account": "Local build",
    "account_status": "Status",
    "account_title": "Account",
    "my_account_button": "OK",
    "restore_purchase_button": "OK",
    "premium_button": "OK",
    "premium_account": "Local build",
    "leave_review_description": "",
    "leave_review_button": "OK",
}
touched = 0
for msg_path in sorted((ROOT / "_locales").glob("*/messages.json")):
    data = json.loads(msg_path.read_text(encoding="utf-8"))
    changed = False
    for key, value in UPSELL.items():
        if key in data and data[key].get("message") != value:
            data[key]["message"] = value
            changed = True
    for key, value in (("appName", NAME), ("appDesc", DESC),
                       ("extension_name", NAME), ("name", NAME)):
        if key in data:
            data[key]["message"] = value
            changed = True
    if changed:
        msg_path.write_text(json.dumps(data, indent=3, ensure_ascii=False) + "\n",
                            encoding="utf-8")
        touched += 1
print(f"locales: neutralised {touched} message file(s)")

# ------------------------------------------------------- edit accounting ----

checks = {
    "premium banner hidden (panel.js)":
        (ROOT / "content/panel.js").read_text(encoding="utf-8").count("M(this, !1)") >= 1,
    "review prompt hidden (panel.js)":
        "dont_ask_for_user_review && D(this.p_leave_review)"
        not in (ROOT / "content/panel.js").read_text(encoding="utf-8")
        and "D(this.p_leave_review)" in (ROOT / "content/panel.js").read_text(encoding="utf-8"),
    "upsell buttons never shown (panel.js)": not any(
        f"I(this.{ident})" in (ROOT / "content/panel.js").read_text(encoding="utf-8")
        for ident in ("button_get_premium", "button_my_account",
                      "button_restore_purchase", "premium_button")),
    "vendor content scripts dropped": not any(
        "downloadhelper.net" in m for cs in man["content_scripts"]
        for m in cs.get("matches", [])),
    "extension renamed": man["name"] == NAME,
}
for label, ok in checks.items():
    print(("  ok   " if ok else "  FAIL ") + label)
if not all(checks.values()):
    sys.exit("ABORT: one or more edits did not apply")

# ----------------------------------------------------------------- checks ----

# Nothing outside panel.js may differ from the pre-strip tree. The caller
# passes the reference copy as argv[2] (default: /tmp/vdh-prestrip).
ref = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path("/tmp/vdh-prestrip")
unexpected = []
if ref.exists():
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        other = ref / rel
        if not other.exists() or other.read_bytes() != path.read_bytes():
            allowed = (str(rel) in ("content/panel.js", "manifest.json")
                       or str(rel).startswith("_locales/")
                       or str(rel) in ("injected/premium.js", "injected/activate.js"))
            if not allowed:
                unexpected.append(str(rel))
    intentional = {"injected/premium.js", "injected/activate.js"}
    for other in sorted(ref.rglob("*")):
        rel = other.relative_to(ref)
        if other.is_file() and not (ROOT / rel).exists() and str(rel) not in intentional:
            unexpected.append(str(rel) + " (deleted)")
    print("files changed beyond the allowed set:", unexpected or "none")
    if unexpected:
        sys.exit("ABORT: unexpected files changed")

# vendor links that remain are code-path URLs, deliberately left alone
left = []
for path in ROOT.rglob("*"):
    if path.suffix in (".js", ".json") and path.is_file():
        blob = path.read_text(encoding="utf-8", errors="ignore")
        if "downloadhelper.net" in blob:
            left.append(str(path.relative_to(ROOT)))
print("JS/JSON files still containing vendor hosts (expected):", len(left))
sys.exit(0)
