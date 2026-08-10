// Notes on disk: one Markdown file per note under /notes.
//
// Plain .md so a note can be pulled off the flash, edited anywhere, and dropped
// back. Everything the device shows is derived from this text — there is no
// second copy of the formatting to keep in sync.
#pragma once
#include "unicode.h"
#include "tiny_fs.h"

namespace note {

constexpr int MAX_NOTES = 8;
constexpr int NAME_LEN = 24;
constexpr int MAX_BYTES = 4000;

struct Info {
  char name[NAME_LEN + 1];
  int bytes;
  int todo;   // unticked checkboxes
  int done;   // ticked
};

inline void path(char* out, size_t n, const char* name) {
  snprintf(out, n, "/notes/%s.md", name);
}

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
  int e = (int)strlen(out);
  while (e > 0 && out[e - 1] == ' ') out[--e] = 0;
  int lead = 0;
  while (out[lead] == ' ') lead++;
  if (lead) memmove(out, out + lead, strlen(out + lead) + 1);
  if (out[0] == 0) strcpy(out, "note");
}

inline bool save(const char* name, const char* body, size_t len) {
  tfs::ensureDir("/notes");
  if (len > MAX_BYTES) len = MAX_BYTES;
  char p[64];
  path(p, sizeof(p), name);
  return tfs::write(p, body, len);
}

inline bool load(const char* name, String& out) {
  char p[64];
  path(p, sizeof(p), name);
  return tfs::read(p, out);
}

inline void remove(const char* name) {
  char p[64];
  path(p, sizeof(p), name);
  tfs::remove(p);
}

// Counts checkboxes without parsing the whole note into blocks — the list
// screen only needs the two totals.
inline void countTasks(const String& body, int* todo, int* done) {
  *todo = *done = 0;
  const char* s = body.c_str();
  for (const char* p = s; *p; p++) {
    if (p[0] != '[' || !p[1] || p[2] != ']') continue;
    const bool atLineStart = (p == s) || p[-1] == ' ';
    if (!atLineStart) continue;
    if (p[1] == ' ')
      (*todo)++;
    else if (p[1] == 'x' || p[1] == 'X')
      (*done)++;
  }
}

inline int list(Info* out, int maxNotes) {
  tfs::ensureDir("/notes");
  char names[MAX_NOTES][NAME_LEN + 1];
  const int cap = maxNotes < MAX_NOTES ? maxNotes : MAX_NOTES;
  const int n = tfs::list("/notes", ".md", &names[0][0], NAME_LEN + 1, cap, NAME_LEN);
  for (int i = 0; i < n; i++) {
    strncpy(out[i].name, names[i], NAME_LEN);
    out[i].name[NAME_LEN] = 0;
    String body;
    load(names[i], body);
    out[i].bytes = (int)body.length();
    countTasks(body, &out[i].todo, &out[i].done);
  }
  return n;
}

// --- pinned note -------------------------------------------------------------
// E-paper holds its last image with no power, so whatever is on screen when the
// device sleeps stays there — that is the "lock screen". Pinning a note means
// power-off paints it instead of a goodbye card, and it sits on the fridge until
// something replaces it.

inline void setPinned(const char* name) {
  tfs::ensureDir("/notes");
  tfs::write("/notes/.pin", name ? name : "", name ? strlen(name) : 0);
}

inline bool getPinned(char* out) {
  out[0] = 0;
  // Asked before read, because nothing pinned is the ordinary case on a new
  // device and it should not print an error to say so.
  if (!tfs::exists("/notes/.pin")) return false;
  String v;
  if (!tfs::read("/notes/.pin", v) || v.length() == 0) return false;
  strncpy(out, v.c_str(), NAME_LEN);
  out[NAME_LEN] = 0;
  // A pin pointing at a deleted note is treated as no pin at all.
  String body;
  if (!load(out, body)) {
    out[0] = 0;
    return false;
  }
  return true;
}

inline bool isPinned(const char* name) {
  char cur[NAME_LEN + 1];
  return getPinned(cur) && strcmp(cur, name) == 0;
}

// A starter note that doubles as a format cheat-sheet on first boot.
inline void ensureSample() {
  Info tmp[MAX_NOTES];
  if (list(tmp, MAX_NOTES) > 0) return;
  static const char kSample[] =
      "# Shopping\n"
      "- [ ] milk\n"
      "- [x] bread\n"
      "- [ ] **eggs** (large)\n"
      "\n"
      "## Notes\n"
      "Tap a line to tick it off.\n"
      "\n"
      "---\n"
      "> Send new notes from your phone\n";
  save("shopping", kSample, strlen(kSample));
}

}  // namespace note
