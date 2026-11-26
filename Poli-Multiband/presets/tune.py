#!/usr/bin/env python3
"""Tune the Poli-Multiband factory presets on reference audio: presets/spec.py (intent + targets) -> presets/factory.json.

    python3 presets/tune.py --audio <dir with ref_vocal.wav ref_drums.wav ref_808.wav ref_mix.wav> [--tool <preset_tool>]

For every preset that isn't "fixed", with the crossovers, ratios, times and knees from spec.py:
  1. Thresh per band: bisection until the band's gain reduction (90th percentile while active, or 97th for "dn97")
     reaches the target ("dn").
  2. Upward Thresh per band (never above Thresh): bisection until the upward boost (90th percentile) reaches "up".
  3. Output per band = tilt + one makeup for all bands, so the output's RMS equals the input's (+ "louder"),
     but never so much that the safety ceiling (the output limiter at -0.1 dBFS) has real work to do on the reference:
     at most LIM_MAX dB on the loudest transient and more than 0.5 dB on at most LIM_FRAC of the samples.
     Fixed presets (the OTT copy) get the same check, as one trim on all band outputs.
Values are rounded the way the knobs show them (0.5 dB), and the result is measured again with the rounded values.
preset_tool is built from presets/preset_tool.cpp with -DMB_STATS (presets/run_tune.sh does both).
"""
import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from spec import PRESETS  # noqa: E402

MATERIAL = {"vocal": "ref_vocal.wav", "drums": "ref_drums.wav", "808": "ref_808.wav", "mix": "ref_mix.wav"}
LIM_MAX = 2.0     # dB: the most the safety ceiling may take off a transient on the reference
LIM_FRAC = 0.002  # share of samples where it may take off more than 0.5 dB
DEFAULT = dict(input=0.0, att=3.0, rel=100.0, knee=0.0, ratio=1.0, uratio=1.0, tilt=0.0)
ARGS = None


def full_band(b):
    d = dict(DEFAULT)
    d.update(b)
    return d


def spec_string(x1, x2, bands):
    parts = ["%g,%g" % (x1, x2)]
    for b in bands:
        parts.append(",".join("%g" % b[k] for k in ("input", "out", "thr", "att", "rel", "knee", "ratio", "uthr", "uratio")))
    return "|".join(parts)


def measure(wav, x1, x2, bands):
    out = subprocess.run([ARGS.tool, wav, "0", spec_string(x1, x2, bands)], capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


def too_hot(m, lim=None):
    lmax, lfrac = lim or (LIM_MAX, LIM_FRAC)
    return m["lim_max"] > lmax or m["lim_frac"] > lfrac


def r05(v):
    return round(v * 2) / 2


def tune(p):
    x1, x2 = p["x1"], p["x2"]
    if p.get("fixed"):
        bands = [dict(b) for b in p["bands"]]
        res = dict(name=p["name"], cat=p["cat"], about=p["about"], x1=x1, x2=x2,
                   bands=[{k: b[k] for k in ("input", "out", "thr", "att", "rel", "knee", "ratio", "uthr", "uratio")} for b in bands])
        if p["name"] != "Init":
            wav = os.path.join(ARGS.audio, MATERIAL["drums"])
            trim = 0.0
            m = measure(wav, x1, x2, bands)
            while too_hot(m, p.get("lim")):   # trim every band output by the same amount
                trim -= 0.5
                for b, o in zip(res["bands"], p["bands"]):
                    b["out"] = o["out"] + trim
                m = measure(wav, x1, x2, res["bands"])
            if trim:
                res["trim"] = trim
            res["measured_on"] = "drums"
            res["measured"] = m
        return res
    wav = os.path.join(ARGS.audio, MATERIAL[p["material"]])
    sb = [full_band(b) for b in p["bands"]]
    bands = [dict(input=b["input"], out=b["tilt"], thr=0.0, att=b["att"], rel=b["rel"], knee=b["knee"], ratio=b["ratio"],
                  uthr=-40.0, uratio=b["uratio"]) for b in sb]
    # 1. Thresh
    lo, hi = [-60.0] * 3, [0.0] * 3
    want = [b.get("dn97", b.get("dn")) for b in sb]
    key = ["down_p97" if "dn97" in b else "down_p90" for b in sb]
    for _ in range(16):
        for i in range(3):
            if want[i] is not None:
                bands[i]["thr"] = (lo[i] + hi[i]) / 2
        m = measure(wav, x1, x2, bands)
        for i in range(3):
            if want[i] is None:
                continue
            if m["bands"][i][key[i]] < want[i]:
                hi[i] = bands[i]["thr"]   # not enough reduction: lower the threshold
            else:
                lo[i] = bands[i]["thr"]
    for i in range(3):
        if want[i] is not None:
            bands[i]["thr"] = r05((lo[i] + hi[i]) / 2)
        if "thr" in sb[i]:   # set by hand in spec.py
            bands[i]["thr"] = sb[i]["thr"]
        bands[i]["uthr"] = min(-40.0, bands[i]["thr"])
    # 2. Upward Thresh
    upw = [b.get("up") for b in sb]
    for i in range(3):
        if "uthr" in sb[i]:   # set by hand in spec.py
            bands[i]["uthr"] = min(sb[i]["uthr"], bands[i]["thr"])
    lo = [-60.0] * 3
    hi = [b["thr"] for b in bands]
    if any(u is not None for u in upw):
        for _ in range(16):
            for i in range(3):
                if upw[i] is not None:
                    bands[i]["uthr"] = (lo[i] + hi[i]) / 2
            m = measure(wav, x1, x2, bands)
            for i in range(3):
                if upw[i] is None:
                    continue
                if m["bands"][i]["up_p90"] < upw[i]:
                    lo[i] = bands[i]["uthr"]   # not enough boost: raise the upward threshold
                else:
                    hi[i] = bands[i]["uthr"]
        for i in range(3):
            if upw[i] is not None:
                bands[i]["uthr"] = r05((lo[i] + hi[i]) / 2)
    # 3. makeup: the same for every band, on top of each band's tilt
    g = 0.0
    for _ in range(4):
        for i in range(3):
            bands[i]["out"] = sb[i]["tilt"] + g
        m = measure(wav, x1, x2, bands)
        g += m["rms_in"] + p.get("louder", 0) - m["rms_out"]
    for i in range(3):
        bands[i]["out"] = sb[i]["tilt"] + g
    g = r05(g)
    while True:
        for i in range(3):
            bands[i]["out"] = max(-24.0, min(24.0, sb[i]["tilt"] + g))
        m = measure(wav, x1, x2, bands)
        if not too_hot(m, p.get("lim")):
            break
        g -= 0.5
    return dict(name=p["name"], cat=p["cat"], about=p["about"], x1=x1, x2=x2, bands=bands, measured_on=p["material"],
                measured=m, targets=[{k: b[k] for k in ("dn", "dn97", "up") if k in b} for b in sb])


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", required=True)
    ap.add_argument("--tool", default="/tmp/preset_tool")
    ap.add_argument("--out", default=os.path.join(HERE, "factory.json"))
    ARGS = ap.parse_args()
    names = [p["name"] for p in PRESETS]
    assert len(set(names)) == len(names), "duplicate preset names"
    for n in names:
        assert len(n) <= 23, "name too long for MPC's PRESET menu: %r" % n
    with ProcessPoolExecutor(initializer=_init, initargs=(ARGS,)) as ex:
        res = list(ex.map(tune, PRESETS))
    for r in res:
        m = r.get("measured")
        if m:
            bd = " ".join("%s %4.1f/+%4.1f" % ("LMH"[i], b["down_p90"], b["up_p90"]) for i, b in enumerate(m["bands"]))
            print("%-22s %-6s rms %+5.1f lim %3.1f/%.2f%%  %s" % (r["name"], r.get("measured_on", ""), m["rms_out"] - m["rms_in"],
                                                                 m["lim_max"], 100 * m["lim_frac"], bd))
    with open(ARGS.out, "w") as f:
        json.dump({"presets": res}, f, indent=1)
    print("wrote", ARGS.out)


def _init(a):
    global ARGS
    ARGS = a


if __name__ == "__main__":
    main()
