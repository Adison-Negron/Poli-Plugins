#!/bin/bash
# Poli-Multiband: skin assets -> aarch64 build + skin -> host test -> release zip -> USB stick (local-plugins/).
# Run from WSL (optional drive letter for the zip: ./build.sh E):  cd /mnt/c/MPC-PLUGINS/Poli-Multiband && ./build.sh
# Install on MPC OS 3.10: plug the Plugin Manager stick in, open the manager, Local tab, install Poli-Multiband, APPLY.
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
ZIP="dist/Poli-Multiband-$VERSION-mpc-aarch64.zip"

# Git on Windows checks the toolchain out with CRLF endings; a "#!/usr/bin/env python3\r" script then fails in Linux and
# Docker ("python3\r: No such file or directory"). Strip CRs, touching only files that have any.
strip_cr() { grep -rlIU --null $'\r' "$@" 2>/dev/null | xargs -0 -r sed -i 's/\r$//' || true; }   # no CRs found is fine
strip_cr --include='*.py' --include='*.sh' --include='*.awk' --include='*.json' "$MPC_VST/tools"
strip_cr --include='*.py' --include='*.conf' --include='*.json' --include='*.sh' generate_skin.py vst/skin_post.py vst/layout.conf vst/*.json test/run_tests.sh presets

echo "=== 0. Factory presets: presets/factory.json -> src/presets.h, the preset list in vst/params.json, PRESETS.md ==="
python3 presets/make_presets.py

echo "=== 1. Skin assets + layout.conf from imgs/ (generate_skin.py) ==="
if python3 -c "import PIL" 2>/dev/null; then
  python3 generate_skin.py
else   # no Pillow in WSL: run it in the same python image the build uses
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/..":/w -w "/w/$(basename "$PWD")" python:3.11-slim sh -c \
    "pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 generate_skin.py"
fi

echo "=== 2. Building aarch64 binary and skin (browser renderer: first run builds a Chromium image) ==="
rm -rf vst/build/skin   # stale skin images from an earlier layout would otherwise ship too
# Expected and handled: "warning: knob r=50 ... 16384 px" is not printed at r=50, but its 128-frame strip is still over 12288 px, so vst/skin_post.py swaps in the 111-frame strip (12210 px)
# and "TOUCH ... overlap by ~8 px" (the mockup's knobs touch; only a sliver between neighbours is shared), and "QLINK Q-Link 16 is on parameter 0" (Band: shown by the tab picture, which is not a control).
"$MPC_VST/tools/build_port.sh" vst/vst.json aarch64

echo "=== 3. Running PC host memory & safety test ==="
"$MPC_VST/tools/test_port.sh" vst/vst.json

echo "=== 3b. Poli-Multiband's own checks: DSP with test tones + mock MPC host (tabs, Q-Links, meter, Pass, presets) ==="
./test/run_tests.sh

echo "=== 4. Packaging release zip ($VERSION) ==="
mkdir -p dist
rm -f dist/Poli-Multiband-*-mpc-aarch64.zip   # keep only this version, so the stick never gets two
sed -i 's/\r$//' vst/build/pluginlist-entry.xml 2>/dev/null || true

python3 "$MPC_VST/tools/release.py" \
  --so vst/build/aarch64/poli_multiband.so \
  --skin "vst/build/skin/Poli - VST - Poli-Multiband" \
  --entry vst/build/pluginlist-entry.xml \
  --version "$VERSION" \
  --about "3-band compressor with downward and upward compression per band" \
  --license MIT \
  --id poli-multiband \
  -o dist

echo "=== 5. Validating package ==="
python3 "$MPC_VST/tools/catalog_check.py" "$ZIP"

copy_to_stick "$ZIP" "Poli-Multiband" || exit 1

echo "All tasks finished successfully!"
