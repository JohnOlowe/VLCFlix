var vdhOpenHistoryOnce=function(){
  function open(kind){
    var h=chrome.runtime.getURL("/content/history.html");
    if(kind==="sidebar"){
      var sb=chrome.runtime.getURL("/content/sidebar.html");
      try{
        chrome.tabs.query({url:sb},function(t){
          if(t&&t.length){chrome.tabs.update(t[0].id,{active:!0})}
          else{chrome.tabs.create({url:sb})}
        });
      }catch(e){chrome.tabs.create({url:sb})}
      return;
    }
    /* popup and window modes both keep the panel size: never a full tab */
    try{
      chrome.windows.create({url:h,type:"popup",width:460,height:760,focused:!0});
    }catch(e){
      chrome.tabs.create({url:h});
    }
  }
  function fromChoice(){
    /* the worker holds the live settings object; it is always current, while
       the persisted copy may still be in flight right after a mode change */
    try{
      var live=globalThis._persistent_state;
      if(live&&(live.dockmode==="sidebar"||live.dockmode==="popup"||live.dockmode==="window")){
        open(live.dockmode);
        return;
      }
    }catch(e){}
    try{
      chrome.storage.local.get("global_persistent_state",function(g){
        var v=g&&g.global_persistent_state,
            m=v&&v.__serde_val&&v.__serde_val.dockmode,
            m2=v&&v.dockmode;
        m=m||m2;
        if(m==="sidebar"||m==="popup"||m==="window"){open(m);return}
        /* no explicit choice: the origin of an open popup tells the truth */
        var po=chrome.runtime.getURL("/content/popup.html");
        try{
          chrome.tabs.query({url:po},function(t){
            if(t&&t.length&&t[0].url&&t[0].url.indexOf("sidebar")>0){open("sidebar")}
            else{open("window")}
          });
        }catch(e){open("window")}
      });
    }catch(e){open("window")}
  }
  try{
    var po=chrome.runtime.getURL("/content/popup.html");
    if(chrome.runtime.getContexts){
      var live=chrome.runtime.getContexts({contextTypes:["POPUP"]});
      if(live&&live.then){
        live.then(function(c){
          if(c&&c.length){
            /* a popup is open, so that is where history belongs. Reloading the
               popup keeps it a popup and shows the fresh list. */
            try{
              chrome.tabs.query({active:!0,currentWindow:!0},function(t){
                if(t&&t[0]&&t[0].url&&t[0].url.indexOf(po)===0){
                  chrome.tabs.reload(t[0].id);
                  return;
                }
                fromChoice();
              });
            }catch(e){fromChoice()}
          }else{fromChoice()}
        }).catch(fromChoice);
        return;
      }
    }
  }catch(e){}
  fromChoice();
};
