# Climate's phone page (place_web.h): suggestions from Open-Meteo's place list
# as you type, and what a pick or a typed name sends to the device. The page
# is rebuilt from the C++ source; the network is stubbed with Playwright
# routes, so nothing leaves the machine.
import json, re, urllib.parse
from playwright.sync_api import sync_playwright

src = open('toybox-core/src/tools/place_web.h', encoding='utf-8').read()
kit = open('toybox-core/src/tools/phone_kit.h', encoding='utf-8').read()
a = src.index('// Suggestions come from the phone'); b = src.index('s.sendContent(body);', a)
lits = re.findall(r'"((?:[^"\\]|\\.)*)"', src[a:b])
lits = [l for l in lits]  # value="" for the current place is left empty
body = ''.join(bytes(l, 'utf-8').decode('unicode_escape') for l in lits)
css = kit[kit.index('R"CSS(') + 6:kit.index(')CSS"')]
html = ('<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" '
        'content="width=device-width,initial-scale=1"><title>Weather place</title><style>' + css +
        '</style></head><body><header><h1>Weather place</h1></header><main>' + body + '</body></html>')

GEO = {"results": [
    {"name": "Springfield", "latitude": 39.80172, "longitude": -89.64371, "admin1": "Illinois", "country": "United States"},
    {"name": "Springfield", "latitude": 37.21533, "longitude": -93.29824, "admin1": "Missouri", "country": "United States"},
    {"name": "Springfield", "latitude": -27.6534, "longitude": 152.9172, "admin1": "Queensland", "country": "Australia"}]}

fails = []
def check(c, m):
    print(("PASS  " if c else "FAIL  ") + m)
    if not c: fails.append(m)

posted = []
asked = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path='/opt/pw-browsers/chromium')
    pg = b.new_page(viewport={'width': 390, 'height': 844})
    internet = {'on': True}
    def geo(route):
        asked.append(route.request.url)
        if not internet['on']:
            return route.abort()
        q = urllib.parse.parse_qs(urllib.parse.urlparse(route.request.url).query)['name'][0]
        res = GEO if q.lower().startswith('spring') else {"generationtime_ms": 0.1}
        route.fulfill(status=200, content_type='application/json',
                      headers={'Access-Control-Allow-Origin': '*'}, body=json.dumps(res))
    pg.route('https://geocoding-api.open-meteo.com/**', geo)
    pg.route('http://toybox.local/place', lambda r: r.fulfill(status=200, content_type='text/html', body=html))
    def post(route):
        posted.append(route.request.post_data)
        route.fulfill(status=200, content_type='application/json', body='{"ok":1}')
    pg.route('http://toybox.local/p', post)
    pg.goto('http://toybox.local/place')

    pg.type('#c', 'Spring', delay=30)
    pg.wait_for_selector('#s button')
    pg.screenshot(path='/tmp/place_suggest.png')
    items = pg.locator('#s button').all_inner_texts()
    check(len(items) == 3, "three suggestions shown")
    check(items[0] == 'Springfield, Illinois, United States', "suggestion reads name, region, country")
    check(len(asked) <= 2, f"typing is debounced ({len(asked)} lookups for 6 letters)")
    pg.locator('#s button').nth(1).click()
    check(pg.input_value('#c') == 'Springfield', "a pick fills the name")
    check('Missouri' in pg.inner_text('#h'), "the pick is named under the field")
    pg.get_by_role('button', name=re.compile('Save to device')).click()
    pg.wait_for_selector('#ok', state='visible')
    q = urllib.parse.parse_qs(posted[-1])
    check(q['c'] == ['Springfield'] and q['la'] == ['37.21533'] and q['lo'] == ['-93.29824'],
          "a pick sends its coordinates")
    check(q['f'] == ['Springfield, United States'], "and a short label")

    # Edited after picking: the coordinates no longer belong to it.
    pg.fill('#c', 'Springfield East')
    pg.get_by_role('button', name=re.compile('Save to device')).click()
    pg.wait_for_timeout(300)
    q = urllib.parse.parse_qs(posted[-1])
    check('la' not in q and q['c'] == ['Springfield East'], "an edited name is sent as typed")

    # Nothing found.
    pg.fill('#c', '')
    pg.type('#c', 'Qwzx', delay=20)
    pg.wait_for_timeout(700)
    check('No place by that name' in pg.inner_text('#h'), "says when nothing matches")

    # The Sticky's own Wi-Fi: no internet, still saves the typed name.
    internet['on'] = False
    pg.fill('#c', '')
    pg.type('#c', 'Bangkok', delay=20)
    pg.wait_for_timeout(700)
    check('without the internet' in pg.inner_text('#h') and pg.locator('#s button').count() == 0,
          "no internet: no list, a plain hint")
    pg.get_by_role('button', name=re.compile('Save to device')).click()
    pg.wait_for_timeout(300)
    q = urllib.parse.parse_qs(posted[-1])
    check(q == {'c': ['Bangkok']}, "and the typed name still saves")
    pg.screenshot(path='/tmp/place_page.png', full_page=True)
    b.close()
print("\nRESULT:", "ALL PASS" if not fails else f"{len(fails)} FAILED")
