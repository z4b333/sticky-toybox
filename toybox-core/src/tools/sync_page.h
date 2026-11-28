// The Reading sync page (D): the account, what to sync, and when it runs by
// itself. One page, drawn the same from Settings > CONNECT and from a book's
// settings (owner, 26 Nov: "Reading sync ›" in Book settings), so the two
// can never disagree about a switch.
#pragma once
#include "../chrome.h"
#include "kosync.h"
#include "readest_hl.h"
#include "tools_ui.h"

namespace syncpg {

inline constexpr int X = 16, W = SCREEN_W - 32;
inline constexpr int ACC_Y = TOPBAR_H + 40, ROW_H = 62, ROWS = 5;
inline int rowY(int i) { return TOPBAR_H + 40 + 70 + 34 + i * (ROW_H + 4) + (i >= 2 ? 34 : 0); }
inline TRect row(int i) { return TRect{X, rowY(i), W, ROW_H}; }
inline TRect signOutRect() { return TRect{SCREEN_W - 16 - 120, ACC_Y + 10, 120, 48}; }

// The kit's light switch (owner, 25 Nov): a rectangle with a rectangular
// slider. Off: white track, black slider left. On: black track, white
// slider right.
inline constexpr int SWITCH_W = 68, SWITCH_H = 34;
inline void lightSwitch(ToolsCanvas& c, int x, int y, bool on) {
  if (on) c.fillRect(x, y, SWITCH_W, SWITCH_H, true);
  else c.drawRect(x, y, SWITCH_W, SWITCH_H, 2, true);
  const int kw = 28, ky = y + 4, kh = SWITCH_H - 8;
  c.fillRect(on ? x + SWITCH_W - 4 - kw : x + 4, ky, kw, kh, !on);
}

struct Row {
  const char* key;
  const char* name;
  const char* sub;
};
inline constexpr Row kRows[ROWS] = {
    {rhl::K_POS, "Reading position", nullptr},
    {rhl::K_HL, "Highlights", "deleted on one side, gone on both"},
    {rhl::K_AUTO, "Auto sync", "off: only the Sync button"},
    {rhl::K_OPEN, "When a book opens", "gets your place and new highlights"},
    {rhl::K_CLOSE, "When a book closes or it sleeps", "sends your place and highlights"},
};

inline void head(ToolsCanvas& c, int y, const char* label, const char* right) {
  c.textTracked(X, y, label, TS_SMALL, true, false, 2);
  if (right) c.text(X + W - c.textWidth(right, TS_SMALL), y, right, TS_SMALL, true);
  chrome2::dottedRule(c, X, y + c.textHeight(TS_SMALL) + 6, W);
}

// Everything under the top bar. `armed`: Sign out asked once already.
inline void render(ToolsCanvas& c, Preferences& p, bool armed, const char* note) {
  const bool acct = kosync::haveAccount(p);
  head(c, ACC_Y - 30, "ACCOUNT", nullptr);
  char name[kosync::USER_MAX] = "", line[96];
  if (acct) kosync::user(p, name, sizeof(name));
  c.text(X, ACC_Y + 8, acct ? (kosync::isReadest(p) ? "Readest" : "KOReader sync") : "Not signed in", TS_MED,
         true, true);
  if (acct)
    snprintf(line, sizeof(line), "%s", name);
  else
    snprintf(line, sizeof(line), "sign in from a book: More > Sync");
  c.textClipped(X, ACC_Y + 8 + c.textHeight(TS_MED) + 2, W - 140, line, TS_SMALL, true);
  if (acct) {
    const TRect so = signOutRect();
    if (armed) c.fillRect(so.x, so.y, so.w, so.h, true);
    else c.drawRect(so.x, so.y, so.w, so.h, 2, true);
    c.textInBox(so.x, so.y, so.w, so.h, armed ? "Sure?" : "Sign out", TS_SMALL, !armed, true);
  }
  head(c, rowY(0) - 30, "WHAT TO SYNC", nullptr);
  head(c, rowY(2) - 30, "AUTO SYNC", "over the home Wi-Fi");
  const bool autoOn = p.getBool(rhl::K_AUTO, true);
  for (int i = 0; i < ROWS; i++) {
    const TRect r = row(i);
    const bool dim = i >= 3 && !autoOn;
    const bool on = p.getBool(kRows[i].key, true);
    const int nameY = r.y + (kRows[i].sub ? 8 : (r.h - c.textHeight(TS_MED)) / 2);
    c.text(r.x, nameY, kRows[i].name, TS_MED, true);
    if (kRows[i].sub) c.text(r.x, nameY + c.textHeight(TS_MED) + 2, kRows[i].sub, TS_SMALL, true);
    const int sx = r.x + r.w - SWITCH_W - 4, sy = r.y + (r.h - SWITCH_H) / 2;
    lightSwitch(c, sx, sy, on && !dim);
    if (dim) {  // the timing switches stand down while auto sync is off: a light screen over them
      for (int yy = r.y; yy < r.y + r.h; yy += 2)
        for (int xx = r.x + (yy & 2 ? 1 : 0); xx < r.x + r.w; xx += 2) c.fillRect(xx, yy, 1, 1, false);
    }
    c.fillRect(r.x, r.y + r.h + 1, r.w, 1, true);
  }
  if (note) c.textCentered(SCREEN_W / 2, 776, note, TS_SMALL, true);
}

// True when the page changed. `armed` and `note` are the caller's to keep.
inline bool tap(ToolsHost& host, int x, int y, bool& armed, const char*& note) {
  Preferences& p = host.prefs();
  const bool wasArmed = armed;
  armed = false;
  note = nullptr;
  if (kosync::haveAccount(p) && signOutRect().hit(x, y)) {
    if (!wasArmed) {
      armed = true;  // two taps: signing out is not undone by a third
      host.beep(2);
    } else {
      kosync::forgetAccount(p);
      note = "signed out: sign in again from a book";
      host.beep(1);
    }
    return true;
  }
  for (int i = 0; i < ROWS; i++) {
    if (!row(i).hit(x, y)) continue;
    if (i >= 3 && !p.getBool(rhl::K_AUTO, true)) {
      host.beep(2);
      note = "turn Auto sync on first";
      return true;
    }
    rhl::flip(p, kRows[i].key);
    host.beep(1);
    return true;
  }
  return wasArmed;
}

}  // namespace syncpg
