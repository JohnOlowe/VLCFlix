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
 * DASH (an .mpd manifest) declares the same thing differently:
 *
 *   <AdaptationSet contentType="text" mimeType="text/vtt" lang="en">
 *     <Role schemeIdUri="urn:mpeg:dash:role:2011" value="subtitle"/>
 *     <Representation id="en"><BaseURL>en.vtt</BaseURL></Representation>
 *   </AdaptationSet>
 *
 * and the Representation is either one complete file or a SegmentTemplate +
 * SegmentTimeline that has to be walked to enumerate the segments. DASH also
 * uses TTML (application/ttml+xml) far more often than HLS does, so TTML is
 * converted to WebVTT here.
 *
 * One reason this was missing for so long: the vendor's DASH parser records a
 * subtitle as an "id" taken from the NAME attribute -- never a URL -- because
 * on desktop the track is muxed by ffmpeg rather than fetched. So for DASH
 * there is nothing to download until the manifest is parsed again here.
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
  const dash = parseDashRef(url);
  if (dash) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    try {
      const got = await fetchDashTrack({
        manifestUrl: dash.manifestUrl, id: dash.id, headers, signal: controller.signal,
      });
      return { text: got.text, segments: got.segments, encrypted: null, bytesIn: got.bytesIn };
    } finally {
      clearTimeout(timer);
    }
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    const first = await fetchText(url, headers, controller.signal);

    // an MPD served where a subtitle file was expected: read the renditions
    // and refuse rather than save the manifest as if it were subtitles
    if (looksLikeMpd(first)) {
      throw new Error("that address is a DASH manifest, not a subtitle file: "
        + "pick one of the languages listed for it");
    }

    // a plain file: nothing to stitch, though TTML still has to be converted
    if (!looksLikePlaylist(first)) {
      return {
        text: toWebVtt(first, null),
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
  const dash = parseDashRef(url);
  if (dash) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    try {
      const xml = await fetchText(dash.manifestUrl, headers, controller.signal);
      const all = renditionsFromMpd(xml, dash.manifestUrl);
      const found = all.find(r => r.id === dash.id) || all.find(r => r.url === url);
      if (!found) return { ok: false, reason: "not in this manifest" };
      if (found.unsupported) return { ok: false, reason: found.unsupported };
      if (found.segments && found.segments.length > MAX_SEGMENTS) {
        return { ok: false, reason: `${found.segments.length} segments (limit ${MAX_SEGMENTS})` };
      }
      return { ok: true, segments: (found.segments && found.segments.length) || 1 };
    } catch (e) {
      return { ok: false, reason: String((e && e.message) || e) };
    } finally {
      clearTimeout(timer);
    }
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    const res = await fetch(url, { headers: new Headers(headers || []), signal: controller.signal });
    if (!res.ok) return { ok: false, reason: `HTTP ${res.status}` };
    const text = await res.text();

    if (!looksLikePlaylist(text)) {
      if (looksLikeMpd(text)) {
        return { ok: false, reason: "that is a DASH manifest: pick one of its languages" };
      }
      if (/^WEBVTT/.test(text.trimStart()) || looksLikeTtml(text)) return { ok: true };
      return { ok: false, reason: "not a WebVTT or TTML file" };
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

  /* The manifest URL, for DASH. The vendor never gives a DASH subtitle a URL:
     it records the Representation id and muxes the track with ffmpeg on
     desktop. There is no ffmpeg here, so the id is resolved into a
     dash:<manifest>#<id> reference that fetchSubtitleTrack can act on. */
  const manifest = (() => {
    for (const key of ["master_url", "url", "manifest_url"]) {
      const value = unwrap(root[key]);
      if (typeof value === "string" && value) return value;
      if (value && typeof value.href === "string" && value.href) return value.href;
    }
    return "";
  })();

  return list
    .map((raw) => {
      const r = unwrap(raw) || {};
      const url = unwrap(r.url);
      const id = unwrap(r.id);
      const type = String(unwrap(r.type) || "");
      let resolved = typeof url === "string" ? url : (url && url.href) || "";
      if (!resolved && type === "id" && id && manifest) {
        resolved = dashRef(manifest, String(id));
      }
      return {
        url: resolved,
        language: String(unwrap(r.language) || ""),
        type,
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
  /* one entry point for both manifest formats: the panel pastes whatever the
     stream uses, and an MPD is XML rather than an #EXTM3U tag list */
  if (looksLikeMpd(text)) return renditionsFromMpd(text, baseUrl);
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


/* ------------------------------------------------------------------- XML ----
 * A service worker has no DOM and therefore no DOMParser, and the manifest is
 * parsed in the worker. So this module carries a small XML reader of its own:
 * enough for MPD and TTML (elements, attributes, entities, CDATA, comments),
 * and it works in the worker, in a page, and under node for tests. */

const XML_ENTITIES = { amp: "&", lt: "<", gt: ">", quot: '"', apos: "'", nbsp: " " };

function decodeEntities(text) {
  return String(text)
    .replace(/&#x([0-9a-fA-F]+);/g, (_, h) => String.fromCodePoint(parseInt(h, 16)))
    .replace(/&#(\d+);/g, (_, d) => String.fromCodePoint(parseInt(d, 10)))
    .replace(/&([a-zA-Z]+);/g, (m, name) =>
      XML_ENTITIES[name] !== undefined ? XML_ENTITIES[name] : m);
}

/**
 * Parse an XML document into { name, attrs, children, text, parts }.
 * Element names are lower-cased and namespace prefixes are dropped, because
 * MPD and TTML files disagree about prefixes (`<MPD>`, `<ns3:MPD>`) while the
 * element names themselves are fixed by the specifications.
 */
export function parseXml(text) {
  const src = String(text || "");
  const root = { name: "#root", attrs: {}, children: [], text: "", parts: [] };
  const stack = [root];
  const tagRe = /<([!?/]?)([\s\S]*?)>/g;
  let i = 0;
  let m;
  while ((m = tagRe.exec(src))) {
    const before = src.slice(i, m.index);
    if (before) {
      const top = stack[stack.length - 1];
      top.text += before;
      top.parts.push(before);
    }
    i = m.index + m[0].length;
    const kind = m[1];
    const body = m[2];
    if (kind === "!") {
      /* search from the start of the construct, not from after the `>` the
         regex stopped at: a comment may contain `>`, and looking for the next
         "-->"/"]]>" from there swallows everything up to the following one */
      if (body.indexOf("--") === 0) {                 // <!-- comment -->
        const end = src.indexOf("-->", m.index + 4);
        i = end < 0 ? src.length : end + 3;
        tagRe.lastIndex = i;
      } else if (body.indexOf("[CDATA[") === 0) {     // <![CDATA[ raw text ]]>
        const start = m.index + 9;
        const end = src.indexOf("]]>", start);
        const data = src.slice(start, end < 0 ? src.length : end);
        const top = stack[stack.length - 1];
        top.text += data;
        top.parts.push(data);
        i = end < 0 ? src.length : end + 3;
        tagRe.lastIndex = i;
      }
      continue;
    }
    if (kind === "?") continue;                     // <?xml ...?>
    if (kind === "/") { if (stack.length > 1) stack.pop(); continue; }
    const selfClosing = /\/\s*$/.test(body);
    const raw = body.replace(/\/\s*$/, "").trim();
    const nameMatch = /^([^\s/]+)/.exec(raw);
    if (!nameMatch) continue;
    const rawName = nameMatch[1];
    const attrs = {};
    const attrRe = /([^\s=/>]+)\s*=\s*("([^"]*)"|'([^']*)'|([^\s"'>]+))/g;
    let a;
    while ((a = attrRe.exec(raw))) {
      attrs[a[1].toLowerCase()] =
        decodeEntities(a[3] !== undefined ? a[3] : a[4] !== undefined ? a[4] : a[5]);
    }
    const node = {
      name: rawName.replace(/^[^:]*:/, "").toLowerCase(),
      rawName,
      attrs,
      children: [],
      text: "",
      parts: [],
    };
    const top = stack[stack.length - 1];
    top.children.push(node);
    top.parts.push(node);
    if (!selfClosing) stack.push(node);
  }
  const tail = src.slice(i);
  if (tail) {
    const top = stack[stack.length - 1];
    top.text += tail;
    top.parts.push(tail);
  }
  return root;
}

/** Direct children with this (lower-case, prefix-free) name. */
function kids(node, name) {
  const want = String(name).toLowerCase();
  return ((node && node.children) || []).filter(c => c.name === want);
}

/** Depth-first search for the first descendant with this name. */
function find(node, name) {
  const direct = kids(node, name);
  if (direct.length) return direct[0];
  for (const child of (node && node.children) || []) {
    const found = find(child, name);
    if (found) return found;
  }
  return null;
}

/** An attribute of this element, ignoring case. */
function at(node, name) {
  const attrs = (node && node.attrs) || {};
  const key = String(name).toLowerCase();
  if (attrs[key] !== undefined) return attrs[key];
  for (const own of Object.keys(attrs)) {
    if (own.toLowerCase() === key) return attrs[own];
  }
  return null;
}

/** Text of an element and of everything inside it; <br/> becomes a newline.
 *  Walks `parts` rather than `children` so a <br/> stays where it was written
 *  instead of ending up after the rest of the sentence. */
function textOf(node) {
  if (!node) return "";
  let out = "";
  for (const part of node.parts || []) {
    if (typeof part === "string") out += part;
    else if (part.name === "br") out += "\n";
    else out += textOf(part);
  }
  return out || node.text || "";
}

/* ------------------------------------------------------------------ DASH ----
 * An MPD declares the same thing HLS declares, differently:
 *
 *   <AdaptationSet contentType="text" mimeType="text/vtt" lang="en">
 *     <Role schemeIdUri="urn:mpeg:dash:role:2011" value="subtitle"/>
 *     <Representation id="en"><BaseURL>en.vtt</BaseURL></Representation>
 *   </AdaptationSet>
 *
 * and a Representation is either one complete file or a SegmentTemplate that
 * has to be walked to enumerate the segments. DASH also carries TTML far more
 * often than HLS does, so TTML is converted to WebVTT here.
 *
 * Why this was missing: the vendor's DASH parser records a subtitle as an "id"
 * taken from the NAME attribute -- never a URL -- because on desktop the track
 * is muxed by ffmpeg rather than fetched. For DASH there is nothing to download
 * until the manifest is parsed here. */

const DASH_TEXT_MIMES = new Set([
  "text/vtt", "text/webvtt", "application/ttml+xml", "application/ttaf+xml",
  "application/mp4", "text/plain",
]);

/** True when a body is an MPD manifest rather than a playlist or a cue file. */
export function looksLikeMpd(text) {
  const head = String(text || "").trimStart().slice(0, 4096);
  return /<\s*(?:[a-z0-9]+:)?MPD[\s>]/i.test(head);
}

/** True when a body is TTML (the XML subtitle format DASH usually carries). */
export function looksLikeTtml(text) {
  const head = String(text || "").trimStart().slice(0, 4096);
  return /<\s*(?:[a-z0-9]+:)?tt[\s>]/i.test(head);
}

const roleOf = (set) => {
  for (const role of kids(set, "Role")) {
    const value = at(role, "value");
    if (value) return String(value).toLowerCase();
  }
  return "";
};

/** "text" for subtitle tracks, "" for the audio and video AdaptationSets. */
function kindOf(set) {
  const contentType = String(at(set, "contentType") || "").toLowerCase();
  if (contentType === "text") return "text";
  const mime = String(at(set, "mimeType") || "").toLowerCase();
  if (DASH_TEXT_MIMES.has(mime)) return "text";
  const codecs = String(at(set, "codecs") || "").toLowerCase();
  if (/\b(stpp|wvtt|ttml|webvtt)\b/.test(codecs)) return "text";
  const role = roleOf(set);
  if (role === "subtitle" || role === "caption") {
    /* a subtitle role with no mime, or a text one, is text. With a video or
       audio mime it is a burned-in or CEA variant that cannot be downloaded. */
    if (!mime || DASH_TEXT_MIMES.has(mime)) return "text";
  }
  return "";
}

/** BaseURLs may appear at the MPD, Period, AdaptationSet and Representation
 *  levels and they concatenate, so walk up the tree collecting them. */
function baseUrlChain(node, nodes, baseUrl) {
  const parts = [];
  let current = node;
  while (current) {
    const own = kids(current, "BaseURL")[0];
    if (own && textOf(own).trim()) parts.unshift(textOf(own).trim());
    current = nodes.parent.get(current) || null;
  }
  let url = baseUrl;
  for (const part of parts) {
    const next = resolveUrl(url, part);
    if (next) url = next;
  }
  return url;
}

/** Fill $Number$, $Time$, $Bandwidth$ and $RepresentationID$ in a template. */
function fillTemplate(template, { id, number, time, bandwidth }) {
  return String(template)
    .replace(/\$RepresentationID\$/g, id || "")
    .replace(/\$Number%(\d+)d\$/g, (_, w) => String(number).padStart(parseInt(w, 10), "0"))
    .replace(/\$Number\$/g, String(number))
    .replace(/\$Time\$/g, String(time))
    .replace(/\$Bandwidth\$/g, String(bandwidth || ""));
}

/**
 * Walk a SegmentTemplate into {url, startMs} entries. Two shapes exist:
 *   <SegmentTemplate media="s-$Number$.vtt"><SegmentTimeline><S t= d= r=/>
 *   <SegmentTemplate media="s-$Number$.vtt" duration="4" startNumber="1"/>
 * The second has no explicit count, so it comes from the Period length.
 */
export function dashTemplateSegments(template, { id, bandwidth, periodSeconds }) {
  const timescale = Number(at(template, "timescale")) || 1;
  const media = at(template, "media");
  if (!media) return [];
  const startNumber = Number(at(template, "startNumber")) || 1;
  const timeline = find(template, "SegmentTimeline");
  if (timeline) {
    const entries = [];
    let time = null;
    for (const s of kids(timeline, "S")) {
      const t = at(s, "t");
      if (t !== null) time = Number(t);
      if (time === null) time = 0;
      const d = Number(at(s, "d")) || 0;
      const repeat = Number(at(s, "r")) || 0;
      for (let i = 0; i <= repeat && entries.length < MAX_SEGMENTS; i++) {
        entries.push({
          number: startNumber + entries.length,
          time,
          startMs: Math.round((time / timescale) * 1000),
        });
        time += d;
      }
    }
    return entries.map(entry => ({
      url: fillTemplate(media, { id, number: entry.number, time: entry.time, bandwidth }),
      startMs: entry.startMs,
    }));
  }
  const duration = Number(at(template, "duration"));
  const segmentSeconds = duration / timescale;
  if (!segmentSeconds) return [];
  const known = Number(periodSeconds) || 0;
  const count = known
    ? Math.min(MAX_SEGMENTS, Math.ceil(known / segmentSeconds))
    : Math.min(MAX_SEGMENTS, 60);   /* live: cap rather than run forever */
  const out = [];
  for (let i = 0; i < count; i++) {
    out.push({
      url: fillTemplate(media, { id, number: startNumber + i, time: i * duration, bandwidth }),
      startMs: Math.round(i * segmentSeconds * 1000),
    });
  }
  return out;
}

const formatOf = (set, rep) => {
  const mime = String(at(rep, "mimeType") || at(set, "mimeType") || "").toLowerCase();
  const codecs = String(at(rep, "codecs") || at(set, "codecs") || "").toLowerCase();
  if (/ttml|stpp|ttaf/.test(mime) || /stpp|ttml/.test(codecs)) return "ttml";
  if (/webvtt|text\/vtt/.test(mime) || /wvtt|webvtt/.test(codecs)) return "vtt";
  return "";
};

/**
 * Read the subtitle renditions out of an MPD manifest.
 *
 * @param {string} xmlText the manifest body
 * @param {string} baseUrl the URL the manifest was fetched from
 * @returns {{url: string, language: string, name: string, type: string,
 *            format: string, segments: {url: string, startMs: number}[]}[]}
 */
export function renditionsFromMpd(xmlText, baseUrl) {
  const root = parseXml(xmlText);
  if (!root || !root.children.length) return [];
  /* remember each node's parent so BaseURL chains can be walked upwards */
  const parent = new Map();
  const link = (node) => {
    for (const child of node.children || []) { parent.set(child, node); link(child); }
  };
  link(root);
  const nodes = { parent };

  const out = [];
  const periods = kids(root, "MPD").length ? kids(kids(root, "MPD")[0], "Period") : [];
  const mpd = kids(root, "MPD")[0] || root;
  const periodList = periods.length ? periods : [mpd];

  for (const period of periodList) {
    const periodSeconds = parseDashDuration(at(period, "duration")) ||
      parseDashDuration(at(mpd, "mediaPresentationDuration")) || 0;
    for (const set of kids(period, "AdaptationSet")) {
      if (kindOf(set) !== "text") continue;
      const language = at(set, "lang") || at(set, "contentLanguage") || "und";
      const labelNode = find(set, "Label");
      const label = at(set, "label") || (labelNode ? textOf(labelNode).trim() : "") || "";
      for (const rep of kids(set, "Representation")) {
        const id = at(rep, "id") || `${language}-${out.length}`;
        const codecs = String(at(rep, "codecs") || at(set, "codecs") || "").toLowerCase();
        if (/\bwvtt\b/.test(codecs)) {
          /* WebVTT inside ISOBMFF boxes: the cues are in mdats and reading them
             means parsing the container, which this module does not do. Say so
             rather than write a file that is not subtitles. */
          out.push({
            url: dashRef(baseUrl, id), language, name: label || language,
            type: "dash", format: "wvtt", id, segments: [],
            unsupported: "WebVTT in an MP4 container (wvtt) cannot be read as plain text",
          });
          continue;
        }
        const format = formatOf(set, rep);
        const setBase = baseUrlChain(set, nodes, baseUrl);
        const repBase = baseUrlChain(rep, nodes, setBase);
        const template = find(rep, "SegmentTemplate") || find(set, "SegmentTemplate");
        if (template) {
          const bandwidth = Number(at(rep, "bandwidth")) || 0;
          const segments = dashTemplateSegments(template, { id, bandwidth, periodSeconds })
            .map(entry => ({ url: resolveUrl(repBase, entry.url), startMs: entry.startMs }))
            .filter(entry => entry.url);
          if (segments.length) {
            out.push({
              url: dashRef(baseUrl, id), language, name: label || language,
              type: "dash", format, id, segments,
            });
            continue;
          }
        }
        /* no usable template: the Representation is one complete file */
        const own = kids(rep, "BaseURL")[0];
        const file = own && textOf(own).trim()
          ? resolveUrl(repBase, textOf(own).trim())
          : (kids(rep, "BaseURL").length ? repBase : null);
        if (file) {
          out.push({
            url: file, language, name: label || language,
            type: "dash", format, id, segments: [],
          });
        }
      }
    }
  }
  return out;
}

/** ISO 8601 duration ("PT1H2M3.5S") -> seconds. Enough for DASH manifests. */
export function parseDashDuration(value) {
  const m = /^P(?:(\d+(?:\.\d+)?)D)?(?:T(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?)?$/
    .exec(String(value || "").trim());
  if (!m) return 0;
  return (Number(m[1]) || 0) * 86400 + (Number(m[2]) || 0) * 3600 +
    (Number(m[3]) || 0) * 60 + (Number(m[4]) || 0);
}

/**
 * A segmented DASH rendition has no single URL to fetch, so it is addressed by
 * the manifest plus the Representation id: dash:<manifest>#<id>. That is a
 * plain string like any other rendition URL, so the journal and the history
 * page need no changes, and fetchSubtitleTrack re-reads the manifest (which is
 * small) to enumerate the segments.
 */
export function dashRef(manifestUrl, id) {
  return `dash:${manifestUrl}#${encodeURIComponent(String(id))}`;
}

export function parseDashRef(url) {
  const m = /^dash:(.*)#([^#]*)$/.exec(String(url || ""));
  if (!m) return null;
  return { manifestUrl: m[1], id: decodeURIComponent(m[2]) };
}

/** TTML (the XML subtitle format) -> WebVTT. */
export function ttmlToVtt(ttml) {
  const root = parseXml(ttml);
  const tt = kids(root, "tt")[0] || root;
  if (!tt || !tt.children.length) return null;
  const frameRate = Number(at(tt, "frameRate")) || 0;
  const toMs = (value) => {
    if (!value) return 0;
    const text = String(value).trim();
    let m = /^(\d+(?:\.\d+)?)f$/.exec(text);
    if (m && frameRate) return Math.round((Number(m[1]) / frameRate) * 1000);
    m = /^(?:(\d+(?:\.\d+)?):)?(\d+):(\d+(?:\.\d+)?)$/.exec(text);
    if (m) {
      return Math.round(((Number(m[1]) || 0) * 3600 + (Number(m[2]) || 0) * 60 +
        Number(m[3])) * 1000);
    }
    m = /^(\d+(?:\.\d+)?)s$/.exec(text);
    if (m) return Math.round(Number(m[1]) * 1000);
    m = /^(\d+(?:\.\d+)?)ms$/.exec(text);
    if (m) return Math.round(Number(m[1]));
    m = /^(\d+(?:\.\d+)?)h$/.exec(text);
    if (m) return Math.round(Number(m[1]) * 3600000);
    return 0;
  };
  const cues = [];
  const walk = (node) => {
    for (const child of node.children || []) {
      if (child.name === "p") {
        const begin = toMs(at(child, "begin"));
        const dur = toMs(at(child, "dur"));
        const end = toMs(at(child, "end")) || (dur ? begin + dur : 0);
        const text = textOf(child).replace(/[ \t]+/g, " ").split("\n")
          .map(line => line.trim()).filter(Boolean).join("\n");
        if (text) {
          /* a cue with no end holds for four seconds rather than being dropped */
          const stop = end > begin ? end : begin + 4000;
          cues.push(`${formatTimestamp(begin)} --> ${formatTimestamp(stop)}\n${text}`);
        }
        continue;
      }
      walk(child);
    }
  };
  walk(tt);
  return stitchVtt(cues);
}

/** Convert one fetched body to WebVTT, according to what it actually is. */
function toWebVtt(text, format) {
  if (format === "ttml" || looksLikeTtml(text)) {
    const converted = ttmlToVtt(text);
    if (converted === null) throw new Error("the TTML document could not be parsed");
    return converted;
  }
  const clean = String(text).replace(/\r/g, "");
  return /^WEBVTT/.test(clean.trimStart()) ? clean : "WEBVTT\n\n" + clean;
}

/**
 * Fetch a DASH rendition addressed as dash:<manifest>#<id>: re-read the
 * manifest, walk the segments, shift each by its own start time, stitch.
 */
export async function fetchDashTrack({ manifestUrl, id, headers, signal }) {
  const xml = await fetchText(manifestUrl, headers, signal);
  const all = renditionsFromMpd(xml, manifestUrl);
  const rendition = all.find(r => r.id === id) ||
    all.find(r => r.url === dashRef(manifestUrl, id)) ||
    all.find(r => String(r.url).indexOf(`#${encodeURIComponent(String(id))}`) > 0);
  if (!rendition) throw new Error(`representation ${id} is not in this manifest`);
  if (rendition.unsupported) throw new Error(rendition.unsupported);
  if (!rendition.segments || !rendition.segments.length) {
    const body = await fetchText(rendition.url, headers, signal);
    return { text: toWebVtt(body, rendition.format), segments: 1, bytesIn: body.length };
  }
  if (rendition.segments.length > MAX_SEGMENTS) {
    throw new Error(`representation ${id} has ${rendition.segments.length} segments ` +
                    `(limit ${MAX_SEGMENTS})`);
  }
  const bodies = [];
  let bytesIn = 0;
  for (const segment of rendition.segments) {
    const body = await fetchText(segment.url, headers, signal);
    bytesIn += body.length;
    /* DASH text segments carry cue times relative to the segment start, like
       HLS segments do, so each is shifted by its position in the timeline */
    bodies.push(shiftSegment(toWebVtt(body, rendition.format), segment.startMs));
  }
  return { text: stitchVtt(bodies), segments: bodies.length, bytesIn };
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
