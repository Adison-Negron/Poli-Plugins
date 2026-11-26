#!/usr/bin/env python3
"""Checks PoliPlug's skin merge against real installed skins, offline.

    python3 test/render_installed.py <out dir> <skin folder>...      (each "<vendor> - VST - <name>" folder, as on the MPC)

Builds a fake MPC: every skin folder in Synths/ (shared folders such as "AW Skin" next to them, merged), each with a
stand-in x86 .so (discovery only reads the ELF header), an MPC.settings registering them, and PoliPlug's own folder.
Loads PoliPlug once (test/build/discover_only), so it finds them and rewrites its TUI.json, then for every hosted page:
  - draws it as MPC would on its own, and as PoliPlug's view of it, and compares the two pictures;
  - lists images that don't resolve, component types the picture can't draw, and the merged skin's size.
Needs test/run_tests.sh to have run once (the x86 poli_plug.so and a stand-in .so).
"""
import json, os, random, re, shutil, subprocess, sys
from PIL import Image, ImageChops, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import preview as P  # noqa: E402

B = os.path.join(HERE, "build")
HDR = open(os.path.join(HERE, "..", "src", "pp_gen.h")).read()
num = lambda n: int(re.search(r"#define %s (\d+)" % n, HDR).group(1))
PI_VIEW, PI_EDITING = num("PI_VIEW"), num("PI_EDITING")
_m = re.search(r"PI_E\(n\) \(\(n\) < (\d+) \? (\d+) \+ \(n\) : (\d+) \+ \(n\) - \d+\)", HDR)
E_LO, E0, E1 = (int(g) for g in _m.groups())


def PE(n):
    """PoliPlug's proxy parameter for the hosted effect's parameter n (two runs, see pp_gen.h)"""
    return E0 + n if n < E_LO else E1 + n - E_LO
missing, undrawn = set(), {}
STATES = int(os.environ.get("STATES", "6"))
SO_LIST = dict(l.split("\t") for l in open(os.environ["SO_LIST"]).read().splitlines() if "\t" in l) if os.environ.get("SO_LIST") else {}


def tab_comps(pd, defs, tab):
    if "componentDefinition" in tab:
        return tab["componentDefinition"]["componentsData"]
    return defs[tab["componentName"]]["componentsData"]


def draw_page(skin, pd, comps, state):
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
    im = Image.new("RGB", (P.W, P.H), (0, 0, 0))
    orig = Image.open

    def safe_open(p, *a):
        if not os.path.exists(p):
            missing.add(os.path.relpath(p, skin) if not p.startswith("/") else p)
            return Image.new("RGBA", (1, 1), (0, 0, 0, 0))
        return orig(p, *a)
    P.Image.open = safe_open
    try:
        P.draw(skin, defs, comps, state, {}, im, ImageDraw.Draw(im))
    finally:
        P.Image.open = orig
    return im


def main(out, folders):
    os.makedirs(out, exist_ok=True)
    root = os.path.join(out, "mpc")
    shutil.rmtree(root, ignore_errors=True)
    syn = os.path.join(root, "Synths")
    os.makedirs(syn)
    stand_in = os.path.join(B, "Synths", "Poli - VST - Poli-Ess", "poli_ess.so")
    entries = []
    for f in folders:
        name = os.path.basename(f.rstrip("/"))
        vendor, _, pname = name.partition(" - VST - ")
        shutil.copytree(f, os.path.join(syn, name))
        par = os.path.dirname(f.rstrip("/"))
        for side in os.listdir(par):   # shared art next to the plugin ("AW Skin"): merged, as MPC installs it once
            sp = os.path.join(par, side)
            if os.path.isdir(sp) and " - VST - " not in side:
                shutil.copytree(sp, os.path.join(syn, side), dirs_exist_ok=True)
        real = SO_LIST.get(pname)   # a real x86 build of it (test/build_hosted.py), else a stand-in
        so = os.path.join(syn, name, os.path.basename(real) if real else "stand_in.so")
        shutil.copy2(real or stand_in, so)
        entries.append('<PLUGIN name="%s" format="VST" category="Effect" manufacturer="%s" file="%s" isInstrument="0"/>'
                       % (pname, vendor, so))
    plug = os.path.join(root, "plug")
    shutil.copytree(os.path.join(HERE, "..", "vst", "build", "skin", "Poli - VST - PoliPlug", "Plugin Skins"),
                    os.path.join(plug, "Plugin Skins"))
    shutil.copy2(os.path.join(B, "plug", "poli_plug.so"), plug)
    esc = lambda s: s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
    sett = os.path.join(root, "MPC.settings")
    open(sett, "w").write(
        '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n'
        '  <VALUE name="SynthContentLocations"><Locations><Location>%s/</Location></Locations></VALUE>\r\n'
        '  <VALUE name="pluginList-arm-64bit" val="%s"/>\r\n</PROPERTIES>\r\n'
        % (syn, esc('<?xml version="1.0"?>\r\n<KNOWNPLUGINS>\r\n' + "\r\n".join(entries) + "\r\n</KNOWNPLUGINS>")))
    env = dict(os.environ, PP_SETTINGS=sett, PP_NO_SCAN="1", ASAN_OPTIONS="detect_leaks=0")
    r = subprocess.run([os.path.join(B, "discover_only"), os.path.join(plug, "poli_plug.so")], env=env,
                       capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip()[-2000:])
    ours = os.path.join(plug, "Plugin Skins")
    tui = json.load(open(os.path.join(ours, "TUI.json")))
    size = os.path.getsize(os.path.join(ours, "TUI.json"))
    reg = [l.split("\t") for l in open(os.path.join(ours, "pp_registry.txt")).read().splitlines() if l]
    rack = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    page = {d["key"]: d["value"] for d in rack}[tui["pageData"]["tabs"][0]["componentName"]]["componentsData"]
    print("merged TUI.json: %.1f MB, %d components on the page, %d definitions, imports %s"
          % (size / 1e6, len(page), len(rack), tui["pageData"]["componentDefinitions"]["importFiles"]))
    found = {row[3] for row in reg}
    for f in folders:
        pname = os.path.basename(f.rstrip("/")).partition(" - VST - ")[2]
        if pname not in found:
            print("NOT HOSTED: %s" % pname)
    report = []
    for v0, nv, _, pname in reg:
        v0, nv = int(v0), int(nv)
        folder = [f for f in folders if f.rstrip("/").endswith(" - VST - " + pname)][0]
        hs = os.path.join(syn, os.path.basename(folder.rstrip("/")), "Plugin Skins")
        ht = json.load(open(os.path.join(hs, "TUI.json")))["pageData"]
        hdefs = {d["key"]: d["value"] for d in ht["componentDefinitions"]["localComponentDefinitions"]}
        sigs = [json.dumps(tab_comps(ht, hdefs, t), sort_keys=True) for t in ht["tabs"][:nv]]
        for ti, tab in enumerate(ht["tabs"][:nv]):
            shown = sigs.index(sigs[ti])   # a page drawn like an earlier one is merged once and shown on that view
            missing.clear()
            # states: all at 0, then random values for every parameter the page switches on (IndexedEnabling)
            cset = {}
            for i, n, q in re.findall(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)", json.dumps(tab_comps(ht, hdefs, tab)) + json.dumps(hdefs)):
                cset.setdefault(int(q), set()).add(int(i))
            rnd = random.Random(1)
            states = [{}] + [{q: rnd.choice(sorted(v) + [0]) for q, v in cset.items()} for _ in range(STATES)]
            pct, bbox, miss_a, miss_b = 0, None, set(), set()
            for k, sa in enumerate(states):
                for q in range(256):
                    sa[("v", q)] = rnd.random() if k else 0.5
                sb = {(("v", PE(q[1])) if isinstance(q, tuple) else PE(q)): x for q, x in sa.items()}
                sb.update({PI_VIEW: v0 + shown, PI_EDITING: 1})
                missing.clear()
                a = draw_page(hs, ht, tab_comps(ht, hdefs, tab), sa)
                miss_a |= missing
                missing.clear()
                b = draw_page(ours, tui["pageData"], page, sb)
                miss_b |= missing
                d = ImageChops.difference(a, b).convert("L").point(lambda x: 255 if x > 24 else 0)
                d.paste(0, (1160, 0, 1280, 34))   # PoliPlug's own < RACK strip
                if d.getbbox():
                    bbox = d.getbbox()
                    pct = max(pct, 100.0 * sum(d.histogram()[255:]) / (P.W * P.H))
            tag = "%s_%d" % (re.sub(r"\W+", "_", pname), ti)
            side = Image.new("RGB", (P.W, P.H * 2 + 8), (255, 0, 255))
            side.paste(a, (0, 0)); side.paste(b, (0, P.H + 8))
            side.resize((P.W // 2, (P.H * 2 + 8) // 2)).save(os.path.join(out, tag + ".png"))
            ncomp = len(tab_comps(ht, hdefs, tab))
            report.append((pname, ti, tab.get("tabName"), ncomp, pct, bbox, sorted(miss_a)[:3], sorted(miss_b)[:3]))
    print("\n%-16s %-3s %-12s %6s %7s  %-22s %s" % ("plugin", "tab", "name", "comps", "diff%", "diff box", "missing (alone | in PoliPlug)"))
    for pname, ti, tn, nc, pct, bbox, ma, mb in report:
        print("%-16s %-3d %-12s %6d %6.2f%%  %-22s %s | %s" % (pname, ti, tn, nc, pct, bbox, ma, mb))
    # identical hosted pages (sub-tabs that only change the Q-Link bank) duplicated in the merged skin
    for v0, nv, _, pname in reg:
        folder = [f for f in folders if f.rstrip("/").endswith(" - VST - " + pname)][0]
        ht = json.load(open(os.path.join(folder, "Plugin Skins", "TUI.json")))["pageData"]
        hdefs = {d["key"]: d["value"] for d in ht["componentDefinitions"]["localComponentDefinitions"]}
        sig = [json.dumps(tab_comps(ht, hdefs, t), sort_keys=True) for t in ht["tabs"]]
        if len(sig) > len(set(sig)):
            print("same-looking pages: %s has %d tabs, %d distinct (merged once each)" % (pname, len(sig), len(set(sig))))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
