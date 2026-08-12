#!/usr/bin/env python3
"""Fail the build when a function puts too much on the stack.

Everything this firmware does in response to a tap runs on the Arduino loop
task. That task's stack has overflowed twice on real hardware:

  * the phone's file list built an 8,448-byte table as a local, and the device
    restarted the moment a phone connected to the file manager;
  * the sidecar cover builder had a 3,968-byte frame, and the device restarted
    on the first book with a cover beside it.

Both times the symptom was a silent reboot that looked like anything but a
stack, and both times a PC ran the same code without complaint -- a desktop
thread has megabytes. So the check has to be static, and it has to run here.

GCC's -fstack-usage (set in platformio.ini) writes a .su file beside every
object, one line per emitted function:

    path/file.cpp:line:col:signature<TAB>bytes<TAB>static|dynamic|bounded

This reads those, ignores vendored libraries (whose worst offenders are entry
points this firmware never calls), and fails if anything of ours is over the
limit. Frames add up down a call chain, so the limit is well under the task's
own stack: openBook -> a cover build -> a row write is three frames deep
before anything unusual happens.

    python tools/check_stack.py [--limit 2048] [--build .pio/build/sticky]
"""
import argparse
import os
import sys

LIMIT = 2048  # bytes in one function
OURS = ("src/", "toybox-core/")
# Vendored code, judged by what we actually call rather than what it contains.
SKIP = ("lib/", "third_party/", "framework-", ".platformio", "/toolchain")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=LIMIT)
    ap.add_argument("--build", default=".pio/build/sticky")
    ap.add_argument("--top", type=int, default=10, help="how many to list")
    args = ap.parse_args()

    rows = []
    for root, _dirs, files in os.walk(args.build):
        for name in files:
            if not name.endswith(".su"):
                continue
            with open(os.path.join(root, name), errors="replace") as f:
                for line in f:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) < 2:
                        continue
                    where = parts[0]
                    try:
                        size = int(parts[1])
                    except ValueError:
                        continue
                    if any(s in where for s in SKIP):
                        continue
                    if not any(o in where for o in OURS):
                        continue
                    rows.append((size, where))

    if not rows:
        print("check_stack: no .su files found -- is -fstack-usage still in build_flags?")
        return 2

    rows.sort(reverse=True)
    over = [r for r in rows if r[0] > args.limit]
    for size, where in rows[: args.top]:
        mark = "  <-- OVER" if size > args.limit else ""
        print(f"{size:6d}  {where}{mark}")
    if over:
        print(f"\n{len(over)} function(s) over {args.limit} bytes of stack.")
        print("Move the big buffers to static (one at a time is fine -- nothing here")
        print("is reentrant) or to the heap, and run again.")
        return 1
    print(f"\nstack ok ({len(rows)} functions, worst {rows[0][0]} bytes, limit {args.limit})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
