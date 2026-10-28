#!/usr/bin/env bash
# Build the Windows application on Linux, from nothing but network access and a
# C++17 compiler for the host. Verified: this is the recipe that produced the local
# (git-ignored) dist/win-native/VLCFlixStudio-win64.zip, and the executable it builds
# has been RUN under Wine 11.19 - see docs/TESTING.md section 11.
#
#   scripts/build-win-native.sh --check     # report what is missing, change nothing
#   scripts/build-win-native.sh            # download, build, stage, verify
#   scripts/build-win-native.sh --zip      # ... and pack dist/win-native/*.zip
#
# What it does
#   1. fetches llvm-mingw (clang + lld + mingw-w64 headers/libs, no install needed)
#   2. fetches SDL2 (official mingw development package), and libpng / zlib /
#      libjpeg-turbo (MSYS2 mingw64 packages, whose import libraries are GCC-built
#      and link cleanly with lld)
#   3. builds FreeType from source, statically, WITHOUT harfbuzz: the MSYS2 build
#      drags in glib, graphite2 and a GCC runtime DLL just to render text
#   4. fetches VideoLAN's own Windows VLC build and generates an import library from
#      `libvlc.dll` with gendef + dlltool, so the app links against the exact libVLC
#      binary it ships (3.0.24 at the time of writing)
#   5. compiles the same sources the Linux build compiles, links a GUI-subsystem exe
#      with the manifest and version block, stages the payload, and runs
#      scripts/check-win-payload.sh over it
#
# What it does NOT do is run the result. A build is not a test: whether the exe opens,
# scans and plays is checked by scripts/windows-selfcheck.ps1 on Windows. See
# docs/TESTING.md section 10.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
CACHE="${VLCFLIX_WIN_CACHE:-$ROOT/.cache/win-native}"
DIST="$ROOT/dist/win-native"
PAYLOAD="$DIST/payload"
OBJ="$CACHE/obj"

LLVM_MINGW_VER="${LLVM_MINGW_VER:-20261020}"
SDL_VER="${SDL_VER:-2.32.10}"
FREETYPE_VER="${FREETYPE_VER:-2.13.3}"
VLC_VER="${VLC_VER:-3.0.24}"
MSYS_BASE="${MSYS_BASE:-https://repo.msys2.org/mingw/mingw64}"
MSYS_PKGS=(
  mingw-w64-x86_64-zlib-1.3.2-3-any.pkg.tar.zst
  mingw-w64-x86_64-libpng-1.6.59-2-any.pkg.tar.zst
  mingw-w64-x86_64-libjpeg-turbo-3.2.0-1-any.pkg.tar.zst
)

say()  { printf '\n==> %s\n' "$*"; }
die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

MODE="build"
case "${1:-}" in
  --check) MODE="check" ;;
  --zip)   MODE="zip" ;;
  "")      ;;
  *) printf 'unknown argument: %s (try --check)\n' "$1" >&2; exit 2 ;;
esac

# --------------------------------------------------------------- requirements
missing=()
have curl || missing+=("curl")
have tar  || missing+=("tar")
have unzip || missing+=("unzip")
python3 -c 'import zstandard' 2>/dev/null || missing+=("python3 zstandard (pip install zstandard; needed to unpack MSYS2 packages)")

if [ "${#missing[@]}" -gt 0 ]; then
  printf 'Cannot build the Windows application here.\n\nMissing:\n'
  for item in "${missing[@]}"; do printf '  - %s\n' "$item"; done
  printf '\nThis means the Windows build is UNVERIFIED here. docs/TESTING.md says so.\n'
  exit 3
fi
[ "$MODE" = "check" ] && { printf 'Ready to build the Windows application (cache: %s)\n' "$CACHE"; exit 0; }

mkdir -p "$CACHE" "$OBJ" "$DIST" "$PAYLOAD"

fetch() {  # fetch <url> <file>
  local url="$1" out="$2"
  [ -s "$CACHE/$out" ] && { printf '  have %s\n' "$out"; return; }
  printf '  fetching %s\n' "$out"
  curl -sSL --retry 2 -o "$CACHE/$out" "$url" || die "download failed: $url"
}

# ------------------------------------------------------------- 1. toolchain
say "llvm-mingw $LLVM_MINGW_VER"
fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VER/llvm-mingw-$LLVM_MINGW_VER-ucrt-ubuntu-22.04-x86_64.tar.xz" llvm-mingw.tar.xz
TC="$CACHE/llvm-mingw-$LLVM_MINGW_VER-ucrt-ubuntu-22.04-x86_64"
[ -d "$TC" ] || tar -xf "$CACHE/llvm-mingw.tar.xz" -C "$CACHE"
CXX="$TC/bin/x86_64-w64-mingw32-g++"
WINDRES="$TC/bin/x86_64-w64-mingw32-windres"
[ -x "$CXX" ] || die "no cross compiler at $CXX"

# --------------------------------------------------------------- 2. the libs
say "SDL2 $SDL_VER"
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz" SDL2-mingw.tar.gz
SDL="$CACHE/SDL2-$SDL_VER/x86_64-w64-mingw32"
[ -d "$SDL" ] || tar -xzf "$CACHE/SDL2-mingw.tar.gz" -C "$CACHE"

say "zlib, libpng, libjpeg-turbo (MSYS2 builds, import libraries only)"
mkdir -p "$CACHE/msys"
for pkg in "${MSYS_PKGS[@]}"; do
  fetch "$MSYS_BASE/$pkg" "msys/$pkg"
  python3 - "$CACHE/msys/$pkg" "$CACHE/msys/root" <<'PY'
import io, os, sys, tarfile, zstandard
pkg, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
with open(pkg, 'rb') as fh, zstandard.ZstdDecompressor().stream_reader(fh) as r:
    with tarfile.open(fileobj=io.BytesIO(r.read())) as t:
        t.extractall(out, filter='data')
PY
done
MSYS="$CACHE/msys/root/mingw64"

# ------------------------------------------------------------- 3. FreeType
say "FreeType $FREETYPE_VER (static, without harfbuzz)"
fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VER.tar.xz" freetype.tar.xz
FT_SRC="$CACHE/freetype-$FREETYPE_VER"
FT_OUT="$CACHE/ft-install"
if [ ! -f "$FT_OUT/lib/libfreetype.a" ]; then
  [ -d "$FT_SRC" ] || tar -xf "$CACHE/freetype.tar.xz" -C "$CACHE"
  ( cd "$FT_SRC" && ./configure --host=x86_64-w64-mingw32 \
      --without-harfbuzz --without-brotli --without-bzip2 --without-png --with-zlib=no \
      --prefix="$FT_OUT" CC="$TC/bin/x86_64-w64-mingw32-gcc" AR="$TC/bin/x86_64-w64-mingw32-ar" \
      > "$CACHE/ft-configure.log" 2>&1 && make -j"$(nproc)" > "$CACHE/ft-make.log" 2>&1 \
      && make install > "$CACHE/ft-install.log" 2>&1 ) || die "freetype build failed (see $CACHE/ft-*.log)"
fi

# ------------------------------------------------------------------ 4. libVLC
say "libVLC $VLC_VER (VideoLAN's own Windows build)"
fetch "https://get.videolan.org/vlc/$VLC_VER/win64/vlc-$VLC_VER-win64.zip" vlc-win64.zip
VLC="$CACHE/vlc-$VLC_VER"
[ -d "$VLC" ] || unzip -q "$CACHE/vlc-win64.zip" -d "$CACHE"
[ -f "$VLC/libvlc.dll" ] || die "libvlc.dll not found in the VLC zip"
fetch "https://get.videolan.org/vlc/$VLC_VER/vlc-$VLC_VER.tar.xz" vlc-src.tar.xz
if [ ! -d "$CACHE/vlc-headers/vlc" ]; then
  mkdir -p "$CACHE/vlc-headers"
  tar -xf "$CACHE/vlc-src.tar.xz" -C "$CACHE" "vlc-$VLC_VER/include" 2>/dev/null \
    || die "could not extract VLC headers"
  cp -r "$CACHE/vlc-$VLC_VER/include/vlc" "$CACHE/vlc-headers/"
fi
if [ ! -f "$CACHE/sdk/lib/libvlc.a" ]; then
  mkdir -p "$CACHE/sdk/lib"
  # gendef writes <name>.def into the *current* directory and says nothing on stdout,
  # so run it in the cache and pick the file up from there. (A redirection to
  # libvlc.def "works" by producing an empty file, and dlltool then reports
  # "definition file empty" - which is exactly how this went wrong the first time.)
  ( cd "$CACHE" && "$TC/bin/gendef" "$VLC/libvlc.dll" >/dev/null 2>&1 ) \
    || die "gendef failed on libvlc.dll"
  [ -s "$CACHE/libvlc.def" ] || die "gendef produced no definition file"
  "$TC/bin/x86_64-w64-mingw32-dlltool" -d "$CACHE/libvlc.def" -l "$CACHE/sdk/lib/libvlc.a" \
    -D libvlc.dll || die "dlltool failed"
fi

# ------------------------------------------------------------------ 5. build
say "compiling"
INC="-I $SRC -I $CACHE/vlc-headers -I $SDL/include/SDL2 -I $MSYS/include -I $MSYS/include/freetype2 -I $FT_OUT/include/freetype2"
FLAGS="-std=c++17 -O2 -DNDEBUG -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wno-unused-parameter"
objs=()
for f in $(find "$SRC/core" "$SRC/library" "$SRC/platform" "$SRC/playback" "$SRC/render" "$SRC/ui" -name '*.cpp' | sort) "$SRC/main.cpp"; do
  o="$OBJ/$(echo "${f#$SRC/}" | tr '/' '_').o"
  objs+=("$o")
  # shellcheck disable=SC2086
  "$CXX" $FLAGS $INC -c -o "$o" "$f" || die "compiling ${f#$SRC/} failed"
done

say "linking (GUI subsystem: double-click opens the window, not a console)"
cat > "$CACHE/version.rc" <<RC
1 24 "$ROOT/packaging/app.manifest"
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
      VALUE "FileVersion",      "2.0.0"
      VALUE "InternalName",     "VLCFlixStudio"
      VALUE "OriginalFilename", "VLCFlixStudio.exe"
      VALUE "ProductName",      "VLCFlix Studio"
      VALUE "ProductVersion",   "2.0.0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
RC
"$WINDRES" -I "$CACHE" -O coff -o "$OBJ/version.res.o" "$CACHE/version.rc" || die "windres failed"

LIBS="-L$CACHE/sdk/lib -lvlc -L$SDL/lib -lmingw32 -lSDL2main -lSDL2 \
 -L$MSYS/lib -lpng16 -ljpeg -lz $FT_OUT/lib/libfreetype.a \
 -lole32 -luuid -lshell32 -lshlwapi -lversion -ldbghelp -lws2_32 -lwinmm -limm32 \
 -lgdi32 -lcomdlg32 -ladvapi32 -luser32"
# shellcheck disable=SC2086
"$CXX" -o "$PAYLOAD/VLCFlixStudio.exe" "${objs[@]}" "$OBJ/version.res.o" -mwindows $LIBS \
  -static-libgcc -static-libstdc++ || die "linking failed"

say "staging the payload"
cp "$SDL/bin/SDL2.dll" "$MSYS/bin/libpng16-16.dll" "$MSYS/bin/zlib1.dll" "$MSYS/bin/libjpeg-8.dll" "$PAYLOAD/"
cp "$VLC/libvlc.dll" "$VLC/libvlccore.dll" "$PAYLOAD/"
rm -rf "$PAYLOAD/plugins"
mkdir -p "$PAYLOAD/plugins"
cp -r "$VLC/plugins/." "$PAYLOAD/plugins/"
cp "$ROOT/LICENSE.txt" "$PAYLOAD/"
cp "$ROOT/scripts/windows-selfcheck.ps1" "$PAYLOAD/"

say "checking that the payload can load at all"
"$ROOT/scripts/check-win-payload.sh" "$PAYLOAD" | tail -20 || die "the payload is incomplete"

if [ "$MODE" = "zip" ]; then
  say "packing"
  ( cd "$PAYLOAD" && zip -qr "$DIST/VLCFlixStudio-win64.zip" . )
  ls -lsh "$DIST/VLCFlixStudio-win64.zip"
fi

printf '\nBuilt: %s\n' "$PAYLOAD/VLCFlixStudio.exe"
printf 'This build has been RUN under Wine 11.19 (docs/TESTING.md section 11), which is\n'
printf 'not Windows. Run scripts/windows-selfcheck.ps1 on a real machine for the last mile.\n'
