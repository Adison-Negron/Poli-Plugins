#!/bin/bash
# Poli-Multiband offline checks (x86, sanitizers), run by build.sh after the toolchain's host test:
#   test/dsp_test.cpp  the engine alone: flat sum of the bands, both compressor stages, the Freq and Thresh rules,
#                      value text ("500 hz", "1k"), meter, Pass, saved state
#   test/ui_sim.c      the engine inside vst2_wrap.c with a mock MPC: band tabs and the Band Q-Link reload the knobs,
#                      snapped values are reported, the meter moves without UpdateDisplay, Pass
# Needs vst/build/params.h (build_port.sh / test_port.sh write it). Uses the host g++ when there is one, else gcc:12 in Docker.
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$(cd "$(dirname "$0")/../../.." && pwd)/mpc-vst-plugins}"
build='
set -e
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
mkdir -p vst/build/test
g++ $SAN -std=gnu++11 -Isrc -I"$MV/wrapper" test/dsp_test.cpp src/engine.cpp src/MultibandDSP.cpp -lm -o vst/build/test/dsp_test
g++ $SAN -std=gnu++11 -Isrc -Ivst/build -I"$MV/wrapper" -c src/engine.cpp -o vst/build/test/e.o
g++ $SAN -std=gnu++11 -Isrc -c src/MultibandDSP.cpp -o vst/build/test/d.o
gcc $SAN -std=gnu11 -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o vst/build/test/w.o
gcc $SAN -std=gnu11 -Ivst/build -c test/ui_sim.c -o vst/build/test/u.o
g++ $SAN vst/build/test/e.o vst/build/test/d.o vst/build/test/w.o vst/build/test/u.o -lm -lpthread -o vst/build/test/ui_sim
vst/build/test/dsp_test
vst/build/test/ui_sim
'
if command -v g++ >/dev/null && command -v gcc >/dev/null; then
  MV="$MPC_VST" bash -c "$build"
else
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w -e MV=/mv gcc:12 bash -c "$build"
fi
