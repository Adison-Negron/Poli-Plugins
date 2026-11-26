#!/usr/bin/env python3
"""Poli-Saturator factory presets: the single source. Writes

    src/presets.h        the C++ table (Type + knob positions 0..1000 in SatDSP's order)
    presets/presets.json names, categories, physical values (what the generator and the docs read)
    PRESETS.md           every preset's values

    python3 presets/factory.py          (run from the Poli-Saturator folder; build.sh does it as step 0)

Values below are PHYSICAL: Saturation / Smash / Crunch / Soften in %, Low / Mid / High as the band's drive relative to
Saturation in dB (0 = the same as Saturation, None = Off: that band stays clean). Anything not given is the Init value.
A preset sets every parameter and the Type, so picking one always gives exactly its sound.

How the presets are built (from how Decapitator, Saturn, Ableton's Saturator and tape-style plugins are used):
  * the Type is the first decision: Tube = warm, even + odd, vocals / bass / keys; Tape = soft, dark, glue, buses;
    Clip = hard, punchy, drums / aggression; Fold = metallic, synth / sound design.
  * Saturation sets how hard it works; most "Bus" and "Master" presets stay under 30 %, "FX" goes to 100 %.
  * Low / Mid / High aim the drive: a kick takes Low and loses High, a vocal takes the top, a telephone sound Mid only.
  * Smash is the density: 0 for transparent, 20-50 for glue, 80+ for a squashed parallel sound.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

TYPES = ["Tube", "Tape", "Clip", "Fold"]
KNOBS = ["sat", "smash", "crunch", "soften", "low", "mid", "high"]
DEFAULT_POS = [500, 0, 400, 250, 500, 500, 500]   # SatDSP.h kDefaultPos

# (menu prefix, category name shown in the browser)
CATS = [("Drum", "Drums"), ("Vox", "Vocals"), ("Bass", "Bass"), ("Keys", "Keys & Synth"), ("Bus", "Bus & Master"), ("FX", "Creative")]
CAT_NAMES = ["All"] + [c[1] for c in CATS]
PREFIX = {c[1]: c[0] for c in CATS}

INIT = dict(type="Tube", sat=50.0, smash=0.0, crunch=40.0, soften=25.0, low=0.0, mid=0.0, high=0.0)


def P(cat, name, type, sat, smash, crunch, soften, low=0.0, mid=0.0, high=0.0):
    return dict(cat=cat, name=name, type=type, sat=sat, smash=smash, crunch=crunch, soften=soften, low=low, mid=mid, high=high)


def band_pos(db):
    """band drive in dB (None = Off) -> knob position (SatMap::bandDb)"""
    if db is None:
        return 0
    p = 500.0 + db / 0.048 if db <= 0 else 500.0 + db / 0.024
    return int(round(min(1000.0, max(1.0, p))))


def pos(d):
    v = [d["sat"] * 10, d["smash"] * 10, d["crunch"] * 10, d["soften"] * 10, band_pos(d["low"]), band_pos(d["mid"]), band_pos(d["high"])]
    return [int(round(min(1000.0, max(0.0, x)))) for x in v]


N = None   # a band that is Off
PRESETS = [
    dict(cat="Init", name="Init", **{k: v for k, v in INIT.items()}),
    # ---- Drums -----------------------------------------------------------------------------------------------------
    P("Drums", "Kick Warmth", "Tube", 35, 10, 30, 40, low=3, mid=-6, high=N),
    P("Drums", "Kick Punch", "Clip", 45, 35, 60, 35, low=2, mid=0, high=-12),
    P("Drums", "Kick Sub Thump", "Tube", 30, 0, 20, 60, low=6, mid=N, high=N),
    P("Drums", "Snare Crack", "Clip", 50, 25, 70, 25, low=-6, mid=3, high=3),
    P("Drums", "Snare Body", "Tube", 40, 30, 40, 30, low=0, mid=3, high=-6),
    P("Drums", "Hat Grit", "Tube", 30, 0, 60, 20, low=N, mid=-12, high=3),
    P("Drums", "Drum Bus Tape", "Tape", 40, 35, 40, 30),
    P("Drums", "Drum Bus Glue", "Tape", 25, 55, 20, 40, mid=-3),
    P("Drums", "Break Crunch", "Clip", 65, 45, 75, 20, mid=3),
    P("Drums", "Loop Smash", "Tube", 55, 80, 50, 30, low=2),
    P("Drums", "808 Harmonics", "Tube", 45, 15, 20, 65, mid=6, high=N),
    P("Drums", "Room Crush", "Tape", 70, 90, 45, 50, high=-3),
    # ---- Vocals ----------------------------------------------------------------------------------------------------
    P("Vocals", "Warm Vocal", "Tube", 25, 20, 30, 45, low=-3),
    P("Vocals", "Presence", "Tube", 35, 25, 50, 30, low=-12, mid=3, high=3),
    P("Vocals", "Smooth Lead", "Tape", 30, 35, 25, 50, low=-3, high=-3),
    P("Vocals", "Gritty Vocal", "Clip", 45, 30, 65, 25, low=-9, mid=3),
    P("Vocals", "Radio Voice", "Clip", 60, 40, 80, 60, low=N, mid=6, high=N),
    P("Vocals", "Tape Vocal", "Tape", 40, 25, 40, 40, low=-3),
    P("Vocals", "Airy Vocal", "Tube", 25, 15, 60, 10, low=N, mid=-6, high=4),
    P("Vocals", "Backing Fat", "Tape", 35, 40, 20, 55, low=2, high=-6),
    P("Vocals", "Rap Punch", "Clip", 40, 45, 55, 35, low=-6, mid=3, high=2),
    P("Vocals", "Dark Vocal", "Tape", 45, 30, 30, 70, high=-12),
    P("Vocals", "Whisper Glue", "Tube", 15, 50, 20, 40),
    # ---- Bass ------------------------------------------------------------------------------------------------------
    P("Bass", "Warm Bass", "Tube", 35, 10, 30, 50, low=2, high=N),
    P("Bass", "Bass Grind", "Clip", 60, 25, 75, 40, mid=3, high=N),
    P("Bass", "Sub Harmonics", "Tube", 40, 0, 15, 75, low=6, mid=N, high=N),
    P("Bass", "Tape Bass", "Tape", 45, 30, 35, 55, low=2, high=-12),
    P("Bass", "Fold Bass", "Fold", 35, 20, 40, 55, mid=3, high=N),
    P("Bass", "Bass Presence", "Tube", 40, 35, 50, 40, low=-6, mid=6),
    P("Bass", "Pluck Crunch", "Clip", 55, 40, 65, 30, mid=3, high=-3),
    P("Bass", "Bass Glue", "Tape", 25, 60, 20, 50, high=N),
    P("Bass", "Rumble", "Tape", 70, 40, 35, 70, low=6, mid=-6, high=N),
    # ---- Keys & Synth -------------------------------------------------------------------------------------------------
    P("Keys & Synth", "Electric Piano", "Tube", 30, 10, 30, 40, high=-3),
    P("Keys & Synth", "Piano Tape", "Tape", 25, 25, 20, 45),
    P("Keys & Synth", "Organ Drive", "Tube", 55, 20, 70, 25, mid=3),
    P("Keys & Synth", "Warm Pad", "Tape", 30, 0, 25, 60, high=-6),
    P("Keys & Synth", "Lead Bite", "Clip", 50, 25, 70, 20, low=-6, mid=3, high=3),
    P("Keys & Synth", "Synth Fold", "Fold", 40, 0, 50, 30, low=-6, high=3),
    P("Keys & Synth", "Saw Smash", "Tube", 60, 55, 60, 25),
    P("Keys & Synth", "Arp Sizzle", "Fold", 30, 10, 60, 35, low=N, high=6),
    P("Keys & Synth", "Chord Tape", "Tape", 35, 20, 30, 50, high=-3),
    # ---- Bus & Master -----------------------------------------------------------------------------------------------
    P("Bus & Master", "Master Glue", "Tape", 15, 35, 15, 55),
    P("Bus & Master", "Gentle Tape", "Tape", 20, 10, 20, 50, high=-3),
    P("Bus & Master", "Mix Bus Warm", "Tube", 15, 25, 20, 50),
    P("Bus & Master", "Mix Bus Drive", "Tape", 30, 40, 40, 40),
    P("Bus & Master", "Master Polish", "Tube", 10, 20, 20, 30, low=-3, high=2),
    P("Bus & Master", "Loud Master", "Clip", 25, 60, 30, 40),
    P("Bus & Master", "Subtle Heat", "Tube", 10, 0, 25, 40),
    P("Bus & Master", "Open Air", "Tube", 20, 10, 50, 20, low=-12, mid=-3, high=4),
    P("Bus & Master", "Parallel Smash", "Tape", 60, 85, 35, 50),
    P("Bus & Master", "Vinyl Warmth", "Tape", 35, 15, 30, 80, low=2, high=-12),
    P("Bus & Master", "Radio Ready", "Clip", 35, 50, 50, 35, mid=2, high=2),
    # ---- Creative --------------------------------------------------------------------------------------------------
    P("Creative", "Lo-Fi Crunch", "Clip", 75, 30, 90, 70, high=-12),
    P("Creative", "Tape Destroyer", "Tape", 90, 60, 70, 40),
    P("Creative", "Fold Madness", "Fold", 80, 0, 80, 10),
    P("Creative", "Metal Fold", "Fold", 60, 20, 100, 5, low=-12, mid=6, high=6),
    P("Creative", "Broken Radio", "Clip", 85, 50, 100, 75, low=N, mid=9, high=N),
    P("Creative", "Warm Mud", "Tube", 85, 40, 20, 80, low=6, high=-12),
    P("Creative", "Screamer", "Clip", 100, 40, 100, 0, low=-6, mid=3),
    P("Creative", "Soft Squash", "Tape", 50, 100, 20, 60),
    P("Creative", "Glitch Fold", "Fold", 50, 40, 70, 20, low=-12, mid=3),
    P("Creative", "Bit Grit", "Clip", 55, 0, 100, 0, low=-6, high=3),
    P("Creative", "Heavy Low End", "Tube", 90, 30, 60, 45, low=8, mid=-6, high=-24),
    P("Creative", "Silk Fold", "Fold", 25, 10, 15, 60, mid=-3),
]


def menu_name(d):
    return d["name"] if d["cat"] == "Init" else "%s: %s" % (PREFIX[d["cat"]], d["name"])


def check():
    names = [menu_name(d) for d in PRESETS]
    assert len(set(names)) == len(names), "duplicate preset names"
    for n in names:
        assert len(n) <= 24, "menu name too long (%d): %s" % (len(n), n)
    for d in PRESETS:
        assert d["cat"] == "Init" or d["cat"] in CAT_NAMES, d
        assert d["type"] in TYPES, d
        for k in ("sat", "smash", "crunch", "soften"):
            assert 0 <= d[k] <= 100, d
        for k in ("low", "mid", "high"):
            assert d[k] is None or -24 <= d[k] <= 12, d
    assert pos(PRESETS[0]) == DEFAULT_POS and PRESETS[0]["type"] == "Tube", (pos(PRESETS[0]), DEFAULT_POS)   # Init = the params' defaults
    for c in CAT_NAMES[1:]:
        assert sum(1 for d in PRESETS if d["cat"] == c) > 0, c


def main():
    check()
    names = [menu_name(d) for d in PRESETS]
    cat_of = [0 if d["cat"] == "Init" else CAT_NAMES.index(d["cat"]) for d in PRESETS]   # 0 = Init / All only
    lines = ["// Generated by presets/factory.py -- edit the values there, not here.", "#pragma once", "#include <stdint.h>", "",
             "struct SatPreset { const char *name; uint8_t cat; uint8_t type; uint16_t pos[7]; };",
             "static const int kNumPresets = %d;" % len(PRESETS),
             "static const int kNumCats = %d;   // real categories (the browser adds \"All\" in front)" % len(CATS),
             "static const char *const kCatNames[kNumCats + 1] = {%s};" % ", ".join('"%s"' % c for c in CAT_NAMES),
             "static const SatPreset kPresets[kNumPresets] = {"]
    for d, n, c in zip(PRESETS, names, cat_of):
        lines.append('    {"%s", %d, %d, {%s}},' % (n.replace('"', '\\"'), c, TYPES.index(d["type"]), ", ".join(str(x) for x in pos(d))))
    lines.append("};")
    with open(os.path.join(ROOT, "src", "presets.h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    out = [{"name": n, "short": d["name"], "cat": d["cat"], "catIndex": c, "type": d["type"], "pos": pos(d)} for d, n, c in zip(PRESETS, names, cat_of)]
    with open(os.path.join(HERE, "presets.json"), "w", newline="\n") as f:
        json.dump({"cats": CAT_NAMES, "presets": out}, f, indent=1)
    db = lambda v: "Off" if v is None else ("%+g dB" % v if v else "0 dB")
    md = ["# Poli-Saturator factory presets", "", "%d presets in %d categories. Generated by `presets/factory.py`." % (len(PRESETS), len(CATS)),
          "Low / Mid / High = that band's drive relative to Saturation (Off = the band stays clean).", ""]
    for cat in ["Init"] + [c[1] for c in CATS]:
        md += ["## %s" % cat, "", "| Preset | Type | Saturation | Smash | Crunch | Soften | Low | Mid | High |", "|---|---|---|---|---|---|---|---|---|"]
        for d in PRESETS:
            if d["cat"] != cat:
                continue
            md.append("| %s | %s | %g %% | %g %% | %g %% | %g %% | %s | %s | %s |" % (d["name"], d["type"], d["sat"], d["smash"], d["crunch"],
                                                                                   d["soften"], db(d["low"]), db(d["mid"]), db(d["high"])))
        md.append("")
    with open(os.path.join(ROOT, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(md))
    print("presets: %d, categories: %s" % (len(PRESETS), ", ".join("%s=%d" % (c, sum(1 for d in PRESETS if d["cat"] == c)) for c in CAT_NAMES[1:])))


if __name__ == "__main__":
    main()
