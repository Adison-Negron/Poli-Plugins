#!/bin/bash
# Poli-Ess offline tests (run from anywhere; build.sh runs them after the host test):
#   dsp_test  - the engine alone: response, reduction, Diff, meter, silence, clicks/zipper, garbage values, CPU
#   host_sim  - through the real wrapper (vst2_wrap.c) driven like MPC: Q-Link/fine moves, Diff screen sync,
#               value texts, meter reports, save + reload
# Both are built with AddressSanitizer + UBSan. Uses the local g++ if there is one, else the gcc Docker image.
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$(cd .. && pwd)/mpc-vst-plugins}"
W="$MPC_VST/wrapper"
python3 "$MPC_VST/tools/gen_vst.py" vst/vst.json --params-h >/dev/null   # vst/build/params.h for the wrapper

OUT=test/build; mkdir -p "$OUT"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
build_and_run() {
  g++ -std=gnu++14 $SAN -Isrc -I"$W" test/dsp_test.cpp src/*.cpp -o "$OUT/dsp_test"
  g++ -std=gnu++14 $SAN -Isrc -I"$W" -c src/DeEsserEngine.cpp -o "$OUT/dsp.o"
  g++ -std=gnu++14 $SAN -Isrc -I"$W" -c src/engine.cpp -o "$OUT/engine.o"
  gcc -std=gnu11 $SAN -w -Ivst/build -I"$W" -c "$W/vst2_wrap.c" -o "$OUT/wrap.o"
  gcc -std=gnu11 $SAN -w -c test/host_sim.c -o "$OUT/host_sim.o"
  g++ $SAN "$OUT/dsp.o" "$OUT/engine.o" "$OUT/wrap.o" "$OUT/host_sim.o" -lm -lpthread -o "$OUT/host_sim"
  echo "=== dsp_test";  "$OUT/dsp_test"
  echo "=== host_sim";  "$OUT/host_sim"
}
if command -v g++ >/dev/null && command -v gcc >/dev/null; then
  build_and_run
else
  PLUG="$PWD"
  docker run --rm -u "$(id -u):$(id -g)" -v "$PLUG":/p -v "$MPC_VST":/mv -w /p -e MPC_VST=/mv gcc:13 bash -c "$(declare -f build_and_run); W=/mv/wrapper OUT=test/build SAN='$SAN' build_and_run"
fi
echo "ALL TESTS PASSED"
