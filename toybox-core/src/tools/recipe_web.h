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

#include "recipe_store.h"

#ifndef TOYBOX_HOST
#include <DNSServer.h>
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
  void stop() { _running = false; }
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

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(_ssid, _pass)) return false;
    delay(200);
    _ip = WiFi.softAPIP();

    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", _ip);

    _server.on("/", HTTP_GET, [this] { sendPage(); });
    _server.on("/save", HTTP_POST, [this] { handleSave(); });
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
  const char* recipeName() const { return _name; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return _url; }
  String wifiPayload() const {
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }
  bool hasClient() const { return _running && WiFi.softAPgetStationNum() > 0; }

 private:
  void sendPage();
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
  DNSServer _dns;
  IPAddress _ip;
  bool _running = false, _received = false;
  char _ssid[20] = {}, _pass[12] = {}, _name[rcp::NAME_LEN] = {};
  char _url[24] = "http://192.168.4.1";
};

// The page. Extraction happens here, in the browser: JSON.parse is free on a
// phone and costs a firmware on the device.
inline void RecipeServer::sendPage() {
  static const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Send a recipe to Toybox</title><style>
*{box-sizing:border-box}
body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:20px 18px 40px;
background:#f5f5f3;color:#111;max-width:620px;margin-inline:auto}
h1{font-size:21px;margin:0 0 2px}
h2{font-size:15px;margin:26px 0 8px;text-transform:uppercase;letter-spacing:.06em;color:#555}
p.sub{margin:0 0 18px;color:#666;font-size:14px}
label{display:block;font-weight:600;margin:14px 0 6px;font-size:15px}
input,textarea{width:100%;padding:12px;border:1px solid #ccc;border-radius:9px;font:inherit;
background:#fff;-webkit-appearance:none}
textarea{min-height:150px;font-size:14px}
button{width:100%;padding:16px;margin-top:16px;border:0;border-radius:9px;background:#111;
color:#fff;font:600 17px system-ui;cursor:pointer}
.hint{font-size:13px;color:#666;margin-top:6px}
ol.steps{margin:0 0 4px;padding-left:22px;font-size:15px}
ol.steps li{margin:5px 0}
.note{font-size:13px;color:#666;background:#eeeeea;border-radius:8px;padding:10px 12px;margin-top:12px}
.err{color:#b00020;font-weight:600;margin-top:10px;min-height:20px}
</style></head><body>
<h1>Send a recipe to Toybox</h1>
<p class="sub">From any recipe website, or typed in below.</p>

<h2>From a website</h2>
<ol class="steps">
<li>Open the recipe in your browser <b>before</b> joining this network (this network has no internet).</li>
<li>Select all of the page (or use your browser's share &rarr; copy, or view-source and copy that).</li>
<li>Paste it here. The recipe is found automatically.</li>
</ol>
<label for="paste">The page, pasted</label>
<textarea id="paste" placeholder="paste the whole page here - the recipe will be dug out"></textarea>
<div class="hint">Works with the hidden recipe data (schema.org JSON-LD) that
nearly every recipe site embeds for search engines.</div>

<h2>Or type it in</h2>
<label for="nm">Name</label><input id="nm" placeholder="Weeknight dal">
<label for="ing">Ingredients, one per line</label>
<textarea id="ing" placeholder="1 cup red lentils&#10;2 cloves garlic"></textarea>
<label for="st">Steps, one per line</label>
<textarea id="st" placeholder="Rinse the lentils.&#10;Fry the garlic."></textarea>

<button onclick="send()">SEND TO TOYBOX</button>
<div class="err" id="err"></div>
<div class="note">The device keeps up to twelve phone-sent recipes; a recipe
with the same name replaces the old one. Larger collections go on the SD
card as .json files in /recipes.</div>
<script>
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
function send(){
  var err=document.getElementById("err");err.textContent="";
  var r=null,p=document.getElementById("paste").value;
  if(p.trim()){r=extract(p);if(!r){err.textContent="No recipe found in the paste - try the typed-in form below.";return}r=compact(r)}
  else{r=manual();if(!r){err.textContent="Paste a page above, or fill in a name and some lines below.";return}}
  var body="data="+encodeURIComponent(JSON.stringify(r));
  fetch("/save",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:body})
    .then(function(res){return res.text().then(function(t){document.open();document.write(t);document.close()})})
    .catch(function(){err.textContent="Could not reach the device - is the phone still on its network?"});
}
</script></body></html>)HTML";
  _server.send_P(200, "text/html", kPage);
}

#endif

}  // namespace rweb
