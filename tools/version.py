# Stamps the build with what it was built from.
#
# Runs before every compile, so `pio run` and tools/make_image.sh agree. The
# same string ends up on the service screen and in the flasher page's
# version.json, which is the point: you can tell what you are about to install,
# and afterwards you can tell what actually installed.
Import("env")  # noqa: F821  (injected by SCons)

import subprocess


def git(*args, fallback="unknown"):
    try:
        return subprocess.check_output(["git", *args], stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return fallback


commit = git("rev-parse", "--short=7", "HEAD")
# A tag when the commit has one, otherwise the nearest tag and how far past it.
describe = git("describe", "--tags", "--dirty", "--always", fallback=commit)
date = git("log", "-1", "--format=%cd", "--date=format:%d %b %Y", fallback="")

env.Append(CPPDEFINES=[
    ("TB_COMMIT", env.StringifyMacro(commit)),
    ("TB_VERSION", env.StringifyMacro(describe)),
    ("TB_DATE", env.StringifyMacro(date)),
])

print("version: %s (%s) %s" % (describe, commit, date))
