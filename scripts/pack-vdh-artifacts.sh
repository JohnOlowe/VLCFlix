#!/usr/bin/env bash
# Pack the PATCHED extension into the three deliverables.
#
#   1. scripts/patch-vdh-mobile-ui.py          pristine tree -> build/extension
#   2. scripts/patch-vdh-keep-on-failure.py    keep the file when handoff fails
#   3. scripts/patch-vdh-pause-resume.py       pause / resume plumbing
#   4. scripts/strip-vendor-links.py           hide upsell UI, drop vendor scripts
#   5. scripts/pack-vdh-artifacts.sh           build/extension -> dist/*
#
#   dist/vdh-pro-10.6.1.2.crx            CRX3, Edge Canary Android
#                                        "Extension install by crx"
#   dist/vdh-pro-10.6.1.2-crx.zip        the crx inside a zip: desktop Chrome
#                                        mangles a loose .crx download
#                                        (CRX_REQUIRED_PROOF_MISSING)
#   dist/vdh-pro-10.6.1.2-sideload.zip   unpacked extension (other Chromium forks)
#
# dist/ is deliberately NOT tracked: every rebuild changes these by a few
# bytes and git would keep a copy forever (force-push is unavailable here).
#
# The signing key keeps the extension ID stable
# (gmppgmnjccgklfklpibfkejdjijlnlnd), so a new build replaces the old one in
# place and downloads/settings survive. Key search order, first hit wins:
#   $VDH_CRX_KEY, ./.secrets/vdh-crx-key.pem, ~/vdh-crx-key.pem
set -euo pipefail
cd "$(dirname "$0")/.."
EXT=${1:-build/extension}
OUT=${2:-dist}
WORK=${TMPDIR:-/tmp}/vdh-crxpack

KEY=""
for cand in "${VDH_CRX_KEY:-}" .secrets/vdh-crx-key.pem "$HOME/vdh-crx-key.pem"; do
  [ -n "$cand" ] && [ -f "$cand" ] && { KEY=$cand; break; }
done
[ -n "$KEY" ] || { echo "no signing key: set VDH_CRX_KEY or place .secrets/vdh-crx-key.pem"; exit 1; }
[ -f "$EXT/manifest.json" ] || { echo "missing extension tree: $EXT"; exit 1; }
grep -q vdhMobileSidebar "$EXT/service/main.js" || { echo "$EXT is not patched"; exit 1; }
grep -q 'vdh-android-ui' "$EXT/content/popup.html" || { echo "$EXT is not patched"; exit 1; }
grep -q 'vdh-android-ui' "$EXT/content/sidebar.html" || { echo "$EXT is not patched"; exit 1; }

CRX=$OUT/vdh-pro-10.6.1.2.crx
CRXZIP=$OUT/vdh-pro-10.6.1.2-crx.zip
SIDE=$OUT/vdh-pro-10.6.1.2-sideload.zip
mkdir -p "$OUT"
command -v node >/dev/null || { echo "node is required (crx3 CLI)"; exit 1; }
[ -x "$WORK/node_modules/.bin/crx3" ] || {
  mkdir -p "$WORK"; ( cd "$WORK" && npm init -y >/dev/null 2>&1 && npm install --silent crx3 ); }
rm -f "$CRX" "$CRXZIP" "$SIDE"
"$WORK/node_modules/.bin/crx3" -p "$KEY" -o "$CRX" "$EXT" >/dev/null

python3 - "$EXT" "$KEY" "$CRX" "$CRXZIP" "$SIDE" <<'PY'
import base64, hashlib, io, os, struct, sys, zipfile
ext, key, crx, crxzip, side = sys.argv[1:6]

data = open(crx, "rb").read()
assert data[:4] == b"Cr24", "not a CRX"
ver = struct.unpack("<I", data[4:8])[0]
hdr_len = struct.unpack("<I", data[8:12])[0]
payload = data[12 + hdr_len:]                 # CRX3: 12-byte prefix + header + zip
assert payload[:2] == b"PK", "payload is not a zip"

names = sorted(
    os.path.relpath(os.path.join(r, f), ext)
    for r, _, fs in os.walk(ext) for f in fs)
with zipfile.ZipFile(side, "w", zipfile.ZIP_DEFLATED) as z:
    for n in names:
        z.write(os.path.join(ext, n), n)
with zipfile.ZipFile(crxzip, "w", zipfile.ZIP_STORED) as z:
    z.write(crx, os.path.basename(crx))


def crc(b):
    return "%08x" % (zipfile.crc32(b) & 0xffffffff)


def crcs(blob):
    z = zipfile.ZipFile(io.BytesIO(blob))
    return {i.filename: crc(z.read(i.filename)) for i in z.infolist()}


assert crcs(payload) == crcs(open(side, "rb").read()), "CRX payload != sideload zip"
assert crcs(open(crxzip, "rb").read())[os.path.basename(crx)] == crc(data), "wrapper zip != crx"
for mark, member in (("vdhMobileSidebar", "service/main.js"),
                     ("vdh-android-ui", "content/popup.html"),
                     ("vdh-android-ui", "content/sidebar.html")):
    blob = zipfile.ZipFile(io.BytesIO(payload)).read(member)
    assert mark.encode() in blob, f"marker missing in packaged {member}"

# Extension ID = sha256 of the PUBLIC (SPKI) key, first 16 bytes, nibbles a-p.
# Mirroring node's crypto to keep this script dependency-free.
import subprocess
ext_id = subprocess.run(["node", "-e", """
const c = require('crypto'), fs = require('fs');
const k = fs.readFileSync(process.argv[1]);
const spki = c.createPublicKey(c.createPrivateKey(k)).export({type:'spki',format:'der'});
const h = c.createHash('sha256').update(spki).digest('hex').slice(0, 32);
process.stdout.write([...h].map(x => String.fromCharCode(97 + parseInt(x, 16))).join(''));
""", key], capture_output=True, text=True, check=True).stdout

print(f"CRX v{ver}, {len(names)} entries, payload CRC table == sideload zip, "
      f"markers inside payload")
print(f"extension id: {ext_id}")
for p in (crx, crxzip, side):
    print(f"  {os.path.basename(p):32s} {os.path.getsize(p):>10,d} B  "
          f"sha256 {hashlib.sha256(open(p, 'rb').read()).hexdigest()}")
PY
