// Books that are volumes of one series, found by their file names.
//
// The shelf groups loose books into one row per series (owner, 18 Nov): ten
// "Slime - Volume NN.epub" files are one row that opens onto ten, in volume
// order, instead of ten rows. Nothing moves on the card -- real folders stay
// exactly as they are -- this is only how a level of the shelf is shown.
//
// The rule: take the file name without its extension, and strip a volume
// marker from its END -- "Volume 02", "Vol. 2", "Vol 2", "v02", "Book 4",
// "Part 3", "#5", "No. 6", or a bare trailing number after a separator
// ("Title - 02", "Title (3)", "Title 07"). What is left, with trailing
// separators trimmed, is the series; two or more books with the same series
// (ignoring case) make a group. A name with no marker is never grouped:
// "1984.epub" and "2001.epub" are not volumes of anything.
//
// The file NAME, not the shelf title: titles are cut to 40 bytes, and a long
// release name loses exactly the volume number this needs.
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace series {

inline constexpr int NAME = 64;

struct Parsed {
  char key[NAME] = {};    // the series, as written in the first file seen
  char label[24] = {};    // the marker as written: "Volume 02", "v03", "7"
  int volume = 0;         // for sorting; x1000 so "2.5" sorts between 2 and 3
};

namespace detail {
inline bool isSep(char c) { return c == ' ' || c == '-' || c == '_' || c == '.' || c == ',' || c == ':' || c == '~'; }

// Case-insensitive "does `word` end exactly at `end` and start a word".
inline bool wordBefore(const char* start, const char* end, const char* word, const char** at) {
  const size_t n = strlen(word);
  if ((size_t)(end - start) < n) return false;
  const char* w = end - n;
  for (size_t i = 0; i < n; i++)
    if (tolower((unsigned char)w[i]) != tolower((unsigned char)word[i])) return false;
  if (w > start && isalnum((unsigned char)w[-1])) return false;  // "Avol 2" is not "vol 2"
  *at = w;
  return true;
}
}  // namespace detail

// The file's base name without its extension: "/books/A/x - Vol 2.epub" ->
// "x - Vol 2".
inline void stem(const char* file, char* out, int cap) {
  const char* b = strrchr(file, '/');
  b = b ? b + 1 : file;
  const char* dot = strrchr(b, '.');
  const int n = dot && dot > b ? (int)(dot - b) : (int)strlen(b);
  snprintf(out, (size_t)cap, "%.*s", n, b);
}

// False when the name carries no volume marker, or nothing is left before it.
inline bool parse(const char* file, Parsed& p) {
  p = Parsed();
  char s[160];
  stem(file, s, sizeof(s));
  int len = (int)strlen(s);
  // Brackets around the number: "Title (3)", "Title [03]".
  bool bracket = false;
  if (len > 2 && (s[len - 1] == ')' || s[len - 1] == ']')) {
    bracket = true;
    s[--len] = 0;
  }
  // The number: digits, optionally one decimal part ("2.5").
  int e = len;
  while (e > 0 && isdigit((unsigned char)s[e - 1])) e--;
  if (e == len) return false;  // no trailing number at all
  int numStart = e;
  if (numStart >= 2 && s[numStart - 1] == '.' && isdigit((unsigned char)s[numStart - 2])) {
    int d = numStart - 1;
    while (d > 0 && isdigit((unsigned char)s[d - 1])) d--;
    numStart = d;  // "2.5"
  }
  const double v = atof(s + numStart);
  const char* head = s;
  const char* markEnd = s + numStart;
  // Optional spaces / "#" / "." between the marker word and the number.
  const char* q = markEnd;
  while (q > head && (q[-1] == ' ' || q[-1] == '#' || q[-1] == '.' || q[-1] == '_')) q--;
  const char* at = nullptr;
  static const char* const words[] = {"volume", "vol", "book", "part", "no", "v", "tome", "band"};
  bool marker = false;
  for (const char* w : words) {
    if (detail::wordBefore(head, q, w, &at)) {
      // "v" only as "v02" / "v 2" right after a separator: "Kiev 2" is not v2.
      if (!strcmp(w, "v") && at > head && !detail::isSep(at[-1])) continue;
      marker = true;
      break;
    }
  }
  if (!marker) {
    // A bare number counts only behind a separator or a bracket: "Title - 02",
    // "Title 07", "Title (3)". "R2D2" has none.
    int pre = numStart;
    if (pre > 0 && s[pre - 1] == '#') pre--;  // "One Piece #5"
    if (!bracket && !(pre > 0 && detail::isSep(s[pre - 1]))) return false;
    at = markEnd;
    while (at > head && (at[-1] == '#')) at--;
    if (bracket) {
      while (at > head && at[-1] == ' ') at--;
      if (at > head && (at[-1] == '(' || at[-1] == '[')) at--;
    }
  }
  // Everything before the marker, separators trimmed, is the series.
  int k = (int)(at - head);
  while (k > 0 && (detail::isSep(head[k - 1]) || head[k - 1] == '(' || head[k - 1] == '[')) k--;
  if (k <= 0) return false;  // "Volume 2.epub" alone names no series
  snprintf(p.key, sizeof(p.key), "%.*s", k, head);
  // The label as written, from the marker on (brackets dropped).
  const char* l = at;
  while (*l == '(' || *l == '[' || *l == ' ') l++;
  snprintf(p.label, sizeof(p.label), "%s", l);
  p.volume = (int)(v * 1000.0 + 0.5);
  return true;
}

inline bool sameKey(const char* a, const char* b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

}  // namespace series
