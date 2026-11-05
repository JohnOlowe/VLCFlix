/* vdhTextSize: one number the user owns -- the base font size, in CSS pixels,
   of every page of this extension (panel, sidebar, history).

   The stylesheet ships `font-size: var(--vdh-ui-base-font, 21px)` instead of a
   hard-coded 21px, so changing that one property rescales the whole UI: the
   root font size drives every rem in the app, and the component hosts read the
   same variable, which is what defeats the vendor's own `:host` shrink rules.

   Storage is deliberately two-tier:
     - localStorage is read synchronously at module evaluation time, before the
       first paint, so a saved size never flashes the default size first;
     - chrome.storage.local is the durable copy and the channel that tells the
       other open extension pages (notably the embedded history iframe) to
       resize themselves live.
   The service worker cannot reach localStorage, which is why both exist. */

export const VDH_TEXT_SIZE_KEY = "vdh_ui_text_size";
/* 15px is the ceiling, not a middle value: it is the size that looks the way
   the panel did before the pages declared a viewport (commit 15e176f), when a
   phone laid the panel out at ~980px and scaled it down to the screen. Below
   it, the range goes down to 5px for a screen where even 15 is too large. The
   stylesheet's own fallback is the same 15px, so an untouched install and the
   card always agree. */
export const VDH_TEXT_SIZE_DEFAULT = 15;
export const VDH_TEXT_SIZE_MIN = 5;
export const VDH_TEXT_SIZE_MAX = 15;

export function clampTextSize(px) {
  let n = Math.round(Number(px));
  if (!isFinite(n)) return VDH_TEXT_SIZE_DEFAULT;
  return Math.min(VDH_TEXT_SIZE_MAX, Math.max(VDH_TEXT_SIZE_MIN, n));
}

/** The size as this page knows it right now, without waiting for storage. */
export function readTextSize() {
  try {
    let cached = window.localStorage.getItem(VDH_TEXT_SIZE_KEY);
    if (cached !== null && cached !== "") return clampTextSize(cached);
  } catch (e) { /* private mode, storage disabled: fall through to default */ }
  return VDH_TEXT_SIZE_DEFAULT;
}

function remember(px) {
  try { window.localStorage.setItem(VDH_TEXT_SIZE_KEY, String(px)); } catch (e) { /* ignore */ }
}

/** Apply a size to this document. `documentElement` is what the CSS hangs off. */
export function applyTextSize(px) {
  let size = clampTextSize(px),
    root = document.documentElement;
  root.style.setProperty("--vdh-ui-base-font", size + "px");
  root.setAttribute("data-vdh-text-size", String(size));
  return size;
}

/** Apply + persist, and let every other extension page follow along. */
export function saveTextSize(px) {
  let size = applyTextSize(px);
  remember(size);
  try {
    chrome.storage.local.set({ [VDH_TEXT_SIZE_KEY]: size });
  } catch (e) { /* ignore */ }
  return size;
}

/* ------------------------------------------------------------------ bootstrap */

function syncUI(size) {
  let ui = window.vdhTextSizeUI;
  if (ui && typeof ui.sync === "function") ui.sync(size);
}

export function initTextSize() {
  /* Fast path: the synchronous copy, so the page is the right size on the
     first paint. Then the durable copy, which also covers a size chosen on
     another of the extension's pages. */
  let size = applyTextSize(readTextSize());
  try {
    chrome.storage.local.get(VDH_TEXT_SIZE_KEY, (got) => {
      let stored = got && got[VDH_TEXT_SIZE_KEY];
      if (typeof stored === "number" && stored !== size) {
        remember(stored);
        syncUI(applyTextSize(stored));
      }
    });
    chrome.storage.onChanged.addListener((changes, area) => {
      if (area !== "local") return;
      let change = changes[VDH_TEXT_SIZE_KEY];
      if (change && typeof change.newValue === "number") {
        remember(change.newValue);
        syncUI(applyTextSize(change.newValue));
      }
    });
  } catch (e) { /* ignore */ }
  return size;
}

/* --------------------------------------------------------------- settings UI */

/* The Settings view is a custom element with its own shadow root and it is only
   built the first time the user opens Settings, so the card cannot be wired at
   load time: watch for it instead. */
function deep(id) {
  function walk(root) {
    if (root.getElementById) {
      let hit = root.getElementById(id);
      if (hit) return hit;
    }
    for (let el of root.querySelectorAll("*")) {
      if (el.shadowRoot) {
        let hit = walk(el.shadowRoot);
        if (hit) return hit;
      }
    }
    return null;
  }
  return walk(document);
}

export const vdhTextSizeUI = {
  box: null,
  range: null,
  value: null,
  preview: null,

  sync(size) {
    if (this.range && Number(this.range.value) !== size) this.range.value = String(size);
    this.paint(size);
  },

  paint(size) {
    if (this.value) this.value.textContent = size + " px";
    if (this.preview) this.preview.style.fontSize = size + "px";
    if (this.range) this.range.setAttribute("aria-valuetext", size + " pixels");
  },

  set(size) {
    return saveTextSize(size);
  },

  step(delta) {
    return this.set(clampTextSize(readTextSize() + delta));
  },

  mount() {
    if (this.box) return true;
    let box = deep("vdh_text_size_box");
    if (!box) return false;
    this.box = box;
    this.range = deep("vdh_text_size_range");
    this.value = deep("vdh_text_size_value");
    this.preview = deep("vdh_text_size_preview");

    let current = readTextSize();
    this.range.min = String(VDH_TEXT_SIZE_MIN);
    this.range.max = String(VDH_TEXT_SIZE_MAX);
    this.range.step = "1";
    this.range.value = String(current);
    this.paint(current);

    /* `input` fires while the thumb is dragged, so the panel resizes live and
       the user can see the size they are choosing instead of guessing. */
    this.range.addEventListener("input", () => this.set(this.range.value));
    this.range.addEventListener("change", () => this.set(this.range.value));

    let minus = deep("vdh_text_size_minus"),
      plus = deep("vdh_text_size_plus"),
      reset = deep("vdh_text_size_default");
    if (minus) minus.onclick = () => this.step(-1);
    if (plus) plus.onclick = () => this.step(1);
    if (reset) reset.onclick = () => this.set(VDH_TEXT_SIZE_DEFAULT);
    return true;
  },

  watch_dom() {
    if (this.mount()) return;
    let observer = new MutationObserver(() => {
      if (vdhTextSizeUI.mount()) observer.disconnect();
    });
    observer.observe(document.documentElement, { childList: true, subtree: true });
    document.addEventListener("click", () => { vdhTextSizeUI.mount(); });
  }
};

export function installTextSizeUI() {
  window.vdhTextSizeUI = vdhTextSizeUI;
  try {
    vdhTextSizeUI.watch_dom();
  } catch (e) {
    console.warn("[vdhTextSize] settings card mount failed", e);
  }
}
