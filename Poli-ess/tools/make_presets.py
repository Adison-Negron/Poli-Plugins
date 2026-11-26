#!/usr/bin/env python3
"""Writes vst/presets.json (MPC's PRESET menu, via vst.json "presets"). Edit the table here, not the JSON.
Attack and Release are knob POSITIONS (0..100, log scale, engine.cpp posToMs): this converts from milliseconds.

Sources for the starting points (2026-11-04):
  * Waves DeEsser factory presets (manual): Default 5506 Hz split, Male Ess 4500 Hz, Male Shh 3385 Hz,
    Female Ess 6779 Hz, Female Shh 5077 Hz.
  * Sibilance 3-6 kHz in male voices, 6-8 kHz in female (Wikipedia "De-essing"); Pro-DS manual: a typical "s"
    around 8-10 kHz; Softube DeEsser manual: often 4-10 kHz.
  * Hi-hats: de-esser cutoff around 10-12 kHz, resonances 4-8 kHz (iZotope, "How to mix hi-hats"). Poli-Ess's
    Freq stops at 10 kHz, so the hat preset sits at 9 kHz.
  * Full-mix / allround and harsh-cymbals-in-a-mix presets exist in FabFilter Pro-DS (Allround mode) and Softube
    DeEsser; parallel (dry/wet) de-essing in Weiss Deess and Softube DeEsser.
  * De-esser timing: fast (< 10 ms) rise/fall (Wikipedia); Steinberg DeEsser release 1-1000 ms.
Thresholds depend on the recording level: every preset is a starting point. Set Threshold so the meter moves on
the S's only."""
import json, math, os

A_LO, A_HI, R_LO, R_HI = 0.1, 10.0, 5.0, 500.0
def pos(ms, lo, hi): return round(100.0 * math.log(ms / lo) / math.log(hi / lo), 4)

#            name                  Freq  Thresh Int  Attack Release Knee Mix   why
PRESETS = [
    ("Init",                      5000, -20, 50, 0.8,  35, 0, 100, "the defaults (1.0.4's sound)"),
    ("Male Ess",                  4500, -24, 55, 0.5,  40, 3, 100, "Waves DeEsser 'Male Ess' 4500 Hz"),
    ("Male Shh",                  3400, -24, 50, 1.0,  70, 4, 100, "Waves 'Male Shh' 3385 Hz; 'sh' lasts longer -> slower release"),
    ("Female Ess",                6800, -24, 55, 0.5,  40, 3, 100, "Waves 'Female Ess' 6779 Hz"),
    ("Female Shh",                5100, -24, 50, 1.0,  70, 4, 100, "Waves 'Female Shh' 5077 Hz"),
    ("Bright Vocal 8k",           8000, -26, 55, 0.5,  40, 3, 100, "Pro-DS: a typical 's' around 8-10 kHz (airy, close-miked vocals)"),
    ("Gentle Vocal",              6000, -18, 30, 1.5,  60, 8, 100, "transparent: low strength, wide soft knee, slower times"),
    ("Heavy Sibilance",           5500, -30, 85, 0.2,  30, 0, 100, "harsh / spitty vocals: deep, fast, hard knee"),
    ("Parallel De-Ess",           6000, -28, 75, 0.3,  40, 4,  50, "strong reduction blended 50/50 (Weiss/Softube parallel mix)"),
    ("Spoken Word",               5500, -22, 45, 1.0,  50, 6, 100, "dialogue / podcast: moderate, smooth"),
    ("Hi-Hat Tamer",              9000, -26, 60, 0.2,  25, 2, 100, "hat sizzle ~10-12 kHz (iZotope); Freq tops out at 10 kHz"),
    ("Cymbals in a Mix",          7000, -22, 40, 0.5,  80, 8,  70, "full mix / drum bus (Pro-DS Allround, Softube 'harsh cymbals'): gentle, blended"),
]

out = {"presets": []}
for name, f, t, i, a, r, k, m, why in PRESETS:
    assert len(name) <= 24 and 2000 <= f <= 10000 and -40 <= t <= 0 and 0 <= i <= 100
    assert A_LO <= a <= A_HI and R_LO <= r <= R_HI and 0 <= k <= 12 and 0 <= m <= 100
    out["presets"].append({"name": name, "about": why, "values": {
        "intensity": i, "thresh": t, "freq": f, "diff": "Off",
        "attack": pos(a, A_LO, A_HI), "release": pos(r, R_LO, R_HI), "knee": k, "mix": m}})
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "..", "vst", "presets.json"), "w", newline="\n") as fh:
    json.dump(out, fh, indent=1)
    fh.write("\n")
print("vst/presets.json: %d presets" % len(PRESETS))
