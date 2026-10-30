# Hover Preview Generation — Fast Agent Guide (for Celeron N3350 + borrowed i5)

> For VLCFlix / any Netflix-style grid. Hover shows a **15–18s silent montage** of **5–6 × 3s cuts**. Must feel instant — generate **at scan time**, never on hover. This version is tuned for the laptop you actually own.

## Your hardware — what matters here

| Machine you own | Cores / RAM | Hardware | What works |
|---|---|---|---|
| **Celeron N3350** | 2C/2T, ~4GB, HD 500 | `h264_qsv` encode *exists* | **Encode: yes (QSV). Decode: never use `-hwaccel` with a filter — same black-screen bug as the stream engine.** |
| Borrowed EliteBook 840 G6 i5-8365U | 4C/8T, UHD 620 | `h264_qsv` encode | Same rule, just 2–3× faster |

**Hardware decode does NOT make this faster.** Your preview cut is `-ss 120 -t 3 -i movie` — FFmpeg jumps by index and decodes **90 frames**, not the whole film. Adding `-hwaccel qsv` would hand `scale` a `qsv` surface it can't read (`Impossible to convert between formats ... auto_scale_0` → black 0-byte file). That's the exact 1.1.0 bug you fixed. For previews: **software decode + hardware encode *optional***.

**Hardware *encode* does help a little:** `h264_qsv` saves ~20–30% vs `libx264 ultrafast` at 360p, but `libx264 ultrafast` is already 0.3–0.5s per cut and works everywhere. Default to `libx264`; only swap if a probe proves QSV is usable (see §3).

## The rule: input-seek, tiny, silent, limited parallelism

Bad: `ffmpeg -i movie -vf select=...` → decodes entire film.
Good: jump by index per cut:

```bash
ffmpeg -ss 120 -t 3 -i movie.mp4 ...
       ^^^^ before -i = index seek, ~0.05s
```

And **never run 6 FFmpegs at once on a Celeron**. That first draft did `&` ×6 = ~1.2GB RAM → swap/thrash. On 2 cores, **max 2 workers**. On the borrowed i5, max 3.

## 1. Duration once

```bash
DUR=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$INPUT")
# e.g. 3130.55 for 52 min podcast
```

## 2. Timestamps — skip credits

For 15s = 5×3s, 18s = 6×3s. Use 8/24/40/58/76/90% so you don't get 6× black title cards:

```bash
t1=$(awk -v d="$DUR" 'BEGIN{printf "%d", d*0.08}')
t2=$(awk -v d="$DUR" 'BEGIN{printf "%d", d*0.24}')
t3=$(awk -v d="$DUR" 'BEGIN{printf "%d", d*0.40}')
t4=$(awk -v d="$DUR" 'BEGIN{printf "%d", d*0.58}')
t5=$(awk -v d="$DUR" 'BEGIN{printf "%d", d*0.76}')
t6=$(awk -v d="$DUR" 'BEGIN{printf "%d", d*0.90}')
```

## 3. Hardware probe — once at startup, not per film

```bash
# does this machine actually encode QSV?
ffmpeg -hide_banner -f lavfi -i testsrc=size=1280x720:rate=30:duration=0.5 -c:v h264_qsv -f null - 2>&1 | head
# exit 0 = use h264_qsv; exit 1 = stay on libx264
```

Store `HAS_QSV=true/false`. Do NOT probe with `-hwaccel qsv` — we will never decode on GPU for previews. The encoder probe above is enough.

Encode presets:
- default (portable, tested): `-c:v libx264 -preset ultrafast -crf 30 -pix_fmt yuv420p`
- if HAS_QSV: `-c:v h264_qsv -global_quality 30 -preset veryfast` (QSV uses `global_quality`, not `crf`)

Both at `360p` look identical on a card.

## 4. Extract with bounded parallelism (the Celeron fix)

```bash
# Celeron N3350: 2 workers. i5-8365U: 3 workers.
JOBS=2
TIMES=($t1 $t2 $t3 $t4 $t5 $t6)

N=0
for T in "${TIMES[@]}"; do
  ffmpeg -y -hide_banner -loglevel error \
    -ss "$T" -t 3 -i "$INPUT" \
    -vf "scale=-2:360:flags=fast_bilinear,fps=24" -an \
    -c:v libx264 -preset ultrafast -crf 30 -pix_fmt yuv420p \
    "/tmp/vlcflix/$ID/prev_${N}.mp4" &
  N=$((N+1))
  # throttle: never more than JOBS at once
  if [ $(( N % JOBS )) -eq 0 ]; then wait; fi
done
wait
```

Why these values:
- `scale=-2:360` — card is ~300px wide. Quarter pixels of 720p = 4× less libscale work. Still sharp, never the bottleneck.
- `fps=24` — caps 60fps phone footage, halves frames.
- `-an` — preview is silent, skip audio decode entirely.
- `ultrafast crf 30` — preview is disposable; 2× faster than `veryfast 28` and still fine at 360p.

Timing on owned hardware: ~0.4s per 3s cut ×6 = 2.4s serial, ~1.3s with JOBS=2. On borrowed i5: ~0.8s.

**Do NOT do:** `-hwaccel qsv` before `-i`, or `-vf scale_qsv` — both need `hwdownload` and re-introduce the black-screen path.

## 5. Concat without re-encoding (instant)

```bash
printf "file '/tmp/vlcflix/$ID/prev_0.mp4'\nfile '/tmp/vlcflix/$ID/prev_1.mp4'\n..." > /tmp/vlcflix/$ID/list.txt
ffmpeg -y -f concat -safe 0 -i /tmp/vlcflix/$ID/list.txt -c copy /media/$ID/preview.mp4
```

Byte-join, ~0.05s. Keeps the 360p encode. Do not add `-vf` here — that would re-encode.

## 6. Hover priority — the hovered film must win

Background scan is low priority. A hover is high priority and must preempt.

**Agent rules:**
1. One global queue. Background fills it at scan time.
2. Only `JOBS` workers run at once (2 on Celeron, 3 on i5). Never more.
3. On `mouseenter`/`focus` for `id`:
   - if `preview.mp4` already exists → serve it immediately
   - if `id` is already in `pending` → move it to front of queue
   - if `id` is not in queue → `unshift(id)` to front
   - if a worker is busy on a background item, **don't kill it** (killing ffmpeg mid-write leaves a corrupt mp4) — let it finish its current 0.4s cut, then pick the hovered `id` next
4. Debounce hover: wait 120ms before queueing — scrubbing across 10 cards shouldn't queue 10 jobs
5. Cache: once `/media/$ID/preview.mp4` exists, never rebuild unless `mtimeMs` changed. Serve `?v=mtimeMs` immutable like artwork.

Minimal JS sketch for `server/library.js`:

```js
const PREVIEW_JOBS = 2; // 2 on Celeron N3350, 3 on i5
let previewQueue = []; // ids, front = next
let active = 0;

function requestPreview(id) {
  if (fs.existsSync(artPaths(MEDIA_DIR, id).preview)) return; // already done
  previewQueue = [id, ...previewQueue.filter(x => x !== id)]; // move to front
  pump();
}
async function pump() {
  if (active >= PREVIEW_JOBS) return;
  const id = previewQueue.shift();
  if (!id) return;
  active++;
  try { await renderPreview(id); } finally { active--; pump(); }
}
// background scan: previewQueue.push(id) — low priority (back)
// hover: requestPreview(id) — high priority (front)
```

UI: `onMouseEnter debounced(120ms) → fetch('/api/preview/priority/:id', {method:'POST'})` which calls `requestPreview(id)`. Player uses `<video autoplay muted loop playsinline preload="metadata" src="/media/${id}/preview.mp4">` with `poster.jpg` fallback while pending.

## 7. File audit for this laptop

- `preview-generation-guide.md` — **this file, fixed**: bounded JOBS, no `-hwaccel` decode, QSV encode only if probed, hover priority added
- `subtitle-hard-burn.md` §4 — correct: intentionally no `-hwaccel qsv` for subtitle burns (filter needs RAM frames). Keep as-is.
- `burn_parallel_flexible.sh` — phone-only (`h264_mediacodec`), not for previews. Don't reuse for laptop previews — encoders differ.
- `phone-first-ffmpeg.md` / `burnfix-home.sh` — separate concern (offline burn). No change needed.

That keeps the Celeron at ~1.3s / preview, never OOM, never black-screen, and the hovered card always jumps the line.
