#!/usr/bin/env python3
"""Write the open-mpc catalog files for the Poli collection (workflow step 6):

    python3 tools/make_catalog_entries.py <github-owner> [<commit>]

writes catalog/collections/poli.json and catalog/plugins/<id>.json. Copy both folders into a fork of
open-mpc/mpc-vst-plugins (same paths) and open one pull request. Versions and checksums are not in these files: the
catalog reads them from the GitHub releases (tools/release_all.py).
With <commit> (a pushed commit that has docs/screenshots/<id>.png), each entry gets a "screenshot" link pinned to that
commit (the catalog asks for a tag or commit, never a branch, so the image can't change or vanish under the card).
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUTHOR = "Poli"
COLLECTION = "poli"
owner = sys.argv[1] if len(sys.argv) > 1 else ""
ref = sys.argv[2] if len(sys.argv) > 2 else ""
if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})", owner) or (ref and not re.fullmatch(r"[0-9a-f]{40}", ref)):
    sys.exit("usage: make_catalog_entries.py <github-owner> [<full 40-character commit>]")
repo = "%s/Poli-Plugins" % owner


def screenshot(pid):
    """the raw link to docs/screenshots/<pid>.png at `ref`, if that commit has the file"""
    if not ref:
        return None
    path = "docs/screenshots/%s.png" % pid
    if subprocess.run(["git", "-C", HERE, "cat-file", "-e", "%s:%s" % (ref, path)], capture_output=True).returncode:
        sys.exit("commit %s has no %s" % (ref, path))
    return "https://raw.githubusercontent.com/%s/%s/%s" % (repo, ref, path)

# id, name, zip stem, style, tags, summary (plain ASCII; say up front anything unusual)
PLUGINS = [
    ("poli-chorus", "Poli-Chorus", "Poli-Chorus", "modulation", ["chorus", "ensemble", "vibrato"],
     "Chorus-ensemble with Classic, Ensemble and Vibrato modes, a live LFO display, feedback, width, filter, warmth and 51 presets."),
    ("poli-delay", "Poli-delay", "Poli-delay", "delay", ["delay", "ping-pong", "tempo-sync"],
     "Stereo delay with offset, ping-pong, tempo sync, filtered and saturated feedback, modulation and 75 presets."),
    ("poli-ess", "Poli-Ess", "Poli-Ess", "dynamics", ["de-esser", "vocal", "sibilance"],
     "De-esser: dynamic sibilance suppressor for vocals, with presets."),
    ("poli-juicebox", "Poli-Juicebox", "Poli-Juicebox", "pitch-vocal", ["vocal", "pitch", "formant"],
     "Low-latency voice shaper: pitch and formant shifting, spectral tamer, harmonic shaper, formant filter, air and 50 presets."),
    ("poli-multiband", "Poli-Multiband", "Poli-Multiband", "dynamics", ["compressor", "multiband", "upward"],
     "3-band compressor with downward and upward compression per band, and presets."),
    ("poliplug", "PoliPlug", "PoliPlug", "multi-fx", ["rack", "chain", "parallel"],
     "Effect rack on one insert: up to 16 of your installed effects in series plus a parallel section, each with its own page "
     "and presets. Reads MPC.settings and the VST2 Plugins folders to find them, rewrites its own skin, and writes a scan "
     "report to MPC Documents."),
    ("poli-q", "Poli-q", "Poli-q", "filter-eq", ["eq", "dynamic-eq", "vocal"],
     "8-band dynamic EQ: bells, shelves, 6-48 dB/oct cuts, notch, band pass and tilt, with a live curve, a visualizer, "
     "Learn (fits a preset to your voice) and 43 presets."),
    ("poli-saturator", "Poli-Saturator", "Poli-Saturator", "distortion", ["saturation", "tape", "tube"],
     "Saturator with Tube, Tape, Clip and Fold types, Smash compression, Crunch, Soften, three-band drive and 65 presets."),
    ("poli-shhhhh", "Shhhhh", "Shhhhh", "mixing", ["denoise", "noise-reduction", "spectral"],
     "Learn-the-noise spectral de-noiser: Learn, Threshold, Reduction, 6 nodes, Pass and Diff."),
    ("poli-tea", "Poli-tea", "Poli-tea", "mixing", ["mastering", "limiter", "bus"],
     "Master bus processor: saturation, 3-band multiband compression, stereo widening and an adaptive true-peak limiter, "
     "with 56 presets."),
    ("poli-verb", "Poli-verb", "Poli-verb", "reverb", ["reverb", "space", "tempo-sync"],
     "Stereo reverb with 14 spaces, tempo-synced pre-delay, reflection probability and 101 presets."),
    ("poliwow", "Poliwow", "Poliwow", "glitch-performance", ["riser", "build-up", "sweep"],
     "One-knob build-up effect: turn Wow up for risers, sweeps and tails."),
]

os.makedirs(os.path.join(HERE, "catalog", "plugins"), exist_ok=True)
os.makedirs(os.path.join(HERE, "catalog", "collections"), exist_ok=True)
coll = {"id": COLLECTION, "title": "Poli effects", "author": AUTHOR, "repo": repo,
        "summary": "12 effects for vocals and mixing: de-esser, dynamic EQ, multiband, saturation, chorus, delay, reverb, "
                   "de-noiser, voice shaper, bus processor, a build-up effect and an effect rack. Gen2 (MPC Live III, One G2, "
                   "Key G2, XL) only."}
json.dump(coll, open(os.path.join(HERE, "catalog", "collections", COLLECTION + ".json"), "w"), indent=2)
for pid, name, stem, style, tags, summary in PLUGINS:
    e = {"id": pid, "name": name, "author": AUTHOR, "repo": repo, "kind": "effect", "style": style, "tags": tags,
         "license": "MIT", "summary": summary + " Gen2 only.", "asset_pattern": "%s-*-mpc-aarch64.zip" % stem,
         "collection": COLLECTION}
    shot = screenshot(pid)
    if shot:
        e["screenshot"] = shot
    json.dump(e, open(os.path.join(HERE, "catalog", "plugins", pid + ".json"), "w"), indent=2)
    open(os.path.join(HERE, "catalog", "plugins", pid + ".json"), "a").write("\n")
open(os.path.join(HERE, "catalog", "collections", COLLECTION + ".json"), "a").write("\n")
print("wrote catalog/collections/%s.json and %d plugin entries for %s" % (COLLECTION, len(PLUGINS), repo))
