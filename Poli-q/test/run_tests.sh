#!/bin/bash
# Poli-q offline checks (x86, ASan + UBSan + LeakSanitizer), run by build.sh after the toolchain's host test:
#   test/dsp_test.cpp  the engine alone: filters = the drawn curve, every type's shape, Init transparent, clicks,
#                      garbage values, Q-Links + bank, display values, type/preset browsers, presets, state, the
#                      analyser (screen up / closed / switch), odd block sizes, denormals, CPU
#   test/ui_sim.c      the engine inside vst2_wrap.c with a mock MPC (see its header)
#   bench (-O3 -ffast-math, the device's flags): the same checks, real CPU
# Needs vst/build/params.h (gen_vst.py / test_port.sh write it). Uses the host g++ when there is one, else gcc:12 in Docker.
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$(cd "$(dirname "$0")/../../.." && pwd)/mpc-vst-plugins}"
build='
set -e
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
mkdir -p vst/build/test
g++ $SAN -std=gnu++11 -Isrc -Ivst/build -I"$MV/wrapper" test/dsp_test.cpp src/engine.cpp src/PoliQ.cpp src/Learn.cpp -lm -o vst/build/test/dsp_test
g++ $SAN -std=gnu++11 -Isrc -Ivst/build -I"$MV/wrapper" -c src/engine.cpp -o vst/build/test/e.o
g++ $SAN -std=gnu++11 -Isrc -c src/PoliQ.cpp -o vst/build/test/d.o
g++ $SAN -std=gnu++11 -Isrc -c src/Learn.cpp -o vst/build/test/l.o
gcc $SAN -std=gnu11 -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o vst/build/test/w.o
gcc $SAN -std=gnu11 -Ivst/build -c test/ui_sim.c -o vst/build/test/u.o
g++ $SAN vst/build/test/e.o vst/build/test/d.o vst/build/test/l.o vst/build/test/w.o vst/build/test/u.o -lm -lpthread -o vst/build/test/ui_sim
g++ -O3 -ffast-math -U__FAST_MATH__ -std=gnu++11 -Isrc -Ivst/build -I"$MV/wrapper" test/dsp_test.cpp src/engine.cpp src/PoliQ.cpp src/Learn.cpp -lm -o vst/build/test/bench
vst/build/test/dsp_test
vst/build/test/ui_sim
echo "--- release flags (-O3 -ffast-math): same checks, real CPU ---"
vst/build/test/bench | grep -E "^(FAIL|info CPU|PASSED|FAILED)"
'
if command -v g++ >/dev/null && command -v gcc >/dev/null; then
  MV="$MPC_VST" bash -c "$build"
else
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w -e MV=/mv gcc:12 bash -c "$build"
fi
