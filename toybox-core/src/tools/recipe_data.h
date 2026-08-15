// A recipe, read from what the internet actually serves.
//
// Nearly every recipe website embeds its recipe as schema.org/Recipe JSON-LD
// (it is what search engines read), so that is the format this parser speaks:
// save the <script type="application/ld+json"> block -- or the page's whole
// JSON -- as a .json file and it opens here. The format in the wild is messy
// in well-known ways, and each is handled rather than wished away:
//
//   - the Recipe may sit alone, in an array, or inside "@graph" beside
//     Articles and BreadcrumbLists -- the parser walks anything and takes the
//     first object whose @type is (or contains) "Recipe";
//   - instructions arrive as one string, an array of strings, an array of
//     HowToStep objects ({"text": ...}), or HowToSections nesting steps in
//     "itemListElement" -- all of them flatten to the same numbered steps;
//   - times are ISO-8601 durations ("PT1H30M"); yield is a string, a number,
//     or an array of both;
//   - text carries JSON escapes (\uXXXX included), HTML tags and HTML
//     entities, all of which are decoded or stripped on the way in.
//
// No DOM is built: one pass over the bytes, fixed-size output, bounded
// recursion. A 200 KB page block parses in the space of the Recipe it fills.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace rcp {

inline constexpr int NAME_LEN = 80;
inline constexpr int YIELD_LEN = 40;
inline constexpr int MAX_ING = 24, ING_LEN = 96;
inline constexpr int MAX_STEPS = 24, STEP_LEN = 320;

struct Recipe {
  char name[NAME_LEN];
  char yield[YIELD_LEN];
  uint16_t totalMin, prepMin, cookMin;
  uint8_t nIng, nSteps;
  char ing[MAX_ING][ING_LEN];
  char steps[MAX_STEPS][STEP_LEN];
};

// --- the walker, kept out of the way -----------------------------------------
namespace jd {

inline constexpr int MAX_DEPTH = 12;

struct P {
  const char* p;
  const char* e;
};

inline void ws(P& c) {
  while (c.p < c.e && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) c.p++;
}

// A byte into an output buffer that is allowed to be full.
struct Out {
  char* d;
  int cap, n = 0;
  bool sp = false;  // a space is owed (whitespace collapses to one)
  void put(char ch) {
    if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r') {
      if (n) sp = true;
      return;
    }
    if (sp && n < cap - 1) d[n++] = ' ';
    sp = false;
    if (n < cap - 1) d[n++] = ch;
  }
  void utf8(uint32_t cp) {
    if (cp < 0x80) {
      put((char)cp);
    } else if (cp < 0x800) {
      put((char)(0xC0 | (cp >> 6)));
      put((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      put((char)(0xE0 | (cp >> 12)));
      put((char)(0x80 | ((cp >> 6) & 0x3F)));
      put((char)(0x80 | (cp & 0x3F)));
    } else {
      put((char)(0xF0 | (cp >> 18)));
      put((char)(0x80 | ((cp >> 12) & 0x3F)));
      put((char)(0x80 | ((cp >> 6) & 0x3F)));
      put((char)(0x80 | (cp & 0x3F)));
    }
  }
  void done() {
    if (d && cap > 0) d[n < cap ? n : cap - 1] = 0;
  }
};

inline uint32_t hex4(const char* s) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    const char ch = s[i];
    v <<= 4;
    if (ch >= '0' && ch <= '9') v |= (uint32_t)(ch - '0');
    else if (ch >= 'a' && ch <= 'f') v |= (uint32_t)(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F') v |= (uint32_t)(ch - 'A' + 10);
  }
  return v;
}

// The common entities recipe sites actually emit, plus numeric ones. An
// unknown entity passes through literally -- wrong-looking beats vanished.
inline bool entity(P& c, Out& o) {
  // The '&' itself is already consumed; c.p points at the entity's first
  // letter.
  const char* s = c.p;
  const char* stop = c.e < s + 12 ? c.e : s + 12;
  const char* semi = nullptr;
  for (const char* q = s; q < stop; q++)
    if (*q == ';') {
      semi = q;
      break;
    }
  if (!semi) return false;
  const int n = (int)(semi - s);
  auto is = [&](const char* w) { return (int)strlen(w) == n && strncmp(s, w, n) == 0; };
  if (is("amp")) o.put('&');
  else if (is("lt")) o.put('<');
  else if (is("gt")) o.put('>');
  else if (is("quot")) o.put('"');
  else if (is("apos")) o.put('\'');
  else if (is("nbsp")) o.put(' ');
  else if (n > 1 && s[0] == '#') {
    uint32_t v = 0;
    if (s[1] == 'x' || s[1] == 'X') {
      for (const char* q = s + 2; q < semi; q++) {
        v <<= 4;
        if (*q >= '0' && *q <= '9') v |= (uint32_t)(*q - '0');
        else if (*q >= 'a' && *q <= 'f') v |= (uint32_t)(*q - 'a' + 10);
        else if (*q >= 'A' && *q <= 'F') v |= (uint32_t)(*q - 'A' + 10);
      }
    } else {
      for (const char* q = s + 1; q < semi; q++)
        if (*q >= '0' && *q <= '9') v = v * 10 + (uint32_t)(*q - '0');
    }
    if (v == 0 || v > 0x10FFFF) return false;
    o.utf8(v);
  } else {
    return false;
  }
  c.p = semi + 1;
  return true;
}

// A JSON string, decoded and sanitised into `out` (which may be null to
// skip). HTML tags are dropped whole; entities and \u escapes become UTF-8;
// runs of whitespace collapse. Returns false only on malformed JSON.
inline bool str(P& c, char* out, int cap) {
  ws(c);
  if (c.p >= c.e || *c.p != '"') return false;
  c.p++;
  Out o{out, out ? cap : 0};
  bool tag = false;
  while (c.p < c.e && *c.p != '"') {
    char ch = *c.p;
    if (ch == '\\') {
      if (c.p + 1 >= c.e) return false;
      const char esc = c.p[1];
      c.p += 2;
      if (tag) continue;
      switch (esc) {
        case 'n': case 't': case 'r': o.put(' '); break;
        case 'u': {
          if (c.p + 4 > c.e) return false;
          uint32_t v = hex4(c.p);
          c.p += 4;
          // A surrogate pair is two \u escapes wearing one codepoint.
          if (v >= 0xD800 && v <= 0xDBFF && c.p + 6 <= c.e && c.p[0] == '\\' && c.p[1] == 'u') {
            const uint32_t lo = hex4(c.p + 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
              c.p += 6;
            }
          }
          o.utf8(v);
          break;
        }
        default: o.put(esc); break;
      }
      continue;
    }
    c.p++;
    if (ch == '<') {
      tag = true;
      continue;
    }
    if (tag) {
      if (ch == '>') {
        tag = false;
        o.sp = o.n > 0;  // a tag boundary keeps words apart
      }
      continue;
    }
    if (ch == '&' && entity(c, o)) continue;
    o.put(ch);
  }
  if (c.p >= c.e) return false;
  c.p++;  // closing quote
  o.done();
  return true;
}

inline bool skipValue(P& c, int depth);

inline bool skipObject(P& c, int depth) {
  c.p++;  // '{'
  ws(c);
  if (c.p < c.e && *c.p == '}') {
    c.p++;
    return true;
  }
  while (c.p < c.e) {
    if (!str(c, nullptr, 0)) return false;
    ws(c);
    if (c.p >= c.e || *c.p != ':') return false;
    c.p++;
    if (!skipValue(c, depth)) return false;
    ws(c);
    if (c.p < c.e && *c.p == ',') {
      c.p++;
      continue;
    }
    break;
  }
  ws(c);
  if (c.p >= c.e || *c.p != '}') return false;
  c.p++;
  return true;
}

inline bool skipArray(P& c, int depth) {
  c.p++;  // '['
  ws(c);
  if (c.p < c.e && *c.p == ']') {
    c.p++;
    return true;
  }
  while (c.p < c.e) {
    if (!skipValue(c, depth)) return false;
    ws(c);
    if (c.p < c.e && *c.p == ',') {
      c.p++;
      continue;
    }
    break;
  }
  ws(c);
  if (c.p >= c.e || *c.p != ']') return false;
  c.p++;
  return true;
}

inline bool skipValue(P& c, int depth) {
  if (depth > MAX_DEPTH) return false;
  ws(c);
  if (c.p >= c.e) return false;
  if (*c.p == '{') return skipObject(c, depth + 1);
  if (*c.p == '[') return skipArray(c, depth + 1);
  if (*c.p == '"') return str(c, nullptr, 0);
  // number / true / false / null: run to the next structural byte
  while (c.p < c.e && *c.p != ',' && *c.p != '}' && *c.p != ']' && *c.p != ' ' &&
         *c.p != '\n' && *c.p != '\t' && *c.p != '\r')
    c.p++;
  return true;
}

// "PT1H30M" (or "P0DT45M") in minutes; 0 for anything unreadable.
inline uint16_t isoMinutes(const char* s) {
  uint32_t mins = 0, num = 0;
  bool inTime = false;
  for (const char* q = s; *q; q++) {
    const char ch = *q;
    if (ch >= '0' && ch <= '9') {
      num = num * 10 + (uint32_t)(ch - '0');
    } else if (ch == 'T' || ch == 't') {
      inTime = true;
      num = 0;
    } else if (ch == 'D' || ch == 'd') {
      mins += num * 24 * 60;
      num = 0;
    } else if ((ch == 'H' || ch == 'h') && inTime) {
      mins += num * 60;
      num = 0;
    } else if ((ch == 'M' || ch == 'm') && inTime) {
      mins += num;
      num = 0;
    } else {
      num = (ch == 'P' || ch == 'p') ? 0 : num;
      if (ch == 'S' || ch == 's') num = 0;
    }
  }
  return mins > 60000 ? 0 : (uint16_t)mins;
}

inline void addIng(Recipe& r, const char* t) {
  if (!t[0] || r.nIng >= MAX_ING) return;
  snprintf(r.ing[r.nIng], ING_LEN, "%s", t);
  r.nIng++;
}

inline void addStep(Recipe& r, const char* t) {
  if (!t[0] || r.nSteps >= MAX_STEPS) return;
  snprintf(r.steps[r.nSteps], STEP_LEN, "%s", t);
  r.nSteps++;
}

inline bool instructions(P& c, Recipe& r, int depth);

// One object inside recipeInstructions: a HowToStep ({"text": ...}), a
// HowToSection or ItemList (steps under "itemListElement"), or something
// site-specific that still carries "text" or "name". Streaming, so the
// candidates are collected and the best one committed at the end.
inline bool instrObject(P& c, Recipe& r, int depth) {
  if (depth > MAX_DEPTH) return false;
  c.p++;  // '{'
  char text[STEP_LEN] = "", name[STEP_LEN] = "";
  bool hadList = false;
  ws(c);
  if (c.p < c.e && *c.p == '}') {
    c.p++;
    return true;
  }
  while (c.p < c.e) {
    char key[24];
    if (!str(c, key, sizeof(key))) return false;
    ws(c);
    if (c.p >= c.e || *c.p != ':') return false;
    c.p++;
    ws(c);
    if (strcmp(key, "text") == 0 && c.p < c.e && *c.p == '"') {
      if (!str(c, text, sizeof(text))) return false;
    } else if (strcmp(key, "name") == 0 && c.p < c.e && *c.p == '"') {
      if (!str(c, name, sizeof(name))) return false;
    } else if (strcmp(key, "itemListElement") == 0) {
      hadList = true;
      if (!instructions(c, r, depth + 1)) return false;
    } else {
      if (!skipValue(c, depth + 1)) return false;
    }
    ws(c);
    if (c.p < c.e && *c.p == ',') {
      c.p++;
      continue;
    }
    break;
  }
  ws(c);
  if (c.p >= c.e || *c.p != '}') return false;
  c.p++;
  // A section's own steps went in through itemListElement; its name is a
  // heading, not a step. A step object keeps its text, or failing that the
  // name some sites put the words in.
  if (!hadList) addStep(r, text[0] ? text : name);
  return true;
}

// recipeInstructions in any of its wild forms.
inline bool instructions(P& c, Recipe& r, int depth) {
  if (depth > MAX_DEPTH) return false;
  ws(c);
  if (c.p >= c.e) return false;
  if (*c.p == '"') {
    // One string. Newlines were collapsed to spaces by str(), so split on
    // sentence-ish boundaries is a guess not worth making: one block step
    // beats wrongly chopped ones. (Sites that publish steps publish arrays.)
    char big[STEP_LEN * 2];
    if (!str(c, big, sizeof(big))) return false;
    if ((int)strlen(big) < STEP_LEN) {
      addStep(r, big);
    } else {
      // Too long for one step: break at the space nearest each cap.
      const char* q = big;
      while (*q && r.nSteps < MAX_STEPS) {
        char piece[STEP_LEN];
        int n = (int)strlen(q);
        if (n >= STEP_LEN) {
          n = STEP_LEN - 1;
          while (n > 0 && q[n] != ' ') n--;
          if (n == 0) n = STEP_LEN - 1;
        }
        memcpy(piece, q, (size_t)n);
        piece[n] = 0;
        addStep(r, piece);
        q += n;
        while (*q == ' ') q++;
      }
    }
    return true;
  }
  if (*c.p == '[') {
    c.p++;
    ws(c);
    if (c.p < c.e && *c.p == ']') {
      c.p++;
      return true;
    }
    while (c.p < c.e) {
      ws(c);
      if (c.p < c.e && *c.p == '"') {
        char t[STEP_LEN];
        if (!str(c, t, sizeof(t))) return false;
        addStep(r, t);
      } else if (c.p < c.e && *c.p == '{') {
        if (!instrObject(c, r, depth + 1)) return false;
      } else {
        if (!skipValue(c, depth + 1)) return false;
      }
      ws(c);
      if (c.p < c.e && *c.p == ',') {
        c.p++;
        continue;
      }
      break;
    }
    ws(c);
    if (c.p >= c.e || *c.p != ']') return false;
    c.p++;
    return true;
  }
  if (*c.p == '{') return instrObject(c, r, depth + 1);
  return skipValue(c, depth + 1);
}

// recipeYield: "4 servings", 4, or ["4", "4 servings"] -- the wordiest wins.
inline bool yieldValue(P& c, Recipe& r, int depth) {
  ws(c);
  if (c.p >= c.e) return false;
  if (*c.p == '"') return str(c, r.yield, YIELD_LEN);
  if (*c.p == '[') {
    c.p++;
    while (c.p < c.e) {
      ws(c);
      if (c.p < c.e && *c.p == '"') {
        char t[YIELD_LEN];
        if (!str(c, t, sizeof(t))) return false;
        if (strlen(t) > strlen(r.yield)) snprintf(r.yield, YIELD_LEN, "%s", t);
      } else {
        if (!skipValue(c, depth + 1)) return false;
      }
      ws(c);
      if (c.p < c.e && *c.p == ',') {
        c.p++;
        continue;
      }
      break;
    }
    ws(c);
    if (c.p >= c.e || *c.p != ']') return false;
    c.p++;
    return true;
  }
  // a bare number
  const char* s = c.p;
  if (!skipValue(c, depth + 1)) return false;
  int n = (int)(c.p - s);
  if (n > YIELD_LEN - 1) n = YIELD_LEN - 1;
  memcpy(r.yield, s, (size_t)n);
  r.yield[n] = 0;
  return true;
}

// Does this object's @type say Recipe? Skims a COPY of the cursor, so the
// caller can still walk the object afterwards. @type may be a string or an
// array of strings, and may sit anywhere in the object.
inline bool typeIsRecipe(P c, int depth) {
  if (depth > MAX_DEPTH || c.p >= c.e || *c.p != '{') return false;
  c.p++;
  ws(c);
  if (c.p < c.e && *c.p == '}') return false;
  while (c.p < c.e) {
    char key[16];
    if (!str(c, key, sizeof(key))) return false;
    ws(c);
    if (c.p >= c.e || *c.p != ':') return false;
    c.p++;
    if (strcmp(key, "@type") == 0) {
      ws(c);
      char t[24];
      if (c.p < c.e && *c.p == '"') return str(c, t, sizeof(t)) && strcmp(t, "Recipe") == 0;
      if (c.p < c.e && *c.p == '[') {
        c.p++;
        while (c.p < c.e) {
          ws(c);
          if (c.p < c.e && *c.p == '"') {
            if (!str(c, t, sizeof(t))) return false;
            if (strcmp(t, "Recipe") == 0) return true;
          } else if (!skipValue(c, depth + 1)) {
            return false;
          }
          ws(c);
          if (c.p < c.e && *c.p == ',') {
            c.p++;
            continue;
          }
          break;
        }
        return false;
      }
      return false;
    }
    if (!skipValue(c, depth + 1)) return false;
    ws(c);
    if (c.p < c.e && *c.p == ',') {
      c.p++;
      continue;
    }
    break;
  }
  return false;
}

// Fill from an object already known to be a Recipe. The cursor is at '{'.
inline bool fill(P& c, Recipe& r, int depth) {
  if (depth > MAX_DEPTH) return false;
  c.p++;
  ws(c);
  if (c.p < c.e && *c.p == '}') {
    c.p++;
    return true;
  }
  while (c.p < c.e) {
    char key[28];
    if (!str(c, key, sizeof(key))) return false;
    ws(c);
    if (c.p >= c.e || *c.p != ':') return false;
    c.p++;
    ws(c);
    if (strcmp(key, "name") == 0 && c.p < c.e && *c.p == '"' && !r.name[0]) {
      if (!str(c, r.name, NAME_LEN)) return false;
    } else if (strcmp(key, "recipeYield") == 0 || strcmp(key, "yield") == 0) {
      if (!yieldValue(c, r, depth + 1)) return false;
    } else if (strcmp(key, "totalTime") == 0 || strcmp(key, "prepTime") == 0 ||
               strcmp(key, "cookTime") == 0) {
      char t[32];
      if (c.p < c.e && *c.p == '"') {
        if (!str(c, t, sizeof(t))) return false;
        const uint16_t m = isoMinutes(t);
        if (key[0] == 't') r.totalMin = m;
        else if (key[0] == 'p') r.prepMin = m;
        else r.cookMin = m;
      } else if (!skipValue(c, depth + 1)) {
        return false;
      }
    } else if (strcmp(key, "recipeIngredient") == 0 || strcmp(key, "ingredients") == 0) {
      ws(c);
      if (c.p < c.e && *c.p == '[') {
        c.p++;
        while (c.p < c.e) {
          ws(c);
          if (c.p < c.e && *c.p == '"') {
            char t[ING_LEN];
            if (!str(c, t, sizeof(t))) return false;
            addIng(r, t);
          } else if (c.p < c.e && *c.p == ']') {
            break;
          } else if (!skipValue(c, depth + 1)) {
            return false;
          }
          ws(c);
          if (c.p < c.e && *c.p == ',') {
            c.p++;
            continue;
          }
          break;
        }
        ws(c);
        if (c.p >= c.e || *c.p != ']') return false;
        c.p++;
      } else if (!skipValue(c, depth + 1)) {
        return false;
      }
    } else if (strcmp(key, "recipeInstructions") == 0 || strcmp(key, "instructions") == 0) {
      if (!instructions(c, r, depth + 1)) return false;
    } else {
      if (!skipValue(c, depth + 1)) return false;
    }
    ws(c);
    if (c.p < c.e && *c.p == ',') {
      c.p++;
      continue;
    }
    break;
  }
  ws(c);
  if (c.p >= c.e || *c.p != '}') return false;
  c.p++;
  return true;
}

// Walk anything -- a bare Recipe, an array, an @graph -- and fill from the
// first Recipe-typed object found. Returns true when one was filled.
inline bool find(P& c, Recipe& r, int depth) {
  if (depth > MAX_DEPTH) return false;
  ws(c);
  if (c.p >= c.e) return false;
  if (*c.p == '{') {
    if (typeIsRecipe(c, depth)) return fill(c, r, depth);
    // Not a Recipe itself: walk its members (this is where @graph lives).
    c.p++;
    ws(c);
    if (c.p < c.e && *c.p == '}') {
      c.p++;
      return false;
    }
    bool found = false;
    while (c.p < c.e) {
      if (!str(c, nullptr, 0)) return false;
      ws(c);
      if (c.p >= c.e || *c.p != ':') return false;
      c.p++;
      ws(c);
      if (!found && c.p < c.e && (*c.p == '{' || *c.p == '[')) {
        found = find(c, r, depth + 1);
        if (!found && c.p >= c.e) return false;
      } else {
        if (!skipValue(c, depth + 1)) return false;
      }
      ws(c);
      if (c.p < c.e && *c.p == ',') {
        c.p++;
        continue;
      }
      break;
    }
    ws(c);
    if (c.p < c.e && *c.p == '}') c.p++;
    return found;
  }
  if (*c.p == '[') {
    c.p++;
    ws(c);
    if (c.p < c.e && *c.p == ']') {
      c.p++;
      return false;
    }
    bool found = false;
    while (c.p < c.e) {
      ws(c);
      if (!found && c.p < c.e && (*c.p == '{' || *c.p == '[')) {
        found = find(c, r, depth + 1);
      } else {
        if (!skipValue(c, depth + 1)) return false;
      }
      ws(c);
      if (c.p < c.e && *c.p == ',') {
        c.p++;
        continue;
      }
      break;
    }
    ws(c);
    if (c.p < c.e && *c.p == ']') c.p++;
    return found;
  }
  return false;
}

}  // namespace jd

// The one entry point: schema.org Recipe JSON (alone, in an array, or under
// @graph) in, a filled Recipe out. False when nothing recognisable was found.
inline bool parse(const char* json, size_t len, Recipe& out) {
  memset(&out, 0, sizeof(out));
  jd::P c{json, json + len};
  if (!jd::find(c, out, 0)) return false;
  return out.name[0] && (out.nIng > 0 || out.nSteps > 0);
}

// "serves 4 - 1 h 30" for the recipe's header line; empty when it knows
// nothing. `cap` should be ~48.
inline void headline(const Recipe& r, char* out, int cap) {
  out[0] = 0;
  int n = 0;
  if (r.yield[0]) {
    // Yields arrive as "4", "4 servings", "Serves 4" -- a bare number gets
    // the word, anything wordier is already a phrase.
    bool bare = true;
    for (const char* q = r.yield; *q; q++)
      if (!(*q >= '0' && *q <= '9') && *q != ' ' && *q != '-') bare = false;
    if (bare)
      n += snprintf(out + n, cap - n, "serves %s", r.yield);
    else
      n += snprintf(out + n, cap - n, "%s", r.yield);
  }
  const uint16_t mins = r.totalMin ? r.totalMin
                                   : (uint16_t)(r.prepMin + r.cookMin);
  if (mins) {
    if (n) n += snprintf(out + n, cap - n, "  ·  ");
    if (mins >= 60)
      n += snprintf(out + n, cap - n, "%u h %u min", mins / 60, mins % 60);
    else
      n += snprintf(out + n, cap - n, "%u min", mins);
  }
}

}  // namespace rcp
