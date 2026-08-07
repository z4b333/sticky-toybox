// The note editor served to the phone.
//
// This is the screen the whole feature exists for: a real writing surface, with
// a formatting toolbar and a preview that shows what the e-paper will actually
// look like. Dictation needs no code of ours — the phone keyboard's microphone
// key works in the text area, and on iOS/Android with a downloaded language pack
// it runs on-device, which matters because this access point has no internet.
#pragma once
#include "note_store.h"
#include "portal.h"

namespace nweb {

#ifdef TOYBOX_HOST

class NoteServer {
 public:
  bool start() { return _portal.begin(); }
  void stop() { _portal.end(); }
  // Preview scaffolding: pretend a phone posted a note after a few polls.
  void loop() {
    if (++_ticks == 3) fakeResult("shopping", 214);
  }
  bool received() const { return _received; }
  const char* lastName() const { return _last; }
  int lastBytes() const { return _bytes; }
  void clearReceived() { _received = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  void fakeResult(const char* n, int b) {
    strncpy(_last, n, note::NAME_LEN);
    _bytes = b;
    _received = true;
  }

 private:
  portal::Portal _portal;
  bool _received = false;
  int _bytes = 0, _ticks = 0;
  char _last[note::NAME_LEN + 1] = {};
};

#else

class NoteServer {
 public:
  bool start() {
    if (_portal.running()) return true;
    _received = false;
    if (!_portal.begin()) return false;

    _portal.serveIndex([this] { sendPage(); });
    _portal.on("/notes", HTTP_GET, [this] { listNotes(); });
    _portal.on("/note", HTTP_GET, [this] { getNote(); });
    _portal.on("/save", HTTP_POST, [this] { saveNote(); });
    return true;
  }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }

  bool received() const { return _received; }
  void clearReceived() { _received = false; }
  const char* lastName() const { return _last; }
  int lastBytes() const { return _bytes; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  void sendPage();

  void listNotes() {
    note::Info infos[note::MAX_NOTES];
    const int n = note::list(infos, note::MAX_NOTES);
    String out = "[";
    for (int i = 0; i < n; i++) {
      if (i) out += ",";
      out += "\"";
      out += infos[i].name;
      out += "\"";
    }
    out += "]";
    _portal.server().send(200, "application/json", out);
  }

  void getNote() {
    WebServer& s = _portal.server();
    if (!s.hasArg("n")) return s.send(400, "text/plain", "");
    char clean[note::NAME_LEN + 1];
    note::sanitizeName(s.arg("n").c_str(), clean);
    String body;
    note::load(clean, body);
    s.send(200, "text/plain", body);
  }

  void saveNote() {
    WebServer& s = _portal.server();
    const String name = s.hasArg("name") ? s.arg("name") : String("note");
    const String data = s.hasArg("data") ? s.arg("data") : String();
    char clean[note::NAME_LEN + 1];
    note::sanitizeName(name.c_str(), clean);
    note::save(clean, data.c_str(), data.length());

    strncpy(_last, clean, note::NAME_LEN);
    _last[note::NAME_LEN] = 0;
    _bytes = (int)data.length();
    _received = true;
    s.send(200, "application/json", "{\"ok\":1}");
  }

  portal::Portal _portal;
  bool _received = false;
  int _bytes = 0;
  char _last[note::NAME_LEN + 1] = {};
};

inline void NoteServer::sendPage() {
  static const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Toybox notes</title><style>
*{box-sizing:border-box}
body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:14px 14px 40px;
background:#f5f5f3;color:#111;max-width:640px;margin-inline:auto}
h1{font-size:20px;margin:0 0 12px}
.row{display:flex;gap:8px;align-items:center;margin-bottom:10px}
select,input[type=text]{flex:1;min-width:0;padding:11px;border:1px solid #ccc;border-radius:9px;
font:inherit;background:#fff;-webkit-appearance:none}
.bar{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0 8px}
.bar button{flex:0 0 auto;width:auto;margin:0;padding:9px 12px;background:#fff;color:#111;
border:1px solid #bbb;border-radius:8px;font:600 14px system-ui;cursor:pointer}
.bar button:active{background:#111;color:#fff}
textarea{width:100%;min-height:230px;padding:12px;border:1px solid #ccc;border-radius:9px;
font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:15px;background:#fff}
button.send{width:100%;padding:16px;margin-top:14px;border:0;border-radius:9px;background:#111;
color:#fff;font:600 17px system-ui;cursor:pointer}
button.send:disabled{background:#999}
.hint{font-size:13px;color:#666;margin:6px 0 0}
h2{font-size:13px;margin:22px 0 8px;text-transform:uppercase;letter-spacing:.06em;color:#555}
/* The preview mimics the panel: 800x480 of pure black on white, scaled down. */
#pv{background:#fff;border:2px solid #111;border-radius:4px;padding:16px;
font-family:ui-monospace,Menlo,monospace;color:#000;aspect-ratio:5/3;overflow:hidden}
#pv .h1{font-size:22px;font-weight:700;border-bottom:2px solid #000;padding-bottom:3px;margin:8px 0 6px}
#pv .h2{font-size:18px;font-weight:700;margin:8px 0 4px}
#pv .h3{font-size:15px;font-weight:700;margin:6px 0 3px}
#pv .p{font-size:14px;margin:3px 0}
#pv .li{font-size:14px;margin:3px 0;padding-left:20px;position:relative}
#pv .li:before{content:"\25CF";position:absolute;left:2px;font-size:10px;top:2px}
#pv .no{font-size:14px;margin:3px 0;padding-left:26px;position:relative}
#pv .no b{position:absolute;left:0}
#pv .ck{font-size:14px;margin:5px 0;padding-left:26px;position:relative}
#pv .ck i{position:absolute;left:0;top:1px;width:15px;height:15px;border:2px solid #000;
font-style:normal;text-align:center;line-height:13px;font-size:12px}
#pv .ck.on span{text-decoration:line-through}
#pv .q{font-size:14px;margin:4px 0;padding-left:12px;border-left:4px solid #000}
#pv hr{border:0;border-top:2px solid #000;margin:8px 0}
#pv .sp{height:8px}
.ok{background:#e8f3e8;border:1px solid #bcd9bc;border-radius:9px;padding:12px;margin-top:12px;
font-size:15px;display:none}
</style></head><body>
<h1>Notes on your Toybox</h1>

<div class="row">
<select id="pick"><option value="">+ new note</option></select>
<input type="text" id="name" maxlength="24" placeholder="note name" value="my note">
</div>

<div class="bar">
<button type="button" data-l="# ">Title</button>
<button type="button" data-l="## ">Heading</button>
<button type="button" data-l="- ">Bullet</button>
<button type="button" data-l="1. ">Number</button>
<button type="button" data-l="- [ ] ">Checkbox</button>
<button type="button" data-l="&gt; ">Quote</button>
<button type="button" data-w="**">Bold</button>
<button type="button" data-i="---">Divider</button>
</div>

<textarea id="t" spellcheck="false" placeholder="Type here, or tap the microphone key on your keyboard and just talk."></textarea>
<p class="hint">Tap the microphone on your phone keyboard to dictate. Put the cursor on a
line and tap a button above to format it.</p>

<button class="send" id="send">Send to device</button>
<div class="ok" id="ok"></div>

<h2>How it will look on the device</h2>
<div id="pv"></div>
<p class="hint">The box is one screenful. Anything below it becomes page 2 &mdash; the
device gets a MORE button. Tap a checkbox on the device to tick it off.</p>

<script>
var t=document.getElementById('t'),pv=document.getElementById('pv'),
    nameI=document.getElementById('name'),pick=document.getElementById('pick'),
    ok=document.getElementById('ok');

function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function inline(s){return esc(s).replace(/\*\*(.+?)\*\*/g,'<b>$1</b>')}

// Mirrors note_md.h so the preview and the panel agree.
function render(src){
 var out='',lines=src.split('\n'),num=0;
 for(var i=0;i<lines.length;i++){
  var l=lines[i].replace(/\s+$/,''),s=l.replace(/^\s+/,'');
  if(!s){out+='<div class="sp"></div>';num=0;continue}
  if(/^-{3,}\s*$/.test(s)){out+='<hr>';num=0;continue}
  var m;
  if((m=s.match(/^(#{1,3})\s+(.*)$/))){out+='<div class="h'+m[1].length+'">'+inline(m[2])+'</div>';num=0;continue}
  if((m=s.match(/^[-*]\s+\[([ xX])\]\s*(.*)$/))){
   var on=m[1]!==' ';
   out+='<div class="ck'+(on?' on':'')+'"><i>'+(on?'&#10003;':'')+'</i><span>'+inline(m[2])+'</span></div>';num=0;continue}
  if((m=s.match(/^[-*]\s+(.*)$/))){out+='<div class="li">'+inline(m[1])+'</div>';num=0;continue}
  if((m=s.match(/^(\d+)\.\s+(.*)$/))){num++;out+='<div class="no"><b>'+num+'.</b>'+inline(m[2])+'</div>';continue}
  if((m=s.match(/^>\s*(.*)$/))){out+='<div class="q">'+inline(m[1])+'</div>';num=0;continue}
  out+='<div class="p">'+inline(s)+'</div>';num=0;
 }
 return out||'<div class="p" style="color:#999">empty</div>';
}
function upd(){pv.innerHTML=render(t.value)}
t.addEventListener('input',upd);

// Toolbar: line prefixes toggle on every selected line; Bold wraps the selection.
function eachLine(fn){
 var v=t.value,s=t.selectionStart,e=t.selectionEnd;
 var a=v.lastIndexOf('\n',s-1)+1,b=v.indexOf('\n',e);if(b<0)b=v.length;
 var seg=v.slice(a,b).split('\n').map(fn).join('\n');
 t.value=v.slice(0,a)+seg+v.slice(b);
 t.selectionStart=t.selectionEnd=a+seg.length;t.focus();upd();
}
Array.prototype.forEach.call(document.querySelectorAll('.bar button'),function(btn){
 btn.addEventListener('click',function(){
  var l=btn.dataset.l,w=btn.dataset.w,ins=btn.dataset.i;
  if(l){var p=l.replace('&gt;','>');
   eachLine(function(line){
    var bare=line.replace(/^(\s*)([-*]\s+\[[ xX]\]\s*|[-*]\s+|\d+\.\s+|#{1,3}\s+|>\s*)/,'$1');
    return bare.trim()===''&&line.trim()!==''?line:bare.replace(/^(\s*)/,'$1'+p)});
   return}
  if(w){var v=t.value,s=t.selectionStart,e=t.selectionEnd;
   if(s===e){t.value=v.slice(0,s)+'****'+v.slice(s);t.selectionStart=t.selectionEnd=s+2}
   else{t.value=v.slice(0,s)+w+v.slice(s,e)+w+v.slice(e);t.selectionStart=t.selectionEnd=e+4}
   t.focus();upd();return}
  if(ins){var v2=t.value,s2=t.selectionStart;
   var a2=v2.lastIndexOf('\n',s2-1)+1;
   t.value=v2.slice(0,a2)+ins+'\n'+v2.slice(a2);
   t.selectionStart=t.selectionEnd=a2+ins.length+1;t.focus();upd()}
 });
});

// Load the note list so an existing note can be edited rather than replaced.
fetch('/notes').then(function(r){return r.json()}).then(function(a){
 a.forEach(function(n){var o=document.createElement('option');o.value=n;o.textContent=n;pick.appendChild(o)});
}).catch(function(){});
pick.addEventListener('change',function(){
 var n=pick.value;ok.style.display='none';
 if(!n){t.value='';nameI.value='my note';upd();return}
 nameI.value=n;
 fetch('/note?n='+encodeURIComponent(n)).then(function(r){return r.text()})
  .then(function(x){t.value=x;upd()});
});

document.getElementById('send').addEventListener('click',function(){
 var b=this;b.disabled=true;b.textContent='Sending...';
 var body='name='+encodeURIComponent(nameI.value||'note')+'&data='+encodeURIComponent(t.value);
 fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(){b.disabled=false;b.textContent='Send to device';
   ok.textContent='Saved. It is on the screen now — keep editing and send again any time.';
   ok.style.display='block';
   if(!Array.prototype.some.call(pick.options,function(o){return o.value===nameI.value})){
    var o=document.createElement('option');o.value=o.textContent=nameI.value;pick.appendChild(o)}
   pick.value=nameI.value;})
  .catch(function(){b.disabled=false;b.textContent='Send to device';
   ok.textContent='Could not reach the device. Still connected to its wifi?';ok.style.display='block'});
});
upd();
</script></body></html>)HTML";
  _portal.server().send_P(200, "text/html", kPage);
}

#endif  // TOYBOX_HOST

}  // namespace nweb
