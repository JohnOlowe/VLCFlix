#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
TESTS="$ROOT/tests"
PKG="$ROOT/packaging"
DIST="$ROOT/dist/win-native"
STAGE="$DIST/payload"
BUILD="$ROOT/.cache/win-native"
OUT_EXE="VLCFlixStudio.exe"

MODE="build"
case "${1:-}" in
  --check|-c) MODE="check" ;;
  "" ) ;;
  *) printf 'unknown argument: %s (try --check)\n' "$1" >&2; exit 2 ;;
esac

die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '\n==> %s\n' "$*"; }

VERSION_FROM_SOURCE=$(sed -n 's/.*kVersion = "\([^"]*\)".*/\1/p' "$ROOT/src/core/brand.h" | head -1)
VERSION="${VERSION:-$VERSION_FROM_SOURCE}"

# ---------------------------------------------------------------- requirements
CXX_BIN=""
for candidate in x86_64-w64-mingw32-g++ x86_64-w64-mingw32-g++-posix; do
  if command -v "$candidate" >/dev/null 2>&1; then CXX_BIN="$candidate"; break; fi
done
WINDRES=""
command -v x86_64-w64-mingw32-windres >/dev/null 2>&1 && WINDRES="x86_64-w64-mingw32-windres"
MAKENSIS=""
command -v makensis >/dev/null 2>&1 && MAKENSIS="makensis"

missing=()
[ -n "$CXX_BIN" ]     || missing+=("mingw-w64 g++ (x86_64-w64-mingw32-g++)")
[ -n "$WINDRES" ]     || missing+=("mingw-w64 windres (embeds the manifest)")
[ -n "$MAKENSIS" ]    || missing+=("NSIS (makensis)")
[ -n "${VLC_DIR:-}" ] || missing+=("VLC_DIR (a Windows VLC install to ship libvlc.dll and plugins/)")

# The Windows build needs Windows builds of the five libraries. pkg-config can
# report them if the cross sysroot is set up; without it there is nothing to link.
for lib in sdl2 freetype2 libpng libjpeg libvlc; do
  x86_64-w64-mingw32-pkg-config --exists "$lib" 2>/dev/null || missing+=("$lib for Windows (no cross pkg-config entry)")
done

if [ "${#missing[@]}" -gt 0 ]; then
  printf 'Cannot build the Windows application on this machine.\n\nMissing:\n'
  for item in "${missing[@]}"; do printf '  - %s\n' "$item"; done
  cat <<'NOTE'

This is not a failure of the source tree. It means the Windows build is
UNVERIFIED here, and docs/TESTING.md lists it that way rather than pretending a
Linux build says anything about Windows.

To build it, either:
  * cross-compile: a mingw-w64 toolchain plus mingw builds of SDL2, FreeType,
    libpng, libjpeg and libVLC on the pkg-config path, NSIS, and VLC_DIR set; or
  * build on Windows with MSYS2 (mingw-w64-x86_64-{gcc,sdl2,freetype,libpng,
    libjpeg-turbo,vlc}) and run this same script from Git Bash.
Then run the manual checklist in docs/TESTING.md section 9 before calling it done.
NOTE
  [ "$MODE" = "check" ] && exit 3
  exit 3
fi

# --------------------------------------------------------------------- build
say "compiling $OUT_EXE (version $VERSION)"
mkdir -p "$BUILD/obj" "$STAGE"
CXXFLAGS_WIN="-std=c++17 -O2 -DNDEBUG -D_WIN32_WINNT=0x0A00 -municode -Wall -Wextra -Wno-unused-parameter"
PKG_CFLAGS=$(x86_64-w64-mingw32-pkg-config --cflags sdl2 freetype2 libpng libjpeg libvlc)
PKG_LIBS=$(x86_64-w64-mingw32-pkg-config --libs sdl2 freetype2 libpng libjpeg libvlc)

objects=()
while IFS= read -r source; do
  object="$BUILD/obj/$(printf '%s' "$source" | sed 's|/|_|g').o"
  objects+=("$object")
  [ -f "$object" ] && [ "$object" -nt "$source" ] && continue
  # shellcheck disable=SC2086
  "$CXX_BIN" $CXXFLAGS_WIN $PKG_CFLAGS -I "$ROOT/src" -c -o "$object" "$source" \
    || die "compiling $(basename "$source") failed"
done < <(find "$SRC/core" "$SRC/library" "$SRC/platform" "$SRC/playback" "$SRC/render" "$SRC/ui" "$SRC" \
           -maxdepth 1 -name '*.cpp' 2>/dev/null; find "$SRC/core" "$SRC/library" "$SRC/platform" \
           "$SRC/playback" "$SRC/render" "$SRC/ui" -name '*.cpp'; find "$SRC" -maxdepth 1 -name 'main.cpp')

# The manifest and the version block live in a resource script, which is how an
# unsigned mingw build gets DPI awareness and a product name in Explorer.
cat > "$BUILD/version.rc" <<RC
1 24 "${PKG}/app.manifest"
1 VERSIONINFO
FILEVERSION 2,0,0,0
PRODUCTVERSION 2,0,0,0
FILEOS 0x40004
FILETYPE 0x1
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName",      "VLCFlix"
      VALUE "FileDescription",  "VLCFlix Studio"
      VALUE "FileVersion",      "${VERSION}"
      VALUE "InternalName",     "VLCFlixStudio"
      VALUE "OriginalFilename", "VLCFlixStudio.exe"
      VALUE "ProductName",      "VLCFlix Studio"
      VALUE "ProductVersion",   "${VERSION}"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
RC
"$WINDRES" -I "$PKG" -O coff -o "$BUILD/version.res.o" "$BUILD/version.rc" || die "windres failed"

say "linking"
# shellcheck disable=SC2086
"$CXX_BIN" -mwindows -o "$STAGE/$OUT_EXE" "${objects[@]}" "$BUILD/version.res.o" $PKG_LIBS \
  -lole32 -luuid -lshell32 -lshlwapi -lversion -ldbghelp -lws2_32 -static-libgcc -static-libstdc++ \
  || die "linking failed"

say "staging the payload"
cp "$ROOT/LICENSE.txt" "$STAGE/LICENSE.txt"
cp "$ROOT/README.md" "$STAGE/README.txt"
# libVLC is shipped, not assumed: the app is useless without it, and picking up an
# arbitrary system libvlc would make the same build behave differently per machine.
cp "$VLC_DIR/libvlc.dll" "$VLC_DIR/libvlccore.dll" "$STAGE/" || die "libvlc.dll not found in VLC_DIR"
rm -rf "$STAGE/plugins"
mkdir -p "$STAGE/plugins"
# Only the modules this app uses; the full plugin folder is ~300 MB.
for module in codec/avcodec plugin/access/ filesystem video_output audio_output \
              packetizer/ demux/mpeg demux/avi demux/mp4 demux/ogg demux/mkv \
              text_renderer/freetype video_filter misc/demux; do
  [ -e "$VLC_DIR/plugins/$module" ] && cp -r "$VLC_DIR/plugins/$module" "$STAGE/plugins/" 2>/dev/null
done

say "building the installer"
mkdir -p "$DIST"
"$MAKENSIS" -V2 -DVERSION="$VERSION" -DSTAGE="$STAGE" -DOUTDIR="$DIST" "$PKG/installer.nsi" \
  || die "makensis failed"

say "packing the portable zip"
command -v zip >/dev/null 2>&1 && (cd "$STAGE" && zip -qr "$DIST/VLCFlixStudio-portable-$VERSION.zip" .) \
  || printf 'zip not installed: portable archive skipped\n'

printf '\nBuilt (unverified - see docs/TESTING.md section 9):\n'
ls -1sh "$DIST"
