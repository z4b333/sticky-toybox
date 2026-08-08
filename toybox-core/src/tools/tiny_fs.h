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
  if (!begin()) return 0;
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

#endif

}  // namespace tfs
