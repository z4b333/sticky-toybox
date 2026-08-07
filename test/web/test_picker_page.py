from playwright.sync_api import sync_playwright
import time

fails = []
def check(cond, msg):
    print(("PASS  " if cond else "FAIL  ") + msg)
    if not cond: fails.append(msg)

with sync_playwright() as p:
    b = p.chromium.launch(executable_path='/opt/pw-browsers/chromium')
    pg = b.new_page(viewport={'width':390,'height':844})
    pg.goto('http://127.0.0.1:8099/', wait_until='networkidle')

    # 1. opens on whatever the device currently holds
    check(pg.input_value('#t') == "Alice\nBob\nCharlie\n", "prefills from /items")
    check('3 of 10 items' in pg.inner_text('#c'), "counter shows 3 of 10")
    check(pg.get_by_role('button', name='Save to device').is_enabled(), "save enabled with items")

    # 2. over-limit warnings
    pg.fill('#t', "\n".join(f"name{i}" for i in range(14)))
    txt = pg.inner_text('#c')
    check('14 of 10' in txt and 'only the first 10' in txt, "warns when over item limit")
    check('bad' in (pg.get_attribute('#c','class') or ''), "over-limit styled as a problem")

    pg.fill('#t', "a"*25 + "\nshort")
    check('trimmed to 20' in pg.inner_text('#c'), "warns when a line is too long")

    # 3. empty list cannot be saved
    pg.fill('#t', "   \n\n  ")
    check('0 of 10 items' in pg.inner_text('#c'), "blank lines are not counted")
    check(pg.get_by_role('button', name='Save to device').is_disabled(), "save disabled when empty")

    # 4. helpers
    pg.fill('#t', "  Zoe  \n\n\nAda\n\nMo\n")
    pg.get_by_role('button', name='Tidy').click()
    check(pg.input_value('#t') == "Zoe\nAda\nMo", "Tidy strips blanks and spaces")
    pg.get_by_role('button', name='Sort A-Z').click()
    check(pg.input_value('#t') == "Ada\nMo\nZoe", "Sort A-Z orders the list")
    pg.get_by_role('button', name='Clear').click()
    check(pg.input_value('#t') == "", "Clear empties the box")

    # 5. save posts a clean payload and confirms
    pg.fill('#t', "  Ana \n\n Ben\nChandra  \n")
    pg.get_by_role('button', name='Save to device').click()
    pg.wait_for_selector('#ok', state='visible')
    sent = open('/tmp/captured.txt').read()
    check(sent == "Ana\nBen\nChandra", "posts trimmed lines, no blanks")
    check('3 items sent' in pg.inner_text('#ok'), "confirms the count from the device")
    check(pg.get_by_role('button', name='Save to device').is_enabled(), "can save again after saving")

    pg.screenshot(path='/tmp/picker_page.png', full_page=True)
    b.close()

print("\nRESULT:", "ALL PASS" if not fails else f"{len(fails)} FAILED")
