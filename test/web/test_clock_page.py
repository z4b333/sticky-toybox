"""The clock page, in Chromium, against a stand-in device.

Run from test/web:  python3 test_clock_page.py
The page is extracted from toybox-core/src/tools/clock_web.h, so this checks
what the device serves. The stand-in applies the device's own rules (8+
character Wi-Fi password, http(s) server) and records every POST.

Checked: the time is sent on load as local wall-clock ms with a POSIX zone
(and the zone matches the browser's for three real zones), the Wi-Fi and
sync-account forms post what was typed, and their client-side checks stop
bad input before it reaches the device."""
import re, threading, json, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from playwright.sync_api import sync_playwright

SRC = open('../../toybox-core/src/tools/clock_web.h').read()
PAGE = re.search(r'R"HTML\((.*?)\)HTML"', SRC, re.S).group(1)
posts = []
scans = []
NETS = [{'s': 'Somchai 2.4G', 'r': -48, 'l': 1}, {'s': 'Cafe <"free">', 'r': -80, 'l': 0}]

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path.startswith('/s'):
            scans.append(self.path)
            # busy once, then the list (as the device does while its scan runs)
            body = {'busy': 1} if len(scans) == 1 or self.path.endswith('r=1') else {'nets': NETS}
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers()
            self.wfile.write(json.dumps(body).encode()); return
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.end_headers()
        self.wfile.write(PAGE.encode())
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        body = dict(urllib.parse.parse_qsl(self.rfile.read(n).decode(), keep_blank_values=True))
        posts.append((self.path, body))
        ok = 1
        if self.path == '/w': ok = int(bool(body.get('s')) and (body.get('p', '') == '' or len(body['p']) >= 8))
        if self.path == '/k': ok = int(bool(body.get('u')) and bool(body.get('p')))
        self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers()
        self.wfile.write(json.dumps({'ok': ok}).encode())

srv = ThreadingHTTPServer(('127.0.0.1', 8098), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()

fails = []
def check(cond, msg):
    print(("PASS  " if cond else "FAIL  ") + msg)
    if not cond: fails.append(msg)

ZONES = {'Asia/Bangkok': 'STD-7', 'Europe/Berlin': 'STD-1DST,M3.5.0,M10.5.0/3',
         'America/New_York': 'STD5DST,M3.2.0,M11.1.0'}
with sync_playwright() as p:
    b = p.chromium.launch(executable_path='/opt/pw-browsers/chromium')
    for zone, want in ZONES.items():
        posts.clear()
        ctx = b.new_context(timezone_id=zone, viewport={'width': 390, 'height': 844})
        pg = ctx.new_page()
        pg.goto('http://127.0.0.1:8098/', wait_until='networkidle')
        pg.wait_for_function("document.getElementById('msg').textContent.indexOf('Done')>=0")
        t = [b_ for (path, b_) in posts if path == '/t']
        check(len(t) == 1 and t[0].get('z') == want, f"{zone}: zone sent as {t[0].get('z') if t else None}")
        # local wall clock: the page's own clock shifted by its offset
        off = pg.evaluate("-new Date().getTimezoneOffset()*60000")
        now = pg.evaluate("Date.now()")
        check(t and abs(int(t[0]['t']) - (now + off)) < 120000, f"{zone}: time is local wall clock")
        ctx.close()

    ctx = b.new_context(timezone_id='Asia/Bangkok', viewport={'width': 390, 'height': 844})
    pg = ctx.new_page()
    pg.goto('http://127.0.0.1:8098/', wait_until='networkidle')
    posts.clear()
    # Wi-Fi: short password stopped in the page
    pg.fill('#ws', 'Somchai 5G'); pg.fill('#wp', 'short')
    pg.get_by_role('button', name='Save home Wi-Fi').click()
    pg.wait_for_timeout(200)
    check('at least 8' in pg.inner_text('#wm') and not posts, "a short Wi-Fi password never leaves the page")
    pg.fill('#wp', 'correct horse')
    pg.get_by_role('button', name='Save home Wi-Fi').click()
    pg.wait_for_function("document.getElementById('wm').textContent.indexOf('Saved')>=0")
    check(posts and posts[-1] == ('/w', {'s': 'Somchai 5G', 'p': 'correct horse'}), f"Wi-Fi posted: {posts[-1] if posts else None}")
    # Backup network: same rules, flagged b=1
    posts.clear()
    pg.fill('#bs', 'Phone hotspot'); pg.fill('#bp', 'hotspot123')
    pg.get_by_role('button', name='Save backup Wi-Fi').click()
    pg.wait_for_function("document.getElementById('bm').textContent.indexOf('Saved')>=0")
    check(posts and posts[-1] == ('/w', {'s': 'Phone hotspot', 'p': 'hotspot123', 'b': '1'}), f"backup posted with b=1: {posts[-1] if posts else None}")
    # Account
    posts.clear()
    pg.fill('#ku', 'reader'); pg.fill('#kp', 'pw&=?x'); pg.fill('#ks', 'ftp://bad')
    pg.get_by_role('button', name='Save sync account').click()
    pg.wait_for_timeout(200)
    check('https://' in pg.inner_text('#km') and not posts, "a non-http server never leaves the page")
    pg.fill('#ks', '')
    pg.get_by_role('button', name='Save sync account').click()
    pg.wait_for_function("document.getElementById('km').textContent.indexOf('Saved')>=0")
    check(posts and posts[-1] == ('/k', {'u': 'reader', 'p': 'pw&=?x', 's': ''}), f"account posted intact: {posts[-1] if posts else None}")
    check('Sync position' in pg.inner_text('#km'), "tells you where Sync lives")
    # The pick list: filled from the device's scan (after one "busy"),
    # names shown as text, choosing one fills the name and moves to the password.
    pg.wait_for_function("document.querySelectorAll('#wl option').length>=4", timeout=8000)
    opts = pg.eval_on_selector_all('#wl option', 'e=>e.map(o=>[o.value,o.textContent])')
    check(len(opts) == 5 and opts[1][0] == 'Somchai 2.4G' and opts[2][0] == 'Cafe <"free">',
          f"scan listed: {opts}")
    check('2 found' in opts[0][1], "the count is said")
    pg.select_option('#wl', 'Somchai 2.4G')
    check(pg.input_value('#ws') == 'Somchai 2.4G', "choosing fills the name")
    check(pg.evaluate("document.activeElement.id") == 'wp', "then the password field")
    pg.select_option('#bl', 'Cafe <"free">')
    check(pg.input_value('#bs') == 'Cafe <"free">', "the backup list too, quotes intact")
    check('2.4' in pg.inner_text('.band') and '5' in pg.inner_text('.band'), "the 2.4 GHz-only note is shown")
    n0 = len(scans)
    pg.select_option('#wl', '\u0001')
    pg.wait_for_timeout(2500)
    check(any(x.endswith('r=1') for x in scans[n0:]), "Scan again asks for a fresh scan")
    pg.screenshot(path='/tmp/clock_page.png', full_page=True)
    b.close()

srv.shutdown()
print("clock page: %d failed" % len(fails) if fails else "clock page ok")
raise SystemExit(1 if fails else 0)
