"""The Readest library page (phone search), in Chromium, against a stand-in device.

Run from test/web:  python3 test_lib_page.py
The page is extracted from toybox-core/src/tools/lib_web.h, so this checks
what the device serves. The stand-in answers /r with what the device would
publish and records every POST.

Checked: the list draws (titles escaped, a tick for books already on the
Sticky, Download for the rest), a search posts the typed text, a Download
tap posts that book's id once, a new version redraws, and a device that
stops answering is said to be unreachable."""
import re, threading, json, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from playwright.sync_api import sync_playwright

SRC = open('../../toybox-core/src/tools/lib_web.h').read()
PAGE = re.search(r'R"HTML\((.*?)\)HTML"', SRC, re.S).group(1)
posts = []
state = {'r': {'v': 1, 'q': '', 'busy': '', 'items': [
    {'id': 'aa11', 'title': 'Walden <b>', 'author': 'Thoreau', 'local': True},
    {'id': 'bb22', 'title': 'Book 02', 'author': 'Author 2', 'local': False}]}, 'down': False}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path == '/r':
            if state['down']:
                self.connection.close(); return
            body = json.dumps(state['r']).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers()
            self.wfile.write(body); return
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.end_headers()
        self.wfile.write(PAGE.encode())
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        posts.append((self.path, dict(urllib.parse.parse_qsl(self.rfile.read(n).decode(), keep_blank_values=True))))
        self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers()
        self.wfile.write(b'{"ok":1}')

srv = ThreadingHTTPServer(('127.0.0.1', 8099), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()

fails = []
def check(cond, msg):
    print(("PASS  " if cond else "FAIL  ") + msg)
    if not cond: fails.append(msg)

with sync_playwright() as p:
    b = p.chromium.launch(executable_path='/opt/pw-browsers/chromium')
    pg = b.new_context(viewport={'width': 390, 'height': 844}).new_page()
    pg.goto('http://127.0.0.1:8099/', wait_until='networkidle')
    pg.wait_for_selector('.b')
    check(pg.locator('.b').count() == 2, "two books listed")
    check('Walden <b>' in pg.locator('.b').nth(0).inner_text(), "a title is shown as text, not markup")
    check('on the Sticky' in pg.locator('.b').nth(0).inner_text(), "a local book shows the tick")
    check(pg.locator('.b').nth(1).locator('button').inner_text() == 'Download', "a cloud book shows Download")
    check(pg.locator('#m').inner_text() == 'Most recently read', "no query: most recently read")

    pg.fill('#q', ' tom & jerry ')
    pg.click('#f button')
    pg.wait_for_timeout(400)
    q = [b_ for (path, b_) in posts if path == '/q']
    check(len(q) == 1 and q[0].get('q') == 'tom & jerry', f"search posted {q}")

    btn = pg.locator('.b').nth(1).locator('button')
    btn.click(); btn.click(force=True)
    pg.wait_for_timeout(400)
    d = [b_ for (path, b_) in posts if path == '/d']
    check(len(d) == 1 and d[0].get('id') == 'bb22', f"download posted once: {d}")
    check(btn.is_disabled(), "the Download button waits")

    state['r'] = {'v': 2, 'q': 'Book', 'busy': 'Downloading on the Sticky...', 'items': [
        {'id': 'bb22', 'title': 'Book 02', 'author': 'Author 2', 'local': True}]}
    pg.wait_for_function("document.getElementById('m').textContent.indexOf('Downloading')>=0", timeout=5000)
    check(pg.locator('.b').count() == 1 and 'on the Sticky' in pg.locator('.b').inner_text(),
          "a new version redraws")

    state['down'] = True
    pg.wait_for_function("document.getElementById('m').textContent.indexOf('not answering')>=0", timeout=5000)
    check(True, "an unreachable Sticky is said so")
    b.close()

srv.shutdown()
if fails:
    print(f"{len(fails)} failed"); raise SystemExit(1)
print("library page ok")
