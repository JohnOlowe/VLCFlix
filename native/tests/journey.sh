#!/usr/bin/env bash
# The end-to-end journey: one continuous user story, driven through the real binary.
#
# Every other layer tests a module. This one tests that the modules agree with each
# other, which is the only thing a unit test cannot prove. It builds a synthetic
# library with ffmpeg, so it does not depend on the bundled samples and it proves the
# app works on files it has never seen.
#
# Usage: tests/journey.sh [--keep] [--binary <path>]
#   --keep            leave the temporary directory in place and print its path
#   --binary <path>   test a binary other than build/vlcflix (CTest passes this)
#
# Exit code 0 means every step passed; any failure prints the failing step, the command
# and the output, and exits non-zero.
set -u -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="$ROOT/build/vlcflix"
KEEP=0
# Arguments: --keep leaves the work directory behind, --binary points at a build
# made somewhere other than build/ (CTest does this).
while [ $# -gt 0 ]; do
  case "$1" in
    --keep) KEEP=1 ;;
    --binary) shift; BIN="${1:-}" ;;
    --binary=*) BIN="${1#--binary=}" ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
  shift
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/vlcflix-journey.XXXXXX")"
DATA="$WORK/data"
LIB="$WORK/library"
mkdir -p "$DATA" "$LIB"

step_no=0
step() { step_no=$((step_no + 1)); printf '\n[%02d] %s\n' "$step_no" "$1"; }
fail() {
  printf '\nFAILED at step %02d: %s\n' "$step_no" "$1"
  KEEP=1            # a failure is exactly when the evidence has to survive
  exit 1
}
pass() { printf '     ok: %s\n' "$1"; }

# index_of <title prefix> <status.json> -> prints the catalog index
# The journey never assumes an order: it asks the catalog where a film is, which also
# checks that the index the CLI accepts is the index the catalog reports.
index_of() {
  python3 - "$1" "$2" <<'IDX'
import json, sys
prefix, path = sys.argv[1], sys.argv[2]
for n, item in enumerate(json.load(open(path))["library"]):
    if item["title"].startswith(prefix):
        print(n)
        break
IDX
}

cleanup() {
  if [ "$KEEP" = "0" ]; then rm -rf "$WORK"; else printf '\nwork directory: %s\n' "$WORK"; fi
}
trap cleanup EXIT

# show <file> [lines] - dump a captured output without libVLC's ALSA noise, which
# this machine prints once per audio attempt and which never means anything is wrong.
show() {
  sed -n "1,${2:-20}p" "$1" | grep -v "ALSA lib" | grep -v "^\s*$" | sed 's/^/       /'
}

# expect_contains <file> <needle> <what>
expect_contains() {
  if grep -qF -- "$2" "$1"; then pass "$3"; else
    printf '     woff: expected "%s" in:\n' "$2"
    show "$1" 40
    fail "$3"
  fi
}

# ---------------------------------------------------------------- preconditions
step "preconditions"
command -v ffmpeg >/dev/null || fail "ffmpeg is required to build the synthetic library"
[ -x "$BIN" ] || fail "$BIN is missing; run 'make' first"
printf '     ffmpeg: %s\n' "$(command -v ffmpeg)"
printf '     binary: %s\n' "$BIN"

# ------------------------------------------------------------ a library that has
# never existed before: different names, different lengths, one 10-bit file, one
# sidecar with metadata, one broken file, one subtitle file.
step "make a synthetic library with ffmpeg"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=640x360:rate=24:duration=8" \
  -f lavfi -i "sine=frequency=440:duration=8" -c:v libx264 -pix_fmt yuv420p -c:a aac \
  -shortest "$LIB/The.Journey.Of.A.Film.2021.1080p.WEB-DL.mkv" || fail "first clip"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "smptebars=size=640x360:rate=24:duration=6" \
  -c:v libx264 -pix_fmt yuv420p "$LIB/Second Feature (2019).mp4" || fail "second clip"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "mandelbrot=size=640x360:rate=24" -t 5 \
  -c:v libx264 -pix_fmt yuv420p10le "$LIB/Deep.Colour.2023.mkv" || fail "10-bit clip"
printf 'not a video at all\n' > "$LIB/Broken.2020.mkv"
cat > "$LIB/The.Journey.Of.A.Film.2021.1080p.WEB-DL.json" <<'JSON'
{"title":"The Journey of a Film","year":2021,"genres":["Drama","Adventure"],"rating":"TV-14",
 "synopsis":"A synthetic clip walks the whole application, from scan to resume."}
JSON
cat > "$LIB/The.Journey.Of.A.Film.2021.1080p.WEB-DL.en.srt" <<'SRT'
1
00:00:00,500 --> 00:00:03,000
The journey starts here.

2
00:00:03,500 --> 00:00:06,000
And the scanner found this subtitle.
SRT
pass "4 videos, 1 broken file, 1 sidecar, 1 subtitle file"
ls -1 "$LIB" | sed 's/^/       /'

# ------------------------------------------------------------------- scan it
step "scan the library (artwork included)"
"$BIN" scan --folders "$LIB" --data "$DATA" > "$WORK/scan.txt" 2>&1 || {
  show "$WORK/scan.txt" 40; fail "scan exited non-zero"; }
expect_contains "$WORK/scan.txt" "unreadable video: Broken.2020.mkv" "the broken file is reported with its name"
# Anchored on the summary line: a bare "N item" also matches the unreadable-file note.
ITEMS=$(grep -oE '^[0-9]+ item\(s\), [0-9]+ error' "$WORK/scan.txt" | head -1 | cut -d' ' -f1)
[ "${ITEMS:-0}" = "4" ] || { show "$WORK/scan.txt" 40; fail "expected 4 items, got ${ITEMS:-none}"; }
pass "4 items in the catalog"
grep -q "1 error" "$WORK/scan.txt" && pass "the scan counts the one unreadable file" || fail "expected 1 error line"

step "the sidecar metadata won over the filename"
# Only stdout is captured here: --json writes the document to stdout and the banner
# to stderr, and the parser below proves that separation holds.
"$BIN" status --data "$DATA" --json > "$WORK/status.json" 2> "$WORK/status.err" || fail "status"
head -c1 "$WORK/status.json" | grep -q "{" || fail "status --json put something before the JSON on stdout"
python3 - "$WORK/status.json" <<'PY' || fail "sidecar metadata"
import json, sys
data = json.load(open(sys.argv[1]))
items = {i["title"]: i for i in data.get("library", [])}
assert "The Journey of a Film" in items, items.keys()
item = items["The Journey of a Film"]
assert item["year"] == 2021, item
assert "Drama" in (item.get("genres") or []), item
assert item.get("poster"), "no poster was drawn"
assert item.get("subtitles"), "the sidecar subtitle was not found"
assert item["subtitles"][0]["lang"] == "en", item["subtitles"]
print("     ok: title, year, genres, poster and subtitles all came from the files")
PY

step "the 10-bit file is recognised as 10-bit (audit A5)"
python3 - "$WORK/status.json" <<'PY' || fail "10-bit detection"
import json, sys
items = {i["title"]: i for i in json.load(open(sys.argv[1]))["library"]}
deep = items.get("Deep Colour")
assert deep, items.keys()
assert "10" in (deep.get("pixFmt") or ""), deep.get("pixFmt")
other = items.get("Second Feature")
assert "10" not in (other.get("pixFmt") or ""), other.get("pixFmt")
print("     ok: yuv420p10le is 10-bit, yuv420p is not")
PY

# --------------------------------------------------- browse it (no display)
step "every screen renders from this library, with no display"
"$BIN" render --all "$WORK/shots" --size 1280x720 --data "$DATA" > "$WORK/render.txt" 2>&1 \
  || { show "$WORK/render.txt"; fail "render"; }
for screen in home browse mylist search detail settings; do
  [ -s "$WORK/shots/$screen.png" ] || fail "no screenshot for $screen"
done
pass "6 screens rendered"
python3 - "$WORK/shots" <<'PY' || fail "screens are not empty"
import sys, zlib, struct
# Count distinct byte values as a crude content check without a decoder: an all-one-colour
# PNG compresses to almost nothing, a drawn screen does not.
import os
for name in ("home", "browse", "settings"):
    size = os.path.getsize(os.path.join(sys.argv[1], name + ".png"))
    assert size > 8000, (name, size)
print("     ok: the screens have content")
PY

# ---------------------------------------------------------- play it for real
step "play a film: seek, subtitles, sync and progress"
JOURNEY_IDX=$(index_of "The Journey" "$WORK/status.json")
[ -n "$JOURNEY_IDX" ] || fail "the film is not in the catalog"
"$BIN" play "$JOURNEY_IDX" --seek 3 --subtitles --wait 3 --data "$DATA" \
  --log "$WORK/play.log" > "$WORK/play.txt" 2>&1 \
  || { show "$WORK/play.txt"; fail "playback"; }
show "$WORK/play.txt"
grep -q "decoded=[1-9]" "$WORK/play.txt" || fail "no frames were decoded"
grep -q "state=playing\|state=ended\|state=paused" "$WORK/play.txt" || fail "unexpected state"
grep -q "length=8" "$WORK/play.txt" || fail "the length did not come from the engine"
grep -q "subtitle: " "$WORK/play.txt" && pass "the sidecar subtitle was on screen" || fail "no subtitle cue"
grep -q "state=" "$WORK/play.txt" && pass "the engine reported a state"

step "progress was saved by playing"
python3 - "$DATA/progress.json" <<'PY' || fail "progress"
import json, sys
data = json.load(open(sys.argv[1]))
assert data, "progress.json is empty"
entry = list(data.values())[0]
assert entry["position"] > 2.0, entry
print("     ok: position %.1f s saved" % entry["position"])
PY

step "the same film resumes where it stopped (relaunch the binary)"
BEFORE=$(python3 -c "import json;d=json.load(open('$DATA/progress.json'));print(max(v['position'] for v in d.values()))")
"$BIN" play "$JOURNEY_IDX" --wait 2 --data "$DATA" --log "$WORK/resume.log" > "$WORK/resume.txt" 2>&1 || fail "resume run"
TIME=$(grep -oE 'time=[0-9.]+' "$WORK/resume.txt" | head -1 | cut -d= -f2)
python3 - "$BEFORE" "$TIME" <<'PY' || fail "resume"
import sys
before, now = float(sys.argv[1]), float(sys.argv[2])
assert now >= before - 0.5, f"resumed at {now}, should be at least {before - 0.5}"
print(f"     ok: resumed at {now:.2f} s (saved {before:.2f} s)")
PY

step "a broken file is refused with a reason, not a black window"
BROKEN_INDEX=$(index_of "Broken" "$WORK/status.json")
"$BIN" play "$BROKEN_INDEX" --wait 2 --data "$DATA" > "$WORK/broken.txt" 2>&1
if grep -qi "could not be read\|ended without producing\|no picture" "$WORK/broken.txt"; then
  pass "the broken file was refused with an explanation"
else
  show "$WORK/broken.txt"
  fail "the broken file produced no explanation"
fi

# ------------------------------------------------------------------- rescan
step "a second scan changes nothing (incremental, no redraw)"
START=$(date +%s%N)
"$BIN" scan --folders "$LIB" --data "$DATA" > "$WORK/rescan.txt" 2>&1 || fail "rescan"
END=$(date +%s%N)
MS=$(( (END - START) / 1000000 ))
grep -qE "^0 of [0-9]+ artwork set\(s\) drawn" "$WORK/rescan.txt" && pass "nothing was redrawn" || {
  show "$WORK/rescan.txt"; fail "the rescan redrew artwork"; }
printf '     ok: the rescan took %d ms\n' "$MS"
[ "$MS" -lt 5000 ] || fail "the rescan took ${MS} ms, which is not incremental"

# ------------------------------------------------- crashing still logs
# The requirement is blunt: a log must exist even if the app dies before the window
# opens. These two steps are the only way to hold it to that, because the failure
# being prevented is exactly "no log to ask for".
step "a crash leaves a log with a stack trace behind"
CRASHLOG="$WORK/crash/crash.log"
mkdir -p "$WORK/crash"
set +e
"$BIN" crash --log "$CRASHLOG" > "$WORK/crash.out" 2>&1
CRASH_RC=$?
set -e
[ "$CRASH_RC" -eq 139 ] || [ "$CRASH_RC" -eq 134 ] || fail "expected a fatal signal (139/134), got $CRASH_RC"
pass "the process really died (exit $CRASH_RC)"
[ -s "$CRASHLOG" ] || fail "a crash wrote no log at all"
grep -qi "CRASH" "$CRASHLOG" || { sed -n '1,20p' "$CRASHLOG" | sed 's/^/       /'; fail "the log has no CRASH record"; }
pass "the log has a CRASH record"
grep -qiE "signal|stack|#[0-9]+ " "$CRASHLOG" && pass "the crash record carries a location" \
  || fail "the crash record says nothing about where it happened"

step "the next run says the previous one ended unexpectedly"
# Two copies, because reading the note is a once-only event: the run that reports the
# crash also closes its own session, so the second run against the same file is clean.
cp "$CRASHLOG" "$WORK/after-crash-human.log"
cp "$CRASHLOG" "$WORK/after-crash-json.log"
"$BIN" status --log "$WORK/after-crash-human.log" 2>/dev/null | grep -q "ended unexpectedly" \
  && pass "the human-readable status says the previous run ended unexpectedly" \
  || fail "the note is missing from the human output"
"$BIN" status --json --log "$WORK/after-crash-json.log" > "$WORK/after-crash.json" 2>/dev/null \
  || fail "status after a crash"
grep -q '"previousRunCrashed": true' "$WORK/after-crash.json" \
  && pass "status --json reports the crash too" || { show "$WORK/after-crash.json"; fail "the crash was not reported"; }

step "a window that cannot open still writes a log"
NOLOG="$WORK/nodisplay/none.log"
mkdir -p "$WORK/nodisplay"
set +e
SDL_VIDEODRIVER=vlcflix-no-such-driver "$BIN" ui --frames 1 --log "$NOLOG" --data "$DATA" > "$WORK/nodisplay.out" 2>&1
UI_RC=$?
set -e
[ "$UI_RC" -eq 5 ] || { sed -n '1,20p' "$WORK/nodisplay.out" | sed 's/^/       /'; fail "expected exit 5 (no window), got $UI_RC"; }
pass "the window refused to open with exit 5"
[ -s "$NOLOG" ] || fail "no log was written when the window could not open"
grep -qi "window could not be opened" "$NOLOG" && pass "the log says why" \
  || fail "the log does not say the window could not be opened"
grep -qi "clean exit" "$NOLOG" && pass "the failed run still closed its session record" \
  || fail "the session record was left open after a clean failure"

# ------------------------------------------------------------------ the log
step "the log records the whole journey and ends with a clean exit"
LOG="$WORK/play.log"
[ -s "$LOG" ] || fail "the play run left no log"
grep -q "\[run\] start" "$LOG" || fail "the log has no run-start record"
grep -q "engine: opened" "$LOG" || fail "the engine did not log opening the film"
grep -q "clean exit" "$LOG" || fail "the run did not record a clean exit"
# The successful run must NOT be mistaken for a crash: that would make the warning
# meaningless the first time it matters.
grep -q "CRASH" "$LOG" && fail "a clean run wrote a CRASH record" || pass "no crash was recorded"
pass "the log records start, the film, and a clean exit"
grep -q "clean exit" "$WORK/resume.log" || fail "the resume run did not record a clean exit"
pass "the resume run logged its own clean exit"

printf '\nall %d journey steps passed\n' "$step_no"
exit 0
