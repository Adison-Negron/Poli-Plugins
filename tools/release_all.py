#!/usr/bin/env python3
"""Make the catalog release zips for every Poli plugin from its built, tested zip (workflow step 5).

    python3 tools/release_all.py <github-owner> [--toolchain ../mpc-vst-plugins]

For each plugin folder it takes the newest dist/<Name>-<version>-mpc-aarch64.zip (made by that plugin's build.sh), repackages
the same .so and skin with the toolchain's current tools/release.py, adding --repo <owner>/poli-plugins (the catalog needs
it), and checks the result with tools/catalog_check.py --catalog. Output: release/<tag>/<zip> and release/RELEASES.md (one
GitHub release per plugin: tag, title, file, sha256). The binaries are not rebuilt: what was tested is what ships.
"""
import argparse, glob, hashlib, json, os, re, shutil, subprocess, sys, tempfile, zipfile

REPO_NAME = "Poli-Plugins"
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOLDERS = ["Poli-Chorus", "Poli-delay", "Poli-ess", "Poli-Juicebox", "Poli-Multiband", "PoliPlug", "Poli-q", "Poli-Saturator",
           "Poli-Shhhh", "Poli-tea", "Poli-verb", "Poliwow"]
ID_FIX = {"Poli-tea": "poli-tea"}   # 1.0.1 was first packaged with Poli-delay's id by mistake

ap = argparse.ArgumentParser()
ap.add_argument("owner", help="GitHub user or organisation that owns %s" % REPO_NAME)
ap.add_argument("--toolchain", default=os.path.join(HERE, "..", "mpc-vst-plugins"))
a = ap.parse_args()
if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})", a.owner):
    sys.exit("not a GitHub user name: %r" % a.owner)
repo = "%s/%s" % (a.owner, REPO_NAME)
mv = os.path.abspath(a.toolchain)
out = os.path.join(HERE, "release")
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
rows, bad = [], 0
for folder in FOLDERS:
    zips = sorted(glob.glob(os.path.join(HERE, folder, "dist", "*-mpc-aarch64.zip")), key=os.path.getmtime)
    if not zips:
        print("%-15s no dist/*-mpc-aarch64.zip: run its build.sh first" % folder)
        bad += 1
        continue
    work = tempfile.mkdtemp()
    with zipfile.ZipFile(zips[-1]) as z:
        top = z.namelist()[0].split("/")[0]
        z.extractall(work)
    dst = os.path.join(work, top)
    man = json.load(open(os.path.join(dst, "mpc-plugin.json")))
    pdir = glob.glob(os.path.join(dst, "portable", "*"))[0]
    meta = open(os.path.join(pdir, "plugin-meta.xml")).read()
    name = re.search(r' name="([^"]+)"', meta).group(1)
    so = os.path.join(pdir, os.path.basename(re.search(r'file="([^"]+)"', meta).group(1)))
    skin = os.path.join(work, "skin", os.path.basename(pdir))
    os.makedirs(skin)
    for f in os.listdir(pdir):
        if f in ("plugin-meta.xml", "mpc-plugin.json", "plugin.json") or f == os.path.basename(so):
            continue
        s = os.path.join(pdir, f)
        (shutil.copytree if os.path.isdir(s) else shutil.copy2)(s, os.path.join(skin, f))
    entry = os.path.join(work, "entry.xml")
    open(entry, "w").write(meta.replace("%payload-path%", "/sdcard/Synths").strip() + "\n")
    pid = ID_FIX.get(name, man["id"])
    tag = "%s-v%s" % (pid, man["version"])
    odir = os.path.join(out, tag)
    cmd = [sys.executable, os.path.join(mv, "tools", "release.py"), "--so", so, "--skin", skin, "--entry", entry,
           "--version", man["version"], "--id", pid, "--repo", repo, "--license", man.get("license") or "MIT", "-o", odir]
    if man.get("about"):
        cmd += ["--about", man["about"]]
    r = subprocess.run(cmd, capture_output=True, text=True)
    zp = (glob.glob(os.path.join(odir, "*-mpc-aarch64.zip")) or [None])[0]
    c = subprocess.run([sys.executable, os.path.join(mv, "tools", "catalog_check.py"), zp or "-", "--catalog",
                        "--expect-id", pid, "--expect-repo", repo], capture_output=True, text=True) if zp else None
    ok = r.returncode == 0 and c is not None and c.returncode == 0
    bad += not ok
    last = (c.stdout + c.stderr).strip().splitlines()[-1] if c else (r.stdout + r.stderr).strip()
    print("%-15s %-7s %-24s %s" % (name, man["version"], tag, last))
    if ok:
        rows.append((tag, "%s %s" % (name, man["version"]), os.path.basename(zp), hashlib.sha256(open(zp, "rb").read()).hexdigest()))
    shutil.rmtree(work)

with open(os.path.join(out, "RELEASES.md"), "w") as f:
    f.write("# GitHub releases to create on %s\n\nOne release per plugin. Tag, title, then attach the one file.\n\n" % repo)
    f.write("| Tag | Title | File | SHA-256 |\n|---|---|---|---|\n")
    for t in rows:
        f.write("| `%s` | %s | `%s` | `%s` |\n" % t)
print("\n%d of %d ready in %s" % (len(rows), len(FOLDERS), out))
sys.exit(1 if bad else 0)
