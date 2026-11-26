#!/bin/bash
# Fallback build without Docker (used for the first Shhhhh build, in a cloud workspace where Docker Hub was blocked):
# same steps and flags as build.sh / tools/build_port.sh, with a native Playwright+Chromium for the skin and Ubuntu's
# aarch64 cross compiler (apt install g++-aarch64-linux-gnu; Ubuntu 24.04 = glibc 2.39, the Live III's).
#   MPC_VST=/path/to/mpc-vst-plugins ./build_a64_native.sh
set -euo pipefail
cd "$(dirname "$0")"
MV="${MPC_VST:-$(cd "$(dirname "$0")/../.." && pwd)/mpc-vst-plugins}"
python3 generate_skin.py
rm -rf vst/build/skin
python3 "$MV/tools/gen_vst.py" vst/vst.json
OUT=vst/build/aarch64; mkdir -p "$OUT"
CF="-Isrc -O3 -ffast-math -U__FAST_MATH__"   # -U: no libmvec (glibc vector sinf/logf) dependency
for f in src/ShhDSP.cpp src/engine.cpp; do
  aarch64-linux-gnu-g++ -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu++11 $CF -Ivst/build -I"$MV/wrapper" -c "$f" -o "$OUT/${f//\//_}.o"
done
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu11 -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o "$OUT/vst2_wrap.o"
aarch64-linux-gnu-g++ -O2 -shared -fPIC -fvisibility=hidden "$OUT"/src_*.o "$OUT/vst2_wrap.o" -lm -lstdc++ -lpthread -Wl,--no-undefined -o "$OUT/poli_shhhhh.so"
aarch64-linux-gnu-strip "$OUT/poli_shhhhh.so"
echo "highest glibc: $(readelf -V "$OUT/poli_shhhhh.so" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (device has 2.39)"
