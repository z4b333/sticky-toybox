// The panel behind the power button, shared by both readers.
//
// The power button used to close the book. That was the only physical way out
// for a hand holding the device by its edge, and it stays the only physical
// way out -- but "out" is now one row of a panel that also carries the things
// a reader wants mid-book and had nowhere to ask for: where am I in this book,
// take me to that chapter, keep this place, make the text bigger.
//
// Drawn as full screens rather than an overlay, for two reasons. E-paper has
// no cheap way to put something translucent over a page and take it away
// again -- every appearance and dismissal costs a full refresh either way --
// and the panel's lists want the whole glass, because a chapter list on a
// 480x800 panel is seven rows at a time whatever else is on the screen.
//
// The geometry is shelf.h's, so a chapter list and a book list are the same
// object to a finger that has used one of them -- and the LOOK is the hub's:
// hairline dividers between things, not boxes around them, sentence case
// rather than shouting, and no filled slabs. The hub is the calmest screen in
// this firmware and it got that way by taking frames off.
#pragma once
#include "shelf.h"
#include "tools_ui.h"

namespace rmenu {

// How a page reaches the glass. There is no third KIND of refresh on this
// panel -- partial costs 0.3 s and leaves ghosting, full costs 1.7 s and does
// not, and that is the whole menu. What a setting can choose is how often a
// full one gets mixed in, so these three are three cadences and nothing more.
//
// Two of them, because the two readers show different things. A page of text
// is mostly white paper and ghosts so faintly that sixteen turns between
// cleans is comfortable; a page of 1-bit artwork has large black areas and
// shows the previous page behind them several turns earlier. Same panel, same
// eyes, different content -- so the answer is allowed to differ, and each
// reader keeps its own.
enum class Refresh : uint8_t { Fast = 0, Normal = 1, Best = 2 };

// Best is "clean every page", which is the same thing as a full refresh every
// time -- so one number covers all three and the host needs no special case.
inline int cleanEvery(Refresh r) {
  return r == Refresh::Fast ? 16 : r == Refresh::Normal ? 8 : 1;
}

inline const char* refreshLabel(Refresh r) {
  return r == Refresh::Fast ? "fast" : r == Refresh::Normal ? "normal" : "best";
}
inline const char* refreshSub(Refresh r) {
  return r == Refresh::Fast    ? "0.3 s a page, clean every 16th"
         : r == Refresh::Normal ? "0.3 s a page, clean every 8th"
                                : "1.7 s a page, never any ghosting";
}

inline Refresh nextRefresh(Refresh r) {
  return r == Refresh::Fast ? Refresh::Normal : r == Refresh::Normal ? Refresh::Best : Refresh::Fast;
}

// NVS keys are short by law, and these two have to be told apart at a glance
// in a dump: ep for the EPUB reader, bk for the .tbk one.
inline const char* refreshKey(bool epub) { return epub ? "rd_ref_ep" : "rd_ref_bk"; }

inline Refresh refreshMode(Preferences& p, bool epub) {
  const uint32_t v = p.getUInt(refreshKey(epub), 0xFF);
  if (v <= (uint32_t)Refresh::Best) return (Refresh)v;
  // No per-reader answer stored yet. A device coming from a build that had one
  // switch for both keeps what that switch was set to, so nothing changes
  // underneath somebody on an update; a device that never touched it gets the
  // default for the kind of thing this reader shows.
  const uint32_t old = p.getUInt("rd_fast", 0xFF);
  if (old == 0) return Refresh::Best;
  if (old == 1) return Refresh::Normal;
  return epub ? Refresh::Fast : Refresh::Normal;
}

inline void setRefreshMode(Preferences& p, bool epub, Refresh r) {
  p.putUInt(refreshKey(epub), (uint32_t)r);
}

// Which page of the panel is showing. `None` means the book itself.
enum class Page : uint8_t { None, Root, Contents, Marks, Text, Keep, Font };

// Rows, generously spaced, divided by hairlines. A thumb needs the height
// whether or not there is a box drawn around it.
// Seven rows fit at 98: 110 + 7*98 = 796, which is the panel. They were 116
// when six was the most the panel offered, and a thumb does not notice the
// difference -- 98 px is still two thirds of an inch.
inline constexpr int ROOT_Y0 = 110, ROOT_H = 98, ROOT_GAP = 0;
inline constexpr int ROOT_MARGIN = 24;

inline TRect rootRect(int i, int w) {
  return {ROOT_MARGIN, ROOT_Y0 + i * (ROOT_H + ROOT_GAP), w - 2 * ROOT_MARGIN, ROOT_H};
}

// A root item. `sub` is the line under the label -- the state of the thing, so
// the panel answers as much as it offers ("3 kept", "medium, normal").
struct Item {
  const char* label;
  const char* sub;
  bool plus = false;  // a square at the right end that does the obvious thing
};

// The + on a row. Big enough to hit without looking, inset far enough that a
// thumb going for the row itself does not land on it. Drawn as a thin cross in
// a hairline circle: it is a second control on a row, not a second row.
inline TRect plusRect(int i, int w) {
  const TRect r = rootRect(i, w);
  return {r.x + r.w - 84, r.y + (r.h - 72) / 2, 72, 72};
}

// `back` names what the panel is over. The readers are over a book, so it
// says READ; the recipe app is over a recipe, and telling a cook to go back to
// "READ" is a word from another screen.
// The time for an options panel's corner, or nullptr when no clock has been
// set -- an invented time is worse than no time, so nothing is drawn.
//
// It belongs on a panel rather than on the page behind it. E-paper holds its
// last picture, so a clock drawn on a page turn is wrong by the next page and
// stays wrong for as long as the reading lasts; a panel is drawn fresh every
// time it is opened, so the time on it is the time it was asked for. Same
// corner and same size as the hub's, which is where people already look.
inline const char* clockCorner(ToolsHost& h, char* out, int cap) {
  int hh = 0, mm = 0;
  if (!h.clockHHMM(hh, mm)) return nullptr;
  snprintf(out, cap, "%02d:%02d", hh, mm);
  return out;
}

inline void drawRoot(ToolsHost& h, ToolsCanvas& c, const char* title, const Item* items, int n,
                     const char* back = "READ", const char* corner = nullptr) {
  h.topBar(title, false, back, corner);
  for (int i = 0; i < n; i++) {
    const TRect r = rootRect(i, c.width());
    if (i > 0) c.fillRect(r.x, r.y, r.w, 1, true);  // hairlines between, none around
    c.text(r.x + 8, r.y + 20, items[i].label, TS_LARGE, true);
    const int subW = r.w - 16 - (items[i].plus ? 92 : 0);
    if (items[i].sub && items[i].sub[0])
      c.textClipped(r.x + 8, r.y + 62, subW, items[i].sub, TS_SMALL, true);
    if (items[i].plus) {
      const TRect p = plusRect(i, c.width());
      const int cx = p.x + p.w / 2, cy = p.y + p.h / 2;
      c.drawCircle(cx, cy, p.w / 2 - 2, 1, true);
      c.fillRect(cx - 15, cy - 1, 30, 2, true);
      c.fillRect(cx - 1, cy - 15, 2, 30, true);
    }
  }
}

inline int hitRoot(int x, int y, int n, int w) {
  for (int i = 0; i < n; i++)
    if (rootRect(i, w).hit(x, y)) return i;
  return -1;
}

// True when the tap landed on that row's +, which means something different
// from the row: the row opens the list, the + adds to it.
inline bool hitPlus(int x, int y, int i, int w) { return plusRect(i, w).hit(x, y); }

// --- a minus/plus stepper ---------------------------------------------------
// Two hairline circles either side of the value it changes, which is how the
// reader's TEXT page has always offered size and spacing. Shared so the recipe
// app offers it the same way: a stepper on one screen and a cycling row on
// another are two controls for one idea, and the second one has to be learned.
//
// A stepper rather than a cycle because these ARE a scale -- smaller and
// bigger, tighter and airier, with ends you can reach. Rotation is not a
// scale, which is why it gets three buttons instead.
inline constexpr int STEP_H = 120;   // one stepper, label and all
inline constexpr int STEP_R = 34;    // the circles
inline constexpr int STEP_IN = 66;   // their centres, in from each edge

inline void drawStepper(ToolsCanvas& c, int y, const char* label, const char* value) {
  c.text(28, y, label, TS_SMALL, true);
  const int cy = y + 64;
  c.drawCircle(STEP_IN, cy, STEP_R, 1, true);
  c.fillRect(STEP_IN - 15, cy - 1, 30, 2, true);
  c.drawCircle(c.width() - STEP_IN, cy, STEP_R, 1, true);
  c.fillRect(c.width() - STEP_IN - 15, cy - 1, 30, 2, true);
  c.fillRect(c.width() - STEP_IN - 1, cy - 15, 2, 30, true);
  c.textCentered(c.width() / 2, cy - c.textHeight(TS_LARGE) / 2, value, TS_LARGE, true);
}

// -1 for the minus, +1 for the plus, 0 for anywhere else on the row. The
// middle is the value, and tapping a value should not change it.
inline int hitStepper(int x, int y, int rowY, int w) {
  const int y0 = rowY + 30;
  if (y < y0 || y >= y0 + 68) return 0;
  if (x < 120) return -1;
  if (x >= w - 120) return 1;
  return 0;
}

// --- line spacing -----------------------------------------------------------
// Three settings, shared: the EPUB reader, the comic reader and the recipe
// app all put air between lines and there is no reason for three vocabularies
// or three sets of numbers. The gap is added to the line's own height, so it
// means the same thing at every text size.
inline constexpr int LEADS = 3;
inline const char* leadName(int i) { return i <= 0 ? "tight" : (i == 1 ? "normal" : "airy"); }
inline int leadAir(int i) { return i <= 0 ? 6 : (i == 1 ? 10 : 18); }

// --- rotation, as three buttons ---------------------------------------------
// One tap to any of the three, rather than a cycle through the one you do not
// want. The arrows say which way the device turns and the filled one is where
// you are. Shared: the readers and the recipe app set the same thing, and a
// setting that looks like two controls is two controls to learn.
//
// Confirmed on glass: rotation 1 turns the text toward the panel's left side,
// 3 toward its right. Offered in the order the arrows suggest.
inline constexpr uint8_t ROT_BTN[3] = {1, 0, 3};

inline TRect rotBtnRect(const TRect& row, int k) {
  const int bw = (row.w - 32) / 3;
  return {row.x + 8 + k * (bw + 8), row.y + 48, bw, 42};
}

inline void drawRotRow(ToolsCanvas& c, int slot, uint8_t rot) {
  const TRect r = rootRect(slot, c.width());
  static const char* kLab[3] = {"< LEFT", "PORTRAIT", "RIGHT >"};
  for (int k = 0; k < 3; k++) {
    const TRect b = rotBtnRect(r, k);
    c.button(b.x, b.y, b.w, b.h, kLab[k], rot == ROT_BTN[k], TS_SMALL);
  }
}

// Which rotation that tap chose, or -1 for none: the row outside the buttons
// chooses nothing rather than the nearest thing.
inline int hitRot(int x, int y, int slot, int w) {
  const TRect r = rootRect(slot, w);
  for (int k = 0; k < 3; k++)
    if (rotBtnRect(r, k).hit(x, y)) return ROT_BTN[k];
  return -1;
}

// A list page: the same seven rows and the same pager as a shelf, with a
// second line per row for whatever the row is about.
inline void drawRow(ToolsCanvas& c, int slot, const char* label, const char* sub, bool rule,
                    bool marked = false) {
  const int y = shelf::Y0 + slot * shelf::ROW_H;
  // Where you are is a thin rule beside the row, not a slab: it has to be
  // findable at a glance without being the loudest thing on the page.
  if (marked) c.fillRect(16, y + 10, 3, shelf::ROW_H - 28, true);
  c.textClipped(marked ? 34 : 24, y + 10, c.width() - (marked ? 58 : 48), label, TS_MED, true);
  if (sub && sub[0]) c.text(marked ? 34 : 24, y + 44, sub, TS_SMALL, true);
  if (rule) c.fillRect(16, y + shelf::ROW_H - 6, c.width() - 32, 1, true);
}

// A row whose label is allowed two lines: bookmarks carry whole sentences
// now, and a sentence clipped to one line usually lost the half somebody
// kept it for. Wrapped forward a word at a time (the same discipline as the
// KEEP screen: measuring a string you have already cut decides line two from
// a length that no longer exists); the second line clips if even two lines
// are not enough, and the sub drops to the row's foot.
inline void drawRowWrap(ToolsCanvas& c, int slot, const char* label, const char* sub, bool rule,
                        bool marked = false) {
  const int y = shelf::Y0 + slot * shelf::ROW_H;
  if (marked) c.fillRect(16, y + 10, 3, shelf::ROW_H - 28, true);
  const int x = marked ? 34 : 24;
  const int w = c.width() - x - 24;
  char line1[128] = "";
  const char* rest = nullptr;
  for (const char* p = label; *p;) {
    const char* e = p;
    while (*e && *e != ' ') e++;
    char cand[128];
    snprintf(cand, sizeof(cand), "%s%s%.*s", line1, line1[0] ? " " : "", (int)(e - p), p);
    if (line1[0] && c.textWidth(cand, TS_MED) > w) {
      rest = p;  // the word that did not fit starts line two
      break;
    }
    snprintf(line1, sizeof(line1), "%s", cand);
    p = e;
    while (*p == ' ') p++;
  }
  c.textClipped(x, y + 6, w, line1, TS_MED, true);
  if (rest) c.textClipped(x, y + 34, w, rest, TS_MED, true);
  if (sub && sub[0]) c.text(x, y + 62, sub, TS_SMALL, true);
  if (rule) c.fillRect(16, y + shelf::ROW_H - 6, c.width() - 32, 1, true);
}

inline void drawEmpty(ToolsCanvas& c, const char* what, const char* how) {
  c.textCentered(c.width() / 2, 320, what, TS_LARGE, true);
  c.textCentered(c.width() / 2, 368, how, TS_SMALL, true);
}

}  // namespace rmenu
