#!/usr/bin/env python3
"""Poli-verb factory presets: the single source. Writes

    src/presets.h        the C++ table (positions 0..1000 in VerbDSP's knob order)
    presets/presets.json names, categories, physical values (what the generator and the docs read)
    PRESETS.md           every preset's values

    python3 presets/factory.py          (run from the Poli-verb folder; build.sh does it as step 0)

Values below are PHYSICAL (%, dB, ms, s, x). Anything not given is the Init value. A preset sets every parameter
(Freeze off, Sync as given, L and R on), so picking one always gives exactly its sound.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

TYPES = ["Hall", "Room", "Chamber", "Plate", "Spring", "Cathedral", "Arena", "Booth", "Canyon", "Ambience", "Cave",
         "Tunnel", "Garage", "Stairwell"]
DIVS = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"]
KNOBS = ["mix", "input", "output", "drive", "low", "tilt", "high", "size", "predelay", "width", "er_level", "er_prob",
         "diffusion", "damping", "modulation", "density", "spread", "bassmult", "attack", "release", "decay"]

# (menu prefix, category name shown in the browser, order)
CATS = [("Room", "Rooms"), ("Hall", "Halls"), ("Plate", "Plates"), ("Spring", "Springs"), ("Church", "Churches"),
        ("Arena", "Arenas"), ("Amb", "Ambience"), ("Vox", "Vocals"), ("Drum", "Drums"), ("FX", "Creative")]

INIT = dict(type="Hall", mix=30, input=0, output=0, drive=0, low=0, tilt=0, high=0, size=50, pd=7.2, width=100, lvl=60,
            prob=100, diff=80, damp=50, mod=30, dens=60, spread=70, bass=1.0, attack=0, release=None, decay=2.45,
            sync=False)


def P(cat, name, type, **kw):
    d = dict(INIT)
    d.update(kw)
    d["type"] = type
    d["cat"] = cat
    d["name"] = name
    return d


def pos(d):
    """physical -> knob positions (0..1000), in KNOBS order"""
    lg = lambda x: math.log(x)
    if d["sync"]:
        i = DIVS.index(d["pd"]) if isinstance(d["pd"], str) else 5
        pd = i * 1000.0 / (len(DIVS) - 1)
    else:
        pd = 1000.0 * math.sqrt(min(500.0, max(0.0, d["pd"])) / 500.0)
    rel = 1000.0 if d["release"] is None else 995.0 * lg(min(10.0, max(0.02, d["release"])) / 0.02) / lg(500.0)
    v = [d["mix"] * 10, 500 + d["input"] * 500 / 24, 500 + d["output"] * 500 / 24, d["drive"] * 10,
         500 + d["low"] * 500 / 12, 500 + d["tilt"] * 500 / 6, 500 + d["high"] * 500 / 12,
         d["size"] * 10, pd, d["width"] * 5, d["lvl"] * 10, d["prob"] * 10,
         d["diff"] * 10, d["damp"] * 10, d["mod"] * 10, d["dens"] * 10, d["spread"] * 10,
         1000.0 * lg(min(4.0, max(0.25, d["bass"])) / 0.25) / lg(16.0),
         1000.0 * math.sqrt(min(1500.0, max(0.0, d["attack"])) / 1500.0), rel,
         1000.0 * lg(min(30.0, max(0.2, d["decay"])) / 0.2) / lg(150.0)]
    return [int(round(min(1000.0, max(0.0, x)))) for x in v]


PRESETS = [
    P("Init", "Init", "Hall"),
    # ---- Rooms ----
    P("Rooms", "Small Room", "Room", mix=22, size=30, pd=4, decay=0.45, damp=55, dens=70, lvl=70),
    P("Rooms", "Wood Room", "Room", mix=25, size=45, decay=0.7, damp=60, bass=1.1, lvl=70),
    P("Rooms", "Tiled Room", "Room", mix=24, size=40, decay=0.9, damp=20, bass=0.8, dens=85, lvl=75, high=1.5),
    P("Rooms", "Live Room", "Room", mix=28, size=65, decay=1.2, damp=40, mod=25),
    P("Rooms", "Dead Booth", "Booth", mix=18, size=35, decay=0.25, damp=65, prob=60),
    P("Rooms", "Vocal Booth", "Booth", mix=22, size=55, decay=0.4, damp=50, pd=6),
    P("Rooms", "Warm Chamber", "Chamber", mix=26, size=45, decay=1.3, damp=62, bass=1.3, pd=8, low=1),
    P("Rooms", "Echo Chamber", "Chamber", mix=30, size=70, decay=2.0, damp=45, pd=22, mod=35),
    P("Rooms", "Garage", "Garage", mix=26, size=60, decay=1.1, damp=35, diff=55, dens=45, prob=80),
    P("Rooms", "Concrete Garage", "Garage", mix=28, size=80, decay=1.9, damp=28, bass=0.9, mod=10),
    # ---- Halls ----
    P("Halls", "Small Hall", "Hall", mix=25, size=35, decay=1.4, damp=50, pd=10),
    P("Halls", "Medium Hall", "Hall", mix=28, size=50, decay=2.2, damp=52, pd=16),
    P("Halls", "Large Hall", "Hall", mix=28, size=70, decay=3.2, damp=55, pd=24, bass=1.1),
    P("Halls", "Concert Hall", "Hall", mix=30, size=80, decay=3.8, damp=48, pd=28, bass=1.2, lvl=55, width=110),
    P("Halls", "Bright Hall", "Hall", mix=26, size=55, decay=2.4, damp=25, diff=85, high=2, pd=14),
    P("Halls", "Warm Hall", "Hall", mix=28, size=60, decay=2.8, damp=72, bass=1.4, low=1.5, high=-2, pd=18),
    P("Halls", "Dark Hall", "Hall", mix=27, size=65, decay=3.4, damp=85, tilt=-2, pd=20),
    P("Halls", "Gothic Hall", "Hall", mix=30, size=85, decay=5.0, damp=60, pd=35, dens=40, mod=35),
    P("Halls", "Tight Hall", "Hall", mix=22, size=30, decay=1.0, damp=45, dens=85, prob=70),
    P("Halls", "Wide Hall", "Hall", mix=28, size=60, decay=2.9, spread=100, width=150, pd=20, mod=40),
    P("Halls", "Piano Hall", "Hall", mix=24, size=55, decay=2.6, damp=58, pd=18, mod=20, bass=1.15),
    P("Halls", "String Hall", "Hall", mix=30, size=70, decay=3.6, damp=60, attack=60, pd=26, width=120),
    P("Halls", "Orchestra Stage", "Hall", mix=26, size=75, decay=2.8, damp=50, lvl=80, prob=90, pd=22),
    P("Halls", "Ambient Hall", "Hall", mix=40, size=80, decay=7.0, damp=65, mod=50, pd=30, width=130),
    # ---- Plates ----
    P("Plates", "Classic Plate", "Plate", mix=25, size=40, decay=1.6, damp=25, pd=0),
    P("Plates", "Bright Plate", "Plate", mix=22, decay=1.8, damp=10, high=2, pd=0),
    P("Plates", "Warm Plate", "Plate", mix=26, decay=2.0, damp=55, bass=1.2, low=1, pd=0),
    P("Plates", "Short Plate", "Plate", mix=20, size=25, decay=0.7, damp=30, pd=0),
    P("Plates", "Long Plate", "Plate", mix=28, size=60, decay=4.0, damp=35, pd=0),
    P("Plates", "Vocal Plate", "Plate", mix=24, decay=1.4, damp=35, pd=18, tilt=1, high=1.5),
    P("Plates", "Snare Plate", "Plate", mix=30, decay=1.1, damp=30, dens=85, pd=0),
    P("Plates", "Dense Plate", "Plate", mix=25, decay=2.5, dens=100, diff=100, pd=0),
    P("Plates", "Silk Plate", "Plate", mix=26, decay=2.2, damp=65, mod=45, pd=4),
    P("Plates", "Huge Plate", "Plate", mix=30, size=85, decay=6.0, damp=40, pd=12),
    # ---- Springs ----
    P("Springs", "Twang", "Spring", mix=30, decay=1.2, damp=45, pd=0),
    P("Springs", "Surf Spring", "Spring", mix=35, decay=1.8, mod=50, high=2, drive=15, pd=0),
    P("Springs", "Boingy", "Spring", mix=38, decay=0.9, dens=0, diff=20, pd=0),
    P("Springs", "Amp Spring", "Spring", mix=30, decay=1.4, drive=25, tilt=1.5, pd=0),
    P("Springs", "Dub Spring", "Spring", mix=40, decay=2.6, damp=60, pd="1/8", sync=True),
    P("Springs", "Soft Spring", "Spring", mix=24, decay=1.0, damp=70, pd=0),
    # ---- Churches ----
    P("Churches", "Small Chapel", "Cathedral", mix=26, size=30, decay=2.0, damp=55, pd=12),
    P("Churches", "Stone Chapel", "Cathedral", mix=27, size=40, decay=3.0, damp=45, pd=16),
    P("Churches", "Village Church", "Cathedral", mix=28, size=50, decay=3.8, damp=55, pd=20),
    P("Churches", "Cathedral", "Cathedral", mix=30, size=65, decay=6.5, damp=55, pd=28),
    P("Churches", "Grand Cathedral", "Cathedral", mix=32, size=85, decay=9.0, damp=50, pd=40),
    P("Churches", "Cloister", "Cathedral", mix=30, size=55, decay=4.5, damp=65, mod=40, bass=1.3, pd=22),
    P("Churches", "Choir Loft", "Cathedral", mix=34, size=60, decay=5.0, damp=70, attack=120, pd=24),
    P("Churches", "Basilica", "Cathedral", mix=33, size=95, decay=12.0, damp=62, pd=50),
    # ---- Arenas ----
    P("Arenas", "Arena", "Arena", mix=25, size=60, decay=3.0, pd=40),
    P("Arenas", "Stadium", "Arena", mix=26, size=80, decay=4.5, pd=70, dens=20),
    P("Arenas", "Coliseum", "Arena", mix=28, size=90, decay=5.5, pd=60),
    P("Arenas", "Slap Hall", "Arena", mix=24, size=70, decay=2.0, pd=90, prob=40, lvl=80),
    P("Arenas", "Canyon Call", "Canyon", mix=30, size=55, decay=3.0, pd=60, prob=50),
    P("Arenas", "Valley Echo", "Canyon", mix=32, size=75, decay=4.0, pd=120, prob=30),
    # ---- Ambience ----
    P("Ambience", "Air", "Ambience", mix=14, size=30, decay=0.25, damp=40, pd=0),
    P("Ambience", "Studio Air", "Ambience", mix=16, size=45, decay=0.35, pd=0),
    P("Ambience", "Tight Ambience", "Ambience", mix=18, size=25, decay=0.2, pd=0),
    P("Ambience", "Drum Room", "Ambience", mix=22, size=50, decay=0.4, pd=2),
    P("Ambience", "Bright Ambience", "Ambience", mix=15, decay=0.3, damp=10, high=2.5, pd=0),
    P("Ambience", "Natural Space", "Ambience", mix=18, size=60, decay=0.6, mod=20, pd=3),
    P("Ambience", "Close Mic Room", "Ambience", mix=12, size=35, decay=0.3, damp=55, pd=0),
    P("Ambience", "Wide Ambience", "Ambience", mix=18, decay=0.4, width=160, spread=100, pd=0),
    P("Ambience", "Subtle Glue", "Ambience", mix=10, size=40, decay=0.3, pd=0),
    P("Ambience", "Snap Early", "Ambience", mix=22, size=40, decay=0.15, lvl=100, prob=100, pd=0),
    # ---- Vocals ----
    P("Vocals", "Pop Vocal", "Plate", mix=22, decay=1.6, pd=20, high=1),
    P("Vocals", "Intimate Vocal", "Room", mix=16, size=30, decay=0.5, pd=5),
    P("Vocals", "Ballad Hall", "Hall", mix=30, size=65, decay=3.0, damp=60, pd=30),
    P("Vocals", "Rap Slap", "Room", mix=20, size=40, decay=0.6, pd="1/16", sync=True),
    P("Vocals", "R&B Silk", "Plate", mix=26, decay=2.2, damp=65, mod=45, pd=24),
    P("Vocals", "Rock Chamber", "Chamber", mix=24, size=55, decay=1.4, pd=10),
    P("Vocals", "Choral Air", "Cathedral", mix=32, size=60, decay=4.0, attack=80, pd=24),
    P("Vocals", "Dream Vocal", "Hall", mix=38, size=75, decay=5.0, damp=70, mod=55, pd="1/8", sync=True),
    P("Vocals", "Whisper Room", "Booth", mix=28, size=50, decay=0.6, damp=60, pd=4),
    P("Vocals", "Backing Wash", "Hall", mix=40, size=70, decay=3.5, damp=80, pd=40),
    P("Vocals", "Lead Slap Hall", "Hall", mix=22, size=55, decay=1.8, pd="1/8.", sync=True, lvl=70),
    P("Vocals", "Spoken Word", "Room", mix=12, size=35, decay=0.6, damp=55, pd=3),
    P("Vocals", "Harmony Plate", "Plate", mix=30, size=50, decay=2.8, damp=45, pd=30),
    P("Vocals", "Shout Chamber", "Chamber", mix=20, size=50, decay=1.1, damp=45, pd=6),
    # ---- Drums ----
    P("Drums", "Snare Room", "Room", mix=28, size=45, decay=0.7, pd=0, lvl=80),
    P("Drums", "Big Snare", "Plate", mix=30, decay=1.3, dens=90, pd=0),
    P("Drums", "Gated Snare", "Hall", mix=42, size=55, decay=3.0, damp=30, release=0.18, pd=0, lvl=40),
    P("Drums", "Tom Hall", "Hall", mix=28, size=55, decay=2.0, pd=6),
    P("Drums", "Kick Air", "Ambience", mix=12, size=30, decay=0.2, low=-2, pd=0),
    P("Drums", "Drum Bus Room", "Room", mix=20, size=70, decay=0.9, damp=45, pd=0),
    P("Drums", "Overhead Air", "Ambience", mix=16, decay=0.35, width=140, high=2, pd=0),
    P("Drums", "Stadium Hits", "Arena", mix=22, size=65, decay=2.8, pd=30),
    P("Drums", "Snap Plate", "Plate", mix=26, size=30, decay=0.9, pd=0),
    P("Drums", "Reverse-ish Swell", "Plate", mix=35, decay=2.0, attack=400, pd=0),
    # ---- Creative ----
    P("Creative", "Long Wash", "Hall", mix=50, size=85, decay=20.0, damp=40, mod=45, pd=30),
    P("Creative", "Swell Wash", "Hall", mix=45, size=75, decay=8.0, attack=700, damp=50, mod=50),
    P("Creative", "Gated Verb", "Hall", mix=40, size=55, decay=4.0, release=0.25, damp=35, pd=0),
    P("Creative", "Tunnel Flutter", "Tunnel", mix=35, size=60, decay=2.5, prob=100, dens=20),
    P("Creative", "Tunnel Sync", "Tunnel", mix=35, size=60, decay=2.5, pd="1/8.", sync=True),
    P("Creative", "Cave Dark", "Cave", mix=40, size=65, decay=8.0, damp=85, bass=1.4, pd=30),
    P("Creative", "Stairwell Echo", "Stairwell", mix=34, size=60, decay=3.0, mod=60, pd=14),
    P("Creative", "Space Drone", "Cathedral", mix=55, size=90, decay=25.0, attack=500, damp=45, mod=60, pd=40),
    P("Creative", "Lo-Fi Tank", "Garage", mix=35, size=55, decay=1.6, damp=90, high=-6, drive=60, mod=40),
    P("Creative", "Metal Hall", "Garage", mix=30, size=75, decay=2.4, diff=20, dens=100, damp=10, mod=0),
    P("Creative", "Ghost Canyon", "Canyon", mix=45, size=70, decay=6.0, damp=65, pd="1/4", sync=True, prob=60),
    P("Creative", "Frozen Air", "Plate", mix=40, size=70, decay=30.0, damp=15, high=2, mod=70, pd=10),
]

CAT_NAMES = ["All"] + [c[1] for c in CATS]
PREFIX = {c[1]: c[0] for c in CATS}
PREFIX["Init"] = ""


def menu_name(d):
    return d["name"] if d["cat"] == "Init" else "%s: %s" % (PREFIX[d["cat"]], d["name"])


def check():
    names = [menu_name(d) for d in PRESETS]
    assert len(set(names)) == len(names), "duplicate preset names"
    for n in names:
        assert len(n) <= 24, "menu name too long (%d): %s" % (len(n), n)
    for d in PRESETS:
        assert d["type"] in TYPES, d
        assert d["cat"] == "Init" or d["cat"] in CAT_NAMES, d
        if d["sync"]:
            assert isinstance(d["pd"], str) and d["pd"] in DIVS, d


def main():
    check()
    names = [menu_name(d) for d in PRESETS]
    cat_of = [0 if d["cat"] == "Init" else CAT_NAMES.index(d["cat"]) for d in PRESETS]   # 0 = Init / All only
    lines = ["// Generated by presets/factory.py -- edit the values there, not here.", "#pragma once", "#include <stdint.h>", "",
             "struct VerbPreset { const char *name; uint8_t cat; uint8_t type; uint8_t sync; uint16_t pos[21]; };",
             "static const int kNumPresets = %d;" % len(PRESETS),
             "static const int kNumCats = %d;   // real categories (the browser adds \"All\" in front)" % len(CATS),
             "static const char *const kCatNames[kNumCats + 1] = {%s};" % ", ".join('"%s"' % c for c in CAT_NAMES),
             "static const VerbPreset kPresets[kNumPresets] = {"]
    for d, n, c in zip(PRESETS, names, cat_of):
        p = pos(d)
        lines.append('    {"%s", %d, %d, %d, {%s}},' % (n.replace('"', '\\"'), c, TYPES.index(d["type"]), 1 if d["sync"] else 0,
                                                       ", ".join(str(x) for x in p)))
    lines.append("};")
    with open(os.path.join(ROOT, "src", "presets.h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    out = [{"name": n, "short": d["name"], "cat": d["cat"], "catIndex": c, "type": d["type"], "pos": pos(d), "sync": d["sync"]}
           for d, n, c in zip(PRESETS, names, cat_of)]
    with open(os.path.join(HERE, "presets.json"), "w", newline="\n") as f:
        json.dump({"cats": CAT_NAMES, "types": TYPES, "presets": out}, f, indent=1)
    md = ["# Poli-verb factory presets", "", "%d presets in %d categories. Generated by `presets/factory.py`." % (len(PRESETS), len(CATS)), ""]
    for cat in ["Init"] + [c[1] for c in CATS]:
        md += ["## %s" % cat, "", "| Preset | Space | Mix | Size | Pre-Delay | Decay | Damp | Other |", "|---|---|---|---|---|---|---|---|"]
        for d in PRESETS:
            if d["cat"] != cat:
                continue
            other = [k for k in ("drive", "low", "tilt", "high", "width", "lvl", "prob", "diff", "mod", "dens", "spread", "bass", "attack", "release")
                     if d.get(k) != INIT.get(k)]
            desc = ", ".join("%s %s" % (k, d[k]) for k in other)
            pd = d["pd"] if d["sync"] else ("%g ms" % d["pd"])
            md.append("| %s | %s | %g %% | %g %% | %s%s | %g s | %g %% | %s |" % (d["name"], d["type"], d["mix"], d["size"], pd,
                                                                               " sync" if d["sync"] else "", d["decay"], d["damp"], desc))
        md.append("")
    with open(os.path.join(ROOT, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(md))
    # the preset list in params.json is written by make_params.py (it owns the file)
    print("presets: %d, categories: %s" % (len(PRESETS), ", ".join("%s=%d" % (c, sum(1 for d in PRESETS if d["cat"] == c)) for c in CAT_NAMES[1:])))


if __name__ == "__main__":
    main()
