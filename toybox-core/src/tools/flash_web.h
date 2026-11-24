// Phone-pairing import: the device raises its own WiFi network, shows a QR that
// joins it, and serves a one-page editor as a captive portal.
//
// Why an access point rather than joining the house WiFi: no credentials to
// enter on an e-paper keyboard, works away from home, and the phone's camera
// can act on a `WIFI:` QR payload natively (iOS 11+/Android 10+). The captive
// portal DNS means the page opens by itself once the phone joins, so there is
// only ever one thing to scan.
//
// The AP is up only while the import screen is open, and the password is
// regenerated every session.
#pragma once
#include <Arduino.h>

#include "lan_web.h"
#include "phone_kit.h"
#include "flash_store.h"

#ifndef TOYBOX_HOST
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

namespace fweb {

#ifdef TOYBOX_HOST

// Host preview stub: pretends an AP is up so the screen can be rendered.
class ImportServer {
 public:
  bool start() {
    strcpy(_ssid, "TOYBOX-4F2A");
    strcpy(_pass, "58204617");
    _running = true;
    return true;
  }
  // On the home Wi-Fi instead of our own access point (lan_web.h).
  bool startHome(Preferences& p, const char* path) {
    if (!lanweb::join(p)) return false;
    _running = true;
    _home = true;
    snprintf(_homeUrl, sizeof(_homeUrl), "http://toybox.local%s", path);
    lanweb::ipUrl(path, _ipUrl, sizeof(_ipUrl));
    return true;
  }
  bool home() const { return _home; }
  const char* homeUrl() const { return _homeUrl; }
  const char* ipUrl() const { return _ipUrl; }
  void stop() {
    if (_home) lanweb::leave();
    _running = _home = false;
  }
  // Preview scaffolding, in the order it happens on a real one: a phone joins
  // the access point on the first poll and posts a deck a couple of polls
  // later. The join has to be modelled, not skipped -- the screen is supposed
  // to move itself on when it happens, and a stub that never has a client
  // cannot tell whether it does.
  void loop() {
    if (!_running) return;
    _ticks++;
    if (_ticks >= 1) _fakeClient = true;
    if (_ticks == 3) fakeResult("chem exam", 42);
  }
  bool running() const { return _running; }
  bool received() const { return _received; }
  int count() const { return _count; }
  const char* deckName() const { return _deck; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return "http://192.168.4.1"; }
  String wifiPayload() const {
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }
  bool hasClient() const { return _fakeClient; }
  void hostSetClient(bool on) { _fakeClient = on; }
  void fakeResult(const char* name, int n) {
    strncpy(_deck, name, fcard::NAME_LEN);
    _count = n;
    _received = true;
  }

 private:
  bool _running = false, _received = false, _fakeClient = false;
  int _count = 0, _ticks = 0;
  char _ssid[20] = {}, _pass[12] = {}, _deck[fcard::NAME_LEN + 1] = {};
  bool _home = false;
  char _homeUrl[40] = {}, _ipUrl[40] = {};
};

#else

class ImportServer {
 public:
  bool start() {
    if (_running) return true;
    _received = false;
    _count = 0;
    _deck[0] = 0;

    uint8_t mac[6] = {};
    WiFi.softAPmacAddress(mac);
    snprintf(_ssid, sizeof(_ssid), "TOYBOX-%02X%02X", mac[4], mac[5]);
    // WPA2 needs >= 8 characters; digits keep it easy to retype by hand.
    for (int i = 0; i < 8; i++) _pass[i] = '0' + (char)(esp_random() % 10);
    _pass[8] = 0;

    // Built only here, never as a member by value: a DNSServer owns an
    // AsyncUDP whose destructor reaches into lwIP, and destroying one that
    // never ran asserts inside FreeRTOS on a mutex that does not exist --
    // which is what backing out of the import screen without ever starting
    // it would do. Same lesson as ~Portal and recipe_web.h.
    if (!_dns) _dns = new DNSServer();
    if (!_dns) return false;

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(_ssid, _pass)) return false;
    delay(200);
    _ip = WiFi.softAPIP();

    _dns->setErrorReplyCode(DNSReplyCode::NoError);
    _dns->start(53, "*", _ip);  // every lookup resolves to us -> captive portal

    _server.on("/", HTTP_GET, [this] { sendPage(); });
    _server.on("/save", HTTP_POST, [this] { handleSave(); });
    // Captive-portal probes used by iOS, Android and Windows. Answering these
    // with the page itself makes the "sign in to network" sheet show the editor.
    _server.on("/hotspot-detect.html", HTTP_GET, [this] { sendPage(); });
    _server.on("/generate_204", HTTP_GET, [this] { sendPage(); });
    _server.on("/ncsi.txt", HTTP_GET, [this] { sendPage(); });
    _server.onNotFound([this] { sendPage(); });
    _server.begin();

    _running = true;
    return true;
  }

  // On the home Wi-Fi instead of our own access point (lan_web.h): the same
  // page at toybox.local{path}, no DNS server, no captive-portal probes.
  bool startHome(Preferences& p, const char* path) {
    if (_running) stop();
    _received = false;
    if (!lanweb::join(p)) return false;
    _server.on("/", HTTP_GET, [this] { sendPage(); });
    _server.on(path, HTTP_GET, [this] { sendPage(); });
    _server.on("/save", HTTP_POST, [this] { handleSave(); });
    _server.onNotFound([this] { sendPage(); });
    _server.begin();
    _home = true;
    _running = true;
    snprintf(_homeUrl, sizeof(_homeUrl), "http://toybox.local%s", path);
    lanweb::ipUrl(path, _ipUrl, sizeof(_ipUrl));
    return true;
  }
  bool home() const { return _home; }
  const char* homeUrl() const { return _homeUrl; }
  const char* ipUrl() const { return _ipUrl; }

  void stop() {
    if (_home) {
      _server.stop();
      lanweb::leave();
      _home = false;
      _running = false;
      return;
    }
    if (!_running) {
      // Nothing ran, but a start() that failed after building the DNS server
      // leaves one behind; it has to go while the stack is still up.
      delete _dns;
      _dns = nullptr;
      return;
    }
    _server.stop();
    _dns->stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delete _dns;
    _dns = nullptr;
    _running = false;
  }

  void loop() {
    if (!_running) return;
    if (_home) {
      _server.handleClient();
      return;
    }
    _dns->processNextRequest();
    _server.handleClient();
  }

  bool running() const { return _running; }
  bool received() const { return _received; }
  int count() const { return _count; }
  const char* deckName() const { return _deck; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return _url; }
  String wifiPayload() const {
    // zxing WiFi config format, understood natively by phone cameras.
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }
  // This one raises its own access point rather than borrowing portal::Portal,
  // so it asks WiFi directly. Same question either way: is a phone on it yet.
  bool hasClient() const { return _running && !_home && WiFi.softAPgetStationNum() > 0; }

 private:
  void sendPage();
  void handleSave() {
    const String name = _server.hasArg("name") ? _server.arg("name") : String("deck");
    const String data = _server.hasArg("data") ? _server.arg("data") : String();
    if (data.length() == 0) {
      _server.send(400, "text/html",
                   "<meta name=viewport content='width=device-width'>"
                   "<body style='font:16px system-ui;padding:24px'>"
                   "<h2>Nothing to import</h2><p>Paste some lines first.</p>"
                   "<a href='/'>Back</a>");
      return;
    }
    char saved[fcard::NAME_LEN + 1];
    const int n = fcard::importDeck(name.c_str(), data.c_str(), saved);
    if (n == 0) {
      _server.send(400, "text/html",
                   "<meta name=viewport content='width=device-width'>"
                   "<body style='font:16px system-ui;padding:24px'>"
                   "<h2>No cards found</h2>"
                   "<p>Each line needs a separator: tab, <code>|</code>, comma "
                   "or <code> - </code>.</p><a href='/'>Back</a>");
      return;
    }
    strncpy(_deck, saved, fcard::NAME_LEN);
    _deck[fcard::NAME_LEN] = 0;
    _count = n;
    _received = true;

    String body =
        "<meta name=viewport content='width=device-width'>"
        "<body style='font:16px system-ui;padding:24px;text-align:center'>"
        "<h2>Sent to Toybox</h2><p style='font-size:40px;margin:8px'>";
    body += n;
    body += "</p><p>cards in <b>";
    body += saved;
    body += "</b></p><p>You can close this page and disconnect.</p>"
            "<a href='/'>Import another deck</a>";
    _server.send(200, "text/html", body);
  }

  WebServer _server{80};
  DNSServer* _dns = nullptr;  // built in start(); a by-value one crashes on exit
  IPAddress _ip;
  bool _running = false, _received = false;
  int _count = 0;
  char _ssid[20] = {}, _pass[12] = {}, _deck[fcard::NAME_LEN + 1] = {};
  char _url[24] = "http://192.168.4.1";
  bool _home = false;
  char _homeUrl[40] = {}, _ipUrl[40] = {};
};

// Kept out of the class body so the (large) literal lands in flash cleanly.
//
// The page has to teach as well as collect: most people arrive here without
// knowing what shape their cards should be in. Every supported separator gets a
// worked example with a one-tap "use this" button, so the page stays useful even
// though the device's network has no internet and the reference links may not
// load until the phone falls back to mobile data.
inline void ImportServer::sendPage() {
  // Kit "Phone Flashcards": the deck as a little stack with its count, then
  // 1 name, 2 a file, 3 or type the cards; formats as small chips; one black
  // button at the foot.
  static const char kBody[] PROGMEM = R"HTML(
<form method="POST" action="/save" id="f" style="display:flex;flex-direction:column;gap:16px">
<div style="display:flex;gap:18px;align-items:center">
<div style="position:relative;width:150px;height:110px;flex:none" aria-hidden="true">
<div style="position:absolute;left:12px;top:0;width:134px;height:92px;background:#fff;border:2px solid #111;transform:rotate(5deg)"></div>
<div style="position:absolute;left:6px;top:4px;width:134px;height:92px;background:#FFD84D;border:2px solid #111;transform:rotate(2deg)"></div>
<div style="position:absolute;left:0;top:10px;width:134px;height:92px;background:#fff;border:2px solid #111;box-shadow:3px 3px 0 #111;transform:rotate(-3deg);display:flex;flex-direction:column;align-items:center;justify-content:center;padding:6px">
<b id="front" style="font-size:24px;max-width:120px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">cat</b><span class="eye" style="font-size:9px">tap to flip</span></div></div>
<div><div id="n" style="font-size:30px;font-weight:800;line-height:1">0</div><div class="eye">cards ready</div>
<div class="hint" style="margin-top:6px">front &mdash; back,<br>one per line</div></div></div>

<div><div class="lab"><span class="num">1</span>Deck name</div>
<input id="name" name="name" maxlength="24" value="my deck" autocapitalize="off"></div>

<div><div class="lab"><span class="num">2</span>Upload a file</div>
<label class="drop" for="file">Choose .csv &middot; .tsv &middot; .txt &middot; Anki</label>
<input id="file" type="file" accept=".csv,.tsv,.txt,text/plain,text/csv" style="display:none">
<div class="hint" style="margin-top:6px">Read here in your browser and put in the box below, so you can check it first.</div></div>

<div><div class="lab"><span class="num">3</span>Or type the cards</div>
<textarea id="data" name="data" spellcheck="false" placeholder="cat | แมว&#10;dog | หมา&#10;elephant | ช้าง"></textarea>
<div id="warn" class="hint" style="display:none;margin-top:8px;padding:10px 12px;border:2px solid #111;background:#FFD84D;color:#111"></div>
<div class="chips" style="margin-top:8px"><button type="button" class="chip sm use" data-t="1">a | b</button>
<button type="button" class="chip sm use" data-t="2">a, b</button><button type="button" class="chip sm use" data-t="0">a &#8677; b</button>
<button type="button" class="chip sm use" data-t="3">a - b</button><span class="hint">tap for an example</span></div></div>

<details><summary>Cards from other apps</summary>
<p><b>Anki</b>: File &rsaquo; Export &rsaquo; Notes in Plain Text, then pick the file above.
<a href="https://docs.ankiweb.net/exporting.html" target="_blank" rel="noreferrer">How to export</a></p>
<p><b>Quizlet</b>: open a set &rsaquo; &hellip; &rsaquo; Export, then copy the text.</p>
<p><b>Google Sheets / Excel</b>: two columns, File &rsaquo; Download &rsaquo; CSV, or select both columns and paste here.</p>
<p><b>Notes or a chatbot</b>: ask for "one card per line, term | meaning" and paste the reply.</p></details>
<details><summary>Good to know</summary>
<p>Blank lines and lines with no separator are skipped, so headings in an export do no harm.</p>
<p>Up to 300 cards a deck. Front up to 128 characters, back up to 256; a long card shows in smaller type, never cut.</p>
<p>Sending a deck name that already exists replaces it but keeps your progress on cards whose front has not changed.</p></details>
<div class="go"><button type="submit" id="go">Send to device &rarr;</button></div>
</form></main>
<script>
var EX=[
 "cat\tแมว\ndog\tหมา\nelephant\tช้าง",
 "cat | แมว\ndog | หมา\nelephant | ช้าง",
 "cat,แมว\n\"oui, bien sur\",\"yes, of course\"\nbird,นก",
 "cat - แมว\ndog - หมา\nbird - นก"];
var d=document.getElementById('data');
var MAXC=300,MAXF=128,MAXB=256;
function cut(l){var m=l.indexOf('\t');if(m>=0)return[l.slice(0,m),l.slice(m+1)];
 m=l.indexOf(' | ');if(m>=0)return[l.slice(0,m),l.slice(m+3)];
 m=l.indexOf('|');if(m>=0)return[l.slice(0,m),l.slice(m+1)];
 var q=false;for(var i=0;i<l.length;i++){if(l[i]=='"')q=!q;if(l[i]==','&&!q)return[l.slice(0,i),l.slice(i+1)];}
 m=l.indexOf(' - ');if(m>=0)return[l.slice(0,m),l.slice(m+3)];return null;}
function len(t){return Array.from(t.trim().replace(/^"|"$/g,'')).length;}
function upd(){var L=d.value.split('\n').filter(function(l){return l.trim()&&/\t| \| |\||,| - /.test(l)});
 document.getElementById('n').textContent=L.length>MAXC?MAXC+'+':L.length;
 var long=[];L.forEach(function(l,i){var c=cut(l);if(c&&(len(c[0])>MAXF||len(c[1])>MAXB))long.push(i+1);});
 var w=[];if(L.length>MAXC)w.push('This has '+L.length+' cards; a deck holds '+MAXC+'. Only the first '+MAXC+' are kept.');
 if(long.length)w.push(long.length+(long.length==1?' card is':' cards are')+' too long (card '+long.slice(0,8).join(', ')+(long.length>8?', &hellip;':'')+'): a front keeps its first '+MAXF+' characters, a back its first '+MAXB+'.');
 var W=document.getElementById('warn');W.innerHTML=w.join('<br>');W.style.display=w.length?'block':'none';
 document.getElementById('front').textContent=L.length?L[0].split(/\t| \| |\||,| - /)[0].replace(/^"/,'').trim():'cat';
 document.getElementById('go').disabled=!L.length;}
d.addEventListener('input',upd);
Array.prototype.forEach.call(document.querySelectorAll('.use'),function(b){
 b.addEventListener('click',function(){d.value=EX[+b.dataset.t];upd();});});
document.getElementById('file').addEventListener('change',function(e){
 var f=e.target.files[0];if(!f)return;var r=new FileReader();
 r.onload=function(){d.value=r.result;upd();
  var nm=document.getElementById('name');
  if(nm.value==='my deck')nm.value=f.name.replace(/\.[^.]+$/,'').slice(0,24);
  d.scrollIntoView({behavior:'smooth',block:'center'});};
 r.readAsText(f);});
document.getElementById('f').addEventListener('submit',function(e){
 if(!d.value.trim()){e.preventDefault();alert('Add some cards first.');}});
upd();
</script>)HTML";
  phonekit::begin(_server, "Send flashcards to Toybox", "Paste or upload a deck", "Flashcards");
  _server.sendContent_P(kBody);
  phonekit::end(_server);
}

#endif  // TOYBOX_HOST

}  // namespace fweb
