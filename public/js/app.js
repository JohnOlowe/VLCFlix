/* =========================================================================
   VLCFlix client
   - Renders the Netflix-style browsing experience
   - Plays through the real VLC (via the server's HTTP control bridge)
     or through the built-in player
   ========================================================================= */

const state = {
  settings: null,
  items: [],
  rails: [],
  myList: [],
  progress: {},
  scan: null,
  vlc: null,
  tools: null,
  isDemo: false,
  view: 'home',
  query: '',
  heroId: null,
  system: null,
  scanPoll: null,
  remotePoll: null,
  playSession: null, // { id, mode }
};

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

/* ------------------------------- helpers -------------------------------- */

const SVG = (name, cls = '') => {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  if (cls) svg.setAttribute('class', cls);
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', `#${name}`);
  svg.appendChild(use);
  return svg;
};

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (value == null || value === false) continue;
    if (key === 'class') node.className = value;
    else if (key === 'text') node.textContent = value;
    else if (key === 'html') node.innerHTML = value;
    else if (key.startsWith('on') && typeof value === 'function') {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else if (key === 'dataset') Object.assign(node.dataset, value);
    else node.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat()) {
    if (child == null || child === false) continue;
    node.append(child.nodeType ? child : document.createTextNode(String(child)));
  }
  return node;
}

function formatDuration(seconds) {
  if (!seconds || seconds < 30) return null;
  const total = Math.round(seconds / 60);
  const h = Math.floor(total / 60);
  const m = total % 60;
  return h ? `${h}h ${m}m` : `${m}m`;
}

function formatTime(seconds) {
  const s = Math.max(0, Math.floor(seconds || 0));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  const pad = (n) => String(n).padStart(2, '0');
  return h ? `${h}:${pad(m)}:${pad(sec)}` : `${m}:${pad(sec)}`;
}

function formatSize(bytes) {
  if (!bytes) return '—';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  let value = bytes;
  while (value >= 1024 && i < units.length - 1) {
    value /= 1024;
    i += 1;
  }
  return `${value.toFixed(i > 1 ? 1 : 0)} ${units[i]}`;
}

function resolutionOf(item) {
  if (!item.height) return null;
  if (item.height >= 2000) return '4K';
  if (item.height >= 1000) return '1080p';
  if (item.height >= 700) return '720p';
  return `${item.height}p`;
}

/** Deterministic hue pair per title, so fallback artwork is stable. */
function huesFor(text) {
  let hash = 0;
  for (let i = 0; i < text.length; i += 1) {
    hash = (hash * 31 + text.charCodeAt(i)) % 360;
  }
  return [`hsl(${hash} 55% 26%)`, `hsl(${(hash + 68) % 360} 60% 18%)`];
}

async function api(path, options = {}) {
  const res = await fetch(path, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
    body: options.body ? JSON.stringify(options.body) : undefined,
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const error = new Error(data.error || `Request failed (${res.status})`);
    Object.assign(error, data, { status: res.status });
    throw error;
  }
  return data;
}

let toastTimer = null;
function toast(message, { error = false, action = null, actionLabel = '' } = {}) {
  const node = $('#toast');
  node.innerHTML = '';
  node.classList.toggle('is-error', error);
  node.append(el('span', { text: message }));
  if (action) {
    node.append(
      el('button', {
        class: 'toast__action',
        text: actionLabel,
        onclick: () => { hideToast(); action(); },
      })
    );
  }
  node.hidden = false;
  requestAnimationFrame(() => node.classList.add('is-on'));
  clearTimeout(toastTimer);
  toastTimer = setTimeout(hideToast, action ? 7000 : 3200);
}

function hideToast() {
  const node = $('#toast');
  node.classList.remove('is-on');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { node.hidden = true; }, 240);
}

/* ------------------------------ bootstrap ------------------------------- */

loadSubPrefs();

async function boot() {
  const [data, system] = await Promise.all([api('/api/bootstrap'), api('/api/system')]);
  Object.assign(state, {
    settings: data.settings,
    items: data.library.items,
    rails: data.library.rails,
    isDemo: data.library.isDemo,
    scan: data.scan,
    vlc: data.vlc,
    tools: data.library.tools,
    system,
  });
  state.myList = data.library.items.filter((i) => i.inMyList).map((i) => i.id);
  state.progress = Object.fromEntries(
    data.library.items.filter((i) => i.progress).map((i) => [i.id, i.progress])
  );

  paintEngine();
  paintFooter();
  route();
  // A scan already in progress on load shows in the header; forcing the settings
  // modal open would steal the view the user actually opened the app for.
  if (state.scan?.running) watchScan();
}

async function refreshCatalog({ keepView = true } = {}) {
  const data = await api('/api/catalog');
  state.items = data.items;
  state.myList = data.items.filter((i) => i.inMyList).map((i) => i.id);
  state.progress = Object.fromEntries(
    data.items.filter((i) => i.progress).map((i) => [i.id, i.progress])
  );
  state.tools = data.tools;
  const boot = await api('/api/bootstrap');
  state.rails = boot.library.rails;
  state.vlc = boot.vlc;
  paintEngine();
  paintFooter();
  if (keepView && state.view === 'home') renderHome();
  if (state.view === 'mylist') renderMyList();
  if (state.view === 'browse') renderBrowse();
}

function paintEngine() {
  const dot = $('#engineDot');
  const label = $('#engineLabel');
  const mode = state.settings?.playMode || 'vlc';
  dot.className = 'engine__dot';
  if (mode === 'ffmpeg') {
    label.textContent = 'FFMPEG';
    dot.classList.add('is-live');
    $('#engineBtn').title = 'Playback goes through ffmpeg (hardware decode when available)';
  } else if (mode === 'browser') {
    label.textContent = 'BROWSER';
    dot.classList.add('is-warn');
    $('#engineBtn').title = 'Playback uses the built-in player';
  } else if (state.vlc?.binary) {
    label.textContent = state.vlc.running ? 'VLC LIVE' : 'VLC';
    dot.classList.add(state.vlc.running ? 'is-live' : 'is-warn');
    $('#engineBtn').title = state.vlc.running
      ? 'VLC is running and controllable'
      : 'VLC found — it starts when you press play';
  } else {
    label.textContent = 'NO VLC';
    dot.classList.add('is-off');
    $('#engineBtn').title = 'VLC not found — the built-in player will be used';
  }
}

function paintFooter() {
  const count = state.items.length;
  const parts = [
    `${count} title${count === 1 ? '' : 's'} in library`,
    state.tools?.ffmpeg ? 'ffmpeg artwork: on' : 'ffmpeg artwork: off',
    state.vlc?.binary ? `VLC: ${state.vlc.binary}` : 'VLC: not detected',
  ];
  if (state.scan?.finishedAt) {
    parts.push(`last scan ${new Date(state.scan.finishedAt).toLocaleTimeString()}`);
  }
  $('#footerMeta').textContent = parts.join(' · ');
}

/* -------------------------------- hero ---------------------------------- */

function pickHero() {
  const withArt = state.items.filter((i) => i.backdropUrl);
  const pool = withArt.length ? withArt : state.items;
  if (!pool.length) return null;
  if (state.heroId) {
    const pinned = pool.find((i) => i.id === state.heroId);
    if (pinned) return pinned;
  }
  return [...pool].sort((a, b) => b.addedAt - a.addedAt)[0];
}

function renderHero() {
  const hero = $('#hero');
  hero.innerHTML = '';
  const item = pickHero();
  state.heroId = item?.id || null;

  if (!item) {
    hero.style.display = 'none';
    return;
  }
  hero.style.display = '';

  if (item.backdropUrl) {
    const media = el('div', { class: 'hero__media' });
    media.style.backgroundImage = `url("${item.backdropUrl}")`;
    hero.append(media);
  } else {
    const [c1, c2] = huesFor(item.title);
    const media = el('div', { class: 'hero__media hero__media--fallback' });
    media.style.setProperty('--accent', item.accent || c1);
    media.style.setProperty('--accent2', c2);
    hero.append(media);
  }

  hero.append(el('div', { class: 'hero__scrim' }));

  const isNew = Date.now() - item.addedAt < 1000 * 60 * 60 * 24 * 14;
  const meta = el('div', { class: 'hero__meta' });
  if (item.rating) meta.append(el('span', { class: 'hero__rating', text: item.rating }));
  if (isNew) meta.append(el('span', { class: 'hero__badge', text: 'New' }));
  if (item.year) meta.append(el('span', { text: String(item.year) }));
  const duration = formatDuration(item.duration);
  if (duration) meta.append(el('span', { class: 'hero__dot', text: '•' }), el('span', { text: duration }));
  const res = resolutionOf(item);
  if (res) meta.append(el('span', { class: 'hero__dot', text: '•' }), el('span', { text: res }));
  if (item.genres?.length) {
    meta.append(el('span', { class: 'hero__dot', text: '•' }));
    meta.append(el('span', { text: item.genres.join(' · ') }));
  }

  const actions = el('div', { class: 'hero__actions' });
  actions.append(
    el('button', { class: 'btn btn--primary', type: 'button', onclick: () => playItem(item.id) },
      SVG('i-play'), el('span', { text: item.progress ? 'Resume' : 'Play' })),
    el('button', { class: 'btn btn--secondary', type: 'button', onclick: () => openDetail(item.id) },
      SVG('i-info'), el('span', { text: 'More Info' })),
    el('button', {
      class: 'btn btn--round',
      type: 'button',
      title: item.inMyList ? 'Remove from My List' : 'Add to My List',
      'aria-label': item.inMyList ? 'Remove from My List' : 'Add to My List',
      onclick: (event) => toggleMyList(item.id, event.currentTarget),
    }, SVG(item.inMyList ? 'i-check' : 'i-plus'))
  );

  hero.append(
    el('div', { class: 'hero__content' },
      el('p', { class: 'hero__flag', text: isNew ? 'Added to your library' : 'Featured in your library' }),
      el('h1', { class: 'hero__title', text: item.title }),
      meta,
      item.synopsis
        ? el('p', { class: 'hero__synopsis', text: item.synopsis })
        : el('p', { class: 'hero__synopsis', text: `${item.filename} · ${formatSize(item.size)}` }),
      actions
    )
  );

  if (item.previewUrl) attachHeroPreview(hero, item);
}

/** Cross-fade into the muted preview clip after a short idle beat. */
function attachHeroPreview(hero, item, { delay = 3800 } = {}) {
  const video = el('video', {
    class: 'hero__video',
    loop: true,
    playsinline: true,
    preload: 'auto',
  });
  // The `muted` content attribute only sets defaultMuted; the property is what
  // the autoplay policy checks. Without this the clip is silently refused and
  // the panel keeps showing the still.
  video.muted = true;
  video.src = item.previewUrl;
  hero.insertBefore(video, hero.firstChild);

  let muted = true;
  const muteBtn = el('button', {
    class: 'hero__mute',
    type: 'button',
    'aria-label': 'Toggle preview sound',
    title: 'Toggle preview sound',
  }, SVG('i-muted'));
  muteBtn.addEventListener('click', () => {
    muted = !muted;
    video.muted = muted;
    muteBtn.replaceChildren(SVG(muted ? 'i-muted' : 'i-volume'));
  });

  const start = () => {
    if (document.hidden) return;
    video.play().then(() => {
      video.classList.add('is-on');
      hero.append(muteBtn);
    }).catch(() => { /* autoplay blocked — still image is fine */ });
  };

  video.addEventListener('canplay', () => setTimeout(start, delay), { once: true });
  // Safety net: `canplay` does not fire at all on some containers until playback
  // is requested. play() is idempotent, so calling it twice is harmless.
  setTimeout(start, delay + 1500);
  video.load();
  // Stop the preview when the hero scrolls out of view.
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      if (!entry.isIntersecting) {
        video.pause();
        video.classList.remove('is-on');
        muteBtn.remove();
      }
    }
  }, { threshold: 0.15 });
  observer.observe(hero);
}

/* -------------------------------- cards --------------------------------- */

let activePreview = null;

function cardEl(item, { poster = false } = {}) {
  const card = el('button', {
    class: 'card',
    type: 'button',
    dataset: { id: item.id },
    'aria-label': `${item.title}${item.year ? ` (${item.year})` : ''}`,
  });

  if (poster) card.classList.add('card--poster');
  const src = poster ? item.posterUrl || item.backdropUrl : item.backdropUrl || item.posterUrl;
  if (src) {
    card.append(el('img', { class: 'card__img', src, alt: '', loading: 'lazy', decoding: 'async' }));
  } else {
    const [c1, c2] = huesFor(item.title);
    const fallback = el('div', { class: 'card__fallback' }, el('span', { text: item.title }));
    fallback.style.setProperty('--c1', item.accent || c1);
    fallback.style.setProperty('--c2', c2);
    card.append(fallback);
  }

  card.append(el('div', { class: 'card__top' }), el('div', { class: 'card__shade' }));

  const metaBits = [];
  if (item.year) metaBits.push(String(item.year));
  const dur = formatDuration(item.duration);
  if (dur) metaBits.push(dur);
  if (item.rating) metaBits.push(item.rating);

  card.append(el('div', { class: 'card__label' },
    el('div', { class: 'card__label-title', text: item.title }),
    metaBits.length
      ? el('div', { class: 'card__label-meta' },
          metaBits.map((bit, index) => el(index === 0 ? 'em' : 'span', { text: bit })),
          item.genres?.[0] ? el('span', { text: `· ${item.genres[0]}` }) : null)
      : null
  ));

  const hover = el('div', { class: 'card__hover' },
    el('div', { class: 'card__hover-actions' },
      el('button', {
        class: 'card__mini card__mini--play',
        type: 'button',
        'aria-label': `Play ${item.title}`,
        onclick: (event) => { event.stopPropagation(); closeDetail(); playItem(item.id); },
      }, SVG('i-play')),
      el('button', {
        class: 'card__mini',
        type: 'button',
        'aria-label': item.inMyList ? 'Remove from My List' : 'Add to My List',
        onclick: (event) => {
          event.stopPropagation();
          toggleMyList(item.id, event.currentTarget, event);
        },
      }, SVG(item.inMyList ? 'i-check' : 'i-plus')),
      el('button', {
        class: 'card__mini',
        type: 'button',
        'aria-label': `Details for ${item.title}`,
        onclick: (event) => { event.stopPropagation(); openDetail(item.id); },
      }, SVG('i-info'))
    )
  );
  card.append(hover);

  if (item.progress?.percent) {
    card.append(el('div', { class: 'card__progress', style: 'z-index:5' },
      el('span', { style: `width:${item.progress.percent}%` })));
  }

  if (item.previewUrl) wireHoverPreview(card, item);
  card.addEventListener('click', () => openDetail(item.id));
  return card;
}

function wireHoverPreview(card, item) {
  if (!window.matchMedia('(hover: hover) and (pointer: fine)').matches) return;
  let timer = null;
  let video = null;

  const stop = () => {
    clearTimeout(timer);
    if (video) { video.pause(); video.remove(); video = null; activePreview = null; }
  };

  card.addEventListener('mouseenter', () => {
    clearTimeout(timer);
    timer = setTimeout(() => {
      if (!card.matches(':hover')) return;
      if (activePreview && activePreview !== card) activePreview.dispatchEvent(new Event('mouseleave'));
      activePreview = card;
      video = el('video', { class: 'card__video', loop: true, playsinline: true, preload: 'auto' });
      video.muted = true; // attribute alone does not satisfy the autoplay policy
      video.src = item.previewUrl;
      card.append(video);
      video.play().then(() => video.classList.add('is-on')).catch(() => {});
    }, 560);
  });

  card.addEventListener('mouseleave', stop);
  card.addEventListener('blur', stop);
  card.addEventListener('focus', () => {
    if (!window.matchMedia('(hover: hover)').matches) return;
  });
}

/* -------------------------------- rails --------------------------------- */

const RAIL_SLOT = {};

function renderRails() {
  const host = $('#rails');
  host.innerHTML = '';
  const rails = state.rails || [];
  const visible = rails.filter((rail) => rail.items?.length);

  if (!visible.length) {
    host.hidden = true;
    return;
  }
  host.hidden = false;

  for (const rail of visible) {
    const railEl = el('section', { class: 'rail', dataset: { rail: rail.id } });
    const viewport = el('div', { class: 'rail__viewport' });
    const track = el('div', { class: 'rail__track' });

    for (const item of rail.items) track.append(cardEl(item));
    viewport.append(track);

    const left = el('button', { class: 'rail__arrow rail__arrow--left', type: 'button', 'aria-label': 'Scroll left' }, SVG('i-chevron-left'));
    const right = el('button', { class: 'rail__arrow rail__arrow--right', type: 'button', 'aria-label': 'Scroll right' }, SVG('i-chevron-right'));

    const page = () => Math.max(viewport.clientWidth * 0.82, 240);
    left.addEventListener('click', () => viewport.scrollBy({ left: -page(), behavior: 'smooth' }));
    right.addEventListener('click', () => viewport.scrollBy({ left: page(), behavior: 'smooth' }));

    const sync = () => {
      const max = viewport.scrollWidth - viewport.clientWidth;
      const scrollable = max > 8;
      railEl.classList.toggle('is-scrollable', scrollable);
      left.disabled = viewport.scrollLeft <= 4;
      right.disabled = viewport.scrollLeft >= max - 4;
    };
    viewport.addEventListener('scroll', sync, { passive: true });
    window.addEventListener('resize', sync);

    const head = el('div', { class: 'rail__head' },
      el('h2', { class: 'rail__title', text: rail.title }),
      el('button', {
        class: 'rail__more',
        type: 'button',
        text: 'Explore all ›',
        onclick: () => goSearch(rail.id === 'all' ? '' : ''),
      })
    );
    if (rail.id === 'continue') {
      head.append(el('button', {
        class: 'rail__more',
        type: 'button',
        text: 'Clear progress',
        onclick: async () => {
          await Promise.all(rail.items.map((i) => api('/api/progress', {
            method: 'POST',
            body: { id: i.id, position: 0, duration: 1 },
          })));
          await refreshCatalog();
          toast('Continue Watching cleared');
        },
      }));
    }

    railEl.append(head, left, viewport, right);
    host.append(railEl);
    requestAnimationFrame(sync);
  }
}

/* --------------------------------- views -------------------------------- */

function renderHome() {
  $('#rails').hidden = false;
  $('#browseView').hidden = true;
  $('#mylistView').hidden = true;
  $('#searchView').hidden = true;
  $('#main').hidden = false;
  renderHero();
  renderRails();
  renderEmptyState();
}

function renderEmptyState() {
  const empty = $('#emptyState');
  const hasItems = state.items.length > 0;
  empty.hidden = hasItems;
  $('#hero').hidden = !hasItems;
  if (hasItems) return;

  empty.innerHTML = '';
  empty.append(
    el('h2', { text: 'Your shelf is empty' }),
    el('p', {
      text: state.settings?.libraryFolders?.length
        ? 'The folders you selected contain no video files yet — or the last scan has not run. Try scanning again.'
        : 'Point VLCFlix at a folder of videos. It reads the files, generates artwork with ffmpeg, and lays everything out like a streaming service.',
    }),
    el('div', { class: 'row' },
      el('button', { class: 'btn btn--vlc', type: 'button', text: 'Choose library folders', onclick: () => openSettings() }),
      el('button', { class: 'btn btn--ghost', type: 'button', text: 'Load the demo library', onclick: loadDemo })
    )
  );
}

function renderGrid(container, items, { poster = true, emptyMessage } = {}) {
  container.innerHTML = '';
  if (!items.length) {
    container.append(el('p', { class: 'view__sub', text: emptyMessage || 'Nothing here yet.' }));
    return;
  }
  for (const item of items) container.append(cardEl(item, { poster }));
}

function renderBrowse() {
  hideHome();
  $('#browseView').hidden = false;
  $('#browseCount').textContent = `${state.items.length} title${state.items.length === 1 ? '' : 's'} in ${state.settings?.libraryFolders?.length || 0} folder(s)`;
  renderGrid($('#browseGrid'), state.items);
}

function renderMyList() {
  hideHome();
  $('#mylistView').hidden = false;
  const items = state.items.filter((i) => i.inMyList);
  $('#mylistCount').textContent = items.length
    ? `${items.length} saved title${items.length === 1 ? '' : 's'}`
    : 'Titles you save appear here.';
  renderGrid($('#mylistGrid'), items, { emptyMessage: 'Nothing saved yet — hover a card and press +.' });
}

function renderSearch(query) {
  hideHome();
  $('#searchView').hidden = false;
  const q = query.trim().toLowerCase();
  $('#searchTitle').textContent = q ? `Results for “${query.trim()}”` : 'Search';
  const items = !q ? [] : state.items.filter((item) => {
    const haystack = [item.title, item.filename, String(item.year || ''), ...(item.genres || [])]
      .join(' ')
      .toLowerCase();
    return q.split(/\s+/).every((token) => haystack.includes(token));
  });
  items.sort((a, b) => (b.title.toLowerCase().startsWith(q) ? 1 : 0) - (a.title.toLowerCase().startsWith(q) ? 1 : 0));
  $('#searchCount').textContent = q
    ? `${items.length} match${items.length === 1 ? '' : 'es'}`
    : 'Type to search titles, genres and years.';
  renderGrid($('#searchGrid'), items, { emptyMessage: 'No titles matched. Try a different word.' });
}

function hideHome() {
  $('#hero').hidden = true;
  $('#rails').hidden = true;
  $('#emptyState').hidden = true;
}

/* -------------------------------- detail -------------------------------- */

function openDetail(id) {
  const item = state.items.find((i) => i.id === id);
  if (!item) return;
  const modal = $('#detailModal');
  const body = $('#detailBody');
  body.innerHTML = '';

  const hero = el('div', { class: 'detail__hero' });
  if (item.backdropUrl) {
    hero.style.backgroundImage = `url("${item.backdropUrl}")`;
  } else {
    const [c1, c2] = huesFor(item.title);
    hero.classList.add('detail__hero--fallback');
    hero.style.setProperty('--c1', item.accent || c1);
    hero.style.setProperty('--c2', c2);
  }

  // Same clip as the card hover, so opening Info continues the motion instead of
  // freezing on a still. The backdrop underneath is the poster frame.
  if (item.previewUrl) attachHeroPreview(hero, item, { delay: 500 });

  const meta = el('div', { class: 'detail__meta-line' });
  const pct = item.progress?.percent;
  if (pct) meta.append(el('span', { class: 'detail__match', text: `${pct}% watched` }));
  const duration = formatDuration(item.duration);
  if (duration) meta.append(el('span', { text: duration }));
  if (item.year) meta.append(el('span', { text: String(item.year) }));
  if (item.rating) meta.append(el('span', { class: 'hero__rating', text: item.rating }));
  const res = resolutionOf(item);
  if (res) meta.append(el('span', { text: res }));
  meta.append(el('span', { text: 'HD' }));

  const playBtn = el('button', { class: 'btn btn--primary', type: 'button', onclick: () => { closeDetail(); playItem(item.id); } },
    SVG('i-play'),
    el('span', { text: pct ? `Resume from ${formatTime(item.progress.position)}` : 'Play' }));

  const mode = state.settings?.playMode || 'vlc';
  const altBtn = el('button', {
    class: 'btn btn--ghost',
    type: 'button',
    onclick: () => { closeDetail(); playItem(item.id, mode === 'vlc' ? 'browser' : 'vlc'); },
  }, mode === 'vlc' ? el('span', { text: 'Play in browser' }) : el('span', { text: 'Play in VLC' }));

  const listBtn = el('button', {
    class: 'btn btn--round',
    type: 'button',
    'aria-label': item.inMyList ? 'Remove from My List' : 'Add to My List',
    title: item.inMyList ? 'Remove from My List' : 'Add to My List',
    onclick: (event) => toggleMyList(item.id, event.currentTarget, event),
  }, SVG(item.inMyList ? 'i-check' : 'i-plus'));

  hero.append(
    el('div', { class: 'detail__hero-scrim' }),
    el('button', { class: 'detail__close', type: 'button', 'aria-label': 'Close', onclick: closeDetail }, SVG('i-close')),
    el('div', { class: 'detail__hero-foot' },
      el('h2', { class: 'detail__title', id: 'detailTitle', text: item.title }),
      el('div', { class: 'detail__actions' }, playBtn, listBtn, altBtn)
    )
  );

  const info = el('dl');
  const push = (label, value) => {
    if (!value) return;
    info.append(el('dt', { text: label }), el('dd', { text: value }));
  };
  push('Genres', item.genres?.length ? item.genres.join(', ') : null);
  push('Added', new Date(item.addedAt).toLocaleDateString(undefined, { month: 'long', day: 'numeric', year: 'numeric' }));
  push('File size', formatSize(item.size));
  push('Resolution', item.width ? `${item.width} × ${item.height}` : null);

  body.append(hero, el('div', { class: 'detail__body' },
    el('div', {},
      meta,
      el('p', { class: 'detail__synopsis', text: item.synopsis || 'No synopsis. Add a sidecar JSON file next to the video with a "synopsis" field to describe it.' }),
      item.genres?.length
        ? el('p', { class: 'detail__synopsis' }, el('span', { style: 'color:var(--text-faint)', text: 'Genres: ' }), el('span', { text: item.genres.join(' · ') }))
        : null
    ),
    el('div', {}, info, el('p', { class: 'detail__tech', text: item.filename }))
  ));

  modal.hidden = false;
  document.body.classList.add('is-locked');
  modal.scrollTop = 0;
  $('.detail__close', modal)?.focus({ preventScroll: true });
}

function closeDetail() {
  const modal = $('#detailModal');
  if (modal.hidden) return;
  modal.hidden = true;
  document.body.classList.remove('is-locked');
}

/* ------------------------------ interactions ---------------------------- */

async function toggleMyList(id, button, event) {
  event?.stopPropagation();
  try {
    const { myList } = await api('/api/mylist', { method: 'POST', body: { id } });
    state.myList = myList;
    const item = state.items.find((i) => i.id === id);
    if (item) item.inMyList = myList.includes(id);

    // Update every on-screen control that represents this title.
    for (const node of $$(`.card[data-id="${id}"]`)) {
      const mini = node.querySelectorAll('.card__mini')[1];
      if (mini) mini.replaceChildren(SVG(item.inMyList ? 'i-check' : 'i-plus'));
    }
    if (button && button.classList.contains('btn--round')) {
      button.replaceChildren(SVG(item.inMyList ? 'i-check' : 'i-plus'));
    }
    const boot = await api('/api/bootstrap');
    state.rails = boot.library.rails;
    if (state.view === 'home') renderRails();
    if (state.view === 'mylist') renderMyList();
    toast(item.inMyList ? `Added “${item.title}” to My List` : `Removed “${item.title}”`);
  } catch (err) {
    toast(err.message, { error: true });
  }
}

async function playItem(id, modeOverride) {
  const item = state.items.find((i) => i.id === id);
  if (!item) return;
  const mode = modeOverride || state.settings?.playMode || 'vlc';
  const startAt = item.progress?.position || 0;

  try {
    const result = await api('/api/player/play', { method: 'POST', body: { id, mode, startAt } });
    state.playSession = { id, mode: result.mode };

    if (result.mode === 'browser') {
      openPlayer(item, result);
    } else {
      openRemote(item);
      toast(`Playing “${item.title}” in VLC`);
    }
  } catch (err) {
    if (err.code === 'VLC_NOT_FOUND' || err.code === 'VLC_UNREACHABLE' || err.status === 502 || err.status === 504) {
      // Never leave the user with a dead button: fall back to the built-in player.
      try {
        const fallback = await api('/api/player/play', { method: 'POST', body: { id, mode: 'browser', startAt } });
        state.playSession = { id, mode: 'browser' };
        openPlayer(item, fallback);
        toast(`${err.message} — using the built-in player instead`, { error: true });
      } catch (inner) {
        toast(inner.message, { error: true });
      }
      return;
    }
    toast(err.message, { error: true });
  }
}

/* --------------------------- built-in player ---------------------------- */

const player = {
  video: null,
  hideTimer: null,
  reportTimer: null,
  current: null,
  wasPlaying: false,
  engine: 'direct', // 'direct' streams the file; 'ffmpeg' pipes a transcoded stream
  streamBase: null,
  offset: 0,        // where the current ffmpeg stream started, in the file
};

/**
 * ffmpeg streams are pipes: there is no duration and nothing to scrub inside them.
 * Position is therefore (stream offset + elapsed) and a seek restarts the stream at
 * the new offset. Direct streams keep the browser's own timeline.
 */
function playerPosition() {
  const video = player.video;
  const elapsed = video && Number.isFinite(video.currentTime) ? video.currentTime : 0;
  return player.engine === 'ffmpeg' ? (player.offset || 0) + elapsed : elapsed;
}

function playerTotal() {
  const known = Number(player.current?.duration) || 0;
  if (player.engine === 'ffmpeg') return known;
  return player.video && Number.isFinite(player.video.duration) && player.video.duration > 0
    ? player.video.duration
    : known;
}

function restartStream(seconds) {
  const video = player.video;
  if (!video || !player.streamBase) return;
  const at = Math.max(0, Math.floor(seconds));
  player.offset = at;
  const wasPlaying = !video.paused;
  const url = `${player.streamBase}${player.streamBase.includes('?') ? '&' : '?'}t=${at}`;
  video.src = url;
  if (wasPlaying) video.play().catch(() => {});
}

function seekToSeconds(seconds) {
  const total = playerTotal();
  const clamped = Math.max(0, Math.min(seconds, total > 0 ? total - 1 : seconds));
  if (player.engine === 'ffmpeg') restartStream(clamped);
  else if (player.video && Number.isFinite(player.video.duration)) player.video.currentTime = clamped;
}

function openPlayer(item, session) {
  player.current = item;
  const root = $('#player');
  const video = $('#playerVideo');
  player.video = video;
  root.hidden = false;
  document.body.classList.add('is-locked');
  $('#playerTitle').textContent = item.title;

  player.engine = session.engine === 'ffmpeg' ? 'ffmpeg' : 'direct';
  player.streamBase = session.streamUrl || null;
  player.offset = 0;

  video.src = session.streamUrl;
  video.muted = false;
  video.volume = 1;
  $('#playerVol').value = '1';
  openSubPanel(false);
  sub.tracksLoaded = false;
  startSubtitles(item);

  const resumeAt = Number(session.seekTo) || 0;
  const onLoaded = () => {
    if (resumeAt > 2) video.currentTime = resumeAt;
    video.play().catch(() => toast('Press play to start (autoplay was blocked)'));
  };
  video.addEventListener('loadedmetadata', onLoaded, { once: true });

  root.classList.add('is-active');
  schedulePlayerHide();
  player.wasPlaying = true;

  clearInterval(player.reportTimer);
  player.reportTimer = setInterval(reportProgress, 5000);
  video.addEventListener('pause', reportProgress);
  video.addEventListener('ended', () => {
    reportProgress();
    closePlayer();
  });

  video.focus?.();
  updatePlayerToggle();
}

/* ------------------------------ subtitles ------------------------------ */

/**
 * Subtitles are drawn by hand rather than through <track>.
 *
 * A native <track> is opaque once loaded: its timing cannot be shifted, and ::cue
 * gives no usable control of size or vertical position. The cues are therefore
 * fetched as WebVTT, parsed here, and painted into an overlay, which makes the
 * three things a viewer actually needs -- delay, size, height -- plain state.
 */
const SUB_STEP_MS = 50;      // VLC's own step
const SUB_SIZE_STEP = 0.1;
const SUB_BOTTOM_STEP = 2;
const SUB_DEFAULTS = { delay: 0, size: 1, bottom: 8, audio: 0 };

const sub = {
  cues: [],
  trackName: null,
  tracks: [],
  ...SUB_DEFAULTS,
  raf: null,
  shown: null,
  panelOpen: false,
};

function loadSubPrefs() {
  try {
    const saved = JSON.parse(localStorage.getItem('vlcflix.subtitles') || '{}');
    Object.assign(sub, {
      delay: Number(saved.delay) || 0,
      size: Number(saved.size) || 1,
      bottom: Number(saved.bottom) || 8,
      audio: Number(saved.audio) || 0,
    });
  } catch { /* first run, or storage blocked */ }
}

function saveSubPrefs() {
  try {
    localStorage.setItem('vlcflix.subtitles',
      JSON.stringify({ delay: sub.delay, size: sub.size, bottom: sub.bottom, audio: sub.audio }));
  } catch { /* storage full or blocked; the session still works */ }
}

/** Parse WebVTT into {start, end, html}. Cue identifiers and settings are ignored. */
function parseVtt(text) {
  const cues = [];
  const stamp = '((?:\\d{1,2}:)?\\d{2}:\\d{2}\\.\\d{3})';
  const re = new RegExp(`${stamp}\\s*-->\\s*${stamp}`, 'g');
  const lines = text.replace(/\r\n?/g, '\n').split('\n');

  const seconds = (value) => {
    const parts = value.split(':').map(Number);
    const [h, m, s] = parts.length === 3 ? parts : [0, parts[0], parts[1]];
    return h * 3600 + m * 60 + s;
  };

  let current = null;
  let buffer = [];
  const flush = () => {
    if (current) {
      current.html = sanitizeCue(buffer.join('\n').trim());
      cues.push(current);
    }
    current = null;
    buffer = [];
  };

  for (const line of lines) {
    re.lastIndex = 0;
    const timing = re.exec(line);
    if (timing) {
      flush();
      current = { start: seconds(timing[1]), end: seconds(timing[2]), html: '' };
    } else if (line.trim() === '') {
      flush();
    } else if (current && !/^WEBVTT/.test(line)) {
      buffer.push(line);
    }
  }
  flush();
  return cues.filter((c) => c.end > c.start && c.html);
}

/**
 * Subtitle text is HTML-ish (<i> is used for lyrics and foreign dialogue), so the
 * markup is escaped and only the harmless inline tags are put back. A subtitle
 * file is untrusted input and is never injected raw.
 */
function sanitizeCue(text) {
  const escaped = text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
  return escaped.replace(/&lt;(\/?)(i|b|u|em|strong)&gt;/gi, '<$1$2>');
}

async function loadSubtitleTrack(url, name) {
  sub.trackName = name;
  stopSubtitleLoop();
  if (!url) {
    sub.cues = [];
    renderCues(0);
    return;
  }
  try {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    sub.cues = parseVtt(await response.text());
  } catch (err) {
    sub.cues = [];
    toast(`Could not load subtitles: ${err.message}`, { error: true });
  }
  applySubtitleStyle();
  startSubtitleLoop();
}

/** Media time the subtitles should be showing right now. */
function subtitleClock() {
  const video = player.video;
  if (!video) return 0;
  // Delayed audio means the sound lags the picture, so the captions that belong
  // to what is being *heard* are the ones from earlier in the file.
  const heard = video.currentTime - (sub.audio || 0);
  // sub.delay is milliseconds (VLC's unit and step); the media clock is seconds.
  return heard - (sub.delay || 0) / 1000;
}

function applySubtitleStyle() {
  const layer = $('#playerSubs');
  if (!layer) return;
  layer.style.setProperty('--sub-size', String(sub.size));
  layer.style.setProperty('--sub-bottom', `${sub.bottom}%`);
}

function renderCues(now) {
  const layer = $('#playerSubs');
  if (!layer) return;
  const active = sub.cues.filter((cue) => now >= cue.start && now < cue.end);
  const key = active.map((c) => c.start + c.html).join('|');
  if (key === sub.shown) return;       // no change: leave the DOM alone
  sub.shown = key;
  layer.replaceChildren(...active.map((cue) => {
    const node = document.createElement('div');
    node.className = 'cue';
    node.innerHTML = cue.html;
    return node;
  }));
}

function startSubtitleLoop() {
  stopSubtitleLoop();
  const tick = () => {
    renderCues(subtitleClock());
    sub.raf = requestAnimationFrame(tick);
  };
  sub.raf = requestAnimationFrame(tick);
}

function stopSubtitleLoop() {
  if (sub.raf) cancelAnimationFrame(sub.raf);
  sub.raf = null;
  sub.shown = null;
  const layer = $('#playerSubs');
  if (layer) layer.replaceChildren();
}

/* ---- audio delay: the browser can only ever delay sound, never advance it ---- */

let audioGraph = null;

function ensureAudioGraph() {
  const video = player.video;
  if (audioGraph || !video) return audioGraph;
  const Ctx = window.AudioContext || window.webkitAudioContext;
  if (!Ctx) return null;
  try {
    const ctx = new Ctx();
    const source = ctx.createMediaElementSource(video);
    const delay = ctx.createDelay(5);
    delay.delayTime.value = 0;
    source.connect(delay);
    delay.connect(ctx.destination);
    audioGraph = { ctx, delay };
  } catch {
    audioGraph = null; // some browsers refuse; audio sync is then unavailable
  }
  return audioGraph;
}

function applyAudioDelay() {
  const wanted = sub.audio;
  if (!wanted) {
    if (audioGraph) audioGraph.delay.delayTime.value = 0;
    return;
  }
  const graph = ensureAudioGraph();
  if (!graph) {
    sub.audio = 0;
    toast('Audio delay needs Web Audio, which this browser refused', { error: true });
    return;
  }
  if (graph.ctx.state === 'suspended') graph.ctx.resume().catch(() => {});
  graph.delay.delayTime.value = Math.max(0, wanted);
}

/* ---- the controls ---- */

function describeDelay(ms) {
  if (!ms) return '0 ms';
  return `${ms > 0 ? '+' : '−'}${Math.abs(ms)} ms`;
}

function paintSubPanel() {
  const delay = $('#subDelay');
  const size = $('#subSize');
  const bottom = $('#subBottom');
  const audio = $('#audioDelay');
  const cc = $('#playerCC');
  if (delay) delay.textContent = describeDelay(sub.delay);
  if (size) size.textContent = `${Math.round(sub.size * 100)}%`;
  if (bottom) bottom.textContent = `${sub.bottom}%`;
  if (audio) audio.textContent = describeDelay(Math.round(sub.audio * 1000));
  if (cc) {
    cc.hidden = false;
    cc.classList.toggle('is-on', Boolean(sub.trackName));
  }
}

function paintTrackList(force) {
  const host = $('#subPanelTracks');
  if (!host) return;
  if (force === 'reload') sub.tracksLoaded = false;
  if (sub.tracksLoaded && host.childElementCount) {
    for (const button of host.querySelectorAll('.subpanel__track')) {
      button.classList.toggle('is-on', (button.dataset.name || null) === sub.trackName);
    }
    return;
  }
  sub.tracksLoaded = true;
  host.replaceChildren();

  const off = el('button', {
    class: `subpanel__track${sub.trackName ? '' : ' is-on'}`,
    type: 'button',
    text: 'Off',
    onclick: () => chooseSubtitle(null),
  });
  host.append(off);

  if (!sub.tracks.length) {
    host.append(el('span', { class: 'subpanel__empty', text: 'No subtitle files beside this video' }));
    return;
  }
  for (const track of sub.tracks) {
    host.append(el('button', {
      class: `subpanel__track${track.name === sub.trackName ? ' is-on' : ''}${track.matches ? '' : ' subpanel__track--quiet'}`,
      type: 'button',
      title: track.name,
      dataset: { name: track.name },
      text: track.label,
      onclick: () => chooseSubtitle(track),
    }));
  }
}

/**
 * Pick up the subtitles a film should start with: the file chosen by hand for this
 * film if there is one, otherwise the track whose name matches the video.
 */
async function startSubtitles(item) {
  applySubtitleStyle();
  const first = (item.subtitles || [])[0];
  if (first) {
    await loadSubtitleTrack(first.url, first.name);
  } else {
    sub.trackName = null;
    sub.cues = [];
    stopSubtitleLoop();
  }
  applyAudioDelay();
  paintSubPanel();
  await loadTrackOptions(item);
}

/** Ask the server which subtitle files exist beside this film. */
async function loadTrackOptions(item) {
  try {
    const data = await api(`/api/subtitles/${item.id}/tracks`);
    sub.tracks = data.tracks || [];
  } catch {
    sub.tracks = (item.subtitles || []).map((t) => ({ name: t.name, label: t.label, url: t.url, matches: true }));
  }
  sub.tracksLoaded = false;
  paintTrackList();
}

async function chooseSubtitle(track) {
  const item = player.current;
  if (!item) return;
  if (!track) {
    sub.trackName = null;
    sub.cues = [];
    stopSubtitleLoop();
    paintSubPanel();
    paintTrackList();
    if (state.settings?.playMode !== 'vlc') {
      // Persist the explicit "off" as no override: the film goes back to its
      // name-matched default next time, which is what "Off" means here.
      api(`/api/subtitles/${item.id}/track`, { method: 'POST', body: { name: null } }).catch(() => {});
    }
    return;
  }
  await loadSubtitleTrack(track.url, track.name);
  paintSubPanel();
  paintTrackList();
  api(`/api/subtitles/${item.id}/track`, { method: 'POST', body: { name: track.name } })
    .catch(() => {}); // remembering the choice is a convenience, not a requirement
}

function openSubPanel(open) {
  const panel = $('#subPanel');
  if (!panel) return;
  sub.panelOpen = open ?? !sub.panelOpen;
  panel.hidden = !sub.panelOpen;
  if (sub.panelOpen) {
    paintSubPanel();
    paintTrackList();
  }
}

/** One entry point for every adjustment, so keys and buttons cannot disagree. */
function adjustSubtitle(kind, direction) {
  switch (kind) {
    case 'delay':
      sub.delay = Math.max(-60000, Math.min(60000, sub.delay + direction * SUB_STEP_MS));
      break;
    case 'size':
      sub.size = Math.max(0.5, Math.min(2.5, Math.round((sub.size + direction * SUB_SIZE_STEP) * 10) / 10));
      break;
    case 'bottom':
      sub.bottom = Math.max(0, Math.min(60, sub.bottom + direction * SUB_BOTTOM_STEP));
      break;
    case 'audio':
      // Only delaying sound is possible in a browser; pressing the "earlier" key
      // at zero has to say so rather than silently doing nothing.
      if (direction < 0 && sub.audio <= 0) {
        toast('Audio is at 0 ms — the browser can delay sound but not advance it. Use Real VLC for that direction.');
        return;
      }
      sub.audio = Math.max(0, Math.min(2, Math.round((sub.audio + direction * 0.05) * 100) / 100));
      applyAudioDelay();
      break;
    default:
      return;
  }
  applySubtitleStyle();
  paintSubPanel();
  saveSubPrefs();
  const label = {
    delay: `Subtitles ${describeDelay(sub.delay)}${sub.delay > 0 ? ' (later)' : sub.delay < 0 ? ' (earlier)' : ''}`,
    size: `Subtitle size ${Math.round(sub.size * 100)}%`,
    bottom: `Subtitles at ${sub.bottom}% from the bottom`,
    audio: `Audio ${describeDelay(Math.round(sub.audio * 1000))}`,
  }[kind];
  toast(label);
}

/** VLC's own bindings on Windows/Linux: G/H subtitles, J/K audio, V next track. */
function handleSubtitleKey(event) {
  if (!player.current) return;
  const target = event.target;
  if (target && /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName)) return;
  if (event.ctrlKey || event.altKey || event.metaKey) return;

  const key = event.key.toLowerCase();
  if (!['g', 'h', 'j', 'k', 'v'].includes(key)) return;
  event.preventDefault();

  if (key === 'g') adjustSubtitle('delay', -1);
  else if (key === 'h') adjustSubtitle('delay', 1);
  else if (key === 'j') adjustSubtitle('audio', -1);
  else if (key === 'k') adjustSubtitle('audio', 1);
  else if (key === 'v') cycleSubtitleTrack();
}

function cycleSubtitleTrack() {
  const options = [{ name: null }, ...sub.tracks];
  const index = options.findIndex((t) => (t.name || null) === sub.trackName);
  const next = options[(index + 1) % options.length];
  chooseSubtitle(next.name ? next : null);
}

function reportProgress() {
  if (!player.current || !player.video) return;
  const duration = playerTotal();
  const position = playerPosition();
  if (!Number.isFinite(duration) || duration <= 0) return;
  api('/api/progress', {
    method: 'POST',
    body: { id: player.current.id, position, duration },
  }).catch(() => {});
}

function closePlayer({ report = true } = {}) {
  const root = $('#player');
  const video = $('#playerVideo');
  if (report) reportProgress();
  clearInterval(player.reportTimer);
  stopSubtitleLoop();
  openSubPanel(false);
  video.pause();
  video.removeAttribute('src');
  video.load();
  root.hidden = true;
  root.classList.remove('is-active', 'is-paused');
  document.body.classList.remove('is-locked');
  if (player.current) {
    refreshCatalog().catch(() => {});
    const finished = player.current;
    player.current = null;
    state.playSession = null;
    toast(`Stopped “${finished.title}”`);
  }
}

function updatePlayerToggle() {
  const video = player.video;
  if (!video) return;
  const root = $('#player');
  const paused = video.paused;
  root.classList.toggle('is-paused', paused);
  $('#playerToggle').replaceChildren(SVG(paused ? 'i-play' : 'i-pause'));
  const position = playerPosition();
  const total = playerTotal();
  $('#playerTime').textContent = `${formatTime(position)} / ${formatTime(total)}`;
  const ratio = total > 0 ? Math.min(1, position / total) : 0;
  const scrub = $('#playerScrub');
  scrub.value = String(Math.round(ratio * 1000));
  scrub.style.setProperty('--fill', `${ratio * 100}%`);
}

function schedulePlayerHide() {
  const root = $('#player');
  root.classList.add('is-active');
  clearTimeout(player.hideTimer);
  player.hideTimer = setTimeout(() => {
    if (!player.video?.paused) root.classList.remove('is-active');
  }, 2800);
}

/* ---------------------------- VLC remote dock --------------------------- */

function openRemote(item) {
  const root = $('#remote');
  root.hidden = false;
  $('#remoteTitle').textContent = item.title;
  $('#remoteArt').style.background = item.accent
    ? `linear-gradient(90deg, ${item.accent}, var(--vlc))`
    : 'linear-gradient(90deg, var(--vlc), var(--brand-red))';
  startRemotePolling();
}

function closeRemote() {
  $('#remote').hidden = true;
  clearInterval(state.remotePoll);
  state.remotePoll = null;
}

function startRemotePolling() {
  clearInterval(state.remotePoll);
  const tick = async () => {
    try {
      const data = await api('/api/player/status');
      if (!data.running || !data.status) {
        if (!$('#remote').hidden) $('#remoteTitle').textContent = 'VLC closed';
        return;
      }
      const s = data.status;
      const idle = !data.active;
      if (idle) $('#remoteTitle').textContent = 'Nothing playing in VLC';
      else if (data.title) $('#remoteTitle').textContent = data.title;
      $('#remoteScrub').disabled = idle;
      $('#remoteToggle').disabled = idle;
      $('#remoteBack').disabled = idle;
      $('#remoteFwd').disabled = idle;
      $('#remoteTime').textContent = `${formatTime(s.time)} / ${formatTime(s.length)}`;
      $('#remoteVol').textContent = `${s.volumePercent}%`;
      const scrub = $('#remoteScrub');
      if (document.activeElement !== scrub) {
        scrub.value = String(Math.round(s.position * 1000));
        scrub.style.setProperty('--fill', `${s.position * 100}%`);
      }
      const playing = s.state === 'playing';
      $('#remoteToggle').replaceChildren(SVG(playing ? 'i-pause' : 'i-play'));
      if (idle) {
        $('#remoteTime').textContent = '0:00 / 0:00';
        $('#remoteScrub').value = '0';
        $('#remoteScrub').style.setProperty('--fill', '0%');
        state.vlcPlaying = false;
        return;
      }
      state.vlcPlayback = s;
      state.vlcPlaying = playing;
    } catch {
      /* server restarting, or VLC gone; the next tick will recover */
    }
  };
  tick();
  state.remotePoll = setInterval(tick, 2000);
}

async function vlcCommand(action, value) {
  try {
    await api('/api/player/command', { method: 'POST', body: { action, value } });
    setTimeout(async () => {
      try {
        const data = await api('/api/player/status');
        if (data.status) {
          state.vlcPlayback = data.status;
          state.vlcPlaying = data.status.state === 'playing';
          $('#remoteToggle').replaceChildren(SVG(state.vlcPlaying ? 'i-pause' : 'i-play'));
          $('#remoteTime').textContent = `${formatTime(data.status.time)} / ${formatTime(data.status.length)}`;
        }
      } catch { /* ignore */ }
    }, 320);
  } catch (err) {
    toast(err.message, { error: true });
  }
}

/* ------------------------------- settings ------------------------------- */

async function openSettings() {
  const modal = $('#settingsModal');
  modal.hidden = false;
  document.body.classList.add('is-locked');
  await renderSettings();
}

function closeSettings() {
  $('#settingsModal').hidden = true;
  // The scan watcher deliberately keeps running: the scan belongs to the server,
  // and the header chip is the only sign it is alive once this panel is gone.

  if (!$('#detailModal').hidden || !$('#player').hidden) return;
  document.body.classList.remove('is-locked');
}

async function renderSettings() {
  const body = $('#settingsBody');
  const system = state.system || await api('/api/system');
  const settings = state.settings;
  body.innerHTML = '';

  /* library folders */
  const folderList = el('div', { class: 'folder-list' });
  const paintFolders = () => {
    folderList.innerHTML = '';
    const folders = state.settings.libraryFolders || [];
    if (!folders.length) {
      folderList.append(el('p', { class: 'field__hint', text: 'No folders yet. Add one below, or load the demo library.' }));
    }
    for (const folder of folders) {
      folderList.append(el('div', { class: 'folder-row' },
        SVG('i-folder'),
        el('span', { text: folder, title: folder }),
        el('button', {
          class: 'icon-btn icon-btn--sm',
          type: 'button',
          'aria-label': `Remove ${folder}`,
          onclick: async () => {
            const next = state.settings.libraryFolders.filter((f) => f !== folder);
            await saveSettings({ libraryFolders: next });
            paintFolders();
          },
        }, SVG('i-close'))
      ));
    }
  };
  paintFolders();

  const folderInput = el('input', {
    class: 'input',
    type: 'text',
    placeholder: 'e.g. D:\\Movies  or  /home/me/Videos',
    spellcheck: 'false',
  });

  const addFolder = async () => {
    const value = folderInput.value.trim();
    if (!value) return;
    const next = [...new Set([...(state.settings.libraryFolders || []), value])];
    await saveSettings({ libraryFolders: next });
    folderInput.value = '';
    paintFolders();
  };

  const scanStatus = el('div', { class: 'field__hint', id: 'scanStatus', text: '' });
  const scanBar = el('div', { class: 'scan-bar' }, el('span', { id: 'scanFill' }));

  const startScan = async () => {
    if (!(state.settings.libraryFolders || []).length) {
      toast('Add a folder first', { error: true });
      return;
    }
    $('#scanStatus').textContent = 'Starting scan…';
    try {
      await api('/api/library/scan', { method: 'POST' });
      pollScan();
    } catch (err) {
      toast(err.message, { error: true });
    }
  };

  const demoBtn = el('button', {
    class: 'btn btn--ghost',
    type: 'button',
    text: state.isDemo ? 'Demo library already loaded' : 'Use the bundled demo library',
    onclick: async () => {
      const next = [...new Set([...(state.settings.libraryFolders || []), system.samplesDir])];
      await saveSettings({ libraryFolders: next });
      paintFolders();
      await startScan();
    },
  });
  if (state.isDemo) demoBtn.disabled = true;
  if (!system.tools.canRender) demoBtn.title = 'ffmpeg is not installed, so artwork will not be generated';

  body.append(
    el('div', { class: 'field' },
      el('span', { class: 'field__label', text: 'Library folders' }),
      folderList,
      el('div', { class: 'row-actions', style: 'margin-top:10px' }, folderInput,
        el('button', { class: 'btn btn--vlc', type: 'button', text: 'Add folder', onclick: addFolder })),
      el('p', { class: 'field__hint', text: 'VLCFlix reads files only — it never moves or deletes them. Artwork and previews are written to data/media.' }),
      el('div', { class: 'row-actions', style: 'margin-top:12px' },
        el('button', { class: 'btn btn--primary', type: 'button', onclick: startScan }, SVG('i-refresh'), el('span', { text: 'Scan library' })),
        demoBtn),
      scanStatus,
      scanBar
    )
  );

  /* playback engine */
  const modeSeg = el('div', { class: 'seg' });
  for (const [value, label] of [['vlc', 'Real VLC'], ['ffmpeg', 'FFmpeg (hardware)'], ['browser', 'Built-in player']]) {
    modeSeg.append(el('button', {
      type: 'button',
      class: (state.settings.playMode === value) ? 'is-active' : '',
      text: label,
      onclick: async (event) => {
        // Capture the button BEFORE awaiting: currentTarget is null after the
        // await yields, and the throw landed after every button had already been
        // de-highlighted, so the control looked like nothing had happened.
        const button = event.currentTarget;
        try {
          await saveSettings({ playMode: value });
        } catch {
          return; // the refusal has been shown in a toast; leave the old choice
        }
        $$('button', modeSeg).forEach((b) => b.classList.remove('is-active'));
        button.classList.add('is-active');
        paintEngine();
      },
    }));
  }

  const vlcPathInput = el('input', {
    class: 'input',
    type: 'text',
    value: state.settings.vlcPath || '',
    placeholder: system.vlc.binary || 'Auto-detect',
    spellcheck: 'false',
  });
  const portInput = el('input', {
    class: 'input',
    type: 'number',
    value: String(state.settings.vlcHttpPort ?? 8080),
    min: '1',
    max: '65535',
  });
  const passInput = el('input', {
    class: 'input',
    type: 'text',
    value: '',
    placeholder: state.settings.vlcPasswordSet ? '•••••• (unchanged)' : 'required by VLC',
  });

  body.append(
    el('div', { class: 'field' },
      el('span', { class: 'field__label', text: 'Playback' }),
      modeSeg,
      el('p', { class: 'field__hint', text: '“Real VLC” launches VLC and controls it over its HTTP interface. “Built-in player” plays inside this window using the browser engine, which cannot decode every codec VLC can.' }),
      el('div', { class: 'status-line' },
        el('span', { class: `dot ${system.vlc.binary ? 'ok' : 'err'}` }),
        el('span', { text: system.vlc.binary ? `VLC found (${system.vlc.source}): ${system.vlc.binary}` : 'VLC was not found on this machine.' })),
      el('div', { class: 'status-line' },
        el('span', { class: `dot ${system.vlc.running ? 'ok' : 'warn'}` }),
        el('span', { text: system.vlc.running ? 'VLC control interface is live on port ' + system.vlc.httpPort : 'VLC is not running yet — it starts on first play.' })),
      el('div', { class: 'status-line' },
        el('span', { class: `dot ${system.tools.ffmpeg ? 'ok' : 'warn'}` }),
        el('span', { text: system.tools.ffmpeg ? 'ffmpeg available — artwork and previews will be generated.' : 'ffmpeg not on PATH — cards fall back to generated gradients and no previews.' }))
    )
  );

  const advanced = el('details', {},
    el('summary', { text: 'VLC connection details', style: 'cursor:pointer;font-size:13px;color:var(--text-dim);margin-bottom:12px' }),
    el('div', { class: 'field' },
      el('span', { class: 'field__label', text: 'VLC executable' }),
      vlcPathInput,
      el('p', { class: 'field__hint', text: 'Leave blank to auto-detect from the standard install locations.' })
    ),
    el('div', { class: 'field', style: 'margin-top:16px' },
      el('span', { class: 'field__label', text: 'Control port' }),
      portInput
    ),
    el('div', { class: 'field', style: 'margin-top:16px' },
      el('span', { class: 'field__label', text: 'Control password' }),
      passInput,
      el('p', { class: 'field__hint', text: 'VLC will not expose its HTTP interface without a password. Stored locally in data/settings.json.' })
    )
  );
  body.append(advanced);

  const fullscreenToggle = el('button', {
    class: 'btn btn--ghost',
    type: 'button',
    text: state.settings.vlcFullscreen ? 'Fullscreen on play: ON' : 'Fullscreen on play: OFF',
    onclick: async (event) => {
      const next = !state.settings.vlcFullscreen;
      await saveSettings({ vlcFullscreen: next });
      event.currentTarget.textContent = next ? 'Fullscreen on play: ON' : 'Fullscreen on play: OFF';
    },
  });

  body.append(el('div', { class: 'row-actions' },
    el('button', {
      class: 'btn btn--vlc',
      type: 'button',
      text: 'Save connection settings',
      onclick: async () => {
        const patch = {
          vlcPath: vlcPathInput.value.trim() || null,
          vlcHttpPort: Number(portInput.value) || 8080,
        };
        if (passInput.value) patch.vlcPassword = passInput.value;
        await saveSettings(patch);
        state.system = await api('/api/system');
        toast('Settings saved');
        renderSettings();
      },
    }),
    fullscreenToggle
  ));

  if (state.scan?.running) watchScan();
  else if (state.scan?.finishedAt) {
    $('#scanStatus').textContent = `Last scan finished ${new Date(state.scan.finishedAt).toLocaleTimeString()} — ${state.items.length} titles.`;
    $('#scanFill').style.width = '100%';
  } else if (state.items.length) {
    $('#scanStatus').textContent = `${state.items.length} titles in the library.`;
  }
}

async function saveSettings(patch) {
  try {
    const { settings } = await api('/api/settings', { method: 'POST', body: patch });
    state.settings = settings;
    paintEngine();
  } catch (err) {
    // Without this the control simply did nothing when the server refused.
    toast(`Could not save: ${err.message}`, { error: true });
    throw err;
  }
}

/** Header chip: the only progress indicator visible outside Settings. */
function paintScanChip(scan) {
  const chip = $('#scanChip');
  const label = $('#scanChipLabel');
  if (!chip || !label) return;

  if (!scan || !scan.running) {
    chip.hidden = true;
    return;
  }
  chip.hidden = false;
  const pct = scan.total ? Math.round((scan.done / scan.total) * 100) : 0;
  if (scan.phase === 'art') {
    label.textContent = scan.total ? `Artwork ${scan.done}/${scan.total} · ${pct}%` : 'Drawing artwork…';
  } else {
    label.textContent = scan.total ? `Reading ${scan.done}/${scan.total}` : 'Reading library…';
  }
}

/**
 * Watch a running scan.
 *
 * Runs independently of the Settings panel. The scan itself is a server-side job
 * that continues whether or not anything is polling, but the catalogue only
 * reaches the screen through this watcher -- so tying it to the panel meant the
 * library silently stopped updating when the panel closed, which is exactly how
 * "it did not continue in the background" looked from the outside.
 */
function watchScan() {
  if (state.scanPoll) return; // already watching
  let lastArt = -1;
  let lastRefresh = 0;

  const tick = async () => {
    let scan;
    try {
      scan = await api('/api/scan/status');
    } catch {
      return; // transient: keep the watcher alive rather than giving up
    }
    state.scan = scan;
    paintScanChip(scan);

    // Settings may or may not be on screen; update it only if it is.
    const status = $('#scanStatus');
    const fill = $('#scanFill');
    if (status) {
      if (scan.running) {
        status.textContent = scan.phase === 'art'
          ? (scan.total ? `Library ready — drawing artwork ${scan.done}/${scan.total}` : 'Library ready — drawing artwork…')
          : (scan.total ? `Reading ${scan.done}/${scan.total} — ${scan.current}` : 'Reading library…');
        if (fill) fill.style.width = `${scan.total ? Math.round((scan.done / scan.total) * 100) : 0}%`;
      }
    }

    if (scan.running) {
      // Pull the catalogue in as artwork lands, so the grid fills while browsing.
      const now = Date.now();
      if (scan.phase === 'art' && scan.done !== lastArt && now - lastRefresh > 1500) {
        lastArt = scan.done;
        lastRefresh = now;
        refreshCatalog().catch(() => {});
      }
      return;
    }

    // finished
    clearInterval(state.scanPoll);
    state.scanPoll = null;
    paintScanChip(null);
    if (fill) fill.style.width = '100%';
    if (status) {
      if (scan.error) status.textContent = `Scan failed: ${scan.error}`;
      else status.textContent = `Scan complete — ${state.items.length} titles.`;
    }
    await refreshCatalog().catch(() => {});
    paintFoldersAfterScan();
    if (scan.error) toast(`Scan failed: ${scan.error}`, { error: true });
    else toast(`Library scanned: ${state.items.length} title${state.items.length === 1 ? '' : 's'}`);
  };

  state.scanPoll = setInterval(tick, 750);
  tick(); // paint immediately instead of waiting for the first interval
}

// Kept as the name the rest of the file uses.
function pollScan() {
  clearInterval(state.scanPoll);
  state.scanPoll = null;
  watchScan();
}

function paintFoldersAfterScan() {
  state.isDemo = (state.settings.libraryFolders || []).some((f) => /samples$/.test(f));
}

async function loadDemo() {
  const system = state.system || await api('/api/system');
  const next = [...new Set([...(state.settings.libraryFolders || []), system.samplesDir])];
  await saveSettings({ libraryFolders: next });
  await api('/api/library/scan', { method: 'POST' }).catch(() => {});
  openSettings();
  pollScan();
  toast('Scanning the bundled demo library…');
}

/* -------------------------------- router -------------------------------- */

function go(view, { query = '' } = {}) {
  const hash = view === 'search' && query
    ? `#/search?q=${encodeURIComponent(query)}`
    : `#/${view}`;
  if (location.hash === hash) route();
  else location.hash = hash;
}

function goSearch(query) {
  if (!query) {
    go('browse');
    return;
  }
  go('search', { query });
}

function route() {
  const raw = location.hash.replace(/^#\/?/, '') || 'home';
  const [path, qs] = raw.split('?');
  const params = new URLSearchParams(qs || '');
  const view = ['home', 'browse', 'mylist', 'search'].includes(path) ? path : 'home';

  state.view = view;
  document.body.dataset.view = view;
  $$('.nav__link').forEach((link) => {
    link.classList.toggle('is-active', link.dataset.view === view);
  });

  if (view === 'home') {
    $('#rails').hidden = false;
    renderHome();
  } else if (view === 'browse') {
    renderBrowse();
  } else if (view === 'mylist') {
    renderMyList();
  } else if (view === 'search') {
    const query = params.get('q') || '';
    state.query = query;
    $('#searchInput').value = query;
    renderSearch(query);
  }
  window.scrollTo({ top: 0, behavior: 'auto' });
}

/* -------------------------------- events -------------------------------- */

function wireEvents() {
  window.addEventListener('hashchange', route);

  /* header background on scroll */
  const onScroll = () => {
    $('#header').classList.toggle('is-solid', window.scrollY > 26);
  };
  window.addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* search */
  const search = $('#searchForm');
  const input = $('#searchInput');
  $('#searchToggle').addEventListener('click', () => {
    if (search.classList.contains('is-open') && !input.value.trim()) {
      search.classList.remove('is-open');
      return;
    }
    search.classList.add('is-open');
    input.focus();
  });
  let searchTimer = null;
  input.addEventListener('input', () => {
    clearTimeout(searchTimer);
    const value = input.value;
    searchTimer = setTimeout(() => {
      if (value.trim()) go('search', { query: value });
      else if (state.view === 'search') go('home');
    }, 180);
  });
  search.addEventListener('submit', (event) => {
    event.preventDefault();
    if (input.value.trim()) go('search', { query: input.value });
  });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      input.value = '';
      search.classList.remove('is-open');
      input.blur();
      if (state.view === 'search') go('home');
    }
  });

  /* settings + engine */
  $('#settingsBtn').addEventListener('click', () => openSettings());
  $('#engineBtn').addEventListener('click', () => openSettings());
  $('#scanChip').addEventListener('click', () => openSettings());

  /* modal closing */
  $$('[data-close]').forEach((node) => node.addEventListener('click', () => {
    closeDetail();
    closeSettings();
  }));

  $$('#detailModal, #settingsModal').forEach((modal) => {
    modal.addEventListener('click', (event) => {
      if (event.target === modal) { closeDetail(); closeSettings(); }
    });
  });

  /* built-in player wiring */
  const video = $('#playerVideo');
  const root = $('#player');
  const toggle = () => {
    if (video.paused) video.play().catch(() => {});
    else video.pause();
  };
  video.addEventListener('click', toggle);
  video.addEventListener('play', () => { updatePlayerToggle(); schedulePlayerHide(); });
  video.addEventListener('pause', updatePlayerToggle);
  video.addEventListener('timeupdate', () => {
    updatePlayerToggle();
    if (video.duration && video.currentTime / video.duration > 0.92 && !video.dataset.reportedEnd) {
      video.dataset.reportedEnd = '1';
    }
  });
  video.addEventListener('ended', () => { video.dataset.reportedEnd = ''; });
  root.addEventListener('mousemove', () => { schedulePlayerHide(); updatePlayerToggle(); });
  $('#playerToggle').addEventListener('click', toggle);
  $('#playerClose').addEventListener('click', () => closePlayer());
  $('#playerCenter').addEventListener('click', toggle);
  $('#playerBack10').addEventListener('click', () => seekToSeconds(playerPosition() - 10));
  $('#playerFwd10').addEventListener('click', () => seekToSeconds(playerPosition() + 10));
  $('#playerScrub').addEventListener('input', (event) => {
    const ratio = Number(event.target.value) / 1000;
    seekToSeconds(ratio * playerTotal());
  });
  $('#playerCC').addEventListener('click', (event) => {
    event.stopPropagation();
    openSubPanel();
  });
  for (const button of document.querySelectorAll('[data-sub-adj]')) {
    button.addEventListener('click', () => {
      adjustSubtitle(button.dataset.subAdj, Number(button.dataset.dir));
    });
  }
  document.addEventListener('keydown', handleSubtitleKey);
  $('#playerMute').addEventListener('click', () => {
    video.muted = !video.muted;
    $('#playerMute').replaceChildren(SVG(video.muted ? 'i-muted' : 'i-volume'));
  });
  $('#playerVol').addEventListener('input', (event) => {
    video.volume = Number(event.target.value);
    video.muted = video.volume === 0;
  });
  $('#playerFull').addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else root.requestFullscreen?.().catch(() => {});
  });

  /* VLC remote */
  $('#remoteClose').addEventListener('click', closeRemote);
  $('#remoteToggle').addEventListener('click', () => {
    vlcCommand(state.vlcPlaying ? 'pause' : 'resume');
  });
  $('#remoteBack').addEventListener('click', () => vlcCommand('seekBy', -10));
  $('#remoteFwd').addEventListener('click', () => vlcCommand('seekBy', 10));
  $('#remoteScrub').addEventListener('change', (event) => {
    const length = state.vlcPlayback?.length || 0;
    if (!length) return;
    vlcCommand('seek', (Number(event.target.value) / 1000) * length);
  });

  /* keyboard */
  document.addEventListener('keydown', (event) => {
    const typing = /^(INPUT|TEXTAREA)$/.test(document.activeElement?.tagName || '');
    if (event.key === 'Escape') {
      if (!$('#player').hidden) return closePlayer();
      if (!$('#settingsModal').hidden) return closeSettings();
      if (!$('#detailModal').hidden) return closeDetail();
      if (typing) document.activeElement.blur();
      return;
    }
    if (typing) return;
    if (event.key === '/') {
      event.preventDefault();
      $('#searchForm').classList.add('is-open');
      $('#searchInput').focus();
    }
    if (!$('#player').hidden) {
      const video = $('#playerVideo');
      if (event.key === ' ') { event.preventDefault(); video.paused ? video.play() : video.pause(); }
      if (event.key === 'ArrowRight') { video.currentTime += 10; }
      if (event.key === 'ArrowLeft') { video.currentTime = Math.max(0, video.currentTime - 10); }
      if (event.key === 'ArrowUp') { event.preventDefault(); video.volume = Math.min(1, video.volume + 0.1); }
      if (event.key === 'ArrowDown') { event.preventDefault(); video.volume = Math.max(0, video.volume - 0.1); }
      if (event.key === 'm') { video.muted = !video.muted; }
      if (event.key === 'f') {
        $('#player').requestFullscreen?.().catch(() => {});
      }
    }
  });

  /* keep progress fresh when the tab is hidden */
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) reportProgress();
  });
  window.addEventListener('beforeunload', () => reportProgress());
}

/* --------------------------------- start -------------------------------- */

boot().then(wireEvents).catch((err) => {
  console.error(err);
  document.body.innerHTML = `<div style="padding:60px;font-family:system-ui;color:#fff">
    <h1 style="font-size:20px">VLCFlix could not start</h1>
    <p style="color:#aaa">${err.message}</p></div>`;
});
