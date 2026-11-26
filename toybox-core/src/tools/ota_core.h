// Updates over Wi-Fi: the parts that are pure decisions.
//
// The device half (src/ota.h) streams bytes into flash; everything that is a
// judgement lives here, with no radio, no flash and no clock, so the host
// harness can put every case through it:
//
//   * is the build on the site newer than this one?
//   * what does version.json say, and is it complete enough to act on?
//   * is the battery good enough to start?
//   * the pairing code a computer on the home network must type, and the
//     lock-out after too many wrong ones;
//   * the rollback rule: a new build that cannot reach the home screen three
//     boots running goes back to the slot it came from.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ota {

// Where a Sticky looks for updates unless told otherwise (NVS `ota_src`).
// version.json and the files it names live under it; paths are relative.
inline constexpr const char* DEFAULT_SRC = "https://z4b333.github.io/sticky-toybox/";
inline constexpr const char* K_SRC = "ota_src";
// The rollback record (see trial*): which slot is on trial, which one it came
// from, and how many boots it has had to prove itself.
inline constexpr const char* K_TRIAL = "ota_trial";  // slot label on trial, "" = none
inline constexpr const char* K_PREV = "ota_prev";    // the slot to go back to
inline constexpr const char* K_TRIES = "ota_tries";  // boots so far without confirming
inline constexpr const char* K_TVER = "ota_tver";    // the version on trial, read from its image
inline constexpr const char* K_ROLLED = "ota_rolled"; // version that was rolled back, for the notice
inline constexpr const char* K_BASE = "ota_base";    // sha256 of the base pack last written over the air
inline constexpr int MAX_TRIES = 3;
// Owner, 26 Nov: 20 % (was 30) for every way of updating, USB or not aside.
inline constexpr int DEFAULT_MIN_BATTERY = 20;

// --- versions -------------------------------------------------------------------
// The strings are `git describe` output: "v1.4.4", "v2.0.0-beta.3", or a build
// between tags, "v2.0.0-beta.3-7-gab89111" (7 commits past the tag).

struct Ver {
  int major = 0, minor = 0, patch = 0;
  bool pre = false;     // has a -beta/-rc part: older than the same X.Y.Z without one
  int preNum = 0;       // beta.N
  int past = 0;         // commits past the tag (a development build)
  bool ok = false;
};

inline Ver parse(const char* s) {
  Ver v;
  if (!s) return v;
  if (*s == 'v' || *s == 'V') s++;
  char* end;
  v.major = (int)strtol(s, &end, 10);
  if (end == s || *end != '.') return v;
  s = end + 1;
  v.minor = (int)strtol(s, &end, 10);
  if (end == s || *end != '.') return v;
  s = end + 1;
  v.patch = (int)strtol(s, &end, 10);
  if (end == s) return v;
  s = end;
  v.ok = true;
  // -beta.N / -rc.N : a prerelease. The number is the last digits before the
  // describe suffix, if any.
  if (*s == '-' && (s[1] < '0' || s[1] > '9')) {
    v.pre = true;
    s++;
    while (*s && *s != '.' && *s != '-') s++;
    if (*s == '.') {
      v.preNum = (int)strtol(s + 1, &end, 10);
      s = end;
    }
  }
  // -N-gHASH : N commits past the tag.
  if (*s == '-' && s[1] >= '0' && s[1] <= '9') {
    const int n = (int)strtol(s + 1, &end, 10);
    if (end[0] == '-' && end[1] == 'g') v.past = n;
  }
  return v;
}

// <0, 0, >0 like strcmp. A development build sorts after its own tag and
// before the next one, which is what "is the site's build newer" needs: a
// device running v2.0.0-3-gabc is not offered v2.0.0 again.
inline int compare(const Ver& a, const Ver& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  if (a.pre != b.pre) return a.pre ? -1 : 1;  // 2.0.0-beta < 2.0.0
  if (a.preNum != b.preNum) return a.preNum < b.preNum ? -1 : 1;
  if (a.past != b.past) return a.past < b.past ? -1 : 1;
  return 0;
}

// Offer an update only for a real, strictly newer version. A running string
// that does not parse (a bare commit hash from a build without tags) is
// offered the site's release: that is what someone holding such a build wants.
inline bool isNewer(const char* remote, const char* running) {
  const Ver r = parse(remote);
  if (!r.ok) return false;
  const Ver l = parse(running);
  if (!l.ok) return true;
  return compare(r, l) > 0;
}

// --- version.json -----------------------------------------------------------------

inline constexpr size_t VER_MAX = 48, PATH_MAX_ = 96, SHA_HEX = 65;

struct Part {
  char path[PATH_MAX_] = {};
  uint32_t bytes = 0;
  char sha256[SHA_HEX] = {};  // 64 lowercase hex digits
  bool present() const { return path[0] && bytes && strlen(sha256) == 64; }
};

struct Remote {
  char version[VER_MAX] = {};
  char date[24] = {};
  Part app, base;
  int minBattery = DEFAULT_MIN_BATTERY;
};

// A deliberately small reader for the one file make_image.sh writes. It finds
// a key inside an optional enclosing object ("ota" -> "app" -> "sha256") by
// scanning for the quoted names in order; it does not need to be a general
// JSON parser, because a file it does not understand simply means "no update".
inline const char* findKey(const char* from, const char* end, const char* key) {
  const size_t n = strlen(key);
  for (const char* p = from; p && p + n + 2 < end; p++) {
    if (*p == '"' && strncmp(p + 1, key, n) == 0 && p[n + 1] == '"') {
      p += n + 2;
      while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
      if (p < end && *p == ':') return p + 1;
    }
  }
  return nullptr;
}

// The span of the {...} that starts at or after `at`.
inline bool objectSpan(const char* at, const char* end, const char*& b, const char*& e) {
  while (at < end && *at != '{') {
    if (*at != ' ' && *at != '\n' && *at != '\r' && *at != '\t') return false;
    at++;
  }
  if (at >= end) return false;
  int depth = 0;
  bool inStr = false;
  for (const char* p = at; p < end; p++) {
    if (inStr) {
      if (*p == '\\') p++;
      else if (*p == '"') inStr = false;
      continue;
    }
    if (*p == '"') inStr = true;
    else if (*p == '{') depth++;
    else if (*p == '}' && --depth == 0) {
      b = at;
      e = p + 1;
      return true;
    }
  }
  return false;
}

inline bool readString(const char* at, const char* end, char* out, size_t n) {
  while (at < end && (*at == ' ' || *at == '\n' || *at == '\r' || *at == '\t')) at++;
  if (at >= end || *at != '"') return false;
  at++;
  size_t i = 0;
  while (at < end && *at != '"') {
    if (*at == '\\' || i + 1 >= n) return false;  // nothing we write needs escapes
    out[i++] = *at++;
  }
  out[i] = 0;
  return at < end;
}

inline bool readUint(const char* at, const char* end, uint32_t& v) {
  while (at < end && (*at == ' ' || *at == '\n' || *at == '\r' || *at == '\t')) at++;
  if (at >= end || *at < '0' || *at > '9') return false;
  uint64_t x = 0;
  while (at < end && *at >= '0' && *at <= '9') {
    x = x * 10 + (uint64_t)(*at - '0');
    if (x > 0xFFFFFFFFull) return false;
    at++;
  }
  v = (uint32_t)x;
  return true;
}

inline bool readPart(const char* b, const char* e, const char* name, Part& out) {
  const char* k = findKey(b, e, name);
  const char *pb, *pe;
  if (!k || !objectSpan(k, e, pb, pe)) return false;
  const char* v;
  if (!(v = findKey(pb, pe, "path")) || !readString(v, pe, out.path, sizeof(out.path))) return false;
  if (!(v = findKey(pb, pe, "bytes")) || !readUint(v, pe, out.bytes)) return false;
  if (!(v = findKey(pb, pe, "sha256")) || !readString(v, pe, out.sha256, sizeof(out.sha256)))
    return false;
  if (strlen(out.sha256) != 64) return false;
  for (const char* s = out.sha256; *s; s++)
    if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return false;
  // A path is relative to the source and stays inside it.
  if (out.path[0] == '/' || strstr(out.path, "..") || strstr(out.path, "://")) return false;
  return true;
}

// True only when everything an install needs is there. The base pack is
// optional: an older version.json without it updates the app alone.
inline bool parseRemote(const char* json, size_t len, Remote& r) {
  r = Remote{};
  const char* end = json + len;
  const char* v = findKey(json, end, "version");
  if (!v || !readString(v, end, r.version, sizeof(r.version)) || !parse(r.version).ok) return false;
  if ((v = findKey(json, end, "date"))) readString(v, end, r.date, sizeof(r.date));
  const char* k = findKey(json, end, "ota");
  const char *ob, *oe;
  if (!k || !objectSpan(k, end, ob, oe)) return false;
  if (!readPart(ob, oe, "app", r.app)) return false;
  readPart(ob, oe, "base", r.base);
  uint32_t mb = 0;
  if ((v = findKey(ob, oe, "minBattery")) && readUint(v, oe, mb) && mb <= 100) r.minBattery = (int)mb;
  return true;
}

// --- may it start? ----------------------------------------------------------------
// Writing an app slot takes about a minute with the radio on; a device that
// browns out half-way keeps running its old build (the new slot never became
// the boot slot), but the person has to start again. On USB power, any level.

enum class Gate : uint8_t { Ok, LowBattery, TooBig };

inline Gate mayStart(int batteryPercent, bool onPower, int minBattery, uint32_t appBytes,
                     uint32_t slotBytes) {
  if (appBytes > slotBytes) return Gate::TooBig;
  if (!onPower && batteryPercent >= 0 && batteryPercent < minBattery) return Gate::LowBattery;
  return Gate::Ok;
}

// --- pairing ----------------------------------------------------------------------
// Updating from a computer opens a page on the home network (toybox.local).
// Anybody on that network can load the page; only somebody who can READ THE
// PANEL can use it. The code is six digits, new every time the page is opened,
// and five wrong tries close the door until the page is opened again.

struct Pairing {
  char code[7] = {};
  uint8_t wrong = 0;
  static constexpr uint8_t MAX_WRONG = 5;

  void fresh(uint32_t random) {
    snprintf(code, sizeof(code), "%06u", (unsigned)(random % 1000000u));
    wrong = 0;
  }
  bool locked() const { return wrong >= MAX_WRONG; }
  // Constant time over the six digits, so the reply time says nothing.
  bool check(const char* tried) {
    if (locked() || !code[0]) return false;
    uint8_t diff = 0;
    size_t n = tried ? strlen(tried) : 0;
    diff |= (uint8_t)(n != 6);
    for (int i = 0; i < 6; i++) diff |= (uint8_t)((i < (int)n ? tried[i] : 0) ^ code[i]);
    if (diff) {
      wrong++;
      return false;
    }
    return true;
  }
};

// --- rollback ---------------------------------------------------------------------
// Before rebooting into a new build the device records a trial: the slot it is
// trying, the slot it came from, zero tries. Every boot of the trial slot
// counts one; reaching the home screen confirms it and clears the record.
// A third boot that never got there means the new build cannot run here:
// switch back. (When the bootloader has its own rollback compiled in, it
// covers the crash-before-setup case too; this covers everything after.)

struct Trial {
  char slot[17] = {};  // the label of the slot on trial
  char prev[17] = {};  // where to go back to
  int tries = 0;
};

enum class BootAction : uint8_t {
  Nothing,    // no trial, or not running the trial slot
  Count,      // on trial: count this boot and carry on
  RollBack,   // out of tries: boot `prev`
  Forget,     // the record names a slot that is not running and was never confirmed: drop it
};

inline BootAction onBoot(const Trial& t, const char* running) {
  if (!t.slot[0]) return BootAction::Nothing;
  if (strcmp(t.slot, running) != 0) {
    // Not running the build on trial: either the bootloader already went
    // back, or the write never became the boot slot. Nothing to count.
    return BootAction::Forget;
  }
  if (t.tries >= MAX_TRIES) return BootAction::RollBack;
  return BootAction::Count;
}

// --- display ------------------------------------------------------------------------

// "1.4 MB" style, for the progress and size lines.
inline void sizeText(uint32_t bytes, char* out, size_t n) {
  if (bytes >= 1024u * 1024u)
    snprintf(out, n, "%u.%u MB", (unsigned)(bytes / (1024u * 1024u)),
             (unsigned)((bytes % (1024u * 1024u)) * 10u / (1024u * 1024u)));
  else
    snprintf(out, n, "%u KB", (unsigned)((bytes + 1023u) / 1024u));
}

// --- from the SD card ---------------------------------------------------------------
// Settings > Update & about > FROM SD CARD (owner, 25-26 Nov). Only these
// names, only in /toybox/update/ -- the files from the release page, as they
// are. Nothing else on the card is read for this.

inline constexpr const char* CARD_DIR = "/toybox/update/";
inline constexpr int CARD_FILES = 5;

struct CardFile {
  const char* file;   // name in CARD_DIR
  const char* label;  // the partition it goes to; nullptr = the app slot
  const char* title;  // as the screen names it
  uint32_t bytes = 0;
  bool found = false;
};

struct Card {
  enum State : uint8_t { NoCard, Nothing, NotToybox, TooBig, Found };
  State state = NoCard;
  char version[VER_MAX] = {};  // read out of toybox-app.bin, if it is there
  char bad[24] = {};           // the file a refusal is about
  CardFile files[CARD_FILES] = {
      {"toybox-app.bin", nullptr, "Firmware"},
      {"base_font.tfp", "base_font", "Base font pack"},
      {"zh_full.tfp", "zh_font", "Chinese pack"},
      {"ko_full.tfp", "ko_font", "Korean pack"},
      {"ja_full.tfp", "ja_font", "Japanese pack"},
  };
  int count() const {
    int n = 0;
    for (const CardFile& f : files) n += f.found;
    return n;
  }
  uint32_t total() const {
    uint32_t t = 0;
    for (const CardFile& f : files)
      if (f.found) t += f.bytes;
    return t;
  }
  bool hasApp() const { return files[0].found; }
};

// A font pack starts with its format's four letters; anything else is not one.
inline bool isPackHead(const uint8_t* b, size_t n) {
  return n >= 4 && b[0] == 'T' && b[1] == 'F' && b[2] == 'P' && (b[3] == '1' || b[3] == '2');
}

// The Toybox marker, looked for across chunk borders, and the version that
// follows it up to its NUL -- what SlotWriter does on the way into flash,
// here for reading a file before anything is written.
struct MarkerScan {
  const char* marker;  // "\x7fTOYBOX-STICKY-APP\x7f"
  size_t len;
  size_t matched = 0;
  bool seen = false;
  int verLen = 0;
  char ver[VER_MAX] = {};
  MarkerScan(const char* m, size_t n) : marker(m), len(n) {}
  void feed(const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
      const char c = (char)b[i];
      if (seen) {
        if (verLen >= 0 && verLen < (int)sizeof(ver) - 1 && c) {
          ver[verLen++] = c;
          ver[verLen] = 0;
        } else {
          verLen = -1;
        }
        continue;
      }
      if (c == marker[matched]) {
        if (++matched == len) seen = true;
      } else {
        matched = (c == marker[0]) ? 1 : 0;
      }
    }
  }
  bool done() const { return seen && verLen < 0; }
};

// --- the device, as the settings page sees it --------------------------------------
// Implemented by the firmware (src/ota.cpp) and by the host harness's fake.
// A port without OTA returns nullptr from ToolsHost::otaDevice() and the page
// shows the version alone.

enum class Err : uint8_t {
  Ok,
  NoWifiSaved,   // send a home Wi-Fi from the phone first (Settings > Clock)
  WifiNotFound,
  WifiRefused,
  NoAnswer,      // joined, but the site did not answer
  NoUpdateFile,  // the site answered with something that is not a v2 version.json
  Download,      // the transfer stopped part-way
  Mismatch,      // the bytes arrived but are not the ones version.json describes
  NotToybox,     // an uploaded file that is not a Toybox build for this device
  Flash,         // the slot would not take it
  LowBattery,
  TooBig,
  LowMemory,
  WrongCode,     // the computer page: the six digits were wrong (or locked out)
};

inline const char* describe(Err e) {
  switch (e) {
    case Err::Ok: return "done";
    case Err::NoWifiSaved: return "no home wifi saved";
    case Err::WifiNotFound: return "home wifi not found";
    case Err::WifiRefused: return "wifi password refused";
    case Err::NoAnswer: return "the update site did not answer";
    case Err::NoUpdateFile: return "no update information there";
    case Err::Download: return "the download stopped";
    case Err::Mismatch: return "the file did not check out";
    case Err::NotToybox: return "that file is not Toybox";
    case Err::Flash: return "the update slot refused it";
    case Err::LowBattery: return "battery too low";
    case Err::TooBig: return "too big for the update slot";
    case Err::LowMemory: return "not enough memory free";
    case Err::WrongCode: return "wrong code";
  }
  return "";
}

struct Check {
  Remote remote;
  bool needApp = false;   // a newer build is on the site
  bool needBase = false;  // the CJK base pack differs from the site's
};

// stage 0 = the app, 1 = the base font pack; called often, the caller throttles.
using Progress = void (*)(void* ctx, uint8_t stage, uint32_t done, uint32_t total);

// The computer page: what the panel shows while it is open.
struct Lan {
  enum Phase : uint8_t { Off, Joining, Waiting, Receiving, Verifying, Done, Failed };
  Phase phase = Off;
  Err err = Err::Ok;
  char url[32] = {};      // http://toybox.local
  char ip[16] = {};       // and the address, for phones that cannot resolve .local
  char code[7] = {};
  char incoming[VER_MAX] = {};  // the version inside the file being sent, once seen
  uint32_t done = 0, total = 0;
};

class Device {
 public:
  virtual ~Device() = default;
  virtual const char* version() = 0;
  virtual const char* slot() = 0;          // "ota_0" / "ota_1"
  virtual uint32_t slotBytes() = 0;
  // Joins the home Wi-Fi, reads version.json, leaves again. Blocks ~5-30 s.
  virtual Err check(Check& out) = 0;
  // Joins, writes what `c` says is needed, leaves. On Ok the next boot runs
  // the new build (on trial); the caller says so and calls restart().
  virtual Err install(const Check& c, Progress p, void* ctx) = 0;
  virtual void restart() = 0;
  // The computer page on the home network. lanLoop() is pumped by the screen.
  virtual bool lanStart() = 0;
  virtual void lanLoop() = 0;
  virtual void lanStop() = 0;
  virtual const Lan& lan() const = 0;
  // Once after a rollback: the version that would not start. Clears it.
  virtual bool takeRollbackNotice(char* ver, size_t n) = 0;
  // FROM SD CARD: what is in /toybox/update/, read but not written. Blocks
  // about a second (the firmware is read through for its marker).
  virtual void cardScan(Card& out) { out = Card{}; }
  // Writes what cardScan found -- packs first, the firmware last -- then
  // renames each file to .done. Progress stages are indexes into files[].
  // On Ok with the firmware among them, the caller says so and restart()s.
  virtual Err cardInstall(const Card& c, Progress p, void* ctx) {
    (void)c, (void)p, (void)ctx;
    return Err::Flash;
  }
};

#ifdef TOYBOX_HOST
// The harness's stand-in, set by host_preview.cpp so every screen of the page
// can be drawn without a radio.
inline Device*& hostDevice() {
  static Device* d = nullptr;
  return d;
}
#endif

}  // namespace ota
