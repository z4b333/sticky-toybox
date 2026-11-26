// Readest highlights on the Sticky (stage D, part A: pull).
//
// Readest serves e-ink readers a book's highlights ("clippings") under the
// same KOReader-sync server the Sticky already signs in to:
//
//   GET  <READEST_URL>/api/v1/clippings/<partial md5>?cursor=N&limit=50
//        -> { cursor, more, items: [ {id, deleted, spine, start_offset,
//             end_offset, text, chapter, ...} ] }
//
// Positions are a spine index and a [start, end) range of visible codepoints
// in CrossPoint's counting -- exactly how Toybox counts (epubcore.h), so a
// pulled highlight lands where it was made. No note text and no device name
// travel this way (owner, 26 Nov: option 1, highlights now, notes later).
//
// Deletion is mutual (owner): an item marked deleted removes the local copy;
// a Readest highlight removed HERE is buried (marks::bury) so a pull does not
// bring it back, and part B sends the burial to Readest.
//
// This file holds the pure parts -- settings, parsing, merging -- so the host
// harness can put every case through them. The radio is in kosync.h.
#pragma once
#include <Preferences.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bookmarks.h"
#include "kosync.h"
#ifdef TOYBOX_HOST
#include <map>
#include <string>
#include <vector>
#endif

namespace rhl {

// --- settings (Settings > CONNECT > Reading sync) ---------------------------------
inline constexpr const char* K_POS = "sy_pos";      // reading position
inline constexpr const char* K_HL = "sy_hl";        // highlights
inline constexpr const char* K_AUTO = "sy_auto";    // auto sync at all
inline constexpr const char* K_OPEN = "sy_open";    // ...when a book opens
inline constexpr const char* K_CLOSE = "sy_close";  // ...when it closes or the Sticky sleeps

inline bool pos(Preferences& p) { return p.getBool(K_POS, true); }
inline bool hl(Preferences& p) { return p.getBool(K_HL, true); }
inline bool autoOn(Preferences& p) { return p.getBool(K_AUTO, true); }
inline bool onOpen(Preferences& p) { return autoOn(p) && p.getBool(K_OPEN, true); }
inline bool onClose(Preferences& p) { return autoOn(p) && p.getBool(K_CLOSE, true); }
inline void flip(Preferences& p, const char* key) { p.putBool(key, !p.getBool(key, true)); }

// --- one clipping ------------------------------------------------------------------
struct Clip {
  char id[marks::ID] = {};
  bool deleted = false;
  uint16_t spine = 0;
  uint32_t start = 0, end = 0;
  char label[marks::LABEL] = {};
};

// The phrase as the page will match it: whitespace runs become one space (the
// page joins its lines with one), cut on a UTF-8 boundary with "..." when it
// is longer than a label.
inline void makeLabel(const char* text, char* out, size_t cap) {
  size_t n = 0;
  bool space = true;  // drops leading whitespace
  const size_t room = cap - 4;  // "..." and the NUL
  bool cut = false;
  for (const char* s = text; *s; s++) {
    const char ch = *s;
    const bool ws = ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
    if (ws) {
      if (space) continue;
      space = true;
      if (n + 1 > room) {
        cut = true;
        break;
      }
      out[n++] = ' ';
      continue;
    }
    space = false;
    if (n + 1 > room) {
      cut = true;
      break;
    }
    out[n++] = ch;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  if (cut) {
    while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80) n--;  // never half a character
    while (n > 0 && ((uint8_t)out[n - 1] & 0xC0) == 0xC0) n--;
    memcpy(out + n, "...", 3);
    n += 3;
  }
  out[n] = 0;
}

// One page of the clippings answer into `out` (appended at n). `cursor` and
// `more` say where the next page starts. Items it cannot use (no id, an id
// too long to keep, no text) are skipped.
inline bool parsePage(const char* body, Clip* out, int max, int& n, uint32_t& cursor, bool& more) {
  if (!body) return false;
  char num[24];
  more = kosync::jsonField(body, "more", num, sizeof(num)) && !strcmp(num, "true");
  if (kosync::jsonField(body, "cursor", num, sizeof(num))) cursor = (uint32_t)atol(num);
  const char* p = strstr(body, "\"items\"");
  if (!p) return false;
  p = strchr(p, '[');
  if (!p) return false;
  p++;
  while (*p) {
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') break;
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
    const size_t len = (size_t)(q - p + 1);
    if (n < max) {
      char* obj = (char*)malloc(len + 1);
      if (obj) {
        memcpy(obj, p, len);
        obj[len] = 0;
        Clip& c = out[n];
        c = Clip{};
        char id[80];
        if (kosync::jsonField(obj, "id", id, sizeof(id)) && strlen(id) < marks::ID) {
          snprintf(c.id, sizeof(c.id), "%s", id);
          c.deleted = kosync::jsonField(obj, "deleted", num, sizeof(num)) &&
                      (!strcmp(num, "1") || !strcmp(num, "true"));
          if (c.deleted) {
            n++;
          } else if (kosync::jsonField(obj, "spine", num, sizeof(num))) {
            c.spine = (uint16_t)atol(num);
            if (kosync::jsonField(obj, "start_offset", num, sizeof(num))) c.start = (uint32_t)atol(num);
            if (kosync::jsonField(obj, "end_offset", num, sizeof(num))) c.end = (uint32_t)atol(num);
            // The text can run to 4 KB; only a label's worth is kept.
            char* text = (char*)malloc(len + 1);
            if (text && kosync::jsonField(obj, "text", text, len + 1)) {
              makeLabel(text, c.label, sizeof(c.label));
              if (c.label[0]) n++;
            }
            free(text);
          }
        }
        free(obj);
      }
    }
    p = q + 1;
  }
  return true;
}

// What a pull changed.
struct Merged {
  int added = 0, removed = 0;
  bool full = false;  // the book's list is full: some were left on the server
};

// Readest's list into the book's: new ones in, deleted ones out, ones buried
// here left out. Highlights kept here (no id) are never touched by a pull.
inline Merged merge(marks::Mark* m, int& n, const Clip* c, int k) {
  Merged r;
  for (int i = 0; i < k; i++) {
    const Clip& x = c[i];
    int at = -1;
    for (int j = 0; j < n; j++)
      if (strcmp(m[j].id, x.id) == 0) {
        at = j;
        break;
      }
    if (x.deleted) {
      if (at >= 0) {
        marks::remove(m, n, at);
        r.removed++;
      }
      continue;
    }
    if (at >= 0 || marks::buried(x.id)) continue;
    marks::Mark nm{};
    nm.spine = x.spine;
    nm.page = marks::NO_PAGE;
    nm.off = x.start;
    nm.end = x.end;
    nm.src = marks::SRC_READEST;
    snprintf(nm.id, sizeof(nm.id), "%s", x.id);
    snprintf(nm.label, sizeof(nm.label), "%s", x.label);
    if (marks::add(m, n, nm) >= 0)
      r.added++;
    else if (n >= marks::MAX)
      r.full = true;
  }
  return r;
}

// --- the radio ------------------------------------------------------------------
// Every page of the book's clippings into `out` (up to max). Joined to the
// home Wi-Fi already: the caller owns the radio. A book Readest has never seen
// answers 404: that is "no highlights", not an error.
inline constexpr int PULL_MAX = 128;  // live and deleted together, a pull's worth
inline constexpr int PAGE = 20;  // texts run to 4 KB each: a page of 20 stays a modest reply

#ifdef TOYBOX_HOST
struct HostClips {
  std::map<std::string, std::vector<std::string>> pages;  // doc -> page bodies, in order
  kosync::Result fail = kosync::Result::Ok;
  int gets = 0;
};
inline HostClips g_hostClips;

inline kosync::Result pull(Preferences& p, const char* doc, Clip* out, int max, int& n) {
  n = 0;
  if (!kosync::haveAccount(p)) return kosync::Result::NoAccount;
  if (g_hostClips.fail != kosync::Result::Ok) return g_hostClips.fail;
  auto it = g_hostClips.pages.find(doc);
  if (it == g_hostClips.pages.end()) return kosync::Result::Ok;
  for (const std::string& body : it->second) {
    g_hostClips.gets++;
    uint32_t cursor = 0;
    bool more = false;
    if (!parsePage(body.c_str(), out, max, n, cursor, more)) return kosync::Result::ServerError;
    if (!more || n >= max) break;
  }
  return kosync::Result::Ok;
}
#else
inline kosync::Result pull(Preferences& p, const char* doc, Clip* out, int max, int& n) {
  n = 0;
  uint32_t cursor = 0;
  bool more = true;
  for (int guard = 0; more && n < max && guard < 8; guard++) {
    char path[96];
    snprintf(path, sizeof(path), "/api/v1/clippings/%s?cursor=%lu&limit=%d", doc,
             (unsigned long)cursor, PAGE);
    String reply;
    const kosync::Result r = kosync::request(p, "GET", path, nullptr, &reply);
    if (r == kosync::Result::NoRecord) return kosync::Result::Ok;
    if (r != kosync::Result::Ok) return r;
    const uint32_t was = cursor;
    more = false;
    if (!parsePage(reply.c_str(), out, max, n, cursor, more)) return kosync::Result::ServerError;
    if (cursor <= was) break;  // a server that does not move on is not followed round
  }
  return kosync::Result::Ok;
}
#endif

}  // namespace rhl
