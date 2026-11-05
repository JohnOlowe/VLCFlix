/* vdhHistoryPanel: show history *inside* the popup/sidebar, the same way the
   Settings view works: the extension's own view swaps in, and a Back button
   puts the panel back. No tab, no window, no navigation away.

   This is a hand-written module, loaded from the panel's own page, because the
   panel HTML only ever loads panel.js and adding a second module script there
   would initialise the whole UI twice. It does not touch extension state; it
   only shows a view that reads the extension's own storage. */
(function () {
  "use strict";
  var VIEW_ID = "vdh_history_view";


  /* the view's own styles, expressed with the app's theme variables so it
     follows light/dark exactly like the settings page does */
  function installStyle() {
    if (document.getElementById("vdh_history_style")) return;
    var css = document.createElement("style");
    css.id = "vdh_history_style";
    css.textContent = [
      "#" + VIEW_ID + " .vdh_view_toolbar {",
      "  padding: var(--spacing-medium, 8px);",
      "  border-bottom: 1px solid var(--theme-box-border);",
      "  background-color: var(--theme-box-bg);",
      "}",
      "#" + VIEW_ID + " .vdh_view_body {",
      "  flex: 1 1 auto; min-height: 0; overflow-y: auto; overflow-x: hidden;",
      "  display: flex; flex-direction: column; gap: var(--spacing-medium, 8px);",
      "  padding: var(--spacing-medium, 8px);",
      "}",
      "#" + VIEW_ID + " .vdh_card { padding: var(--spacing-large, 14px); }",
      "#" + VIEW_ID + " .vdh_card h2 { margin: 0 0 6px 0; }",
      "#" + VIEW_ID + " .vdh_history_row:last-child { border-bottom: 0; padding-bottom: 0; }",
      "#" + VIEW_ID + " .vdh_history_actions { display: flex; flex-wrap: wrap; gap: 8px; }",
      "#" + VIEW_ID + " .vdh_history_label { word-break: break-word; }",
      "#" + VIEW_ID + " label { display: flex; gap: 10px; align-items: flex-start; margin: 4px 0; }",
      "#" + VIEW_ID + " input[type=radio], #" + VIEW_ID + " input[type=checkbox] {",
      "  width: 1.1em; height: 1.1em; flex: none; margin-top: .15em;",
      "}"
    ].join("\n");
    document.head.appendChild(css);
  }

  function makeButton(label, onclick) {
    var b = document.createElement("button");
    b.textContent = label;
    b.onclick = onclick;
    return b;
  }

  function fmt(n) {
    return n >= 1073741824 ? (n / 1073741824).toFixed(2) + " GB"
      : n >= 1048576 ? (n / 1048576).toFixed(1) + " MB"
      : n >= 1024 ? (n / 1024).toFixed(0) + " KB" : (n || 0) + " B";
  }

  /* is a rendition savable? asks the worker, which records the verdict */
  function askProbe(key) {
    try {
      chrome.runtime.sendMessage({
        msg: { name: "vdh_subtitle_probe", data: { key: key } },
        channel: 1
      });
    } catch (e) {}
  }

  function saveSubtitle(key, index, button, language) {
    button.disabled = true;
    button.textContent = "Fetching\u2026";
    try {
      chrome.runtime.sendMessage({
        msg: { name: "vdh_subtitle_save", data: { key: key, index: index } },
        channel: 1
      });
      button.textContent = "Subtitles: " + language + " \u2713";
      setTimeout(function () {
        button.disabled = false;
        button.textContent = "Subtitles: " + language;
      }, 2500);
    } catch (e) {
      button.textContent = "Subtitles: " + language + " (failed)";
      button.disabled = false;
    }
  }

  function retire(key, row) {
    try {
      chrome.runtime.sendMessage({
        msg: { name: "vdh_retire_download", data: { key: key } },
        channel: 1
      });
      row.remove();
    } catch (e) {}
  }

  function render(list, journal) {
    list.textContent = "";
    var entries = Object.keys(journal).filter(function (k) {
      var r = journal[k];
      return r && r.download_id;
    });
    entries.sort(function (a, b) {
      return (journal[b].updated_at || 0) - (journal[a].updated_at || 0);
    });
    if (!entries.length) {
      var p = document.createElement("p");
      p.textContent = "No downloads recorded yet.";
      list.appendChild(p);
      return;
    }
    entries.forEach(function (key) {
      var e = journal[key];
      var row = document.createElement("div");
      row.className = "vdh_history_row";
      row.style.cssText = "display:flex; flex-direction:column; gap:6px; " +
        "padding-bottom:8px; border-bottom:1px solid var(--theme-box-border, rgba(128,128,128,.3));";

      var label = document.createElement("span");
      label.className = "vdh_history_label";
      var pct = e.percent ? " - " + Math.round(e.percent) + "%" : "";
      label.textContent = (e.filename || e.download_id) + " - " +
        (e.status || "unknown") + " - " + fmt(e.bytes || 0) + pct;
      row.appendChild(label);

      var actions = document.createElement("div");
      actions.className = "vdh_history_actions";

      var subs = Array.isArray(e.subtitles) ? e.subtitles : [];
      var unavailable = Array.isArray(e.subtitles_unavailable) ? e.subtitles_unavailable : [];
      var probed = Array.isArray(e.subtitles_unavailable);
      subs.forEach(function (r, i) {
        if (!r || !r.url) return;
        if (unavailable.indexOf(i) >= 0) return;
        var lang = r.language || "?";
        var b = makeButton("Subtitles: " + lang, function () {
          saveSubtitle(key, i, b, lang);
        });
        actions.appendChild(b);
      });
      if (subs.length && !probed) askProbe(key);

      actions.appendChild(makeButton("Forget", function () { retire(key, row); }));
      row.appendChild(actions);
      list.appendChild(row);
    });
  }


  /* the same three format choices as the standalone history page, so the
     options are not hidden just because history is shown inside the panel */
  function buildSettings() {
    var box = document.createElement("vbox");
    box.id = "vdh_history_settings";
    box.className = "roundedbox vdh_card";
    box.style.cssText = "display:flex; flex-direction:column; gap:6px;";

    var title = document.createElement("h2");
    title.textContent = "Subtitle format";
    box.appendChild(title);

    var choices = [
      ["keep", "Keep exactly as it is in the stream"],
      ["smart", "Smart convert (SRT only when nothing is lost)"],
      ["srt", "Always convert to SRT"]
    ];
    var radios = [];
    choices.forEach(function (c) {
      var label = document.createElement("label");
      label.style.cssText = "display:flex; gap:10px; align-items:flex-start;";
      var input = document.createElement("input");
      input.type = "radio";
      input.name = "vdh_history_format";
      input.value = c[0];
      input.onchange = function () { saveSettings(inputs()); };
      var txt = document.createElement("span");
      txt.textContent = c[1];
      label.appendChild(input);
      label.appendChild(txt);
      box.appendChild(label);
      radios.push(input);
    });

    var embedLabel = document.createElement("label");
    embedLabel.style.cssText = "display:flex; gap:10px; align-items:flex-start;";
    var embed = document.createElement("input");
    embed.type = "checkbox";
    embed.onchange = function () { saveSettings(inputs()); };
    var embedTxt = document.createElement("span");
    embedTxt.textContent = "Embed into the movie file when possible (needs a " +
      "running or interrupted download; otherwise a separate file is saved)";
    embedLabel.appendChild(embed);
    embedLabel.appendChild(embedTxt);
    box.appendChild(embedLabel);

    function inputs() {
      var format = "keep";
      radios.forEach(function (r) { if (r.checked) format = r.value; });
      return { format: format, placement: embed.checked ? "embed" : "file" };
    }

    function saveSettings(next) {
      try {
        chrome.runtime.sendMessage({
          msg: { name: "vdh_subtitle_settings_set", data: next },
          channel: 1
        });
      } catch (e) {}
    }

    chrome.storage.local.get("vdh_subtitle_settings", function (got) {
      var s = (got && got.vdh_subtitle_settings) || {};
      radios.forEach(function (r) { r.checked = r.value === (s.format || "keep"); });
      embed.checked = s.placement === "embed";
    });

    return box;
  }

  function build() {
    var view = document.createElement("vbox");
    view.id = VIEW_ID;
    view.setAttribute("flex", "1");
    view.hidden = true;
    /* Same shape as the app's own views: a flex child of <body>, hidden by the
       attribute rather than by inline styles, so the layout is identical to how
       com-main and com-settings behave. */
    view.style.cssText =
      "flex:1 1 auto; min-height:0; display:flex; flex-direction:column; " +
      "gap:var(--spacing-medium, 8px); padding:var(--spacing-medium, 8px); " +
      "overflow-y:auto; overflow-x:hidden; box-sizing:border-box;";

    /* the app's settings view puts its Back in a banner with a border under it;
       match that so this page reads as the same place, not a different one */
    var bar = document.createElement("hbox");
    bar.setAttribute("xalign", "center");
    bar.className = "vdh_view_toolbar";
    var back = document.createElement("button");
    back.innerHTML =
      '<svg fill="none" stroke="currentColor" stroke-width="1.5" viewBox="0 0 24 24" ' +
      'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' +
      '<path d="m15 6-6 6 6 6"/></svg><span>Back</span>';
    back.onclick = function () { api.close(); };
    back.style.cssText = "display:inline-flex; align-items:center; gap:6px;";
    back.querySelector("svg").style.cssText = "width:1.1em; height:1.1em;";
    var spacer = document.createElement("spacer");
    spacer.setAttribute("flex", "1");
    var title = document.createElement("span");
    title.textContent = "History";
    title.style.cssText = "font-weight:bold;";
    bar.appendChild(back);
    bar.appendChild(spacer);
    bar.appendChild(title);

    var list = document.createElement("div");
    list.id = "vdh_history_list";
    list.style.cssText = "display:flex; flex-direction:column; gap:8px;";

    /* one card per download, the same rounded box the settings sections use */
    var listCard = document.createElement("vbox");
    listCard.className = "roundedbox vdh_card";
    var listTitle = document.createElement("h2");
    listTitle.textContent = "Downloads";
    listCard.appendChild(listTitle);
    listCard.appendChild(list);

    var noteCard = document.createElement("vbox");
    noteCard.className = "roundedbox vdh_card";
    var note = document.createElement("p");
    note.style.cssText = "margin:0; opacity:.75;";
    note.textContent = "Downloaded files stay in the browser's downloads. " +
      "Forget removes the entry from this list only.";
    noteCard.appendChild(note);

    var scroller = document.createElement("div");
    scroller.className = "vdh_view_body";
    scroller.appendChild(listCard);
    scroller.appendChild(noteCard);
    scroller.appendChild(buildSettings());

    view.appendChild(bar);
    view.appendChild(scroller);
    return view;
  }

  var api = {
    visible: false,
    view: null,
    open: function () {
      if (!this.view) {
        installStyle();
        this.view = build();
        var toolbar = document.getElementById("toolbar");
        if (toolbar && toolbar.parentNode === document.body) {
          document.body.insertBefore(this.view, toolbar);
        } else {
          document.body.appendChild(this.view);
        }
      }
      /* exactly what the settings toggle does: hide the other views with the
         hidden attribute and show this one. The toolbar stays, as it does for
         settings, so there is always a way back. */
      this.hideOthers(true);
      this.view.removeAttribute("hidden");
      this.visible = true;
      this.syncScale();
      this.refresh();
    },
    hideOthers: function (hide) {
      ["main", "settings"].forEach(function (id) {
        var el = document.getElementById(id);
        if (!el) return;
        if (hide) el.setAttribute("hidden", "true");
        else el.removeAttribute("hidden");
      });
    },
    close: function () {
      if (!this.view) return;
      this.view.setAttribute("hidden", "true");
      /* restore whatever the app itself considered visible: settings if the
         settings view was open, otherwise the main view */
      var settings = document.getElementById("settings"),
        wasSettings = settings && !settings.hasAttribute("hidden");
      this.hideOthers(false);
      if (wasSettings) {
        var main = document.getElementById("main");
        if (main) main.setAttribute("hidden", "true");
      } else if (settings) {
        settings.setAttribute("hidden", "true");
      }
      this.visible = false;
    },
    /* follow the panel's own scale: whatever the app set on <html>, use it */
    syncScale: function () {
      if (!this.view) return;
      var root = document.documentElement,
        size = getComputedStyle(root).fontSize;
      this.view.style.fontSize = size;
      var bar = this.view.querySelector("hbox");
      if (bar) bar.style.fontSize = size;
    },
    refresh: function () {
      var self = this;
      chrome.storage.local.get("vdh_journal", function (got) {
        var journal = (got && got.vdh_journal) || {};
        render(self.view.querySelector("#vdh_history_list"), journal);
      });
    }
  };

  window.vdhHistoryPanel = api;

  try {
    chrome.runtime.onMessage.addListener(function (m) {
      var a = m && m.msg ? m.msg : m;
      if (a && a.name === "vdh_show_history") { api.open(); }
    });
  } catch (e) {}

  try {
    chrome.storage.onChanged.addListener(function (ch) {
      if (ch.vdh_journal && api.visible) api.refresh();
      /* the panel re-applies the font size on <html>; mirror it onto the view so
         a change while history is open takes effect immediately */
      if (ch.global_session_state || ch.global_persistent_state) api.syncScale();
    });
  } catch (e) {}
})();
