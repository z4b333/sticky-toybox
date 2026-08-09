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
# Note that `describe` already contains the short hash for an untagged build,
# which is why the hash is not passed into the build on its own: at a tag it
# would be the only thing making a rebuild differ from the image committed at
# that tag, and a release you cannot reproduce from its own tag is not much of
# a release.
# No --dirty: building regenerates the tracked firmware images, so every
# release would be marked dirty by its own output. The commit identifies the
# source, which is the thing worth identifying.
describe = git("describe", "--tags", "--always", fallback=commit)
date = git("log", "-1", "--format=%cd", "--date=format:%d %b %Y", fallback="")

env.Append(CPPDEFINES=[
    ("TB_VERSION", env.StringifyMacro(describe)),
    ("TB_DATE", env.StringifyMacro(date)),
])

print("version: %s (%s) %s" % (describe, commit, date))
