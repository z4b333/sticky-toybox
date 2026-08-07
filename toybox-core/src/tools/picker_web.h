// The picker's phone page: a plain text box, one item per line.
//
// Typing ten names on an 8x8-font on-screen keyboard is the worst job in the
// firmware, so the picker gets the same escape hatch the notes and flashcard
// tools have. The list is small enough that a textarea beats any custom widget:
// it edits, reorders and pastes with the phone's own keyboard for free.
//
// The server does not own the list. The tool hands it a reader and a writer at
// start(), so NVS stays the single source of truth and the page always opens on
// whatever is currently on the device.
#pragma once
#include <functional>

#include "picker_list.h"
#include "portal.h"

namespace pweb {

#ifdef TOYBOX_HOST

class ListServer {
 public:
  using Provider = std::function<String()>;
  using Receiver = std::function<int(const String&)>;

  bool start(Provider p, Receiver r) {
    _provide = p;
    _receive = r;
    _received = false;
    _ticks = 0;
    return _portal.begin();
  }
  void stop() { _portal.end(); }
  // Preview scaffolding: pretend a phone posted a list after a few polls.
  void loop() {
    if (++_ticks != 3 || !_receive) return;
    _count = _receive(String("Ana\nBen\nChandra\nDiego\nEmi\n"));
    _received = true;
  }
  bool received() const { return _received; }
  int lastCount() const { return _count; }
  void clearReceived() { _received = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  portal::Portal _portal;
  Provider _provide;
  Receiver _receive;
  bool _received = false;
  int _count = 0, _ticks = 0;
};

#else

class ListServer {
 public:
  using Provider = std::function<String()>;
  using Receiver = std::function<int(const String&)>;

  bool start(Provider p, Receiver r) {
    _provide = p;
    _receive = r;
    if (_portal.running()) return true;
    _received = false;
    if (!_portal.begin()) return false;

    _portal.serveIndex([this] { sendPage(); });
    _portal.on("/items", HTTP_GET, [this] { getItems(); });
    _portal.on("/save", HTTP_POST, [this] { saveItems(); });
    return true;
  }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }

  bool received() const { return _received; }
  int lastCount() const { return _count; }
  void clearReceived() { _received = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  void sendPage();

  void getItems() {
    _portal.server().send(200, "text/plain", _provide ? _provide() : String());
  }

  void saveItems() {
    WebServer& s = _portal.server();
    const String data = s.hasArg("data") ? s.arg("data") : String();
    _count = _receive ? _receive(data) : 0;
    _received = true;
    s.send(200, "application/json", String("{\"n\":") + _count + "}");
  }

  portal::Portal _portal;
  Provider _provide;
  Receiver _receive;
  bool _received = false;
  int _count = 0;
};

inline void ListServer::sendPage() {
  static const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Toybox picker</title><style>
*{box-sizing:border-box}
body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:14px 14px 40px;
background:#f5f5f3;color:#111;max-width:640px;margin-inline:auto}
h1{font-size:20px;margin:0 0 4px}
p.sub{margin:0 0 12px;color:#555;font-size:14px}
textarea{width:100%;min-height:240px;padding:12px;border:1px solid #ccc;border-radius:9px;
font:15px/1.7 ui-monospace,Menlo,Consolas,monospace;background:#fff;resize:vertical}
.bar{display:flex;gap:6px;margin:10px 0}
.bar button{flex:1;padding:9px 12px;background:#fff;color:#111;border:1px solid #bbb;
border-radius:8px;font:600 14px system-ui;cursor:pointer}
.bar button:active{background:#111;color:#fff}
button.go{width:100%;margin-top:10px;padding:15px;background:#111;color:#fff;border:0;
border-radius:10px;font:600 17px system-ui;cursor:pointer}
button.go:disabled{background:#999}
#c{margin-top:8px;font-size:14px;color:#555}
#c.bad{color:#b00;font-weight:600}
#ok{display:none;margin-top:12px;padding:12px;border-radius:9px;background:#e8f5e9;
border:1px solid #a5d6a7;font-size:15px}
ul{margin:6px 0 0;padding-left:20px;color:#555;font-size:13px}
</style></head><body>
<h1>Picker list</h1>
<p class="sub">One item per line. Saving replaces the list on the device.</p>
<textarea id="t" autocapitalize="words" spellcheck="false"></textarea>
<div class="bar">
<button onclick="tidy()">Tidy</button>
<button onclick="sortAZ()">Sort A-Z</button>
<button onclick="t.value='';count()">Clear</button>
</div>
<div id="c"></div>
<ul>
<li>up to <b id="mi"></b> items, <b id="ml"></b> characters each</li>
<li>blank lines are ignored; longer lines are trimmed</li>
</ul>
<button class="go" id="g" onclick="save()">Save to device</button>
<div id="ok"></div>
<script>
const MAXI=__MAXI__, MAXL=__MAXL__;
const t=document.getElementById('t'), c=document.getElementById('c');
const g=document.getElementById('g'), ok=document.getElementById('ok');
document.getElementById('mi').textContent=MAXI;
document.getElementById('ml').textContent=MAXL;
const lines=()=>t.value.split('\n').map(s=>s.trim()).filter(s=>s.length);
function count(){
  const L=lines(); const over=L.filter(s=>s.length>MAXL).length;
  let m=L.length+' of '+MAXI+' items';
  if(L.length>MAXI) m+=' - only the first '+MAXI+' will be kept';
  if(over) m+=' - '+over+' line(s) will be trimmed to '+MAXL;
  c.textContent=m; c.className=(L.length>MAXI||over)?'bad':'';
  g.disabled=L.length===0;
}
function tidy(){ t.value=lines().join('\n'); count(); }
function sortAZ(){ t.value=lines().sort((a,b)=>a.localeCompare(b)).join('\n'); count(); }
function save(){
  g.disabled=true; g.textContent='Saving...';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'data='+encodeURIComponent(lines().join('\n'))})
   .then(r=>r.json()).then(j=>{
     ok.style.display='block';
     ok.textContent=j.n+' item'+(j.n===1?'':'s')+' sent. Tap DONE on the device, or keep editing and save again.';
     g.textContent='Save to device'; count();
   }).catch(()=>{ ok.style.display='block'; ok.textContent='Could not reach the device - still joined to its wifi?';
     g.textContent='Save to device'; g.disabled=false; });
}
t.addEventListener('input',count);
fetch('/items').then(r=>r.text()).then(v=>{t.value=v;count();}).catch(count);
</script></body></html>)HTML";
  // The two limits are compile-time constants on the device; patch them into
  // the page so the phone's warnings can never drift from what NVS accepts.
  String page(kPage);
  page.replace("__MAXI__", String(plist::MAX_ITEMS));
  page.replace("__MAXL__", String(plist::MAX_LEN));
  _portal.server().send(200, "text/html", page);
}

#endif  // TOYBOX_HOST

}  // namespace pweb
