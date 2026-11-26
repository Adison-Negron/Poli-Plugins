#!/bin/bash
# Poliwow offline checks (x86, sanitizers), run by build.sh after the toolchain's host test:
#   test/dsp_test.cpp  the engine alone: Wow 0 passes audio bit for bit, every preset's build-up (louder top end, wider,
#                      no clipping), tails ring after the drop, silence stays silent, state, arrows, "%" text
#   test/ui_sim.c      the engine inside vst2_wrap.c with a mock MPC: preset arrows, PRESET menu, knob value text,
#                      save + reload (chunk), broken chunks
#   test/stress_test.cpp  the hiccup hunt: 0 dBFS input at Wow 0, back to exact after a build-up, 60 s worst case per
#                      preset, denormals, hostile values, 20000 fuzzed blocks, clicks, NaN safety net, speed (~2 min)
#   test/grain_test.cpp  the 1.2 room granulator: extremes on every preset, Wow 0 untouched, Split / Retrace / Speed / Level
#                      do what they say, Sync locks grains to the tempo grid, Variation moves them off it, CPU
#   test/render_demos.cpp  one 16-bar build-up WAV per preset into test/demo-1.2/ (124 bpm loop with gaps: drums,
#                      chord stabs, a lead), so tails and pads can be heard
#   (test/calibrate.cpp, not run here: per-preset balance of core / cloud / echo / reverb; build with -DPOLIWOW_PROBE)
# Needs vst/build/params.h (build_port.sh / test_port.sh write it). Uses the host g++ when there is one, else gcc:12 in Docker.
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$(cd "$(dirname "$0")/../../.." && pwd)/mpc-vst-plugins}"
build='
set -e
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1"   # any sanitizer hit fails the run
mkdir -p vst/build/test test/demo-1.2
g++ $SAN -std=gnu++11 -Isrc -I"$MV/wrapper" test/dsp_test.cpp src/engine.cpp src/WowDSP.cpp -lm -o vst/build/test/dsp_test
g++ $SAN -std=gnu++11 -Isrc -Ivst/build -I"$MV/wrapper" -c src/engine.cpp -o vst/build/test/e.o
g++ $SAN -std=gnu++11 -Isrc -c src/WowDSP.cpp -o vst/build/test/d.o
gcc $SAN -std=gnu11 -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o vst/build/test/w.o
gcc $SAN -std=gnu11 -Ivst/build -c test/ui_sim.c -o vst/build/test/u.o
g++ $SAN vst/build/test/e.o vst/build/test/d.o vst/build/test/w.o vst/build/test/u.o -lm -lpthread -o vst/build/test/ui_sim
g++ $SAN -fsanitize=float-cast-overflow -std=gnu++11 -Wno-invalid-offsetof -Isrc -I"$MV/wrapper" test/stress_test.cpp src/engine.cpp src/WowDSP.cpp -lm -o vst/build/test/stress_test
g++ $SAN -fsanitize=float-cast-overflow -std=gnu++11 -Wno-invalid-offsetof -Isrc -I"$MV/wrapper" test/grain_test.cpp src/engine.cpp src/WowDSP.cpp -lm -o vst/build/test/grain_test
g++ -O2 -std=gnu++11 -Isrc test/render_demos.cpp src/WowDSP.cpp -lm -o vst/build/test/render_demos
vst/build/test/dsp_test
vst/build/test/ui_sim
vst/build/test/stress_test
vst/build/test/grain_test
vst/build/test/render_demos test/demo-1.2
'
if command -v g++ >/dev/null && command -v gcc >/dev/null; then
  MV="$MPC_VST" bash -c "$build"
else
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w -e MV=/mv gcc:12 bash -c "$build"
fi
