#!/bin/bash
# PoliPlug: mockup -> params, layout and skin -> aarch64 build -> host tests -> release zip -> USB stick.
# Run from WSL (optional drive letter for the zip: ./build.sh E):  cd /mnt/c/MPC-PLUGINS/PoliPlug && ./build.sh
# Nothing is bundled: on the MPC, PoliPlug finds the installed effects and adds their skins to its own (src/discover.c).
# The offline tests use ../Poli-ess as the "installed" effect (built first with its own build.sh).
# Install on MPC OS 3.10: plug the Plugin Manager stick in, open the manager, Local tab, install PoliPlug, APPLY.
set -euo pipefail
cd "$(dirname "$0")"

MPC_VST="${MPC_VST:-$(cd "$(dirname "$0")/../.." && pwd)/mpc-vst-plugins}"   # a checkout of open-mpc/mpc-vst-plugins beside this repo
# Where the zip goes: ./build.sh E (or USB_DRIVE=E ./build.sh); with neither it asks (Enter = last used). See
# ../shared/usb_stick.sh. "./build.sh none" builds without copying.
source "../shared/usb_stick.sh"
pick_drive "$@"
export MPC_VST

VERSION=$(python3 -c "import json; v=json.load(open('vst/vst.json'))['version']; print('%d.%d.%d' % (v//1000, v//100%10, v//10%10))")
ZIP="dist/PoliPlug-$VERSION-mpc-aarch64.zip"

# Git on Windows checks files out with CRLF; a "#!/usr/bin/env python3\r" script then fails in Linux and Docker.
strip_cr() { grep -rlIU --null $'\r' "$@" 2>/dev/null | xargs -0 -r sed -i 's/\r$//' || true; }   # no CRs found is fine
strip_cr --include='*.py' --include='*.sh' --include='*.awk' --include='*.json' "$MPC_VST/tools"
strip_cr --include='*.py' --include='*.conf' --include='*.json' --include='*.sh' --include='*.c' --include='*.h' \
    generate.py build.sh vst src test

echo "=== 1. Params, layout and skin assets (generate.py) ==="
if python3 -c "import PIL" 2>/dev/null; then
  python3 generate.py
else   # no Pillow in WSL: run it in the same python image the build uses (the toolchain is mounted for its font)
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/..":/w -w /w/PoliPlug -e MPC_VST=/w/mpc-vst-plugins python:3.11-slim sh -c \
    "pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 generate.py"
fi

echo "=== 2. Building aarch64 binary and the rack skin (skin_post.py keeps it as pp_base.json) ==="
rm -rf vst/build/skin
# Expected and harmless: 16 "QLINK Q-Link n is on parameter .., which no control on this page shows" warnings: the
# Q-Links drive the plugin that was opened (proxies q1..q16), which has no control of its own on the rack page.
"$MPC_VST/tools/build_port.sh" vst/vst.json aarch64

echo "=== 3. Host tests (x86, sanitizers) ==="
if command -v gcc >/dev/null && command -v g++ >/dev/null; then
  # the toolchain's host test, then a fake MPC with Poli-Ess installed: PoliPlug finds it, rebuilds its skin and hosts
  # it (rack, effect list, proxies, audio chain, live meter, saved state)
  test/run_tests.sh
else
  "$MPC_VST/tools/test_port.sh" vst/vst.json
  echo "NOTE: no gcc/g++ in WSL, so the chain test (test/run_tests.sh) was skipped: sudo apt install build-essential"
fi

echo "=== 4. Packaging release zip ($VERSION) ==="
mkdir -p dist
rm -f dist/PoliPlug-*-mpc-aarch64.zip
sed -i 's/\r$//' vst/build/pluginlist-entry.xml 2>/dev/null || true
python3 "$MPC_VST/tools/release.py" \
  --so vst/build/aarch64/poli_plug.so \
  --skin "vst/build/skin/Poli - VST - PoliPlug" \
  --entry vst/build/pluginlist-entry.xml \
  --version "$VERSION" \
  --about "Effect rack: up to 16 of your installed effects in series plus a parallel section, each with its own skin and presets" \
  --license MIT \
  --id poliplug \
  -o dist

echo "=== 5. Validating package ==="
python3 "$MPC_VST/tools/catalog_check.py" "$ZIP"

copy_to_stick "$ZIP" "PoliPlug" || exit 1

echo "All tasks finished successfully!"
