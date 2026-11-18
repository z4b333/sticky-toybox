// KOReader progress sync: the reading position, shared with Readest and
// KOReader through a KOReader sync server (sync.koreader.rocks by default, or
// a self-hosted one).
//
// The protocol is small and old: an account is a user name and the MD5 of a
// password; a book is the MD5 of a few samples of its bytes ("binary"
// matching -- what Readest asks you to choose); a position is an xpointer
// plus a fraction of the book. GET /syncs/progress/<book> reads the last one
// stored, PUT /syncs/progress stores ours.
//
// What the device does with it is decided in decide() and nowhere else, so
// the harness can hold the rules still:
//   - nothing stored yet, or the last record is this device's own: upload;
//   - another reader is further on: ASK, never jump on its own (owner,
//     18 Nov) -- the answer comes from the person, on the panel;
//   - another reader is behind: upload ours.
//
// Account keys in NVS: ks_url, ks_user, ks_key (the MD5, never the password).
//
// Readest (owner, 18 Nov): the device asks web.readest.com for a sign-in
// code, shows it as a QR code, and polls while the owner approves it on a
// phone signed in to Readest. The answer is a device key; stored as the
// "password" of a KOReader account on READEST_URL, everything below works
// unchanged -- Readest's server checks x-auth-key = md5(key), keeps the
// timestamps itself (no clock needed here) and links the book by its
// partial MD5, the same id KOReader and Readest use.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "epub/epubcore.h"
#include "md5_tiny.h"

#ifndef TOYBOX_HOST
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#else
#include <map>
#include <string>
#endif

namespace kosync {

inline constexpr const char* K_URL = "ks_url";
inline constexpr const char* K_USER = "ks_user";
inline constexpr const char* K_KEY = "ks_key";
inline constexpr const char* DEFAULT_URL = "https://sync.koreader.rocks";
// Readest's own cloud, behind the same KOReader protocol (Readest added it
// for CrossPoint readers): progress lands in the book config every Readest
// app syncs, so Readest on a PC or a phone and KOReader with the Readest
// plugin all meet there. Signed in with a code, never a password.
inline constexpr const char* READEST_URL = "https://web.readest.com/api/crosspoint";
inline constexpr const char* DEVICE_NAME = "Toybox";
inline constexpr size_t URL_MAX = 96, USER_MAX = 64, XP_MAX = 240;

// --- the account -------------------------------------------------------------

inline bool haveAccount(Preferences& p) { return p.isKey(K_USER) && p.isKey(K_KEY); }

inline void url(Preferences& p, char* out, size_t n) {
  snprintf(out, n, "%s", DEFAULT_URL);
  if (p.isKey(K_URL)) p.getString(K_URL, out, n);
}

inline void user(Preferences& p, char* out, size_t n) {
  out[0] = 0;
  if (p.isKey(K_USER)) p.getString(K_USER, out, n);
}

// The password is hashed here and never stored. An empty server means the
// public one. Returns false, storing nothing, on anything a server would only
// reject later.
inline bool saveAccount(Preferences& p, const char* server, const char* name,
                        const char* password) {
  if (!name || !name[0] || strlen(name) >= USER_MAX) return false;
  if (!password || !password[0]) return false;
  char base[URL_MAX];
  snprintf(base, sizeof(base), "%s", (server && server[0]) ? server : DEFAULT_URL);
  if (strlen(server ? server : "") >= URL_MAX) return false;
  if (strncmp(base, "https://", 8) != 0 && strncmp(base, "http://", 7) != 0) return false;
  size_t n = strlen(base);
  while (n > 0 && base[n - 1] == '/') base[--n] = 0;
  for (const char* s = name; *s; s++)
    if ((uint8_t)*s < 0x20 || *s == '"' || *s == '\\') return false;  // headers and JSON
  char key[33];
  md5t::hex(password, key);
  p.putString(K_URL, base);
  p.putString(K_USER, name);
  p.putString(K_KEY, key);
  return true;
}

inline void forgetAccount(Preferences& p) {
  if (p.isKey(K_URL)) p.remove(K_URL);
  if (p.isKey(K_USER)) p.remove(K_USER);
  if (p.isKey(K_KEY)) p.remove(K_KEY);
}

// --- naming a book -----------------------------------------------------------

// KOReader's "binary" document id: MD5 over up to 1 KB read at 0, 1 KB,
// 4 KB, 16 KB, ... 1 KB << 20, stopping at the end of the file. (KOReader's
// loop starts at i = -1, and LuaJIT turns 1024 << -2 into 0.) Checked against
// CrossPoint's implementation and an independent one in the harness.
inline bool docHash(epubc::IO& io, char out[33]) {
  md5t::Md5 m;
  const uint32_t size = io.size();
  static uint8_t buf[1024];  // static: the loop task's stack is small (check_stack.py)
  for (int i = -1; i <= 10; i++) {
    const uint32_t off = i < 0 ? 0 : (1024u << (2 * i));
    if (off >= size) break;
    const uint32_t want = (size - off) < 1024 ? (size - off) : 1024;
    const int got = io.read(off, buf, want);
    if (got < 0) return false;  // a half-read book must not get a name
    m.add(buf, (size_t)got);
  }
  m.finishHex(out);
  return true;
}

// --- a stored position -------------------------------------------------------

struct Remote {
  bool have = false;
  char progress[XP_MAX] = {};
  double percentage = 0;
  char device[40] = {};
  char deviceId[40] = {};
  int64_t timestamp = 0;
};

// The value of "key" in a flat JSON object, unescaped into `out`. Enough JSON
// for the five fields a sync server sends back; nested objects are skipped.
inline bool jsonField(const char* js, const char* key, char* out, size_t cap, bool* isStr = nullptr) {
  if (!js || !out || cap == 0) return false;
  out[0] = 0;
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = js;
  while ((p = strstr(p, pat)) != nullptr) {
    const char* q = p + strlen(pat);
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
    if (*q != ':') {
      p = q;
      continue;
    }
    q++;
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
    size_t n = 0;
    if (*q == '"') {
      if (isStr) *isStr = true;
      q++;
      while (*q && *q != '"') {
        char ch = *q++;
        if (ch == '\\' && *q) {
          const char e = *q++;
          if (e == 'u') {
            unsigned v = 0;
            for (int k = 0; k < 4 && *q; k++, q++)
              v = v * 16 + (unsigned)(*q >= 'a' ? *q - 'a' + 10 : *q >= 'A' ? *q - 'A' + 10 : *q - '0');
            // UTF-8 out; surrogate pairs are not something a position contains.
            char u[3];
            int ul = 0;
            if (v < 0x80) u[ul++] = (char)v;
            else if (v < 0x800) {
              u[ul++] = (char)(0xC0 | (v >> 6));
              u[ul++] = (char)(0x80 | (v & 0x3F));
            } else {
              if (n + 3 < cap) {
                out[n++] = (char)(0xE0 | (v >> 12));
                out[n++] = (char)(0x80 | ((v >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (v & 0x3F));
              }
              continue;
            }
            for (int k = 0; k < ul && n + 1 < cap; k++) out[n++] = u[k];
            continue;
          }
          ch = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
        }
        if (n + 1 < cap) out[n++] = ch;
      }
    } else {
      if (isStr) *isStr = false;
      while (*q && *q != ',' && *q != '}' && *q != ' ' && *q != '\n' && *q != '\r' && n + 1 < cap)
        out[n++] = *q++;
      if (n == 4 && !strncmp(out, "null", 4)) n = 0;
    }
    out[n] = 0;
    return n > 0;
  }
  return false;
}

// A GET's body. An empty object ("{}") is a server saying it has nothing.
inline bool parseRemote(const char* body, Remote& r) {
  r = Remote();
  char num[32];
  jsonField(body, "progress", r.progress, sizeof(r.progress));
  if (jsonField(body, "percentage", num, sizeof(num))) r.percentage = atof(num);
  jsonField(body, "device", r.device, sizeof(r.device));
  jsonField(body, "device_id", r.deviceId, sizeof(r.deviceId));
  if (jsonField(body, "timestamp", num, sizeof(num))) r.timestamp = atoll(num);
  r.have = r.progress[0] != 0;
  if (r.percentage < 0) r.percentage = 0;
  if (r.percentage > 1) r.percentage = 1;
  return r.have;
}

// A PUT's body. The xpointer is ours and plain ASCII, but escaped anyway.
inline int buildPut(char* out, int cap, const char* doc, const char* xp, double pct,
                    const char* devId) {
  static char esc[XP_MAX * 2];  // static: off the loop task's stack
  int n = 0;
  for (const char* s = xp; *s && n < (int)sizeof(esc) - 2; s++) {
    if (*s == '"' || *s == '\\') esc[n++] = '\\';
    esc[n++] = *s;
  }
  esc[n] = 0;
  return snprintf(out, (size_t)cap,
                  "{\"document\":\"%s\",\"progress\":\"%s\",\"percentage\":%.6f,"
                  "\"device\":\"%s\",\"device_id\":\"%s\"}",
                  doc, esc, pct, DEVICE_NAME, devId);
}

// This device's id: the MD5 of its MAC, so it survives reflashes and is the
// same string every sync (KOReader uses it to recognise its own records).
inline void deviceId(char out[33]) {
#ifdef TOYBOX_HOST
  md5t::hex("toybox-host", out);
#else
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char s[24];
  snprintf(s, sizeof(s), "toybox-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  md5t::hex(s, out);
#endif
}

// --- the rule ----------------------------------------------------------------

enum class Decision : uint8_t { Upload, AskJump, Same };

// One page of a long book is well under a thousandth of it; two readers that
// close are at the same place, and asking about it would be noise.
inline constexpr double SAME_EPS = 0.001;

// The same rule by paragraph, once the other reader's xpointer has been found
// in this file: on the page shown (start inclusive, end exclusive, in the
// reader's visible-codepoint offsets) is the same place; past it is further
// on; before it is behind. Two readers' percentages measure different things
// (bytes here, Readest's own pages there), so the percentage is only the
// fallback for a path this file does not have.
inline Decision decideAt(int theirSpine, uint32_t theirOff, int mySpine, uint32_t pageStart,
                         uint32_t pageEnd) {
  if (theirSpine != mySpine) return theirSpine > mySpine ? Decision::AskJump : Decision::Upload;
  if (theirOff < pageStart) return Decision::Upload;
  if (theirOff < pageEnd) return Decision::Same;
  return Decision::AskJump;
}

inline Decision decide(double localPct, const Remote& r, const char* myId) {
  if (!r.have) return Decision::Upload;
  if (r.deviceId[0] && myId && !strcmp(r.deviceId, myId)) return Decision::Upload;
  if (fabs(r.percentage - localPct) < SAME_EPS) return Decision::Same;
  return r.percentage > localPct ? Decision::AskJump : Decision::Upload;
}

// A percentage, placed in a book this reader has: the chapter by the same
// byte measure the reader reports its own percentage in, and the place in the
// chapter by its share of the chapter's characters. The fallback for an
// xpointer this file does not contain.
inline bool placeFraction(epubc::Book& b, double pct, int& spine, uint32_t& off) {
  const uint32_t total = b.spineTotalBytes();
  if (total == 0 || b.spineCount() <= 0) return false;
  if (pct < 0) pct = 0;
  if (pct > 1) pct = 1;
  const double at = pct * (double)total;
  double before = 0;
  int i = 0;
  for (; i < b.spineCount() - 1; i++) {
    if (before + b.spineBytes(i) > at) break;
    before += b.spineBytes(i);
  }
  const uint32_t bytes = b.spineBytes(i);
  double f = bytes ? (at - before) / (double)bytes : 0;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  spine = i;
  off = (uint32_t)(f * (double)b.chapterLength(i));
  return true;
}

inline bool isReadest(Preferences& p) {
  char u[URL_MAX];
  url(p, u, sizeof(u));
  return haveAccount(p) && !strcmp(u, READEST_URL);
}

// One sign-in in progress (RFC 8628 device authorization).
struct DeviceCode {
  char device[96] = {};    // secret; only ever sent back to the server
  char user[16] = {};      // what the person types if they cannot scan
  char uri[64] = {};       // where they type it
  char uriFull[96] = {};   // the same page with the code filled in: the QR code
  uint32_t expiresS = 600, intervalS = 5;
};

enum class Poll : uint8_t { Pending, Approved, Expired, Error };

// The body of a code answer.
inline bool parseCode(const char* body, DeviceCode& c) {
  c = DeviceCode();
  char num[16];
  jsonField(body, "device_code", c.device, sizeof(c.device));
  jsonField(body, "user_code", c.user, sizeof(c.user));
  jsonField(body, "verification_uri", c.uri, sizeof(c.uri));
  jsonField(body, "verification_uri_complete", c.uriFull, sizeof(c.uriFull));
  if (jsonField(body, "expires_in", num, sizeof(num))) c.expiresS = (uint32_t)atol(num);
  if (jsonField(body, "interval", num, sizeof(num))) c.intervalS = (uint32_t)atol(num);
  if (c.intervalS < 2) c.intervalS = 2;
  if (!c.uriFull[0]) snprintf(c.uriFull, sizeof(c.uriFull), "%s", c.uri);
  return c.device[0] && c.user[0] && c.uri[0];
}

// The body of a token answer: approved (the account is saved), still
// pending, or over.
inline Poll takeToken(Preferences& p, int status, const char* body) {
  char key[80], name[USER_MAX], err[32];
  if (status == 200 && jsonField(body, "access_token", key, sizeof(key))) {
    if (!jsonField(body, "username", name, sizeof(name))) snprintf(name, sizeof(name), "Readest");
    // The key is the account's password: saveAccount keeps only its MD5,
    // which is exactly the x-auth-key Readest checks.
    return saveAccount(p, READEST_URL, name, key) ? Poll::Approved : Poll::Error;
  }
  if (jsonField(body, "error", err, sizeof(err))) {
    if (!strcmp(err, "authorization_pending") || !strcmp(err, "slow_down")) return Poll::Pending;
    if (!strcmp(err, "expired_token") || !strcmp(err, "access_denied")) return Poll::Expired;
  }
  return Poll::Error;
}

// --- talking to the server ---------------------------------------------------

enum class Result : uint8_t {
  Ok,
  NoRecord,      // GET: the server has nothing for this book yet
  NoAccount,
  AuthFailed,    // 401: name or password
  NetworkError,  // no answer, TLS failure, DNS
  ServerError,   // anything else it said
  LowMemory,
};

inline const char* describe(Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::NoRecord: return "nothing stored yet";
    case Result::NoAccount: return "no sync account saved";
    case Result::AuthFailed: return "sync account refused";
    case Result::NetworkError: return "could not reach the sync server";
    case Result::ServerError: return "the sync server said no";
    case Result::LowMemory: return "not enough memory to connect";
  }
  return "";
}

#ifdef TOYBOX_HOST

// A sync server in a map, for the harness: records by document, plus a
// switch to make it fail the way real ones do.
struct HostServer {
  std::map<std::string, std::string> rec;  // document -> stored JSON
  Result fail = Result::Ok;
  int gets = 0, puts = 0;
  std::string lastPut;
  // Readest sign-in: answers pending this many polls, then approves (or
  // expires, when `expire` is set).
  int pendingPolls = 2, polls = 0, codes = 0;
  bool expire = false, codeFail = false;
};
inline HostServer g_server;

inline Result requestCode(DeviceCode& c) {
  g_server.codes++;
  if (g_server.codeFail) return Result::NetworkError;
  parseCode("{\"device_code\":\"dc-secret\",\"user_code\":\"BCDF-GHJK\","
            "\"verification_uri\":\"https://web.readest.com/link\","
            "\"verification_uri_complete\":\"https://web.readest.com/link?code=BCDF-GHJK\","
            "\"expires_in\":600,\"interval\":5}",
            c);
  return Result::Ok;
}

inline Poll pollCode(Preferences& p, const DeviceCode& c) {
  g_server.polls++;
  if (strcmp(c.device, "dc-secret") != 0) return Poll::Error;
  if (g_server.polls <= g_server.pendingPolls)
    return takeToken(p, 400, "{\"error\":\"authorization_pending\"}");
  if (g_server.expire) return takeToken(p, 400, "{\"error\":\"expired_token\"}");
  return takeToken(p, 200,
                   "{\"access_token\":\"devkey-123\",\"token_type\":\"bearer\","
                   "\"id\":\"d1\",\"username\":\"reader@example.com\"}");
}

inline Result get(Preferences& p, const char* doc, Remote& out) {
  out = Remote();
  if (!haveAccount(p)) return Result::NoAccount;
  g_server.gets++;
  if (g_server.fail != Result::Ok) return g_server.fail;
  auto it = g_server.rec.find(doc);
  if (it == g_server.rec.end()) return Result::NoRecord;
  return parseRemote(it->second.c_str(), out) ? Result::Ok : Result::NoRecord;
}

inline Result put(Preferences& p, const char* doc, const char* xp, double pct) {
  if (!haveAccount(p)) return Result::NoAccount;
  g_server.puts++;
  if (g_server.fail != Result::Ok) return g_server.fail;
  char id[33], body[XP_MAX * 2 + 200];
  deviceId(id);
  buildPut(body, sizeof(body), doc, xp, pct, id);
  g_server.lastPut = body;
  g_server.rec[doc] = body;
  return Result::Ok;
}

#else

// The certificate bundle ESP-IDF already links into mbedTLS: the same list
// of public roots a browser trusts, so a self-hosted server on Let's Encrypt
// works as well as the public one. Never setInsecure(): the account key
// travels in these headers.
extern "C" const uint8_t kosync_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t kosync_bundle_end[] asm("_binary_x509_crt_bundle_end");

// One HTTP(S) exchange. `user`/`key` empty = no KOReader auth headers.
// Returns the status, or <= 0 for no answer at all.
inline int call(const char* fullUrl, const char* method, const char* body, const char* name,
                const char* key, String* reply) {
  // A TLS handshake wants ~40 KB of internal RAM at once.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 24000) return -100;
  const bool tls = !strncmp(fullUrl, "https://", 8);
  NetworkClientSecure secure;
  NetworkClient plain;
  if (tls) secure.setCACertBundle(kosync_bundle_start, kosync_bundle_end - kosync_bundle_start);
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(10000);
  const bool began = tls ? http.begin(secure, fullUrl) : http.begin(plain, fullUrl);
  if (!began) return -1;
  http.addHeader("Accept", "application/vnd.koreader.v1+json");
  if (name && name[0]) http.addHeader("x-auth-user", name);
  if (key && key[0]) http.addHeader("x-auth-key", key);
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/json");
    code = http.sendRequest(method, (uint8_t*)body, strlen(body));
  } else {
    code = http.sendRequest(method);
  }
  if (code > 0 && reply) *reply = http.getString();
  http.end();
  return code;
}

inline Result request(Preferences& p, const char* method, const char* path, const char* body,
                      String* reply) {
  if (!haveAccount(p)) return Result::NoAccount;
  char base[URL_MAX], name[USER_MAX], key[40];
  url(p, base, sizeof(base));
  user(p, name, sizeof(name));
  p.getString(K_KEY, key, sizeof(key));
  String full = String(base) + path;
  const int code = call(full.c_str(), method, body, name, key, reply);
  if (code == -100) return Result::LowMemory;
  if (code <= 0) return Result::NetworkError;
  if (code == 401 || code == 403) return Result::AuthFailed;
  if (code == 204 || code == 404) return Result::NoRecord;
  if (code >= 200 && code < 300) return Result::Ok;
  return Result::ServerError;
}

inline Result get(Preferences& p, const char* doc, Remote& out) {
  out = Remote();
  String reply;
  char path[64];
  snprintf(path, sizeof(path), "/syncs/progress/%s", doc);
  const Result r = request(p, "GET", path, nullptr, &reply);
  if (r != Result::Ok) return r;
  return parseRemote(reply.c_str(), out) ? Result::Ok : Result::NoRecord;
}

inline Result put(Preferences& p, const char* doc, const char* xp, double pct) {
  char id[33];
  static char body[XP_MAX * 2 + 200];  // static: off the loop task's stack
  deviceId(id);
  buildPut(body, sizeof(body), doc, xp, pct, id);
  return request(p, "PUT", "/syncs/progress", body, nullptr);
}

inline Result requestCode(DeviceCode& c) {
  String reply;
  const int code = call((String(READEST_URL) + "/device/code").c_str(), "POST", "{}", nullptr,
                        nullptr, &reply);
  if (code == -100) return Result::LowMemory;
  if (code <= 0) return Result::NetworkError;
  if (code != 200) return Result::ServerError;
  return parseCode(reply.c_str(), c) ? Result::Ok : Result::ServerError;
}

// No "grant": the answer is this device's own key, not a Readest session
// (that is the KOReader plugin's form).
inline Poll pollCode(Preferences& p, const DeviceCode& c) {
  char body[160];
  snprintf(body, sizeof(body), "{\"device_code\":\"%s\"}", c.device);
  String reply;
  const int code = call((String(READEST_URL) + "/device/token").c_str(), "POST", body, nullptr,
                        nullptr, &reply);
  if (code <= 0) return Poll::Pending;  // a dropped poll is not a refusal: ask again
  return takeToken(p, code, reply.c_str());
}

#endif

}  // namespace kosync
