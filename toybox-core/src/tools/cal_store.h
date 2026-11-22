// Calendar items (v2, kit "Calendar").
//
// Notes with a time, not alarms (owner, 21 Nov): on battery a sleeping Sticky
// is switched off and cannot wake itself, so nothing here ever rings. An item
// whose time has come pops up over Home the next time the device is on --
// Remind later (again at the next wake) or Done.
//
// In flash (LittleFS), one fixed-length file: the calendar belongs to the
// device rather than to a card, and a card-less Sticky still has a calendar.
#pragma once
#include <stdint.h>
#include <string.h>

#include "tiny_fs.h"

namespace cal {

inline constexpr int MAX = 48;
inline constexpr int BEFORE_MAX = 7;
inline constexpr int TITLE = 40;
inline constexpr const char* PATH = "/calendar.bin";
inline constexpr uint32_t MAGIC = 0x314C4143u;  // "CAL1"

enum Repeat : uint8_t { REPEAT_NONE = 0, REPEAT_DAILY = 1, REPEAT_WEEKLY = 2, REPEAT_YEARLY = 3 };
inline constexpr int REPEATS = 4;

struct Item {
  uint16_t year;
  uint8_t month, day;   // 1-based
  uint8_t hour, minute;
  uint8_t repeat;       // Repeat
  uint8_t popup;        // 1 = pop up over Home when due
  uint8_t before;       // start popping up this many days ahead (0 = on the day)
  uint8_t allDay;       // 1 = no time: the whole day (v2, 22 Nov)
  uint32_t doneDay;     // yyyymmdd of the last occurrence marked done (0 = never)
  uint32_t until;       // a repeat's last day, yyyymmdd (0 = never ends)
  char title[TITLE];
};
static_assert(sizeof(Item) == 60, "calendar item layout");
// The first v2 layout, before all-day and repeat ends: read and converted.
inline constexpr size_t OLD_ITEM = 56;

struct Store {
  int n = 0;
  Item items[MAX];
};

inline uint32_t dayKey(int y, int m, int d) { return (uint32_t)(y * 10000 + m * 100 + d); }

// Sakamoto: 0 = Sunday.
inline int weekday(int y, int m, int d) {
  static const int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  y -= m < 3;
  return (y + y / 4 - y / 100 + y / 400 + T[m - 1] + d) % 7;
}
inline int daysIn(int y, int m) {
  static const int D[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : D[m - 1];
}
// Days since 2000-01-01, for "same weekday" and ordering.
inline int32_t dayNum(int y, int m, int d) {
  int32_t n = 0;
  for (int yy = 2000; yy < y; yy++) n += (yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0 ? 366 : 365;
  for (int mm = 1; mm < m; mm++) n += daysIn(y, mm);
  return n + d - 1;
}

// Does this item happen on (y, m, d)?
inline bool on(const Item& it, int y, int m, int d) {
  const int32_t start = dayNum(it.year, it.month, it.day), day = dayNum(y, m, d);
  if (day < start) return false;
  if (it.repeat != REPEAT_NONE && it.until && dayKey(y, m, d) > it.until) return false;
  switch (it.repeat) {
    case REPEAT_DAILY: return true;
    case REPEAT_WEEKLY: return (day - start) % 7 == 0;
    case REPEAT_YEARLY: return m == it.month && d == it.day;
    default: return day == start;
  }
}

inline void fromDayNum(int32_t n, int& y, int& m, int& d) {
  y = 2000;
  for (;;) {
    const int len = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 ? 366 : 365;
    if (n < len) break;
    n -= len;
    y++;
  }
  m = 1;
  while (n >= daysIn(y, m)) n -= daysIn(y, m++);
  d = (int)n + 1;
}

// How many days until the occurrence this item is popping up for, or -1 when
// it is not due now. 0 = today (or overdue, for a one-off never done). Fills
// occKey with that occurrence's yyyymmdd, which is what Done marks.
//
// The window opens `before` days ahead of the day and stays open until the
// occurrence is done. On the day itself, with no days ahead asked for, it
// opens at the item's time.
inline int dueIn(const Item& it, int y, int m, int d, int hh, int mm, uint32_t& occKey) {
  if (!it.popup) return -1;
  const int32_t today = dayNum(y, m, d);
  const int ahead = it.before > BEFORE_MAX ? BEFORE_MAX : it.before;
  const int nowMin = hh * 60 + mm, atMin = it.allDay ? 0 : it.hour * 60 + it.minute;
  if (it.repeat == REPEAT_NONE) {
    if (it.doneDay) return -1;
    const int32_t day = dayNum(it.year, it.month, it.day);
    occKey = dayKey(it.year, it.month, it.day);
    if (day < today) return 0;                                // overdue
    if (day == today) return ahead > 0 || atMin <= nowMin ? 0 : -1;
    return day - today <= ahead ? (int)(day - today) : -1;
  }
  for (int k = 0; k <= ahead; k++) {
    int oy, om, od;
    fromDayNum(today + k, oy, om, od);
    if (!on(it, oy, om, od)) continue;
    occKey = dayKey(oy, om, od);
    if (it.doneDay == occKey) return -1;
    if (k == 0 && ahead == 0 && atMin > nowMin) return -1;
    return k;
  }
  return -1;
}

inline bool due(const Item& it, int y, int m, int d, int hh, int mm) {
  uint32_t k = 0;
  return dueIn(it, y, m, d, hh, mm, k) >= 0;
}

inline void load(Store& s) {
  s.n = 0;
  size_t len = 0;
  char* buf = tfs::readAlloc(PATH, len);
  if (!buf) return;
  uint32_t magic = 0;
  uint16_t n = 0;
  if (len >= 6) {
    memcpy(&magic, buf, 4);
    memcpy(&n, buf + 4, 2);
  }
  if (magic == MAGIC && n <= MAX && len == 6 + (size_t)n * sizeof(Item)) {
    memcpy(s.items, buf + 6, (size_t)n * sizeof(Item));
    s.n = n;
  } else if (magic == MAGIC && n <= MAX && len == 6 + (size_t)n * OLD_ITEM) {
    // The first layout: fields up to `before`, then doneDay, then the title.
    for (int i = 0; i < n; i++) {
      const char* o = buf + 6 + (size_t)i * OLD_ITEM;
      Item it{};
      memcpy(&it, o, 9);  // year .. before
      memcpy(&it.doneDay, o + 12, 4);
      memcpy(it.title, o + 16, TITLE);
      it.title[TITLE - 1] = 0;
      s.items[i] = it;
    }
    s.n = n;
  }
  free(buf);
}

inline bool save(const Store& s) {
  const size_t len = 6 + (size_t)s.n * sizeof(Item);
  char* buf = (char*)malloc(len);
  if (!buf) return false;
  const uint32_t magic = MAGIC;
  const uint16_t n = (uint16_t)s.n;
  memcpy(buf, &magic, 4);
  memcpy(buf + 4, &n, 2);
  memcpy(buf + 6, s.items, (size_t)s.n * sizeof(Item));
  const bool ok = tfs::write(PATH, buf, len);
  free(buf);
  return ok;
}

// Adds in time order (by date, then time). False when full.
inline bool add(Store& s, const Item& it) {
  if (s.n >= MAX) return false;
  const int64_t key = (int64_t)dayNum(it.year, it.month, it.day) * 1440 +
                      (it.allDay ? 0 : it.hour * 60 + it.minute);
  int at = s.n;
  for (int i = 0; i < s.n; i++) {
    const Item& o = s.items[i];
    if ((int64_t)dayNum(o.year, o.month, o.day) * 1440 + (o.allDay ? 0 : o.hour * 60 + o.minute) > key) {
      at = i;
      break;
    }
  }
  memmove(&s.items[at + 1], &s.items[at], (size_t)(s.n - at) * sizeof(Item));
  s.items[at] = it;
  s.n++;
  return true;
}

inline void removeAt(Store& s, int i) {
  if (i < 0 || i >= s.n) return;
  memmove(&s.items[i], &s.items[i + 1], (size_t)(s.n - i - 1) * sizeof(Item));
  s.n--;
}

// "Remind later" lasts until the device is next switched on: kept in RAM only,
// so a power-off (which is what sleep is on battery) clears it.
inline uint64_t& snoozed() {
  static uint64_t bits = 0;
  return bits;
}

// The due items, in store order, skipping the snoozed ones. Returns the count
// and fills idx (up to cap).
inline int dueList(const Store& s, int y, int m, int d, int hh, int mm, int* idx, int cap) {
  int k = 0;
  for (int i = 0; i < s.n && k < cap; i++)
    if (!(snoozed() & (1ull << i)) && due(s.items[i], y, m, d, hh, mm)) idx[k++] = i;
  return k;
}

}  // namespace cal
