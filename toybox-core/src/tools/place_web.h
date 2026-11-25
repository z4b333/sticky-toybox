// Climate's phone page (C5, owner 25 Nov): one field, the place for the
// outdoor weather. Served on the home Wi-Fi at toybox.local/place when a home
// network is saved -- the phone keeps its own network -- and on the Sticky's
// own access point as the backup, the same two roads as Recipes, Flashcards
// and Picker (lan_web.h, import_screen.h). The clock page's Weather place
// field (clock_web.h) stays as well; both write wx_city.
#pragma once
#include <Preferences.h>
#include <math.h>

#include "phone_kit.h"
#include "portal.h"
#include "weather.h"

namespace plweb {

#ifdef TOYBOX_HOST

class PlaceServer {
 public:
  bool start(Preferences& p) {
    _prefs = &p;
    _got = false;
    return _portal.begin();
  }
  bool startHome(Preferences& p, const char* path) {
    _prefs = &p;
    _got = false;
    return _portal.beginHome(p, path);
  }
  void stop() { _portal.end(); }
  void loop() {}
  // The harness's phone.
  bool hostSave(const char* city) {
    _got = _prefs && wx::saveCity(*_prefs, city);
    return _got;
  }
  // A suggestion tapped on the phone: name, coordinates and label together.
  bool hostPick(const char* city, int32_t lat, int32_t lon, const char* label) {
    _got = _prefs && wx::savePlace(*_prefs, city, lat, lon, label);
    return _got;
  }
  bool home() const { return _portal.home(); }
  const char* homeUrl() const { return _portal.homeUrl(); }
  const char* ipUrl() const { return _portal.ipUrl(); }
  bool received() const { return _got; }
  void clearReceived() { _got = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  portal::Portal _portal;
  Preferences* _prefs = nullptr;
  bool _got = false;
};

#else

class PlaceServer {
 public:
  bool start(Preferences& p) {
    _prefs = &p;
    _got = false;
    if (_portal.running()) return true;
    if (!_portal.begin()) return false;
    _portal.serveIndex([this] { sendPage(); });
    _portal.on("/p", HTTP_POST, [this] { take(); });
    return true;
  }
  bool startHome(Preferences& p, const char* path) {
    _prefs = &p;
    _got = false;
    if (!_portal.beginHome(p, path)) return false;
    _portal.on("/", HTTP_GET, [this] { sendPage(); });
    _portal.on(path, HTTP_GET, [this] { sendPage(); });
    _portal.on("/p", HTTP_POST, [this] { take(); });
    return true;
  }
  void stop() { _portal.end(); }
  void loop() { _portal.loop(); }
  bool home() const { return _portal.home(); }
  const char* homeUrl() const { return _portal.homeUrl(); }
  const char* ipUrl() const { return _portal.ipUrl(); }
  bool received() const { return _got; }
  void clearReceived() { _got = false; }
  const char* ssid() const { return _portal.ssid(); }
  const char* password() const { return _portal.password(); }
  const char* url() const { return _portal.url(); }
  String wifiPayload() const { return _portal.wifiPayload(); }
  bool hasClient() const { return _portal.hasClient(); }

 private:
  portal::Portal _portal;
  Preferences* _prefs = nullptr;
  bool _got = false;

  void take() {
    WebServer& s = _portal.server();
    bool ok = false;
    if (_prefs && s.hasArg("c")) {
      if (s.hasArg("la") && s.hasArg("lo"))  // picked from the suggestions
        ok = wx::savePlace(*_prefs, s.arg("c").c_str(), (int32_t)lroundf(s.arg("la").toFloat() * 10000.0f),
                           (int32_t)lroundf(s.arg("lo").toFloat() * 10000.0f),
                           s.hasArg("f") ? s.arg("f").c_str() : "");
      else
        ok = wx::saveCity(*_prefs, s.arg("c").c_str());
    }
    if (ok) _got = true;
    s.send(200, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
  }

  void sendPage() {
    WebServer& s = _portal.server();
    phonekit::begin(s, "Weather place", "Climate", "Weather place");
    String now;
    if (_prefs) {
      char c[wx::CITY_MAX];
      wx::city(*_prefs, c, sizeof(c));
      for (const char* q = c; *q; q++) {  // as an attribute value
        if (*q == '"') now += "&quot;";
        else if (*q == '<') now += "&lt;";
        else if (*q == '&') now += "&amp;";
        else now += *q;
      }
    }
    // Suggestions come from the phone, not the Sticky: on the home Wi-Fi the
    // phone has the internet, and Open-Meteo's place list answers a browser
    // directly. On the Sticky's own Wi-Fi there is no internet; the name is
    // then sent as typed and looked up on the first Refresh.
    String body = F("<div class=\"card\"><div class=\"lab\">Town or city</div>"
                    "<input id=\"c\" maxlength=\"47\" autocapitalize=\"words\" autocomplete=\"off\" "
                    "placeholder=\"Start typing, e.g. Bangk\" value=\"");
    body += now;
    body += F("\"><div id=\"s\" style=\"display:flex;flex-direction:column;gap:6px;margin-top:8px\"></div>"
              "<div class=\"hint\" id=\"h\" style=\"margin-top:8px\">Pick your place from the list as you "
              "type. Climate shows its weather next to the room when you tap Refresh on the Sticky.</div>"
              "</div><div class=\"err\" id=\"e\"></div><div class=\"ok\" id=\"ok\"></div></main>"
              "<div class=\"go\"><button id=\"g\" onclick=\"save()\">Save to device &rarr;</button></div>"
              "<script>var c=document.getElementById('c'),S=document.getElementById('s'),"
              "H=document.getElementById('h'),sel=null,tm=0,seq=0;"
              "function lab(r){var a=[r.name];if(r.admin1&&r.admin1!==r.name)a.push(r.admin1);"
              "if(r.country)a.push(r.country);return a.join(', ')}"
              "function show(list){S.innerHTML='';list.forEach(function(r){var b=document.createElement('button');"
              "b.className='chip';b.style.textAlign='left';b.textContent=lab(r);b.onclick=function(){"
              "sel={n:r.name,la:r.latitude,lo:r.longitude,f:(r.name+(r.country?', '+r.country:'')).slice(0,47)};"
              "c.value=r.name;S.innerHTML='';H.textContent='Picked: '+lab(r)};S.appendChild(b)})}"
              "c.addEventListener('input',function(){sel=null;clearTimeout(tm);var q=c.value.trim();"
              "if(q.length<2){S.innerHTML='';return}var my=++seq;tm=setTimeout(function(){"
              "fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&format=json&language=en&name='+"
              "encodeURIComponent(q)).then(function(r){return r.json()}).then(function(j){if(my!==seq)return;"
              "show(j.results||[]);if(!j.results)H.textContent='No place by that name. Try another spelling.'})"
              ".catch(function(){S.innerHTML='';H.textContent='No suggestions without the internet: type the "
              "name, the Sticky looks it up on Refresh.'})},350)});"
              "function save(){var v=c.value.trim(),e=document.getElementById('e'),o=document.getElementById('ok');"
              "e.textContent='';if(!v){e.textContent='Type a place first.';return}"
              "var b='c='+encodeURIComponent(sel?sel.n:v);if(sel)b+='&la='+sel.la+'&lo='+sel.lo+'&f='+"
              "encodeURIComponent(sel.f);"
              "fetch('/p',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})"
              ".then(function(r){return r.json()}).then(function(j){if(j.ok){o.style.display='block';"
              "o.textContent='Saved. On the Sticky, tap Done, then Refresh outdoor.'}else{"
              "e.textContent='The device did not accept that name.'}})"
              ".catch(function(){e.textContent='Could not reach the device.'})}</script>");
    s.sendContent(body);
    phonekit::end(s);
  }
};

#endif

}  // namespace plweb
