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
#include "phone_kit.h"
#include <functional>

#include "picker_list.h"
#include "portal.h"

namespace pweb {

#ifdef TOYBOX_HOST

class ListServer {
 public:
  // The choices of list `l`; the lists as JSON ({"cur":0,"lists":[{"name":..,"n":..}]});
  // and a save: text into list `l` (-1: a new list called `name`, -2: the
  // list on the screen), answering its count and which list it went to.
  struct Saved {
    int n, list;
  };
  using Provider = std::function<String(int)>;
  using Lister = std::function<String()>;
  using Receiver = std::function<Saved(const String&, int, const String&)>;

  bool start(Provider p, Lister l, Receiver r) {
    _provide = p;
    _lists = l;
    _receive = r;
    _received = false;
    _ticks = 0;
    return _portal.begin();
  }
  bool startHome(Preferences& pr, const char* path, Provider p, Lister l, Receiver r) {
    _provide = p;
    _lists = l;
    _receive = r;
    _received = false;
    _ticks = 0;
    return _portal.beginHome(pr, path);
  }
  bool home() const { return _portal.home(); }
  const char* homeUrl() const { return _portal.homeUrl(); }
  const char* ipUrl() const { return _portal.ipUrl(); }
  void stop() { _portal.end(); }
  // Preview scaffolding: pretend a phone posted a list after a few polls.
  void loop() {
    if (++_ticks != 3 || !_receive) return;
    // Not all Latin: the list used to be folded down to ASCII on its way in,
    // so a Thai list reached the panel as rows of question marks. The scaffold
    // sends what a phone actually sends.
    _count = _receive(String("Ana\nBen\nกะเพรา\nข้าวมันไก่\nJosé\n"), -2, String()).n;
    _received = true;
  }
  // The harness's phone: a save as the page would send it.
  Saved hostSave(const String& text, int list, const String& name) {
    Saved r = _receive(text, list, name);
    _count = r.n;
    _received = true;
    return r;
  }
  String hostLists() const { return _lists ? _lists() : String(); }
  String hostItems(int l) const { return _provide ? _provide(l) : String(); }
  bool received() const { return _received; }
  int lastCount() const { return _count; }
  void clearReceived() { _received = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  portal::Portal _portal;
  Provider _provide;
  Lister _lists;
  Receiver _receive;
  bool _received = false;
  int _count = 0, _ticks = 0;
};

#else

class ListServer {
 public:
  // The choices of list `l`; the lists as JSON ({"cur":0,"lists":[{"name":..,"n":..}]});
  // and a save: text into list `l` (-1: a new list called `name`, -2: the
  // list on the screen), answering its count and which list it went to.
  struct Saved {
    int n, list;
  };
  using Provider = std::function<String(int)>;
  using Lister = std::function<String()>;
  using Receiver = std::function<Saved(const String&, int, const String&)>;

  bool start(Provider p, Lister l, Receiver r) {
    _provide = p;
    _lists = l;
    _receive = r;
    if (_portal.running()) return true;
    _received = false;
    if (!_portal.begin()) return false;

    _portal.serveIndex([this] { sendPage(); });
    routes();
    return true;
  }
  bool startHome(Preferences& pr, const char* path, Provider p, Lister l, Receiver r) {
    _provide = p;
    _lists = l;
    _receive = r;
    _received = false;
    if (!_portal.beginHome(pr, path)) return false;
    _portal.on("/", HTTP_GET, [this] { sendPage(); });
    _portal.on(path, HTTP_GET, [this] { sendPage(); });
    routes();
    return true;
  }
  bool home() const { return _portal.home(); }
  const char* homeUrl() const { return _portal.homeUrl(); }
  const char* ipUrl() const { return _portal.ipUrl(); }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }

  bool received() const { return _received; }
  int lastCount() const { return _count; }
  void clearReceived() { _received = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  void sendPage();

  void routes() {
    _portal.on("/lists", HTTP_GET, [this] {
      _portal.server().send(200, "application/json", _lists ? _lists() : String("{}"));
    });
    _portal.on("/items", HTTP_GET, [this] { getItems(); });
    _portal.on("/save", HTTP_POST, [this] { saveItems(); });
  }

  void getItems() {
    WebServer& s = _portal.server();
    const int l = s.hasArg("l") ? s.arg("l").toInt() : -2;
    s.send(200, "text/plain; charset=utf-8", _provide ? _provide(l) : String());
  }

  void saveItems() {
    WebServer& s = _portal.server();
    const String data = s.hasArg("data") ? s.arg("data") : String();
    const int l = s.hasArg("l") ? s.arg("l").toInt() : -2;
    const String name = s.hasArg("name") ? s.arg("name") : String();
    Saved r{0, -1};
    if (_receive) r = _receive(data, l, name);
    _count = r.n;
    _received = true;
    s.send(200, "application/json", String("{\"n\":") + r.n + ",\"l\":" + r.list + "}");
  }

  portal::Portal _portal;
  Provider _provide;
  Lister _lists;
  Receiver _receive;
  bool _received = false;
  int _count = 0;
};

inline void ListServer::sendPage() {
  // Kit "Phone Picker": the wheel as a picture, the choices one a line on
  // ruled paper, Tidy / Sort A-Z / Clear and a count, one black button.
  static const char kPage[] PROGMEM = R"HTML(
<div style="display:flex;gap:18px;align-items:center">
<svg width="132" height="132" viewBox="0 0 132 132" aria-hidden="true" style="flex:none">
<circle cx="66" cy="66" r="62" fill="#F5F1E6" stroke="#111" stroke-width="3"/>
<path d="M66 66 L119.7 97 A62 62 0 0 1 66 128 Z" fill="#FFD84D" stroke="#111" stroke-width="2"/>
<path d="M66 66 L66 4 M66 66 L119.7 35 M66 66 L119.7 97 M66 66 L66 128 M66 66 L12.3 97 M66 66 L12.3 35" stroke="#111" stroke-width="2"/>
<circle cx="66" cy="66" r="9" fill="#111"/><path d="M66 0 L58 14 L74 14 Z" fill="#111"/></svg>
<div><div class="eye">On the Sticky</div><div style="font-size:15px;margin-top:4px">Spin on the Sticky: it rolls like a slot reel and stops on one.</div></div></div>
<div><div class="lab">Which list</div>
<div style="display:flex;gap:8px"><select id="ls" style="flex:1;font:inherit;padding:10px;border:2px solid #111;background:#fff"></select></div>
<div id="nw" style="display:none;margin-top:8px"><input id="nm" maxlength="__MAXN__" placeholder="Name of the new list, e.g. Lunch" autocapitalize="words"></div></div>
<div><div class="lab">Choices, one per line</div>
<textarea id="t" autocapitalize="words" spellcheck="false" style="min-height:230px"></textarea></div>
<div class="chips"><button class="chip" onclick="tidy()">Tidy</button><button class="chip" onclick="sortAZ()">Sort A&ndash;Z</button>
<button class="chip" onclick="t.value='';count()">Clear</button><span class="eye" id="c" style="margin-left:auto"></span></div>
<div class="hint">Up to <b id="mi"></b> choices, <b id="ml"></b> characters each, and up to <b id="mx"></b> lists. Blank lines are skipped; longer lines are trimmed. Saving replaces that list on the Sticky.</div>
<div class="ok" id="ok"></div>
</main>
<div class="go"><button id="g" onclick="save()">Save to device &rarr;</button></div>
<script>
const MAXI=__MAXI__, MAXL=__MAXL__, MAXN=__MAXN__, MAXLISTS=__MAXLISTS__;
const t=document.getElementById('t'), c=document.getElementById('c');
const g=document.getElementById('g'), ok=document.getElementById('ok');
const ls=document.getElementById('ls'), nw=document.getElementById('nw'), nm=document.getElementById('nm');
document.getElementById('mi').textContent=MAXI;
document.getElementById('ml').textContent=MAXL;
document.getElementById('mx').textContent=MAXLISTS;
let L0=[];
const lines=()=>t.value.split('\n').map(s=>s.trim()).filter(s=>s.length);
function count(){
  const L=lines(); const over=L.filter(s=>Array.from(s).length>MAXL).length;
  let m=L.length+' choice'+(L.length===1?'':'s');
  if(L.length>MAXI) m+=' (first '+MAXI+' kept)';
  if(over) m+=' ('+over+' trimmed)';
  c.textContent=m.toUpperCase(); c.style.color=(L.length>MAXI||over)?'#B00020':'';
  g.disabled=L.length===0||(ls.value==='-1'&&!nm.value.trim());
}
function tidy(){ t.value=lines().join('\n'); count(); }
function sortAZ(){ t.value=lines().sort((a,b)=>a.localeCompare(b)).join('\n'); count(); }
function fill(j,pick){
  L0=j.lists||[]; ls.innerHTML='';
  L0.forEach((x,i)=>{const o=document.createElement('option');o.value=i;
    o.textContent=x.name+' ('+x.n+')'+(i===j.cur?' \u2014 on screen':'');ls.appendChild(o);});
  if(L0.length<MAXLISTS){const o=document.createElement('option');o.value=-1;o.textContent='+ New list';ls.appendChild(o);}
  ls.value=String(pick);
}
function load(){
  nw.style.display=ls.value==='-1'?'block':'none';
  if(ls.value==='-1'){t.value='';count();nm.focus();return;}
  fetch('/items?l='+ls.value).then(r=>r.text()).then(v=>{t.value=v;count();}).catch(count);
}
ls.addEventListener('change',load);
nm.addEventListener('input',count);
function save(){
  g.disabled=true; g.innerHTML='Saving&hellip;';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'l='+ls.value+'&name='+encodeURIComponent(nm.value.trim())+'&data='+encodeURIComponent(lines().join('\n'))})
   .then(r=>r.json()).then(j=>{
     ok.style.display='block';
     ok.textContent=j.l<0?'The Sticky already has '+MAXLISTS+' lists. Delete one there first.':
       j.n+' choice'+(j.n===1?'':'s')+' on the Sticky. Tap Done there, or keep editing and save again.';
     g.innerHTML='Save to device &rarr;';
     if(j.l>=0){nm.value='';fetch('/lists').then(r=>r.json()).then(k=>{fill(k,j.l);nw.style.display='none';count();});}
     else count();
   }).catch(()=>{ ok.style.display='block'; ok.textContent='Could not reach the Sticky. Is the phone still on the same Wi-Fi?';
     g.innerHTML='Save to device &rarr;'; g.disabled=false; });
}
t.addEventListener('input',count);
fetch('/lists').then(r=>r.json()).then(j=>{fill(j,j.cur);load();}).catch(()=>{fill({cur:0,lists:[{name:'My list',n:0}]},0);load();});
</script>)HTML";
  // The two limits are compile-time constants on the device; patch them into
  // the page so the phone's warnings can never drift from what NVS accepts.
  String page(kPage);
  page.replace("__MAXI__", String(plist::MAX_ITEMS));
  page.replace("__MAXL__", String(plist::MAX_CHARS));
  page.replace("__MAXN__", String(plist::NAME_CHARS));
  page.replace("__MAXLISTS__", String(plist::MAX_LISTS));
  WebServer& srv = _portal.server();
  phonekit::begin(srv, "Toybox picker", "What the wheel spins", "Picker list");
  srv.sendContent(page);
  phonekit::end(srv);
}

#endif  // TOYBOX_HOST

}  // namespace pweb
