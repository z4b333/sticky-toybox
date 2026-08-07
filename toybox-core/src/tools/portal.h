// Phone pairing plumbing, shared by every tool that needs a phone.
//
// The device raises its own access point and serves a captive portal, so there
// are no credentials to type on an e-paper keyboard, it works away from home,
// and the phone camera can join straight from a `WIFI:` QR (iOS 11+/Android
// 10+). The captive DNS answers every lookup with our own address, which makes
// the phone pop the page open by itself — so the user only ever scans one thing.
//
// A tool calls begin(), registers its routes, and pumps loop(). Only one tool is
// open at a time, so a single instance is enough. The AP lives only while that
// screen is open and the password is regenerated every session.
#pragma once
#include <Arduino.h>

#ifndef TOYBOX_HOST
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

namespace portal {

#ifdef TOYBOX_HOST

// Host preview stub: pretends an AP is up so the screens can be rendered.
class Portal {
 public:
  bool begin() {
    strcpy(_ssid, "TOYBOX-4F2A");
    strcpy(_pass, "58204617");
    _running = true;
    return true;
  }
  void end() { _running = false; }
  void loop() {}
  bool running() const { return _running; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return "http://192.168.4.1"; }
  String wifiPayload() const {
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }

 private:
  bool _running = false;
  char _ssid[20] = {}, _pass[12] = {};
};

#else

class Portal {
 public:
  using Handler = std::function<void()>;

  bool begin() {
    if (_running) return true;

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
    _dns.start(53, "*", _ip);  // every lookup resolves here -> captive portal
    _server.begin();
    _running = true;
    return true;
  }

  void end() {
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

  // Point every entry path at the tool's page: "/", the captive-portal probes
  // iOS, Android and Windows use, and anything else the phone asks for.
  // Answering the probes with the page itself is what makes the "sign in to
  // network" sheet show the editor rather than a browser tab the user must find.
  void serveIndex(Handler h) {
    _server.on("/", HTTP_GET, h);
    _server.on("/hotspot-detect.html", HTTP_GET, h);
    _server.on("/generate_204", HTTP_GET, h);
    _server.on("/gen_204", HTTP_GET, h);
    _server.on("/ncsi.txt", HTTP_GET, h);
    _server.on("/connecttest.txt", HTTP_GET, h);
    _server.onNotFound(h);
  }
  void on(const char* path, HTTPMethod method, Handler h) { _server.on(path, method, h); }

  WebServer& server() { return _server; }
  bool running() const { return _running; }
  const char* ssid() const { return _ssid; }
  const char* password() const { return _pass; }
  const char* url() const { return _url; }
  String wifiPayload() const {
    // zxing WiFi config format, understood natively by phone cameras.
    return String("WIFI:T:WPA;S:") + _ssid + ";P:" + _pass + ";;";
  }

 private:
  WebServer _server{80};
  DNSServer _dns;
  IPAddress _ip;
  bool _running = false;
  char _ssid[20] = {}, _pass[12] = {};
  char _url[24] = "http://192.168.4.1";
};

#endif

}  // namespace portal
