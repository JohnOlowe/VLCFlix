'use strict';

/**
 * Library scanning and artwork.
 *
 * Walks the folders you point it at, reads what it can about each video, and
 * produces the three images the Netflix-style UI needs:
 *   poster   (2:3)  - the card face
 *   backdrop (16:9) - the hero billboard
 *   preview  (8s)   - the muted clip that plays when a card is hovered
 *
 * Artwork is produced with ffmpeg when it is installed. When it is not, the
 * UI falls back to a deterministic gradient derived from the title, so the app
 * still works on a bare machine.
 */

const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');

const run = promisify(execFile);

const VIDEO_EXT = new Set([
  '.mp4', '.mkv', '.avi', '.mov', '.m4v', '.webm', '.wmv', '.flv',
  '.mpg', '.mpeg', '.ts', '.m2ts', '.ogv', '.3gp', '.divx', '.vob',
]);

const MIME = {
  '.mp4': 'video/mp4', '.m4v': 'video/mp4', '.webm': 'video/webm',
  '.ogv': 'video/ogg', '.mov': 'video/quicktime', '.mkv': 'video/x-matroska',
  '.avi': 'video/x-msvideo', '.wmv': 'video/x-ms-wmv', '.flv': 'video/x-flv',
  '.ts': 'video/mp2t', '.m2ts': 'video/mp2t', '.mpg': 'video/mpeg',
  '.mpeg': 'video/mpeg', '.3gp': 'video/3gpp', '.vob': 'video/dvd', '.divx': 'video/divx',
};

const TAG_NOISE = new Set([
  '1080p', '2160p', '720p', '480p', '4k', 'uhd', 'hdr', 'hdr10', 'dv', 'sdr',
  'bluray', 'blu-ray', 'brrip', 'bdrip', 'webrip', 'web-dl', 'webdl', 'web',
  'hdtv', 'dvdrip', 'dvdscr', 'hdrip', 'remux', 'x264', 'x265', 'h264', 'h265',
  'hevc', 'avc', 'aac', 'ac3', 'dts', 'ddp', 'dd5', 'truehd', 'atmos',
  '10bit', '8bit', 'proper', 'repack', 'extended', 'unrated', 'remastered',
  'multi', 'dual', 'audio', 'subs', 'subbed', 'dubbed', 'internal',
]);

const SMALL_WORDS = new Set(['a', 'an', 'and', 'as', 'at', 'but', 'by', 'for', 'in', 'of', 'on', 'or', 'the', 'to', 'vs', 'with']);

const sha1 = (s) => crypto.createHash('sha1').update(s).digest('hex').slice(0, 12);

// Applied to every extracted still. Real libraries contain flat, overexposed
// frames; a touch of contrast and a vignette keeps white text legible on top.
const GRADE = 'eq=contrast=1.1:saturation=1.22,vignette=angle=PI/5.2';

function titleCase(words) {
  return words
    .map((w, i) => {
      if (!w) return w;
      const lower = w.toLowerCase();
      // Preserve words that are deliberately shouty or contain digits.
      if (/[0-9]/.test(w) || (w === w.toUpperCase() && w.length > 1)) return w;
      if (i > 0 && SMALL_WORDS.has(lower)) return lower;
      return lower.charAt(0).toUpperCase() + lower.slice(1);
    })
    .join(' ');
}

/** "The.Matrix.1999.1080p.BluRay.x264.mp4" -> { title: "The Matrix", year: 1999 } */
function parseName(filename) {
  const base = filename.replace(/\.[^.]+$/, '');
  const spaced = base.replace(/[._]+/g, ' ').replace(/\s+/g, ' ').trim();

  let year = null;
  const yearMatch = spaced.match(/\b(19\d{2}|20\d{2})\b/);
  if (yearMatch) year = Number(yearMatch[1]);

  const kept = [];
  for (const rawWord of spaced.split(' ')) {
    const word = rawWord.replace(/^[\[(]+|[)\]]+$/g, '');
    const key = word.toLowerCase().replace(/[^a-z0-9]/g, '');
    if (!key) continue;
    if (yearMatch && word === yearMatch[1]) break;
    if (TAG_NOISE.has(key)) break; // everything after the first release tag is noise
    kept.push(word);
  }

  const title = titleCase(kept.length ? kept : spaced.split(' ').slice(0, 6));
  return { title: title.trim() || base, year };
}

async function hasBinary(name) {
  try {
    await run(name, ['-version'], { timeout: 5000 });
    return true;
  } catch {
    return false;
  }
}

async function probeTools() {
  const ffprobe = await hasBinary('ffprobe');
  const ffmpeg = await hasBinary('ffmpeg');
  return { ffmpeg, ffprobe, canRender: ffmpeg };
}

async function probeVideo(file) {
  const empty = { duration: 0, width: 0, height: 0, vcodec: null, acodec: null, pixFmt: null };
  try {
    const { stdout } = await run(
      'ffprobe',
      ['-v', 'error', '-show_entries',
       'format=duration:stream=codec_type,codec_name,width,height,pix_fmt',
       '-of', 'json', file],
      { timeout: 20000, maxBuffer: 1 << 20 }
    );
    const data = JSON.parse(stdout || '{}');
    const streams = data.streams || [];
    const video = streams.find((st) => st.codec_type === 'video') || {};
    const audio = streams.find((st) => st.codec_type === 'audio') || {};
    return {
      duration: Number.parseFloat(data.format?.duration) || 0,
      width: Number(video.width) || 0,
      height: Number(video.height) || 0,
      vcodec: video.codec_name || null,
      acodec: audio.codec_name || null,
      pixFmt: video.pix_fmt || null,
    };
  } catch {
    return empty;
  }
}

/** Bounded parallelism: N workers pulling from one queue, order preserved. */
async function runPool(items, limit, worker) {
  const results = new Array(items.length);
  let next = 0;
  const runners = Array.from({ length: Math.max(1, Math.min(limit, items.length)) }, async () => {
    for (;;) {
      const index = next;
      next += 1;
      if (index >= items.length) return;
      results[index] = await worker(items[index], index);
    }
  });
  await Promise.all(runners);
  return results;
}

/**
 * Mean luma (0-255) of an image or frame, via ffmpeg's signalstats.
 * Returns null when it cannot be measured.
 */
async function measureLuma(file) {
  try {
    const { stdout, stderr } = await run('ffmpeg', [
      '-hide_banner', '-nostats', '-i', file, '-frames:v', '1',
      '-vf', 'signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-',
      '-f', 'null', '-',
    ], { timeout: 20000, maxBuffer: 1 << 20 });
    const text = (stdout || '') + (stderr || '');
    const match = /lavfi\.signalstats\.YAVG=([\d.]+)/.exec(text);
    return match ? Number.parseFloat(match[1]) : null;
  } catch {
    return null;
  }
}

/**
 * Dark openings and washed-out masters are the norm, not the exception.
 * Correct with gamma rather than brightness: gamma lifts the midtones that
 * dominate a frame while leaving the black point at black, so the image gets
 * readable without turning to fog. A small contrast bump restores punch.
 */
async function correctExposure(file, target = 70) {
  const luma = await measureLuma(file);
  if (luma == null || luma <= 0) return luma;

  const ratio = target / Math.max(luma, 1);
  if (ratio > 0.86 && ratio < 1.16) return luma; // close enough

  const gamma = Math.max(0.62, Math.min(1.95, Math.pow(ratio, 0.62)));
  const contrast = ratio > 1 ? 1.07 : 1.03;

  try {
    await run('ffmpeg', [
      '-y', '-hide_banner', '-loglevel', 'error', '-i', file,
      '-vf', `eq=gamma=${gamma.toFixed(3)}:contrast=${contrast}:saturation=1.04`,
      '-q:v', '3', file,
    ], { timeout: 30000 });
  } catch {
    /* keep the uncorrected still */
  }
  return luma;
}

/**
 * Bump when the preview format changes, so existing libraries re-render their
 * clips instead of keeping the old style forever (artwork is cached by
 * size+mtime, and a format change is invisible to that check).
 */
const PREVIEW_VERSION = 2;

/**
 * Where the preview snippets are taken from: spread across the film, never past
 * its end, never so short that a frame or two is all you get.
 */
function previewSegments(duration, { count = 5, length = 2 } = {}) {
  if (!(duration > 0)) return [];
  const len = Math.min(length, Math.max(0.6, duration / count));
  const fractions = [0.12, 0.32, 0.52, 0.72, 0.9].slice(0, count);
  const starts = [];
  for (const fraction of fractions) {
    const start = Math.max(0, Math.min(duration * fraction, duration - len));
    // A very short film cannot support distinct snippets; do not repeat frames.
    if (starts.some((existing) => Math.abs(existing - start) < 0.15)) continue;
    starts.push(start);
  }
  return starts.map((start) => ({ start, length: len }));
}

function artPaths(outDir, id) {
  return {
    poster: path.join(outDir, `${id}-poster.jpg`),
    backdrop: path.join(outDir, `${id}-backdrop.jpg`),
    preview: path.join(outDir, `${id}-preview.mp4`),
  };
}

const fileHasContent = (file) => {
  try { return fs.statSync(file).size > 0; } catch { return false; }
};

/**
 * What is actually missing on disk, which is not the same as what the catalogue
 * claims: a file can be deleted, or never finished because the app was closed
 * mid-render. Trusting the catalogue is what makes artwork start over.
 */
function missingArt(item, mediaDir) {
  const files = artPaths(mediaDir, item.id);
  return {
    poster: !fileHasContent(files.poster),
    backdrop: !fileHasContent(files.backdrop),
    preview: item.previewVersion !== PREVIEW_VERSION || !fileHasContent(files.preview),
  };
}

const needsArtwork = (item, mediaDir) => Object.values(missingArt(item, mediaDir)).some(Boolean);

/** Give up on a file that will not render; a manual scan asks for it again. */
const ART_MAX_ATTEMPTS = 3;
const shouldRetryArt = (item) => (item.artAttempts || 0) < ART_MAX_ATTEMPTS;

/** Half-written files from a pass that was killed: never shown, never reused. */
function sweepTempArt(outDir) {
  let removed = 0;
  try {
    for (const name of fs.readdirSync(outDir)) {
      if (!/\.tmp[0-9a-z-]+\.(jpg|mp4)$/i.test(name)) continue;
      const full = path.join(outDir, name);
      try {
        if (Date.now() - fs.statSync(full).mtimeMs < 5 * 60 * 1000) continue;
        fs.unlinkSync(full);
        removed += 1;
      } catch { /* another pass may own it */ }
    }
  } catch { /* no media dir yet */ }
  return removed;
}

async function renderArt(file, duration, outDir, id,
                        needs = { poster: true, backdrop: true, preview: true }) {
  const at = (fraction) => Math.max(0, Math.min(duration * fraction, Math.max(0, duration - 1)));
  const final = artPaths(outDir, id);
  // Render to a temp name and rename when complete, so an interrupted pass can
  // never leave a truncated image or clip that looks finished.
  const tag = `.tmp${process.pid}-${crypto.randomBytes(3).toString('hex')}`;
  const outputs = {
    poster: final.poster.replace(/(\.jpg)$/i, `${tag}$1`),
    backdrop: final.backdrop.replace(/(\.jpg)$/i, `${tag}$1`),
    preview: final.preview.replace(/(\.mp4)$/i, `${tag}$1`),
  };

  const jobs = [];
  const keys = [];

  if (needs.poster) {
    keys.push('poster');
    jobs.push(run('ffmpeg', ['-y', '-loglevel', 'error', '-ss', String(at(0.38)), '-i', file,
      '-frames:v', '1', '-vf', 'scale=400:600:force_original_aspect_ratio=increase,crop=400:600,' + GRADE,
      '-q:v', '4', outputs.poster], { timeout: 60000 }));
  }
  if (needs.backdrop) {
    keys.push('backdrop');
    jobs.push(run('ffmpeg', ['-y', '-loglevel', 'error', '-ss', String(at(0.22)), '-i', file,
      '-frames:v', '1', '-vf', 'scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720,' + GRADE,
      '-q:v', '3', outputs.backdrop], { timeout: 60000 }));
  }
  if (needs.preview) {
    const segments = previewSegments(duration);
    if (segments.length) {
      // One ffmpeg pass: seek to each snippet, normalise it, then concat. A single
      // input would only ever show one moment of the film.
      const inputs = segments.flatMap((seg) => ['-ss', String(seg.start.toFixed(3)),
                                                '-t', String(seg.length.toFixed(3)), '-i', file]);
      const normalise = segments
        .map((_, i) => `[${i}:v]scale=640:-2,setsar=1,fps=24[v${i}]`)
        .join(';');
      const concat = segments.map((_, i) => `[v${i}]`).join('') + `concat=n=${segments.length}:v=1:a=0[out]`;
      keys.push('preview');
      jobs.push(run('ffmpeg', ['-y', '-loglevel', 'error', ...inputs,
        '-filter_complex', `${normalise};${concat}`, '-map', '[out]', '-an',
        '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '30', '-pix_fmt', 'yuv420p',
        '-movflags', '+faststart', outputs.preview], { timeout: 180000 }));
    }
  }

  const settled = await Promise.allSettled(jobs);
  const produced = {};
  const failures = [];
  settled.forEach((result, i) => {
    const key = keys[i];
    if (result.status === 'fulfilled' && fileHasContent(outputs[key])) produced[key] = true;
    else if (result.status === 'rejected') {
      failures.push(key);
      console.warn(`[library] ${key} failed for ${path.basename(file)}: ${result.reason?.message?.split('\n')[0]}`);
    }
  });

  // Fix stills that came out too dark or too flat for white text to sit on.
  await Promise.all(
    ['poster', 'backdrop']
      .filter((key) => produced[key])
      .map((key) => correctExposure(outputs[key], key === 'poster' ? 64 : 72))
  );

  const done = {};
  for (const key of Object.keys(produced)) {
    try {
      fs.renameSync(outputs[key], final[key]);
      done[key] = path.basename(final[key]);
    } catch (err) {
      failures.push(key);
      console.warn(`[library] could not publish ${key} for ${path.basename(file)}: ${err.message}`);
    }
  }
  for (const key of Object.keys(outputs)) {
    if (done[key]) continue;
    try { fs.unlinkSync(outputs[key]); } catch { /* nothing to clean */ }
  }
  return { produced: done, failures };
}

const SUB_EXT = new Set(['.srt', '.vtt']);

const LANG_NAMES = {
  en: 'English', eng: 'English', fr: 'French', fre: 'French', fra: 'French',
  es: 'Spanish', spa: 'Spanish', de: 'German', ger: 'German', deu: 'German',
  it: 'Italian', ita: 'Italian', pt: 'Portuguese', por: 'Portuguese',
  ru: 'Russian', rus: 'Russian', ar: 'Arabic', ara: 'Arabic',
  zh: 'Chinese', chi: 'Chinese', zho: 'Chinese', ja: 'Japanese', jpn: 'Japanese',
  ko: 'Korean', kor: 'Korean', hi: 'Hindi', hin: 'Hindi', nl: 'Dutch', pl: 'Polish',
  tr: 'Turkish', sv: 'Swedish', no: 'Norwegian', da: 'Danish', fi: 'Finnish',
  yo: 'Yoruba', yor: 'Yoruba', ig: 'Igbo', ibo: 'Igbo', ha: 'Hausa', hau: 'Hausa',
};

/**
 * Sidecar subtitles sitting next to the video: Movie.srt, Movie.en.srt,
 * Movie.forced.srt. This is the naming every player expects, so VLC finds them
 * on its own and the built-in player gets them from the same convention.
 */
function findSubtitles(file, entries) {
  const base = path.basename(file, path.extname(file));
  const tracks = [];

  for (const name of entries) {
    const ext = path.extname(name).toLowerCase();
    if (!SUB_EXT.has(ext)) continue;
    const stem = path.basename(name, path.extname(name));
    if (stem !== base && !stem.startsWith(base + '.')) continue;

    const suffix = stem === base ? '' : stem.slice(base.length + 1);
    const key = suffix.toLowerCase();
    const lang = /^[a-z]{2,3}$/.test(key) || /^[a-z]{2,3}[-_][a-z]{2,4}$/i.test(key)
      ? key.split(/[-_]/)[0]
      : null;
    const label = !suffix
      ? 'Default'
      : LANG_NAMES[key] || LANG_NAMES[lang] || suffix.replace(/[-_]/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());

    tracks.push({ name, label, lang, ext: ext.slice(1) });
  }

  // Plain Movie.srt is what people expect to be on by default, so it sorts first.
  tracks.sort((a, b) => (a.label === 'Default' ? -1 : b.label === 'Default' ? 1 : a.label.localeCompare(b.label)));
  return tracks;
}

/** The label shown for a subtitle file: language suffix, else the filename. */
function labelFor(base, name) {
  const stem = path.basename(name, path.extname(name));
  if (stem === base) return 'Default';
  const suffix = stem.startsWith(base + '.') ? stem.slice(base.length + 1) : stem;
  const key = suffix.toLowerCase();
  const lang = /^[a-z]{2,3}([-_][a-z]{2,4})?$/i.test(key) ? key.split(/[-_]/)[0] : null;
  // Not a language tag and not a name match: show the file's own stem, since
  // mangling "Ghostlight.2019.1080p.WEB-DL" into words helps nobody.
  return LANG_NAMES[key] || LANG_NAMES[lang] || suffix;
}

/**
 * Every subtitle file sitting beside the film.
 *
 * Name matching is the convention, but downloaded subtitles are frequently named
 * after the release rather than the video, so the picker offers everything in the
 * folder instead of pretending only exact matches exist.
 */
function listFolderSubtitles(file, entries) {
  const base = path.basename(file, path.extname(file));
  const found = [];
  for (const name of entries) {
    const ext = path.extname(name).toLowerCase();
    if (!SUB_EXT.has(ext)) continue;
    const stem = path.basename(name, path.extname(name));
    found.push({
      name,
      ext: ext.slice(1),
      label: labelFor(base, name),
      matches: stem === base || stem.startsWith(base + '.'),
    });
  }
  // Name matches first, then everything else alphabetically.
  found.sort((a, b) => (b.matches - a.matches) || a.name.localeCompare(b.name));
  return found;
}

/** SRT is not a web format; WebVTT is the same idea with a stricter header. */
function toVtt(input) {
  const text = Buffer.isBuffer(input) ? input.toString('utf8') : String(input);
  let body = text.replace(/^\uFEFF/, '').replace(/\r\n?/g, '\n');
  if (/^WEBVTT/.test(body)) return body;

  body = body
    // 00:00:20,000 -> 00:00:20.000 (only inside timestamp lines)
    .replace(/(\d\d:\d\d:\d\d),(\d\d\d)/g, '$1.$2')
    // SRT cue numbers are not needed and become stray identifiers
    .replace(/^[ \t]*\d+[ \t]*\n(?=[ \t]*\d\d:\d\d:\d\d\.\d\d\d[ \t]*-->)/gm, '')
    // X1:/Y1:/X2:/Y2: position hints are SRT-only
    .replace(/^(.+-->\s*\d\d:\d\d:\d\d\.\d\d\d)[ \t]+X1:.*$/gm, '$1')
    .replace(/\n{3,}/g, '\n\n')
    .trim();

  return `WEBVTT\n\n${body}\n`;
}

function readSidecar(file) {
  const candidate = file.replace(/\.[^.]+$/, '.json');
  if (candidate === file) return null;
  try {
    return JSON.parse(fs.readFileSync(candidate, 'utf8'));
  } catch {
    return null;
  }
}

async function walk(folder, found = [], depth = 0) {
  if (depth > 6) return found;
  let entries;
  try {
    entries = await fsp.readdir(folder, { withFileTypes: true });
  } catch (err) {
    console.warn(`[library] cannot read ${folder}: ${err.message}`);
    return found;
  }
  for (const entry of entries) {
    if (entry.name.startsWith('.')) continue;
    const full = path.join(folder, entry.name);
    if (entry.isDirectory()) {
      if (/^(node_modules|System Volume Information|\$RECYCLE\.BIN)$/i.test(entry.name)) continue;
      await walk(full, found, depth + 1);
    } else if (VIDEO_EXT.has(path.extname(entry.name).toLowerCase())) {
      found.push(full);
    }
  }
  return found;
}

/**
 * Scan folders and build the catalog.
 *
 * Deliberately two phases, because reading a file and drawing it are wildly
 * different costs. Phase 1 asks ffprobe for duration and dimensions and touches
 * no pixels: that is fast enough to publish a usable catalog in seconds. Phase 2
 * renders artwork in the background and republishes the catalog as it goes, so the
 * user is browsing while their thumbnails are still being made.
 *
 * @param {string[]} folders
 * @param {object} opts
 * @param {object} opts.previous      previous catalog, used to skip unchanged files
 * @param {string} opts.mediaDir      where generated artwork is written
 * @param {(p: {phase:string,done:number,total:number,current:string}) => void} [opts.onProgress]
 * @param {(catalog: object) => void} [opts.onCatalog]  called whenever the catalog improves
 */
/**
 * Draw everything that is missing and nothing else, so a pass can be stopped at
 * any point and the next one picks up the remainder. Shared by a full scan and
 * by the pass that runs when the app is reopened.
 */
async function renderMissingArtwork(items, {
  mediaDir,
  onProgress = () => {},
  onCatalog = () => {},
  retryFailed = false,
  cancel = () => false,
} = {}) {
  const candidates = items.filter((item) => retryFailed || shouldRetryArt(item));
  const pending = candidates.filter((item) => needsArtwork(item, mediaDir));
  if (!pending.length) return { total: 0, drawn: 0, remaining: 0 };

  let drawn = 0;
  let remaining = 0;
  let lastPublish = Date.now();
  onProgress({ phase: 'art', done: 0, total: pending.length, current: '' });

  await runPool(pending, 2, async (item) => {
    if (cancel()) { remaining += 1; return; }
    const files = artPaths(mediaDir, item.id);
    try {
      // A catalogue entry with no duration has never been probed (an interrupted
      // first run); probe just this file rather than rescanning the library.
      if (!item.duration) {
        if (!fileHasContent(item.path)) throw new Error('the video file is missing');
        const info = await probeVideo(item.path);
        item.duration = info.duration;
        item.width = info.width || item.width;
        item.height = info.height || item.height;
        item.vcodec = info.vcodec || item.vcodec;
        item.acodec = info.acodec || item.acodec;
      }
      if (!item.duration) throw new Error('the video could not be read');

      const needs = missingArt(item, mediaDir);
      const { produced, failures } = await renderArt(item.path, item.duration, mediaDir, item.id, needs);

      item.poster = produced.poster || (fileHasContent(files.poster) ? path.basename(files.poster) : item.poster || null);
      item.backdrop = produced.backdrop || (fileHasContent(files.backdrop) ? path.basename(files.backdrop) : item.backdrop || null);
      item.preview = produced.preview || (fileHasContent(files.preview) ? path.basename(files.preview) : item.preview || null);
      // Only claim the new format once a clip exists, or a transient ffmpeg
      // failure would mark it done and never retry.
      if (!needs.preview || produced.preview) item.previewVersion = PREVIEW_VERSION;

      if (needsArtwork(item, mediaDir)) {
        throw new Error(failures.length ? `${failures.join(', ')} could not be drawn` : 'artwork incomplete');
      }
      drawn += 1;
      delete item.artError;
      delete item.artAttempts;
    } catch (err) {
      remaining += 1;
      item.artAttempts = (item.artAttempts || 0) + 1;
      item.artError = err.message;
      console.warn(`[library] artwork failed for ${item.filename}: ${err.message}`);
    }

    onProgress({ phase: 'art', done: drawn, total: pending.length, current: item.filename });
    // Publish at a sane rate; one write per frame would thrash the disk. The
    // next pass starts from whatever the last publish recorded.
    if (Date.now() - lastPublish > 1200 || drawn + remaining === pending.length) {
      lastPublish = Date.now();
      onCatalog();
    }
  });

  onProgress({ phase: 'art', done: drawn, total: pending.length, current: '' });
  onCatalog();
  return { total: pending.length, drawn, remaining };
}

async function scan(folders, {
  previous = null,
  mediaDir,
  onProgress = () => {},
  onCatalog = () => {},
} = {}) {
  const tools = await probeTools();
  await fsp.mkdir(mediaDir, { recursive: true });

  const previousById = new Map((previous?.items || []).map((i) => [i.id, i]));
  const files = [];
  const errors = [];

  for (const folder of folders) {
    if (!folder || !fs.existsSync(folder)) {
      errors.push({ folder, message: 'Folder does not exist or is not accessible' });
      continue;
    }
    await walk(folder, files);
  }

  // One directory listing per folder, so sidecar subtitles cost nothing per file.
  const entriesByDir = new Map();
  for (const file of files) {
    const dir = path.dirname(file);
    if (entriesByDir.has(dir)) continue;
    try {
      entriesByDir.set(dir, await fsp.readdir(dir));
    } catch {
      entriesByDir.set(dir, []);
    }
  }

  /* ---------------- phase 1: read (fast, no pixels) ---------------- */

  let read = 0;
  const items = await runPool(files, 6, async (file) => {
    let stat;
    try {
      stat = await fsp.stat(file);
    } catch {
      return null;
    }

    const id = sha1(path.resolve(file));
    const cached = previousById.get(id);
    const unchanged = cached && cached.size === stat.size && cached.mtimeMs === Math.round(stat.mtimeMs);
    const subtitles = findSubtitles(file, entriesByDir.get(path.dirname(file)) || []);

    read += 1;
    onProgress({ phase: 'read', done: read, total: files.length, current: path.basename(file) });

    if (unchanged) {
      return { ...cached, subtitles };
    }

    const parsed = parseName(path.basename(file));
    const sidecar = readSidecar(file) || {};
    const info = tools.ffprobe ? await probeVideo(file) : { duration: 0, width: 0, height: 0 };

    if (tools.canRender && info.duration === 0) {
      errors.push({ folder: path.dirname(file), message: `Unreadable video: ${path.basename(file)}` });
    }

    return {
      id,
      title: sidecar.title || parsed.title,
      year: sidecar.year ?? parsed.year ?? null,
      synopsis: sidecar.synopsis || null,
      genres: Array.isArray(sidecar.genres) ? sidecar.genres : [],
      rating: sidecar.rating || null,
      accent: sidecar.accent || null,
      path: path.resolve(file),
      filename: path.basename(file),
      size: stat.size,
      mtimeMs: Math.round(stat.mtimeMs),
      addedAt: cached?.addedAt || Math.round(stat.mtimeMs),
      duration: info.duration,
      width: info.width,
      height: info.height,
      vcodec: info.vcodec || null,
      acodec: info.acodec || null,
      subtitles,
      // Rendered in phase 2; carried over when the file has not changed.
      poster: cached?.poster || null,
      backdrop: cached?.backdrop || null,
      preview: cached?.preview || null,
      // 0 forces a re-render: the file changed, so the old clip is stale.
      previewVersion: 0,
    };
  });

  const catalog = {
    scannedAt: Date.now(),
    folders,
    tools,
    errors,
    items: items.filter(Boolean).sort((a, b) => b.addedAt - a.addedAt),
  };

  // Publish now: everything the grid needs to be usable is already here.
  onProgress({ phase: 'art', done: 0, total: catalog.items.length, current: '' });
  onCatalog(catalog);

  /* ---------------- phase 2: draw (background) ---------------- */

  if (tools.canRender) {
    await renderMissingArtwork(catalog.items, {
      mediaDir,
      retryFailed: true,
      onProgress,
      onCatalog: () => onCatalog(catalog),
    });
  }

  return catalog;
}

function mimeFor(file) {
  return MIME[path.extname(file).toLowerCase()] || 'application/octet-stream';
}

module.exports = { scan, parseName, mimeFor, probeTools, toVtt, findSubtitles,
  listFolderSubtitles, previewSegments, PREVIEW_VERSION, VIDEO_EXT,
  artPaths, missingArt, needsArtwork, shouldRetryArt, renderMissingArtwork,
  sweepTempArt, ART_MAX_ATTEMPTS };
