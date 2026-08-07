// Device-to-device messaging over ESP-NOW.
//
// Namespace `radio` rather than `link`: POSIX declares a global link() and the
// two collide the moment <unistd.h> is in scope, which on the device it always is.
//
// ESP-NOW is connectionless WiFi: no router, no access point, no pairing
// ceremony — you send a frame to a MAC address and it arrives. That fits a game
// between two people in a room far better than one device raising an AP and the
// other joining it, and it leaves the phone-pairing portal free for what it is
// already used for.
//
// Messages here are tiny by design: a Battleship turn is two bytes. The link
// deliberately knows nothing about delivery guarantees — retries live in the
// game, which is the only layer that knows what a lost message means.
//
// On the host build the same class connects instances to each other in-process,
// so two players can be simulated and the protocol tested without hardware.
#pragma once
#include <Arduino.h>
#include <string.h>

#ifndef TOYBOX_HOST
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

namespace radio {

constexpr int MAC_LEN = 6;
constexpr int MAX_PAYLOAD = 32;
constexpr uint8_t MAGIC0 = 'T', MAGIC1 = 'B';
constexpr uint8_t VERSION = 1;
constexpr uint8_t CHANNEL = 1;  // both sides must agree; ESP-NOW does not scan

struct Msg {
  uint8_t from[MAC_LEN];
  uint8_t type;
  uint8_t len;
  uint8_t data[MAX_PAYLOAD];
};

inline bool sameMac(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, MAC_LEN) == 0;
}

// A short human-checkable code derived from a MAC. Two devices in the same room
// need some way to confirm they joined each other and not the pair at the next
// table; four digits is enough for that and fits a headline.
inline uint16_t codeOf(const uint8_t* mac) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < MAC_LEN; i++) {
    h ^= mac[i];
    h *= 16777619u;
  }
  return (uint16_t)(h % 10000u);
}

namespace detail {
// Wire format: magic, version, type, length, payload.
constexpr int HEADER = 4;
inline int encode(uint8_t* out, uint8_t type, const void* data, uint8_t len) {
  if (len > MAX_PAYLOAD) return 0;
  out[0] = MAGIC0;
  out[1] = MAGIC1;
  out[2] = (uint8_t)((VERSION << 4) | (type & 0x0F));
  out[3] = len;
  if (len) memcpy(out + HEADER, data, len);
  return HEADER + len;
}
inline bool decode(const uint8_t* in, int n, Msg& m) {
  if (n < HEADER || in[0] != MAGIC0 || in[1] != MAGIC1) return false;
  if ((in[2] >> 4) != VERSION) return false;
  const uint8_t len = in[3];
  if (len > MAX_PAYLOAD || HEADER + len > n) return false;
  m.type = in[2] & 0x0F;
  m.len = len;
  if (len) memcpy(m.data, in + HEADER, len);
  return true;
}
}  // namespace detail

#ifdef TOYBOX_HOST

// Host build: instances find each other through a shared registry, so a test can
// stand up two "devices" and play them against one another.
class Link {
 public:
  ~Link() { end(); }

  bool begin() {
    if (_up) return true;
    Link** slots = registry();
    for (int i = 0; i < kMax; i++) {
      if (slots[i]) continue;
      slots[i] = this;
      memset(_mac, 0, MAC_LEN);
      _mac[0] = 0x02;              // locally administered, like a real fake MAC
      _mac[5] = (uint8_t)(i + 1);  // distinct per simulated device
      _up = true;
      _qn = 0;
      return true;
    }
    return false;
  }

  void end() {
    Link** slots = registry();
    for (int i = 0; i < kMax; i++)
      if (slots[i] == this) slots[i] = nullptr;
    _up = false;
    _qn = 0;
  }

  bool up() const { return _up; }
  const uint8_t* mac() const { return _mac; }
  uint16_t code() const { return codeOf(_mac); }

  bool broadcast(uint8_t type, const void* d, uint8_t n) { return sendRaw(nullptr, type, d, n); }
  bool sendTo(const uint8_t* peer, uint8_t type, const void* d, uint8_t n) {
    return sendRaw(peer, type, d, n);
  }

  bool poll(Msg& out) {
    if (!_up || _qn == 0) return false;
    out = _q[0];
    for (int i = 1; i < _qn; i++) _q[i - 1] = _q[i];
    _qn--;
    return true;
  }

  // Drops the next `n` messages this link would have sent. Lets a test make the
  // radio lose packets on purpose and check that the game recovers.
  void dropNextSends(int n) { _drop = n; }

 private:
  static constexpr int kMax = 4;
  static constexpr int kQueue = 8;
  static Link** registry() {
    static Link* slots[kMax] = {};
    return slots;
  }

  bool sendRaw(const uint8_t* peer, uint8_t type, const void* d, uint8_t n) {
    if (!_up || n > MAX_PAYLOAD) return false;
    if (_drop > 0) {
      _drop--;
      return true;  // the radio reported success; the frame never landed
    }
    Link** slots = registry();
    for (int i = 0; i < kMax; i++) {
      Link* other = slots[i];
      if (!other || other == this) continue;
      if (peer && !sameMac(other->_mac, peer)) continue;
      other->deliver(_mac, type, d, n);
    }
    return true;
  }

  void deliver(const uint8_t* from, uint8_t type, const void* d, uint8_t n) {
    if (_qn >= kQueue) return;
    Msg& m = _q[_qn++];
    memcpy(m.from, from, MAC_LEN);
    m.type = type;
    m.len = n;
    if (n) memcpy(m.data, d, n);
  }

  bool _up = false;
  uint8_t _mac[MAC_LEN] = {};
  Msg _q[kQueue];
  int _qn = 0, _drop = 0;
};

#else

class Link {
 public:
  ~Link() { end(); }

  bool begin() {
    if (_up) return true;
    // ESP-NOW does not scan, so both devices must sit on one agreed channel.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    WiFi.setSleep(false);
    if (esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    if (esp_now_init() != ESP_OK) {
      WiFi.mode(WIFI_OFF);
      return false;
    }
    WiFi.macAddress(_mac);
    _self = this;
    _head = _tail = 0;
    if (esp_now_register_recv_cb(onRecv) != ESP_OK) {
      esp_now_deinit();
      WiFi.mode(WIFI_OFF);
      return false;
    }
    addPeer(kBroadcast);
    _up = true;
    return true;
  }

  void end() {
    if (!_up) return;
    _up = false;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    _self = nullptr;
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);  // the radio is the biggest draw on the board
  }

  bool up() const { return _up; }
  const uint8_t* mac() const { return _mac; }
  uint16_t code() const { return codeOf(_mac); }

  bool broadcast(uint8_t type, const void* d, uint8_t n) { return sendRaw(kBroadcast, type, d, n); }
  bool sendTo(const uint8_t* peer, uint8_t type, const void* d, uint8_t n) {
    return sendRaw(peer, type, d, n);
  }

  // Single producer (the WiFi task) and single consumer (the app loop), so the
  // ring needs no lock: each side only ever advances its own index.
  bool poll(Msg& out) {
    if (!_up || _head == _tail) return false;
    out = _q[_tail];
    _tail = (uint8_t)((_tail + 1) % kQueue);
    return true;
  }

  void dropNextSends(int) {}  // test hook; nothing to do on real hardware

 private:
  static constexpr int kQueue = 8;
  static inline const uint8_t kBroadcast[MAC_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  static void addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t p{};
    memcpy(p.peer_addr, mac, MAC_LEN);
    p.channel = CHANNEL;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    esp_now_add_peer(&p);
  }

  bool sendRaw(const uint8_t* peer, uint8_t type, const void* d, uint8_t n) {
    if (!_up) return false;
    uint8_t buf[detail::HEADER + MAX_PAYLOAD];
    const int len = detail::encode(buf, type, d, n);
    if (!len) return false;
    addPeer(peer);
    return esp_now_send(peer, buf, (size_t)len) == ESP_OK;
  }

  static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    Link* self = _self;
    if (!self || !info || !self->_up) return;
    const uint8_t next = (uint8_t)((self->_head + 1) % kQueue);
    if (next == self->_tail) return;  // full: drop, the game will retry
    Msg& m = self->_q[self->_head];
    if (!detail::decode(data, len, m)) return;
    memcpy(m.from, info->src_addr, MAC_LEN);
    self->_head = next;
  }

  static inline Link* _self = nullptr;
  bool _up = false;
  uint8_t _mac[MAC_LEN] = {};
  Msg _q[kQueue];
  volatile uint8_t _head = 0, _tail = 0;
};

#endif  // TOYBOX_HOST

}  // namespace radio
