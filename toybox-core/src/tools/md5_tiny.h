// MD5, small and the same on the device and on a PC.
//
// KOReader sync is built on it twice over: a book is named by the MD5 of a
// few samples of its bytes, and the account password travels as its MD5.
// The Arduino core has an MD5Builder, but the host harness does not, and the
// one thing these hashes must never do is differ between the code the harness
// checks and the code the device runs. RFC 1321, nothing clever.
#pragma once
#include <stdint.h>
#include <string.h>

namespace md5t {

class Md5 {
 public:
  Md5() { reset(); }

  void reset() {
    _a = 0x67452301u;
    _b = 0xefcdab89u;
    _c = 0x98badcfeu;
    _d = 0x10325476u;
    _len = 0;
    _bufN = 0;
  }

  void add(const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    _len += n;
    while (n > 0) {
      const size_t take = (64 - _bufN) < n ? (64 - _bufN) : n;
      memcpy(_buf + _bufN, p, take);
      _bufN += take;
      p += take;
      n -= take;
      if (_bufN == 64) {
        block(_buf);
        _bufN = 0;
      }
    }
  }

  void add(const char* s) { add(s, strlen(s)); }

  // 16 raw bytes. The object is spent afterwards; reset() to reuse it.
  void finish(uint8_t out[16]) {
    const uint64_t bits = _len * 8;
    const uint8_t pad = 0x80;
    add(&pad, 1);
    const uint8_t zero = 0;
    while (_bufN != 56) add(&zero, 1);
    uint8_t lenLe[8];
    for (int i = 0; i < 8; i++) lenLe[i] = (uint8_t)(bits >> (8 * i));
    add(lenLe, 8);
    const uint32_t v[4] = {_a, _b, _c, _d};
    for (int i = 0; i < 4; i++)
      for (int k = 0; k < 4; k++) out[i * 4 + k] = (uint8_t)(v[i] >> (8 * k));
  }

  // 32 lowercase hex characters and a NUL, as every KOReader server expects.
  void finishHex(char out[33]) {
    uint8_t d[16];
    finish(d);
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
      out[i * 2] = hx[d[i] >> 4];
      out[i * 2 + 1] = hx[d[i] & 15];
    }
    out[32] = 0;
  }

 private:
  static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

  void block(const uint8_t* p) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
        0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
        0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
        0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
        0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
        0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391};
    static const uint8_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
      m[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) | ((uint32_t)p[i * 4 + 2] << 16) |
             ((uint32_t)p[i * 4 + 3] << 24);
    uint32_t a = _a, b = _b, c = _c, d = _d;
    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) & 15;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) & 15;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) & 15;
      }
      const uint32_t t = d;
      d = c;
      c = b;
      b = b + rol(a + f + K[i] + m[g], S[i]);
      a = t;
    }
    _a += a;
    _b += b;
    _c += c;
    _d += d;
  }

  uint32_t _a, _b, _c, _d;
  uint64_t _len;
  uint8_t _buf[64];
  size_t _bufN;
};

inline void hex(const char* s, char out[33]) {
  Md5 m;
  m.add(s);
  m.finishHex(out);
}

}  // namespace md5t
