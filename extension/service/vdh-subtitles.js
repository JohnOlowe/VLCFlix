/* Subtitles as files.
 *
 * HLS declares subtitle renditions in the master playlist:
 *
 *   #EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",LANGUAGE="es",URI="subs/es.m3u8"
 *
 * and the URI is either a single WebVTT file or -- very commonly -- a media
 * playlist of WebVTT segments, one file per few seconds. The vendor bundle
 * already parses the renditions (`Vm` in service/main.js) and, on the ffmpeg
 * muxed paths, embeds ONE of them into the output container. What it never does
 * is write a separate .vtt file, which is what you want if the player on the
 * phone ignores embedded mov_text or you want subtitles next to the video.
 *
 * This module turns a subtitle rendition URI into one clean WebVTT document:
 *
 *   fetchSubtitleTrack({url, headers})  ->  { text, segments, encrypted, ... }
 *
 * Stitching rules that matter in practice:
 *   - every segment repeats a "WEBVTT" header; only one may survive
 *   - segments usually carry "X-TIMESTAMP-MAP=MPEGTS:<ticks>,LOCAL:00:00:00.000";
 *     each segment's cues are shifted by (its MPEGTS - the first segment's) / 90000
 *     so the result is continuous instead of every cue restarting at zero
 *   - "#EXT-X-MAP" segments (init) are fetched first when present
 *   - encryption (EXT-X-KEY with a real METHOD) and EXT-X-BYTERANGE are not
 *     supported: we say so instead of writing a broken file
 *
 * Deliberately a module so it can be imported by the service worker *and*
 * loaded directly by a test page, i.e. the tests exercise this exact code.
 */

const MAX_SEGMENTS = 400;
const MAX_BYTES = 32 * 1024 * 1024;
const FETCH_TIMEOUT_MS = 20000;

/** Resolve a possibly relative reference against a base URL. */
export function resolveUrl(base, ref) {
  try {
    return new URL(ref, base).href;
  } catch {
    return null;
  }
}

/** "00:01:02.500" or "01:02.500" (or seconds) -> milliseconds. */
export function parseTimestamp(value) {
  const parts = String(value).trim().split(":");
  if (parts.length === 0 || parts.length > 3) return null;
  const secs = parseFloat(parts[parts.length - 1]);
  if (!Number.isFinite(secs)) return null;
  let ms = secs * 1000;
  if (parts.length >= 2) ms += parseInt(parts[parts.length - 2], 10) * 60000;
  if (parts.length === 3) ms += parseInt(parts[0], 10) * 3600000;
  return Math.round(ms);
}

/** milliseconds -> "HH:MM:SS.mmm", the form WebVTT requires. */
export function formatTimestamp(ms) {
  if (ms < 0) ms = 0;
  const h = Math.floor(ms / 3600000);
  const m = Math.floor((ms % 3600000) / 60000);
  const s = Math.floor((ms % 60000) / 1000);
  const frac = Math.round(ms % 1000);
  const pad = (n, w = 2) => String(n).padStart(w, "0");
  return `${pad(h)}:${pad(m)}:${pad(s)}.${pad(frac, 3)}`;
}

/** Is this text an HLS playlist at all? */
export function looksLikePlaylist(text) {
  return /^\uFEFF?#EXTM3U/.test(String(text).trimStart());
}

/**
 * Read a subtitle media playlist.
 * Returns { segments: [absolute url], initSegment, encrypted, byteRanges }.
 */
export function parseSubtitlePlaylist(text, baseUrl) {
  const lines = String(text).replace(/\r/g, "").split("\n");
  const segments = [];
  let initSegment = null;
  let encrypted = null;
  let byteRanges = false;
  let pendingRange = false;

  for (const raw of lines) {
    const line = raw.trim();
    if (!line) continue;
    if (line.startsWith("#")) {
      if (line.startsWith("#EXT-X-KEY")) {
        const method = /METHOD=([^,]+)/.exec(line);
        // METHOD=NONE means "no encryption from here on"
        if (method && method[1].toUpperCase() !== "NONE") {
          encrypted = method[1].toUpperCase();
        }
      } else if (line.startsWith("#EXT-X-MAP")) {
        const uri = /URI="([^"]+)"/.exec(line);
        if (uri) initSegment = resolveUrl(baseUrl, uri[1]);
      } else if (line.startsWith("#EXT-X-BYTERANGE")) {
        pendingRange = true;
      }
      continue;
    }
    // a non-comment line is a segment URI
    if (pendingRange) byteRanges = true;
    const abs = resolveUrl(baseUrl, line);
    if (abs) segments.push(abs);
  }
  return { segments, initSegment, encrypted, byteRanges };
}

/** Pull "X-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000" out of a header. */
export function parseTimestampMap(text) {
  const m = /X-TIMESTAMP-MAP\s*=\s*MPEGTS:(\d+)\s*,\s*LOCAL:([0-9:.]+)/.exec(String(text));
  if (!m) return null;
  const mpegts = parseInt(m[1], 10);
  const local = parseTimestamp(m[2]);
  if (!Number.isFinite(mpegts) || local === null) return null;
  return { mpegts, local };
}

/**
 * Shift the cue timings of one segment by `offsetMs` and strip its headers.
 * A WebVTT cue timing line is "start --> end [settings]" and may be preceded by
 * an optional identifier line.
 */
export function shiftSegment(text, offsetMs) {
  const lines = String(text).replace(/\r/g, "").split("\n");
  const out = [];
  for (const line of lines) {
    if (/^WEBVTT/.test(line) || /^X-TIMESTAMP-MAP/i.test(line) || /^NOTE\b/.test(line)) continue;
    const timing = /^(\s*)([0-9:.]+)\s*-->\s*([0-9:.]+)(.*)$/.exec(line);
    if (timing) {
      const start = parseTimestamp(timing[2]);
      const end = parseTimestamp(timing[3]);
      if (start !== null && end !== null && offsetMs !== 0) {
        out.push(`${timing[1]}${formatTimestamp(start + offsetMs)} --> ` +
                 `${formatTimestamp(end + offsetMs)}${timing[4]}`);
        continue;
      }
    }
    out.push(line);
  }
  // collapse leading/trailing blank lines so cues join cleanly
  while (out.length && out[0].trim() === "") out.shift();
  while (out.length && out[out.length - 1].trim() === "") out.pop();
  return out.join("\n");
}

/** Join shifted segment bodies into one valid WebVTT document. */
export function stitchVtt(bodies) {
  const clean = bodies.filter(b => b && b.trim()).map(b => b.replace(/\s+$/, ""));
  if (!clean.length) return "WEBVTT\n";
  return "WEBVTT\n\n" + clean.join("\n\n") + "\n";
}

async function fetchText(url, headers, signal) {
  const res = await fetch(url, { headers: new Headers(headers || []), signal });
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${url.slice(0, 120)}`);
  return await res.text();
}

/**
 * Fetch one subtitle rendition and return a single WebVTT document.
 *
 * @param {{url: string, headers?: (string[]|Headers), language?: string}} track
 * @returns {Promise<{text: string, segments: number, encrypted: string|null,
 *                    bytesIn: number}>}
 */
export async function fetchSubtitleTrack({ url, headers }) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    const first = await fetchText(url, headers, controller.signal);

    // a plain WebVTT file: nothing to stitch
    if (!looksLikePlaylist(first)) {
      const text = first.replace(/\r/g, "");
      return {
        text: /^WEBVTT/.test(text.trimStart()) ? text : "WEBVTT\n\n" + text,
        segments: 1,
        encrypted: null,
        bytesIn: first.length,
      };
    }

    const plan = parseSubtitlePlaylist(first, url);
    if (plan.encrypted) {
      throw new Error(`subtitle playlist is encrypted (${plan.encrypted})`);
    }
    if (plan.byteRanges) {
      throw new Error("subtitle playlist uses EXT-X-BYTERANGE, which is not supported");
    }
    if (!plan.segments.length) throw new Error("subtitle playlist has no segments");
    if (plan.segments.length > MAX_SEGMENTS) {
      throw new Error(`subtitle playlist has ${plan.segments.length} segments ` +
                      `(limit ${MAX_SEGMENTS})`);
    }

    const bases = [];
    if (plan.initSegment) bases.push(plan.initSegment);
    let bytesIn = first.length;
    let total = 0;
    const bodies = [];
    let baseMpegts = null;
    let count = 0;

    for (const segmentUrl of [...bases, ...plan.segments]) {
      const text = await fetchText(segmentUrl, headers, controller.signal);
      bytesIn += text.length;
      total += text.length;
      if (total > MAX_BYTES) throw new Error("subtitle track exceeds the size limit");
      const map = parseTimestampMap(text);
      if (map) {
        if (baseMpegts === null) baseMpegts = map.mpegts;
        // 90 ticks per millisecond (90 kHz clock)
        const offsetMs = Math.round((map.mpegts - baseMpegts) / 90);
        bodies.push(shiftSegment(text, offsetMs));
      } else {
        bodies.push(shiftSegment(text, 0));
      }
      count++;
    }

    return { text: stitchVtt(bodies), segments: count, encrypted: null, bytesIn };
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Cheap "can this rendition actually be saved?" probe, so the history page never
 * shows a button that is guaranteed to fail. Downloads only the playlist head
 * (a few KB), not the segments.
 *
 * @param {{url: string, headers?: (string[]|Headers)}} rendition
 * @returns {Promise<{ok: boolean, reason?: string}>}
 */
export async function probeSubtitleRendition({ url, headers }) {
  if (!url) return { ok: false, reason: "no url" };
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    const res = await fetch(url, { headers: new Headers(headers || []), signal: controller.signal });
    if (!res.ok) return { ok: false, reason: `HTTP ${res.status}` };
    const text = await res.text();

    if (!looksLikePlaylist(text)) {
      return /^WEBVTT/.test(text.trimStart())
        ? { ok: true }
        : { ok: false, reason: "not a WebVTT file" };
    }
    const plan = parseSubtitlePlaylist(text, url);
    if (plan.encrypted) return { ok: false, reason: `encrypted (${plan.encrypted})` };
    if (plan.byteRanges) return { ok: false, reason: "uses EXT-X-BYTERANGE" };
    if (!plan.segments.length) return { ok: false, reason: "no segments" };
    if (plan.segments.length > MAX_SEGMENTS) {
      return { ok: false, reason: `${plan.segments.length} segments (limit ${MAX_SEGMENTS})` };
    }
    return { ok: true, segments: plan.segments.length };
  } catch (e) {
    return { ok: false, reason: String((e && e.message) || e) };
  } finally {
    clearTimeout(timer);
  }
}

/**
 * The media object on a journal record arrives in the extension's serde wire
 * format (`{__serde_tag: "object", __serde_val: {...}}`). Walk that back to a
 * list of subtitle renditions, so the history page can still offer subtitles
 * for a download that was recorded before a crash.
 *
 * @param {any} media serde-encoded media payload
 * @returns {{url: string, language: string, type: string}[]}
 */
export function renditionsFromMedia(media) {
  const unwrap = (v) => {
    if (v === null || v === undefined) return null;
    if (Array.isArray(v)) return v;
    if (typeof v !== "object") return v;
    const tag = v.__serde_tag;
    if (tag === "none") return null;
    if (tag === "url") return typeof v.__serde_val === "string" ? v.__serde_val : null;
    if (tag === "primitive" || tag === "some" || tag === "ok" || tag === "err") {
      return unwrap(v.__serde_val);
    }
    if (tag === "array" || tag === "set") {
      return (v.__serde_val || []).map(unwrap);
    }
    if (tag === "object") return v.__serde_val || {};
    return v.__serde_val !== undefined ? v.__serde_val : v;
  };

  const root = unwrap(media) || {};
  const list = unwrap(root.subtitles);
  if (!Array.isArray(list)) return [];
  return list
    .map((raw) => {
      const r = unwrap(raw) || {};
      const url = unwrap(r.url);
      return {
        url: typeof url === "string" ? url : (url && url.href) || "",
        language: String(unwrap(r.language) || ""),
        type: String(unwrap(r.type) || ""),
      };
    })
    .filter((r) => r.url);
}

/**
 * Read subtitle renditions straight out of a *master* playlist, so subtitles can
 * be fetched for a stream that was never downloaded. Resolves each URI against
 * the playlist URL, keeps the language and the NAME label.
 *
 * @param {string} text master playlist body
 * @param {string} baseUrl the URL the playlist was fetched from
 * @returns {{url: string, language: string, name: string, type: string}[]}
 */
export function renditionsFromPlaylist(text, baseUrl) {
  const out = [];
  const seen = new Set();
  for (const raw of String(text || "").replace(/\r/g, "").split("\n")) {
    const line = raw.trim();
    if (!line.startsWith("#EXT-X-MEDIA:")) continue;
    const attrs = {};
    /* EXT-X-MEDIA attributes are comma separated, but values may be quoted */
    const re = /([A-Z0-9-]+)=("[^"]*"|[^,]*)/g;
    let m;
    while ((m = re.exec(line.slice(13)))) {
      attrs[m[1]] = m[2].replace(/^"|"$/g, "");
    }
    if ((attrs.TYPE || "").toUpperCase() !== "SUBTITLES") continue;
    if (!attrs.URI) continue;
    let url;
    try { url = new URL(attrs.URI, baseUrl).href; } catch (e) { continue; }
    if (seen.has(url)) continue;
    seen.add(url);
    out.push({
      url,
      language: attrs.LANGUAGE || attrs.NAME || "und",
      name: attrs.NAME || attrs.LANGUAGE || "",
      type: "http",
    });
  }
  return out;
}

/** Make a language tag safe for a file name; falls back to "und" (undetermined). */
export function safeLanguage(language) {
  const cleaned = String(language || "").trim().replace(/[^A-Za-z0-9-]+/g, "-")
    .replace(/^-+|-+$/g, "").toLowerCase();
  return cleaned || "und";
}

/* ------------------------------------------------------------------ formats */

/**
 * Which format a fetched subtitle document is really in. Our stitcher always
 * joins *segmented* playlists into WebVTT, but some streams publish a single
 * plain `.srt` file, and that must not be relabelled as WebVTT.
 *
 * @param {string} text
 * @returns {"vtt"|"srt"}
 */
export function detectFormat(text) {
  const body = String(text || "").replace(/^\uFEFF/, "").trimStart();
  if (/^WEBVTT(\s|$)/.test(body)) return "vtt";
  /* a SubRip file has no header at all: look for the first timestamp line */
  if (/^\d+\s*$/m.test(body) && /\d{2}:\d{2}:\d{2},\d{3}\s*-->\s*\d{2}:\d{2}:\d{2},\d{3}/.test(body)) {
    return "srt";
  }
  return "vtt";
}

/** What would be lost by turning this WebVTT document into SubRip? */
export function assessSubtitleLoss(text) {
  const src = String(text || "");
  const reasons = [];

  if (/^STYLE\s*$/m.test(src)) reasons.push("style block");
  if (/^REGION\s*$/m.test(src)) reasons.push("region block");
  if (/<ruby|<rt[ >]/i.test(src)) reasons.push("ruby annotations");
  if (/<lang[ >]/i.test(src)) reasons.push("language spans");
  if (/<\d{2}:\d{2}:\d{2}\.\d{3}>/.test(src)) reasons.push("karaoke timing");
  /* class-based styling, e.g. <c.yellow> or <v Bob.class> */
  if (/<c\.[^>]*>|<v\s[^>]*\.[^>]*>/i.test(src)) reasons.push("css classes");
  /* cue settings such as align:start position:10% line:0 */
  const settings = src.match(/-->\s*[^\r\n]*?(\b(?:align|position|line|size|region|vertical):[^\r\n]*)/i);
  if (settings) reasons.push("cue positioning");

  return { lossy: reasons.length > 0, reasons };
}

function srtTime(stamp) {
  const m = /^(?:(\d+):)?(\d{1,2}):(\d{2})[.,](\d{1,3})$/.exec(String(stamp).trim());
  if (!m) return null;
  const hh = String(m[1] ? Number(m[1]) : 0).padStart(2, "0");
  const mm = String(Number(m[2])).padStart(2, "0");
  const ss = String(Number(m[3])).padStart(2, "0");
  const ms = String(m[4]).padEnd(3, "0");
  return `${hh}:${mm}:${ss},${ms}`;
}

/** Keep only the inline markup SubRip players understand. */
function srtSafeMarkup(block) {
  return block
    .replace(/<(\/?)(b|i|u)>/gi, "<$1$2>")
    .replace(/<font\s+color\s*=\s*"?([#\w]+)"?\s*>/gi, '<font color="$1">')
    .replace(/<\/font>/gi, "</font>")
    /* everything else is WebVTT-only or positional */
    .replace(/<[^>]*>/g, "");
}

/**
 * Convert a WebVTT document to SubRip. Strips WebVTT-only markup, cue settings
 * and note/style/region blocks, renumbers the cues and switches the decimal
 * separator from `.` to `,` the way SubRip requires.
 *
 * @param {string} vtt
 * @returns {string}
 */
export function vttToSrt(vtt) {
  const lines = String(vtt || "").replace(/\r\n?/g, "\n").split("\n");
  const out = [];
  let i = 0;
  let index = 0;

  /* skip the WEBVTT header and any header metadata up to the first blank line */
  if (/^WEBVTT/.test(lines[0] || "")) {
    i = 1;
    while (i < lines.length && lines[i].trim() !== "") i++;
  }

  while (i < lines.length) {
    if (lines[i].trim() === "") { i++; continue; }

    /* NOTE / STYLE / REGION blocks run until the next blank line */
    if (/^(NOTE|STYLE|REGION)\b/.test(lines[i])) {
      while (i < lines.length && lines[i].trim() !== "") i++;
      continue;
    }

    /* an optional cue identifier line, then the timing line */
    let timing = lines[i];
    if (!timing.includes("-->")) {
      i++;
      timing = lines[i] || "";
      if (!timing.includes("-->")) { i++; continue; }
    }
    i++;

    const arrow = timing.split(/\s*-->\s*/);
    const start = srtTime(arrow[0]);
    /* drop cue settings: "00:00:02.000 align:start position:10%" */
    const end = srtTime((arrow[1] || "").trim().split(/\s+/)[0]);
    if (!start || !end) continue;

    const text = [];
    while (i < lines.length && lines[i].trim() !== "") {
      text.push(srtSafeMarkup(lines[i]));
      i++;
    }
    while (text.length && text[text.length - 1].trim() === "") text.pop();
    if (!text.length) continue;

    index++;
    out.push(String(index));
    out.push(`${start} --> ${end}`);
    out.push(...text);
    out.push("");
  }

  return out.join("\n");
}

export const SUBTITLE_MODES = ["keep", "smart", "srt"];

/**
 * Apply the user's format choice.
 *
 * - `keep`  : no conversion at all; WebVTT stays WebVTT, SubRip stays SubRip
 * - `smart` : convert to SubRip only when nothing meaningful would be lost
 * - `srt`   : always convert, dropping whatever SubRip cannot carry
 *
 * @param {string} text    document as produced by the stitcher
 * @param {"keep"|"smart"|"srt"} mode
 * @returns {{text: string, ext: "vtt"|"srt", converted: boolean,
 *            reasons: string[], mode: string}}
 */
export function applySubtitleFormat(text, mode) {
  const chosen = SUBTITLE_MODES.includes(mode) ? mode : "keep";
  const source = detectFormat(text);
  const loss = assessSubtitleLoss(text);

  if (chosen === "keep") {
    return { text, ext: source, converted: false, reasons: loss.reasons, mode: chosen };
  }
  if (source === "srt") {
    /* already SubRip: nothing to do in any mode */
    return { text, ext: "srt", converted: false, reasons: loss.reasons, mode: chosen };
  }
  if (chosen === "smart" && loss.lossy) {
    return { text, ext: "vtt", converted: false, reasons: loss.reasons, mode: chosen };
  }
  return {
    text: vttToSrt(text),
    ext: "srt",
    converted: true,
    reasons: loss.reasons,
    mode: chosen,
  };
}
