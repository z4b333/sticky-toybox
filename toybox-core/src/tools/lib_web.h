// The phone page for searching the Readest library, served over the HOME
// Wi-Fi (owner, 18 Nov): the Sticky has already joined the router to talk to
// Readest, so it serves this page at its own address there and the phone --
// on the same Wi-Fi, keeping its internet -- opens it from a QR code. Typing
// a title on a phone beats typing it on the panel.
//
// The page never talks to Readest itself. It hands the device a search (POST
// /q) or a download (POST /d); the reader does the work in its own tick, puts
// the results on the panel, and publishes them back for the page (GET /r,
// with a version number so the page knows when they changed). The device
// stays the only thing holding the account key.
//
// Unlike the clock page there is no access point and no captive DNS: just a
// WebServer on port 80 of the station interface, alive while the library
// screen shows the QR code or its results.
#pragma once
#include <Arduino.h>
#include <string.h>

#ifndef TOYBOX_HOST
#include <WebServer.h>
#include <WiFi.h>
#endif

namespace libweb {

inline constexpr int QUERY_MAX = 64;
inline constexpr int ID_MAX = 33;

class ServerBase {
 public:
  // A search the phone asked for, once.
  bool takeQuery(char* out, int cap) {
    if (!_hasQuery) return false;
    snprintf(out, (size_t)cap, "%s", _query);
    _hasQuery = false;
    return true;
  }
  // A download the phone asked for, once.
  bool takeDownload(char* out, int cap) {
    if (!_hasDl) return false;
    snprintf(out, (size_t)cap, "%s", _dl);
    _hasDl = false;
    return true;
  }
  // What GET /r answers: {"v":N,"q":"..","busy":"..","items":[..]}. Built by
  // the reader; the version is bumped here.
  void publish(const String& json) {
    _results = json;
    _version++;
  }
  uint32_t version() const { return _version; }

 protected:
  void gotQuery(const char* q) {
    snprintf(_query, sizeof(_query), "%s", q);
    _hasQuery = true;
  }
  void gotDownload(const char* id) {
    snprintf(_dl, sizeof(_dl), "%s", id);
    _hasDl = true;
  }
  String _results = "{\"v\":0,\"items\":[]}";
  uint32_t _version = 0;
  char _query[QUERY_MAX] = {}, _dl[ID_MAX] = {};
  bool _hasQuery = false, _hasDl = false;
};

#ifdef TOYBOX_HOST

class Server : public ServerBase {
 public:
  bool start() {
    _up = true;
    return true;
  }
  void stop() { _up = false; }
  void loop() {}
  bool running() const { return _up; }
  const char* url() const { return "http://192.168.1.23/"; }
  // The harness plays the phone.
  void hostSearch(const char* q) { gotQuery(q); }
  void hostDownload(const char* id) { gotDownload(id); }
  const String& hostResults() const { return _results; }

 private:
  bool _up = false;
};

#else

class Server : public ServerBase {
 public:
  ~Server() { stop(); }
  bool start() {
    if (_srv) return true;
    if (WiFi.status() != WL_CONNECTED) return false;
    snprintf(_url, sizeof(_url), "http://%s/", WiFi.localIP().toString().c_str());
    _srv = new WebServer(80);
    _srv->on("/", HTTP_GET, [this] { _srv->send_P(200, "text/html", PAGE); });
    _srv->on("/q", HTTP_POST, [this] {
      if (_srv->hasArg("q")) gotQuery(_srv->arg("q").c_str());
      _srv->send(200, "application/json", "{\"ok\":1}");
    });
    _srv->on("/d", HTTP_POST, [this] {
      const bool ok = _srv->hasArg("id") && _srv->arg("id").length() < ID_MAX;
      if (ok) gotDownload(_srv->arg("id").c_str());
      _srv->send(200, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
    });
    _srv->on("/r", HTTP_GET, [this] { _srv->send(200, "application/json", _results); });
    _srv->onNotFound([this] { _srv->send(404, "text/plain", "not here"); });
    _srv->begin();
    return true;
  }
  void stop() {
    if (!_srv) return;
    _srv->stop();
    delete _srv;
    _srv = nullptr;
  }
  void loop() {
    if (_srv) _srv->handleClient();
  }
  bool running() const { return _srv != nullptr; }
  const char* url() const { return _url; }

 private:
  static constexpr const char* PAGE = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Toybox - Readest library</title><style>
body{font-family:system-ui,sans-serif;margin:20px;max-width:560px;color:#111}
h1{font-size:22px;margin:0 0 12px}input{font-size:17px;padding:10px;width:100%;box-sizing:border-box;
border:1px solid #999;border-radius:8px}button{font-size:16px;padding:9px 14px;border-radius:8px;
border:1px solid #111;background:#111;color:#fff}form{display:flex;gap:8px;margin-bottom:12px}
.b{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid #ddd}
.t{flex:1}.t b{display:block}.t span{color:#666;font-size:14px}.ok{color:#2a7a2a;font-weight:600}
#m{color:#444;margin:8px 0}</style></head><body>
<h1>Readest library</h1>
<form id="f"><input id="q" placeholder="Title or author" autocapitalize="off" autocomplete="off" maxlength="60">
<button>Search</button></form><div id="m"></div><div id="l"></div>
<script>
var v=-1;function esc(s){return String(s||'').replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function draw(j){var m=document.getElementById('m'),l=document.getElementById('l');
 m.textContent=j.busy?j.busy:(j.q?'Results for "'+j.q+'"':'Most recently read');
 l.innerHTML=(j.items||[]).map(function(b){return '<div class="b"><div class="t"><b>'+esc(b.title)+
 '</b><span>'+esc(b.author)+'</span></div>'+(b.local?'<span class="ok">&#10003; on the Sticky</span>':
 '<button data-id="'+esc(b.id)+'">Download</button>')+'</div>'}).join('')||'<p>No books.</p>';
 Array.prototype.forEach.call(l.querySelectorAll('button'),function(x){x.onclick=function(){
  x.disabled=true;x.textContent='Asked...';fetch('/d',{method:'POST',headers:{'Content-Type':
  'application/x-www-form-urlencoded'},body:'id='+encodeURIComponent(x.getAttribute('data-id'))})}})}
function poll(){fetch('/r').then(function(r){return r.json()}).then(function(j){if(j.v!==v){v=j.v;draw(j)}})
 .catch(function(){document.getElementById('m').textContent='The Sticky is not answering. Same Wi-Fi?'})}
document.getElementById('f').addEventListener('submit',function(e){e.preventDefault();
 fetch('/q',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
 body:'q='+encodeURIComponent(document.getElementById('q').value.trim())});
 document.getElementById('m').textContent='Searching on the Sticky...'});
poll();setInterval(poll,1500);
</script></body></html>)HTML";
  WebServer* _srv = nullptr;
  char _url[40] = {};
};

#endif

}  // namespace libweb
