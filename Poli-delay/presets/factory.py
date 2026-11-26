#!/usr/bin/env python3
"""Poli-delay factory presets: the single source. Writes

    src/presets.h        the C++ table (knob positions 0..1000 in DelayDSP's order)
    presets/presets.json names, categories, physical values (what the generator and the docs read)
    PRESETS.md           every preset's values

    python3 presets/factory.py          (run from the Poli-delay folder; build.sh does it as step 0)

Values below are PHYSICAL (ms, %, Hz). Anything not given is the Init value. A preset sets every parameter (Sync, Offset
and Ping-Pong as given), so picking one always gives exactly its sound. `time` is milliseconds, or a note value
("1/8.") when sync=True. `off` (offset, % of Time, + = right side later, - = left side later) switches Offset on;
leave it out for Offset off.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

DIVS = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/1"]
KNOBS = ["time", "offset_amt", "feedback", "hp", "lp", "mix", "rate", "depth", "drive", "tone", "spread"]
DEFAULT_POS = [683, 500, 350, 200, 850, 300, 450, 0, 0, 500, 500]   # DelayDSP.h kDefaultPos

# (menu prefix, category name shown in the browser)
CATS = [("Classic", "Classic"), ("Tape", "Tape"), ("Ping", "Ping-Pong"), ("Stereo", "Stereo"), ("Amb", "Ambient"),
        ("Vox", "Vocals"), ("Drum", "Drums"), ("FX", "Creative")]

# the Init sound = the knobs' default positions, in physical units
INIT = dict(time=5.0 * 400 ** (683 / 1000.0), sync=False, off=None, fb=35.0, hp=20.0 * 100 ** 0.2, lp=500.0 * 40 ** 0.85,
            mix=30.0, rate=0.05 * 200 ** 0.45, depth=0.0, drive=0.0, tone=0.0, spread=100.0, pp=False)


def P(cat, name, **kw):
    d = dict(INIT)
    d.update(kw)
    if "sync" in kw or isinstance(d["time"], str):
        d["sync"] = isinstance(d["time"], str)
    d["cat"] = cat
    d["name"] = name
    return d


def pos(d):
    """physical -> knob positions (0..1000), in KNOBS order"""
    lg = math.log
    if d["sync"]:
        t = DIVS.index(d["time"]) * 1000.0 / (len(DIVS) - 1)
    else:
        t = 1000.0 * lg(min(2000.0, max(5.0, d["time"])) / 5.0) / lg(400.0)
    off = 500.0 + (d["off"] or 0.0) * 5.0
    v = [t, off, d["fb"] * 10, 1000.0 * lg(min(2000.0, max(20.0, d["hp"])) / 20.0) / lg(100.0),
         1000.0 * lg(min(20000.0, max(500.0, d["lp"])) / 500.0) / lg(40.0), d["mix"] * 10,
         1000.0 * lg(min(10.0, max(0.05, d["rate"])) / 0.05) / lg(200.0), d["depth"] * 10, d["drive"] * 10,
         500.0 + d["tone"] * 5, d["spread"] * 5]
    return [int(round(min(1000.0, max(0.0, x)))) for x in v]


PRESETS = [
    P("Init", "Init"),
    # ---- Classic ----
    P("Classic", "Slapback", time=95, fb=10, hp=150, lp=6500, mix=26, drive=10),
    P("Classic", "Rockabilly Slap", time=130, fb=22, hp=120, lp=5500, mix=28, drive=25, tone=-10),
    P("Classic", "Quarter Echo", time="1/4", fb=32, lp=9000, mix=28),
    P("Classic", "Dotted Eighth", time="1/8.", fb=38, hp=200, lp=8000, mix=30),
    P("Classic", "Eighth Echo", time="1/8", fb=28, mix=26),
    P("Classic", "Triplet Echo", time="1/8T", fb=35, mix=28),
    P("Classic", "Digital Clean", time=340, fb=30, hp=30, lp=18000, mix=25),
    P("Classic", "Double Tap", time=62, fb=0, lp=9000, mix=35, spread=120),
    P("Classic", "Long Echo", time=750, fb=48, lp=7000, mix=30),
    P("Classic", "Half Note Echo", time="1/2", fb=35, lp=6000, mix=28),
    # ---- Tape ----
    P("Tape", "Tape Echo", time=260, fb=45, hp=140, lp=4200, drive=30, tone=-20, rate=0.6, depth=14, mix=32),
    P("Tape", "Worn Tape", time=320, fb=50, hp=180, lp=3000, drive=45, tone=-35, rate=0.8, depth=30),
    P("Tape", "Tape Dub", time="1/4", fb=55, lp=3500, drive=40, rate=0.4, depth=16),
    P("Tape", "Space Tape", time="1/8.", fb=60, lp=3800, drive=35, rate=0.5, depth=22, spread=150, mix=34),
    P("Tape", "Warble Tape", time=280, fb=42, lp=4800, drive=25, rate=1.6, depth=38),
    P("Tape", "Cassette Delay", time=210, fb=40, hp=220, lp=2800, drive=55, tone=-15, rate=1.1, depth=26),
    P("Tape", "Reel Slap", time=110, fb=18, lp=5000, drive=35, depth=10),
    P("Tape", "Dusty Echo", time=450, fb=52, hp=260, lp=2400, drive=50, tone=-45, depth=20),
    P("Tape", "Bright Tape", time=240, fb=40, lp=9000, drive=28, tone=25, depth=10),
    P("Tape", "Wow & Flutter", time=300, fb=45, lp=4200, drive=20, rate=2.4, depth=60),
    # ---- Ping-Pong ----
    P("Ping-Pong", "Classic Ping-Pong", time="1/8", pp=True, fb=45, lp=8500),
    P("Ping-Pong", "Wide Bounce", time="1/4", pp=True, fb=48, spread=150),
    P("Ping-Pong", "Triplet Ping", time="1/8T", pp=True, fb=50),
    P("Ping-Pong", "Dotted Ping", time="1/8.", pp=True, fb=48, hp=200),
    P("Ping-Pong", "Fast Bounce", time="1/16", pp=True, fb=40, mix=26),
    P("Ping-Pong", "Slow Bounce", time="1/2", pp=True, fb=45, lp=6500),
    P("Ping-Pong", "Soft Ping", time="1/4", pp=True, fb=40, hp=160, lp=4200, drive=15, mix=26),
    P("Ping-Pong", "Ping Offset", time="1/8", pp=True, off=50, fb=45),
    P("Ping-Pong", "Tape Ping-Pong", time=280, pp=True, fb=52, lp=4000, drive=35, depth=12),
    P("Ping-Pong", "Ping Cloud", time=420, pp=True, fb=62, lp=5500, rate=0.3, depth=18, spread=170, mix=38),
    # ---- Stereo (Offset) ----
    P("Stereo", "Haas Widener", time=22, off=35, fb=0, lp=12000, mix=40),
    P("Stereo", "Dot vs Straight", time="1/8", off=50, fb=40),
    P("Stereo", "Straight/Triplet", time="1/8", off=33, fb=38),
    P("Stereo", "Stereo Slap", time=85, off=-40, fb=12, mix=30),
    P("Stereo", "Wide Offset", time=380, off=25, fb=38, spread=160),
    P("Stereo", "Left Heavy", time=300, off=-70, fb=40),
    P("Stereo", "Stereo Dub", time="1/4", off=50, fb=52, lp=4200, drive=25, mix=32),
    P("Stereo", "Offset Haze", time=520, off=18, fb=55, lp=5000, rate=0.35, depth=20, spread=150),
    P("Stereo", "Stagger", time="1/16", off=100, fb=35),
    P("Stereo", "Narrow Offset", time=160, off=60, fb=30, spread=60),
    # ---- Ambient ----
    P("Ambient", "Cloud Echo", time=680, fb=65, hp=200, lp=4800, drive=10, rate=0.3, depth=22, spread=160, mix=40),
    P("Ambient", "Glass Tail", time=540, fb=70, hp=400, lp=12000, tone=40, rate=0.7, depth=30, spread=180, mix=38),
    P("Ambient", "Dream Wash", time="1/2", fb=72, lp=3800, drive=15, rate=0.25, depth=35, spread=170, mix=42),
    P("Ambient", "Infinite Drift", time=600, fb=92, hp=250, lp=3500, drive=20, rate=0.2, depth=40, mix=35),
    P("Ambient", "Frozen Pad", time=1200, fb=96, hp=300, lp=3200, rate=0.15, depth=28, spread=170, mix=40),
    P("Ambient", "Slow Motion", time=1400, fb=55, lp=4000, depth=24, mix=38),
    P("Ambient", "Dark Space", time="1/4.", fb=68, hp=120, lp=1800, drive=30, tone=-50, mix=40),
    P("Ambient", "Airy Repeats", time=450, fb=58, hp=500, lp=16000, tone=60, depth=18, spread=160, mix=32),
    # ---- Vocals ----
    P("Vocals", "Vocal Slap", time=105, fb=8, hp=220, lp=7000, mix=22),
    P("Vocals", "Vocal Eighth", time="1/8", fb=22, hp=260, lp=6500, mix=20),
    P("Vocals", "Vocal Dotted", time="1/8.", fb=28, hp=300, lp=6000, mix=22),
    P("Vocals", "Vocal Throw", time="1/4", fb=18, hp=350, lp=5000, mix=24),
    P("Vocals", "Whisper Echo", time="1/4", fb=40, hp=450, lp=3600, mix=18, spread=140),
    P("Vocals", "Wide Double", time=38, off=40, fb=0, rate=0.8, depth=20, mix=30),
    P("Vocals", "Plate-ish Echo", time=180, fb=32, hp=250, lp=4800, drive=15, mix=20),
    P("Vocals", "Ballad Echo", time="1/4", fb=30, hp=220, lp=5500, rate=0.5, depth=8, mix=22),
    # ---- Drums ----
    P("Drums", "Snare Echo", time="1/8", fb=28, hp=300, lp=6500, mix=24),
    P("Drums", "Snare Dotted", time="1/8.", fb=25, hp=300, lp=6500, mix=24),
    P("Drums", "Hat Ticks", time="1/16", pp=True, fb=35, hp=700, lp=14000, mix=22),
    P("Drums", "Tom Roll", time="1/16", fb=55, hp=150, lp=5000, mix=26),
    P("Drums", "Clap Wide", time="1/8T", off=40, fb=30, spread=150, mix=26),
    P("Drums", "Dub Snare", time="1/4", fb=62, hp=350, lp=3000, drive=40, mix=30),
    P("Drums", "Percussion Air", time="1/8", fb=30, hp=600, lp=9000, mix=25),
    P("Drums", "Room Doubler", time=48, fb=0, hp=100, lp=9000, drive=20, mix=30),
    # ---- Creative ----
    P("Creative", "Dub Siren", time=380, fb=85, hp=300, lp=2800, drive=45, rate=0.5, depth=25, mix=40),
    P("Creative", "Lo-Fi Crunch", time=260, fb=50, hp=450, lp=2500, drive=85, tone=-20, mix=32),
    P("Creative", "Chorus Echo", time=22, fb=35, rate=0.7, depth=55, spread=170, mix=40),
    P("Creative", "Flange Echo", time=6, fb=70, rate=0.25, depth=45, mix=40),
    P("Creative", "Pitch Warble", time=320, fb=55, rate=3.2, depth=85, mix=35),
    P("Creative", "Radio Echo", time=200, fb=45, hp=700, lp=2600, drive=60, mix=30),
    P("Creative", "Robot Comb", time=11, fb=85, hp=150, lp=9000, mix=35),
    P("Creative", "Runaway", time=330, fb=100, hp=400, lp=3500, drive=55, mix=38),
    P("Creative", "Bouncy Castle", time="1/16", pp=True, fb=70, rate=2.0, depth=30),
    P("Creative", "Vibrato Echo", time=240, fb=40, lp=6000, rate=5.5, depth=70),
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
        assert d["cat"] == "Init" or d["cat"] in CAT_NAMES, d
        if d["sync"]:
            assert d["time"] in DIVS, d
        assert d["off"] is None or -100 <= d["off"] <= 100, d
    assert pos(PRESETS[0]) == DEFAULT_POS, (pos(PRESETS[0]), DEFAULT_POS)   # Init = the params' defaults


def main():
    check()
    names = [menu_name(d) for d in PRESETS]
    cat_of = [0 if d["cat"] == "Init" else CAT_NAMES.index(d["cat"]) for d in PRESETS]   # 0 = Init / All only
    lines = ["// Generated by presets/factory.py -- edit the values there, not here.", "#pragma once", "#include <stdint.h>", "",
             "struct DelayPreset { const char *name; uint8_t cat; uint8_t sync; uint8_t offset; uint8_t pingpong; uint16_t pos[11]; };",
             "static const int kNumPresets = %d;" % len(PRESETS),
             "static const int kNumCats = %d;   // real categories (the browser adds \"All\" in front)" % len(CATS),
             "static const char *const kCatNames[kNumCats + 1] = {%s};" % ", ".join('"%s"' % c for c in CAT_NAMES),
             "static const DelayPreset kPresets[kNumPresets] = {"]
    for d, n, c in zip(PRESETS, names, cat_of):
        lines.append('    {"%s", %d, %d, %d, %d, {%s}},' % (n.replace('"', '\\"'), c, 1 if d["sync"] else 0, 1 if d["off"] is not None else 0,
                                                          1 if d["pp"] else 0, ", ".join(str(x) for x in pos(d))))
    lines.append("};")
    with open(os.path.join(ROOT, "src", "presets.h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    out = [{"name": n, "short": d["name"], "cat": d["cat"], "catIndex": c, "pos": pos(d), "sync": d["sync"], "offset": d["off"] is not None,
            "pingpong": d["pp"]} for d, n, c in zip(PRESETS, names, cat_of)]
    with open(os.path.join(HERE, "presets.json"), "w", newline="\n") as f:
        json.dump({"cats": CAT_NAMES, "presets": out}, f, indent=1)
    md = ["# Poli-delay factory presets", "", "%d presets in %d categories. Generated by `presets/factory.py`." % (len(PRESETS), len(CATS)), ""]
    for cat in ["Init"] + [c[1] for c in CATS]:
        md += ["## %s" % cat, "", "| Preset | Time | Offset | Feedback | HP | LP | Mix | Mod (Rate / Depth) | Drive | Tone | Spread | Ping-Pong |",
               "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for d in PRESETS:
            if d["cat"] != cat:
                continue
            t = ("%s sync" % d["time"]) if d["sync"] else ("%g ms" % round(d["time"], 1))
            off = "-" if d["off"] is None else ("%+g %%" % d["off"])
            fq = lambda f: ("%.1f kHz" % (f / 1000.0)) if f >= 1000 else ("%d Hz" % round(f))
            md.append("| %s | %s | %s | %g %% | %s | %s | %g %% | %.2g Hz / %g %% | %g %% | %+g %% | %g %% | %s |" % (
                d["name"], t, off, round(d["fb"], 1), fq(d["hp"]), fq(d["lp"]), d["mix"], d["rate"], d["depth"], d["drive"], d["tone"],
                d["spread"], "on" if d["pp"] else "-"))
        md.append("")
    with open(os.path.join(ROOT, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(md))
    print("presets: %d, categories: %s" % (len(PRESETS), ", ".join("%s=%d" % (c, sum(1 for d in PRESETS if d["cat"] == c)) for c in CAT_NAMES[1:])))


if __name__ == "__main__":
    main()
