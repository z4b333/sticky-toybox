// The clock, set from a phone.
//
// Until now the only way to set this device's clock was to send it a note: the
// notes page posts the phone's local time alongside the note, and the firmware
// takes it. That works, and nobody could be expected to guess it. This is the
// same handover with nothing else attached — the phone opens the page and the
// page sends the time by itself, so the whole interaction is "scan this".
//
// The page still offers a button, for the second time and the times after
// that: a phone that crossed a timezone is the one case where somebody knows
// the device is wrong and wants to say so.
#pragma once
#include "clock_set.h"
#include "home_net.h"
#include "kosync.h"
#include "portal.h"
#include "tools_ui.h"

namespace cweb {

#ifdef TOYBOX_HOST

// Preview scaffolding, in the order a real session goes in: the phone joins on
// the first poll and its page sends the time a couple of polls later.
class ClockServer {
 public:
  bool start(Preferences& prefs) {
    _prefs = &prefs;
    _ticks = 0;
    _set = false;
    _refused = false;
    _wifiSaved = false;
    return _portal.begin();
  }
  void stop() { _portal.end(); }
  void loop() {
    _ticks++;
    if (_ticks >= 1) _portal.hostSetClient(true);
    if (_ticks == 3) hostSendTime(1787305260000LL, "STD-7");  // 21 Aug 2026, 09:41 in Bangkok
  }
  // The harness posts through the same door a phone would, including the door
  // slamming: a hook that says no leaves the page refused, never set.
  void hostSendTime(int64_t localEpochMs, const char* tz = nullptr) {
    if (tz && _prefs) homenet::saveTz(*_prefs, tz);
    if (clockset::apply(localEpochMs))
      _set = true;
    else
      _refused = true;
  }
  // The page's home-Wi-Fi form, through the same rules the device applies.
  bool hostSendWifi(const char* ssid, const char* pass, int slot = 0) {
    const bool ok = _prefs && homenet::saveWifi(*_prefs, ssid, pass, slot);
    if (ok) _wifiSaved = true;
    return ok;
  }
  bool hostSendAccount(const char* server, const char* user, const char* pass) {
    return _prefs && kosync::saveAccount(*_prefs, server, user, pass);
  }
  bool wasSet() const { return _set; }
  bool wasRefused() const { return _refused; }
  bool wifiSaved() const { return _wifiSaved; }
  bool hasClient() const { return _portal.hasClient(); }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  portal::Portal _portal;
  Preferences* _prefs = nullptr;
  bool _set = false, _refused = false, _wifiSaved = false;
  int _ticks = 0;
};

#else

class ClockServer {
 public:
  bool start(Preferences& prefs) {
    if (_portal.running()) return true;
    _prefs = &prefs;
    _set = false;
    _refused = false;
    _wifiSaved = false;
    if (!_portal.begin()) return false;
    _portal.serveIndex([this] { sendPage(); });
    _portal.on("/t", HTTP_POST, [this] { takeTime(); });
    _portal.on("/w", HTTP_POST, [this] { takeWifi(); });
    _portal.on("/k", HTTP_POST, [this] { takeAccount(); });
    _portal.on("/s", HTTP_GET, [this] { sendScan(); });
    // The networks around, for the page's pick list: the station half of the
    // radio scans while the access point keeps serving. Started now, so the
    // list is usually ready by the time the page asks.
    WiFi.mode(WIFI_AP_STA);
    WiFi.scanNetworks(true);
    return true;
  }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }
  bool wasSet() const { return _set; }
  // The phone sent, the firmware could not keep it. Its own state, because a
  // page that shows "waiting for the phone" after a refusal sends somebody
  // back to scan a QR code that was never the problem.
  bool wasRefused() const { return _refused; }
  bool wifiSaved() const { return _wifiSaved; }
  bool hasClient() const { return _portal.hasClient(); }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  void takeTime() {
    WebServer& s = _portal.server();
    // The zone rides along with the time: the phone is the only thing here
    // that knows it, and the internet clock (home_net.h) cannot work without.
    if (s.hasArg("z") && _prefs) homenet::saveTz(*_prefs, s.arg("z").c_str());
    // atoll rather than toInt(): the number is milliseconds since 1970 and
    // overflowed a 32-bit int in 1970 + 24 days.
    if (s.hasArg("t")) {
      if (clockset::apply(atoll(s.arg("t").c_str()))) {
        _set = true;
        _refused = false;
      } else {
        _refused = true;
      }
    }
    s.send(200, "application/json", _set ? "{\"ok\":1}" : "{\"ok\":0}");
  }

  // The home network, for the device to fetch the time (and, later, sync)
  // on its own. Kept until replaced; the device never shows the password.
  void takeWifi() {
    WebServer& s = _portal.server();
    const int slot = s.hasArg("b") && s.arg("b") == "1" ? 1 : 0;  // the backup
    const bool ok = _prefs && s.hasArg("s") &&
                    homenet::saveWifi(*_prefs, s.arg("s").c_str(),
                                      s.hasArg("p") ? s.arg("p").c_str() : "", slot);
    if (ok) _wifiSaved = true;
    s.send(200, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
  }

  // The KOReader sync account. The password is hashed on arrival
  // (kosync::saveAccount) and never stored or sent anywhere as typed.
  void takeAccount() {
    WebServer& s = _portal.server();
    const bool ok = _prefs && s.hasArg("u") && s.hasArg("p") &&
                    kosync::saveAccount(*_prefs, s.hasArg("s") ? s.arg("s").c_str() : "",
                                        s.arg("u").c_str(), s.arg("p").c_str());
    s.send(200, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
  }

  // The scan as JSON: {"busy":1} while it runs, else the networks heard,
  // strongest first, one row per name (a mesh repeats its name on every
  // node), hidden ones left out. Only 2.4 GHz is ever heard: the ESP32-S3's
  // radio has no 5 GHz band, so the list is also the list it can join.
  // GET /s?r=1 asks for a fresh scan.
  void sendScan() {
    WebServer& s = _portal.server();
    const int16_t n = WiFi.scanComplete();
    if (s.hasArg("r") || n == WIFI_SCAN_FAILED) {
      if (n != WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
      }
      s.send(200, "application/json", "{\"busy\":1}");
      return;
    }
    if (n == WIFI_SCAN_RUNNING) {
      s.send(200, "application/json", "{\"busy\":1}");
      return;
    }
    // Strongest first: an index sort over at most the first 40 heard.
    int idx[40], m = 0;
    for (int i = 0; i < n && m < 40; i++) {
      const String id = WiFi.SSID(i);
      if (!id.length()) continue;
      int same = -1;
      for (int k = 0; k < m && same < 0; k++)
        if (WiFi.SSID(idx[k]) == id) same = k;
      if (same >= 0) {  // the same name again: keep whichever node is louder
        if (WiFi.RSSI(i) > WiFi.RSSI(idx[same])) idx[same] = i;
        continue;
      }
      idx[m++] = i;
    }
    for (int a = 1; a < m; a++)
      for (int b = a; b > 0 && WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[b - 1]); b--) {
        const int t = idx[b];
        idx[b] = idx[b - 1];
        idx[b - 1] = t;
      }
    String j = "{\"nets\":[";
    for (int k = 0; k < m && k < 20; k++) {
      if (k) j += ",";
      j += "{\"s\":\"";
      const String id = WiFi.SSID(idx[k]);
      for (size_t c = 0; c < id.length(); c++) {
        const char ch = id[c];
        if (ch == '"' || ch == '\\') j += '\\';
        if ((unsigned char)ch >= 0x20) j += ch;
      }
      j += "\",\"r\":";
      j += (int)WiFi.RSSI(idx[k]);
      j += ",\"l\":";
      j += WiFi.encryptionType(idx[k]) == WIFI_AUTH_OPEN ? "0}" : "1}";
    }
    j += "]}";
    s.send(200, "application/json", j);
  }

  void sendPage();

  portal::Portal _portal;
  Preferences* _prefs = nullptr;
  bool _set = false, _refused = false, _wifiSaved = false;
};

// The phone's page. It sends on load and says what it sent, because the device
// screen cannot be watched and typed on at the same time -- the phone is the
// half of this the person is looking at.
inline void ClockServer::sendPage() {
  static const char kPage[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Set the device clock</title><style>
body{font:16px/1.5 -apple-system,system-ui,sans-serif;margin:0;padding:24px;background:#fff;color:#111}
h1{font-size:20px;margin:0 0 4px}p{margin:0 0 16px;color:#444}
#now{font-size:44px;font-weight:600;letter-spacing:1px;margin:24px 0 8px}
#msg{padding:12px;border:1px solid #ccc;border-radius:8px;margin-bottom:16px}
button{font-size:17px;padding:12px 18px;width:100%;border:1px solid #111;border-radius:8px;
background:#111;color:#fff}
small{color:#666;display:block;margin-top:20px}
h2{font-size:17px;margin:28px 0 4px}h2 span{font-weight:400;color:#666}
input{font-size:17px;padding:10px;width:100%;box-sizing:border-box;border:1px solid #999;
border-radius:8px;margin-bottom:10px}#wm{margin-top:8px;color:#444}
select{font-size:17px;padding:10px;width:100%;box-sizing:border-box;border:1px solid #999;
border-radius:8px;margin-bottom:10px;background:#fff;color:#111}.band{font-size:14px}
</style></head><body>
<h1>Set the device clock</h1>
<p>Toybox takes the time and time zone from this phone.</p>
<div id="now">--:--</div>
<div id="msg">Sending&hellip;</div>
<button id="again">Send this time again</button>
<h2>Home Wi-Fi <span>(optional)</span></h2>
<p>Lets the device set its own clock from the internet later, and sync your
reading. The password stays on the device.</p>
<p class="band">This device uses <b>2.4&nbsp;GHz</b> Wi-Fi only &mdash; it cannot join a
5&nbsp;GHz network. A router that gives both bands one name is fine. Only networks it
can join are listed.</p>
<form id="wf"><select id="wl"><option value="">Looking for networks&hellip;</option></select>
<input id="ws" placeholder="Wi-Fi name" autocapitalize="off" autocomplete="off" maxlength="32">
<input id="wp" type="password" placeholder="Password" autocomplete="off" maxlength="63">
<button>Save home Wi-Fi</button></form><div id="wm"></div>
<h2>Backup Wi-Fi <span>(optional)</span></h2>
<p>Tried when the home Wi-Fi is not there: a phone hotspot, a second router.</p>
<form id="bf"><select id="bl"><option value="">Looking for networks&hellip;</option></select>
<input id="bs" placeholder="Wi-Fi name" autocapitalize="off" autocomplete="off" maxlength="32">
<input id="bp" type="password" placeholder="Password" autocomplete="off" maxlength="63">
<button>Save backup Wi-Fi</button></form><div id="bm"></div>
<h2>Other sync server <span>(optional)</span></h2>
<p>For Readest, skip this: on the device open a book, then Options &rarr;
Sync position, and scan its code. Here only for a KOReader sync server
(matching: Binary).</p>
<form id="kf"><input id="ku" placeholder="User name" autocapitalize="off" autocomplete="off" maxlength="63">
<input id="kp" type="password" placeholder="Password" autocomplete="off">
<input id="ks" placeholder="Server (blank = sync.koreader.rocks)" autocapitalize="off" autocomplete="off" maxlength="95">
<button>Save sync account</button></form><div id="km"></div>
<small>Your time zone is sent with the time. If you travel, open this page
again and send.</small>
<script>
function posixTz(){
 function off(t){return -new Date(t).getTimezoneOffset()}
 function hm(m){m=Math.abs(m);var h=Math.floor(m/60),r=m%60;return h+(r?':'+(r<10?'0':'')+r:'')}
 function name(m){return (m>0?'-':m<0?'':'')+hm(m)}
 var y=new Date().getFullYear(),a=Date.UTC(y,0,1),b=Date.UTC(y+1,0,1),D=864e5,tr=[];
 for(var t=a;t<b;t+=D){if(off(t)!==off(t+D)){var lo=t,hi=t+D;
  while(hi-lo>6e4){var mid=lo+Math.floor((hi-lo)/12e4)*6e4;if(off(mid)===off(lo))lo=mid;else hi=mid}
  tr.push(hi)}}
 var s=off(a),j=off(Date.UTC(y,6,1));
 if(tr.length!==2||s===j){return 'STD'+name(off(Date.now()))}
 var std=Math.min(s,j),dst=Math.max(s,j);
 function rule(T){var w=new Date(T+off(T-6e4)*6e4),d=w.getUTCDate(),
  last=new Date(Date.UTC(w.getUTCFullYear(),w.getUTCMonth()+1,0)).getUTCDate(),
  wk=d+7>last?5:Math.ceil(d/7),mins=w.getUTCHours()*60+w.getUTCMinutes();
  return ',M'+(w.getUTCMonth()+1)+'.'+wk+'.'+w.getUTCDay()+(mins===120?'':'/'+hm(mins))}
 var on=off(tr[0])===dst?tr[0]:tr[1],offT=on===tr[0]?tr[1]:tr[0];
 return 'STD'+name(std)+'DST'+(dst-std===60?'':name(dst))+rule(on)+rule(offT)}
function two(n){return (n<10?'0':'')+n}
function send(){
 var d=new Date();
 document.getElementById('now').textContent=two(d.getHours())+':'+two(d.getMinutes());
 var body='t='+(d.getTime()-d.getTimezoneOffset()*60000)+'&z='+encodeURIComponent(posixTz());
 fetch('/t',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(r){return r.json()})
  .then(function(j){document.getElementById('msg').textContent=
    j.ok?'Done. The device clock is set.':'The device did not keep it. Its clock chip is not answering \u2014 see the device screen.'})
  .catch(function(){document.getElementById('msg').textContent=
    'Could not reach the device. Still connected to its wifi?'});
}
document.getElementById('again').addEventListener('click',send);
send();
document.getElementById('kf').addEventListener('submit',function(e){
 e.preventDefault();var m=document.getElementById('km'),u=document.getElementById('ku').value,
 k=document.getElementById('kp').value,v=document.getElementById('ks').value.trim();
 if(!u||!k){m.textContent='User name and password, please.';return}
 if(v&&!/^https?:\/\//.test(v)){m.textContent='The server starts with https:// (or http://).';return}
 fetch('/k',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'u='+encodeURIComponent(u)+'&p='+encodeURIComponent(k)+'&s='+encodeURIComponent(v)})
 .then(function(r){return r.json()}).then(function(j){m.textContent=j.ok?
  'Saved. Open a book, then Options \u2192 Sync position.':'The device did not accept that.'})
 .catch(function(){m.textContent='Could not reach the device. Still connected to its wifi?'})});
function wifiForm(f,ns,ps,ms,b){
document.getElementById(f).addEventListener('submit',function(e){
 e.preventDefault();var m=document.getElementById(ms),n=document.getElementById(ns).value,
 k=document.getElementById(ps).value;
 if(!n){m.textContent='Type the Wi-Fi name first.';return}
 if(k&&k.length<8){m.textContent='A Wi-Fi password has at least 8 characters.';return}
 fetch('/w',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'s='+encodeURIComponent(n)+'&p='+encodeURIComponent(k)+(b?'&b=1':'')})
 .then(function(r){return r.json()}).then(function(j){m.textContent=j.ok?
  (b?'Saved. The device tries '+n+' when the home Wi-Fi is not there.':
  'Saved. The device will use '+n+' when it goes online.'):'The device did not accept that.'})
 .catch(function(){m.textContent='Could not reach the device. Still connected to its wifi?'})})}
wifiForm('wf','ws','wp','wm',0);wifiForm('bf','bs','bp','bm',1);
function bars(r){return r>=-60?'\u2582\u2584\u2586':r>=-72?'\u2582\u2584':'\u2582'}
function fill(nets){['wl','bl'].forEach(function(id){var sel=document.getElementById(id),
 o='<option value="">'+(nets.length?'Choose a network ('+nets.length+' found)':'No 2.4 GHz networks heard')+'</option>';
 nets.forEach(function(n){var t=document.createElement('span');t.textContent=n.s;
  o+='<option value="'+t.innerHTML.replace(/"/g,'&quot;')+'">'+t.innerHTML+' '+bars(n.r)+(n.l?' \ud83d\udd12':'')+'</option>'});
 o+='<option value="\u0001">Scan again</option><option value="\u0002">Type the name myself</option>';
 sel.innerHTML=o})}
var tries=0;
function scan(again){if(again)tries=0;fetch('/s'+(again?'?r=1':'')).then(function(r){return r.json()}).then(function(j){
 if(j.busy&&++tries<20){setTimeout(scan,1500);return}fill(j.nets||[])}).catch(function(){
 if(++tries<5)setTimeout(scan,2000);else fill([])})}
[['wl','ws','wp'],['bl','bs','bp']].forEach(function(a){document.getElementById(a[0]).addEventListener('change',
 function(){var v=this.value;if(v==='\u0001'){this.innerHTML='<option>Looking for networks\u2026</option>';scan(1);return}
 if(v==='\u0002'){document.getElementById(a[1]).focus();this.value='';return}
 if(v){document.getElementById(a[1]).value=v;document.getElementById(a[2]).focus()}})});
scan(0);
</script></body></html>)HTML";
  _portal.server().send_P(200, "text/html", kPage);
}

#endif

}  // namespace cweb
