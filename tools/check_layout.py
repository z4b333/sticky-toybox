#!/usr/bin/env python3
"""The installer must never write where settings or notes live.

Reads partitions_toybox.csv, the offsets docs/index.html hands the flasher,
and the real files in docs/firmware, and fails if:
  * NVS or LittleFS moved from where v1 put them (a v1 device would lose its
    settings or notes on the move to v2),
  * any part the installer writes would overlap NVS or LittleFS,
  * a part does not fit the partition it is written into,
  * the page's offsets disagree with the table.

Run from the repo root (make_image.sh runs it on every release):
    python3 tools/check_layout.py
"""
import csv
import os
import re
import sys

V1_NVS = (0x9000, 0x5000)
V1_LFS = (0xB50000, 0x4B0000)


def table():
    rows = [r for r in csv.reader(l for l in open('partitions_toybox.csv', encoding='utf-8')
                                  if l.strip() and not l.startswith('#'))]
    return {r[0].strip(): (int(r[3], 16), int(r[4], 16)) for r in rows}


def main():
    fail = []
    t = table()
    if t.get('nvs') != V1_NVS:
        fail.append('nvs moved from v1 %s: v1 devices would lose their settings' % str(V1_NVS))
    if t.get('spiffs') != V1_LFS:
        fail.append('LittleFS moved from v1: v1 devices would lose their notes')
    page = open('docs/index.html', encoding='utf-8').read()
    off = {k: int(v, 16) for k, v in re.findall(r'(zh|ko|ja): (0x[0-9a-f]+)', page)}
    base = int(re.search(r'BASE_FONT = (0x[0-9a-f]+)', page).group(1), 16)
    fw = 'docs/firmware/'
    parts = [  # (file, offset, partition it lands in or None for fixed areas)
        ('toybox-boot.bin', 0x0, None, 0x8000),
        ('toybox-parts.bin', 0x8000, None, 0x1000),
        ('toybox-ota0.bin', 0xE000, 'otadata', None),
        ('toybox-app.bin', 0x10000, 'ota_0', None),
        ('base_font.tfp', base, 'base_font', None),
    ]
    for lang in ('zh', 'ko', 'ja'):
        parts.append(('%s_full.tfp' % lang, off.get(lang, -1), '%s_font' % lang, None))
        parts.append(('blank4k.bin', off.get(lang, -1), '%s_font' % lang, None))
    keep = [('NVS', *t['nvs']), ('LittleFS', *t['spiffs'])]
    for name, at, part, room in parts:
        path = fw + name
        if not os.path.exists(path):
            fail.append('%s missing' % path)
            continue
        n = os.path.getsize(path)
        if part:
            if part not in t:
                fail.append('%s: no partition %s' % (name, part))
                continue
            if at != t[part][0]:
                fail.append('%s written at %#x, %s is at %#x' % (name, at, part, t[part][0]))
            room = t[part][1]
        if n > room:
            fail.append('%s is %d bytes, room for %d' % (name, n, room))
        for k, ko, ks in keep:
            if at < ko + ks and ko < at + n:
                fail.append('%s (%#x..%#x) overlaps %s' % (name, at, at + n, k))
    for f in fail:
        print('LAYOUT FAIL:', f)
    if fail:
        sys.exit(1)
    print('layout ok: %d parts, NVS and LittleFS untouched' % len(parts))


if __name__ == '__main__':
    main()
