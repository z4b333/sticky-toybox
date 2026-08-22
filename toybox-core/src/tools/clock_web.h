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
#include "portal.h"
#include "tools_ui.h"

namespace cweb {

#ifdef TOYBOX_HOST

// Preview scaffolding, in the order a real session goes in: the phone joins on
// the first poll and its page sends the time a couple of polls later.
class ClockServer {
 public:
  bool start() {
    _ticks = 0;
    _set = false;
    _refused = false;
    return _portal.begin();
  }
  void stop() { _portal.end(); }
  void loop() {
    _ticks++;
    if (_ticks >= 1) _portal.hostSetClient(true);
    if (_ticks == 3) hostSendTime(1787305260000LL);  // 21 Aug 2026, 09:41 as the phone reads it
  }
  // The harness posts through the same door a phone would, including the door
  // slamming: a hook that says no leaves the page refused, never set.
  void hostSendTime(int64_t localEpochMs) {
    if (clockset::apply(localEpochMs))
      _set = true;
    else
      _refused = true;
  }
  bool wasSet() const { return _set; }
  bool wasRefused() const { return _refused; }
  bool hasClient() const { return _portal.hasClient(); }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  portal::Portal _portal;
  bool _set = false, _refused = false;
  int _ticks = 0;
};

#else

class ClockServer {
 public:
  bool start() {
    if (_portal.running()) return true;
    _set = false;
    _refused = false;
    if (!_portal.begin()) return false;
    _portal.serveIndex([this] { sendPage(); });
    _portal.on("/t", HTTP_POST, [this] { takeTime(); });
    return true;
  }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }
  bool wasSet() const { return _set; }
  // The phone sent, the firmware could not keep it. Its own state, because a
  // page that shows "waiting for the phone" after a refusal sends somebody
  // back to scan a QR code that was never the problem.
  bool wasRefused() const { return _refused; }
  bool hasClient() const { return _portal.hasClient(); }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }

 private:
  void takeTime() {
    WebServer& s = _portal.server();
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

  void sendPage();

  portal::Portal _portal;
  bool _set = false, _refused = false;
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
</style></head><body>
<h1>Set the device clock</h1>
<p>Toybox has no internet, so it takes the time from this phone.</p>
<div id="now">--:--</div>
<div id="msg">Sending&hellip;</div>
<button id="again">Send this time again</button>
<small>The device stores the digits, not your timezone. If you travel, open this
page again and send.</small>
<script>
function two(n){return (n<10?'0':'')+n}
function send(){
 var d=new Date();
 document.getElementById('now').textContent=two(d.getHours())+':'+two(d.getMinutes());
 var body='t='+(d.getTime()-d.getTimezoneOffset()*60000);
 fetch('/t',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(r){return r.json()})
  .then(function(j){document.getElementById('msg').textContent=
    j.ok?'Done. The device clock is set.':'The device did not keep it. Its clock chip is not answering \u2014 see the device screen.'})
  .catch(function(){document.getElementById('msg').textContent=
    'Could not reach the device. Still connected to its wifi?'});
}
document.getElementById('again').addEventListener('click',send);
send();
</script></body></html>)HTML";
  _portal.server().send_P(200, "text/html", kPage);
}

#endif

}  // namespace cweb
