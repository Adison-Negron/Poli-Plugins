#!/bin/bash
# PoliPlug offline tests (x86, ASan/UBSan), run by build.sh after the device build (needs gcc/g++ in WSL):
#   1. the toolchain's generic host test;
#   2. a fake MPC with installed plugins: test/build/Synths/<plugin> (x86 builds of TEST_PLUGINS, default ../Poli-ess,
#      with their built skins), an MPC.settings registering them (plus an instrument, a missing .so and PoliPlug itself,
#      which must all be left out), and PoliPlug's own folder with the skin build_port.sh made;
#   3. test/host_chain_test.c loads that PoliPlug as MPC does: it finds the effects, rebuilds its skin, hosts them;
#   4. checks on the rebuilt TUI.json, and preview pictures of it (test/preview/);
#   5. test/stress_test.c (random use from two threads) and test/bad_inputs.py (broken installs).
set -euo pipefail
export PP_VST2_ROOTS=   # fake MPCs only: never this computer's own mounted "VST2 Plugins" folders
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$(cd .. && pwd)/mpc-vst-plugins}"
export MPC_VST
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
B="$PWD/test/build"
SKIN="vst/build/skin/Poli - VST - PoliPlug"
[ -f "$SKIN/Plugin Skins/pp_base.json" ] || { echo "build the skin first (build.sh step 2)"; exit 1; }
rm -rf "$B"; mkdir -p "$B/Synths" "$B/plug"

echo "=== toolchain host test ==="
PP_NO_SCAN=1 "$MPC_VST/tools/test_port.sh" vst/vst.json

echo "=== a fake MPC: x86 builds of the installed effects ==="
python3 - "$B" ${TEST_PLUGINS:-../Poli-ess} <<'PY'
import json, os, shutil, subprocess, sys
b, plugins = sys.argv[1], sys.argv[2:]
mv = os.environ["MPC_VST"]
san = "-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1".split()
entries = []
for pdir in plugins:
    cfg = json.load(open(os.path.join(pdir, "vst", "vst.json")))
    folder = "%s - VST - %s" % (cfg["vendor"], cfg["name"])
    skin = os.path.join(pdir, "vst", "build", "skin", folder)
    dst = os.path.join(b, "Synths", folder)
    shutil.copytree(skin, dst)
    subprocess.run([sys.executable, os.path.join(mv, "tools", "gen_vst.py"), os.path.join(pdir, "vst", "vst.json"), "--params-h"],
                   check=True, stdout=subprocess.DEVNULL)
    root = os.path.normpath(os.path.join(pdir, "vst", cfg["build"].get("root", ".")))
    inc = ["-I" + os.path.join(pdir, "vst", "build"), "-I" + os.path.join(mv, "wrapper")]
    objs = []
    for i, src in enumerate(cfg["build"]["sources"] + [os.path.join(mv, "wrapper", "vst2_wrap.c")]):
        path = src if os.path.isabs(src) else os.path.join(root, src)
        o = os.path.join(b, "%s_%d.o" % (cfg["so"], i))
        cxx = path.endswith((".cpp", ".cc", ".cxx"))
        cflags = [f if not f.startswith("-I") else "-I" + os.path.join(root, f[2:]) for f in cfg["build"].get("cflags", [])]
        subprocess.run(["g++" if cxx else "gcc"] + san + ["-fPIC", "-fvisibility=hidden", "-std=gnu++11" if cxx else "-std=gnu11"]
                       + cflags + inc + ["-c", path, "-o", o], check=True)
        objs.append(o)
    so = os.path.join(dst, cfg["so"])
    subprocess.run(["g++"] + san + ["-shared"] + objs + cfg["build"].get("libs", []) + ["-lpthread", "-o", so], check=True)
    entries.append('<PLUGIN name="%s" descriptiveName="%s" format="VST" category="%s" manufacturer="%s" version="1" file="%s" '
                   'uid="%s" isInstrument="%d" numInputs="2" numOutputs="2" isShell="0"/>'
                   % (cfg["name"], cfg["name"], "Effect" if cfg.get("effect") else "Synth", cfg["vendor"], so,
                      cfg["uid"].encode().hex(), 0 if cfg.get("effect") else 1))
    print("installed", folder)
# "Wide FX": the same plugin with its skin in the other shape MPC reads (Airwindows ports): the page inline in the tab
# ("componentDefinition"), its images in a shared folder beside the plugins ("../../Shared Skin/"), and parameters past
# 256 (a big plugin like Poli-q, 270+ parameters: 1.3.0 left it out of the list)
import re
src = os.path.join(b, "Synths", "Poli - VST - Poli-Ess")
dst = os.path.join(b, "Synths", "Test - VST - Wide FX")
shared = os.path.join(b, "Synths", "Shared Skin")
os.makedirs(os.path.join(dst, "Plugin Skins")); os.makedirs(shared)
t = json.load(open(os.path.join(src, "Plugin Skins", "TUI.json")))
pd = t["pageData"]
defs = {d["key"]: d for d in pd["componentDefinitions"]["localComponentDefinitions"]}
for tab in pd["tabs"]:
    tab["componentDefinition"] = defs.pop(tab.pop("componentName"))["value"]
    tab["version"] = 1
pd["componentDefinitions"]["localComponentDefinitions"] = list(defs.values())
for f in os.listdir(os.path.join(src, "Plugin Skins")):
    if f.endswith(".png"):
        shutil.copy2(os.path.join(src, "Plugin Skins", f), os.path.join(shared, f))
txt = json.dumps(t)
for f in os.listdir(shared):
    txt = txt.replace('"%s"' % f, '"../../Shared Skin/%s"' % f)
txt = re.sub(r"Parameter (\d+)", lambda m: "Parameter %d" % (int(m.group(1)) + 260), txt)
open(os.path.join(dst, "Plugin Skins", "TUI.json"), "w").write(txt)
shutil.copy2(os.path.join(src, "Plugin Skins", "Q-Links.json"), os.path.join(dst, "Plugin Skins", "Q-Links.json"))
shutil.copy2(so, os.path.join(dst, "wide_fx.so"))
entries.append('<PLUGIN name="Wide FX" format="VST" category="Effect" manufacturer="Test" file="%s" isInstrument="0"/>'
               % os.path.join(dst, "wide_fx.so"))
# "X Presets": a plain VST2 effect with 20 programs and no preset selector of its own (test/preset_fx.c), like a
# third-party plugin: PoliPlug's own preset bar and browser serve it, and MPC's PRESET menu lists its programs
dst = os.path.join(b, "Synths", "Test - VST - X Presets")
shutil.copytree(os.path.join(src, "Plugin Skins"), os.path.join(dst, "Plugin Skins"))
ql = json.load(open(os.path.join(dst, "Plugin Skins", "Q-Links.json")))   # its Q-Link 1 on parameter 200 (> 127: 1.3.0
ql["Screen Mode Q-Links"]["map"][0]["Q-Links"]["Q-Link 1"] = 200          # stored Q-Link targets in a signed char)
json.dump(ql, open(os.path.join(dst, "Plugin Skins", "Q-Links.json"), "w"))
subprocess.run(["gcc"] + san + ["-fPIC", "-shared", "-fvisibility=hidden", "-std=gnu11", "test/preset_fx.c", "-o",
                                os.path.join(dst, "x_presets.so")], check=True)
entries.append('<PLUGIN name="X Presets" format="VST" category="Effect" manufacturer="Test" file="%s" isInstrument="0"/>'
               % os.path.join(dst, "x_presets.so"))
# left out by discovery: an instrument, a missing file, PoliPlug itself
entries += ['<PLUGIN name="Some Synth" format="VST" category="Synth" manufacturer="X" file="%s" isInstrument="1"/>' % so,
            '<PLUGIN name="Gone" format="VST" category="Effect" manufacturer="X" file="/nope/gone.so" isInstrument="0"/>',
            '<PLUGIN name="PoliPlug" format="VST" category="Effect" manufacturer="Poli" file="%s/plug/poli_plug.so" uid="506c5067" isInstrument="0"/>' % b]
esc = lambda s: s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
open(os.path.join(b, "MPC.settings"), "w").write(
    '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n'
    '  <VALUE name="SynthContentLocations"><Locations><Location>%s/Synths/</Location></Locations></VALUE>\r\n'
    '  <VALUE name="pluginList-arm-64bit" val="%s"/>\r\n</PROPERTIES>\r\n'
    % (b, esc('<?xml version="1.0"?>\r\n<KNOWNPLUGINS>\r\n' + "\r\n".join(entries) + "\r\n</KNOWNPLUGINS>")))
PY

echo "=== x86 PoliPlug in its own folder + chain test ==="
cp -r "$SKIN/Plugin Skins" "$B/plug/"
gcc $SAN -Wno-format-truncation -fPIC -fvisibility=hidden -std=gnu11 -Isrc -Ivst/build -I"$MPC_VST/wrapper" -shared \
    src/poliplug.c src/discover.c src/scan.c src/cJSON.c "$MPC_VST/wrapper/vst2_wrap.c" -lm -ldl -lpthread -o "$B/plug/poli_plug.so"
gcc $SAN -std=gnu11 -Isrc test/host_chain_test.c -ldl -lm -o "$B/host_chain_test"
PP_SETTINGS="$B/MPC.settings" PP_NO_SCAN=1 "$B/host_chain_test" "$B/plug/poli_plug.so"

echo "=== the skin PoliPlug rebuilt ==="
python3 - "$B/plug/Plugin Skins" <<'PY'
import json, os, re, sys
d = sys.argv[1]
t = json.load(open(os.path.join(d, "TUI.json")))
s = json.dumps(t)
reg = {l.split("\t")[3]: int(l.split("\t")[0]) for l in open(os.path.join(d, "pp_registry.txt")).read().splitlines() if l}
bad = 0
def check(c, msg):
    global bad
    print(("ok   " if c else "FAIL ") + msg)
    bad += not c
check(set(reg) == {"Wide FX", "Poli-Ess", "X Presets"}, "registry: %s" % reg)
for name, v0 in reg.items():
    check(("IndexedEnabling/%d/96/Parameter 39" % v0) in s, "%s: its page is gated on view %d" % (name, v0))
check("Poli - VST - Poli-Ess/Plugin Skins/sh_bg_0.png\"" in s, "Poli-Ess: images referenced in its own folder")
check("/Shared Skin/sh_bg_0.png\"" in s, "Wide FX: images in a shared folder ('../../Shared Skin/') still found")
missing = [p for p in set(re.findall(r'"(\.\./[^"]+\.png)"', s)) if not os.path.exists(os.path.join(d, p))]
check(not missing, "every relative image resolves from PoliPlug's Plugin Skins (%d missing)" % len(missing))
check("pp_h%d__shKnob" % reg["Poli-Ess"] in s, "Poli-Ess: its knob definition renamed")
check("Parameter 85" in s and "Parameter 89" in s, "Poli-Ess: Parameter 0..4 -> PoliPlug's e0..e4 (85..89)")
pk = [p["key"] for p in json.load(open("vst/params.json"))["params"]]
e260, e264 = pk.index("e260"), pk.index("e264")
check(("Parameter %d" % e260) in s and ("Parameter %d" % e264) in s,
      "Wide FX (an inline page, Parameters 260..264): -> e260..e264 (%d..%d, the 1.3.1 run)" % (e260, e264))
names = [x["key"] for x in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]]
check(len(names) == len(set(names)), "no duplicate definition keys")
sys.exit(bad)
PY
python3 test/preview.py "$B/plug/Plugin Skins" test/preview

echo "=== stress: two threads, random taps, audio of every block size, damaged projects (${STRESS_SECS:-30} s each) ==="
gcc $SAN -std=gnu11 -Isrc test/stress_test.c -ldl -lm -lpthread -o "$B/stress_test"
PP_SETTINGS="$B/MPC.settings" PP_NO_SCAN=1 "$B/stress_test" "$B/plug/poli_plug.so" "${STRESS_SECS:-30}" 1
FILL=1 PP_SETTINGS="$B/MPC.settings" PP_NO_SCAN=1 "$B/stress_test" "$B/plug/poli_plug.so" "${STRESS_SECS:-30}" 2

echo "=== broken things installed: damaged skins, bad plugin files, too many effects, no settings, unwritable skin ==="
gcc $SAN -std=gnu11 -Isrc test/discover_only.c -ldl -lm -o "$B/discover_only"
python3 test/bad_inputs.py "$B/bad"

echo "=== MPC OS 3.10.1+: effects in VST2 Plugins (plugin.json), with and without MPC.settings ==="
PP_VST2_ROOTS= python3 test/vst2_test.py "$B/vst2"
echo "PASSED (all)"
