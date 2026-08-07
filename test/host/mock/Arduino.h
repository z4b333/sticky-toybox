// Minimal Arduino mock for host-side preview rendering.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using std::max;
using std::min;

inline unsigned long g_millis = 100000;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long ms) { g_millis += ms; }
template <typename T>
T constrain(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }

// Just enough of Arduino's String for the flashcard storage layer.
#include <string>
class String {
 public:
  String() = default;
  String(const char* s) : _s(s ? s : "") {}
  String& operator=(const char* s) {
    _s = s ? s : "";
    return *this;
  }
  String& operator+=(const char* s) {
    _s += s;
    return *this;
  }
  String& operator+=(char c) {
    _s += c;
    return *this;
  }
  String& operator+=(int v) {
    _s += std::to_string(v);
    return *this;
  }
  String operator+(const char* s) const { return String((_s + s).c_str()); }
  friend String operator+(const String& a, const String& b) {
    return String((a._s + b._s).c_str());
  }
  unsigned length() const { return (unsigned)_s.size(); }
  const char* c_str() const { return _s.c_str(); }
  char operator[](unsigned i) const { return i < _s.size() ? _s[i] : 0; }
  void reserve(size_t n) { _s.reserve(n); }

 private:
  std::string _s;
};
