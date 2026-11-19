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
#include <ctype.h>
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

// --- Readest's library ------------------------------------------------------
// The EPUBs the owner has uploaded to Readest's cloud, as Readest offers them
// to e-ink readers: GET /books?page=&per_page=&q= (most recently read first;
// per_page + 1 rows come back, the extra one meaning "there is more"), then
// GET /books/<hash> for a short-lived download link. The hash is the same
// partial MD5 docHash() makes, so a downloaded book syncs from its first open.

struct LibBook {
  char id[33] = {};
  char title[96] = {};
  char author[64] = {};
};
inline constexpr int LIB_PER_PAGE = 8;  // one shelf page, with one row spare
inline constexpr const char* LIB_DIR = "/books/Readest";

// One "items" array into `out`. Returns how many; `more` says a next page exists.
inline int parseLibrary(const char* body, LibBook* out, int max, bool& more) {
  more = false;
  const char* p = body ? strstr(body, "\"items\"") : nullptr;
  if (!p) return 0;
  p = strchr(p, '[');
  if (!p) return 0;
  p++;
  int n = 0, seen = 0;
  char obj[640];
  while (*p) {
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') break;
    // The object, by brace depth, strings respected.
    const char* q = p;
    int depth = 0;
    bool str = false;
    for (; *q; q++) {
      if (str) {
        if (*q == '\\' && q[1]) q++;
        else if (*q == '"') str = false;
      } else if (*q == '"') str = true;
      else if (*q == '{') depth++;
      else if (*q == '}' && --depth == 0) break;
    }
    if (!*q) break;
    const int len = (int)(q - p + 1);
    seen++;
    if (n < max && len < (int)sizeof(obj)) {
      memcpy(obj, p, (size_t)len);
      obj[len] = 0;
      LibBook& b = out[n];
      b = LibBook();
      jsonField(obj, "id", b.id, sizeof(b.id));
      jsonField(obj, "title", b.title, sizeof(b.title));
      jsonField(obj, "author", b.author, sizeof(b.author));
      if (b.id[0] && b.title[0]) n++;
    }
    p = q + 1;
  }
  more = seen > max;
  return n;
}

// The file a library book becomes on the card: /books/Readest/<title>.epub,
// the title made safe for FAT (no \ / : * ? " < > | or control characters,
// no trailing dot or space) and cut on a UTF-8 boundary.
inline void libPath(const LibBook& b, char* out, int cap) {
  char name[100];
  int n = 0;
  for (const char* s = b.title; *s && n < (int)sizeof(name) - 1; s++) {
    const unsigned char c = (unsigned char)*s;
    name[n++] = (c < 0x20 || strchr("\\/:*?\"<>|", c)) ? '_' : (char)c;
  }
  name[n] = 0;
  while (n > 0 && ((unsigned char)name[n - 1] & 0xC0) == 0x80) n--;  // a cut character
  if (n > 0 && ((unsigned char)name[n - 1] & 0xC0) == 0xC0) n--;
  name[n] = 0;
  while (n > 0 && (name[n - 1] == '.' || name[n - 1] == ' ')) name[--n] = 0;
  if (!n) snprintf(name, sizeof(name), "%s", b.id);
  snprintf(out, (size_t)cap, "%s/%s.epub", LIB_DIR, name);
}

// Where a download's bytes go, and whether to carry on: the reader supplies
// one that streams to the card and paints a progress bar.
struct Sink {
  virtual bool open() = 0;
  virtual bool write(const uint8_t* d, uint32_t n) = 0;
  virtual bool close(bool keep) = 0;
  // false = the person cancelled
  virtual bool progress(uint32_t got, uint32_t total) = 0;
  virtual ~Sink() {}
};

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

// The library: n books named "Book NN", id = md5 of the title.
inline int g_hostLibBooks = 11;
inline Result g_hostLibFail = Result::Ok;
inline std::string g_hostLastQuery;
inline uint32_t g_hostBookBytes = 50000;

inline Result listLibrary(Preferences& p, int page, const char* q, LibBook* out, int max,
                          int& n, bool& more) {
  n = 0;
  more = false;
  if (!haveAccount(p)) return Result::NoAccount;
  if (g_hostLibFail != Result::Ok) return g_hostLibFail;
  g_hostLastQuery = q ? q : "";
  std::string body = "{\"items\":[";
  int listed = 0;
  bool first = true;
  for (int i = 0; i < g_hostLibBooks; i++) {
    char title[48];
    snprintf(title, sizeof(title), "Book %02d%s", i + 1, i == 2 ? ": A/B \\\"quoted\\\"" : "");
    if (q && *q && !strstr(title, q)) continue;
    if (listed++ < (page - 1) * max) continue;
    if (listed > page * max + 1) break;
    char id[33];
    md5t::hex(title, id);
    if (!first) body += ",";
    first = false;
    body += std::string("{\"id\":\"") + id + "\",\"title\":\"" + title +
            "\",\"author\":\"Author " + std::to_string(i + 1) + "\",\"url\":\"x\"}";
  }
  body += "]}";
  n = parseLibrary(body.c_str(), out, max, more);
  return Result::Ok;
}

// The "download": g_hostBookBytes of a pattern, 4 KB a step.
class Download {
 public:
  Result begin(Preferences& p, const LibBook& b, Sink& sink) {
    (void)b;
    _sink = nullptr;
    got = total = 0;
    if (!haveAccount(p)) return Result::NoAccount;
    if (g_hostLibFail != Result::Ok) return g_hostLibFail;
    if (!sink.open()) return Result::ServerError;
    _sink = &sink;
    total = g_hostBookBytes;
    return Result::Ok;
  }
  // True when finished; `r` is then the outcome.
  bool step(Result& r) {
    if (!_sink) {
      r = Result::ServerError;
      return true;
    }
    uint8_t buf[4096];
    const uint32_t n = total - got < sizeof(buf) ? total - got : sizeof(buf);
    for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)(got + i);
    if (!_sink->write(buf, n)) {
      _sink->close(false);
      _sink = nullptr;
      r = Result::ServerError;
      return true;
    }
    got += n;
    _sink->progress(got, total);
    if (got < total) return false;
    r = _sink->close(true) ? Result::Ok : Result::ServerError;
    _sink = nullptr;
    return true;
  }
  void cancel() {
    if (_sink) _sink->close(false);
    _sink = nullptr;
  }
  uint32_t got = 0, total = 0;

 private:
  Sink* _sink = nullptr;
};

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

inline void urlEncode(const char* in, String& out) {
  static const char* hx = "0123456789ABCDEF";
  for (const unsigned char* s = (const unsigned char*)in; *s; s++) {
    if (isalnum(*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~')
      out += (char)*s;
    else {
      out += '%';
      out += hx[*s >> 4];
      out += hx[*s & 15];
    }
  }
}

inline Result listLibrary(Preferences& p, int page, const char* q, LibBook* out, int max,
                          int& n, bool& more) {
  n = 0;
  more = false;
  String path = "/books?page=" + String(page) + "&per_page=" + String(max);
  if (q && *q) {
    path += "&q=";
    urlEncode(q, path);
  }
  String reply;
  const Result r = request(p, "GET", path.c_str(), nullptr, &reply);
  if (r != Result::Ok) return r;
  n = parseLibrary(reply.c_str(), out, max, more);
  return Result::Ok;
}

// The link first (Readest answers with a signed URL on its storage), then
// the bytes, a step at a time so the reader can paint progress and hear a
// Cancel between steps: a book is megabytes and the heap is not.
class Download {
 public:
  Result begin(Preferences& p, const LibBook& b, Sink& sink) {
    end();
    got = total = 0;
    String reply;
    const Result r = request(p, "GET", (String("/books/") + b.id).c_str(), nullptr, &reply);
    if (r != Result::Ok) return r;
    static char url[1024];  // signed URLs are long; static, off the stack
    if (!jsonField(reply.c_str(), "downloadUrl", url, sizeof(url))) return Result::ServerError;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 24000)
      return Result::LowMemory;
    _secure = new NetworkClientSecure();
    _http = new HTTPClient();
    _secure->setCACertBundle(kosync_bundle_start, kosync_bundle_end - kosync_bundle_start);
    _http->setTimeout(15000);
    _http->setConnectTimeout(10000);
    _http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!_http->begin(*_secure, url)) {
      end();
      return Result::NetworkError;
    }
    const int code = _http->GET();
    if (code != 200) {
      end();
      return code <= 0 ? Result::NetworkError : Result::ServerError;
    }
    const int size = _http->getSize();  // -1 when the server does not say
    total = size > 0 ? (uint32_t)size : 0;
    if (!sink.open()) {
      end();
      return Result::ServerError;
    }
    _sink = &sink;
    _idle = millis();
    return Result::Ok;
  }

  // Reads for up to ~150 ms. True when finished; `r` is then the outcome.
  bool step(Result& r) {
    static uint8_t buf[4096];
    if (!_sink || !_http) {
      r = Result::ServerError;
      return true;
    }
    NetworkClient* s = _http->getStreamPtr();
    const uint32_t t0 = millis();
    while (millis() - t0 < 150) {
      if (total && got >= total) break;
      const int avail = s ? s->available() : 0;
      if (avail <= 0) {
        if (!_http->connected()) break;
        if (millis() - _idle > 15000) return fail(r, Result::NetworkError);  // gone quiet
        delay(2);
        continue;
      }
      const int n = s->readBytes(buf, avail < (int)sizeof(buf) ? avail : (int)sizeof(buf));
      if (n <= 0) continue;
      _idle = millis();
      if (!_sink->write(buf, (uint32_t)n)) return fail(r, Result::ServerError);  // card full
      got += (uint32_t)n;
    }
    _sink->progress(got, total);
    const bool finished = (total && got >= total) || (!_http->connected() && (!s || s->available() <= 0));
    if (!finished) return false;
    if ((total && got != total) || got == 0) return fail(r, Result::NetworkError);
    Sink* sk = _sink;
    _sink = nullptr;
    end();
    r = sk->close(true) ? Result::Ok : Result::ServerError;
    return true;
  }

  void cancel() {
    if (_sink) _sink->close(false);
    _sink = nullptr;
    end();
  }
  ~Download() { cancel(); }
  uint32_t got = 0, total = 0;

 private:
  bool fail(Result& r, Result why) {
    if (_sink) _sink->close(false);
    _sink = nullptr;
    end();
    r = why;
    return true;
  }
  void end() {
    if (_http) {
      _http->end();
      delete _http;
      _http = nullptr;
    }
    delete _secure;
    _secure = nullptr;
  }
  NetworkClientSecure* _secure = nullptr;
  HTTPClient* _http = nullptr;
  Sink* _sink = nullptr;
  uint32_t _idle = 0;
};

#endif

}  // namespace kosync
