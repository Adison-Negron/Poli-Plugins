#!/bin/bash
# PoliPlug against real installed effects, offline (x86, ASan/UBSan). Optional, after test/run_tests.sh:
#
#   test/real_plugins.sh <out dir> <plugin>...
#
# Each <plugin> is "<skin folder>" or "<skin folder>@<vst.json>":
#   <skin folder>  that effect's "<vendor> - VST - <name>" folder as MPC installs it (a copy from the MPC's Synths folder,
#                  or a port's build/skin/ output); a shared art folder beside it ("AW Skin") comes along;
#   <vst.json>     its port's source, to build a real x86 .so of it (test/build_hosted.py) for the runtime half.
#
#   1. test/render_installed.py: a fake MPC with them installed, PoliPlug merges their skins; every page is drawn on its
#      own and inside PoliPlug in several states and compared pixel by pixel; missing images listed.
#   2. test/host_real_test.c (plugins with a vst.json): added, opened and driven through PoliPlug's proxies, compared with
#      the same plugin on its own (names, values, value texts, audio, pages, saved state).
# MPC_WRAPPER=<wrapper dir> builds the ports against another wrapper checkout (the Airwindows ports need process_float).
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$(cd .. && pwd)/mpc-vst-plugins}"
export MPC_VST
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
OUT="$1"; shift
mkdir -p "$OUT/so"
[ -f test/build/plug/poli_plug.so ] || { echo "run test/run_tests.sh first"; exit 1; }
gcc $SAN -std=gnu11 -Isrc test/discover_only.c -ldl -lm -o test/build/discover_only
gcc $SAN -std=gnu11 -Isrc test/host_real_test.c -ldl -lm -o test/build/host_real_test

skins=(); cfgs=()
for a in "$@"; do skins+=("${a%%@*}"); [[ "$a" == *@* ]] && cfgs+=("${a#*@}"); done
: > "$OUT/so/list.tsv"
[ ${#cfgs[@]} -gt 0 ] && python3 test/build_hosted.py "$OUT/so" "${cfgs[@]}" | grep $'\t' >> "$OUT/so/list.tsv"

echo "=== 1. their skins merged into PoliPlug's ==="
SO_LIST="$OUT/so/list.tsv" python3 test/render_installed.py "$OUT" "${skins[@]}"

echo "=== 2. hosted at run time ==="
M="$OUT/mpc"
args=()
while IFS=$'\t' read -r n p; do
    d=$(ls "$M/Synths" | grep -- "- VST - $n\$")
    args+=("$n=$M/Synths/$d/$(basename "$p")")
done < "$OUT/so/list.tsv"
[ ${#args[@]} -eq 0 ] && { echo "(no vst.json given: skipped)"; exit 0; }
PER_INSTANCE="${PER_INSTANCE:-TONE3000}" PP_SETTINGS="$M/MPC.settings" PP_NO_SCAN=1 ASAN_OPTIONS=detect_leaks=0 \
    test/build/host_real_test "$M/plug/poli_plug.so" "${args[@]}"
