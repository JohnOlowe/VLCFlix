'use strict';

/**
 * Sidecar subtitles and SRT -> WebVTT conversion.
 *
 * The conversion matters because <track> silently ignores anything that is not
 * WebVTT: a broken conversion shows no error, just subtitles that never appear.
 */

const test = require('node:test');
const assert = require('node:assert/strict');
const { toVtt, findSubtitles } = require('../server/library');

const srt = [
  '1',
  '00:00:01,000 --> 00:00:03,500',
  'Opening line',
  '',
  '2',
  '00:00:04,000 --> 00:00:06,000',
  'Second <i>line</i>',
  '',
].join('\r\n');

test('serves a WebVTT header, which <track> requires', () => {
  assert.ok(toVtt(srt).startsWith('WEBVTT\n\n'));
});

test('rewrites comma decimals to dot decimals', () => {
  const vtt = toVtt(srt);
  assert.match(vtt, /00:00:01\.000 --> 00:00:03\.500/);
  assert.doesNotMatch(vtt, /\d,\d{3}/);
});

test('keeps cues separated by a blank line', () => {
  // Regression: a cue-number regex using \s* swallowed the blank line between
  // cues, merging them into one unreadable block.
  const vtt = toVtt(srt);
  assert.match(vtt, /Opening line\n\n00:00:04\.000/);
});

test('drops SRT cue numbers and keeps the text', () => {
  const vtt = toVtt(srt);
  assert.match(vtt, /Second <i>line<\/i>/);
  assert.doesNotMatch(vtt, /^\s*1\s*$/m);
});

test('strips SRT-only position hints', () => {
  const withHints = '1\n00:00:01,000 --> 00:00:02,000 X1:120 X2:400 Y1:10 Y2:40\nHi\n';
  const vtt = toVtt(withHints);
  assert.match(vtt, /00:00:01\.000 --> 00:00:02\.000\n/);
  assert.doesNotMatch(vtt, /X1:/);
});

test('passes an existing VTT file through unchanged', () => {
  const vtt = 'WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nHello\n';
  assert.equal(toVtt(vtt), vtt);
});

test('strips a UTF-8 BOM', () => {
  assert.ok(toVtt('\uFEFF' + srt).startsWith('WEBVTT'));
});

test('accepts a Buffer, as read from disk', () => {
  assert.ok(toVtt(Buffer.from(srt, 'utf8')).startsWith('WEBVTT'));
});

test('finds Movie.srt, Movie.en.srt and Movie.forced.srt beside the video', () => {
  const entries = ['Movie.mkv', 'Movie.srt', 'Movie.en.srt', 'Movie.forced.srt'];
  const tracks = findSubtitles('/videos/Movie.mkv', entries);
  assert.deepEqual(tracks.map((t) => t.name).sort(),
    ['Movie.en.srt', 'Movie.forced.srt', 'Movie.srt']);
});

test('ignores subtitles belonging to a different film', () => {
  const entries = ['Movie.mkv', 'Movie.srt', 'Other.srt', 'Movie-2.srt'];
  const names = findSubtitles('/videos/Movie.mkv', entries).map((t) => t.name);
  assert.deepEqual(names, ['Movie.srt']);
});

test('labels the bare sidecar Default and sorts it first', () => {
  const tracks = findSubtitles('/v/Movie.mkv', ['Movie.en.srt', 'Movie.srt']);
  assert.equal(tracks[0].label, 'Default');
  assert.equal(tracks[0].name, 'Movie.srt');
});

test('names known languages and tags them', () => {
  const tracks = findSubtitles('/v/Movie.mkv', ['Movie.en.srt', 'Movie.fr.srt', 'Movie.yoruba.srt']);
  const byName = Object.fromEntries(tracks.map((t) => [t.name, t]));
  assert.equal(byName['Movie.en.srt'].label, 'English');
  assert.equal(byName['Movie.en.srt'].lang, 'en');
  assert.equal(byName['Movie.fr.srt'].label, 'French');
  assert.equal(byName['Movie.yoruba.srt'].label, 'Yoruba');
});

test('picks up .vtt sidecars too', () => {
  const tracks = findSubtitles('/v/Movie.mkv', ['Movie.en.vtt']);
  assert.equal(tracks.length, 1);
  assert.equal(tracks[0].ext, 'vtt');
});

test('a video with no sidecars reports no tracks', () => {
  assert.deepEqual(findSubtitles('/v/Movie.mkv', ['Movie.mkv', 'notes.txt']), []);
});

const { previewSegments } = require('../server/library');

test('preview samples several different parts of the film', () => {
  const segments = previewSegments(600);
  assert.equal(segments.length, 5);
  const starts = segments.map((s) => s.start);
  // Spread out, strictly increasing: one moment repeated five times is not a preview.
  for (let i = 1; i < starts.length; i += 1) assert.ok(starts[i] > starts[i - 1]);
  assert.ok(starts[0] > 0, 'should not open on the studio logo');
  assert.ok(starts[starts.length - 1] > 600 * 0.8, 'should reach the last act');
});

test('preview never runs past the end of the film', () => {
  for (const duration of [3, 16, 60, 5400]) {
    for (const segment of previewSegments(duration)) {
      assert.ok(segment.start + segment.length <= duration + 0.001,
        `segment ${segment.start}+${segment.length} overruns a ${duration}s film`);
    }
  }
});

test('a very short clip does not repeat the same frames', () => {
  const segments = previewSegments(1);
  assert.ok(segments.length <= 2);
  const starts = segments.map((s) => s.start);
  assert.equal(new Set(starts).size, starts.length);
});

test('an unreadable or empty file yields no preview', () => {
  assert.deepEqual(previewSegments(0), []);
  assert.deepEqual(previewSegments(NaN), []);
});
