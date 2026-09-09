#!/usr/bin/env python3
"""Regenerate the kernel patch from draft/.

Two kinds of content go into the patch:

  new files      draft/<file>              (NEW_FILES below)
  in-tree edits  draft/tree/<kernel path>  diffed against draft/pristine/<path>

draft/pristine holds untouched copies from the kernel tarball; draft/tree is
the same set of files with our changes applied. Edit draft/tree, run this,
and the hunks are rebuilt by diff(1) -- no more hand-maintained hunks.

    ./regen-patch.py          # rebuild patch, fix APKBUILD checksums
    ./regen-patch.py --bump   # ...and bump pkgrel
"""
import hashlib, re, subprocess, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent
PATCH = ROOT / ("pmaports/device/testing/linux-postmarketos-qcom-msm8974/"
                "0001-add-samsung-mondrianwifi.patch")
PRISTINE = ROOT / "draft/pristine"
TREE = ROOT / "draft/tree"
NEW_FILES = {
    "drivers/gpu/drm/panel/panel-samsung-r63319.c":
        "draft/panel-samsung-r63319.c",
    "arch/arm/boot/dts/qcom/qcom-msm8974-samsung-mondrianwifi.dts":
        "draft/qcom-msm8974-samsung-mondrianwifi.dts",
    "Documentation/devicetree/bindings/display/panel/samsung,r63319.yaml":
        "draft/bindings/samsung,r63319.yaml",
}

out = []

# in-tree edits: every file under draft/tree that differs from draft/pristine
for edited in sorted(TREE.rglob("*")):
    if not edited.is_file():
        continue
    rel = edited.relative_to(TREE)
    orig = PRISTINE / rel
    if not orig.exists():
        sys.exit(f"{rel}: in draft/tree but not in draft/pristine")
    r = subprocess.run(["diff", "-u", "--label", f"a/{rel}",
                        "--label", f"b/{rel}", str(orig), str(edited)],
                       capture_output=True, text=True)
    if r.returncode == 0:
        continue
    if r.returncode != 1:
        sys.exit(f"diff failed for {rel}: {r.stderr}")
    out.append(r.stdout)
    added = sum(1 for l in r.stdout.splitlines() if l.startswith("+") and not l.startswith("+++"))
    removed = sum(1 for l in r.stdout.splitlines() if l.startswith("-") and not l.startswith("---"))
    print(f"  edit {rel}  (+{added} -{removed})")

# new files
for path, src in NEW_FILES.items():
    lines = (ROOT / src).read_text().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    hunk = "".join("+" + l + "\n" for l in lines)
    out.append(f"--- a/{path}\n+++ b/{path}\n"
               f"@@ -0,0 +1,{len(lines)} @@\n{hunk}")
    print(f"  new  {path}  ({len(lines)} lines from {src})")

PATCH.write_text("".join(out))
print(f"wrote {PATCH.relative_to(ROOT)}")

# abuild verifies sha512 of every non-URL source, so any edit to the patch or
# the kernel config must be reflected in APKBUILD or the build dies before it
# even unpacks. Doing this by hand is how we lost a build cycle.
APKBUILD = PATCH.parent / "APKBUILD"
a = APKBUILD.read_text()
for f in ("config-postmarketos-qcom-msm8974.armv7", PATCH.name):
    digest = hashlib.sha512((PATCH.parent / f).read_bytes()).hexdigest()
    a, n = re.subn(rf"(?m)^[0-9a-f]{{128}}(  {re.escape(f)})$", digest + r"\1", a)
    if n != 1:
        sys.exit(f"APKBUILD: expected 1 checksum line for {f}, found {n}")
    print(f"  checksum {f} -> {digest[:16]}...")

# pkgrel must move or apk silently reuses the cached build and the change is
# invisible on the device.
m = re.search(r"(?m)^pkgrel=(\d+)$", a)
rel = int(m.group(1))
if "--bump" in sys.argv:
    a = a.replace(m.group(0), f"pkgrel={rel + 1}")
    print(f"  pkgrel {rel} -> {rel + 1}")
else:
    print(f"NOTE: pkgrel is {rel} - pass --bump (or edit) if this is a rebuild")
APKBUILD.write_text(a)
