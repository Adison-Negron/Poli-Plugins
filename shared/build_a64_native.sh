#!/bin/bash
# Build any Poli plugin without Docker (the cloud-workspace path; build.sh in each plugin folder is the normal WSL + Docker
# pipeline and does the same steps): skin with a native Playwright/Chromium, the .so with Ubuntu's aarch64 cross compiler
# (apt install g++-aarch64-linux-gnu), then the release zip and the catalog check.
#   MPC_VST=/path/to/mpc-vst-plugins shared/build_a64_native.sh <plugin folder> "<about text>" <id> [generate script]
# The plugin's own generate step (generate_skin.py / generate.py, presets) must have run first, or pass it as 4th arg.
set -euo pipefail
PLUG="$(cd "$1" && pwd)"; ABOUT="$2"; ID="$3"; GEN="${4:-}"
MV="${MPC_VST:-$(cd "$(dirname "$0")/../mpc-vst-plugins" && pwd)}"
cd "$PLUG"
[ -n "$GEN" ] && python3 "$GEN" >/dev/null
rm -rf vst/build/skin
GL="$(mktemp)"
if ! python3 "$MV/tools/gen_vst.py" vst/vst.json > "$GL" 2>&1; then cat "$GL"; echo "gen_vst failed"; exit 1; fi
grep -v "overlap by\|QLINK Q-Link\|16384 px image limit" "$GL" || true; rm -f "$GL"
eval "$(python3 "$MV/tools/gen_vst.py" vst/vst.json --shell)"
OUT=vst/build/aarch64; mkdir -p "$OUT"; rm -f "$OUT"/*.o
OBJS=""
for f in $SOURCES; do
  o="$OUT/$(echo "$f" | tr / _).o"
  case "$f" in
    *.cpp) aarch64-linux-gnu-g++ -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu++11 $CFLAGS \
             -Ivst/build -I"$MV/wrapper" -c "$f" -o "$o" ;;
    *)     aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu11 $CFLAGS \
             -Ivst/build -I"$MV/wrapper" -c "$f" -o "$o" ;;
  esac
  OBJS="$OBJS $o"
done
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu11 -Ivst/build \
  -c "$MV/wrapper/vst2_wrap.c" -o "$OUT/vst2_wrap.o"
case "$SOURCES" in
  *.cpp*) LINK=aarch64-linux-gnu-g++ ;;
  *) LINK=aarch64-linux-gnu-gcc ;;
esac
$LINK -O2 -shared -fPIC -fvisibility=hidden $OBJS "$OUT/vst2_wrap.o" $LIBS -lpthread -Wl,--no-undefined -o "$OUT/$SO"
aarch64-linux-gnu-strip "$OUT/$SO"
echo "exported: $(readelf --dyn-syms -W "$OUT/$SO" | grep -E ' GLOBAL .* [0-9]+ [A-Za-z]' | grep -v UND | awk '{print $8}' | tr '\n' ' ')"
echo "highest glibc: $(readelf -V "$OUT/$SO" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (device has 2.39)"
if readelf -d "$OUT/$SO" | grep -q libmvec; then echo "ERROR: $SO needs libmvec"; exit 1; fi
VERSION=$(python3 -c "import json; v=json.load(open('vst/vst.json'))['version']; print('%d.%d.%d' % (v//1000, v//100%10, v//10%10))")
VENDOR=$(python3 -c "import json; print(json.load(open('vst/vst.json'))['vendor'])")
mkdir -p dist
rm -f dist/"$NAME"-*-mpc-aarch64.zip
sed -i 's/\r$//' vst/build/pluginlist-entry.xml 2>/dev/null || true
python3 "$MV/tools/release.py" --so "$OUT/$SO" --skin "vst/build/skin/$VENDOR - VST - $NAME" \
  --entry vst/build/pluginlist-entry.xml --version "$VERSION" --about "$ABOUT" --license MIT --id "$ID" -o dist
python3 "$MV/tools/catalog_check.py" dist/"$NAME-$VERSION-mpc-aarch64.zip"
