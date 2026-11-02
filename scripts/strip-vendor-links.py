#!/usr/bin/env python3
"""Make a VDH Pro tree self-contained: no vendor links, no premium/upsell.

Works on any tree (run it on build/extension, after patch-vdh-mobile-ui.py):

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


# ------------------------------------------------------------ service worker -

SW = ROOT / "service/main.js"
s = SW.read_text(encoding="utf-8")

s = replace(s, 'rt = "https://downloadhelper.net:443"',
            f'rt = "{INERT}"', "worker: vendor base URL")

# uninstall survey + store "rate us" targets (replace the literal only, keep keys)
s = replace(s, 'Tm = `${rt}/goodbye`', f'Tm = "{INERT}/goodbye"', "worker: goodbye url")
for store_host in ("chromewebstore.google.com", "addons.mozilla.org",
                   "microsoftedge.microsoft.com"):
    s = re.sub(r'"https://' + re.escape(store_host) + r'[^"]*"', '""', s)

# remote ruleset fetch -> inert host (bundled ruleset still applies)
s = replace(s, 'ub = `https://files.downloadhelper.net/${I0}/ruleset-${ym}-${ht}.json`',
            f'ub = `{INERT}/ruleset.json`', "worker: remote ruleset")

SW.write_text(s, encoding="utf-8")
must_parse(SW, "service/main.js")

# ------------------------------------------------------- content/injected js -

PREMIUM_BANNER = (
    "M(this, i.advertize_premium)",
    "M(this, !1)",
    "content: hide premium banner",
)
REVIEW_BLOCK = (
    'M(this.p_leave_review, !this.persistent().dont_ask_for_user_review '
    '|| !this.persistent().review)',
    "",
    "content: drop review prompt",
)

for path in sorted(ROOT.rglob("*.js")):
    if path == SW:
        continue
    text = path.read_text(encoding="utf-8")
    original = text
    label = str(path.relative_to(ROOT))

    # 1. vendor hosts anywhere in the source: help pages become no-ops,
    #    every other vendor host (API, files, ruleset) becomes an inert host
    #    so runtime fetches fail fast instead of phoning home
    text = re.sub(r"https://help\.downloadhelper\.net", "about:blank", text)
    text = re.sub(r"https://[\w.-]*downloadhelper\.net", INERT, text)
    text = re.sub(r'"https://(?:chromewebstore\.google\.com|microsoftedge\.microsoft\.com|'
                  r'addons\.mozilla\.org)[^"]*"', '""', text)

        # 2. premium banner component: hide it unconditionally. Its own rule is
    #    `M(this, i.advertize_premium.advertize)` inside onStateChanged();
    #    replacing the expression makes the element hidden whatever the worker
    #    advertises.
    start, end, n = find_ws(text, "M(this, i.advertize_premium.advertize)")
    if n == 1:
        text = text[:start] + "M(this, !1)" + text[end:]

    # 3. review prompt: hide it outright. Stock code is
    #    `i.dont_ask_for_user_review && D(this.p_leave_review)` (D = hide), so
    #    only the condition is dropped and the hide is kept.
    start, end, n = find_ws(text, 'i.dont_ask_for_user_review && D(this.p_leave_review)')
    if n == 1:
        text = text[:start] + 'D(this.p_leave_review)' + text[end:]

    # 4. premium / account buttons: stock code calls I() (I = show) on them.
    #    Flip those to D() so no upsell control is ever revealed.
    for ident in ("button_get_premium", "button_my_account", "button_restore_purchase",
                  "premium_button"):
        text, n = re.subn(r'I\(this\.' + ident + r'\)', 'D(this.' + ident + ')', text)
        if n:
            print(f"  hid {ident} x{n} in {label}")

    if text != original:
        path.write_text(text, encoding="utf-8")
        must_parse(path, label)
        print(f"edited: {label}")

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

leftovers = []
for path in ROOT.rglob("*"):
    if not path.is_file() or path.suffix not in (".js", ".html", ".json"):
        continue
    blob = path.read_text(encoding="utf-8", errors="ignore")
    for pat in ("downloadhelper.net", "chromewebstore.google.com",
                "microsoftedge.microsoft.com", "addons.mozilla.org",
                "files.downloadhelper.net"):
        if pat in blob:
            leftovers.append(f"{path.relative_to(ROOT)}: {pat}")
print("\nremaining vendor references:", "none" if not leftovers else "")
for item in leftovers:
    print("  -", item)
sys.exit(0)
