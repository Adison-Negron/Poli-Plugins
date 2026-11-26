#!/usr/bin/env python3
"""Poli-Chorus factory presets: the single source. Writes

    src/presets.h        the C++ table (Mode + knob positions 0..1000 in ChoDSP's order)
    presets/presets.json names, categories, physical values (what the generator and the docs read)
    PRESETS.md           every preset's values

    python3 presets/factory.py          (run from the Poli-Chorus folder; build.sh does it as step 1)

Values below are PHYSICAL: Rate in Hz, Amount / Feedback / Width / Warmth / Mix in %, LP Filter in Hz (None = Off), Output in dB.
A preset sets every parameter and the Mode, so picking one always gives exactly its sound.

How the presets are built (from how Ableton's Chorus-Ensemble, the Juno / Dimension / Solina style chorus pedals and ensembles are used):
  * the Mode is the first decision: Classic = two voices, the clean "juno" shimmer; Ensemble = three voices, strings and pads;
    Vibrato = pitch wobble only (Mix 100 %, the dry would only mask it).
  * Rate under 1 Hz and Amount 30-70 % is the useful chorus zone; above 4 Hz it turns into a rotary / vibrato effect.
  * Width 100 % for pads and keys, 25-70 % for things that must stay solid in mono (bass-ish, vocals, guitars).
  * LP Filter and Warmth darken the wet signal like a bucket-brigade chorus does; Feedback adds the resonant, jet-like colour.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

MODES = ["Classic", "Ensemble", "Vibrato"]
KNOBS = ["rate", "amount", "feedback", "width", "lpf", "warmth", "output", "mix"]
DEFAULT_POS = [523, 500, 0, 600, 1000, 250, 667, 500]   # ChoDSP.h kDefaultPos

# (menu prefix, category name shown in the browser)
CATS = [("Cls", "Classic"), ("Ens", "Ensemble"), ("Vib", "Vibrato"), ("Gtr", "Guitar & Keys"), ("Vox", "Vocals & Pads"), ("FX", "Creative")]
CAT_NAMES = ["All"] + [c[1] for c in CATS]
PREFIX = {c[1]: c[0] for c in CATS}

INIT = dict(mode="Classic", rate=0.8, amount=50.0, feedback=0.0, width=60.0, lpf=None, warmth=25.0, output=0.0, mix=50.0)


def P(cat, name, mode, rate, amount, feedback, width, lpf, warmth, output, mix):
    return dict(cat=cat, name=name, mode=mode, rate=rate, amount=amount, feedback=feedback, width=width, lpf=lpf, warmth=warmth,
                output=output, mix=mix)


def rate_pos(hz):
    return 1000.0 * math.log(hz / 0.05) / math.log(200.0)


def lpf_pos(hz):
    return 1000.0 if hz is None else 1000.0 * math.log(hz / 800.0) / math.log(25.0)


def out_pos(db):
    return 667.0 * (db + 24.0) / 24.0 if db <= 0 else 667.0 + 333.0 * db / 12.0


def pos(d):
    v = [rate_pos(d["rate"]), d["amount"] * 10, d["feedback"] * 10, d["width"] * 10, lpf_pos(d["lpf"]), d["warmth"] * 10, out_pos(d["output"]),
         d["mix"] * 10]
    return [int(round(min(1000.0, max(0.0, x)))) for x in v]


C, E, V = "Classic", "Ensemble", "Vibrato"
N = None   # LP Filter Off
PRESETS = [
    dict(cat="Init", name="Init", **INIT),
    # ---- Classic (two voices) ---------------------------------------------------------------------------------------
    P("Classic", "Silver Shimmer", C, 0.9, 45, 0, 70, N, 20, 0, 50),
    P("Classic", "Juno Lush", C, 0.6, 70, 12, 100, 9000, 35, 0, 50),
    P("Classic", "Subtle Thickener", C, 0.5, 22, 0, 55, N, 10, 0, 35),
    P("Classic", "Wide Dimension", C, 0.35, 38, 10, 100, 12000, 25, 0, 45),
    P("Classic", "Slow Swirl", C, 0.15, 80, 30, 85, 7500, 30, 0, 55),
    P("Classic", "Bright Sparkle", C, 2.2, 30, 0, 80, N, 0, 0, 40),
    P("Classic", "Studio Sweet", C, 1.1, 40, 18, 75, 14000, 15, 0, 50),
    P("Classic", "Mono Safe", C, 0.8, 35, 0, 25, N, 15, 0, 40),
    P("Classic", "Rotor Glide", C, 4.5, 20, 0, 60, N, 10, 0, 45),
    # ---- Ensemble (three voices) ------------------------------------------------------------------------------------
    P("Ensemble", "String Machine", E, 0.55, 60, 0, 90, 10000, 45, 0, 60),
    P("Ensemble", "Solina Dream", E, 0.45, 75, 6, 100, 8000, 50, 0, 65),
    P("Ensemble", "Big Ensemble", E, 0.7, 85, 12, 100, 11000, 30, 0, 70),
    P("Ensemble", "Choir Pad", E, 0.35, 55, 15, 100, N, 25, 0, 60),
    P("Ensemble", "Subtle Strings", E, 0.6, 30, 0, 80, N, 15, 0, 45),
    P("Ensemble", "Detuned Cloud", E, 1.4, 65, 25, 100, 6500, 40, 0, 70),
    P("Ensemble", "Lush Wash", E, 0.25, 90, 35, 100, 5500, 55, 0, 75),
    P("Ensemble", "Silky Pad", E, 0.5, 50, 10, 85, N, 20, 0, 55),
    P("Ensemble", "Wide Hall", E, 0.3, 70, 18, 100, 9000, 35, 0, 65),
    # ---- Vibrato (pitch only) ---------------------------------------------------------------------------------------
    P("Vibrato", "Gentle Vibrato", V, 5.2, 18, 0, 40, N, 0, 0, 100),
    P("Vibrato", "Tape Wobble", V, 0.9, 35, 0, 60, 9000, 40, 0, 100),
    P("Vibrato", "Fast Flutter", V, 8.5, 12, 0, 50, N, 10, 0, 100),
    P("Vibrato", "Slow Pitch Sway", V, 0.3, 55, 0, 70, N, 10, 0, 100),
    P("Vibrato", "Rotary Spin", V, 6.6, 45, 0, 100, 10000, 30, 0, 100),
    P("Vibrato", "Seasick", V, 0.2, 100, 15, 80, 8000, 20, 0, 100),
    P("Vibrato", "Lo-Fi Warble", V, 2.0, 60, 0, 60, 5000, 60, 0, 100),
    P("Vibrato", "Stereo Vibrato", V, 5.5, 25, 0, 100, N, 0, 0, 100),
    P("Vibrato", "Half Wet Vibe", V, 5.0, 30, 0, 60, N, 15, 0, 55),
    # ---- Guitar & Keys ----------------------------------------------------------------------------------------------
    P("Guitar & Keys", "Clean Guitar Shine", C, 0.8, 35, 0, 80, N, 15, 0, 45),
    P("Guitar & Keys", "Dreamy Guitar", C, 0.4, 55, 25, 90, 9000, 25, 0, 50),
    P("Guitar & Keys", "80s Rhythm", C, 0.7, 60, 20, 100, 12000, 30, 0, 55),
    P("Guitar & Keys", "E-Piano Sheen", C, 0.6, 40, 0, 90, N, 25, 0, 45),
    P("Guitar & Keys", "Rhodes Wide", E, 0.45, 45, 5, 100, N, 25, 0, 50),
    P("Guitar & Keys", "Organ Shimmer", V, 6.0, 35, 0, 90, N, 20, 0, 65),
    P("Guitar & Keys", "Acoustic Sparkle", C, 1.0, 25, 0, 70, N, 5, 0, 35),
    P("Guitar & Keys", "Jangle", C, 1.8, 28, 10, 85, N, 0, 0, 40),
    # ---- Vocals & Pads ----------------------------------------------------------------------------------------------
    P("Vocals & Pads", "Vocal Thickener", C, 0.6, 20, 0, 65, N, 10, 0, 30),
    P("Vocals & Pads", "Backing Wide", E, 0.5, 40, 0, 100, N, 15, 0, 45),
    P("Vocals & Pads", "Airy Doubler", C, 1.3, 18, 0, 80, N, 0, 0, 35),
    P("Vocals & Pads", "Soft Pad Bloom", E, 0.3, 65, 15, 100, 10000, 25, 0, 60),
    P("Vocals & Pads", "Warm Choir", E, 0.4, 50, 10, 90, 8500, 40, 0, 55),
    P("Vocals & Pads", "Dream Vox", C, 0.25, 70, 25, 100, 8000, 30, 0, 50),
    P("Vocals & Pads", "Synth Pad Wide", E, 0.4, 60, 8, 100, N, 20, 0, 55),
    # ---- Creative ---------------------------------------------------------------------------------------------------
    P("Creative", "Underwater", C, 0.2, 100, 70, 90, 2500, 50, 0, 60),
    P("Creative", "Metal Wash", C, 3.0, 70, 85, 100, N, 20, -2, 70),
    P("Creative", "Robot Choir", E, 6.0, 40, 50, 100, N, 25, -1, 70),
    P("Creative", "Warped Tape", V, 0.5, 100, 35, 85, 4000, 70, 0, 100),
    P("Creative", "Jet Chorus", C, 0.1, 60, 90, 80, N, 15, -3, 60),
    P("Creative", "Spring Boing", V, 9.0, 70, 0, 60, N, 25, 0, 100),
    P("Creative", "Space Shimmer", E, 0.8, 80, 60, 100, 7000, 30, -1, 70),
    P("Creative", "Broken Cassette", V, 1.5, 85, 25, 70, 3500, 80, 0, 100),
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
        assert d["mode"] in MODES, d
        assert 0.05 <= d["rate"] <= 10.0, d
        for k in ("amount", "feedback", "width", "warmth", "mix"):
            assert 0 <= d[k] <= 100, d
        assert d["lpf"] is None or 800 <= d["lpf"] <= 20000, d
        assert -24 <= d["output"] <= 12, d
    assert pos(PRESETS[0]) == DEFAULT_POS and PRESETS[0]["mode"] == "Classic", (pos(PRESETS[0]), DEFAULT_POS)   # Init = the params' defaults
    for c in CAT_NAMES[1:]:
        assert sum(1 for d in PRESETS if d["cat"] == c) > 0, c


def main():
    check()
    names = [menu_name(d) for d in PRESETS]
    cat_of = [0 if d["cat"] == "Init" else CAT_NAMES.index(d["cat"]) for d in PRESETS]   # 0 = Init / All only
    lines = ["// Generated by presets/factory.py -- edit the values there, not here.", "#pragma once", "#include <stdint.h>", "",
             "struct ChoPreset { const char *name; uint8_t cat; uint8_t mode; uint16_t pos[8]; };",
             "static const int kNumPresets = %d;" % len(PRESETS),
             "static const int kNumCats = %d;   // real categories (the browser adds \"All\" in front)" % len(CATS),
             "static const char *const kCatNames[kNumCats + 1] = {%s};" % ", ".join('"%s"' % c for c in CAT_NAMES),
             "static const ChoPreset kPresets[kNumPresets] = {"]
    for d, n, c in zip(PRESETS, names, cat_of):
        lines.append('    {"%s", %d, %d, {%s}},' % (n.replace('"', '\\"'), c, MODES.index(d["mode"]), ", ".join(str(x) for x in pos(d))))
    lines.append("};")
    with open(os.path.join(ROOT, "src", "presets.h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    out = [{"name": n, "short": d["name"], "cat": d["cat"], "catIndex": c, "mode": d["mode"], "pos": pos(d)} for d, n, c in zip(PRESETS, names, cat_of)]
    with open(os.path.join(HERE, "presets.json"), "w", newline="\n") as f:
        json.dump({"cats": CAT_NAMES, "presets": out}, f, indent=1)
    md = ["# Poli-Chorus factory presets", "", "%d presets in %d categories. Generated by `presets/factory.py`." % (len(PRESETS), len(CATS)),
          "Rate in Hz, LP Filter in Hz (Off = open), Output in dB, the rest in %.", ""]
    for cat in ["Init"] + [c[1] for c in CATS]:
        md += ["## %s" % cat, "", "| Preset | Mode | Rate | Amount | Feedback | Width | LP Filter | Warmth | Output | Mix |",
               "|---|---|---|---|---|---|---|---|---|---|"]
        for d in PRESETS:
            if d["cat"] != cat:
                continue
            md.append("| %s | %s | %g Hz | %g %% | %g %% | %g %% | %s | %g %% | %g dB | %g %% |" % (
                d["name"], d["mode"], d["rate"], d["amount"], d["feedback"], d["width"], "Off" if d["lpf"] is None else "%g Hz" % d["lpf"],
                d["warmth"], d["output"], d["mix"]))
        md.append("")
    with open(os.path.join(ROOT, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(md))
    print("presets: %d, categories: %s" % (len(PRESETS), ", ".join("%s=%d" % (c, sum(1 for d in PRESETS if d["cat"] == c)) for c in CAT_NAMES[1:])))


if __name__ == "__main__":
    main()
