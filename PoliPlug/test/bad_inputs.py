#!/usr/bin/env python3
"""PoliPlug with broken things installed on the MPC, offline (x86, ASan/UBSan):

    python3 test/bad_inputs.py <out dir>          (after test/run_tests.sh; uses its x86 builds)

Fake MPCs with damaged skins (not JSON, empty, no page, a missing definition, 9 tabs, a parameter past 383, missing
images, nonsense values), damaged or missing Q-Links, names with & < > ", plugin files that aren't libraries, have no
VSTPluginMain or return no plugin, more effects than PoliPlug lists, more pages than it has, no MPC.settings, a skin
folder it can't write, a damaged registry. Each time PoliPlug must start, say what's wrong, write a valid skin, list
and run the good effects, and refuse the broken ones without crashing.
"""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
GOOD = os.path.join(B, "Synths", "Poli - VST - Poli-Ess")
BASE = os.path.join(HERE, "..", "vst", "build", "skin", "Poli - VST - PoliPlug", "Plugin Skins")
SAN = "-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1".split()
fails = 0


def check(c, msg):
    global fails
    print(("ok   " if c else "FAIL ") + msg)
    fails += not c


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def mpc(root, plugins, settings=True):
    """plugins: [(vendor, name, so_path, skin_fn)]; skin_fn(dir) writes <dir>/Plugin Skins"""
    shutil.rmtree(root, ignore_errors=True)
    syn = os.path.join(root, "Synths")
    os.makedirs(syn)
    entries = []
    for vendor, name, so, skin in plugins:
        d = os.path.join(syn, "%s - VST - %s" % (vendor, name))
        os.makedirs(d)
        if skin:
            skin(d)
        dst = os.path.join(d, os.path.basename(so))
        shutil.copy2(so, dst)
        entries.append('<PLUGIN name="%s" format="VST" category="Effect" manufacturer="%s" file="%s" isInstrument="0"/>'
                       % (esc(name), esc(vendor), esc(dst)))
    plug = os.path.join(root, "plug")
    shutil.copytree(BASE, os.path.join(plug, "Plugin Skins"))
    shutil.copy2(os.path.join(B, "plug", "poli_plug.so"), plug)
    sett = os.path.join(root, "MPC.settings")
    if settings:
        open(sett, "w").write(
            '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n'
            '  <VALUE name="SynthContentLocations"><Locations><Location>%s/</Location></Locations></VALUE>\r\n'
            '  <VALUE name="pluginList-arm-64bit" val="%s"/>\r\n</PROPERTIES>\r\n'
            % (syn, esc('<?xml version="1.0"?>\r\n<KNOWNPLUGINS>\r\n' + "\r\n".join(entries) + "\r\n</KNOWNPLUGINS>")))
    return sett, plug


def run(sett, plug, add=True):
    env = dict(os.environ, PP_SETTINGS=sett, PP_NO_SCAN="1", ASAN_OPTIONS="detect_leaks=0")
    r = subprocess.run([os.path.join(B, "discover_only"), os.path.join(plug, "poli_plug.so")] + (["add"] if add else []),
                       env=env, capture_output=True, text=True, timeout=900)
    out = r.stdout + r.stderr
    crashed = r.returncode != 0 or "ERROR: AddressSanitizer" in out or "runtime error" in out
    return out, crashed


def skin_ok(plug):
    d = os.path.join(plug, "Plugin Skins")
    try:
        t = json.load(open(os.path.join(d, "TUI.json")))
    except Exception as e:
        return "not JSON: %s" % e
    s = json.dumps(t)
    bad = [p for p in set(re.findall(r'"(\.\./[^"]+\.png)"', s)) if not os.path.exists(os.path.join(d, p))
           and "No Images" not in p]   # that one's images really are missing
    return "missing images %s" % bad[:3] if bad else None


# ---- skins ---------------------------------------------------------------------------------------------------------
GT = json.load(open(os.path.join(GOOD, "Plugin Skins", "TUI.json")))


def good(d):
    shutil.copytree(os.path.join(GOOD, "Plugin Skins"), os.path.join(d, "Plugin Skins"))


def writer(text=None, obj=None, qlinks="copy", pngs=True):
    def f(d):
        ps = os.path.join(d, "Plugin Skins")
        os.makedirs(ps)
        if pngs:
            for x in os.listdir(os.path.join(GOOD, "Plugin Skins")):
                if x.endswith(".png"):
                    shutil.copy2(os.path.join(GOOD, "Plugin Skins", x), ps)
        open(os.path.join(ps, "TUI.json"), "w").write(text if text is not None else json.dumps(obj))
        if qlinks == "copy":
            shutil.copy2(os.path.join(GOOD, "Plugin Skins", "Q-Links.json"), ps)
        elif qlinks is not None:
            open(os.path.join(ps, "Q-Links.json"), "w").write(qlinks)
    return f


def variant(fn):
    t = json.loads(json.dumps(GT))
    fn(t)
    return t


def nine_tabs(t):
    t["pageData"]["tabs"] = [dict(t["pageData"]["tabs"][0], fnKeySubIndex=i) for i in range(9)]


def missing_def(t):
    t["pageData"]["tabs"][0]["componentName"] = "NoSuchPage"


def too_big(t):
    s = json.dumps(t).replace('"Parameter 1"', '"Parameter 400"')
    t.clear(); t.update(json.loads(s))


def weird(t):
    pd = t["pageData"]
    page = [d for d in pd["componentDefinitions"]["localComponentDefinitions"] if d["key"] == pd["tabs"][0]["componentName"]][0]
    page["value"]["componentsData"] += [7, "x", None, [], {"bounds": 5}, {"componentData": {"type": 3}}]
    pd["componentDefinitions"]["localComponentDefinitions"].append({"key": 5})
    pd["componentDefinitions"]["importFiles"] = [3, "../nope.json", None]


def lib(name, code):
    src = os.path.join(B, name + ".c")
    so = os.path.join(B, name + ".so")
    open(src, "w").write(code)
    subprocess.run(["gcc"] + SAN + ["-shared", "-fPIC", src, "-o", so], check=True)
    return so


def main(out):
    so = os.path.join(GOOD, "poli_ess.so")
    nomain = lib("bad_nomain", "int nothing(void) { return 0; }\n")
    nullmain = lib("bad_nullmain", "void *VSTPluginMain(void *m) { (void)m; return 0; }\n")
    text = os.path.join(B, "bad_text.so")
    open(text, "w").write("this is not a library\n")

    print("=== 1. one MPC with every kind of broken effect next to a good one ===")
    plugins = [
        ("Poli", "Poli-Ess", so, good),
        ("Bad", "Not JSON", so, writer(text="{ this is : not json")),
        ("Bad", "Empty File", so, writer(text="")),
        ("Bad", "No Page", so, writer(obj={"hello": 1})),
        ("Bad", "Missing Def", so, writer(obj=variant(missing_def))),
        ("Bad", "Nine Tabs", so, writer(obj=variant(nine_tabs))),
        ("Bad", "Too Big", so, writer(obj=variant(too_big))),
        ("Bad", "Weird Values", so, writer(obj=variant(weird))),
        ("Bad", "Bad QLinks", so, writer(obj=GT, qlinks="[1,2,{")),
        ("Bad", "No QLinks", so, writer(obj=GT, qlinks=None)),
        ("Bad", "No Images", so, writer(obj=GT, pngs=False)),
        ('Tom & "Jerry"', "Amp <X>", so, good),
        ("Bad", "Text File", text, good),
        ("Bad", "No Main", nomain, good),
        ("Bad", "Null Main", nullmain, good),
        ("Bad", "No Skin", so, None),
    ]
    sett, plug = mpc(os.path.join(out, "mixed"), plugins)
    o, crashed = run(sett, plug)
    print("\n".join("     " + l for l in o.splitlines() if l.startswith(("status", "add"))))
    check(not crashed, "no crash, no sanitizer report" + ("" if not crashed else ":\n" + o[-3000:]))
    check(skin_ok(plug) is None, "its skin is valid JSON and every image resolves (%s)" % skin_ok(plug))
    added = re.findall(r"^add (.+?) +-> added", o, re.M)
    listed = re.findall(r"^add (.+?) +->", o, re.M)
    check("Poli-Ess" in added and "Amp <X>" in added, "the good effects are listed and run, & < > \" in names too (%s)" % added)
    check("Too Big" not in listed, "a skin binding a parameter past 383 (PoliPlug 1.3.1: 384 proxies) isn't listed")
    check("Text File" not in listed, "a .so that isn't a library isn't listed")
    check("No Skin" not in listed, "an effect without a skin isn't listed")
    for nm in ("No Main", "Null Main"):
        m = re.search(r"^add %s +-> (.*)$" % nm, o, re.M)
        check(m and "not added" in m.group(1), "%s is refused with a reason: %s" % (nm, m.group(1) if m else "not listed"))
    for nm in ("Not JSON", "Empty File", "No Page"):
        check(nm not in listed, "%s: no usable page, not listed" % nm)
    check("Missing Def" not in listed, "a skin whose page doesn't resolve isn't listed")
    t = json.dumps(json.load(open(os.path.join(plug, "Plugin Skins", "TUI.json"))))
    check(re.search(r'"(\.\./)+[^"]*Bad - VST - No Images/Plugin Skins/sh_bg_0\.png"', t) is not None,
          "a missing image stays pointed at its own folder (not at PoliPlug's own file of that name)")
    for nm in ("Nine Tabs", "Weird Values", "Bad QLinks", "No QLinks", "No Images"):
        m = re.search(r"^add %s +-> (.*)$" % nm, o, re.M)
        check(m is None or "audio peak" in m.group(1), "%s: %s" % (nm, m.group(1) if m else "not listed"))

    print("=== 2. more effects than PoliPlug lists (60), and more pages than it has ===")
    many = [("Many", "E%02d" % i, so, writer(obj=variant(nine_tabs) if i < 13 else GT)) for i in range(60)]
    sett, plug = mpc(os.path.join(out, "many"), many)
    o, crashed = run(sett, plug, add=False)
    reg = open(os.path.join(plug, "Plugin Skins", "pp_registry.txt")).read().splitlines()
    print("     " + o.strip().replace("\n", "\n     "))
    check(not crashed, "no crash")
    check(skin_ok(plug) is None, "valid skin (%s)" % skin_ok(plug))
    views = sorted((int(l.split("\t")[0]), int(l.split("\t")[1])) for l in reg)
    check(all(v0 + nv <= 96 for v0, nv in views) and all(a[0] + a[1] <= b[0] for a, b in zip(views, views[1:])),
          "page ranges stay inside 1..95 and don't overlap (%d effects have pages)" % len(views))
    o2, _ = run(sett, plug, add=False)
    check(open(os.path.join(plug, "Plugin Skins", "pp_registry.txt")).read().splitlines() == reg, "the same pages on the next start")

    print("=== 3. no MPC.settings ===")
    sett, plug = mpc(os.path.join(out, "nosettings"), [], settings=False)
    o, crashed = run(sett, plug)
    check(not crashed and "status:" in o, "starts: " + o.splitlines()[0] if o else "")

    print("=== 4. a skin folder it can't write ===")
    sett, plug = mpc(os.path.join(out, "readonly"), [("Poli", "Poli-Ess", so, good)])
    os.makedirs(os.path.join(plug, "Plugin Skins", "TUI.json.tmp"))   # in the way of its temp file
    o, crashed = run(sett, plug, add=True)
    print("     " + o.strip().replace("\n", "\n     "))
    check(not crashed, "no crash")
    check("Couldn't write" in o.splitlines()[0], "the status says it couldn't write its skin")
    check(json.load(open(os.path.join(plug, "Plugin Skins", "TUI.json"))) is not None, "the old skin is left as it was")

    print("=== 5. a damaged registry ===")
    sett, plug = mpc(os.path.join(out, "badreg"), [("Poli", "Poli-Ess", so, good), ("Bad", "Nine Tabs", so, writer(obj=variant(nine_tabs))),
                                                   ("Bad", "One Tab", so, good)])
    open(os.path.join(plug, "Plugin Skins", "pp_registry.txt"), "w").write(
        "garbage\n-5\t3\tPoli\tPoli-Ess\n90\t20\tBad\tNine Tabs\n1\t1\tPoli\tPoli-Ess\n1\t1\tBad\tNine Tabs\n"
        "1\t1\tBad\tOne Tab\n3\t1\tPoli\tPoli-Ess\n\t\t\n" + "x" * 1000 + "\n")
    o, crashed = run(sett, plug)
    reg = open(os.path.join(plug, "Plugin Skins", "pp_registry.txt")).read().splitlines()
    print("     registry now: %s" % reg)
    views = sorted((int(l.split("\t")[0]), int(l.split("\t")[1])) for l in reg)
    check(not crashed, "no crash")
    check(all(a[0] + a[1] <= b[0] for a, b in zip(views, views[1:])), "no two effects share a page")
    check(skin_ok(plug) is None and o.count("audio peak") == 3, "valid skin, all three effects open and run")
    views = [int(re.search(r"view (\d+)", l).group(1)) for l in o.splitlines() if "open: view" in l]
    check(len(set(views)) == 3, "each opens its own page (views %s)" % views)
    print("PASSED" if not fails else "FAILED (%d)" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
