#!/usr/bin/env bash
# Poli-Juicebox offline tests (x86, ASan + UBSan): the DSP checks and the UI simulation through the real VST2 wrapper.
set -euo pipefail
cd "$(dirname "$0")/.."
MV="${MPC_VST:-$(cd .. && pwd)/mpc-vst-plugins}"
python3 "$MV/tools/gen_vst.py" vst/vst.json --params-h >/dev/null
mkdir -p vst/build/t
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
D="-DHAS_DISPLAY_REV=1"
[ -f vst/build/params.h ] || python3 "$MV/tools/gen_vst.py" vst/vst.json >/dev/null
g++ $SAN -std=gnu++11 -ffast-math -U__FAST_MATH__ -Isrc test/dsp_test.cpp src/JuiceDSP.cpp -o vst/build/t/dsp_test
./vst/build/t/dsp_test
g++ $SAN -std=gnu++11 -ffast-math -U__FAST_MATH__ $D -Isrc -Ivst/build -I"$MV/wrapper" -c src/engine.cpp -o vst/build/t/engine.o
g++ $SAN -std=gnu++11 -ffast-math -U__FAST_MATH__ -Isrc -c src/JuiceDSP.cpp -o vst/build/t/dsp.o
gcc $SAN -std=gnu11 -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o vst/build/t/wrap.o
gcc $SAN -std=gnu11 -Ivst/build -c test/ui_sim.c -o vst/build/t/ui.o
g++ $SAN vst/build/t/*.o -lpthread -o vst/build/t/ui_sim
ASAN_OPTIONS=detect_leaks=0 ./vst/build/t/ui_sim

# release-optimised build: the CPU gate (>20x real time with every module on) and, with numpy, the voice checks on synthetic voices
g++ -O3 -std=gnu++11 -ffast-math -U__FAST_MATH__ -DCPU_GATE -Isrc test/dsp_test.cpp src/JuiceDSP.cpp -o vst/build/t/dsp_test_o3
./vst/build/t/dsp_test_o3 | tail -4
if python3 -c "import numpy" 2>/dev/null; then
  g++ -O2 -std=gnu++11 -ffast-math -U__FAST_MATH__ -Isrc test/juice_cli.cpp src/JuiceDSP.cpp -o vst/build/t/juice_cli
  python3 test/voice_eval.py vst/build/t/juice_cli | tail -25
  python3 test/rasp_eval.py vst/build/t/juice_cli
  python3 test/run_eval.py vst/build/t/juice_cli
else
  echo "(numpy not installed: voice_eval.py skipped)"
fi
