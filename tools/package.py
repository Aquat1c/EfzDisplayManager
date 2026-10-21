#!/usr/bin/env python3
"""Packages dist/DisplayManager-<ver>/: the DisplayManager/ mod folder (DLL and
INI, copied into the game's mods/ as-is) plus README, docs and licence notices,
and a zip alongside. Run after tools/build-xp.bat."""
import os
import re
import shutil
import zipfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ver = re.search(r'DM_VERSION_STRING "([^"]+)"',
                open(os.path.join(root, "src/version.h")).read()).group(1)
dist = os.path.join(root, "dist", f"DisplayManager-{ver}")
mod = os.path.join(dist, "DisplayManager")
shutil.rmtree(dist, ignore_errors=True)  # never ship leftovers of an older run
os.makedirs(mod)
for src, dst in (("build/out/DisplayManager.dll", mod),
                 ("DisplayManager.ini", mod),
                 ("README.md", dist),
                 ("LICENSE.ported", dist)):
    shutil.copy2(os.path.join(root, src), dst)
shutil.copytree(os.path.join(root, "docs"), os.path.join(dist, "docs"))
zpath = os.path.join(root, "dist", f"DisplayManager-{ver}.zip")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for base, _, files in os.walk(dist):
        for fn in sorted(files):
            full = os.path.join(base, fn)
            z.write(full, os.path.relpath(full, os.path.dirname(dist)))
            print(os.path.relpath(full, dist))
print(f"zip: {zpath} ({os.path.getsize(zpath)} bytes)")
