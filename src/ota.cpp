// See ota.h. Nothing here is compiled into the host harness: the harness has
// its own fake ota::Device, and the decisions this file leans on are tested in
// test/host/ota_test.cpp.
#ifndef TOYBOX_HOST
#include "ota.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "sdcard.h"
#include "sensors.h"
#include "tools/home_net.h"

#ifndef TB_VERSION
#define TB_VERSION "unknown"
#endif

// Every Toybox image for the Sticky carries this, version straight after it.
// An uploaded file without it is refused before it can become the boot slot:
// a CrossPoint image, a Toybox build for another board, or a random .bin
// would otherwise boot-loop until the rollback caught it.
extern "C" __attribute__((used)) const char toybox_app_marker[] =
    "\x7fTOYBOX-STICKY-APP\x7f" TB_VERSION;
static constexpr size_t MARKER_LEN = 19;  // the bytes up to and including the second \x7f

// The certificate bundle ESP-IDF links in (as kosync.h uses it).
extern "C" const uint8_t ota_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t ota_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace otadev {
namespace {

using ota::Err;

constexpr size_t CHUNK = 4096;

void hex(const uint8_t* d, size_t n, char* out) {
  static const char* k = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = k[d[i] >> 4];
    out[i * 2 + 1] = k[d[i] & 15];
  }
  out[n * 2] = 0;
}

Err fromNet(homenet::Result r) {
  switch (r) {
    case homenet::Result::Ok: return Err::Ok;
    case homenet::Result::NoWifiSaved: return Err::NoWifiSaved;
    case homenet::Result::Refused: return Err::WifiRefused;
    default: return Err::WifiNotFound;
  }
}

// --- where the bytes go -----------------------------------------------------------

class Sink {
 public:
  virtual ~Sink() = default;
  virtual Err write(const uint8_t* b, size_t n) = 0;
};

// The slot that is not running. OTA_WITH_SEQUENTIAL_WRITES erases as it goes,
// so the first byte lands at once instead of after a 3 MB erase.
class SlotWriter : public Sink {
 public:
  ~SlotWriter() override { abort(); }

  Err begin(uint32_t expectBytes) {
    abort();
    _part = esp_ota_get_next_update_partition(nullptr);
    if (!_part) return Err::Flash;
    if (expectBytes > _part->size) return Err::TooBig;
    if (esp_ota_begin(_part, OTA_WITH_SEQUENTIAL_WRITES, &_h) != ESP_OK) return Err::Flash;
    _open = true;
    _done = 0;
    _matched = 0;
    _seen = false;
    _verLen = 0;
    _ver[0] = 0;
    mbedtls_sha256_init(&_sha);
    mbedtls_sha256_starts(&_sha, 0);
    return Err::Ok;
  }

  Err write(const uint8_t* b, size_t n) override {
    if (!_open) return Err::Flash;
    if (_done + n > _part->size) return Err::TooBig;
    if (esp_ota_write(_h, b, n) != ESP_OK) return Err::Flash;
    mbedtls_sha256_update(&_sha, b, n);
    scan(b, n);
    _done += n;
    return Err::Ok;
  }

  // expectSha null: an upload, judged by the marker and the image checksum.
  Err finish(const char* expectSha) {
    if (!_open) return Err::Flash;
    uint8_t d[32];
    char got[65];
    mbedtls_sha256_finish(&_sha, d);
    mbedtls_sha256_free(&_sha);
    hex(d, 32, got);
    if (expectSha && strcmp(expectSha, got) != 0) {
      abort();
      return Err::Mismatch;
    }
    if (!_seen) {
      abort();
      return Err::NotToybox;
    }
    _open = false;
    // Checks the image header, every segment and the image's own SHA-256.
    if (esp_ota_end(_h) != ESP_OK) return Err::Mismatch;
    return Err::Ok;
  }

  void abort() {
    if (_open) {
      esp_ota_abort(_h);
      mbedtls_sha256_free(&_sha);
    }
    _open = false;
  }

  // Makes the written slot the boot slot, on trial.
  Err activate(Preferences& p) {
    if (!_part || esp_ota_set_boot_partition(_part) != ESP_OK) return Err::Flash;
    const esp_partition_t* run = esp_ota_get_running_partition();
    p.putString(ota::K_TRIAL, _part->label);
    p.putString(ota::K_PREV, run ? run->label : "");
    p.putInt(ota::K_TRIES, 0);
    // The version read out of the image itself, for the notice if it fails.
    p.putString(ota::K_TVER, _ver[0] ? _ver : _part->label);
    return Err::Ok;
  }

  uint32_t done() const { return _done; }
  const char* version() const { return _ver; }

 private:
  // The marker's only border is its one-byte \x7f, so restarting the match on
  // a mismatching \x7f is exact. After the marker, the version up to its NUL.
  void scan(const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
      const char c = (char)b[i];
      if (_seen) {
        if (_verLen >= 0 && _verLen < (int)sizeof(_ver) - 1 && c) {
          _ver[_verLen++] = c;
          _ver[_verLen] = 0;
        } else {
          _verLen = -1;  // done with the version
        }
        continue;
      }
      if (c == toybox_app_marker[_matched]) {
        if (++_matched == MARKER_LEN) _seen = true;
      } else {
        _matched = (c == toybox_app_marker[0]) ? 1 : 0;
      }
    }
  }

  const esp_partition_t* _part = nullptr;
  esp_ota_handle_t _h = 0;
  bool _open = false;
  uint32_t _done = 0;
  mbedtls_sha256_context _sha;
  size_t _matched = 0;
  bool _seen = false;
  int _verLen = 0;
  char _ver[ota::VER_MAX] = {};
};

// The base font partition: erased for the length it needs, written in order,
// read back and hashed before it is believed. gfx has it memory-mapped -- the
// settings page draws in Latin while this runs, and Latin never looks there.
class PartWriter : public Sink {
 public:
  Err begin(const char* label, uint32_t bytes) {
    _part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!_part) return Err::Flash;
    if (bytes > _part->size) return Err::TooBig;
    _bytes = bytes;
    _done = 0;
    const uint32_t span = (bytes + 4095u) & ~4095u;
    if (esp_partition_erase_range(_part, 0, span) != ESP_OK) return Err::Flash;
    return Err::Ok;
  }
  Err write(const uint8_t* b, size_t n) override {
    if (_done + n > _bytes) return Err::TooBig;
    if (esp_partition_write(_part, _done, b, n) != ESP_OK) return Err::Flash;
    _done += n;
    return Err::Ok;
  }
  uint32_t done() const { return _done; }
  const esp_partition_t* part() const { return _part; }

 private:
  const esp_partition_t* _part = nullptr;
  uint32_t _bytes = 0, _done = 0;
};

// SHA-256 of the first `bytes` of a partition, read back from flash.
bool partSha(const esp_partition_t* part, uint32_t bytes, char out[65]) {
  if (!part || bytes > part->size) return false;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
  if (!buf) return false;
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  bool ok = true;
  for (uint32_t at = 0; at < bytes && ok; at += CHUNK) {
    const size_t n = bytes - at < CHUNK ? bytes - at : CHUNK;
    ok = esp_partition_read(part, at, buf, n) == ESP_OK;
    if (ok) mbedtls_sha256_update(&c, buf, n);
  }
  uint8_t d[32];
  mbedtls_sha256_finish(&c, d);
  mbedtls_sha256_free(&c);
  free(buf);
  if (ok) hex(d, 32, out);
  return ok;
}

// --- the network ------------------------------------------------------------------

bool lowInternal() {
  // A TLS handshake wants ~40 KB of internal RAM at once (see kosync.h).
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 24000;
}

void sourceUrl(Preferences& p, const char* path, char* out, size_t n) {
  char src[160] = {};
  if (p.isKey(ota::K_SRC)) p.getString(ota::K_SRC, src, sizeof(src));
  if (strncmp(src, "https://", 8) != 0) snprintf(src, sizeof(src), "%s", ota::DEFAULT_SRC);
  const size_t L = strlen(src);
  snprintf(out, n, "%s%s%s", src, (L && src[L - 1] == '/') ? "" : "/", path);
}

// GETs `url` into `sink`, which must take exactly `bytes` (or, if the server
// does not say how long it is, whatever arrives -- the hash decides).
Err fetch(const char* url, uint32_t bytes, Sink& sink, ota::Progress prog, void* ctx,
          uint8_t stage) {
  if (lowInternal()) return Err::LowMemory;
  NetworkClientSecure sec;
  sec.setCACertBundle(ota_bundle_start, ota_bundle_end - ota_bundle_start);
  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  if (!http.begin(sec, url)) return Err::NoAnswer;
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return code > 0 ? Err::NoUpdateFile : Err::NoAnswer;
  }
  const int size = http.getSize();
  if (size >= 0 && (uint32_t)size != bytes) {
    http.end();
    return Err::Mismatch;
  }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
  if (!buf) {
    http.end();
    return Err::LowMemory;
  }
  NetworkClient* s = http.getStreamPtr();
  uint32_t got = 0, lastData = millis();
  Err err = Err::Ok;
  while (got < bytes) {
    const int avail = s->available();
    if (avail <= 0) {
      if (!s->connected() || millis() - lastData > 20000) {
        err = Err::Download;
        break;
      }
      delay(2);
      continue;
    }
    size_t want = (size_t)avail;
    if (want > CHUNK) want = CHUNK;
    if (want > bytes - got) want = bytes - got;
    const int n = s->readBytes(buf, want);
    if (n <= 0) continue;
    lastData = millis();
    err = sink.write(buf, (size_t)n);
    if (err != Err::Ok) break;
    got += (uint32_t)n;
    if (prog) prog(ctx, stage, got, bytes);
  }
  free(buf);
  http.end();
  return err;
}

// --- the computer page -------------------------------------------------------------
// Served from flash, in the web kit's look. Everything it needs is inline: the
// computer may be on a network with no internet at all.
const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Update your Sticky</title>
<style>
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:#F5F1E6;background-image:radial-gradient(#D9D2BF 1.1px,transparent 1.3px);background-size:16px 16px;color:#111;font:500 17px/1.55 system-ui,-apple-system,"Segoe UI",sans-serif}
header{background:#fff;border-bottom:2px solid #111;padding:10px 16px;display:flex;align-items:center;gap:12px}
.tile{width:40px;height:40px;flex:none;background:#FFD84D;border:2px solid #111;border-radius:9px;box-shadow:2px 2px 0 #111;display:flex;align-items:center;justify-content:center}
.lbl{font:600 11px/1.4 ui-monospace,Menlo,Consolas,monospace;letter-spacing:1.5px}
h1{margin:0;font-size:19px;font-weight:800}
main{max-width:560px;margin:0 auto;padding:22px 16px 40px;display:flex;flex-direction:column;gap:18px}
.card{background:#fff;border:2px solid #111;box-shadow:4px 4px 0 #111;padding:16px;display:flex;flex-direction:column;gap:12px}
label{font-weight:700;font-size:15px}
input[type=text]{height:52px;border:2px solid #111;padding:0 12px;font:700 26px ui-monospace,Menlo,Consolas,monospace;letter-spacing:8px;width:100%}
input[type=file]{font-size:15px}
button{height:52px;border:2px solid #111;box-shadow:3px 3px 0 #111;background:#FFD84D;color:#111;font:700 17px system-ui,sans-serif;cursor:pointer}
button:disabled{background:#F5F1E6;border-style:dashed;box-shadow:none;color:#4D483D}
.bar{height:18px;border:2px solid #111;background:#fff}
.bar i{display:block;height:100%;width:0;background:repeating-linear-gradient(-45deg,#111 0 5px,#FFD84D 5px 10px)}
.warn{border:2px solid #111;background:#fff}
.warn b.s{display:block;height:10px;border-bottom:2px solid #111;background:repeating-linear-gradient(-45deg,#111 0 6px,#FFD84D 6px 12px)}
.warn p{margin:0;padding:10px 12px;font-size:15px}
.msg{font-size:15px;min-height:1.5em}
.mono{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px}
</style></head><body>
<header><span class="tile"><svg width="30" height="30" viewBox="0 0 100 100" aria-hidden="true"><path d="M48 6Q48 20 62 20Q48 20 48 34Q48 20 34 20Q48 20 48 6ZM73 19.5Q73 30 83.5 30Q73 30 73 40.5Q73 30 62.5 30Q73 30 73 19.5ZM24 21.5Q24 30 32.5 30Q24 30 24 38.5Q24 30 15.5 30Q24 30 24 21.5Z" fill="#111"/><path d="M12 46H84L78 90H18Z" fill="#111" stroke="#111" stroke-width="5" stroke-linejoin="round"/><path d="M12 46L3 32M84 46L93 32" stroke="#111" stroke-width="5" stroke-linecap="round"/><g transform="translate(47 69) scale(0.78) translate(-48 -71)" fill="none" stroke="#FFD84D" stroke-width="5" stroke-linecap="square" stroke-linejoin="round"><path d="M25 58H45M35 58V84M53 58V84M53 58H62.5a6 6 0 0 1 0 12H53M53 70H64a7 7 0 0 1 0 14H53"/></g></svg></span>
<div><div class="lbl">SERVED BY YOUR STICKY</div><h1>Update the firmware</h1></div></header>
<main>
<div class="card"><span class="lbl">ON THE DEVICE NOW</span><span class="mono">%VER%</span></div>
<div class="card" id="step1"><label for="code">The six digits on the Sticky's screen</label>
<input id="code" type="text" inputmode="numeric" autocomplete="one-time-code" maxlength="6" pattern="[0-9]{6}">
<button id="go1">Pair</button><div class="msg" id="m1" role="status"></div></div>
<div class="card" id="step2" hidden><label for="file">A Toybox firmware file (toybox-app.bin)</label>
<input id="file" type="file" accept=".bin,application/octet-stream">
<div class="warn"><b class="s"></b><p>Keep the Sticky awake and nearby until it restarts. Your notes, books and settings stay.</p></div>
<button id="go2" disabled>Send to the Sticky</button>
<div class="bar"><i id="bar"></i></div><div class="msg" id="m2" role="status"></div></div>
</main>
<script>
var code='';
function $(i){return document.getElementById(i)}
$('go1').onclick=function(){
 code=$('code').value.trim();
 fetch('/p?c='+encodeURIComponent(code),{method:'POST'}).then(function(r){return r.json()}).then(function(j){
  if(j.ok){$('step1').hidden=true;$('step2').hidden=false;}
  else $('m1').textContent=j.locked?'Too many tries. Open the page again on the Sticky.':'That is not the code on the screen.';
 }).catch(function(){$('m1').textContent='The Sticky did not answer.'});
};
$('file').onchange=function(){$('go2').disabled=!this.files.length};
$('go2').onclick=function(){
 var f=$('file').files[0];if(!f)return;
 $('go2').disabled=true;$('m2').textContent='Sending '+f.name+'...';
 var fd=new FormData();fd.append('f',f,f.name);
 var x=new XMLHttpRequest();x.open('POST','/u?c='+encodeURIComponent(code));
 x.upload.onprogress=function(e){if(e.lengthComputable)$('bar').style.width=(100*e.loaded/e.total)+'%'};
 x.onload=function(){var j={};try{j=JSON.parse(x.responseText)}catch(e){}
  if(j.ok){$('bar').style.width='100%';$('m2').textContent='Checked and installed ('+j.version+'). The Sticky is restarting.';}
  else{$('m2').textContent='Not installed: '+(j.err||'no answer')+'.';$('go2').disabled=false;}};
 x.onerror=function(){$('m2').textContent='The connection dropped. Nothing was changed; try again.';$('go2').disabled=false};
 x.send(fd);
};
</script></body></html>)HTML";

class StickyOta : public ota::Device {
 public:
  explicit StickyOta(Preferences& p) : _p(p) {}

  const char* version() override { return TB_VERSION; }
  const char* slot() override {
    const esp_partition_t* r = esp_ota_get_running_partition();
    return r ? r->label : "";
  }
  uint32_t slotBytes() override {
    const esp_partition_t* n = esp_ota_get_next_update_partition(nullptr);
    return n ? n->size : 0;
  }

  Err check(ota::Check& out) override {
    out = ota::Check{};
    const homenet::Result j = homenet::connect(_p);
    if (j != homenet::Result::Ok) return fromNet(j);
    Err err = Err::Ok;
    {
      char url[200];
      sourceUrl(_p, "firmware/version.json", url, sizeof(url));
      if (lowInternal()) {
        err = Err::LowMemory;
      } else {
        NetworkClientSecure sec;
        sec.setCACertBundle(ota_bundle_start, ota_bundle_end - ota_bundle_start);
        HTTPClient http;
        http.setTimeout(15000);
        http.setConnectTimeout(15000);
        // The site is a CDN: a cache-buster, as the flasher page uses.
        String full = String(url) + "?t=" + String((uint32_t)esp_random());
        if (!http.begin(sec, full)) {
          err = Err::NoAnswer;
        } else {
          const int code = http.GET();
          if (code == 200) {
            String body = http.getString();
            if (body.length() == 0 || body.length() > 8192 ||
                !ota::parseRemote(body.c_str(), body.length(), out.remote))
              err = Err::NoUpdateFile;
          } else {
            err = code > 0 ? Err::NoUpdateFile : Err::NoAnswer;
          }
          http.end();
        }
      }
    }
    homenet::off();
    if (err != Err::Ok) return err;
    out.needApp = ota::isNewer(out.remote.version, version());
    out.needBase = out.remote.base.present() && baseDiffers(out.remote.base);
    return Err::Ok;
  }

  Err install(const ota::Check& c, ota::Progress prog, void* ctx) override {
    if (!c.needApp && !c.needBase) return Err::Ok;
    const ota::Gate g = ota::mayStart(sensors::batteryPercent(), sensors::charging(),
                                      c.remote.minBattery, c.needApp ? c.remote.app.bytes : 0,
                                      slotBytes());
    if (g == ota::Gate::LowBattery) return Err::LowBattery;
    if (g == ota::Gate::TooBig) return Err::TooBig;
    const homenet::Result j = homenet::connect(_p);
    if (j != homenet::Result::Ok) return fromNet(j);
    char url[200];
    Err err = Err::Ok;
    // The base pack first. Every v2 build reads the same format, so the
    // running app is happy with the new one; if this fails nothing has been
    // switched, and the next check offers it again.
    if (c.needBase) {
      _p.remove(ota::K_BASE);
      PartWriter pw;
      err = pw.begin("base_font", c.remote.base.bytes);
      if (err == Err::Ok) {
        sourceUrl(_p, c.remote.base.path, url, sizeof(url));
        err = fetch(url, c.remote.base.bytes, pw, prog, ctx, 1);
      }
      if (err == Err::Ok && pw.done() != c.remote.base.bytes) err = Err::Download;
      char got[65];
      if (err == Err::Ok && !(partSha(pw.part(), c.remote.base.bytes, got) &&
                              strcmp(got, c.remote.base.sha256) == 0))
        err = Err::Mismatch;
      if (err == Err::Ok) _p.putString(ota::K_BASE, c.remote.base.sha256);
    }
    if (err == Err::Ok && c.needApp) {
      SlotWriter w;
      err = w.begin(c.remote.app.bytes);
      if (err == Err::Ok) {
        sourceUrl(_p, c.remote.app.path, url, sizeof(url));
        err = fetch(url, c.remote.app.bytes, w, prog, ctx, 0);
      }
      if (err == Err::Ok && w.done() != c.remote.app.bytes) err = Err::Download;
      if (err == Err::Ok) err = w.finish(c.remote.app.sha256);
      if (err == Err::Ok) err = w.activate(_p);
      if (err != Err::Ok) w.abort();
    }
    homenet::off();
    return err;
  }

  void restart() override {
    delay(300);
    ESP.restart();
  }

  // --- the computer page ---

  bool lanStart() override {
    lanStop();
    _lan = ota::Lan{};
    _lan.phase = ota::Lan::Joining;
    const homenet::Result j = homenet::connect(_p);
    if (j != homenet::Result::Ok) {
      _lan.phase = ota::Lan::Failed;
      _lan.err = fromNet(j);
      return false;
    }
    WiFi.setSleep(false);  // a sleeping radio drops upload packets
    MDNS.begin("toybox");
    MDNS.addService("http", "tcp", 80);
    snprintf(_lan.url, sizeof(_lan.url), "http://toybox.local");
    snprintf(_lan.ip, sizeof(_lan.ip), "%s", WiFi.localIP().toString().c_str());
    _pair.fresh(esp_random());
    snprintf(_lan.code, sizeof(_lan.code), "%s", _pair.code);
    _server = new WebServer(80);
    _server->on("/", HTTP_GET, [this] { sendPage(); });
    _server->on("/p", HTTP_POST, [this] { pair(); });
    _server->on("/u", HTTP_POST, [this] { uploadDone(); }, [this] { uploadChunk(); });
    _server->onNotFound([this] { _server->send(404, "text/plain", "not here"); });
    _server->begin();
    _lan.phase = ota::Lan::Waiting;
    return true;
  }

  void lanLoop() override {
    if (_server) _server->handleClient();
    // Restart once the reply has had time to reach the computer.
    if (_lan.phase == ota::Lan::Done && _doneAt && millis() - _doneAt > 2500) {
      lanStop();
      restart();
    }
  }

  void lanStop() override {
    if (_server) {
      _server->stop();
      delete _server;
      _server = nullptr;
      MDNS.end();
    }
    _writer.abort();
    if (_lan.phase != ota::Lan::Off) homenet::off();
    if (_lan.phase != ota::Lan::Done) _lan.phase = ota::Lan::Off;
    _doneAt = 0;
  }

  const ota::Lan& lan() const override { return _lan; }

  bool takeRollbackNotice(char* ver, size_t n) override {
    if (!_p.isKey(ota::K_ROLLED)) return false;
    _p.getString(ota::K_ROLLED, ver, n);
    _p.remove(ota::K_ROLLED);
    return true;
  }

  // --- from the SD card ---

  void cardScan(ota::Card& out) override {
    out = ota::Card{};
    if (!sdcard::browseOpen()) return;  // NoCard: nothing answered
    uint8_t* buf = (uint8_t*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    char path[64];
    out.state = ota::Card::Nothing;
    for (ota::CardFile& f : out.files) {
      snprintf(path, sizeof(path), "%s%s", ota::CARD_DIR, f.file);
      const int n = sdcard::fileSize(path);
      if (n <= 0) continue;
      f.bytes = (uint32_t)n;
      f.found = true;
      // How big its home is.
      const esp_partition_t* home =
          f.label ? esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                             f.label)
                  : esp_ota_get_next_update_partition(nullptr);
      if (!home || f.bytes > home->size) {
        out.state = ota::Card::TooBig;
        snprintf(out.bad, sizeof(out.bad), "%s", f.file);
        break;
      }
      if (!buf) continue;
      if (f.label) {
        // A pack: its four letters.
        const int k = sdcard::readAt(path, 0, buf, 4);
        if (!ota::isPackHead(buf, k > 0 ? (size_t)k : 0)) {
          out.state = ota::Card::NotToybox;
          snprintf(out.bad, sizeof(out.bad), "%s", f.file);
          break;
        }
      } else {
        // The firmware: read through for the marker and the version after it.
        ota::MarkerScan ms(toybox_app_marker, MARKER_LEN);
        for (uint32_t at = 0; at < f.bytes && !ms.done();) {
          const int k = sdcard::readAt(path, at, buf, CHUNK);
          if (k <= 0) break;
          ms.feed(buf, (size_t)k);
          at += (uint32_t)k;
        }
        if (!ms.seen) {
          out.state = ota::Card::NotToybox;
          snprintf(out.bad, sizeof(out.bad), "%s", f.file);
          break;
        }
        snprintf(out.version, sizeof(out.version), "%s", ms.ver);
      }
    }
    sdcard::readAtClose();
    free(buf);
    sdcard::browseClose();
    if (out.state == ota::Card::Nothing && out.count() > 0) out.state = ota::Card::Found;
  }

  Err cardInstall(const ota::Card& c, ota::Progress prog, void* ctx) override {
    if (c.state != ota::Card::Found) return Err::Download;
    const ota::Gate g = ota::mayStart(sensors::batteryPercent(), sensors::charging(),
                                      ota::DEFAULT_MIN_BATTERY,
                                      c.hasApp() ? c.files[0].bytes : 0, slotBytes());
    if (g == ota::Gate::LowBattery) return Err::LowBattery;
    if (g == ota::Gate::TooBig) return Err::TooBig;
    if (!sdcard::browseOpen()) return Err::Download;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    if (!buf) {
      sdcard::browseClose();
      return Err::LowMemory;
    }
    char path[64];
    Err err = Err::Ok;
    // Packs first (every v2 build reads the same pack format, so the running
    // app is content with a new one), the firmware last, as the site does.
    for (int i = 1; i <= ota::CARD_FILES && err == Err::Ok; i++) {
      const int idx = i % ota::CARD_FILES;  // 1,2,3,4 then 0
      const ota::CardFile& f = c.files[idx];
      if (!f.found) continue;
      snprintf(path, sizeof(path), "%s%s", ota::CARD_DIR, f.file);
      if (f.label) {
        if (strcmp(f.label, "base_font") == 0) _p.remove(ota::K_BASE);
        PartWriter pw;
        err = pw.begin(f.label, f.bytes);
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        for (uint32_t at = 0; err == Err::Ok && at < f.bytes;) {
          const int k = sdcard::readAt(path, at, buf, CHUNK);
          if (k <= 0) {
            err = Err::Download;
            break;
          }
          mbedtls_sha256_update(&sha, buf, (size_t)k);
          err = pw.write(buf, (size_t)k);
          at += (uint32_t)k;
          if (prog) prog(ctx, (uint8_t)idx, at, f.bytes);
        }
        uint8_t d[32];
        char want[65], got[65];
        mbedtls_sha256_finish(&sha, d);
        mbedtls_sha256_free(&sha);
        hex(d, 32, want);
        // Read back from flash: what landed is what the card held.
        if (err == Err::Ok && !(partSha(pw.part(), f.bytes, got) && strcmp(got, want) == 0))
          err = Err::Mismatch;
        if (err == Err::Ok && strcmp(f.label, "base_font") == 0) _p.putString(ota::K_BASE, got);
      } else {
        SlotWriter w;
        err = w.begin(f.bytes);
        for (uint32_t at = 0; err == Err::Ok && at < f.bytes;) {
          const int k = sdcard::readAt(path, at, buf, CHUNK);
          if (k <= 0) {
            err = Err::Download;
            break;
          }
          err = w.write(buf, (size_t)k);
          at += (uint32_t)k;
          if (prog) prog(ctx, 0, at, f.bytes);
        }
        if (err == Err::Ok) err = w.finish(nullptr);  // the marker and the image checksum
        if (err == Err::Ok) err = w.activate(_p);
        if (err != Err::Ok) w.abort();
      }
    }
    sdcard::readAtClose();
    // Installed once: the files become .done, so the next look finds nothing.
    if (err == Err::Ok) {
      for (const ota::CardFile& f : c.files) {
        if (!f.found) continue;
        char done[72];
        snprintf(path, sizeof(path), "%s%s", ota::CARD_DIR, f.file);
        snprintf(done, sizeof(done), "%s.done", path);
        sdcard::moveFile(path, done);
      }
    }
    free(buf);
    sdcard::browseClose();
    return err;
  }

 private:
  bool baseDiffers(const ota::Part& base) {
    char have[65] = {};
    if (_p.isKey(ota::K_BASE)) _p.getString(ota::K_BASE, have, sizeof(have));
    if (strcmp(have, base.sha256) == 0) return false;
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "base_font");
    if (!part) return false;  // an old table: nowhere to put it
    char got[65];
    if (partSha(part, base.bytes, got) && strcmp(got, base.sha256) == 0) {
      _p.putString(ota::K_BASE, got);  // the installer wrote it; remember
      return false;
    }
    return true;
  }

  void sendPage() {
    String page = FPSTR(PAGE);
    page.replace("%VER%", String(TB_VERSION) + " &middot; slot " + slot());
    _server->sendHeader("Cache-Control", "no-store");
    _server->send(200, "text/html; charset=utf-8", page);
  }

  void pair() {
    const bool ok = _pair.check(_server->arg("c").c_str());
    String r = String("{\"ok\":") + (ok ? "true" : "false") +
               ",\"locked\":" + (_pair.locked() ? "true" : "false") + "}";
    _server->send(ok ? 200 : 403, "application/json", r);
  }

  void uploadChunk() {
    HTTPUpload& up = _server->upload();
    if (up.status == UPLOAD_FILE_START) {
      _reject = Err::Ok;
      _writer.abort();
      // The right code again (the page sends what it paired with); a wrong one
      // here counts toward the lock-out like any other.
      if (!_pair.check(_server->arg("c").c_str())) {
        _reject = Err::WrongCode;
        return;
      }
      const ota::Gate g = ota::mayStart(sensors::batteryPercent(), sensors::charging(),
                                        ota::DEFAULT_MIN_BATTERY, 0, slotBytes());
      if (g == ota::Gate::LowBattery) {
        _reject = Err::LowBattery;
        return;
      }
      if (lowInternal()) {
        _reject = Err::LowMemory;
        return;
      }
      _reject = _writer.begin(0);
      _lan.phase = ota::Lan::Receiving;
      _lan.done = 0;
      _lan.total = (uint32_t)_server->clientContentLength();
      _lan.incoming[0] = 0;
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (_reject != Err::Ok) return;
      _reject = _writer.write(up.buf, up.currentSize);
      _lan.done = _writer.done();
      if (!_lan.incoming[0] && _writer.version()[0])
        snprintf(_lan.incoming, sizeof(_lan.incoming), "%s", _writer.version());
      if (_reject != Err::Ok) _writer.abort();
    } else if (up.status == UPLOAD_FILE_END) {
      if (_reject != Err::Ok) return;
      _lan.phase = ota::Lan::Verifying;
      _reject = _writer.finish(nullptr);
      if (_reject == Err::Ok) _reject = _writer.activate(_p);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
      _writer.abort();
      _reject = Err::Download;
    }
  }

  void uploadDone() {
    const bool ok = _reject == Err::Ok && _lan.phase == ota::Lan::Verifying;
    if (ok) {
      _lan.phase = ota::Lan::Done;
      _doneAt = millis();
    } else {
      _lan.phase = ota::Lan::Failed;
      _lan.err = _reject == Err::Ok ? Err::Download : _reject;
    }
    String r = String("{\"ok\":") + (ok ? "true" : "false") + ",\"err\":\"" +
               (ok ? "" : ota::describe(_lan.err)) + "\",\"version\":\"" + _lan.incoming + "\"}";
    _server->send(ok ? 200 : 400, "application/json", r);
    // A failure leaves the page open for another try, unless it was the code
    // and the door is now shut.
    if (!ok && !_pair.locked()) _lan.phase = ota::Lan::Waiting;
  }

  Preferences& _p;
  ota::Lan _lan;
  ota::Pairing _pair;
  WebServer* _server = nullptr;
  SlotWriter _writer;
  Err _reject = Err::Ok;
  uint32_t _doneAt = 0;
};

StickyOta* g_dev = nullptr;

bool haveOtaTable() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                  nullptr) &&
         esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                  nullptr);
}

}  // namespace

ota::Device* device(Preferences& prefs) {
  if (!haveOtaTable()) return nullptr;
  if (!g_dev) g_dev = new StickyOta(prefs);
  return g_dev;
}

void bootGuard(Preferences& p) {
  ota::Trial t;
  if (p.isKey(ota::K_TRIAL)) p.getString(ota::K_TRIAL, t.slot, sizeof(t.slot));
  if (p.isKey(ota::K_PREV)) p.getString(ota::K_PREV, t.prev, sizeof(t.prev));
  t.tries = p.getInt(ota::K_TRIES, 0);
  const esp_partition_t* run = esp_ota_get_running_partition();
  const char* running = run ? run->label : "";
  switch (ota::onBoot(t, running)) {
    case ota::BootAction::Nothing:
      return;
    case ota::BootAction::Forget:
      p.remove(ota::K_TRIAL);
      p.remove(ota::K_PREV);
      p.remove(ota::K_TRIES);
      return;
    case ota::BootAction::Count:
      p.putInt(ota::K_TRIES, t.tries + 1);
      return;
    case ota::BootAction::RollBack: {
      const esp_partition_t* back = esp_partition_find_first(
          ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, t.prev[0] ? t.prev : nullptr);
      p.remove(ota::K_TRIAL);
      p.remove(ota::K_PREV);
      p.remove(ota::K_TRIES);
      if (!back || back == run) return;
      // What was on trial, for the notice the old build shows once.
      char ver[ota::VER_MAX] = {};
      if (p.isKey(ota::K_TVER)) p.getString(ota::K_TVER, ver, sizeof(ver));
      p.putString(ota::K_ROLLED, ver[0] ? ver : running);
      esp_ota_set_boot_partition(back);
      delay(100);
      ESP.restart();
    }
  }
}

void confirm(Preferences& p) {
  // With the bootloader's own rollback compiled in, this is what tells it the
  // image is good; without it, a harmless no-op.
  esp_ota_mark_app_valid_cancel_rollback();
  if (!p.isKey(ota::K_TRIAL)) return;
  p.remove(ota::K_TRIAL);
  p.remove(ota::K_PREV);
  p.remove(ota::K_TRIES);
}

}  // namespace otadev
#endif
