// KOReader sync, the parts that do not need a screen: MD5, the book's name,
// the JSON, the decide() rule, and above all the xpointers -- both directions,
// for every drawn character of the fixture, against positions computed by an
// independent implementation (tools/kosync_reference.py).
//
// Build and run (from test/host):
//   g++ -std=gnu++17 -O2 -w -DTOYBOX_HOST -I . -I mock -I ../../src \
//     -I ../../toybox-core/src -I ../../lib/miniz/src \
//     kosync_test.cpp ../../toybox-core/src/epubcore.cpp \
//     ../../lib/miniz/src/toybox_miniz_impl.c -o kosync_test
//   ./kosync_test fixtures/kosync.epub fixtures/kosync_expect.txt
//
// Regenerate the fixture and its expectations (from the repo root):
//   python3 tools/make_kosync_fixture.py
//   python3 tools/kosync_reference.py test/host/fixtures/kosync.epub \
//     > test/host/fixtures/kosync_expect.txt
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tools/kosync.h"

namespace {

int g_fail = 0;
#define CHECK(cond, ...)          \
  do {                            \
    if (!(cond)) {                \
      printf("KOSYNC FAIL: ");    \
      printf(__VA_ARGS__);        \
      printf("\n");               \
      g_fail++;                   \
    }                             \
  } while (0)

struct FileIO : epubc::IO {
  FILE* f = nullptr;
  uint32_t sz = 0;
  int read(uint32_t pos, void* dst, uint32_t n) override {
    if (fseek(f, (long)pos, SEEK_SET) != 0) return -1;
    return (int)fread(dst, 1, n, f);
  }
  uint32_t size() override { return sz; }
};

struct MemIO : epubc::IO {
  std::vector<uint8_t> d;
  int read(uint32_t pos, void* dst, uint32_t n) override {
    if (pos >= d.size()) return 0;
    if (pos + n > d.size()) n = (uint32_t)(d.size() - pos);
    memcpy(dst, d.data() + pos, n);
    return (int)n;
  }
  uint32_t size() override { return (uint32_t)d.size(); }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    printf("usage: kosync_test <fixture.epub> <expect.txt>\n");
    return 2;
  }

  // --- MD5, against RFC 1321's own vectors --------------------------------
  {
    char h[33];
    md5t::hex("", h);
    CHECK(!strcmp(h, "d41d8cd98f00b204e9800998ecf8427e"), "md5('') = %s", h);
    md5t::hex("abc", h);
    CHECK(!strcmp(h, "900150983cd24fb0d6963f7d28e17f72"), "md5('abc') = %s", h);
    md5t::hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890", h);
    CHECK(!strcmp(h, "57edf4a22be3c955ac49da2e2107b67a"), "md5(80 digits) = %s", h);
  }

  // --- a book's name: KOReader's partial MD5 -------------------------------
  // 300,000 bytes of a known pattern; the expected value is Python's hashlib
  // over the same samples (offsets 0, 1K, 4K, 16K, 64K, 256K).
  {
    MemIO m;
    m.d.resize(300000);
    for (size_t i = 0; i < m.d.size(); i++) m.d[i] = (uint8_t)((i * 7 + 3) & 255);
    char h[33];
    CHECK(kosync::docHash(m, h) && !strcmp(h, "7f79a1051a44620f9633d5a081d3f5b6"),
          "partial md5 of the pattern = %s", h);
    // Shorter than the first sample: one short read, then nothing.
    MemIO s;
    s.d = {'a', 'b', 'c'};
    CHECK(kosync::docHash(s, h) && !strcmp(h, "900150983cd24fb0d6963f7d28e17f72"),
          "partial md5 of 'abc' = %s", h);
  }

  // --- JSON both ways --------------------------------------------------------
  {
    kosync::Remote r;
    const char* body =
        "{\"timestamp\":1787300000,\"device\":\"Readest \\\"Pixel\\\"\",\"percentage\":0.4213,"
        "\"progress\":\"/body/DocFragment[3]/body/p[2]/text()[1].5\",\"device_id\":\"abc123\","
        "\"document\":\"d\"}";
    CHECK(kosync::parseRemote(body, r), "a full record did not parse");
    CHECK(!strcmp(r.progress, "/body/DocFragment[3]/body/p[2]/text()[1].5"), "progress '%s'",
          r.progress);
    CHECK(!strcmp(r.device, "Readest \"Pixel\""), "device '%s'", r.device);
    CHECK(fabs(r.percentage - 0.4213) < 1e-9, "percentage %f", r.percentage);
    CHECK(r.timestamp == 1787300000, "timestamp %lld", (long long)r.timestamp);
    CHECK(!kosync::parseRemote("{}", r) && !r.have, "an empty object read as a record");
    CHECK(!kosync::parseRemote("{\"progress\":null}", r), "a null progress read as a record");
    char out[512];
    kosync::buildPut(out, sizeof(out), "d0c", "/body/DocFragment[1]/body/p[1]/text()[1].0", 0.25,
                     "id1");
    kosync::Remote back;
    CHECK(kosync::parseRemote(out, back) && fabs(back.percentage - 0.25) < 1e-9 &&
              !strcmp(back.deviceId, "id1") && !strcmp(back.device, "Toybox"),
          "our own PUT body does not read back: %s", out);
  }

  // --- the rule --------------------------------------------------------------
  {
    kosync::Remote none, mine, ahead, behind, near;
    mine.have = ahead.have = behind.have = near.have = true;
    strcpy(mine.deviceId, "me");
    mine.percentage = 0.9;
    strcpy(ahead.deviceId, "readest");
    ahead.percentage = 0.5;
    strcpy(behind.deviceId, "readest");
    behind.percentage = 0.1;
    strcpy(near.deviceId, "readest");
    near.percentage = 0.3004;
    using D = kosync::Decision;
    CHECK(kosync::decide(0.3, none, "me") == D::Upload, "nothing stored should upload");
    CHECK(kosync::decide(0.3, mine, "me") == D::Upload, "our own record should be replaced");
    CHECK(kosync::decide(0.3, ahead, "me") == D::AskJump, "further on elsewhere should ASK");
    CHECK(kosync::decide(0.3, behind, "me") == D::Upload, "behind elsewhere should upload");
    CHECK(kosync::decide(0.3, near, "me") == D::Same, "a page apart is the same place");
  }

  // --- the rule by paragraph ---------------------------------------------------
  {
    using D = kosync::Decision;
    CHECK(kosync::decideAt(3, 500, 3, 400, 900) == D::Same, "on the page shown");
    CHECK(kosync::decideAt(3, 400, 3, 400, 900) == D::Same, "at the page's first character");
    CHECK(kosync::decideAt(3, 900, 3, 400, 900) == D::AskJump, "the next page's first character");
    CHECK(kosync::decideAt(3, 399, 3, 400, 900) == D::Upload, "just before the page");
    CHECK(kosync::decideAt(4, 0, 3, 400, 900) == D::AskJump, "a later chapter");
    CHECK(kosync::decideAt(2, 99999, 3, 400, 900) == D::Upload, "an earlier chapter");
  }

  // --- accounts --------------------------------------------------------------
  {
    Preferences p;
    p.begin("t", false);
    CHECK(!kosync::saveAccount(p, "", "", "pw"), "an empty user was saved");
    CHECK(!kosync::saveAccount(p, "ftp://x", "u", "pw"), "a non-http server was saved");
    CHECK(kosync::saveAccount(p, "https://sync.example.org///", "reader", "secret"),
          "a good account was refused");
    char u[96];
    kosync::url(p, u, sizeof(u));
    CHECK(!strcmp(u, "https://sync.example.org"), "trailing slashes kept: %s", u);
    char key[40];
    p.getString(kosync::K_KEY, key, sizeof(key));
    char want[33];
    md5t::hex("secret", want);
    CHECK(!strcmp(key, want), "the stored key is not md5(password)");
  }

  // --- Readest sign-in: the answers its server gives --------------------------
  {
    kosync::DeviceCode c;
    CHECK(kosync::parseCode("{\"device_code\":\"abc\",\"user_code\":\"BCDF-GHJK\","
                            "\"verification_uri\":\"https://web.readest.com/link\","
                            "\"verification_uri_complete\":\"https://web.readest.com/link?code=BCDF-GHJK\","
                            "\"expires_in\":600,\"interval\":5}",
                            c) &&
              !strcmp(c.user, "BCDF-GHJK") && !strcmp(c.uri, "https://web.readest.com/link") &&
              !strcmp(c.uriFull, "https://web.readest.com/link?code=BCDF-GHJK") && c.intervalS == 5,
          "a code answer did not parse");
    CHECK(!kosync::parseCode("{\"error\":\"server_error\"}", c), "an error parsed as a code");
    Preferences p;
    p.begin("r", false);
    using P = kosync::Poll;
    CHECK(kosync::takeToken(p, 400, "{\"error\":\"authorization_pending\"}") == P::Pending,
          "pending");
    CHECK(kosync::takeToken(p, 400, "{\"error\":\"expired_token\"}") == P::Expired, "expired");
    CHECK(kosync::takeToken(p, 500, "{\"error\":\"server_error\"}") == P::Error, "server error");
    CHECK(!kosync::haveAccount(p), "a refused sign-in saved an account");
    CHECK(kosync::takeToken(p, 200,
                            "{\"access_token\":\"k3y\",\"token_type\":\"bearer\",\"id\":\"1\","
                            "\"username\":null}") == P::Approved,
          "an approval with no username was refused");
    char key[40], want[33], name[64];
    p.getString(kosync::K_KEY, key, sizeof(key));
    md5t::hex("k3y", want);
    kosync::user(p, name, sizeof(name));
    CHECK(kosync::isReadest(p) && !strcmp(key, want) && !strcmp(name, "Readest"),
          "approval stored readest=%d key-ok=%d name '%s'", kosync::isReadest(p), !strcmp(key, want),
          name);
  }

  // --- xpointers --------------------------------------------------------------
  FileIO io;
  io.f = fopen(argv[1], "rb");
  if (!io.f) {
    printf("cannot open %s\n", argv[1]);
    return 2;
  }
  fseek(io.f, 0, SEEK_END);
  io.sz = (uint32_t)ftell(io.f);
  epubc::Book book;
  if (!book.open(io)) {
    printf("open failed: %s\n", book.error());
    return 2;
  }
  FILE* ex = fopen(argv[2], "r");
  if (!ex) {
    printf("cannot open %s\n", argv[2]);
    return 2;
  }
  struct Row {
    int spine;
    uint32_t off;
    std::string xp;
  };
  std::vector<Row> rows;
  {
    int s;
    unsigned long o;
    char xp[400];
    while (fscanf(ex, "%d %lu %399s", &s, &o, xp) == 3) rows.push_back({s, (uint32_t)o, xp});
  }
  fclose(ex);
  CHECK(rows.size() > 300, "only %zu expected positions", rows.size());

  int okTo = 0, okFrom = 0;
  for (const Row& r : rows) {
    char got[300];
    if (book.xpointerFor(r.spine, r.off, got, sizeof(got)) && r.xp == got)
      okTo++;
    else if (g_fail < 12)
      CHECK(false, "offset %d:%u -> '%s', want '%s'", r.spine, r.off, got, r.xp.c_str());
    int s = -1;
    uint32_t o = 0;
    if (book.offsetFor(r.xp.c_str(), s, o) && s == r.spine && o == r.off)
      okFrom++;
    else if (g_fail < 12)
      CHECK(false, "'%s' -> %d:%u, want %d:%u", r.xp.c_str(), s, o, r.spine, r.off);
  }
  CHECK(okTo == (int)rows.size() && okFrom == (int)rows.size(),
        "xpointers: %d/%zu offset->path, %d/%zu path->offset", okTo, rows.size(), okFrom,
        rows.size());

  // A place in whitespace lands on the next drawn character.
  {
    char got[300];
    // Chapter 1 opens "\n  " before <h1>; offset 0 is whitespace.
    CHECK(book.xpointerFor(0, 0, got, sizeof(got)) &&
              !strcmp(got, rows[0].xp.c_str()),
          "a whitespace start gave '%s', want '%s'", got, rows[0].xp.c_str());
    // Past the end of a chapter: its body.
    CHECK(book.xpointerFor(0, 100000, got, sizeof(got)) &&
              !strcmp(got, "/body/DocFragment[1]/body"),
          "past the end gave '%s'", got);
  }
  // Forms KOReader and others write that are not ours: indices left out, an
  // element with no text() part, an unknown path, a bad fragment.
  {
    int s = -1;
    uint32_t o = 0, want = 0;
    for (const Row& r : rows)
      if (r.xp == "/body/DocFragment[1]/body/div[1]/section[1]/p[2]/text()[1].0") want = r.off;
    CHECK(book.offsetFor("/body/DocFragment[1]/body/div/section/p[2]/text().0", s, o) && s == 0 &&
              o == want,
          "indices left out: %d:%u, want 0:%u", s, o, want);
    CHECK(book.offsetFor("/body/DocFragment[1]/body/div[1]/section[1]/p[2]", s, o) && s == 0 &&
              o == want,
          "element only: %d:%u, want 0:%u", s, o, want);
    CHECK(book.offsetFor("/body/DocFragment[2]/body", s, o) && s == 1 && o == 0,
          "a chapter's body: %d:%u", s, o);
    CHECK(!book.offsetFor("/body/DocFragment[1]/body/table[3]/tr[1]/text().0", s, o),
          "a path this file does not have was 'found'");
    CHECK(!book.offsetFor("/body/DocFragment[9]/body/p[1]", s, o), "a missing chapter was 'found'");
    CHECK(!book.offsetFor("garbage", s, o), "garbage was 'found'");
  }
  // A bare percentage: the chapter by bytes, the place by characters.
  {
    int s = -1;
    uint32_t o = 0;
    CHECK(kosync::placeFraction(book, 0.0, s, o) && s == 0 && o == 0, "0%% -> %d:%u", s, o);
    CHECK(kosync::placeFraction(book, 1.0, s, o) && s == book.spineCount() - 1,
          "100%% -> %d:%u", s, o);
    const uint32_t len1 = book.chapterLength(1);
    CHECK(len1 > 50 && o <= len1, "100%% landed at %u of %u", o, len1);
  }

  if (g_fail) {
    printf("kosync: %d FAILED\n", g_fail);
    return 1;
  }
  printf("kosync ok (md5, book name, json, rule, account, %zu xpointers both ways, fallbacks)\n",
         rows.size());
  return 0;
}
