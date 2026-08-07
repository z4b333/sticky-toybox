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
  void stop() { _running = false; }
  // Preview scaffolding: pretend a phone posted a deck after a few polls, so the
  // "received" screen can be rendered without a network.
  void loop() {
    if (_running && ++_ticks == 3) fakeResult("chem exam", 42);
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
  void fakeResult(const char* name, int n) {
    strncpy(_deck, name, fcard::NAME_LEN);
    _count = n;
    _received = true;
  }

 private:
  bool _running = false, _received = false;
  int _count = 0, _ticks = 0;
  char _ssid[20] = {}, _pass[12] = {}, _deck[fcard::NAME_LEN + 1] = {};
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

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(_ssid, _pass)) return false;
    delay(200);
    _ip = WiFi.softAPIP();

    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", _ip);  // every lookup resolves to us -> captive portal

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

  void stop() {
    if (!_running) return;
    _server.stop();
    _dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _running = false;
  }

  void loop() {
    if (!_running) return;
    _dns.processNextRequest();
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
  DNSServer _dns;
  IPAddress _ip;
  bool _running = false, _received = false;
  int _count = 0;
  char _ssid[20] = {}, _pass[12] = {}, _deck[fcard::NAME_LEN + 1] = {};
  char _url[24] = "http://192.168.4.1";
};

// Kept out of the class body so the (large) literal lands in flash cleanly.
//
// The page has to teach as well as collect: most people arrive here without
// knowing what shape their cards should be in. Every supported separator gets a
// worked example with a one-tap "use this" button, so the page stays useful even
// though the device's network has no internet and the reference links may not
// load until the phone falls back to mobile data.
inline void ImportServer::sendPage() {
  static const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Toybox flashcards</title><style>
*{box-sizing:border-box}
body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:20px 18px 40px;
background:#f5f5f3;color:#111;max-width:620px;margin-inline:auto}
h1{font-size:21px;margin:0 0 2px}
h2{font-size:15px;margin:26px 0 8px;text-transform:uppercase;letter-spacing:.06em;color:#555}
p.sub{margin:0 0 20px;color:#666;font-size:14px}
label{display:block;font-weight:600;margin:16px 0 6px;font-size:15px}
input,textarea{width:100%;padding:12px;border:1px solid #ccc;border-radius:9px;font:inherit;
background:#fff;-webkit-appearance:none}
textarea{min-height:220px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:14px}
button{width:100%;padding:16px;margin-top:18px;border:0;border-radius:9px;background:#111;
color:#fff;font:600 17px system-ui;cursor:pointer}
.hint{font-size:13px;color:#666;margin-top:6px}
.count{font-size:14px;font-weight:600;margin-top:10px;min-height:20px}
ol.steps{margin:0 0 4px;padding-left:22px;font-size:15px}
ol.steps li{margin:5px 0}
.fmt{background:#fff;border:1px solid #e2e2dd;border-radius:10px;padding:12px 14px;margin:8px 0}
.fmt b{font-size:15px}
.fmt .why{font-size:13px;color:#666;margin:2px 0 8px}
.fmt pre{margin:0;padding:9px 11px;background:#f0f0ec;border-radius:7px;overflow-x:auto;
font:13px/1.5 ui-monospace,Menlo,monospace;white-space:pre}
.fmt pre .tab{color:#b3b3aa}
.fmt .use{width:auto;margin:9px 0 0;padding:7px 13px;background:#fff;color:#111;
border:1px solid #bbb;border-radius:7px;font:600 13px system-ui}
ul.links{margin:6px 0 0;padding-left:20px;font-size:14px}
ul.links li{margin:6px 0}
a{color:#0a58c9}
.note{font-size:13px;color:#666;background:#eeeeea;border-radius:8px;padding:10px 12px;margin-top:10px}
</style></head><body>
<h1>Send flashcards to Toybox</h1>
<p class="sub">Nothing to install. Fill this in and tap send.</p>

<ol class="steps">
<li>Name the deck.</li>
<li>Paste your cards, or pick a file.</li>
<li>Tap <b>Send to device</b> &mdash; it appears on the Toybox straight away.</li>
</ol>

<form method="POST" action="/save" id="f">
<label for="name">Deck name</label>
<input id="name" name="name" maxlength="24" value="my deck" autocapitalize="off">

<label for="file">Upload a file <span style="font-weight:400;color:#666">(optional)</span></label>
<input id="file" type="file" accept=".csv,.tsv,.txt,text/plain,text/csv">
<div class="hint">Read here in your browser and dropped into the box below, so you can check
it before sending. .txt, .csv and .tsv all work.</div>

<label for="data">Cards &mdash; one per line</label>
<textarea id="data" name="data" spellcheck="false"
placeholder="bonjour | hello&#10;merci | thank you&#10;au revoir | goodbye"></textarea>
<div class="count" id="c"></div>
<button type="submit">Send to device</button>
</form>

<h2>Formats it understands</h2>
<div class="fmt"><b>Tab</b>
<div class="why">What Anki exports, and what you get pasting from a spreadsheet.
The <span class="tab">&#8677;</span> below marks a real tab character.</div>
<pre id="e0">bonjour<span class="tab">&#8677;</span>hello
merci<span class="tab">&#8677;</span>thank you</pre>
<button class="use" data-t="0" type="button">Use this example</button></div>

<div class="fmt"><b>Vertical bar</b>
<div class="why">Easiest to type by hand, and never clashes with your text.</div>
<pre id="e1">bonjour | hello
merci | thank you</pre>
<button class="use" data-t="1" type="button">Use this example</button></div>

<div class="fmt"><b>Comma (CSV)</b>
<div class="why">Quizlet, Excel and Google Sheets. Wrap a field in quotes if it
contains a comma &mdash; only the first comma splits the line.</div>
<pre id="e2">bonjour,hello
"oui, bien sur","yes, of course"</pre>
<button class="use" data-t="2" type="button">Use this example</button></div>

<div class="fmt"><b>Spaced dash</b>
<div class="why">Reads naturally in notes. Needs spaces around it, so hyphenated
words stay intact.</div>
<pre id="e3">bonjour - hello
merci - thank you</pre>
<button class="use" data-t="3" type="button">Use this example</button></div>

<h2>Getting cards out of other apps</h2>
<ul class="links">
<li><b>Anki</b> &mdash; File &rsaquo; Export &rsaquo; Notes in Plain Text.
<a href="https://docs.ankiweb.net/exporting.html" target="_blank" rel="noreferrer">How to export</a>
&middot; <a href="https://docs.ankiweb.net/importing/text-files.html" target="_blank" rel="noreferrer">text file format</a></li>
<li><b>Quizlet</b> &mdash; open a set &rsaquo; &hellip; &rsaquo; Export, then copy the text.
<a href="https://help.quizlet.com/hc/en-us/articles/360034345672-Exporting-your-sets" target="_blank" rel="noreferrer">Exporting your sets</a></li>
<li><b>Google Sheets / Excel</b> &mdash; two columns, then File &rsaquo; Download &rsaquo; CSV.
Or just select both columns and paste them straight in (that arrives as tabs).</li>
<li><b>Notes or a chatbot</b> &mdash; ask for "one card per line, term | meaning" and paste the reply.</li>
</ul>
<div class="note">Those links need mobile data: this Toybox network has no internet of its
own. The examples above work offline.</div>

<h2>Good to know</h2>
<ul class="links">
<li>Blank lines and lines with no separator are skipped, so headings in an export do no harm.</li>
<li>Up to 200 cards per deck. Front up to 64 characters, back up to 96.</li>
<li>Sending to a deck name that already exists replaces it but <b>keeps your progress</b>
on cards whose front text has not changed.</li>
</ul>

<script>
var EX=[
 "bonjour\thello\nmerci\tthank you\nau revoir\tgoodbye",
 "bonjour | hello\nmerci | thank you\nau revoir | goodbye",
 "bonjour,hello\n\"oui, bien sur\",\"yes, of course\"\nau revoir,goodbye",
 "bonjour - hello\nmerci - thank you\nau revoir - goodbye"];
var d=document.getElementById('data'),c=document.getElementById('c');
function upd(){var n=d.value.split('\n').filter(function(l){
 return l.trim()&&/\t| \| |\||,| - /.test(l)}).length;
 c.textContent=n?n+' card'+(n==1?'':'s')+' ready to send':'';}
d.addEventListener('input',upd);
Array.prototype.forEach.call(document.querySelectorAll('.use'),function(b){
 b.addEventListener('click',function(){d.value=EX[+b.dataset.t];upd();
  d.scrollIntoView({behavior:'smooth',block:'center'});});});
document.getElementById('file').addEventListener('change',function(e){
 var f=e.target.files[0];if(!f)return;var r=new FileReader();
 r.onload=function(){d.value=r.result;upd();
  var nm=document.getElementById('name');
  if(nm.value==='my deck')nm.value=f.name.replace(/\.[^.]+$/,'').slice(0,24);
  d.scrollIntoView({behavior:'smooth',block:'center'});};
 r.readAsText(f);});
document.getElementById('f').addEventListener('submit',function(e){
 if(!d.value.trim()){e.preventDefault();alert('Add some cards first.');}});
</script></body></html>)HTML";
  _server.send_P(200, "text/html", kPage);
}

#endif  // TOYBOX_HOST

}  // namespace fweb
