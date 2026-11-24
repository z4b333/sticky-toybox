// Phone-pasting import for recipes: the device raises its access point, shows
// the QR that joins it, and serves one page. Same shape as the flashcards
// import (flash_web.h) for the same reasons -- no credentials on an e-paper
// keyboard, captive portal opens the page by itself.
//
// The clever part runs in the PHONE's browser, which is where the horsepower
// is: paste a recipe page's source (or just its JSON) and the page's script
// digs the schema.org Recipe out of the <script type="application/ld+json">
// blocks, boils it down to compact standard JSON, and posts that. The device
// only ever stores and parses clean schema.org -- the same format the card
// route uses. A manual form underneath covers recipes that were never on a
// website at all.
#pragma once
#include <Arduino.h>

#include "lan_web.h"
#include "phone_kit.h"
#include "recipe_store.h"

#ifndef TOYBOX_HOST
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

namespace rweb {

#ifdef TOYBOX_HOST

// Host preview stub: pretends an AP is up, and can be handed a "post" so the
// guards walk the same store the real page feeds.
class RecipeServer {
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
  void loop() {}
  bool running() const { return _running; }
  bool received() const { return _received; }
  const char* recipeName() const { return _name; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return "http://192.168.4.1"; }
  String wifiPayload() const {
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }
  bool hasClient() const { return _fakeClient; }
  void hostSetClient(bool on) { _fakeClient = on; }
  // What handleSave() does on the device, callable without a network.
  bool hostPost(const char* json) {
    if (rstore::save(json, strlen(json), _name, sizeof(_name)) < 0) return false;
    _received = true;
    return true;
  }

 private:
  bool _running = false, _received = false, _fakeClient = false;
  bool _home = false;
  char _homeUrl[40] = {}, _ipUrl[40] = {};
  char _ssid[20] = {}, _pass[12] = {}, _name[rcp::NAME_LEN] = {};
};

#else

class RecipeServer {
 public:
  bool start() {
    if (_running) return true;
    _received = false;
    _name[0] = 0;

    uint8_t mac[6] = {};
    WiFi.softAPmacAddress(mac);
    snprintf(_ssid, sizeof(_ssid), "TOYBOX-%02X%02X", mac[4], mac[5]);
    for (int i = 0; i < 8; i++) _pass[i] = '0' + (char)(esp_random() % 10);
    _pass[8] = 0;

    // Built only here, never as a member by value: a DNSServer owns an
    // AsyncUDP whose destructor reaches into lwIP, and destroying one that
    // never ran asserts inside FreeRTOS on a mutex that does not exist. The
    // same lesson portal.h already carries -- this class learned it on glass,
    // backing out of the help card into the hub. See ~Portal.
    if (!_dns) _dns = new DNSServer();
    if (!_dns) return false;

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(_ssid, _pass)) return false;
    delay(200);
    _ip = WiFi.softAPIP();

    _dns->setErrorReplyCode(DNSReplyCode::NoError);
    _dns->start(53, "*", _ip);

    _server.on("/", HTTP_GET, [this] { sendPage(); });
    _server.on("/save", HTTP_POST, [this] { handleSave(); });
    _server.on("/fetch", HTTP_POST, [this] { handleFetch(); });
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
    _server.on("/fetch", HTTP_POST, [this] { handleFetch(); });
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
  const char* recipeName() const { return _name; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return _url; }
  String wifiPayload() const {
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }
  bool hasClient() const { return _running && !_home && WiFi.softAPgetStationNum() > 0; }

 private:
  void sendPage();

  // A link pasted on the phone (owner, 24 Nov): the phone's browser may not
  // read another site's page, but the Sticky, on the home Wi-Fi, can. It
  // fetches the page and hands back only its recipe data -- the
  // <script type="application/ld+json"> blocks -- for the phone to show and
  // confirm before anything is kept. Pages run to megabytes; the blocks are
  // a few kilobytes, so the page streams through a scanner, never into RAM.
  struct LdScan : public Stream {
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    String out;
    uint8_t state = 0;  // 0 look for ld+json, 1 wait for '>', 2 copy to </script
    bool inTag = false;
    char win[24] = {};
    size_t seen = 0;
    static constexpr size_t OUT_MAX = 96 * 1024, PAGE_MAX = 3 * 1024 * 1024;
    bool tail(const char* t) const {  // does the window end with t (lower case)?
      const size_t n = strlen(t);
      for (size_t i = 0; i < n; i++)
        if (win[sizeof(win) - n + i] != t[i]) return false;
      return true;
    }
    size_t write(uint8_t b) override {
      if (++seen > PAGE_MAX) return 0;  // stop the download
      memmove(win, win + 1, sizeof(win) - 1);
      win[sizeof(win) - 1] = (char)tolower(b);
      if (state == 0) {
        // Only inside a <script ...> tag: "ld+json" in a page's prose is not one.
        if (tail("<script")) inTag = true;
        else if (b == '>') inTag = false;
        else if (inTag && tail("ld+json")) state = 1;
      } else if (state == 1) {
        if (b == '>') {
          state = 2;
          out += "<script type=\"application/ld+json\">";
        }
      } else {
        if (out.length() < OUT_MAX) out += (char)b;
        if (tail("</script")) {
          out.remove(out.length() - 8);  // the "</script" just copied
          out += "</script>\n";
          state = 0;
          inTag = false;
        }
      }
      return 1;
    }
    size_t write(const uint8_t* p, size_t n) override {
      for (size_t i = 0; i < n; i++)
        if (!write(p[i])) return i;
      return n;
    }
  };

  void handleFetch() {
    const String url = _server.hasArg("url") ? _server.arg("url") : String();
    if (!_home) {
      _server.send(503, "text/plain",
                   "The Sticky's own Wi-Fi has no internet. Paste the page instead, or "
                   "save your home Wi-Fi on the Sticky first.");
      return;
    }
    if (!(url.startsWith("http://") || url.startsWith("https://")) || url.length() > 600) {
      _server.send(400, "text/plain", "That does not look like a link.");
      return;
    }
    NetworkClientSecure sec;
    sec.setInsecure();  // any recipe site; what comes back is only shown, then checked
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 Chrome/124 Mobile Safari/537.36");
    const bool ok = url.startsWith("https://") ? http.begin(sec, url) : http.begin(url);
    if (!ok) {
      _server.send(502, "text/plain", "The Sticky could not open that link.");
      return;
    }
    http.addHeader("Accept", "text/html,application/xhtml+xml");
    const int code = http.GET();
    if (code != 200) {
      http.end();
      _server.send(502, "text/plain",
                   code > 0 ? String("The site answered ") + code + ". Some sites only show recipes to a browser -- paste the page instead."
                            : String("The site did not answer. Try again, or paste the page instead."));
      return;
    }
    LdScan scan;
    http.writeToStream(&scan);
    http.end();
    if (scan.out.length() == 0) {
      _server.send(404, "text/plain", "No recipe data on that page. Paste the page instead, or type it in.");
      return;
    }
    _server.send(200, "text/plain", scan.out);
  }

  void handleSave() {
    const String data = _server.hasArg("data") ? _server.arg("data") : String();
    if (data.length() == 0 || data.length() > 24000) {
      _server.send(400, "text/html",
                   "<meta name=viewport content='width=device-width'>"
                   "<body style='font:16px system-ui;padding:24px'>"
                   "<h2>Nothing usable arrived</h2><a href='/'>Back</a>");
      return;
    }
    if (rstore::save(data.c_str(), data.length(), _name, sizeof(_name)) < 0) {
      _server.send(400, "text/html",
                   "<meta name=viewport content='width=device-width'>"
                   "<body style='font:16px system-ui;padding:24px'>"
                   "<h2>Could not keep it</h2>"
                   "<p>Either no recipe was found in what was pasted, or all twelve "
                   "slots are taken - remove one on the device first.</p>"
                   "<a href='/'>Back</a>");
      return;
    }
    _received = true;
    String body =
        "<meta name=viewport content='width=device-width'>"
        "<body style='font:16px system-ui;padding:24px;text-align:center'>"
        "<h2>Sent to Toybox</h2><p><b>";
    body += _name;
    body += "</b></p><p>You can close this page and disconnect.</p>"
            "<a href='/'>Send another</a>";
    _server.send(200, "text/html", body);
  }

  WebServer _server{80};
  DNSServer* _dns = nullptr;  // built in start(); a by-value one crashes on exit
  IPAddress _ip;
  bool _running = false, _received = false;
  char _ssid[20] = {}, _pass[12] = {}, _name[rcp::NAME_LEN] = {};
  char _url[24] = "http://192.168.4.1";
  bool _home = false;
  char _homeUrl[40] = {}, _ipUrl[40] = {};
};

// The page. Extraction happens here, in the browser: JSON.parse is free on a
// phone and costs a firmware on the device.
inline void RecipeServer::sendPage() {
  // Kit "Phone Recipe": From a website | Type it in; what was found shown as
  // a card to confirm; one black button at the foot that does the next thing
  // (find it, then keep it).
  static const char kBody[] PROGMEM = R"HTML(
<div class="seg" role="tablist"><button id="tabW" class="on" onclick="tab(0)">From a website</button><button id="tabT" onclick="tab(1)">Type it in</button></div>
<div id="web">
<div class="lab">A link, or the page pasted</div>
<textarea id="paste" style="min-height:96px" placeholder="https://... or paste the whole page"></textarea>
<div class="hint">On your home Wi-Fi a link is enough: the Sticky fetches the page. On the Sticky's own Wi-Fi (no internet), paste the page's text instead.</div>
</div>
<div id="typed" style="display:none;flex-direction:column;gap:14px">
<div><div class="lab">Name</div><input id="nm" placeholder="Weeknight dal"></div>
<div><div class="lab">Ingredients, one per line</div><textarea id="ing" style="min-height:120px" placeholder="1 cup red lentils&#10;2 cloves garlic"></textarea></div>
<div><div class="lab">Steps, one per line</div><textarea id="st" style="min-height:120px" placeholder="Rinse the lentils.&#10;Fry the garlic."></textarea></div>
</div>
<div class="err" id="err"></div>
<div id="preview" class="card" style="display:none">
<div style="display:flex;justify-content:space-between;gap:10px;align-items:flex-start">
<div><div class="eye">Found on the page</div><div id="pvName" style="font-size:20px;font-weight:700;line-height:1.25"></div></div>
<span class="tag" id="pvTime" style="display:none"></span></div>
<div id="pvMeta" class="hint" style="margin-top:4px"></div>
<div style="display:flex;gap:14px;margin-top:12px">
<div style="flex:1;min-width:0"><div class="lab" id="pvIngH">Ingredients</div><div id="pvIng" style="display:flex;flex-direction:column;gap:5px;font-size:14px"></div></div>
<div style="width:2px;background:#111"></div>
<div style="flex:1;min-width:0"><div class="lab" id="pvStH">Steps</div><div id="pvSt" style="display:flex;flex-direction:column;gap:6px;font-size:14px"></div></div>
</div>
<div class="chips" style="margin-top:14px"><button class="chip" onclick="restart()">Start over</button><span class="hint">Not right? Start over, or type it in.</span></div>
</div>
<div class="ok" id="ok"></div>
<div class="hint">On the Sticky: one step at a time, in big type. Up to twelve phone-sent recipes; the same name replaces the old one. More go on the card as .json in /recipes.</div>
</main>
<div class="go"><button id="go" onclick="go()">FIND THE RECIPE &rarr;</button></div>
<script>
)HTML";
  static const char kScript[] PROGMEM = R"HTML(
function firstRecipe(o){
  if(!o||typeof o!=="object")return null;
  var t=o["@type"];
  if(t==="Recipe"||(Array.isArray(t)&&t.indexOf("Recipe")>=0))return o;
  if(Array.isArray(o)){for(var i=0;i<o.length;i++){var r=firstRecipe(o[i]);if(r)return r}return null}
  for(var k in o){if(o[k]&&typeof o[k]==="object"){var r2=firstRecipe(o[k]);if(r2)return r2}}
  return null;
}
function extract(text){
  var cands=[];
  try{cands.push(JSON.parse(text))}catch(e){}
  var re=/<script[^>]*ld\+json[^>]*>([\s\S]*?)<\/script>/gi,m;
  while((m=re.exec(text))){try{cands.push(JSON.parse(m[1]))}catch(e){}}
  for(var i=0;i<cands.length;i++){var r=firstRecipe(cands[i]);if(r)return r}
  return null;
}
function txt(v){return v==null?"":(typeof v==="string"?v:String(v))}
function flatSteps(v,out){
  if(v==null)return;
  if(typeof v==="string"){if(v.trim())out.push(v.trim());return}
  if(Array.isArray(v)){for(var i=0;i<v.length;i++)flatSteps(v[i],out);return}
  if(typeof v==="object"){
    if(v.itemListElement)return flatSteps(v.itemListElement,out);
    var t=txt(v.text)||txt(v.name);if(t.trim())out.push(t.trim());
  }
}
function compact(r){
  var steps=[];flatSteps(r.recipeInstructions||r.instructions,steps);
  var ing=(r.recipeIngredient||r.ingredients||[]).map(txt).filter(function(s){return s.trim()});
  var y=r.recipeYield;if(Array.isArray(y))y=y[y.length-1];
  var o={"@type":"Recipe",name:txt(r.name).trim()||"recipe",
    recipeIngredient:ing.slice(0,24),recipeInstructions:steps.slice(0,24)};
  if(y!=null)o.recipeYield=txt(y);
  if(r.totalTime)o.totalTime=txt(r.totalTime);
  if(r.prepTime)o.prepTime=txt(r.prepTime);
  if(r.cookTime)o.cookTime=txt(r.cookTime);
  return o;
}
function manual(){
  var nm=document.getElementById("nm").value.trim();
  var ing=document.getElementById("ing").value.split("\n").map(function(s){return s.trim()}).filter(Boolean);
  var st=document.getElementById("st").value.split("\n").map(function(s){return s.trim()}).filter(Boolean);
  if(!nm||(ing.length===0&&st.length===0))return null;
  return {"@type":"Recipe",name:nm,recipeIngredient:ing.slice(0,24),recipeInstructions:st.slice(0,24)};
}
var pending=null,mode=0;
function el(id){return document.getElementById(id)}
function tab(i){mode=i;el("tabW").className=i?"":"on";el("tabT").className=i?"on":"";
  el("web").style.display=i?"none":"block";el("typed").style.display=i?"flex":"none";restart()}
function setGo(t,dis){var b=el("go");b.innerHTML=t+" &rarr;";b.disabled=!!dis}
function row(box,t,i){var d=document.createElement("div");d.style.display="flex";d.style.gap="6px";
  var m=document.createElement("span");
  if(i==null){m.style.cssText="width:11px;height:11px;border:1.5px solid #111;flex:none;margin-top:5px"}
  else{m.className="mono";m.style.cssText="background:#111;color:#FFD84D;padding:0 5px;height:20px;font-weight:700;flex:none";m.textContent=i}
  var s=document.createElement("span");s.textContent=t;d.appendChild(m);d.appendChild(s);box.appendChild(d)}
function show(r){
  pending=r;el("err").textContent="";el("ok").style.display="none";
  el("pvName").textContent=r.name;
  var tm=r.totalTime||r.cookTime||"";tm=tm.replace(/^PT/,"").replace("H"," h ").replace("M"," min").trim();
  el("pvTime").style.display=tm?"inline-block":"none";el("pvTime").textContent=tm.toUpperCase();
  el("pvMeta").textContent=r.recipeYield?("makes "+r.recipeYield):"";
  el("pvIngH").textContent="Ingredients · "+r.recipeIngredient.length;
  el("pvStH").textContent="Steps · "+r.recipeInstructions.length;
  var a=el("pvIng");a.innerHTML="";r.recipeIngredient.forEach(function(t){row(a,t)});
  var b=el("pvSt");b.innerHTML="";r.recipeInstructions.forEach(function(t,i){row(b,t,i+1)});
  el("preview").style.display="block";el("preview").scrollIntoView({behavior:"smooth"});
  setGo("SEND TO TOYBOX");
}
function restart(){pending=null;el("preview").style.display="none";el("err").textContent="";
  setGo(mode?"CHECK IT":"FIND THE RECIPE")}
function go(){if(pending)return keep();if(mode)return checkManual();find()}
function find(){
  var err=el("err");err.textContent="";
  var p=el("paste").value.trim();
  if(!p){err.textContent="Paste a link or a page first.";return}
  if(/^https?:\/\/\S+$/i.test(p)){
    setGo("THE STICKY IS FETCHING IT",1);
    fetch("/fetch",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"url="+encodeURIComponent(p)})
      .then(function(res){return res.text().then(function(t){
        setGo("FIND THE RECIPE");
        if(!res.ok){err.textContent=t;return}
        var r=extract(t);if(!r){err.textContent="The page had data, but no recipe in it.";return}
        show(compact(r));
      })})
      .catch(function(){setGo("FIND THE RECIPE");err.textContent="Could not reach the Sticky. Is the phone on the same Wi-Fi?"});
    return;
  }
  var r=extract(p);
  if(!r){err.textContent="No recipe found in the paste. Try a link, or type it in.";return}
  show(compact(r));
}
function checkManual(){var r=manual();if(!r){el("err").textContent="Fill in a name and some lines.";return}show(r)}
function keep(){
  setGo("SENDING",1);
  fetch("/save",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},
    body:"data="+encodeURIComponent(JSON.stringify(pending))})
    .then(function(res){return res.text().then(function(t){
      if(!res.ok){setGo("SEND TO TOYBOX");el("err").textContent=t.replace(/<[^>]*>/g," ").replace(/\s+/g," ").replace(/Back$/,"").trim();return}
      var ok=el("ok");ok.style.display="block";ok.textContent="Kept on the Sticky: "+pending.name+". Send another, or tap Done on the Sticky.";
      el("paste").value="";pending=null;el("preview").style.display="none";setGo(mode?"CHECK IT":"FIND THE RECIPE");
    })})
    .catch(function(){setGo("SEND TO TOYBOX");el("err").textContent="Could not reach the Sticky. Is the phone still on its network?"});
}
</script>)HTML";
  phonekit::begin(_server, "Send a recipe to Toybox", "From a website or by hand", "Recipes");
  _server.sendContent_P(kBody);
  _server.sendContent_P(kScript);
  phonekit::end(_server);
}

#endif

}  // namespace rweb
