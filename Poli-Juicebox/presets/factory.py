#!/usr/bin/env python3
"""Poli-Juicebox factory presets: the single source. Writes

    src/presets.h        the C++ table (knob positions 0..1000 in JuiceDSP's Pid order + the P&F Link switch)
    presets/presets.json names, categories, positions (what the generator and the docs read)
    PRESETS.md           every preset's physical values

    python3 presets/factory.py          (run from the Poli-Juicebox folder; generate_skin.py does it as a step)

Values below are PHYSICAL (semitones, Hz, dB, ms, %). Anything not given is the Init value. A preset sets every parameter, so
picking one always gives exactly its sound.

How they are built (voice-shaping practice; the numbers are starting points to tune by ear on your own voice):
  * Pitch stays at 0 (or +12 / -12 for the octave presets): the voice keeps its own melody, and what changes is its tone, resonance and
    overall shape: the Formant (1-5 semitones for the gender presets), the resonant peaks, the harmonics. Moving Pitch afterwards pulls
    the formants along by F-Track (P&F Link is on), so the voice stays natural over the whole range.
  * P&F Link = "Variable": the formant then follows the pitch by F-Track (formant = Formant knob + F-Track x Pitch).
  * Polish presets keep Pitch / Formant at 0 (the shifter is then a perfect bypass), so they only tame, warm and brighten.
  * Spectral Tamer: threshold -30..-40 dB for sibilance on a normal vocal level; Low end 100-200 Hz tames boom / proximity.
  * Harmonic Shape: Odd = 3rd/5th (clear, edgy), Even = 2nd/4th (warm); Color sets which part of the voice they grow from.
  * Natural Formant Filter: three +6 dB resonant peaks (F1 / F2 / F3), Q Off .. 14; centres near typical vowel formants.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

KEYS = ["pitch", "formant", "ftrack", "thresh", "lowend", "attack", "release", "odd", "even", "color",
        "f1c", "f1q", "f2c", "f2q", "f3c", "f3q", "fsmooth", "air", "natural", "mix",
        "hold", "rasp", "glide", "guard", "morph", "floor", "ceil", "overlap"]
DEFAULT_POS = [500, 500, 500, 1000, 0, 486, 463, 0, 0, 537, 569, 0, 483, 0, 473, 0, 300, 0, 400, 1000,
               400, 500, 400, 600, 486, 0, 790, 300]   # JuiceDSP.h kDefaultPos

# (menu prefix, category name shown in the browser)
CATS = [("MtF", "MtF"), ("FtM", "FtM"), ("Polish", "Polish"), ("Char", "Character"), ("FX", "Creative")]
CAT_NAMES = ["All"] + [c[1] for c in CATS]
PREFIX = {c[1]: c[0] for c in CATS}

# physical Init (the positions above, back-converted): None = Off
INIT = dict(pitch=0.0, formant=0.0, ftrack=50.0, link=0, thresh=0.0, lowend=None, attack=3.0, release=40.0, odd=0.0, even=0.0,
            color=1000.0, f1c=500.0, f1q=None, f2c=1700.0, f2q=None, f3c=3301.5, f3q=None, fsmooth=30.0, air=0.0, natural=40.0, mix=100.0,
            hold=60.0, rasp=50.0, glide=40.0, guard=60.0, morph=30.0, floor=70.0, ceil=700.0, overlap=30.0)


def lg(v, lo, hi):
    return 1000.0 * math.log(v / lo) / math.log(hi / lo)


def pos_of(key, v):
    """physical value -> knob position (the inverse of JuiceMap in JuiceDSP.h)"""
    if key in ("pitch", "formant"):
        p = 500.0 + v / 0.024
    elif key in ("ftrack", "odd", "even", "fsmooth", "air", "natural", "mix", "rasp", "glide", "guard", "overlap"):
        p = v * 10.0
    elif key == "hold":
        p = v / 0.15
    elif key == "morph":
        p = lg(v, 5.0, 200.0)
    elif key == "floor":
        p = lg(v, 70.0, 200.0)
    elif key == "ceil":
        p = lg(v, 250.0, 920.0)
    elif key == "thresh":
        p = (v + 60.0) / 0.06
    elif key == "lowend":
        p = 0.0 if v is None else lg(v, 80.0, 350.0)
    elif key == "attack":
        p = lg(v, 0.5, 20.0)
    elif key == "release":
        p = lg(v, 10.0, 200.0)
    elif key == "color":
        p = lg(v, 200.0, 4000.0)
    elif key == "f1c":
        p = lg(v, 200.0, 1000.0)
    elif key == "f2c":
        p = lg(v, 1000.0, 3000.0)
    elif key == "f3c":
        p = lg(v, 2500.0, 4500.0)
    elif key in ("f1q", "f2q", "f3q"):
        p = 0.0 if v is None else 6.0 + 994.0 * math.log(v / 0.7) / math.log(20.0)
    else:
        raise KeyError(key)
    return int(round(min(1000.0, max(0.0, p))))


def pos(d):
    return [pos_of(k, d[k]) for k in KEYS]


def P(cat, name, **kw):
    d = dict(INIT)
    d.update(kw)
    d["cat"], d["name"] = {"Char": "Character", "FX": "Creative"}.get(cat, cat), name
    return d


PRESETS = [dict(INIT, cat="Init", name="Init")]


def fm(formant, ft=40, **kw):
    """a voice-shape preset: the pitch stays where it is (0), the formants move by `formant` semitones, P&F Link is on with F-Track `ft` %, so if the
    user then moves Pitch the formants follow it (formant = Formant + F-Track x Pitch) and the voice stays natural over the whole range"""
    d = dict(pitch=0.0, formant=float(formant), ftrack=float(ft), link=1)
    d.update(kw)
    return d


def octave(up, ft, formant=0.0, **kw):
    """an octave shift: +12 / -12 with the formants following by F-Track"""
    d = dict(pitch=12.0 if up else -12.0, formant=float(formant), ftrack=float(ft), link=1)
    d.update(kw)
    return d


# the tracking page: a general-purpose tracker, a source-voice range for MtF (lower voices in) and FtM (higher voices in), and the raspy settings
TRK = dict(hold=70, rasp=55, glide=40, guard=65, morph=30, overlap=40)
MTF_RANGE = dict(floor=70, ceil=450)
FTM_RANGE = dict(floor=95, ceil=650)
RASPY = dict(hold=110, rasp=78, glide=55, guard=80, morph=60, overlap=55, fsmooth=55, natural=62)

# Pitch stays at 0 (or +12 / -12 for the octave presets): what changes is the tone, the resonance and the overall shape of the voice (formants,
# the three resonant peaks, harmonics, air). Only presets that sound clearly different from each other are kept.

# ---- MtF: a lower voice towards a higher, lighter one (formants up) --------------------------------------------------------------------
PRESETS += [
    P("MtF", "Subtle Lift", **fm(1.0, thresh=-34, air=15, fsmooth=40, natural=50, **TRK, **MTF_RANGE)),
    P("MtF", "Natural Speaker", **fm(2.0, thresh=-34, lowend=130, air=25, fsmooth=40, natural=55, f2c=1900, f2q=1.5, **TRK, **MTF_RANGE)),
    P("MtF", "Bright Speaker", **fm(2.5, thresh=-36, air=35, odd=10, color=2500, fsmooth=40, natural=55, f3c=3500, f3q=1.5, **TRK, **MTF_RANGE)),
    P("MtF", "Soft Alto", **fm(1.5, thresh=-33, lowend=150, even=20, color=450, f1c=600, f1q=1.2, air=15, fsmooth=45, natural=60, **TRK, **MTF_RANGE)),
    P("MtF", "Full Transform", **fm(3.5, thresh=-36, lowend=140, air=30, fsmooth=45, natural=60, f2c=2000, f2q=1.5, **TRK, **MTF_RANGE)),
    P("MtF", "Light Soprano", **fm(4.5, ft=45, thresh=-38, air=45, odd=12, color=3000, fsmooth=40, natural=60, **TRK, **MTF_RANGE)),
    P("MtF", "Raspy Smooth", **fm(2.5, thresh=-34, lowend=140, air=20, f2c=1900, f2q=1.5, **dict(TRK, **RASPY), **MTF_RANGE)),
]

# ---- FtM: a higher voice towards a lower, bigger one (formants down) -------------------------------------------------------------------
PRESETS += [
    P("FtM", "Subtle Drop", **fm(-1.0, thresh=-34, fsmooth=40, natural=50, **TRK, **FTM_RANGE)),
    P("FtM", "Natural Speaker", **fm(-2.0, thresh=-34, even=15, color=300, fsmooth=40, natural=55, **TRK, **FTM_RANGE)),
    P("FtM", "Deep Speaker", **fm(-3.0, thresh=-34, even=20, color=250, lowend=200, fsmooth=45, natural=60, **TRK, **FTM_RANGE)),
    P("FtM", "Full Transform", **fm(-4.0, ft=45, thresh=-34, even=25, odd=8, color=300, fsmooth=45, natural=60, **TRK, **FTM_RANGE)),
    P("FtM", "Warm Baritone", **fm(-2.5, even=30, odd=8, color=280, f1c=450, f1q=1.5, natural=60, fsmooth=40, **TRK, **FTM_RANGE)),
    P("FtM", "Raspy Smooth", **fm(-2.5, thresh=-34, even=22, color=280, lowend=220, **dict(TRK, **RASPY), **FTM_RANGE)),
]

# ---- Polish: no shifting (the shifter is bypassed), just tamer, colour and air; two formant-only size changes ---------------------------
PRESETS += [
    P("Polish", "Neutral Polish", thresh=-34, air=8, fsmooth=30, natural=40),
    P("Polish", "De-Ess Only", thresh=-38, attack=2.0, release=45),
    P("Polish", "Warm Vocal", thresh=-34, even=22, odd=5, color=400, air=5),
    P("Polish", "Airy Pop", thresh=-36, air=60, odd=10, color=3000),
    P("Polish", "Broadcast Deep", thresh=-32, lowend=180, even=28, color=220, f1c=420, f1q=1.2, air=6),
    P("Polish", "Radio Voice", thresh=-30, odd=30, color=1500, f1c=700, f1q=3.0, f2c=1700, f2q=3.0, air=0),
    P("Polish", "Vocal Presence", thresh=-34, odd=14, even=8, color=2200, f2c=2200, f2q=2.0, air=18),
    P("Polish", "Bigger Voice", **fm(-1.5, thresh=-34, even=15, color=300, natural=55, **TRK)),
    P("Polish", "Smaller Voice", **fm(1.5, thresh=-34, air=15, natural=55, **TRK)),
]

# ---- Character: a different kind of person (a different body: formants, resonance, texture) -------------------------------------------
PRESETS += [
    P("Char", "Child", **fm(5.0, ft=50, thresh=-38, air=35, natural=55, **TRK)),
    P("Char", "Elder", **fm(-0.5, thresh=-34, air=20, natural=95, fsmooth=20, **dict(TRK, hold=90, rasp=65))),
    P("Char", "Announcer", **fm(-1.5, thresh=-33, lowend=160, even=25, color=260, fsmooth=40, **TRK)),
    P("Char", "Giant", **octave(False, 50, thresh=-32, lowend=220, odd=15, even=15, color=220, fsmooth=50, natural=60, **TRK)),
    P("Char", "Ogre", **fm(-5.0, ft=50, thresh=-30, odd=35, even=20, color=300, fsmooth=40, natural=70, **dict(TRK, hold=100, rasp=70))),
    P("Char", "Chipmunk", **octave(True, 60, thresh=-40, air=30, natural=40, **TRK)),
]

# ---- Creative: octave blends, formant-only effects, textures ---------------------------------------------------------------------------
PRESETS += [
    P("FX", "Octave Up Blend", **octave(True, 20, mix=45, thresh=-36, **TRK)),
    P("FX", "Octave Down Blend", **octave(False, 20, mix=45, lowend=200, thresh=-34, **TRK)),
    P("FX", "Thin Voice", **fm(4.0, ft=40, thresh=-36, air=20, **TRK)),
    P("FX", "Alien", **fm(7.0, ft=40, odd=30, color=1800, f2c=2300, f2q=4.0, f3c=3800, f3q=4.0, thresh=-34, **TRK)),
    P("FX", "Old Telephone", thresh=-30, odd=25, color=1200, f1c=500, f1q=4.0, f2c=1800, f2q=4.0, f3c=3200, f3q=2.0, air=0, lowend=300),
    P("FX", "Breathy Air", air=90, natural=75, thresh=-34, fsmooth=50, even=10, color=800),
    P("FX", "Dark Whisper", **fm(-3.0, ft=40, air=70, natural=80, lowend=250, thresh=-30, mix=100, **dict(TRK, hold=100, rasp=75))),
]


def menu_name(d):
    return d["name"] if d["cat"] == "Init" else "%s: %s" % (PREFIX[d["cat"]], d["name"])


def fmt(key, v):
    if v is None:
        return "Off"
    if key in ("pitch", "formant"):
        return "%+.1f st" % v if v else "0"
    if key in ("ftrack", "odd", "even", "fsmooth", "air", "natural", "mix", "rasp", "glide", "guard", "overlap"):
        return "%g %%" % v
    if key == "thresh":
        return "%g dB" % v
    if key in ("attack", "release", "hold", "morph"):
        return "%g ms" % v
    if key.endswith("q"):
        return "%g" % v
    return ("%.1f kHz" % (v / 1000.0)) if v >= 1000 else ("%.0f Hz" % v)


def check():
    names = [menu_name(d) for d in PRESETS]
    assert len(set(names)) == len(names), "duplicate preset names"
    for n in names:
        assert len(n) <= 24, "menu name too long (%d): %s" % (len(n), n)
    for d in PRESETS:
        assert d["cat"] == "Init" or d["cat"] in CAT_NAMES, d
        assert d["pitch"] in (0.0, 12.0, -12.0), ("the pitch stays put (or an octave)", d["name"])
        assert -12 <= d["formant"] <= 12, d
        assert d["lowend"] is None or 80 <= d["lowend"] <= 350, d
        assert 200 <= d["color"] <= 4000 and 200 <= d["f1c"] <= 1000 and 1000 <= d["f2c"] <= 3000 and 2500 <= d["f3c"] <= 4500, d
        assert 0.5 <= d["attack"] <= 20 and 10 <= d["release"] <= 200, d
        for q in ("f1q", "f2q", "f3q"):
            assert d[q] is None or 0.7 <= d[q] <= 14, d
        assert -60 <= d["thresh"] <= 0, d
        assert 0 <= d["hold"] <= 150 and 5 <= d["morph"] <= 200 and 70 <= d["floor"] <= 200 and 250 <= d["ceil"] <= 920, d
        assert d["ceil"] > d["floor"] * 2, d
    assert pos(PRESETS[0]) == DEFAULT_POS and PRESETS[0]["link"] == 0, (pos(PRESETS[0]), DEFAULT_POS)   # Init = the params' defaults
    for c in CAT_NAMES[1:]:
        assert sum(1 for d in PRESETS if d["cat"] == c) > 0, c


def main():
    check()
    names = [menu_name(d) for d in PRESETS]
    cat_of = [0 if d["cat"] == "Init" else CAT_NAMES.index(d["cat"]) for d in PRESETS]   # 0 = Init / All only
    lines = ["// Generated by presets/factory.py -- edit the values there, not here.", "#pragma once", "#include <stdint.h>", "",
             "struct JuicePreset { const char *name; uint8_t cat; uint8_t link; uint16_t pos[%d]; };" % len(KEYS),
             "static const int kNumPresets = %d;" % len(PRESETS),
             "static const int kNumCats = %d;   // real categories (the browser adds \"All\" in front)" % len(CATS),
             "static const char *const kCatNames[kNumCats + 1] = {%s};" % ", ".join('"%s"' % c for c in CAT_NAMES),
             "static const JuicePreset kPresets[kNumPresets] = {"]
    for d, n, c in zip(PRESETS, names, cat_of):
        lines.append('    {"%s", %d, %d, {%s}},' % (n.replace('"', '\\"'), c, d["link"], ", ".join(str(x) for x in pos(d))))
    lines.append("};")
    with open(os.path.join(ROOT, "src", "presets.h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    out = [{"name": n, "short": d["name"], "cat": d["cat"], "catIndex": c, "link": d["link"], "pos": pos(d)} for d, n, c in zip(PRESETS, names, cat_of)]
    with open(os.path.join(HERE, "presets.json"), "w", newline="\n") as f:
        json.dump({"cats": CAT_NAMES, "keys": KEYS, "presets": out}, f, indent=1)
    heads = ["Pitch", "Formant", "F-Track", "Link", "Thresh", "Low end", "Atk", "Rel", "Odd", "Even", "Color", "F1", "F1 Q", "F2", "F2 Q", "F3", "F3 Q", "F-Smooth", "Air", "Natural", "Mix",
             "Hold", "Rasp", "Glide", "Guard", "Morph", "Floor", "Ceil", "Overlap"]
    order = ["pitch", "formant", "ftrack", "link", "thresh", "lowend", "attack", "release", "odd", "even", "color", "f1c", "f1q", "f2c", "f2q", "f3c", "f3q", "fsmooth", "air", "natural", "mix",
             "hold", "rasp", "glide", "guard", "morph", "floor", "ceil", "overlap"]
    md = ["# Poli-Juicebox factory presets", "", "%d presets in %d categories. Generated by `presets/factory.py`." % (len(PRESETS), len(CATS)),
          "Pitch / Formant in semitones. Link = P&F Link (Variable): the formant follows the pitch by F-Track. Q Off = that formant peak is bypassed.", ""]
    for cat in ["Init"] + [c[1] for c in CATS]:
        md += ["## %s" % cat, "", "| Preset | " + " | ".join(heads) + " |", "|---|" + "---|" * len(heads)]
        for d in PRESETS:
            if d["cat"] != cat:
                continue
            cells = [("On" if d["link"] else "Off") if k == "link" else fmt(k, d[k]) for k in order]
            md.append("| %s | %s |" % (d["name"], " | ".join(cells)))
        md.append("")
    with open(os.path.join(ROOT, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(md))
    print("presets: %d, categories: %s" % (len(PRESETS), ", ".join("%s=%d" % (c, sum(1 for d in PRESETS if d["cat"] == c)) for c in CAT_NAMES[1:])))


if __name__ == "__main__":
    main()
