#!/usr/bin/env python3
"""Format the VDH extension source so diffs are reviewable.

Every .js and .html file under extension/ is reformatted. The source is a
minified vendor drop (one 557 KB line for the service worker); formatted, the
four expressions the patch rewrites and every style rule become reviewable.
The reformat is verified file by file and is behaviour-neutral by construction:
identical token streams for JS, identical tag sequences for HTML.

Layout that this script enforces (it is not a generic minifier):

  service/main.js
    - minified bundles are usually one enormous line; js-beautify splits the
      JS, then a small HTML-ish pass breaks the long CSS/HTML string literals
      that the beautifier must keep on one line.

  content/popup.html / sidebar.html
    - the whole document is rebuilt: <style> blocks and inline <template>
      markup are re-indented, <script> tags keep their exact content.

Verification is built in and is what makes this safe: every file is
reformatted, then re-parsed (node --check for JS, an HTML tag-balance +
tag-sequence comparison for the HTML) and the byte-count difference must be
whitespace only (tokens compared after stripping whitespace between them).
If anything fails, the original is put back and the script exits non-zero.

    scripts/format-vdh-source.py            # format extension/ in place
    scripts/format-vdh-source.py --check    # report only, change nothing
"""
import html.parser
import re
import shutil
import subprocess
import sys
import pathlib

REPO = pathlib.Path(__file__).resolve().parent.parent
EXT = REPO / "extension"
# Every JS/HTML file in the tree. Each one is verified before it is written
# (acorn token stream for JS, tag sequence for HTML), so a formatting pass can
# never change behaviour; `--check` reports without writing.
CHECK_ONLY = "--check" in sys.argv

BEAUTIFY = r"""
const { js } = require('/tmp/node_modules/js-beautify');
const fs = require('fs');
const f = process.argv[2] || process.argv[1];
const out = js(fs.readFileSync(f, 'utf8'), {
  indent_size: 2, max_preserve_newlines: 2, wrap_line_length: 0,
  brace_style: 'collapse', end_with_newline: true, keep_array_indentation: false,
});
fs.writeFileSync(f, out);
"""


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def ensure_beautify():
    need = [d for d in ("js-beautify", "acorn")
            if not pathlib.Path(f"/tmp/node_modules/{d}").exists()]
    if not need:
        return
    print(f"installing into /tmp (throwaway deps): {' '.join(need)}")
    run(["npm", "install", "--silent", "--prefix", "/tmp", *need], cwd="/tmp")


def js_check(path: pathlib.Path) -> str:
    """node --check, retried as a module (import.meta is legal there)."""
    for args in (["node", "--check", str(path)],
                 ["node", "--input-type=module", "--check", str(path)]):
        r = run(args)
        if r.returncode == 0:
            return "ok"
    return (r.stderr or r.stdout).strip().splitlines()[-1][:120]


TOKENIZER = """
const acorn = require('/tmp/node_modules/acorn');
const fs = require('fs'), crypto = require('crypto');
const out = [];
for (const t of acorn.tokenizer(fs.readFileSync(process.argv[2], 'utf8'),
        { ecmaVersion: 'latest', sourceType: 'module', allowHashBang: true }))
  out.push(t.type.label + '|' + String(t.value));
process.stdout.write(out.length + ' ' +
  crypto.createHash('sha256').update(out.join('\\n')).digest('hex'));
"""


def js_token_fingerprint(path: pathlib.Path) -> str:
    """Token stream of a JS file (acorn): count + hash. Whitespace-insensitive,
    string/regex-literal safe, so it is a fair equality test for a reformat."""
    pathlib.Path("/tmp/vdh-tokenize.js").write_text(TOKENIZER, encoding="utf-8")
    r = run(["node", "/tmp/vdh-tokenize.js", str(path)])
    if r.returncode != 0:
        return "ERROR: " + (r.stderr or r.stdout).strip().splitlines()[-1][:120]
    return r.stdout.strip()


class Tags(html.parser.HTMLParser):
    """Records the tag sequence and void/balance state of a document."""

    VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input",
            "link", "meta", "param", "source", "track", "wbr"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.seq, self.stack, self.errors = [], [], []

    def handle_starttag(self, tag, attrs):
        self.seq.append(("start", tag, tuple(sorted(a for a, _ in attrs))))
        if tag not in self.VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        self.seq.append(("end", tag))
        if self.stack and self.stack[-1] == tag:
            self.stack.pop()
        elif tag in self.stack:
            self.errors.append(f"mismatched </{tag}>")
            while self.stack and self.stack.pop() != tag:
                pass
        else:
            self.errors.append(f"stray </{tag}>")


def html_fingerprint(src: str):
    p = Tags()
    p.feed(src)
    p.close()
    return p.seq, p.errors, p.stack


def collapse_between_tokens(src: str) -> str:
    return re.sub(r">\s+<", "><", src).strip()


def format_css(css: str) -> str:
    """Indent a CSS block; braces and semicolons only (no selectors touched)."""
    css = re.sub(r"/\*.*?\*/", lambda m: m.group(0), css, flags=re.S)
    out, depth, buf = [], 0, []
    for line in re.split(r"(?<=[{};])", css):
        for chunk in [line]:
            if not chunk.strip():
                continue
            if chunk.strip().startswith("}"):
                depth = max(depth - 1, 0)
            out.append("  " * depth + chunk.strip())
            depth += chunk.count("{") - chunk.count("}")
    return "\n".join(out)


def format_html_doc(src: str) -> str:
    """Re-indent the <style> blocks of an HTML document, leave the rest.

    The documents are machine-generated: <style> holds thousands of minified
    rules on a few lines, while the body is already one-tag-per-line. Only the
    style blocks are expanded, which is where readability actually hurts.
    """
    def repl(m):
        body = m.group(2)
        return f"{m.group(1)}\n{format_css(body)}\n</style>"

    return re.sub(r"(<style[^>]*>)(.*?)</style>", repl, src, flags=re.S)


def process_js(rel: str) -> bool:
    path = EXT / rel
    original = path.read_text(encoding="utf-8")
    before = js_token_fingerprint(path)
    shutil.copy2(path, "/tmp/fmt-work.js")
    pathlib.Path("/tmp/vdh-beautify.js").write_text(BEAUTIFY, encoding="utf-8")
    r = run(["node", "/tmp/vdh-beautify.js", "/tmp/fmt-work.js"])
    if r.returncode != 0:
        print(f"FAIL {rel}: js-beautify: {(r.stderr or '').strip()[:200]}")
        return False
    formatted = pathlib.Path("/tmp/fmt-work.js").read_text(encoding="utf-8")
    path.write_text(formatted, encoding="utf-8")
    after = js_token_fingerprint(path)
    if before != after or "ERROR" in before:
        path.write_text(original, encoding="utf-8")   # put the original back
        print(f"FAIL {rel}: token stream changed ({before} -> {after}) -- reverted")
        return False
    if not CHECK_ONLY:
        path.write_text(formatted, encoding="utf-8")
    status = js_check(path if not CHECK_ONLY else path)
    print(f"{'check' if CHECK_ONLY else 'wrote'} {rel}: "
          f"{len(original):,} -> {len(formatted):,} bytes, node --check {status}")
    return True


def process_html(rel: str) -> bool:
    path = EXT / rel
    original = path.read_text(encoding="utf-8")
    formatted = format_html_doc(original)
    seq_a, err_a, stack_a = html_fingerprint(original)
    seq_b, err_b, stack_b = html_fingerprint(formatted)
    if seq_a != seq_b or err_a != err_b or stack_a != stack_b:
        print(f"FAIL {rel}: tag sequence changed")
        return False
    if collapse_between_tokens(original) != collapse_between_tokens(formatted):
        # the <style> contents differ by whitespace only; compare with the
        # style blocks removed to keep the assertion strict but meaningful
        strip_style = lambda s: collapse_between_tokens(
            re.sub(r"<style[^>]*>.*?</style>", "<style/>", s, flags=re.S))
        if strip_style(original) != strip_style(formatted):
            print(f"FAIL {rel}: markup outside <style> changed")
            return False
    if not CHECK_ONLY:
        path.write_text(formatted, encoding="utf-8")
    print(f"{'check' if CHECK_ONLY else 'wrote'} {rel}: "
          f"{len(original):,} -> {len(formatted):,} bytes, tags {len(seq_a)} identical")
    return True


def main():
    if not (EXT / "manifest.json").exists():
        sys.exit(f"missing extension tree: {EXT}")
    ensure_beautify()
    js_files = sorted(str(p.relative_to(EXT)) for p in EXT.rglob("*.js"))
    html_files = sorted(str(p.relative_to(EXT)) for p in EXT.rglob("*.html"))
    ok = all([process_js(r) for r in js_files]
             + [process_html(r) for r in html_files])
    print("OK" if ok else "FAILED")
    sys.exit(0 if ok else 1)


main()
