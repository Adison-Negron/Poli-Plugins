#!/usr/bin/env bash
# Poli-Chorus offline tests (x86, ASan + UBSan): the DSP checks and the UI simulation through the real VST2 wrapper.
set -euo pipefail
cd "$(dirname "$0")/.."
MV="${MPC_VST:-$(cd .. && pwd)/mpc-vst-plugins}"
python3 "$MV/tools/gen_vst.py" vst/vst.json --params-h >/dev/null
mkdir -p vst/build/t
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
D="-DHAS_DISPLAY_REV=1 -DDISPLAY_REV_NO_UPDATE=1"
[ -f vst/build/params.h ] || python3 "$MV/tools/gen_vst.py" vst/vst.json >/dev/null
g++ $SAN -std=gnu++11 -ffast-math -U__FAST_MATH__ -Isrc test/dsp_test.cpp src/ChoDSP.cpp -o vst/build/t/dsp_test
./vst/build/t/dsp_test
g++ $SAN -std=gnu++11 -ffast-math -U__FAST_MATH__ $D -Isrc -Ivst/build -I"$MV/wrapper" -c src/engine.cpp -o vst/build/t/engine.o
g++ $SAN -std=gnu++11 -ffast-math -U__FAST_MATH__ -Isrc -c src/ChoDSP.cpp -o vst/build/t/dsp.o
gcc $SAN -std=gnu11 $D -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o vst/build/t/wrap.o
gcc $SAN -std=gnu11 -Ivst/build -c test/ui_sim.c -o vst/build/t/ui.o
g++ $SAN vst/build/t/engine.o vst/build/t/dsp.o vst/build/t/wrap.o vst/build/t/ui.o -lpthread -o vst/build/t/ui_sim
ASAN_OPTIONS=detect_leaks=0 ./vst/build/t/ui_sim
# the same DSP checks at the shipping optimisation level (-O3): the CPU limit
g++ -std=gnu++11 -O3 -ffast-math -U__FAST_MATH__ -Isrc test/dsp_test.cpp src/ChoDSP.cpp -o vst/build/t/dsp_test_o3
./vst/build/t/dsp_test_o3 | grep -E "CPU|FAIL|PASSED|FAILED"
