#!/usr/bin/env python3
"""Poli-tea factory presets: the single source. Writes

    src/presets.h         the C++ table (knob positions 0..1000 in TeaDSP's order, plus the sidechain switch)
    presets/presets.json  names, categories, physical values (what the generator and the docs read)
    PRESETS.md            every preset's values and what it is for

    python3 presets/factory.py          (run from the Poli-tea folder; build.sh does it as step 1)

Values are PHYSICAL: Input / Limiter Threshold / Output in dB, the rest in % (Low / Mid / High are -100..+100 %, which is
+-6 / +-4 / +-6 dB of tone; Amount 0..200). Anything not given is the Init value. A preset sets every parameter, so picking
one always gives exactly its sound. `sc` = 1 keys the compressors from the raw input ("External").

How to read them: Input is gain staging (aim for the green zone of the input meter, then trim here); Drive and Amount set how
much tape / tube colour and multiband glue you get; Limiter Threshold sets how hard the limiter is pushed (the lower, the louder)
and Output is the ceiling. Loudness presets sit at -0.3 dB, streaming ones at -1 dB (headroom for lossy encodes).
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

KNOBS = ["input", "drive", "low", "mid", "high", "amount", "ldrive", "lthresh", "output"]
DEFAULT_POS = [500, 300, 500, 500, 500, 500, 200, 889, 988]   # TeaDSP.h kDefaultPos

# (menu prefix, category name shown in the browser)
CATS = [("Master", "Master"), ("Glue", "Glue"), ("Loud", "Loud"), ("Warm", "Warm"), ("Air", "Air"), ("Wide", "Wide"),
        ("Beats", "Beats"), ("Lo-Fi", "Lo-Fi")]

INIT = dict(inp=0.0, drv=30.0, lo=0.0, mi=0.0, hi=0.0, amt=100.0, ld=20.0, lt=-2.0, out=-0.3, sc=0, use="Neutral starting point")


def P(cat, name, use, **kw):
    d = dict(INIT)
    d.update(kw)
    d["cat"] = cat
    d["name"] = name
    d["use"] = use
    return d


def r(x):
    return int(math.floor(x + 0.5))


def pos(d):
    """physical -> knob positions (0..1000), in KNOBS order"""
    v = [(d["inp"] + 18.0) / 0.036, d["drv"] * 10.0, 500.0 + d["lo"] * 5.0, 500.0 + d["mi"] * 5.0, 500.0 + d["hi"] * 5.0,
         d["amt"] * 5.0, d["ld"] * 10.0, (d["lt"] + 18.0) / 0.018, (d["out"] + 24.0) / 0.024]
    return [min(1000, max(0, r(x))) for x in v]


PRESETS = [
    dict(INIT, cat="Init", name="Init"),
    # ---- Master: whole-mix mastering chains ----
    P("Master", "Green Tea", "All-round gentle master: a little weight and air, 2 dB of limiting", lo=10, hi=15, amt=80, drv=20, ld=25, lt=-2.5, out=-0.3),
    P("Master", "Earl Grey", "Mix bus polish: slight mid cut, tight lows, light tape", lo=15, mi=-5, hi=10, amt=100, drv=30, ld=30, lt=-2.0, out=-0.5),
    P("Master", "Matcha Streaming", "Streaming-ready: ceiling -1 dB, moderate loudness", lo=10, hi=10, amt=90, drv=25, ld=35, lt=-3.5, out=-1.0),
    P("Master", "Jasmine Clarity", "Opens a cloudy mix: mids pulled back, air lifted", mi=-12, hi=25, amt=70, drv=15, ld=20, lt=-2.0, out=-0.5),
    P("Master", "Oolong Balanced", "Fuller, more present master with a firmer limiter", lo=20, mi=5, hi=15, amt=110, drv=35, ld=40, lt=-4.0, out=-0.3),
    P("Master", "Chamomile Gentle", "Barely there: the lightest touch of glue and ceiling", hi=5, amt=50, drv=10, ld=10, lt=-1.0, out=-0.5),
    P("Master", "Pu-erh Heavy", "Dense, weighty master for big genres", lo=30, mi=10, amt=140, drv=50, ld=50, lt=-5.0, out=-0.3),
    P("Master", "White Tea Clear", "Transparent: ceiling protection and a whisper of width", hi=8, amt=40, drv=5, ld=15, lt=-1.5, out=-0.3),
    # ---- Glue: bus cohesion, not loudness ----
    P("Glue", "Chai Glue", "Classic bus glue for a full mix", lo=5, mi=5, amt=90, drv=20, ld=15, lt=-1.0),
    P("Glue", "Rooibos Soft", "Soft, slow glue for acoustic and sparse material", mi=3, amt=60, drv=15, ld=10, lt=-0.5),
    P("Glue", "Assam Tight", "Tighter, more controlled bus: more compression", lo=10, amt=130, drv=25, ld=20, lt=-1.5),
    P("Glue", "Hojicha Roasted", "Roasted glue: more tube body in the mids", lo=10, mi=10, hi=-5, amt=110, drv=40, ld=15, lt=-1.0),
    P("Glue", "Darjeeling Light", "Very light cohesion for already polished mixes", hi=5, amt=45, drv=10, ld=10, lt=-0.5),
    P("Glue", "Ceylon Cohesion", "Keeps stems sitting together, low end steady", lo=15, amt=100, drv=25, ld=20, lt=-1.5),
    P("Glue", "Raw Key Glue", "External-style key: compressors listen to the raw input, so Input and EQ don't change how hard they work",
      amt=100, drv=25, lo=10, ld=15, lt=-1.0, sc=1),
    # ---- Loud: commercial loudness ----
    P("Loud", "Builder's Brew", "Strong, even loudness for pop and rock", lo=15, mi=5, hi=15, amt=120, drv=40, ld=60, lt=-6.0),
    P("Loud", "Masala Max", "Hot and punchy: club-ready loudness", lo=25, mi=10, hi=20, amt=150, drv=60, ld=70, lt=-7.5),
    P("Loud", "Boba Bomb", "Maximum push: extreme loudness, expect colour", lo=30, mi=8, hi=15, amt=170, drv=70, ld=85, lt=-9.0),
    P("Loud", "Thai Tea Punch", "Loud with the transients kept alive", lo=20, hi=10, amt=130, drv=35, ld=50, lt=-6.5),
    P("Loud", "Brick Tea Wall", "Wall of sound for hard genres", lo=20, mi=15, hi=10, amt=160, drv=55, ld=80, lt=-8.5),
    P("Loud", "Quick Steep", "Quick +4 dB of safe loudness", amt=90, drv=20, ld=35, lt=-4.0),
    P("Loud", "Kombucha Kick", "Bass-forward loudness, a bit fizzy on top", lo=35, hi=12, amt=140, drv=45, ld=65, lt=-7.0),
    # ---- Warm: tape and tube flavours ----
    P("Warm", "Tape Tea", "Tape-style saturation and a soft top", lo=20, hi=-10, amt=100, drv=65, ld=15, lt=-1.5),
    P("Warm", "Tube Latte", "Creamy tube harmonics in the mids", lo=10, mi=12, amt=110, drv=60, ld=15, lt=-1.5),
    P("Warm", "Honey Tea", "Sweet, rounded top end", lo=12, mi=5, hi=-18, amt=90, drv=45, ld=15, lt=-1.0),
    P("Warm", "Roasted Barley", "Thick and toasty: a lot of low-mid colour", lo=25, mi=8, hi=-12, amt=130, drv=70, ld=20, lt=-2.0),
    P("Warm", "Cinnamon Cuppa", "Warm with a touch of spice in the highs", lo=15, hi=5, amt=105, drv=55, ld=15, lt=-1.5),
    P("Warm", "Vintage Pot", "Old-console feel: gentle everything", lo=12, mi=-3, hi=-8, amt=95, drv=50, ld=10, lt=-1.0),
    P("Warm", "Smoky Lapsang", "Dark and smoky: heavy colour, rolled-off air", lo=22, mi=10, hi=-30, amt=140, drv=80, ld=25, lt=-2.5),
    # ---- Air: openness and shine ----
    P("Air", "Silver Needle", "Silky air on top without harshness", hi=45, amt=80, drv=20, ld=20, lt=-2.0),
    P("Air", "Peach Oolong", "Fruity brightness: highs and a little presence", mi=5, hi=35, amt=90, drv=25, ld=25, lt=-2.0),
    P("Air", "Mint Sparkle", "Cool, sparkling top end", lo=-5, hi=55, amt=100, drv=20, ld=20, lt=-2.0),
    P("Air", "Dew Drop", "Light lift for dull recordings", mi=-5, hi=30, amt=70, drv=10, ld=15, lt=-1.5),
    P("Air", "Lemon Zest", "Bright and forward, vocals pop", mi=10, hi=40, amt=100, drv=30, ld=30, lt=-3.0),
    P("Air", "Frosted Glass", "Crystal highs with a clean low end", lo=8, mi=-8, hi=60, amt=90, drv=15, ld=25, lt=-2.5),
    # ---- Wide: stereo image ----
    P("Wide", "Jasmine Pearl", "Wide, airy mix; the lows stay mono", hi=25, amt=150, drv=20, ld=25, lt=-2.0),
    P("Wide", "Sakura Spread", "Gentle widening for stereo-poor mixes", hi=12, amt=120, drv=15, ld=15, lt=-1.5),
    P("Wide", "Milk Tea Stereo", "Creamy, wide mids and highs", mi=8, hi=15, amt=140, drv=35, ld=20, lt=-2.0),
    P("Wide", "Ocean Mist", "Maximum width with air", hi=35, amt=190, drv=15, ld=25, lt=-2.5),
    P("Wide", "Mono-Safe Tea", "Narrower, very mono-friendly: little widening", amt=45, drv=15, ld=15, lt=-1.5),
    P("Wide", "Festival Wide", "Big and wide for large speakers", lo=20, hi=20, amt=165, drv=40, ld=45, lt=-5.0),
    # ---- Beats: drum bus and beat-making ----
    P("Beats", "Boba Drums", "Punchy drum bus with fat lows", lo=20, mi=5, hi=8, amt=120, drv=40, ld=35, lt=-3.5),
    P("Beats", "Boom Bap Brew", "Dusty boom-bap bus: warm and heavy", lo=25, hi=-8, amt=120, drv=60, ld=30, lt=-3.0),
    P("Beats", "Trap Tea", "Weighty sub with crisp hats", lo=30, hi=20, amt=130, drv=45, ld=55, lt=-6.0),
    P("Beats", "Punchy Pearls", "Snappy transients and tight lows", lo=15, mi=8, hi=12, amt=110, drv=35, ld=30, lt=-3.0),
    P("Beats", "Sample Smash", "Glues chopped samples into one block", lo=15, mi=10, amt=140, drv=65, ld=40, lt=-4.5),
    P("Beats", "House Steep", "Four-on-the-floor bus: steady low end", lo=25, hi=15, amt=115, drv=35, ld=45, lt=-5.0),
    P("Beats", "Break Brew", "Breakbeat bus: lively, a bit gritty", lo=12, mi=8, hi=10, amt=135, drv=55, ld=35, lt=-4.0),
    P("Beats", "MPC Master", "Quick beat-tape master: warm, loud, safe", lo=20, hi=10, amt=115, drv=50, ld=50, lt=-5.5),
    # ---- Lo-Fi: creative and character ----
    P("Lo-Fi", "Dusty Tea", "Dark and dusty: rolled-off highs, tape warmth", lo=10, hi=-45, amt=150, drv=80, ld=20, lt=-2.0),
    P("Lo-Fi", "Cassette Cup", "Cassette-ish squeeze and fuzz", lo=15, mi=15, hi=-25, amt=160, drv=85, ld=35, lt=-4.0),
    P("Lo-Fi", "Crunchy Boba", "Crunch: heavy drive and compression", lo=10, hi=10, amt=180, drv=100, ld=70, lt=-8.0),
    P("Lo-Fi", "Telephone Tea", "Mid-focused, thin: lows and highs pulled down", lo=-45, mi=35, hi=-35, amt=130, drv=60, ld=30, lt=-3.0),
    P("Lo-Fi", "Mud Puddle", "Murky and thick: lows up, highs gone", lo=40, mi=-10, hi=-50, amt=140, drv=70, ld=25, lt=-2.5),
    P("Lo-Fi", "Radio Brew", "Broadcast-style squeeze and presence", lo=-10, mi=20, hi=20, amt=170, drv=55, ld=60, lt=-7.0),
    P("Lo-Fi", "Smash Tea Bag", "Everything pushed: for effect, not subtlety", lo=20, mi=15, hi=15, amt=200, drv=90, ld=90, lt=-10.0),
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
        assert -18 <= d["inp"] <= 18 and 0 <= d["drv"] <= 100 and -100 <= d["lo"] <= 100 and -100 <= d["mi"] <= 100, d
        assert -100 <= d["hi"] <= 100 and 0 <= d["amt"] <= 200 and 0 <= d["ld"] <= 100 and -18 <= d["lt"] <= 0, d
        assert -24 <= d["out"] <= 0 and d["sc"] in (0, 1), d
        assert len(d["name"]) <= 17, "name too long for the header selector: " + d["name"]
    assert pos(PRESETS[0]) == DEFAULT_POS, (pos(PRESETS[0]), DEFAULT_POS)   # Init = the params' defaults


def main():
    check()
    names = [menu_name(d) for d in PRESETS]
    cat_of = [0 if d["cat"] == "Init" else CAT_NAMES.index(d["cat"]) for d in PRESETS]   # 0 = Init / All only
    lines = ["// Generated by presets/factory.py -- edit the values there, not here.", "#pragma once", "#include <stdint.h>", "",
             "struct TeaPreset { const char *name; uint8_t cat; uint8_t sc; uint16_t pos[9]; };",
             "static const int kNumPresets = %d;" % len(PRESETS),
             "static const int kNumCats = %d;   // real categories (the browser adds \"All\" in front)" % len(CATS),
             "static const char *const kCatNames[kNumCats + 1] = {%s};" % ", ".join('"%s"' % c for c in CAT_NAMES),
             "static const TeaPreset kPresets[kNumPresets] = {"]
    for d, n, c in zip(PRESETS, names, cat_of):
        lines.append('    {"%s", %d, %d, {%s}},' % (n.replace('"', '\\"'), c, d["sc"], ", ".join(str(x) for x in pos(d))))
    lines.append("};")
    with open(os.path.join(ROOT, "src", "presets.h"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    out = [{"name": n, "short": d["name"], "cat": d["cat"], "catIndex": c, "pos": pos(d), "sc": d["sc"], "use": d["use"]}
           for d, n, c in zip(PRESETS, names, cat_of)]
    with open(os.path.join(HERE, "presets.json"), "w", newline="\n") as f:
        json.dump({"cats": CAT_NAMES, "presets": out}, f, indent=1)
    md = ["# Poli-tea factory presets", "",
          "%d presets in %d categories (+ Init). Generated by `presets/factory.py`. Low / Mid / High are -100..+100 %% "
          "(+-6 / +-4 / +-6 dB); Amount is 0..200 %%; Input, Limiter Threshold and Output are in dB." % (len(PRESETS), len(CATS)), ""]
    for cat in ["Init"] + [c[1] for c in CATS]:
        md += ["## %s" % cat, "", "| Preset | Input | Drive | Low | Mid | High | Amount | Lim Drive | Lim Thresh | Output | Sidechain | Use |",
               "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for d in PRESETS:
            if d["cat"] != cat:
                continue
            md.append("| %s | %+g dB | %g %% | %+g | %+g | %+g | %g %% | %g %% | %g dB | %g dB | %s | %s |" % (
                d["name"], d["inp"], d["drv"], d["lo"], d["mi"], d["hi"], d["amt"], d["ld"], d["lt"], d["out"],
                "External" if d["sc"] else "Internal", d["use"]))
        md.append("")
    with open(os.path.join(ROOT, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(md))
    print("presets: %d, categories: %s" % (len(PRESETS), ", ".join("%s=%d" % (c, sum(1 for d in PRESETS if d["cat"] == c)) for c in CAT_NAMES[1:])))


if __name__ == "__main__":
    main()
