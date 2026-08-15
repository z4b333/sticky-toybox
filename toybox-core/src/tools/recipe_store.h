// Recipes that arrived from a phone, kept in the device's own flash.
//
// The card is the bulk route (drop .json files in /recipes on a PC); this is
// the other one: a phone pastes a recipe page into the portal, the browser
// boils it down to compact schema.org JSON, and the device keeps THAT --
// still the standard format, so the one parser reads both stores and a file
// copied back out is a valid recipe anywhere.
//
// A dozen slots. Recipes a household actually cooks from fit in a dozen; a
// collection lives on the card, where a PC can manage it.
#pragma once
#include "recipe_data.h"
#include "tiny_fs.h"

namespace rstore {

inline constexpr int SLOTS = 12;

inline void path(int slot, char* out, int cap) { snprintf(out, (size_t)cap, "/rc_%d", slot); }

// One shared scratch Recipe for name-peeking and validation: 10 KB is too
// much to give every caller its own, and nothing here is re-entrant.
inline rcp::Recipe& scratch() {
  static rcp::Recipe r;
  return r;
}

// The stored JSON, parsed. False for an empty or unreadable slot.
inline bool load(int slot, rcp::Recipe& out) {
  char p[16];
  path(slot, p, sizeof(p));
  size_t len = 0;
  char* buf = tfs::readAlloc(p, len);
  if (!buf) return false;
  const bool ok = rcp::parse(buf, len, out);
  free(buf);
  return ok;
}

inline void remove(int slot) {
  char p[16];
  path(slot, p, sizeof(p));
  tfs::remove(p);
}

// Every stored recipe's name, in slot order. Returns how many; slots[] says
// where each lives, because deletion leaves holes.
inline int list(int* slots, char names[][rcp::NAME_LEN], int max) {
  int n = 0;
  for (int s = 0; s < SLOTS && n < max; s++) {
    if (!load(s, scratch())) continue;
    slots[n] = s;
    snprintf(names[n], rcp::NAME_LEN, "%s", scratch().name);
    n++;
  }
  return n;
}

// Raw JSON in, slot out (-1 when full or unparseable). A recipe with the
// same name as a stored one replaces it -- pasting the page again is an
// update, not a twin.
inline int save(const char* json, size_t len, char* nameOut, int cap) {
  if (!rcp::parse(json, len, scratch())) return -1;
  char name[rcp::NAME_LEN];
  snprintf(name, sizeof(name), "%s", scratch().name);
  int freeSlot = -1, match = -1;
  for (int s = 0; s < SLOTS; s++) {
    if (!load(s, scratch())) {
      if (freeSlot < 0) freeSlot = s;
      continue;
    }
    if (match < 0 && strcmp(scratch().name, name) == 0) match = s;
  }
  const int at = match >= 0 ? match : freeSlot;  // same name replaces, always
  if (at < 0) return -1;
  char p[16];
  path(at, p, sizeof(p));
  if (!tfs::write(p, json, len)) return -1;
  if (nameOut) snprintf(nameOut, (size_t)cap, "%s", name);
  return at;
}

}  // namespace rstore
