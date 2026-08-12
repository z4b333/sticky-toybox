// The one file seam the tools share.
//
// On device this is the 3.4 MB LittleFS partition; the host preview build swaps
// in an in-memory map so every screen can be rendered without hardware.
#pragma once
#include <Arduino.h>

#ifdef TOYBOX_HOST
#include <map>
#include <string>
#else
#include <LittleFS.h>
#endif

namespace tfs {

#ifdef TOYBOX_HOST

inline std::map<std::string, std::string>& hostFs() {
  static std::map<std::string, std::string> fs;
  return fs;
}
inline bool begin() { return true; }
inline bool read(const char* path, String& out) {
  auto it = hostFs().find(path);
  if (it == hostFs().end()) return false;
  out = it->second.c_str();
  return true;
}
inline bool write(const char* path, const char* data, size_t len) {
  hostFs()[path] = std::string(data, len);
  return true;
}
inline bool remove(const char* path) { return hostFs().erase(path) > 0; }
inline std::string& appendPath() {
  static std::string p;
  return p;
}
inline bool appendOpen(const char* path) {
  appendPath() = path;
  hostFs()[path].clear();
  return true;
}
inline bool appendChunk(const void* data, size_t n) {
  if (appendPath().empty()) return false;
  hostFs()[appendPath()].append((const char*)data, n);
  return true;
}
inline bool appendClose() {
  if (appendPath().empty()) return false;
  appendPath().clear();
  return true;
}
inline bool exists(const char* path) { return hostFs().count(path) > 0; }
inline bool rename(const char* from, const char* to) {
  auto it = hostFs().find(from);
  if (it == hostFs().end()) return false;
  hostFs()[to] = it->second;
  hostFs().erase(it);
  return true;
}
inline size_t size(const char* path) {
  auto it = hostFs().find(path);
  return it == hostFs().end() ? 0 : it->second.size();
}

// Reads a whole file into a fresh buffer the caller frees. For the multi-
// megabyte font packs, which must not pass through String.
inline char* readAlloc(const char* path, size_t& len) {
  auto it = hostFs().find(path);
  if (it == hostFs().end()) return nullptr;
  len = it->second.size();
  char* buf = (char*)malloc(len);
  if (buf) memcpy(buf, it->second.data(), len);
  return buf;
}

// Lists base names (no directory, no extension) of files in `dir` ending in
// `ext`. `stride` is the row size of the caller's name array.
inline int list(const char* dir, const char* ext, char* names, int stride, int maxNames,
                int nameLen) {
  const std::string prefix = std::string(dir) + "/";
  const std::string suffix = ext;
  int n = 0;
  for (auto& kv : hostFs()) {
    const std::string& p = kv.first;
    if (p.rfind(prefix, 0) != 0) continue;
    if (p.size() <= prefix.size() + suffix.size()) continue;
    if (p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
    if (n >= maxNames) break;
    const std::string base =
        p.substr(prefix.size(), p.size() - prefix.size() - suffix.size());
    char* slot = names + (size_t)n * stride;
    strncpy(slot, base.c_str(), nameLen);
    slot[nameLen] = 0;
    n++;
  }
  return n;
}

inline void ensureDir(const char*) {}

// A file written a piece at a time, for images too big to hold whole. The
// host build just gathers the pieces, which is all a preview needs.
class Out {
 public:
  bool begin(const char* path) {
    _path = path;
    _buf.clear();
    _open = true;
    return true;
  }
  bool write(const void* d, size_t n) {
    if (!_open) return false;
    _buf.append((const char*)d, n);
    return true;
  }
  bool end() {
    if (!_open) return false;
    hostFs()[_path] = _buf;
    _open = false;
    return true;
  }
  void abort() {
    _open = false;
    _buf.clear();
  }

 private:
  std::string _path, _buf;
  bool _open = false;
};

#else

inline bool begin() {
  static bool mounted = false;
  if (mounted) return true;
  mounted = LittleFS.begin(true);  // format on first boot
  return mounted;
}
inline void ensureDir(const char* dir) {
  if (begin() && !LittleFS.exists(dir)) LittleFS.mkdir(dir);
}
inline bool read(const char* path, String& out) {
  if (!begin()) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}
inline bool write(const char* path, const char* data, size_t len) {
  if (!begin()) return false;
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.write((const uint8_t*)data, len);
  f.close();
  return true;
}
inline bool remove(const char* path) {
  if (!begin()) return false;
  return LittleFS.remove(path);
}

// A file written in pieces. The lock screen's cover copy is 48,008 bytes and
// used to want all of them in RAM at once to hand to write() -- a transient
// allocation that big is how a heap gets chopped up, and on this device the
// chopping is what stops books opening. Open, feed, close.
inline File g_appendFile;
inline bool appendOpen(const char* path) {
  if (!begin()) return false;
  if (g_appendFile) g_appendFile.close();
  g_appendFile = LittleFS.open(path, "w");
  return (bool)g_appendFile;
}
inline bool appendChunk(const void* data, size_t n) {
  if (!g_appendFile) return false;
  return g_appendFile.write((const uint8_t*)data, n) == n;
}
inline bool appendClose() {
  if (!g_appendFile) return false;
  g_appendFile.close();
  return true;
}
inline bool rename(const char* from, const char* to) {
  if (!begin()) return false;
  LittleFS.remove(to);  // LittleFS will not rename over an existing name
  return LittleFS.rename(from, to);
}

// Asked before opening, wherever a file is expected to be missing.
//
// LittleFS logs a red `open(): ... does not exist, no permits for creation` at
// error level every time a read fails, and a normal first boot produced three
// of them -- the fonts directory, the pinned-note marker, and nothing else.
// They are the most alarming thing in a healthy log and they all mean "this
// device has no notes yet". Checking first costs a directory lookup and makes
// the log say only true things.
inline bool exists(const char* path) { return begin() && LittleFS.exists(path); }

// Length without reading the contents -- the lock screen picture is 48 KB and
// only ever needs its size checked before a settings label is drawn.
inline size_t size(const char* path) {
  if (!begin()) return 0;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

// Reads a whole file into one allocation the caller frees. Prefers PSRAM: a
// font pack is megabytes, and the 8 MB PSRAM is otherwise idle, while internal
// RAM is what WiFi and the framebuffer live on.
inline char* readAlloc(const char* path, size_t& len) {
  if (!begin()) return nullptr;
  File f = LittleFS.open(path, "r");
  if (!f) return nullptr;
  len = f.size();
  char* buf = (char*)ps_malloc(len);
  if (!buf) buf = (char*)malloc(len);
  if (buf && f.read((uint8_t*)buf, len) != len) {
    free(buf);
    buf = nullptr;
  }
  f.close();
  return buf;
}

inline int list(const char* dir, const char* ext, char* names, int stride, int maxNames,
                int nameLen) {
  if (!begin() || !LittleFS.exists(dir)) return 0;
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) return 0;
  int n = 0;
  for (File f = d.openNextFile(); f && n < maxNames; f = d.openNextFile()) {
    String fn = f.name();
    const int slash = fn.lastIndexOf('/');
    if (slash >= 0) fn = fn.substring(slash + 1);
    if (!fn.endsWith(ext)) continue;
    fn = fn.substring(0, fn.length() - strlen(ext));
    char* slot = names + (size_t)n * stride;
    strncpy(slot, fn.c_str(), nameLen);
    slot[nameLen] = 0;
    n++;
  }
  return n;
}

// A file written a piece at a time. A 48 KB cover would otherwise have to be
// assembled in RAM before it could be saved, and during a book's first open
// that RAM is already carrying a zip window and an image decoder.
class Out {
 public:
  bool begin(const char* path) {
    if (!tfs::begin()) return false;
    strncpy(_path, path, sizeof(_path) - 1);
    _path[sizeof(_path) - 1] = 0;
    _f = LittleFS.open(_path, "w");
    _ok = (bool)_f;
    return _ok;
  }
  bool write(const void* d, size_t n) {
    if (!_ok) return false;
    if (_f.write((const uint8_t*)d, n) != n) _ok = false;
    return _ok;
  }
  bool end() {
    if (!_f) return false;
    _f.close();
    if (!_ok) LittleFS.remove(_path);  // a torn file is worse than none
    return _ok;
  }
  void abort() {
    if (_f) _f.close();
    LittleFS.remove(_path);
    _ok = false;
  }

 private:
  File _f;
  char _path[32] = {};
  bool _ok = false;
};

#endif

}  // namespace tfs
