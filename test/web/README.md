# Phone-page tests

The captive-portal pages are real UI with real logic (input limits, warnings,
the payload that reaches NVS), so they are checked in a browser rather than by
eye. The page source is extracted straight out of the firmware header, so the
test cannot drift from what the device actually serves.

```bash
python3 - <<'PY'
import re
src = open('../../src/tools/picker_web.h').read()
html = re.search(r'R"HTML\((.*?)\)HTML"', src, re.S).group(1)
open('/tmp/picker_page.html','w').write(html.replace('__MAXI__','10').replace('__MAXL__','20'))
PY
python3 serve_picker_page.py &     # stands in for the device: /items and /save
python3 test_picker_page.py        # drives Chromium at 390px
```

`serve_picker_page.py` mimics the device's own truncation rules, so the count
the page reports back is the count the firmware would have stored.
