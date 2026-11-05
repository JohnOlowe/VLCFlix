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
    box.style.cssText = "display:flex; flex-direction:column; gap:6px; " +
      "border-top:1px solid rgba(128,128,128,.35); padding-top:10px;";

    var title = document.createElement("span");
    title.textContent = "Subtitle format";
    title.style.cssText = "font-weight:bold; font-size:22px;";
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
    view.style.cssText =
      "display:flex; flex-direction:column; gap:10px; padding:10px; " +
      "overflow:auto; box-sizing:border-box; font-size:21px;";

    var bar = document.createElement("hbox");
    bar.setAttribute("xalign", "center");
    bar.style.cssText = "display:flex; align-items:center; gap:10px;";
    var back = makeButton("\u2039 Back", function () { api.close(); });
    var spacer = document.createElement("spacer");
    spacer.setAttribute("flex", "1");
    var title = document.createElement("span");
    title.textContent = "History";
    title.style.cssText = "font-weight:bold; font-size:23px;";
    bar.appendChild(back);
    bar.appendChild(spacer);
    bar.appendChild(title);

    var list = document.createElement("div");
    list.id = "vdh_history_list";
    list.style.cssText = "display:flex; flex-direction:column; gap:8px;";

    var note = document.createElement("p");
    note.style.cssText = "opacity:.75; margin:0;";
    note.textContent = "Downloaded files stay in the browser's downloads. " +
      "Forget removes the entry from this list only.";

    view.appendChild(bar);
    view.appendChild(list);
    view.appendChild(note);
    view.appendChild(buildSettings());
    return view;
  }

  var api = {
    visible: false,
    view: null,
    open: function () {
      if (!this.view) {
        this.view = build();
        document.body.appendChild(this.view);
      }
      var main = document.getElementById("main"),
        settings = document.getElementById("settings"),
        toolbar = document.getElementById("toolbar");
      if (main) main.style.display = "none";
      if (settings) settings.style.display = "none";
      if (toolbar) toolbar.style.display = "none";
      this.view.hidden = false;
      this.visible = true;
      this.refresh();
    },
    close: function () {
      if (!this.view) return;
      this.view.hidden = true;
      var main = document.getElementById("main"),
        settings = document.getElementById("settings"),
        toolbar = document.getElementById("toolbar");
      if (main) main.style.display = "";
      if (settings) settings.style.display = "";
      if (toolbar) toolbar.style.display = "";
      this.visible = false;
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
    });
  } catch (e) {}
})();
