#!/usr/bin/env python3
"""Raspy-voice stability check: a synthetic voice with strong jitter / shimmer / aspiration, period doubling and missing pulses (vocal fry)
and a KNOWN f0 curve goes through a shift; the output pitch is tracked and compared with (input f0 x ratio) frame by frame.
   python3 test/rasp_eval.py [juice_cli] [key=value ...]
Prints: frames within 40 cents of the target, octave-type errors (> 500 cents off), sudden jumps of the contour between neighbouring
frames (> 150 cents in 10 ms) and the 'level wobble' (largest 10 ms RMS step in dB inside voiced stretches)."""
import os
import subprocess
import sys
import tempfile
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voice_synth as vs
from voice_eval import f0_auto

FS = 44100.0
CLI = os.path.expanduser("~/scratch/juice_cli")
args = []
for a in sys.argv[1:]:
    if "=" in a:
        args.append(a)
    else:
        CLI = a
TMP = tempfile.mkdtemp()


def run(x, kv):
    i, o = os.path.join(TMP, "i.raw"), os.path.join(TMP, "o.raw")
    x.astype(np.float32).tofile(i)
    subprocess.check_call([CLI, i, o] + kv)
    return np.fromfile(o, np.float32)


def evaluate(name, kv, pitch_st, f0=130.0, seed=11, verbose=True):
    """runs the shifter with a trace (per 128-sample block: voiced flag, tracked f0, the pitch ratio actually applied)"""
    x, f0t, spans = vs.raspy_sentence(f0=f0, seed=seed)
    i, o, tr = os.path.join(TMP, "i.raw"), os.path.join(TMP, "o.raw"), os.path.join(TMP, "tr.txt")
    x.astype(np.float32).tofile(i)
    subprocess.check_call([CLI, i, o, "trace=" + tr] + kv)
    T = np.loadtxt(tr)
    t, v, f, rho = T[:, 0] - 128 / FS, T[:, 1], T[:, 2], T[:, 3]   # the trace time is the input time at the end of the block
    voiced = np.zeros(len(t), bool)
    for (a, b) in spans:
        voiced |= (t > a + 0.08) & (t < b - 0.04)
    tv = v[voiced]
    on = np.mean(tv) * 100
    # a flip = the voiced flag dropping out in the middle of a voiced stretch
    flips = 0
    for (a, b) in spans:
        m = (t > a + 0.08) & (t < b - 0.04)
        flips += int(np.sum(np.diff(v[m]) < 0))
    ff = f[voiced & (v > 0)]
    tt = t[voiced & (v > 0)]
    truth = np.array([f0t(x_) for x_ in tt])
    err = 1200 * np.log2(np.maximum(ff, 1) / truth)
    octave = np.mean(np.abs(err) > 500) * 100 if len(err) else 100
    good = np.mean(np.abs(err) < 100) * 100 if len(err) else 0
    lr = 12 * np.log2(np.maximum(rho, 1e-3))
    steps = np.abs(np.diff(lr))
    stm = (voiced[1:] & voiced[:-1])
    big = int(np.sum(steps[stm] > 1.0))                       # the pitch ratio applied jumps by more than a semitone between 3 ms blocks
    # harmonic lock: how much of the 0-1.6 kHz energy sits on the harmonics of the TARGET pitch (input f0 x ratio), relative to the input's own
    y = np.fromfile(o, np.float32)
    rho_t = 2 ** (pitch_st / 12)
    def lock(sig, delay, ratio):
        sc = []
        for (a, b) in spans:
            for t0 in np.arange(a + 0.10, b - 0.06, 0.02):
                n0 = int(t0 * FS) + delay
                if n0 < 0 or n0 + 2048 > len(sig):
                    continue
                X = np.abs(np.fft.rfft(sig[n0:n0 + 2048] * np.hanning(2048))) ** 2
                fr = np.fft.rfftfreq(2048, 1 / FS)
                f = f0t(t0 + 0.0) * ratio
                m = np.zeros(len(fr), bool)
                for h in range(1, 13):
                    if h * f < 1600:
                        m |= np.abs(fr - h * f) < 0.04 * h * f * 0.5 + 8
                tot = X[(fr > 60) & (fr < 1600)].sum() + 1e-12
                sc.append(X[m].sum() / tot)
        return float(np.mean(sc))
    lk_out = lock(y, int(os.environ.get("JB_LAT", "512")), rho_t)
    lk_in = lock(x, 0, 1.0)
    print("  %-24s voiced %5.1f %% | drop-outs %3d | f0 within 100c %5.1f %% | octave errors %5.1f %% | ratio jumps > 1 st: %3d | harmonic lock %.2f of input's" % (name, on, flips, good, octave, big, lk_out / lk_in))
    return dict(on=on, flips=flips, good=good, octave=octave, big=big, lock=lk_out / lk_in)


if __name__ == "__main__":
    print("== raspy synthetic voice (jitter 1-2 %, shimmer 10-15 %, aspiration, period doubling in stretches, dropped pulses), known f0")
    OLD = ["hold=0", "rasp=0", "guard=0", "morph=5", "glide=20"]   # the tracker without the tolerance (old behaviour, as near as the controls go)
    cases = [("pitch +5", ["pitch=5"], 5), ("MtF +6 / formant +3", ["pitch=6", "formant=3"], 6), ("FtM -5 / formant -2.5", ["pitch=-5", "formant=-2.5"], -5)]
    res = {}
    for label, extra in (("tolerance off:", OLD), ("new defaults:", [])):
        print(label)
        for name, kv, st in cases:
            res[(label, name)] = evaluate(name, kv + extra + args, st)
    bad = 0
    for name, kv, st in cases:
        o, n = res[("tolerance off:", name)], res[("new defaults:", name)]
        ok = n["on"] >= 90 and n["flips"] <= 5 and n["big"] <= max(3, o["big"] // 4) and n["flips"] < o["flips"] / 3
        print("%s %s: voicing drop-outs %d -> %d, pitch-ratio jumps %d -> %d, voiced %.0f %% -> %.0f %%" % ("ok  " if ok else "FAIL", name, o["flips"], n["flips"], o["big"], n["big"], o["on"], n["on"]))
        bad += not ok
    sys.exit(1 if bad else 0)
