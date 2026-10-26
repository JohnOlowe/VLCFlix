'use strict';

/**
 * Builds a small demo library so VLCFlix has something to show on a fresh
 * clone. Each clip is procedural "footage" rendered by ffmpeg from gradient
 * sources, grain and camera drift -- no licensed material, no downloads.
 *
 * Writes samples/<slug>.mp4 plus a <slug>.json sidecar describing the fake
 * production. The scanner reads those sidecars to populate the catalog.
 *
 *   npm run demo
 */

const fs = require('node:fs');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');

const run = promisify(execFile);

const OUT_DIR = path.join(__dirname, '..', 'samples');
const W = 1280;
const H = 720;
const GEN_W = 1600; // rendered wide, then cropped for camera drift
const GEN_H = 900;
const SECONDS = 16;

const TITLES = [
  {
    slug: 'neon-divide',
    title: 'Neon Divide',
    year: 2024,
    rating: 'TV-MA',
    genres: ['Sci-Fi', 'Thriller'],
    accent: '#ff2d6f',
    synopsis:
      'In a city split down the middle by a wall of light, a courier smuggles memories across the border and discovers her own were sold years ago.',
    colors: ['0x12002b', '0xff2d6f', '0x00d4ff'],
    speed: 0.009,
    drift: 70,
    grade: 'eq=contrast=1.14:saturation=1.22',
  },
  {
    slug: 'the-silent-coast',
    title: 'The Silent Coast',
    year: 2021,
    rating: 'TV-14',
    genres: ['Drama'],
    accent: '#2e8b7a',
    synopsis:
      'A lighthouse keeper on a coast that has been losing its shoreline for forty years begins to record the sea, one tape at a time.',
    colors: ['0x04181c', '0x2b7a72', '0xc0cfa6'],
    speed: 0.006,
    drift: 45,
    grade: 'eq=contrast=1.12:saturation=1.04',
  },
  {
    slug: 'iron-meridian',
    title: 'Iron Meridian',
    year: 2023,
    rating: 'TV-MA',
    genres: ['Action', 'Adventure'],
    accent: '#ff6b1a',
    synopsis:
      'A decommissioned freight line becomes the last route out when the northern grid collapses, and one engineer refuses to stop hauling passengers.',
    colors: ['0x1a0f05', '0xff6b1a', '0x8a4b2a'],
    speed: 0.014,
    drift: 90,
    grade: 'eq=contrast=1.2:saturation=1.1',
  },
  {
    slug: 'ghostlight',
    title: 'Ghostlight',
    year: 2022,
    rating: 'TV-MA',
    genres: ['Thriller', 'Mystery'],
    accent: '#35ff9e',
    synopsis:
      'Every night at 3:07 the stage lights come up in a theatre that has been condemned for a decade. The caretaker has stopped asking why.',
    colors: ['0x0b0416', '0x6a2cb8', '0x35ff9e'],
    speed: 0.0075,
    drift: 60,
    grade: 'eq=contrast=1.18:saturation=1.05:gamma=0.96',
  },
  {
    slug: 'salt-and-static',
    title: 'Salt & Static',
    year: 2025,
    rating: 'TV-14',
    genres: ['Mystery', 'Drama'],
    accent: '#c62828',
    synopsis:
      'A late-night radio host takes a call from a listener describing tomorrow\u2019s weather with impossible accuracy. Then the caller names her.',
    colors: ['0x101014', '0xc62828', '0x9aa5b1'],
    speed: 0.011,
    drift: 50,
    grade: 'eq=contrast=1.22:saturation=0.82',
  },
  {
    slug: 'orbital-drift',
    title: 'Orbital Drift',
    year: 2020,
    rating: 'TV-PG',
    genres: ['Sci-Fi', 'Documentary'],
    accent: '#1e6fd9',
    synopsis:
      'Six months aboard a station nobody is allowed to name, told entirely through maintenance logs and the sound of the hull at night.',
    colors: ['0x010913', '0x1e6fd9', '0xcfe9ff'],
    speed: 0.005,
    drift: 38,
    grade: 'eq=contrast=1.1:saturation=1.15:brightness=0.01',
  },
  {
    slug: 'the-long-harrow',
    title: 'The Long Harrow',
    year: 2019,
    rating: 'TV-MA',
    genres: ['Western', 'Drama'],
    accent: '#b5651d',
    synopsis:
      'Two brothers split a failing ranch down the fence line and spend eleven years proving which of them was right about the water.',
    colors: ['0x1c1108', '0xb5651d', '0xd9b382'],
    speed: 0.0045,
    drift: 42,
    grade: 'eq=contrast=1.12:saturation=0.9:gamma=1.03',
  },
  {
    slug: 'paper-cities',
    title: 'Paper Cities',
    year: 2024,
    rating: 'TV-PG',
    genres: ['Documentary'],
    accent: '#d4a017',
    synopsis:
      'The architects of a boom town that was never built explain, three decades later, what they thought they were drawing.',
    colors: ['0x140e08', '0xbe8c18', '0x6b707a'],
    speed: 0.008,
    drift: 55,
    grade: 'eq=contrast=1.16:saturation=0.98',
  },
];

function filterFor(spec) {
  // Crop offsets are the TOP-LEFT of the window, so the usable range is
  // (GEN - OUT). Going beyond it gets silently clamped and every frame ends up
  // pinned in the same corner -- which is exactly how an earlier version of
  // this script rendered the whole library almost black.
  const maxX = (GEN_W - W) / 2;
  const maxY = (GEN_H - H) / 2;
  const driftX = Math.min(maxX, spec.drift * 0.42);
  const driftY = Math.min(maxY, spec.drift * 0.3);

  return [
    `[0:v][1:v]blend=all_mode=softlight:all_opacity=0.42[base]`,
    `[base]noise=alls=9:allf=t+u,vignette=angle=PI/5.4,${spec.grade}[graded]`,
    `[graded]crop=${W}:${H}:x='${maxX}+${driftX.toFixed(1)}*sin(t/7)':y='${maxY}+${driftY.toFixed(1)}*cos(t/9)'[v]`,
  ].join(';');
}

async function renderTitle(spec) {
  const [c0, c1, c2] = spec.colors;
  const out = path.join(OUT_DIR, `${spec.slug}.mp4`);
  const args = [
    '-y', '-loglevel', 'error',
    '-f', 'lavfi',
    '-i', `gradients=s=${GEN_W}x${GEN_H}:c0=${c0}:c1=${c1}:c2=${c2}:n=3:speed=${spec.speed}:d=${SECONDS}:r=30`,
    '-f', 'lavfi',
    '-i', `gradients=s=${GEN_W}x${GEN_H}:c0=${c1}:c1=${c2}:n=2:speed=${spec.speed * 2.4}:d=${SECONDS}:r=30`,
    '-filter_complex', filterFor(spec),
    '-map', '[v]',
    '-t', String(SECONDS),
    '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '31',
    '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
    out,
  ];
  await run('ffmpeg', args, { timeout: 240000, maxBuffer: 1 << 22 });

  const sidecar = {
    title: spec.title,
    year: spec.year,
    rating: spec.rating,
    genres: spec.genres,
    accent: spec.accent,
    synopsis: spec.synopsis,
    sample: true,
  };
  fs.writeFileSync(
    path.join(OUT_DIR, `${spec.slug}.json`),
    JSON.stringify(sidecar, null, 2)
  );
  return out;
}

async function main() {
  fs.mkdirSync(OUT_DIR, { recursive: true });
  const only = process.argv.slice(2);
  const wanted = only.length ? TITLES.filter((t) => only.includes(t.slug)) : TITLES;
  if (!wanted.length) {
    console.error(`No matching slugs. Known: ${TITLES.map((t) => t.slug).join(', ')}`);
    process.exit(1);
  }
  let total = 0;
  for (const spec of wanted) {
    process.stdout.write(`rendering ${spec.title}... `);
    const out = await renderTitle(spec);
    const { size } = fs.statSync(out);
    total += size;
    console.log(`${(size / 1024 / 1024).toFixed(1)} MB`);
  }
  console.log(`\n${wanted.length} clip(s) in ${OUT_DIR} — ${(total / 1024 / 1024).toFixed(1)} MB total`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});

module.exports = { TITLES };
