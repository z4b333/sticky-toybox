// The decisions behind updates over Wi-Fi (toybox-core/src/tools/ota_core.h):
// version order, version.json, the battery gate, pairing and rollback.
//   cd test/host && g++ -std=gnu++17 -O2 -I ../../toybox-core/src ota_test.cpp -o ota_test && ./ota_test ../../docs/firmware/version.json
#include <assert.h>
#include <stdio.h>
#include <string>
#include "tools/ota_core.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(int argc, char** argv) {
  using namespace ota;
  // --- order ---
  CHECK(isNewer("v1.4.5", "v1.4.4"));
  CHECK(isNewer("v2.0.0", "v1.9.9"));
  CHECK(isNewer("v2.0.0", "v2.0.0-beta.9"));
  CHECK(isNewer("v2.0.0-beta.10", "v2.0.0-beta.9"));   // numeric, not text
  CHECK(!isNewer("v2.0.0-beta.9", "v2.0.0-beta.10"));
  CHECK(!isNewer("v1.4.4", "v1.4.4"));
  CHECK(!isNewer("v1.4.4", "v1.4.4-3-gab89111"));      // dev build past the tag
  CHECK(isNewer("v1.4.5", "v1.4.4-3-gab89111"));
  CHECK(isNewer("v1.4.4-3-gab89111", "v1.4.4"));
  CHECK(!isNewer("v1.4.3", "v1.4.4"));
  CHECK(isNewer("v1.4.4", "ab89111"));                 // a build without tags gets the release
  CHECK(!isNewer("garbage", "v1.0.0"));
  CHECK(!isNewer("", "v1.0.0"));
  CHECK(isNewer("v1.10.0", "v1.9.0"));
  CHECK(parse("v2.0.0-beta.3-7-gab89111").past == 7 && parse("v2.0.0-beta.3-7-gab89111").preNum == 3);

  // --- version.json ---
  const char* good =
      "{\n  \"version\": \"v2.0.0\",\n  \"commit\": \"v2.0.0\",\n  \"date\": \"20 Nov 2026\",\n"
      "  \"bytes\": 4049072,\n  \"sha256\": \"6963e7c943964a2e\",\n  \"appBytes\": 2400000,\n"
      "  \"ota\": {\n    \"app\": { \"path\": \"firmware/toybox-app.bin\", \"bytes\": 2400000, \"sha256\": \""
      "aa00000000000000000000000000000000000000000000000000000000000001\" },\n"
      "    \"base\": { \"path\": \"firmware/base_font.tfp\", \"bytes\": 1103072, \"sha256\": \""
      "bb00000000000000000000000000000000000000000000000000000000000002\" },\n"
      "    \"minBattery\": 25\n  }\n}\n";
  Remote r;
  CHECK(parseRemote(good, strlen(good), r));
  CHECK(!strcmp(r.version, "v2.0.0") && r.app.bytes == 2400000 && r.app.present());
  CHECK(!strcmp(r.app.path, "firmware/toybox-app.bin") && r.base.present() && r.minBattery == 25);
  CHECK(!strcmp(r.date, "20 Nov 2026"));
  // The top-level "sha256" (16 digits, the full image) must not be taken for the app's.
  CHECK(r.app.sha256[0] == 'a');

  auto bad = [&](std::string s) { Remote x; return !parseRemote(s.data(), s.size(), x); };
  std::string g(good);
  auto repl = [&](const char* a, const char* b) { std::string s = g; s.replace(s.find(a), strlen(a), b); return s; };
  CHECK(bad("{\"version\": \"v1.4.4\", \"appBytes\": 3983536}"));  // a v1 file: no ota block
  CHECK(bad(repl("aa00000000000000000000000000000000000000000000000000000000000001", "aa01")));
  CHECK(bad(repl("aa00000000000000000000000000000000000000000000000000000000000001",
                 "AA00000000000000000000000000000000000000000000000000000000000001")));
  CHECK(bad(repl("firmware/toybox-app.bin", "../evil.bin")));
  CHECK(bad(repl("firmware/toybox-app.bin", "/abs.bin")));
  CHECK(bad(repl("firmware/toybox-app.bin", "https://elsewhere/x.bin")));
  CHECK(bad(repl("\"v2.0.0\",\n  \"commit\"", "\"nonsense\",\n  \"commit\"")));
  CHECK(bad(repl("\"bytes\": 2400000", "\"bytes\": 99999999999")));
  CHECK(bad(g.substr(0, g.size() / 2)));                      // truncated download
  { Remote x; std::string s = repl("    \"base\"", "    \"bass\""); CHECK(parseRemote(s.data(), s.size(), x) && !x.base.present()); }
  { Remote x; std::string s = repl("\"minBattery\": 25", "\"minBattery\": 250"); CHECK(parseRemote(s.data(), s.size(), x) && x.minBattery == DEFAULT_MIN_BATTERY); }

  // The real file make_image.sh wrote, when there is one with an ota block.
  if (argc > 1) {
    FILE* f = fopen(argv[1], "rb");
    if (f) {
      char buf[4096];
      size_t n = fread(buf, 1, sizeof(buf), f);
      fclose(f);
      Remote x;
      const bool ok = parseRemote(buf, n, x);
      printf("%s: %s\n", argv[1], ok ? x.version : "no ota block (pre-v2 file)");
    }
  }

  // --- battery ---
  CHECK(mayStart(80, false, 30, 2400000, 0x2f0000) == Gate::Ok);
  CHECK(mayStart(29, false, 30, 2400000, 0x2f0000) == Gate::LowBattery);
  CHECK(mayStart(5, true, 30, 2400000, 0x2f0000) == Gate::Ok);       // on USB
  CHECK(mayStart(-1, false, 30, 2400000, 0x2f0000) == Gate::Ok);     // gauge unknown
  CHECK(mayStart(90, true, 30, 0x2f0001, 0x2f0000) == Gate::TooBig);

  // --- pairing ---
  Pairing p;
  CHECK(!p.check("000000"));  // no code yet: nothing opens
  p.fresh(42);
  CHECK(!strcmp(p.code, "000042"));
  CHECK(p.check("000042"));
  CHECK(!p.check("00004"));
  CHECK(!p.check("0000420"));
  CHECK(!p.check(nullptr));
  CHECK(!p.check("111111"));
  CHECK(!p.check("222222"));  // five wrong now
  CHECK(p.locked() && !p.check("000042"));  // even the right one, once locked
  p.fresh(987654321);
  CHECK(!p.locked() && !strcmp(p.code, "654321") && p.check("654321"));

  // --- rollback ---
  Trial t;
  CHECK(onBoot(t, "ota_0") == BootAction::Nothing);
  strcpy(t.slot, "ota_1"); strcpy(t.prev, "ota_0");
  CHECK(onBoot(t, "ota_0") == BootAction::Forget);   // never became the boot slot / bootloader went back
  t.tries = 0; CHECK(onBoot(t, "ota_1") == BootAction::Count);
  t.tries = 2; CHECK(onBoot(t, "ota_1") == BootAction::Count);
  t.tries = 3; CHECK(onBoot(t, "ota_1") == BootAction::RollBack);

  char sz[16];
  sizeText(2400000, sz, sizeof(sz)); CHECK(!strcmp(sz, "2.2 MB"));
  sizeText(1500, sz, sizeof(sz)); CHECK(!strcmp(sz, "2 KB"));

  if (fails) { printf("%d ota checks failed\n", fails); return 1; }
  printf("ota decisions ok\n");
  return 0;
}
