#!/usr/bin/env bash
# Is the Windows payload complete enough to start?
#
# The most common way a Windows build "does not work" is that it cannot even load: a
# DLL is missing, and the user gets a dialog or nothing at all. That failure is fully
# checkable without Windows - walk every DLL the executable and its libraries import,
# and require each one to be either in the payload or part of Windows itself.
#
#   scripts/check-win-payload.sh <payload-dir>
#
# Exit 0 = every imported library is present or is a Windows system library.
# Exit 1 = something is missing, and it is named.
set -uo pipefail

DIR="${1:-}"
[ -n "$DIR" ] || { printf 'usage: %s <payload-dir>\n' "$0" >&2; exit 2; }
[ -d "$DIR" ] || { printf 'not a directory: %s\n' "$DIR" >&2; exit 2; }
command -v objdump >/dev/null || { printf 'objdump is required (binutils)\n' >&2; exit 2; }

# Libraries that ship with Windows 10/11 and must NOT be in the payload: shipping a
# copy of one is how a build ends up broken on the machine it was meant for.
system_dlls="
kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll shlwapi.dll ole32.dll
oleaut32.dll uuid.dll comdlg32.dll comctl32.dll version.dll winmm.dll imm32.dll
ws2_32.dll dbghelp.dll setupapi.dll rpcrt4.dll usp10.dll dwrite.dll
msvcrt.dll ucrtbase.dll ntdll.dll bcrypt.dll crypt32.dll secur32.dll iphlpapi.dll
gdiplus.dll dnsapi.dll mpr.dll wldap32.dll wininet.dll winhttp.dll psapi.dll
oleacc.dll avrt.dll powrprof.dll userenv.dll netapi32.dll wtsapi32.dll
d3d9.dll d3d11.dll dxgi.dll opengl32.dll glu32.dll
api-ms-win-crt-convert-l1-1-0.dll api-ms-win-crt-environment-l1-1-0.dll
api-ms-win-crt-filesystem-l1-1-0.dll api-ms-win-crt-heap-l1-1-0.dll
api-ms-win-crt-locale-l1-1-0.dll api-ms-win-crt-math-l1-1-0.dll
api-ms-win-crt-multibyte-l1-1-0.dll api-ms-win-crt-private-l1-1-0.dll
api-ms-win-crt-process-l1-1-0.dll api-ms-win-crt-runtime-l1-1-0.dll
api-ms-win-crt-stdio-l1-1-0.dll api-ms-win-crt-string-l1-1-0.dll
api-ms-win-crt-time-l1-1-0.dll api-ms-win-crt-utility-l1-1-0.dll
api-ms-win-core-file-l1-1-0.dll api-ms-win-core-synch-l1-1-0.dll
avicap32.dll cfgmgr32.dll msimg32.dll bcryptprimitives.dll d2d1.dll ncrypt.dll
"

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
is_system() {
  local needle dll; needle="$(lower "$1")"
  for dll in $system_dlls; do [ "$dll" = "$needle" ] && return 0; done
  # api-ms-win-* / ext-ms-* are API-set contracts, not real files: Windows 10+ and
  # Wine both resolve them internally. Bundling a copy is what breaks a payload, so
  # they are system by pattern instead of by an endless enumeration. (Found by the
  # ffmpeg.exe/ffprobe.exe binaries once they were actually put in the payload.)
  case "$needle" in
    api-ms-win-*.dll|ext-ms-*.dll) return 0 ;;
  esac
  return 1
}
in_payload() {
  local needle f; needle="$(lower "$1")"
  for f in "$DIR"/*.dll "$DIR"/*.DLL; do
    [ -f "$f" ] || continue
    [ "$(lower "$(basename "$f")")" = "$needle" ] && return 0
  done
  return 1
}

checked=0
missing=0
declare -A seen=()

scan() {
  local file="$1"
  local base; base="$(basename "$file")"
  [ -n "${seen[$base]:-}" ] && return
  seen[$base]=1
  # Only PE files have imports; anything else is skipped rather than guessed at.
  local imports
  imports="$(objdump -p "$file" 2>/dev/null | sed -n 's/.*DLL Name: //p' | sort -u)"
  [ -n "$imports" ] || return
  local dll
  while IFS= read -r dll; do
    [ -n "$dll" ] || continue
    checked=$((checked + 1))
    if in_payload "$dll"; then
      printf '  ok      %-38s (payload)      <- %s\n' "$dll" "$base"
      # Walk it too: a DLL that is present can still need another one.
      for f in "$DIR"/*.dll "$DIR"/*.DLL; do
        [ "$(lower "$(basename "$f")")" = "$(lower "$dll")" ] && scan "$f"
      done
    elif is_system "$dll"; then
      printf '  system  %-38s (Windows)      <- %s\n' "$dll" "$base"
    else
      printf '  MISSING %-38s (nowhere)      <- %s\n' "$dll" "$base"
      missing=$((missing + 1))
    fi
  done <<< "$imports"
}

printf 'payload: %s\n\n' "$DIR"
for exe in "$DIR"/*.exe "$DIR"/*.EXE; do
  [ -f "$exe" ] && scan "$exe"
done

# libVLC is useless without its plugins, and this is the check that turns "the exe
# opens then closes" into a message before it is ever run.
if in_payload libvlc.dll; then
  if [ -d "$DIR/plugins" ] && [ -n "$(ls -A "$DIR/plugins" 2>/dev/null)" ]; then
    printf '  ok      %-38s (%s plugins)\n' "plugins/" "$(ls "$DIR/plugins" | wc -l)"
    # Named files rather than directories: VLC's plugin folders do not carry one
    # file per format, and a silent absence is what makes an app decode nothing.
    for needed in \
        codec/libavcodec_plugin.dll \
        demux/libmp4_plugin.dll demux/libmkv_plugin.dll demux/libavi_plugin.dll \
        packetizer/libpacketizer_h264_plugin.dll \
        video_output/libvmem_plugin.dll video_output/libdirect3d11_plugin.dll \
        audio_output/libmmdevice_plugin.dll audio_output/libdirectsound_plugin.dll \
        access/libfilesystem_plugin.dll; do
      if [ -f "$DIR/plugins/$needed" ]; then
        printf '  ok      %-38s (plugin)\n' "$needed"
      else
        printf '  MISSING %-38s (plugin)\n' "$needed"
        missing=$((missing + 1))
      fi
    done
  else
    printf '  MISSING %-38s (libvlc.dll without plugins decodes nothing)\n' "plugins/"
    missing=$((missing + 1))
  fi
fi

printf '\n%d import(s) checked, %d missing\n' "$checked" "$missing"
[ "$missing" -eq 0 ] || exit 1
