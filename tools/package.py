#!/usr/bin/env python3
"""Packages the dist: dist/DisplayManager-<ver>/ with DLL + documented INI
+ README + ported-license, plus a zip alongside. Run after tools/build-xp.bat."""
import os
import re
import zipfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ver = re.search(r'DM_VERSION_STRING "([^"]+)"',
                open(os.path.join(root, "src/version.h")).read()).group(1)
dist = os.path.join(root, "dist", f"DisplayManager-{ver}")
mod = os.path.join(dist, "DisplayManager")
os.makedirs(mod, exist_ok=True)
for name in ("DisplayManager.dll",):
    src = os.path.join(root, "build/out", name)
    with open(src, "rb") as f:
        data = f.read()
    with open(os.path.join(mod, name), "wb") as f:
        f.write(data)
    print(f"{name}: {len(data)} bytes")
for name in ("DisplayManager.ini", "README.md", "LICENSE.ported"):
    src = os.path.join(root, name)
    if os.path.exists(src):
        with open(src, "rb") as f:
            data = f.read()
        with open(os.path.join(dist, name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} bytes")
zpath = os.path.join(root, "dist", f"DisplayManager-{ver}.zip")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for base, _, files in os.walk(dist):
        for fn in files:
            full = os.path.join(base, fn)
            z.write(full, os.path.relpath(full, os.path.dirname(dist)))
print(f"zip: {zpath} ({os.path.getsize(zpath)} bytes)")
