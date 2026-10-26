#!/usr/bin/env bash
# Build the Windows desktop app: VLCFlix.exe + payload + installer.
#
# Produces, in dist/win/:
#   VLCFlix-Setup-<version>.exe   NSIS installer (Program Files, desktop icon)
#   VLCFlix-portable-<version>.zip  the same payload, no install
#
# Cross-compiled on Linux with mingw-w64; the resulting binaries cannot be run
# here, so treat the build as verified-by-compilation only (see CONTEXT.md).
#
# Requires: x86_64-w64-mingw32-g++, makensis, curl, unzip, node/npm.

set -euo pipefail

VERSION="${VERSION:-1.0.0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="$ROOT/.cache/winbuild"
DIST="$ROOT/dist/win"
STAGE="$DIST/VLCFlix"
NODE_VERSION="20.18.1"
WV2_VERSION="1.0.2592.51"

say() { printf '\n\033[1;33m==> %s\033[0m\n' "$*"; }

command -v x86_64-w64-mingw32-g++ >/dev/null || { echo "missing: mingw-w64"; exit 1; }
command -v makensis >/dev/null || { echo "missing: nsis"; exit 1; }
command -v zip >/dev/null || { echo "missing: zip"; exit 1; }

mkdir -p "$CACHE" "$DIST"
export TMPDIR="$CACHE/tmp"
mkdir -p "$TMPDIR"
rm -rf "$STAGE"
mkdir -p "$STAGE/resources/app"

# --- 1. node.exe ------------------------------------------------------------
NODE_DIR="$CACHE/node-v$NODE_VERSION-win-x64"
if [[ ! -f "$NODE_DIR/node.exe" ]]; then
  say "fetching node.exe $NODE_VERSION"
  curl -sSL -o "$CACHE/node.zip" \
    "https://nodejs.org/dist/v$NODE_VERSION/node-v$NODE_VERSION-win-x64.zip"
  ( cd "$CACHE" && unzip -o -q node.zip )
fi
cp "$NODE_DIR/node.exe" "$STAGE/resources/node.exe"

# --- 2. WebView2 loader + headers ------------------------------------------
WV2_DIR="$CACHE/webview2-$WV2_VERSION"
if [[ ! -f "$WV2_DIR/build/native/include/WebView2.h" ]]; then
  say "fetching WebView2 SDK $WV2_VERSION"
  curl -sSL -o "$CACHE/webview2.nupkg" \
    "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$WV2_VERSION"
  mkdir -p "$WV2_DIR"
  ( cd "$WV2_DIR" && unzip -o -q "$CACHE/webview2.nupkg" )
fi
cp "$WV2_DIR/runtimes/win-x64/native/WebView2Loader.dll" "$STAGE/resources/WebView2Loader.dll"

# --- 3. native shell --------------------------------------------------------
say "compiling VLCFlix.exe"
x86_64-w64-mingw32-windres "$ROOT/src/win/app.rc" -O coff -o "$CACHE/app.res.o"
x86_64-w64-mingw32-g++ \
  -std=c++17 -municode -mwindows -O2 -DUNICODE -D_UNICODE \
  -I "$ROOT/src/win/shim" \
  -I "$WV2_DIR/build/native/include" \
  "$ROOT/src/win/main.cpp" "$CACHE/app.res.o" \
  -o "$STAGE/VLCFlix.exe" \
  -lws2_32 -lole32 -loleaut32 -lshell32 -luuid -ladvapi32 -luser32 -lgdi32 \
  -static-libgcc -static-libstdc++ -s

# --- 4. application payload -------------------------------------------------
say "assembling payload"
cp -r "$ROOT/server" "$ROOT/public" "$STAGE/resources/app/"
cp "$ROOT/package.json" "$ROOT/package-lock.json" "$STAGE/resources/app/"
cp -r "$ROOT/samples" "$STAGE/resources/app/" 2>/dev/null || true

# Production dependencies only. express and fast-xml-parser are pure JavaScript,
# so the Linux-installed tree is byte-identical to what Windows would install.
( cd "$STAGE/resources/app" && npm ci --omit=dev --no-audit --no-fund --silent )
rm -rf "$STAGE/resources/app/data"

cat > "$STAGE/resources/app/README.txt" <<'TXT'
VLCFlix - desktop build

Double-click VLCFlix.exe (or the Start Menu / desktop shortcut).

Requirements for playback:
  * VLC media player installed (https://www.videolan.org) - VLCFlix drives it.
  * ffmpeg on PATH is optional; without it, artwork falls back to generated
    gradients and hover previews are unavailable.

Your library settings, progress and generated artwork are stored in
  %LOCALAPPDATA%\VLCFlix
and are kept when you uninstall, so reinstalling remembers your library.
TXT

# --- 5. manifest for the portable zip --------------------------------------
( cd "$STAGE" && find . -type f -printf '%s %p\n' | sort -k2 > "$DIST/manifest.txt" )

# --- 6. installer -----------------------------------------------------------
say "building installer"
# NSIS mmaps a solid-compression block in its temp directory; /tmp is often a
# small tmpfs, so point it at the build cache instead of failing with a SIGBUS.
export TMPDIR="$CACHE/tmp"
mkdir -p "$TMPDIR"
makensis -V2 -DVERSION="$VERSION" -DSTAGE="$STAGE" -DOUTDIR="$DIST" \
  -DICONPATH="$ROOT/src/win/icon.ico" \
  -DLICENSEPATH="$ROOT/LICENSE.txt" \
  "$ROOT/src/win/installer.nsi"

# --- 7. portable zip --------------------------------------------------------
say "building portable zip"
PORTABLE="$DIST/VLCFlix-portable-$VERSION.zip"
rm -f "$PORTABLE"
( cd "$DIST" && zip -qr "$(basename "$PORTABLE")" VLCFlix )

say "artifacts"
ls -lh "$DIST"/*.exe "$DIST"/*.zip 2>/dev/null | awk '{print "  " $5 "\t" $9}'
du -sh "$STAGE"
