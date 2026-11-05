var vdhOpenHistoryOnce=function(){
  var H="/content/history.html";
  function u(p){return chrome.runtime.getURL(p)}

  /* History is a view inside the panel, the same shape as Settings: the panel
     swaps its own view in and Back swaps it out. A history view already open
     anywhere is focused, never duplicated. Everything here is best-effort: a
     throw must never stop the caller from doing its own fallback. */
  function open(tabId){
    var done=false;
    function finish(ok){
      if(done){return}
      done=true;
      return ok;
    }
    try{
      chrome.tabs.query({url:u(H)+"*"},function(t){
        if(t&&t.length){
          try{chrome.tabs.update(t[0].id,{active:!0})}catch(e){}
          try{chrome.windows.update(t[0].windowId,{focused:!0})}catch(e){}
          return finish(true);
        }
        /* the page that pressed the button */
        if(typeof tabId==="number"){
          try{
            chrome.tabs.sendMessage(tabId,{name:"vdh_show_history"},function(){
              void chrome.runtime.lastError;
              finish(true);
            });
            return;
          }catch(e){}
        }
        /* sidebar mode: the sidebar page is the panel, so it gets the view */
        try{
          chrome.tabs.query({url:u("/content/sidebar.html")+"*"},function(t2){
            if(t2&&t2.length){
              try{chrome.tabs.sendMessage(t2[0].id,{name:"vdh_show_history"})}catch(e){}
              try{chrome.tabs.update(t2[0].id,{active:!0})}catch(e){}
            }
            finish(true);
          });
        }catch(e){finish(false)}
      });
    }catch(e){finish(false)}
  }
  return {open:open};
}();
