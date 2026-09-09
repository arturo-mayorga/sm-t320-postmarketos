#!/usr/bin/env python3
"""Regenerate the kernel patch's three new-file sections from draft/.

The six in-tree edits (dsi_cfg.c, qcom-msm8974.dtsi, qcom.yaml, panel Kconfig,
panel Makefile, dts Makefile) are stable and copied through untouched.
"""
import hashlib, re, sys, pathlib

PATCH = pathlib.Path("pmaports/device/testing/linux-postmarketos-qcom-msm8974/"
                     "0001-add-samsung-mondrianwifi.patch")
NEW_FILES = {
    "drivers/gpu/drm/panel/panel-samsung-r63319.c":
        "draft/panel-samsung-r63319.c",
    "arch/arm/boot/dts/qcom/qcom-msm8974-samsung-mondrianwifi.dts":
        "draft/qcom-msm8974-samsung-mondrianwifi.dts",
    "Documentation/devicetree/bindings/display/panel/samsung,r63319.yaml":
        "draft/bindings/samsung,r63319.yaml",
}

text = PATCH.read_text()
# split into per-file sections on the '--- a/' boundary
parts = re.split(r"(?m)^(?=--- a/)", text)
out = []
for part in parts:
    if not part.strip():
        continue
    path = part.split("\n", 1)[0][len("--- a/"):].strip()
    src = NEW_FILES.get(path)
    if src is None:
        out.append(part)          # in-tree edit, keep verbatim
        continue
    body = pathlib.Path(src).read_text()
    lines = body.split("\n")
    if lines and lines[-1] == "":
        lines.pop()               # trailing newline, not a line
    hunk = "".join("+" + l + "\n" for l in lines)
    out.append(f"--- a/{path}\n+++ b/{path}\n"
               f"@@ -0,0 +1,{len(lines)} @@\n{hunk}")
    print(f"  regenerated {path}  ({len(lines)} lines from {src})")

PATCH.write_text("".join(out))
print(f"wrote {PATCH}")

# abuild verifies sha512 of every non-URL source, so any edit to the patch or
# the kernel config must be reflected in APKBUILD or the build dies before it
# even unpacks. Doing this by hand is how we lost a build cycle.
APKBUILD = PATCH.parent / "APKBUILD"
a = APKBUILD.read_text()
for f in ("config-postmarketos-qcom-msm8974.armv7",
          PATCH.name):
    digest = hashlib.sha512((PATCH.parent / f).read_bytes()).hexdigest()
    a, n = re.subn(rf"(?m)^[0-9a-f]{{128}}(  {re.escape(f)})$",
                   digest + r"\1", a)
    if n != 1:
        sys.exit(f"APKBUILD: expected 1 checksum line for {f}, found {n}")
    print(f"  checksum {f} -> {digest[:16]}...")
APKBUILD.write_text(a)

# pkgrel must move or apk silently reuses the cached build and the change is
# invisible on the device.
m = re.search(r"(?m)^pkgrel=(\d+)$", a)
print(f"NOTE: pkgrel is currently {m.group(1)} - bump it if this is a rebuild")
