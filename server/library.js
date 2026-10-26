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
  try {
    const { stdout } = await run(
      'ffprobe',
      ['-v', 'error', '-show_entries', 'format=duration:stream=width,height',
       '-select_streams', 'v:0', '-of', 'json', file],
      { timeout: 20000, maxBuffer: 1 << 20 }
    );
    const data = JSON.parse(stdout || '{}');
    const stream = (data.streams || [])[0] || {};
    return {
      duration: Number.parseFloat(data.format?.duration) || 0,
      width: Number(stream.width) || 0,
      height: Number(stream.height) || 0,
    };
  } catch {
    return { duration: 0, width: 0, height: 0 };
  }
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

async function renderArt(file, duration, outDir, id) {
  const at = (fraction) => Math.max(0, Math.min(duration * fraction, Math.max(0, duration - 1)));
  const outputs = {
    poster: path.join(outDir, `${id}-poster.jpg`),
    backdrop: path.join(outDir, `${id}-backdrop.jpg`),
    preview: path.join(outDir, `${id}-preview.mp4`),
  };

  const jobs = [
    run('ffmpeg', ['-y', '-loglevel', 'error', '-ss', String(at(0.38)), '-i', file,
      '-frames:v', '1', '-vf', 'scale=400:600:force_original_aspect_ratio=increase,crop=400:600,' + GRADE,
      '-q:v', '4', outputs.poster], { timeout: 60000 }),
    run('ffmpeg', ['-y', '-loglevel', 'error', '-ss', String(at(0.22)), '-i', file,
      '-frames:v', '1', '-vf', 'scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720,' + GRADE,
      '-q:v', '3', outputs.backdrop], { timeout: 60000 }),
    run('ffmpeg', ['-y', '-loglevel', 'error', '-ss', String(at(0.3)), '-t', '8', '-i', file,
      '-an', '-vf', 'scale=640:-2', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '30',
      '-pix_fmt', 'yuv420p', '-movflags', '+faststart', outputs.preview], { timeout: 120000 }),
  ];

  const settled = await Promise.allSettled(jobs);
  const produced = {};
  const keys = ['poster', 'backdrop', 'preview'];
  settled.forEach((result, i) => {
    const file_path = outputs[keys[i]];
    if (result.status === 'fulfilled' && fs.existsSync(file_path)) produced[keys[i]] = path.basename(file_path);
    else if (result.status === 'rejected') {
      console.warn(`[library] ${keys[i]} failed for ${path.basename(file)}: ${result.reason?.message?.split('\n')[0]}`);
    }
  });

  // Fix stills that came out too dark or too flat for white text to sit on.
  await Promise.all(
    ['poster', 'backdrop']
      .filter((key) => produced[key])
      .map((key) => correctExposure(outputs[key], key === 'poster' ? 64 : 72))
  );

  return produced;
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
 * @param {string[]} folders
 * @param {object} opts
 * @param {object} opts.previous  previous catalog, used to skip unchanged files
 * @param {string} opts.mediaDir  where generated artwork is written
 * @param {(p: {done:number,total:number,current:string}) => void} [opts.onProgress]
 */
async function scan(folders, { previous = null, mediaDir, onProgress = () => {} } = {}) {
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

  const items = [];
  let done = 0;

  for (const file of files) {
    const id = sha1(path.resolve(file));
    let stat;
    try {
      stat = await fsp.stat(file);
    } catch {
      continue;
    }

    onProgress({ done, total: files.length, current: path.basename(file) });
    done += 1;

    const cached = previousById.get(id);
    const unchanged = cached && cached.size === stat.size && cached.mtimeMs === Math.round(stat.mtimeMs);
    if (unchanged) {
      items.push(cached);
      continue;
    }

    const parsed = parseName(path.basename(file));
    const sidecar = readSidecar(file) || {};
    const info = tools.ffprobe ? await probeVideo(file) : { duration: 0, width: 0, height: 0 };
    const art = tools.canRender && info.duration > 0
      ? await renderArt(file, info.duration, mediaDir, id)
      : {};

    if (tools.canRender && info.duration === 0) {
      errors.push({ folder: path.dirname(file), message: `Unreadable video: ${path.basename(file)}` });
    }

    items.push({
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
      poster: art.poster || null,
      backdrop: art.backdrop || null,
      preview: art.preview || null,
    });
  }

  onProgress({ done: files.length, total: files.length, current: '' });
  items.sort((a, b) => b.addedAt - a.addedAt);

  return {
    scannedAt: Date.now(),
    folders,
    tools,
    errors,
    items,
  };
}

function mimeFor(file) {
  return MIME[path.extname(file).toLowerCase()] || 'application/octet-stream';
}

module.exports = { scan, parseName, mimeFor, probeTools, VIDEO_EXT };
