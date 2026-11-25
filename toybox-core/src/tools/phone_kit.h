// The look of the pages a phone opens (kit "Phone ..." artboards): warm paper
// with a dot grid, a white bar with a bold title under a small mono line, a
// yellow STICKY tag, black-bordered boxes with hard shadows, and one black
// button at the foot with a yellow shadow. Shared by Recipes, Flashcards and
// Picker so the three read as one place. No web fonts are fetched -- the
// Sticky's own Wi-Fi has no internet -- so the stacks fall back to the
// phone's own sans and mono.
#pragma once
#include <Arduino.h>

#ifndef TOYBOX_HOST
namespace phonekit {

inline const char CSS[] PROGMEM = R"CSS(*{box-sizing:border-box}
body{margin:0;background:#F5F1E6 radial-gradient(#D9D2BF 1.1px,transparent 1.3px) 0 0/16px 16px;color:#111;
font:500 16px/1.45 "Noto Sans","Noto Sans Thai Looped",system-ui,-apple-system,sans-serif}
.mono,.eye,.lab,.badge,.tag{font-family:"Noto Sans Mono",ui-monospace,Menlo,Consolas,monospace}
header{position:sticky;top:0;z-index:2;height:60px;display:flex;align-items:center;gap:12px;background:#fff;
border-bottom:2px solid #111;padding:0 14px}
.mark{width:40px;height:40px;flex:none;background:#FFD84D;border:2px solid #111;border-radius:9px;box-shadow:2px 2px 0 #111;
display:flex;align-items:center;justify-content:center}
.mark svg{display:block}
header .t{flex:1;display:flex;flex-direction:column;min-width:0}
.eye{font-size:10px;font-weight:600;letter-spacing:2px;text-transform:uppercase}
header h1{margin:0;font-size:19px;font-weight:700;line-height:1.2}
.badge{font-size:11px;font-weight:700;background:#FFD84D;border:2px solid #111;padding:4px 8px;transform:rotate(-2deg);
box-shadow:2px 2px 0 #111;white-space:nowrap}
main{max-width:560px;margin:0 auto;padding:18px 18px 120px;display:flex;flex-direction:column;gap:16px}
.lab{font-size:11px;font-weight:700;letter-spacing:1.5px;text-transform:uppercase;display:flex;gap:8px;align-items:center;
margin-bottom:6px}
.num{background:#111;color:#FFD84D;padding:0 6px}
input,textarea{width:100%;padding:12px;border:2px solid #111;border-radius:0;background:#fff;font:inherit;color:#111;
-webkit-appearance:none}
textarea{min-height:170px;line-height:24px;background:#fff repeating-linear-gradient(#fff 0 23px,#E9E4D6 23px 24px);
background-attachment:local}
.seg{display:grid;grid-template-columns:1fr 1fr;border:2px solid #111;background:#fff}
.seg button{height:42px;border:0;background:#fff;color:#111;font:600 14px system-ui}
.seg button.on{background:#111;color:#fff;font-weight:700}
.card{background:#fff;border:2px solid #111;box-shadow:4px 4px 0 #111;padding:14px}
.tag{background:#FFD84D;border:2px solid #111;font-size:11px;font-weight:700;padding:3px 6px;transform:rotate(3deg);
display:inline-block}
.chips{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.chip{border:2px solid #111;background:#fff;color:#111;padding:8px 14px;font:700 14px system-ui}
.chip.sm{padding:4px 8px;font:600 12px ui-monospace,Menlo,monospace}
.drop{display:block;border:2px dashed #111;background:#fff;padding:12px;text-align:center;font-size:14px}
.hint{font-size:13px}
.err{color:#B00020;font-weight:700;min-height:20px}
.ok{background:#fff;border:2px solid #111;padding:10px 12px;font-weight:600;display:none}
details{background:#fff;border:2px solid #111;padding:10px 12px;font-size:14px}
summary{font-weight:700;cursor:pointer}
a{color:#111}
.go{position:fixed;left:0;right:0;bottom:0;padding:14px 18px 18px;background:linear-gradient(rgba(245,241,230,0),#F5F1E6 35%)}
.go button{display:block;width:100%;max-width:524px;margin:0 auto;height:54px;border:0;background:#111;color:#fff;
font:700 17px system-ui;letter-spacing:1px;box-shadow:4px 4px 0 #FFD84D,4px 4px 0 2px #111}
.go button:disabled{opacity:.5}
)CSS";

// Starts a page: everything up to and including <main>. Sent in pieces, so
// the shared style is stored once however many pages use it.
template <class Server>
inline void begin(Server& s, const char* title, const char* eyebrow, const char* h1) {
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, "text/html; charset=utf-8", "");
  String head = F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  head += title;
  head += F("</title><style>");
  s.sendContent(head);
  s.sendContent_P(CSS);
  String bar = F("</style></head><body><header><span class=\"mark\" role=\"img\" aria-label=\"Toybox\"><svg width=\"30\" height=\"30\" viewBox=\"0 0 100 100\"><path d=\"M48 4Q48 19 63 19Q48 19 48 34Q48 19 33 19Q48 19 48 4Z\" fill=\"#111\"/><path d=\"M76 19Q76 30 87 30Q76 30 76 41Q76 30 65 30Q76 30 76 19Z\" fill=\"#111\"/><path d=\"M20 22Q20 31 29 31Q20 31 20 40Q20 31 11 31Q20 31 20 22Z\" fill=\"#111\"/><path d=\"M12 46H84L78 90H18Z\" fill=\"#111\" stroke=\"#111\" stroke-width=\"7\" stroke-linejoin=\"round\"/><path d=\"M12 46L3 32M84 46L93 32\" stroke=\"#111\" stroke-width=\"7\" stroke-linecap=\"round\"/><g transform=\"translate(47 69) scale(0.85) translate(-48 -71)\" fill=\"none\" stroke=\"#FFD84D\" stroke-width=\"6.5\" stroke-linecap=\"square\"><path d=\"M25 58H45M35 58V84\"/><path d=\"M53 58V84M53 58H62.5a6 6 0 0 1 0 12H53M53 70H64a7 7 0 0 1 0 14H53\" stroke-linejoin=\"round\"/></g></svg></span><div class=\"t\">"
                 "<span class=\"eye\">");
  bar += eyebrow;
  bar += F("</span><h1>");
  bar += h1;
  bar += F("</h1></div><span class=\"badge\" role=\"status\">&#9638; STICKY</span></header><main>");
  s.sendContent(bar);
}

template <class Server>
inline void end(Server& s) {
  s.sendContent(F("</body></html>"));
  s.sendContent("");  // the end of a chunked reply
}

}  // namespace phonekit
#endif
