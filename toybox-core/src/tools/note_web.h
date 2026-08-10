// The note editor served to the phone.
//
// This is the screen the whole feature exists for: a real writing surface, with
// a formatting toolbar and a preview that shows what the e-paper will actually
// look like. Dictation needs no code of ours — the phone keyboard's microphone
// key works in the text area, and on iOS/Android with a downloaded language pack
// it runs on-device, which matters because this access point has no internet.
#pragma once
#include "lock_image.h"
#include "note_store.h"
#include "portal.h"

namespace nweb {

// Set by the firmware if it has a real-time clock. Takes milliseconds since the
// Unix epoch, already shifted to the phone's local time.
using SetClockFn = void (*)(int64_t localEpochMs);
inline SetClockFn g_setClock = nullptr;
inline void setClockHook(SetClockFn fn) { g_setClock = fn; }

#ifdef TOYBOX_HOST

class NoteServer {
 public:
  bool start() { return _portal.begin(); }
  void stop() { _portal.end(); }
  // Preview scaffolding: a phone joins on the first poll and posts a note a
  // couple of polls later, which is the order it happens in.
  void loop() {
    _ticks++;
    if (_ticks >= 1) _portal.hostSetClient(true);
    if (_ticks == 3) fakeResult("shopping", 214);
  }
  bool received() const { return _received; }
  const char* lastName() const { return _last; }
  int lastBytes() const { return _bytes; }
  void clearReceived() { _received = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }
  bool pictureStored() const { return lockimg::have(); }
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
    // The picture arrives as a multipart upload rather than a form field: it is
    // 48 KB of packed bits and full of zero bytes, and an Arduino String would
    // stop at the first one.
    _portal.server().on(
        "/pic", HTTP_POST, [this] { _portal.server().send(200, "text/plain", _picOk ? "ok" : "bad"); },
        [this] { receivePicture(); });
    _portal.on("/pic", HTTP_DELETE, [this] {
      tfs::remove(picPath());
      _portal.server().send(200, "text/plain", "ok");
    });
    _portal.on("/picture", HTTP_GET, [this] { sendPicturePage(); });
    _portal.on("/picstat", HTTP_GET, [this] {
      _portal.server().send(200, "text/plain", lockimg::have() ? "1" : "0");
    });
    return true;
  }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }

  bool received() const { return _received; }
  void clearReceived() { _received = false; }
  bool pictureStored() const { return lockimg::have(); }
  const char* lastName() const { return _last; }
  int lastBytes() const { return _bytes; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  void sendPage();
  void sendPicturePage();

  // Where a picture upload lands: the lock screen file, or the home screen's
  // wallpaper when the request says ?to=wall. Resolved once at the start of
  // the upload, because the arg is the same for every chunk and asking the
  // server again mid-stream buys nothing.
  const char* picPath() {
    return _portal.server().arg("to") == "wall" ? wallimg::PATH : lockimg::PATH;
  }

  // Streamed straight to the filesystem as it arrives. Buffering 48 KB in RAM
  // while WiFi is up is exactly the allocation this device cannot spare.
  void receivePicture() {
    HTTPUpload& up = _portal.server().upload();
    if (up.status == UPLOAD_FILE_START) {
      _picOk = false;
      _picLen = 0;
      _picPath = picPath();
      tfs::begin();
      _picFile = LittleFS.open(_picPath, "w");
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (_picFile) {
        _picFile.write(up.buf, up.currentSize);
        _picLen += up.currentSize;
      }
    } else if (up.status == UPLOAD_FILE_END) {
      if (_picFile) _picFile.close();
      // A half-arrived picture is worse than none: it would draw as a photo
      // that turns to noise partway down the panel.
      _picOk = (_picLen == tbimg::FILE_SIZE);
      if (!_picOk) tfs::remove(_picPath);
    } else {
      if (_picFile) _picFile.close();
      tfs::remove(_picPath);
    }
  }

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
    // The phone posts its local clock alongside the note. The device has no
    // network time and no way to ask the user, so this is the one moment it can
    // learn what time it is -- free, and exactly when someone is already here.
    if (s.hasArg("t") && nweb::g_setClock) nweb::g_setClock(atoll(s.arg("t").c_str()));
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
  bool _picOk = false;
  uint32_t _picLen = 0;
  const char* _picPath = lockimg::PATH;
  File _picFile;
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
<p class="hint" style="margin:-6px 0 12px"><a href="/picture" style="color:#111">Send a
picture instead — lock screen or home wallpaper &rsaquo;</a></p>

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
 var body='name='+encodeURIComponent(nameI.value||'note')+'&data='+encodeURIComponent(t.value)
   +'&t='+(Date.now()-new Date().getTimezoneOffset()*60000);
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

// The lock screen picture page.
//
// Everything that turns a photograph into something a one-bit panel can show
// happens here, in the phone's browser: it already decodes JPEG, PNG and the
// HEIC that iPhone photos actually are, and it can put the dithered result in
// front of you before it is sent. That last part is the whole argument. A badly
// chosen photograph becomes mud at one bit per pixel and there is no way to know
// but to look; doing this on the device would mean upload, refresh, squint,
// repeat, at 1.7 seconds a refresh.
//
// What leaves the phone is 48,000 bytes of packed bits in the panel's own
// convention, not a photograph.
inline void NoteServer::sendPicturePage() {
  static const char kPic[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pictures</title><style>
*{box-sizing:border-box}
body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:14px 14px 40px;
background:#f5f5f3;color:#111;max-width:640px;margin-inline:auto}
h1{font-size:20px;margin:0 0 4px}
.hint{font-size:13px;color:#666;margin:0 0 14px}
.seg{display:flex;gap:6px;margin:12px 0}
.seg button{flex:1;padding:10px 4px;border:1px solid #bbb;border-radius:8px;background:#fff;
color:#111;font:600 14px system-ui;cursor:pointer}
.seg button.on{background:#111;color:#fff;border-color:#111}
label.file{display:block;padding:16px;border:2px dashed #bbb;border-radius:10px;background:#fff;
text-align:center;font-weight:600;cursor:pointer}
label.file input{display:none}
#wrap{margin:14px 0;display:none}
canvas{width:100%;max-width:300px;display:block;margin:0 auto;border:1px solid #111;
background:#fff;image-rendering:pixelated;touch-action:none}
button.send{width:100%;padding:16px;margin-top:6px;border:0;border-radius:9px;background:#111;
color:#fff;font:600 17px system-ui;cursor:pointer}
button.send:disabled{background:#999}
button.rm{width:100%;padding:12px;margin-top:8px;border:1px solid #bbb;border-radius:9px;
background:#fff;color:#111;font:600 15px system-ui}
#ok{display:none;margin-top:10px;padding:10px;border-radius:8px;background:#e7f3e7;font-size:14px}
a{color:#111}
</style></head><body>
<h1>Pictures</h1>
<p class="hint">480 x 800, black and white. What you see below is exactly what the
panel will show — the device is sent the finished picture, not the photo.</p>

<div class="seg" id="dest">
  <button data-v="lock" class="on">Lock screen</button>
  <button data-v="wall">Home wallpaper</button>
</div>

<label class="file">Choose a picture<input id="f" type="file" accept="image/*"></label>

<div id="wrap">
  <div class="seg" id="fit">
    <button data-v="crop" class="on">Fill (crop)</button>
    <button data-v="fit">Fit whole picture</button>
  </div>
  <div class="seg" id="dit">
    <button data-v="fs" class="on">Photo</button>
    <button data-v="atk">Crisp</button>
    <button data-v="thr">Line art</button>
  </div>
  <canvas id="c" width="480" height="800"></canvas>
  <p class="hint" id="dragh">Drag the picture to choose what is kept.</p>
  <button class="send" id="send">Send to device</button>
</div>

<button class="rm" id="rm">Remove this picture from the device</button>
<div id="ok"></div>
<p class="hint"><a href="/">Back to notes</a></p>

<script>
var W=480,H=800,cv=document.getElementById('c'),cx=cv.getContext('2d');
var img=null,mode='crop',dither='fs',offX=0,offY=0,drag=null,dest='lock';

function seg(id,set){
 var box=document.getElementById(id);
 Array.prototype.forEach.call(box.querySelectorAll('button'),function(b){
  b.addEventListener('click',function(){
   Array.prototype.forEach.call(box.querySelectorAll('button'),function(o){o.classList.remove('on')});
   b.classList.add('on');set(b.dataset.v);render();
  });
 });
}
seg('fit',function(v){mode=v;offX=offY=0});
seg('dit',function(v){dither=v});
seg('dest',function(v){dest=v});

document.getElementById('f').addEventListener('change',function(e){
 var file=e.target.files[0];if(!file)return;
 var im=new Image();
 im.onload=function(){img=im;offX=offY=0;
  document.getElementById('wrap').style.display='block';render()};
 im.src=URL.createObjectURL(file);
});

// Draw the photo into 480x800 the chosen way, then reduce it to one bit.
function render(){
 if(!img)return;
 cx.fillStyle='#fff';cx.fillRect(0,0,W,H);
 var s=(mode==='crop')?Math.max(W/img.width,H/img.height):Math.min(W/img.width,H/img.height);
 var w=img.width*s,h=img.height*s;
 var x=(W-w)/2+offX,y=(H-h)/2+offY;
 if(mode==='crop'){ // never let the drag pull an edge inside the panel
  x=Math.min(0,Math.max(W-w,x));y=Math.min(0,Math.max(H-h,y));offX=x-(W-w)/2;offY=y-(H-h)/2}
 else {x=(W-w)/2;y=(H-h)/2}
 cx.drawImage(img,x,y,w,h);
 document.getElementById('dragh').style.display=(mode==='crop')?'block':'none';

 var d=cx.getImageData(0,0,W,H),p=d.data;
 // Rec. 601 luma, which is what the eye does with a greyscale photograph.
 var g=new Float32Array(W*H);
 for(var i=0,j=0;i<p.length;i+=4,j++) g[j]=0.299*p[i]+0.587*p[i+1]+0.114*p[i+2];

 if(dither==='thr'){
  for(var k=0;k<g.length;k++) g[k]=g[k]<128?0:255;
 } else if(dither==='fs'){
  // Floyd-Steinberg: the smoothest gradients, slightly muddier blacks.
  for(var yy=0;yy<H;yy++)for(var xx=0;xx<W;xx++){
   var idx=yy*W+xx,old=g[idx],nv=old<128?0:255,er=old-nv;g[idx]=nv;
   if(xx+1<W)g[idx+1]+=er*7/16;
   if(yy+1<H){if(xx>0)g[idx+W-1]+=er*3/16;g[idx+W]+=er*5/16;
    if(xx+1<W)g[idx+W+1]+=er*1/16}
  }
 } else {
  // Atkinson passes on only 3/4 of the error, which keeps whites white and
  // edges sharp -- the look people recognise as e-ink.
  for(var yy2=0;yy2<H;yy2++)for(var xx2=0;xx2<W;xx2++){
   var id2=yy2*W+xx2,o2=g[id2],n2=o2<128?0:255,e2=(o2-n2)/8;g[id2]=n2;
   if(xx2+1<W)g[id2+1]+=e2;if(xx2+2<W)g[id2+2]+=e2;
   if(yy2+1<H){if(xx2>0)g[id2+W-1]+=e2;g[id2+W]+=e2;if(xx2+1<W)g[id2+W+1]+=e2;
    if(yy2+2<H)g[id2+2*W]+=e2}
  }
 }
 for(var m=0,q=0;m<g.length;m++,q+=4){var v=g[m]<128?0:255;p[q]=p[q+1]=p[q+2]=v;p[q+3]=255}
 cx.putImageData(d,0,0);
}

// Dragging the crop. Pointer events cover finger and mouse alike.
cv.addEventListener('pointerdown',function(e){if(mode!=='crop')return;
 drag={x:e.clientX,y:e.clientY,ox:offX,oy:offY};cv.setPointerCapture(e.pointerId)});
cv.addEventListener('pointermove',function(e){if(!drag)return;
 var k=W/cv.getBoundingClientRect().width;
 offX=drag.ox+(e.clientX-drag.x)*k;offY=drag.oy+(e.clientY-drag.y)*k;render()});
cv.addEventListener('pointerup',function(){drag=null});

function say(t){var o=document.getElementById('ok');o.textContent=t;o.style.display='block'}

document.getElementById('send').addEventListener('click',function(){
 var b=this;b.disabled=true;b.textContent='Sending...';
 var d=cx.getImageData(0,0,W,H).data;
 // 8 bytes of header, then one bit a pixel, MSB first, 1 = white: the
 // framebuffer's own layout, so the device stores what it will draw.
 var out=new Uint8Array(8+W*H/8);
 out[0]=84;out[1]=66;out[2]=73;out[3]=49;            // 'TBI1'
 out[4]=W&255;out[5]=W>>8;out[6]=H&255;out[7]=H>>8;
 for(var y=0;y<H;y++)for(var x=0;x<W;x++){
  if(d[(y*W+x)*4]>=128) out[8+y*(W/8)+(x>>3)] |= (128>>(x&7));
 }
 var fd=new FormData();
 fd.append('f',new Blob([out],{type:'application/octet-stream'}),'pic.tbi');
 var q=(dest==='wall')?'?to=wall':'';
 fetch('/pic'+q,{method:'POST',body:fd}).then(function(r){return r.text()}).then(function(t){
  b.disabled=false;b.textContent='Send to device';
  say(t!=='ok'?'The device did not accept it. Try sending again.'
     :dest==='wall'?'Sent. It is now behind the home screen.'
     :'Sent. Set the lock screen to "a picture" in Settings on the device.');
 }).catch(function(){b.disabled=false;b.textContent='Send to device';
  say('Could not reach the device. Still connected to its wifi?')});
});

document.getElementById('rm').addEventListener('click',function(){
 var q=(dest==='wall')?'?to=wall':'';
 fetch('/pic'+q,{method:'DELETE'}).then(function(){say('Removed.')})
  .catch(function(){say('Could not reach the device.')});
});
</script></body></html>)HTML";
  _portal.server().send_P(200, "text/html", kPic);
}

#endif  // TOYBOX_HOST

}  // namespace nweb
