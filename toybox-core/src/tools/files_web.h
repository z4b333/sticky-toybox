// The card, managed from a phone.
//
// Until now the only way to put a book on the card was to take the card out.
// This raises the same access point the notes and flashcards screens use, and
// serves a page that lists the card, uploads files onto it, and renames or
// deletes what is already there.
//
// The one thing that makes this different from every other pairing screen is
// the bus. The card shares the display's SPI lines, so while a transfer is
// running the panel cannot be touched at all. The claim is therefore lazy and
// batched: nothing is claimed while the phone is only pairing, the first file
// operation claims, and a couple of quiet seconds after the last one the
// session lets go and the screen catches up. A phone sending six books holds
// the bus once, not six times.
#pragma once
#include "portal.h"
#include "tools_ui.h"

namespace fweb {

// How long the card sits idle before the session gives the bus back and the
// device screen redraws. Long enough that a phone queueing files back to back
// never loses the claim between them.
inline constexpr uint32_t IDLE_RELEASE_MS = 2000;

struct Counts {
  uint16_t added = 0, removed = 0, renamed = 0;
  uint32_t bytes = 0;  // KB, so a card's worth still fits
};

#ifdef TOYBOX_HOST

// Preview scaffolding: a phone joins on the first poll, then sends a book and
// deletes something, which is the order a real session tends to go in.
class FilesServer {
 public:
  bool start(ToolsHost& h) {
    _host = &h;
    _counts = Counts{};
    _ticks = 0;
    _busy = false;
    _dirty = false;
    return _portal.begin();
  }
  void stop() {
    if (_busy && _host) _host->sdMgrClose();
    _busy = false;
    _portal.end();
  }
  void loop() {
    _ticks++;
    if (_ticks >= 1) _portal.hostSetClient(true);
    if (_ticks == 3) hostUpload("books", "ships-log.tbk", 640000);
    if (_busy && _ticks >= 5) settle();
  }

  // The harness drives real host calls through these, so the fake card sees
  // exactly what a phone would have done to a real one.
  bool hostUpload(const char* dir, const char* name, uint32_t bytes) {
    if (!claim()) return false;
    if (!_host->sdMgrWriteOpen(dir, name)) return false;
    _host->sdMgrWriteChunk(nullptr, bytes);
    if (!_host->sdMgrWriteClose(true)) return false;
    _counts.added++;
    _counts.bytes += bytes / 1024;
    return true;
  }
  bool hostDelete(const char* path) {
    if (!claim()) return false;
    if (!_host->sdMgrDelete(path)) return false;
    _counts.removed++;
    return true;
  }
  bool hostRename(const char* path, const char* bare) {
    if (!claim()) return false;
    if (!_host->sdMgrRename(path, bare)) return false;
    _counts.renamed++;
    return true;
  }
  int hostList(ToolsHost::SdFile* out, int max) {
    if (!claim()) return -1;
    return _host->sdMgrList(out, max);
  }

  bool holdingBus() const { return _busy; }
  bool dirty() const { return _dirty; }
  void clearDirty() { _dirty = false; }
  const Counts& counts() const { return _counts; }
  bool touched() const { return _counts.added || _counts.removed || _counts.renamed; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  bool claim() {
    if (_busy) return true;
    if (!_host || !_host->sdMgrOpen()) return false;
    _busy = true;
    return true;
  }
  void settle() {
    if (!_busy) return;
    _host->sdMgrClose();
    _busy = false;
    _dirty = true;
  }

  portal::Portal _portal;
  ToolsHost* _host = nullptr;
  Counts _counts;
  int _ticks = 0;
  bool _busy = false, _dirty = false;
};

#else

class FilesServer {
 public:
  bool start(ToolsHost& h) {
    _host = &h;
    _counts = Counts{};
    _busy = false;
    _dirty = false;
    if (_portal.running()) return true;
    if (!_portal.begin()) return false;

    _portal.serveIndex([this] { sendPage(); });
    _portal.on("/ls", HTTP_GET, [this] { listFiles(); });
    _portal.on("/rm", HTTP_POST, [this] { removeFile(); });
    _portal.on("/mv", HTTP_POST, [this] { renameFile(); });
    // Uploads arrive as multipart and are written straight through to the
    // card: a book is megabytes, and this device has kilobytes.
    _portal.server().on(
        "/up", HTTP_POST, [this] { _portal.server().send(200, "text/plain", _upOk ? "ok" : "bad"); },
        [this] { receiveFile(); });
    return true;
  }

  void stop() {
    if (_busy) {
      _host->sdMgrClose();
      _busy = false;
    }
    _portal.end();
  }

  void loop() {
    _portal.loop();
    // Quiet for long enough? Give the bus back so the panel can speak again.
    if (_busy && millis() - _lastOp > IDLE_RELEASE_MS) {
      _host->sdMgrClose();
      _busy = false;
      _dirty = true;
    }
  }

  bool holdingBus() const { return _busy; }
  bool dirty() const { return _dirty; }
  void clearDirty() { _dirty = false; }
  const Counts& counts() const { return _counts; }
  bool touched() const { return _counts.added || _counts.removed || _counts.renamed; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  // Every handler goes through this: the bus is taken on demand and the idle
  // clock restarted, so a burst of requests is one claim.
  bool claim() {
    _lastOp = millis();
    if (_busy) return true;
    if (!_host->sdMgrOpen()) return false;
    _busy = true;
    return true;
  }

  static void appendEscaped(String& out, const char* s) {
    for (const char* p = s; *p; p++) {
      if (*p == '"' || *p == '\\') out += '\\';
      out += *p;
    }
  }

  // Forty is plenty for a shelf and keeps the JSON -- built in RAM, while
  // WiFi is up -- to a few kilobytes.
  static constexpr int LIST_MAX = 40;

  void listFiles() {
    WebServer& s = _portal.server();
    if (!claim()) return s.send(503, "application/json", "{\"card\":0}");
    static ToolsHost::SdFile files[LIST_MAX];
    const int n = _host->sdMgrList(files, LIST_MAX);
    String out;
    out.reserve(2048);  // one allocation rather than a dozen reallocs
    out = "{\"card\":1,\"freeMb\":";
    out += _host->sdMgrFreeMb();
    out += ",\"files\":[";
    for (int i = 0; i < (n < 0 ? 0 : n); i++) {
      if (i) out += ',';
      out += "{\"p\":\"";
      appendEscaped(out, files[i].path);
      out += "\",\"s\":";
      out += files[i].size;
      out += '}';
    }
    out += "]}";
    s.send(200, "application/json", out);
  }

  void removeFile() {
    WebServer& s = _portal.server();
    if (!s.hasArg("p") || !claim()) return s.send(400, "text/plain", "no");
    const bool ok = _host->sdMgrDelete(s.arg("p").c_str());
    if (ok) _counts.removed++;
    s.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "no");
  }

  void renameFile() {
    WebServer& s = _portal.server();
    if (!s.hasArg("p") || !s.hasArg("to") || !claim()) return s.send(400, "text/plain", "no");
    const bool ok = _host->sdMgrRename(s.arg("p").c_str(), s.arg("to").c_str());
    if (ok) _counts.renamed++;
    s.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "no");
  }

  void receiveFile() {
    HTTPUpload& up = _portal.server().upload();
    WebServer& s = _portal.server();
    if (up.status == UPLOAD_FILE_START) {
      _upOk = false;
      _upBytes = 0;
      _upOpen = false;
      if (!claim()) return;
      const String dir = s.hasArg("dir") ? s.arg("dir") : String("books");
      // The phone names the file; the device names the folder. That is the
      // whole of the path safety: a name with a slash in it is refused
      // downstairs rather than pasted into a path here.
      _upOpen = _host->sdMgrWriteOpen(dir.c_str(), up.filename.c_str());
    } else if (up.status == UPLOAD_FILE_WRITE) {
      _lastOp = millis();
      if (_upOpen && !_host->sdMgrWriteChunk(up.buf, up.currentSize)) _upOpen = false;
      _upBytes += up.currentSize;
    } else if (up.status == UPLOAD_FILE_END) {
      if (_upOpen) {
        _upOk = _host->sdMgrWriteClose(true);
        if (_upOk) {
          _counts.added++;
          _counts.bytes += _upBytes / 1024;
        }
      }
      _upOpen = false;
      _lastOp = millis();
    } else {
      // Aborted: throw the part away rather than leave a book that ends
      // halfway through chapter four.
      if (_upOpen) _host->sdMgrWriteClose(false);
      _upOpen = false;
      _upOk = false;
    }
  }

  void sendPage();

  portal::Portal _portal;
  ToolsHost* _host = nullptr;
  Counts _counts;
  uint32_t _lastOp = 0, _upBytes = 0;
  bool _busy = false, _dirty = false, _upOk = false, _upOpen = false;
};

inline void FilesServer::sendPage() {
  static const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Toybox files</title><style>
*{box-sizing:border-box}
body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:14px 14px 40px;
background:#f5f5f3;color:#111;max-width:640px;margin-inline:auto}
h1{font-size:20px;margin:0 0 4px}
.sub{color:#666;font-size:14px;margin:0 0 14px}
.card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:12px;margin-bottom:12px}
.row{display:flex;gap:10px;align-items:center;padding:9px 0;border-bottom:1px solid #eee}
.row:last-child{border-bottom:0}
.nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sz{color:#777;font-size:13px;white-space:nowrap}
button{font:inherit;padding:7px 12px;border-radius:8px;border:1px solid #bbb;background:#fff}
button.p{background:#111;color:#fff;border-color:#111;width:100%;padding:13px}
button.s{padding:5px 9px;font-size:14px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.06em;color:#666;margin:16px 0 6px}
#drop{border:2px dashed #bbb;border-radius:10px;padding:18px;text-align:center;color:#666}
select{font:inherit;padding:8px;border-radius:8px;border:1px solid #bbb;background:#fff}
.bar{height:6px;background:#eee;border-radius:3px;overflow:hidden;margin-top:8px}
.bar>i{display:block;height:100%;background:#111;width:0}
.note{font-size:13px;color:#666;margin-top:8px}
.err{color:#a00}
</style></head><body>
<h1>Toybox files</h1>
<p class="sub" id="free">reading the card&hellip;</p>

<div class="card">
  <h2 style="margin-top:0">Add files</h2>
  <div class="row" style="border:0;padding-top:0">
    <select id="dir">
      <option value="books">/books</option>
      <option value="wallpapers">/wallpapers</option>
      <option value="root">card root</option>
    </select>
    <span class="sz">.epub &middot; .tbk &middot; .tbi</span>
  </div>
  <div id="drop">Choose books or drop them here</div>
  <input type="file" id="pick" multiple hidden>
  <button class="p" style="margin-top:10px" onclick="pick.click()">CHOOSE FILES</button>
  <div class="bar"><i id="pb"></i></div>
  <div class="note" id="st">The screen on the device stays still while files
    are moving &mdash; the card and the display share one wire.</div>
</div>

<div class="card">
  <h2 style="margin-top:0">On the card</h2>
  <div id="list"><p class="sub">reading&hellip;</p></div>
</div>

<script>
const $=s=>document.querySelector(s);
const fmt=b=>b>=1048576?(b/1048576).toFixed(1)+' MB':b>=1024?Math.round(b/1024)+' KB':b+' B';
let busy=false;

async function load(){
  try{
    const r=await fetch('/ls',{cache:'no-store'});
    const j=await r.json();
    if(!j.card){$('#free').innerHTML='<span class="err">no card in the slot</span>';return}
    $('#free').textContent=j.files.length+' files · '+j.freeMb+' MB free';
    const l=$('#list');
    if(!j.files.length){l.innerHTML='<p class="sub">nothing here yet</p>';return}
    l.innerHTML='';
    for(const f of j.files){
      const d=document.createElement('div');d.className='row';
      const nm=document.createElement('div');nm.className='nm';nm.textContent=f.p;
      const sz=document.createElement('div');sz.className='sz';sz.textContent=fmt(f.s);
      const mv=document.createElement('button');mv.className='s';mv.textContent='Rename';
      const rm=document.createElement('button');rm.className='s';rm.textContent='Delete';
      mv.onclick=()=>rename(f.p);rm.onclick=()=>remove(f.p);
      d.append(nm,sz,mv,rm);l.append(d);
    }
  }catch(e){$('#free').innerHTML='<span class="err">the device stopped answering</span>'}
}

async function remove(p){
  if(busy||!confirm('Delete '+p+' ?'))return;
  const r=await fetch('/rm?p='+encodeURIComponent(p),{method:'POST'});
  if(!r.ok)alert('could not delete that');
  load();
}

async function rename(p){
  if(busy)return;
  const bare=p.split('/').pop();
  const to=prompt('New name',bare);
  if(!to||to===bare)return;
  const r=await fetch('/mv?p='+encodeURIComponent(p)+'&to='+encodeURIComponent(to),{method:'POST'});
  if(!r.ok)alert('could not rename that — no slashes, and the name must be free');
  load();
}

// One file at a time and strictly in order: the device writes straight to the
// card as bytes arrive, and two at once would interleave into each other.
async function send(files){
  busy=true;const dir=$('#dir').value;
  for(let i=0;i<files.length;i++){
    const f=files[i];
    $('#st').textContent='sending '+f.name+' ('+(i+1)+' of '+files.length+')…';
    const fd=new FormData();fd.append('f',f,f.name);
    await new Promise(res=>{
      const x=new XMLHttpRequest();
      x.open('POST','/up?dir='+dir);
      x.upload.onprogress=e=>{
        const pct=e.lengthComputable?(e.loaded/e.total*100):0;
        $('#pb').style.width=((i+pct/100)/files.length*100)+'%';
      };
      x.onloadend=()=>{if(x.status!==200)alert('could not send '+f.name);res()};
      x.send(fd);
    });
  }
  $('#pb').style.width='0';
  $('#st').textContent='done — the device screen will catch up in a moment.';
  busy=false;load();
}

$('#pick').onchange=e=>{if(e.target.files.length)send([...e.target.files])};
const dz=$('#drop');
dz.ondragover=e=>{e.preventDefault();dz.style.borderColor='#111'};
dz.ondragleave=()=>dz.style.borderColor='#bbb';
dz.ondrop=e=>{e.preventDefault();dz.style.borderColor='#bbb';
  if(e.dataTransfer.files.length)send([...e.dataTransfer.files])};
load();
</script></body></html>)HTML";
  _portal.server().send_P(200, "text/html", kPage);
}

#endif

}  // namespace fweb
