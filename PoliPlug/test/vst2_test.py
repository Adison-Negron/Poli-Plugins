#!/usr/bin/env python3
"""PoliPlug on MPC OS 3.10.1+: effects installed in "<drive>/VST2 Plugins/<Vendor> - VST - <Name>/" with a plugin.json
(Akai's folder format), offline (x86, ASan/UBSan):

    python3 test/vst2_test.py <out dir>          (after test/run_tests.sh; uses its x86 builds)

PP_VST2_ROOTS stands in for the mounted drives. Checks: found with no MPC.settings at all; preferred over the old
registered copy of the same effect (listed once); the same effect on two drives listed once (a drive path with a space);
instruments, PoliPlug itself, missing or escaping binaries, no skin, broken plugin.json and loose files left out;
"file" used when there is no file32/file64; the found effect is added and runs audio from its VST2 Plugins copy.
"""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
GOOD = os.path.join(B, "Synths", "Poli - VST - Poli-Ess")
BASE = os.path.join(HERE, "..", "vst", "build", "skin", "Poli - VST - PoliPlug", "Plugin Skins")
fails = 0


def check(c, msg):
    global fails
    print(("ok   " if c else "FAIL ") + msg)
    fails += not c


def vst2(drive, folder, pj, so=True, skin=True):
    """a plugin folder in <drive>/VST2 Plugins: Poli-Ess's x86 build and skin, with the given plugin.json"""
    d = os.path.join(drive, "VST2 Plugins", folder)
    os.makedirs(d)
    if skin:
        shutil.copytree(os.path.join(GOOD, "Plugin Skins"), os.path.join(d, "Plugin Skins"))
    if so:
        shutil.copy2(os.path.join(GOOD, "poli_ess.so"), os.path.join(d, "poli_ess.so"))
    if pj is not None:
        open(os.path.join(d, "plugin.json"), "w").write(pj if isinstance(pj, str) else json.dumps(pj))
    return d


def pj(name, vendor="Poli", **kw):
    j = {"name": name, "manufacturer": vendor, "version": "1.0.0", "format": "vst2", "kind": "effect", "uid": "0x506f6c69",
         "numInputs": 2, "numOutputs": 2, "file64": "poli_ess.so"}
    j.update(kw)
    return {k: v for k, v in j.items() if v is not None}


def settings(root, entries):
    esc = lambda s: s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
    p = os.path.join(root, "MPC.settings")
    open(p, "w").write('<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n'
                       '  <VALUE name="SynthContentLocations"><Locations><Location>%s/Synths/</Location></Locations></VALUE>\r\n'
                       '  <VALUE name="pluginList-arm-64bit" val="%s"/>\r\n</PROPERTIES>\r\n'
                       % (root, esc('<?xml version="1.0"?>\r\n<KNOWNPLUGINS>\r\n' + "\r\n".join(entries) + "\r\n</KNOWNPLUGINS>")))
    return p


def run(root, drives, sett=None, add=False):
    plug = os.path.join(root, "plug")
    shutil.copytree(BASE, os.path.join(plug, "Plugin Skins"))
    shutil.copy2(os.path.join(B, "plug", "poli_plug.so"), plug)
    env = dict(os.environ, PP_SETTINGS=sett or os.path.join(root, "no-such-MPC.settings"), PP_NO_SCAN="1",
               PP_VST2_ROOTS=":".join(drives))
    r = subprocess.run([os.path.join(B, "discover_only"), os.path.join(plug, "poli_plug.so")] + (["add"] if add else []),
                       env=env, capture_output=True, text=True, timeout=300)
    out = r.stdout + r.stderr
    stamp = os.path.join(plug, "Plugin Skins", "pp_stamp.txt")
    skins = re.findall(r"^\d+ \d+ (.*) \d+ \d+$", open(stamp).read(), re.M) if os.path.exists(stamp) else []
    reg = os.path.join(plug, "Plugin Skins", "pp_registry.txt")
    names = [l.split("\t")[3] for l in open(reg).read().splitlines() if l] if os.path.exists(reg) else []
    tui = os.path.join(plug, "Plugin Skins", "TUI.json")
    valid = os.path.exists(tui) and bool(json.load(open(tui)))
    return r.returncode, out, skins, names, valid


out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(B, "vst2"))
shutil.rmtree(out, ignore_errors=True)

print("=== 1. only in VST2 Plugins, no MPC.settings (a fresh 3.10.1 install): found, added, runs ===")
root = os.path.join(out, "only")
drive = os.path.join(root, "usb")
d = vst2(drive, "Poli - VST - Poli-Ess", pj("Poli-Ess"))
rc, o, skins, names, valid = run(root, [drive], add=True)
check(rc == 0 and "ERROR: AddressSanitizer" not in o and "runtime error" not in o, "no crash, no sanitizer report")
check(bool(re.search(r"status: 1 (effects )?found", o)), "status says 1 effect found (%s)" % o.splitlines()[0] if o else "no output")
check(skins == [os.path.realpath(d)], "its pages come from the VST2 Plugins copy: %s" % skins)
m = re.search(r"add Poli-Ess\s+-> added.*audio peak ([0-9.]+)", o)
check(bool(m) and 0 < float(m.group(1)) < 10, "added from the list, opened, and ran audio (%s)" % (m.group(0) if m else o))
check(valid, "wrote a valid skin")

print("=== 2. moved: registered the old way AND in VST2 Plugins: listed once, the VST2 copy ===")
root = os.path.join(out, "moved")
old = os.path.join(root, "Synths", "Poli - VST - Poli-Ess")
shutil.copytree(GOOD, old)
drive = os.path.join(root, "internal")
d = vst2(drive, "Poli - VST - Poli-Ess", pj("Poli-Ess"))
s = settings(root, ['<PLUGIN name="Poli-Ess" format="VST" category="Effect" manufacturer="Poli" file="%s/poli_ess.so" '
                    'uid="506f6c69" isInstrument="0"/>' % old])
rc, o, skins, names, valid = run(root, [drive], s)
check(rc == 0 and bool(re.search(r"status: 1 (effects )?found", o)), "one effect, not two (%s)" % o.strip())
check(skins == [os.path.realpath(d)], "the VST2 Plugins copy wins: %s" % skins)
check(names == ["Poli-Ess"], "registry keeps the same key (vendor + name), so saved racks still match: %s" % names)

print("=== 3. old registrations still work when nothing is in VST2 Plugins (3.10, or not moved yet) ===")
root = os.path.join(out, "legacy")
old = os.path.join(root, "Synths", "Poli - VST - Poli-Ess")
shutil.copytree(GOOD, old)
s = settings(root, ['<PLUGIN name="Poli-Ess" format="VST" category="Effect" manufacturer="Poli" file="%s/poli_ess.so" '
                    'uid="506f6c69" isInstrument="0"/>' % old])
rc, o, skins, names, valid = run(root, [], s)
check(rc == 0 and bool(re.search(r"status: 1 (effects )?found", o)) and skins == [old], "found the old way: %s %s" % (o.strip(), skins))

print("=== 4. junk in VST2 Plugins: only the real effects are listed ===")
root = os.path.join(out, "junk")
drive = os.path.join(root, "drive A")   # a space in the drive path
vst2(drive, "Poli - VST - Poli-Ess", pj("Poli-Ess"))
vst2(drive, "Poli - VST - File Key", pj("File Key", uid="FkFx", file64=None, file="poli_ess.so"))   # only "file": used
vst2(drive, "X - VST - A Synth", pj("A Synth", "X", kind="instrument"))
vst2(drive, "X - VST - No Kind", pj("No Kind", "X", kind=None))            # MPC's default kind is instrument
vst2(drive, "X - VST - Says Instrument", pj("Says Instrument", "X", isInstrument=True))
vst2(drive, "Poli - VST - PoliPlug", pj("PoliPlug", uid="PlPg"))
vst2(drive, "Poli - VST - Renamed Self", pj("Renamed Self", uid="0x506c5067"))
vst2(drive, "X - VST - No Binary", pj("No Binary", "X"), so=False)
vst2(drive, "X - VST - Escapes", pj("Escapes", "X", file64="../Poli - VST - Poli-Ess/poli_ess.so"), so=False)
vst2(drive, "X - VST - Absolute", pj("Absolute", "X", file64=os.path.join(GOOD, "poli_ess.so")), so=False)
vst2(drive, "X - VST - Only 32", pj("Only 32", "X", file64=None, file32="poli_ess.so"))   # no binary for this CPU
vst2(drive, "X - VST - No Skin", pj("No Skin", "X"), skin=False)
vst2(drive, "X - VST - Broken Json", '{"name": "Broken", "manufacturer": ')
vst2(drive, "X - VST - No Json", None)
vst2(drive, "X - VST - Not A Library", pj("Not A Library", "X", file64="plugin.json"))
open(os.path.join(drive, "VST2 Plugins", "loose file.txt"), "w").write("x")
rc, o, skins, names, valid = run(root, [drive])
check(rc == 0 and "runtime error" not in o and "AddressSanitizer" not in o, "no crash, no sanitizer report")
check(sorted(names) == ["File Key", "Poli-Ess"], "listed: %s" % sorted(names))
check(valid, "wrote a valid skin")

print("=== 5. the same effect on two drives (internal + USB): listed once ===")
root = os.path.join(out, "two")
a, b = os.path.join(root, "internal"), os.path.join(root, "usb stick")
vst2(a, "Poli - VST - Poli-Ess", pj("Poli-Ess"))
vst2(b, "Poli - VST - Poli-Ess", pj("Poli-Ess"))
rc, o, skins, names, valid = run(root, [a, b])
check(rc == 0 and bool(re.search(r"status: 1 (effects )?found", o)) and len(skins) == 1, "once: %s %s" % (o.strip(), skins))

print("=== 6. nothing anywhere: says so, no crash ===")
root = os.path.join(out, "none")
os.makedirs(root)
rc, o, skins, names, valid = run(root, [])
check(rc == 0 and ("Can't read MPC.settings" in o or "No installed effects found" in o), "status: %s" % o.strip())

print("=== 7. a skin from another generator: version-1 bounds, no additionalInvalidatingHandles (Argevox Geraet drew over the rack) ===")
root = os.path.join(out, "foreign")
drive = os.path.join(root, "usb")
d = vst2(drive, "Voc - VST - Vocoder", pj("Vocoder", "Voc", uid="VcDr"))
tp = os.path.join(d, "Plugin Skins", "TUI.json")
t = json.load(open(tp))
def strip_keys(x):
    if isinstance(x, dict):
        b = x.get("bounds")
        if isinstance(b, dict) and "componentData" in x:
            b.pop("additionalInvalidatingHandles", None); b.pop("showWhenDataModelInvalid", None)
            b["version"] = 1   # as Argevox Geraet, Voxman, Voxboy, Twenty FX write it: MPC reads no gate in a v1 bounds
        for v in x.values(): strip_keys(v)
    elif isinstance(x, list):
        for v in x: strip_keys(v)
strip_keys(t)
pd = t["pageData"]
defs = {e["key"]: e["value"] for e in pd["componentDefinitions"]["localComponentDefinitions"]}
page = pd["tabs"][0].get("componentDefinition") or defs[pd["tabs"][0]["componentName"]]
page["componentsData"].append({"version": 2, "componentData": {"version": 1, "name": "No bounds", "type": "Label", "data": {}},
                               "handle remapping": {"version": 1, "map": []}})   # can't be gated at all: must be left out
json.dump(t, open(tp, "w"))
vst2(drive, "Poli - VST - Poli-Ess", pj("Poli-Ess"))
rc, o, skins, names, valid = run(root, [drive])
check(rc == 0 and "runtime error" not in o and "AddressSanitizer" not in o and "LeakSanitizer" not in o,
      "no crash, no leak (1.5.2 leaked one array per ungated component)")
check(sorted(names) == ["Poli-Ess", "Vocoder"], "both listed: %s" % sorted(names))
def rack_of(p):
    q = json.load(open(p))["pageData"]
    ds = {e["key"]: e["value"] for e in q["componentDefinitions"]["localComponentDefinitions"]}
    return ds[q["tabs"][0]["componentName"]]["componentsData"]
# gated the way MPC obeys it: the handle in a version-2 bounds (Akai: "Version 2 adds additionalInvalidatingHandles")
gated = lambda c: c.get("bounds", {}).get("version", 0) >= 2 and any(re.match(r"IndexedEnabling/\d+/\d+/Parameter \d+$", h)
                      for h in c.get("bounds", {}).get("additionalInvalidatingHandles", []))
base = rack_of(os.path.join(BASE, "pp_base.json"))
merged = rack_of(os.path.join(root, "plug", "Plugin Skins", "TUI.json"))
extra = sum(not gated(c) for c in merged) - sum(not gated(c) for c in base)
check(extra == 0, "every hosted component is limited to its own page (%d drawn on every page)" % extra)
check(not any(c.get("componentData", {}).get("name") == "No bounds" for c in merged), "a component with no bounds is left out")
check(len(merged) > len(base), "the vocoder's page was merged (%d -> %d components)" % (len(base), len(merged)))

print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
