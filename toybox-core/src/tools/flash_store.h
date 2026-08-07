// Flashcard deck storage and parsing.
//
// Decks live in the on-board 3.4 MB filesystem as plain TSV (`front<TAB>back`),
// with a sidecar `.box` file holding one Leitner box level per card. Both are
// trivial to inspect or hand-edit, and the sidecar means re-importing a deck
// never silently discards your progress on the cards that stayed the same.
//
// Import accepts whatever people actually paste: tab, `|`, comma (quote-aware),
// or ` - ` separated lines. That covers Anki and Quizlet exports, spreadsheet
// copies, and hand-typed lists without asking the user to pick a format.
#pragma once
#include "unicode.h"
#include <Arduino.h>

#include "tiny_fs.h"

namespace fcard {

constexpr int MAX_CARDS = 200;
constexpr int FRONT_LEN = 64;
constexpr int BACK_LEN = 96;
constexpr int NAME_LEN = 24;
constexpr int MAX_DECKS = 12;
constexpr uint8_t MAX_BOX = 4;  // Leitner boxes 0..4; 4 == mastered

struct Card {
  char front[FRONT_LEN + 1];
  char back[BACK_LEN + 1];
  uint8_t box;
};

struct DeckInfo {
  char name[NAME_LEN + 1];
  int cards;
  int mastered;
};

inline bool fsBegin() {
  tfs::ensureDir("/decks");
  return tfs::begin();
}
inline bool fsRead(const char* path, String& out) { return tfs::read(path, out); }
inline bool fsWrite(const char* path, const char* d, size_t n) { return tfs::write(path, d, n); }
inline bool fsRemove(const char* path) { return tfs::remove(path); }
inline int fsListDecks(char names[][NAME_LEN + 1], int maxNames) {
  fsBegin();
  return tfs::list("/decks", ".tsv", &names[0][0], NAME_LEN + 1, maxNames, NAME_LEN);
}

// ---------------------------------------------------------------------------
// Parsing

inline void trim(char* s) {
  int n = (int)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
  int lead = 0;
  while (s[lead] == ' ' || s[lead] == '\t') lead++;
  if (lead) memmove(s, s + lead, n - lead + 1);
}

// Strip one layer of surrounding double quotes and unescape doubled quotes,
// which is how spreadsheets emit fields containing commas.
inline void unquote(char* s) {
  const int n = (int)strlen(s);
  if (n < 2 || s[0] != '"' || s[n - 1] != '"') return;
  memmove(s, s + 1, n - 2);
  s[n - 2] = 0;
  char* w = s;
  for (char* r = s; *r; r++) {
    if (r[0] == '"' && r[1] == '"') r++;
    *w++ = *r;
  }
  *w = 0;
}

// Split one line into front/back. Returns false for blank or unsplittable lines.
// Separator precedence follows how ambiguous each one is: a tab or a pipe is
// almost never part of the content, whereas a comma or a dash often is, so
// those are only tried once the unambiguous ones are ruled out.
inline bool splitLine(const char* line, char* front, char* back) {
  const char* sep = nullptr;
  int sepLen = 0;

  if ((sep = strchr(line, '\t')) != nullptr) {
    sepLen = 1;
  } else if ((sep = strstr(line, " | ")) != nullptr) {
    sepLen = 3;
  } else if ((sep = strchr(line, '|')) != nullptr) {
    sepLen = 1;
  } else if ((sep = strchr(line, ',')) != nullptr) {
    // Quote-aware: skip commas inside a quoted field.
    bool inQuote = false;
    sep = nullptr;
    for (const char* p = line; *p; p++) {
      if (*p == '"') inQuote = !inQuote;
      if (*p == ',' && !inQuote) {
        sep = p;
        break;
      }
    }
    sepLen = 1;
  }
  if (sep == nullptr) {
    if ((sep = strstr(line, " - ")) != nullptr) sepLen = 3;
  }
  if (sep == nullptr) return false;

  int fn = (int)(sep - line);
  if (fn > FRONT_LEN) fn = FRONT_LEN;
  memcpy(front, line, fn);
  front[fn] = 0;
  strncpy(back, sep + sepLen, BACK_LEN);
  back[BACK_LEN] = 0;

  trim(front);
  trim(back);
  unquote(front);
  unquote(back);
  trim(front);
  trim(back);
  return front[0] != 0 && back[0] != 0;
}

// Parse a whole pasted/uploaded blob into cards. Returns the card count.
inline int parseDeck(const char* text, Card* out, int maxCards) {
  int n = 0;
  const char* p = text;
  char line[FRONT_LEN + BACK_LEN + 8];
  while (*p && n < maxCards) {
    int len = 0;
    while (p[len] && p[len] != '\n' && len < (int)sizeof(line) - 1) len++;
    memcpy(line, p, len);
    line[len] = 0;
    p += len;
    while (*p == '\n' || *p == '\r') p++;

    if (splitLine(line, out[n].front, out[n].back)) {
      out[n].box = 0;
      n++;
    }
  }
  return n;
}

// ---------------------------------------------------------------------------
// Deck files

inline void deckPath(char* out, size_t n, const char* name, const char* ext) {
  snprintf(out, n, "/decks/%s%s", name, ext);
}

// Sanitise a user-supplied deck name into something safe for a filename.
inline void sanitizeName(const char* in, char* out) {
  // Names come from a phone keyboard in any language, and the filesystem only
  // truly objects to '/' and control bytes -- so UTF-8 passes through whole. A
  // Thai or Chinese note keeps its name instead of turning into underscores.
  // The cut at NAME_LEN falls on a codepoint boundary, never inside one.
  int n = 0;
  for (const char* p = in; *p;) {
    const char* q = p;
    const uint32_t cp = uni::next(q);
    const int len = (int)(q - p);
    if (n + len > NAME_LEN) break;
    if (cp >= 0x80) {
      for (int k = 0; k < len; k++) out[n++] = *p++;
      continue;
    }
    const char c = *p++;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
    out[n++] = ok ? c : '_';
  }
  out[n] = 0;
  trim(out);
  if (out[0] == 0) strcpy(out, "deck");
}

inline bool saveDeck(const char* name, const Card* cards, int count) {
  String body;
  body.reserve((size_t)count * 48);
  for (int i = 0; i < count; i++) {
    body += cards[i].front;
    body += '\t';
    body += cards[i].back;
    body += '\n';
  }
  char path[64];
  deckPath(path, sizeof(path), name, ".tsv");
  return fsWrite(path, body.c_str(), body.length());
}

inline bool saveBoxes(const char* name, const Card* cards, int count) {
  char path[64];
  deckPath(path, sizeof(path), name, ".box");
  uint8_t buf[MAX_CARDS];  // 200 bytes: cheap enough to keep on the stack
  const int n = count > MAX_CARDS ? MAX_CARDS : count;
  for (int i = 0; i < n; i++) buf[i] = cards[i].box;
  return fsWrite(path, (const char*)buf, (size_t)n);
}

inline int loadDeck(const char* name, Card* out, int maxCards) {
  char path[64];
  deckPath(path, sizeof(path), name, ".tsv");
  String body;
  if (!fsRead(path, body)) return 0;
  const int n = parseDeck(body.c_str(), out, maxCards);

  // Re-apply saved Leitner levels, ignoring a stale/short sidecar.
  deckPath(path, sizeof(path), name, ".box");
  String boxes;
  if (fsRead(path, boxes)) {
    const int m = (int)boxes.length() < n ? (int)boxes.length() : n;
    for (int i = 0; i < m; i++) {
      const uint8_t b = (uint8_t)boxes[i];
      out[i].box = b <= MAX_BOX ? b : 0;
    }
  }
  return n;
}

inline void deleteDeck(const char* name) {
  char path[64];
  deckPath(path, sizeof(path), name, ".tsv");
  fsRemove(path);
  deckPath(path, sizeof(path), name, ".box");
  fsRemove(path);
}

// Import a blob under `rawName`, preserving box levels of cards whose front text
// is unchanged — so refreshing a deck from the web app keeps your progress.
inline uint32_t frontHash(const char* s) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// The largest working set in the app: 200 cards, plus the hashes used to carry
// Leitner progress across a re-import. At ~34 KB that is a tenth of the
// device's RAM, so it is borrowed from the heap for the length of one import
// rather than parked in .bss for the lifetime of the firmware. That matters
// most in the CrossPoint port, where an idle Toybox should cost the e-reader
// as close to nothing as possible.
struct ImportScratch {
  Card cards[MAX_CARDS];
  uint32_t oldHash[MAX_CARDS];
  uint8_t oldBox[MAX_CARDS];
};

inline int importDeck(const char* rawName, const char* text, char* savedName) {
  sanitizeName(rawName, savedName);
  ImportScratch* sc = (ImportScratch*)malloc(sizeof(ImportScratch));
  if (!sc) return 0;
  int imported = 0;

  // Remember the old cards by hash first, so re-importing a deck keeps the
  // Leitner progress of every card whose front text is unchanged.
  const int old = loadDeck(savedName, sc->cards, MAX_CARDS);
  for (int j = 0; j < old; j++) {
    sc->oldHash[j] = frontHash(sc->cards[j].front);
    sc->oldBox[j] = sc->cards[j].box;
  }

  const int n = parseDeck(text, sc->cards, MAX_CARDS);
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      const uint32_t h = frontHash(sc->cards[i].front);
      for (int j = 0; j < old; j++) {
        if (sc->oldHash[j] == h) {
          sc->cards[i].box = sc->oldBox[j];
          break;
        }
      }
    }
    if (saveDeck(savedName, sc->cards, n)) {
      saveBoxes(savedName, sc->cards, n);
      imported = n;
    }
  }

  free(sc);
  return imported;
}

// Counts come straight from the files: one line per card, one box byte per
// card. Parsing whole decks here would cost tens of KB of RAM for a screen that
// only shows two numbers.
inline int listDecks(DeckInfo* out, int maxDecks) {
  char names[MAX_DECKS][NAME_LEN + 1];
  const int n = fsListDecks(names, maxDecks < MAX_DECKS ? maxDecks : MAX_DECKS);
  char path[64];
  for (int i = 0; i < n; i++) {
    strncpy(out[i].name, names[i], NAME_LEN);
    out[i].name[NAME_LEN] = 0;
    out[i].cards = 0;
    out[i].mastered = 0;

    String body;
    deckPath(path, sizeof(path), names[i], ".tsv");
    if (fsRead(path, body)) {
      for (unsigned k = 0; k < body.length(); k++)
        if (body[k] == '\n' && out[i].cards < MAX_CARDS) out[i].cards++;
      if (body.length() && body[body.length() - 1] != '\n') out[i].cards++;
    }
    String boxes;
    deckPath(path, sizeof(path), names[i], ".box");
    if (fsRead(path, boxes)) {
      for (unsigned k = 0; k < boxes.length(); k++)
        if ((uint8_t)boxes[k] >= MAX_BOX) out[i].mastered++;
    }
  }
  return n;
}

// A tiny starter deck so the app is never an empty screen on first boot.
inline void ensureSampleDeck() {
  DeckInfo tmp[MAX_DECKS];
  if (listDecks(tmp, MAX_DECKS) > 0) return;
  static const char kSample[] =
      "bonjour\thello\n"
      "merci\tthank you\n"
      "s'il vous plait\tplease\n"
      "au revoir\tgoodbye\n"
      "oui\tyes\n"
      "non\tno\n"
      "l'eau\twater\n"
      "le pain\tbread\n";
  char saved[NAME_LEN + 1];
  importDeck("french starter", kSample, saved);
}

}  // namespace fcard
