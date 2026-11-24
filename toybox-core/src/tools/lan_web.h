// The phone pages on the HOME Wi-Fi (owner, 24 Nov): when a home network is
// saved, Recipes, Flashcards and Picker join it and serve their page at
// toybox.local, the same way updates from a computer do -- the phone stays on
// its own network, with its internet. The Sticky's own access point stays as
// the backup, for no saved network or one that does not answer.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "home_net.h"

#ifndef TOYBOX_HOST
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

namespace lanweb {

#ifdef TOYBOX_HOST
inline bool join(Preferences& p) { return homenet::connect(p) == homenet::Result::Ok; }
inline void leave() { homenet::off(); }
inline void ipUrl(const char* path, char* out, size_t n) { snprintf(out, n, "http://192.168.1.42%s", path); }
#else
// Joins the saved network (the backup one too) and answers to toybox.local.
inline bool join(Preferences& p) {
  if (homenet::connect(p) != homenet::Result::Ok) return false;
  MDNS.begin("toybox");
  MDNS.addService("http", "tcp", 80);
  return true;
}
inline void leave() {
  MDNS.end();
  homenet::off();
}
// The address as numbers, for phones whose browser will not look up .local.
inline void ipUrl(const char* path, char* out, size_t n) {
  snprintf(out, n, "http://%s%s", WiFi.localIP().toString().c_str(), path);
}
#endif

}  // namespace lanweb
