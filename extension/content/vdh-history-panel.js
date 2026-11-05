/* vdhHistoryPanel: show the *real* history page inside the popup/sidebar.

   The first attempt reimplemented the history list inside the panel, which lost
   things the real page has (the app's own download list, Retry, storage info,
   purge). So this does not reimplement anything: it lays the actual
   /content/history.html over the panel, with a Back bar on top. Everything the
   real page does keeps working, and Back just puts the panel back. */
(function () {
  "use strict";
  var OVERLAY = "vdh_history_overlay";

  function installStyle() {
    if (document.getElementById("vdh_history_style")) return;
    var css = document.createElement("style");
    css.id = "vdh_history_style";
    css.textContent = [
      "#" + OVERLAY + " {",
      "  position: fixed; inset: 0; z-index: 2147483000;",
      "  display: flex; flex-direction: column;",
      "  background: var(--theme-box-bg, #fff);",
      "}",
      "#" + OVERLAY + " .vdh_history_bar {",
      "  flex: 0 0 auto; display: flex; align-items: center; gap: 10px;",
      "  padding: var(--spacing-medium, 8px);",
      "  border-bottom: 1px solid var(--theme-box-border);",
      "}",
      "#" + OVERLAY + " .vdh_history_bar button {",
      "  display: inline-flex; align-items: center; gap: 6px;",
      "}",
      "#" + OVERLAY + " .vdh_history_bar svg { width: 1.1em; height: 1.1em; }",
      "#" + OVERLAY + " .vdh_history_bar span.title { font-weight: bold; }",
      "#" + OVERLAY + " iframe {",
      "  flex: 1 1 auto; min-height: 0; width: 100%; border: 0; background: transparent;",
      "}"
    ].join("\n");
    document.head.appendChild(css);
  }

  var api = {
    visible: false,
    overlay: null,
    build: function () {
      var box = document.createElement("div");
      box.id = OVERLAY;

      var bar = document.createElement("div");
      bar.className = "vdh_history_bar";
      var back = document.createElement("button");
      back.type = "button";
      back.innerHTML =
        '<svg fill="none" stroke="currentColor" stroke-width="1.5" viewBox="0 0 24 24" ' +
        'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' +
        '<path d="m15 6-6 6 6 6"/></svg><span>Back</span>';
      back.onclick = function () { api.close(); };
      var spacer = document.createElement("spacer");
      spacer.setAttribute("flex", "1");
      spacer.style.flex = "1 1 auto";
      var title = document.createElement("span");
      title.className = "title";
      title.textContent = "History";

      bar.appendChild(back);
      bar.appendChild(spacer);
      bar.appendChild(title);

      var frame = document.createElement("iframe");
      frame.setAttribute("title", "History");
      /* the real page, not a copy of it: Retry, the app's download list,
         storage info and every other existing feature come along with it */
      frame.src = chrome.runtime.getURL("/content/history.html") + "?vdh_embedded=1";

      box.appendChild(bar);
      box.appendChild(frame);
      return box;
    },
    open: function () {
      installStyle();
      if (!this.overlay) {
        this.overlay = this.build();
        document.body.appendChild(this.overlay);
      }
      this.overlay.style.display = "flex";
      this.visible = true;
      /* ask the embedded page to hide its own Back banner: Back belongs to this
         overlay, not to the page underneath it */
      try {
        var f = this.overlay.querySelector("iframe");
        if (f && f.contentWindow) {
          f.contentWindow.postMessage({ vdhEmbedded: true }, "*");
        }
      } catch (e) {}
    },
    close: function () {
      if (!this.overlay) return;
      this.overlay.style.display = "none";
      this.visible = false;
    }
  };

  window.vdhHistoryPanel = api;

  try {
    chrome.runtime.onMessage.addListener(function (m) {
      var a = m && m.msg ? m.msg : m;
      if (a && a.name === "vdh_show_history") { api.open(); }
    });
  } catch (e) {}
})();
