# Preview generation

A hover preview can be a short, silent montage generated as part of library
scanning rather than on the UI thread. The notes below are design guidance; confirm
the implementation and measure on the target hardware before relying on timings.

## Keep the work small

- Probe the duration once, and select a few short segments across the film while
  avoiding the opening titles and end credits. Five or six three-second cuts are a
  useful target for a longer film; short films need fewer cuts.
- Seek before the input (`ffmpeg -ss ... -i ...`) where practical, scale to a small
  preview size, cap frame rate and omit audio.
- Keep worker concurrency bounded for the available CPU and memory. On a two-core
  machine, start conservatively with two workers and measure.
- Software decoding is the portable default. Probe an optional hardware encoder
  before using it, and handle incompatible hardware frames explicitly if filters
  require a download to system memory.
- When the clips share a compatible format, concatenate without re-encoding.

## Prioritize without corrupting files

Give the hovered film priority in the pending queue and avoid enqueuing the same
film repeatedly when the pointer moves quickly. Let in-flight work finish rather
than killing an encoder mid-write. Publish complete output atomically and reuse it
until the source file or preview format changes.
