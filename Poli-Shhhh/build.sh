#!/bin/bash
# Shhhhh: skin assets -> aarch64 build + skin -> host test -> release zip -> USB stick (local-plugins/).
# Run from WSL (optional drive letter for the zip: ./build.sh E):  cd /mnt/c/MPC-PLUGINS/Poli-Shhhh && ./build.sh
# Install on MPC OS 3.10: plug the Plugin Manager stick in, open the manager, Local tab, install Shhhhh, APPLY.
set -euo pipefail
cd "$(dirname "$0")"

# Path to Open-MPC tools
MPC_VST="${MPC_VST:-$(cd "$(dirname "$0")/../.." && pwd)/mpc-vst-plugins}"   # a checkout of open-mpc/mpc-vst-plugins beside this repo
# Where the zip goes: ./build.sh E (or USB_DRIVE=E ./build.sh); with neither it asks (Enter = last used). See
# ../shared/usb_stick.sh. "./build.sh none" builds without copying.
source "../shared/usb_stick.sh"
pick_drive "$@"

# Release version from vst.json ("version": 1010 -> 1.0.1), so the two never disagree
VERSION=$(python3 -c "import json; v=json.load(open('vst/vst.json'))['version']; print('%d.%d.%d' % (v//1000, v//100%10, v//10%10))")
ZIP="dist/Shhhhh-$VERSION-mpc-aarch64.zip"

# Git on Windows checks the toolchain out with CRLF endings; a "#!/usr/bin/env python3\r" script then fails in Linux and
# Docker ("python3\r: No such file or directory"). Strip CRs, touching only files that have any.
strip_cr() { grep -rlIU --null $'\r' "$@" 2>/dev/null | xargs -0 -r sed -i 's/\r$//' || true; }   # no CRs found is fine
strip_cr --include='*.py' --include='*.sh' --include='*.awk' --include='*.json' "$MPC_VST/tools"
strip_cr --include='*.py' --include='*.conf' --include='*.json' --include='*.sh' generate_skin.py vst/skin_post.py vst/layout.conf vst/*.json test/run_tests.sh

echo "=== 1. Skin assets + layout.conf from imgs/ (generate_skin.py) ==="
if python3 -c "import PIL" 2>/dev/null; then
  python3 generate_skin.py
else   # no Pillow in WSL: run it in the same python image the build uses
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/..":/w -w "/w/$(basename "$PWD")" python:3.11-slim sh -c \
    "pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 generate_skin.py"
fi

echo "=== 2. Building aarch64 binary and skin (browser renderer: first run builds a Chromium image) ==="
rm -rf vst/build/skin   # stale skin images from an earlier layout would otherwise ship too
# Expected: "vst2_wrap.c: render_frames defined but not used" (toolchain). No knobs, so no filmstrip swaps are needed; vst/skin_post.py checks every strip.

"$MPC_VST/tools/build_port.sh" vst/vst.json aarch64

echo "=== 3. Running PC host memory & safety test ==="
"$MPC_VST/tools/test_port.sh" vst/vst.json

echo "=== 3b. Shhhhh's own checks: DSP (latency, Learn, reduction, clicks, state, CPU) + mock MPC host (Learn, nodes, spectrum, Pass/Diff, Q-Links, chunk) ==="
./test/run_tests.sh

echo "=== 4. Packaging release zip ($VERSION) ==="
mkdir -p dist
rm -f dist/Shhhhh-*-mpc-aarch64.zip   # keep only this version, so the stick never gets two
sed -i 's/\r$//' vst/build/pluginlist-entry.xml 2>/dev/null || true

python3 "$MPC_VST/tools/release.py" \
  --so vst/build/aarch64/poli_shhhhh.so \
  --skin "vst/build/skin/Poli - VST - Shhhhh" \
  --entry vst/build/pluginlist-entry.xml \
  --version "$VERSION" \
  --about "Learn-the-noise spectral de-noiser (RX style): Learn, Threshold, Reduction, 6 nodes, Pass, Diff" \
  --license MIT \
  --id poli-shhhhh \
  -o dist

echo "=== 5. Validating package ==="
python3 "$MPC_VST/tools/catalog_check.py" "$ZIP"

copy_to_stick "$ZIP" "Shhhhh" || exit 1

echo "All tasks finished successfully!"
