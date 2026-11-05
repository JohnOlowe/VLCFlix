var vdhOpenHistoryOnce=function(){
  var H="/content/history.html", SB="/content/sidebar.html", PO="/content/popup.html";
  function u(p){return chrome.runtime.getURL(p)}
  function liveMode(){
    try{
      var s=globalThis._persistent_state;
      if(s&&(s.dockmode==="sidebar"||s.dockmode==="popup"||s.dockmode==="window")){
        return s.dockmode;
      }
    }catch(e){}
    return null;
  }
  /* an already open history view is the best target: focus it, never duplicate */
  function focusExisting(done){
    try{
      chrome.tabs.query({url:u(H)+"*"},function(t){
        if(t&&t.length){
          try{chrome.tabs.update(t[0].id,{active:!0})}catch(e){}
          try{chrome.windows.update(t[0].windowId,{focused:!0})}catch(e){}
          return done(true);
        }
        done(false);
      });
    }catch(e){done(false)}
  }
  /* sidebar mode: the sidebar page becomes history, so the choice is respected.
     Sidebar mode on this build is a page, not a browser panel. */
  function sidebar(){
    /* the sidebar page is the sidebar on this build. If it is open, it becomes
       history in place (no extra view). If it is still starting up, wait for it
       briefly instead of racing it and leaving two views behind. */
    var tries=0;
    (function look(){
      tries++;
      try{
        chrome.tabs.query({url:u(SB)+"*"},function(t){
          if(t&&t.length){
            try{chrome.tabs.update(t[0].id,{url:u(H),active:!0})}
            catch(e){try{chrome.tabs.create({url:u(H)})}catch(e2){}}
            return;
          }
          if(tries<10){return setTimeout(look,150)}
          try{chrome.tabs.create({url:u(H)})}catch(e){}
        });
      }catch(e){
        if(tries<10){return setTimeout(look,150)}
        try{chrome.tabs.create({url:u(H)})}catch(e2){}
      }
    })();
  }
  /* popup and window modes: a compact window keeps the panel size and, unlike
     the toolbar popup, can actually be looked at. */
  function compact(){
    try{
      chrome.windows.create({url:u(H),type:"popup",width:460,height:760,focused:!0});
      return;
    }catch(e){}
    try{chrome.tabs.create({url:u(H)})}catch(e){}
  }
  function open(){
    focusExisting(function(found){
      if(found)return;
      var m=liveMode();
      if(m==="sidebar"){sidebar();return}
      compact();
    });
  }
  /* the panel asks for this when the button is pressed and the popup is about to
     close: do not let the request die with the popup */
  function openSoon(){
    open();
    setTimeout(function(){},0);
  }
  return {open:open, openSoon:openSoon};
}();
