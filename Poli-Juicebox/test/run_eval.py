#!/usr/bin/env python3
"""Fast-run / trill check: a sung phrase with quick note changes (known f0) goes through a pitch shift; the output pitch is tracked and
compared with (input f0 x ratio). Reports, over the voiced spans:
  voiced %        the tracker's voiced flag (a drop = the voice falls back to the unshifted 'robotic' path)
  drop-outs       voiced flag falling inside a note stretch
  within 50c      frames of the OUTPUT whose pitch is within 50 cents of the target (after settling 25 ms from a note change)
  ratio dips      the applied shift ratio sagging by > 1 st below the requested one (a fade towards the dry voice)
  track lag ms    how late the tracked f0 follows a note change
   python3 test/run_eval.py [juice_cli] [key=value ...]"""
import os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voice_synth as vs
from voice_eval import f0_auto

FS = 44100.0
CLI = os.path.expanduser("~/scratch/juice_cli")
args = []
for a in sys.argv[1:]:
    (args.append(a) if "=" in a else None)
    if "=" not in a:
        CLI = a
TMP = tempfile.mkdtemp()


def lock(sig, delay, ratio, f0t, spans, win=1024):
    """share of the 0-2 kHz energy sitting on the harmonics of the target pitch (f0 x ratio), windows that straddle a note change skipped"""
    sc = []
    fr = np.fft.rfftfreq(win, 1 / FS)
    for (a, b) in spans:
        for t0 in np.arange(a + 0.06, b - 0.05, 0.01):
            tc = t0 + win / 2 / FS
            if max(abs(12 * np.log2(max(f0t(tc + d), 1) / max(f0t(tc), 1))) for d in (-win / 2 / FS, win / 2 / FS)) > 0.3:
                continue
            n0 = int(t0 * FS) + delay
            if n0 < 0 or n0 + win > len(sig):
                continue
            X = np.abs(np.fft.rfft(sig[n0:n0 + win] * np.hanning(win))) ** 2
            f = f0t(tc) * ratio
            m = np.zeros(len(fr), bool)
            for h in range(1, 14):
                if h * f < 2000:
                    m |= np.abs(fr - h * f) < 0.05 * f + 6
            sc.append(X[m].sum() / (X[(fr > 60) & (fr < 2000)].sum() + 1e-12))
    return float(np.mean(sc))


def evaluate(name, kv, pitch_st, f0=190.0, delay=int(os.environ.get("JB_LAT", "512")), harsh=0.0):
    x, f0t, spans = vs.runs_trills(f0=f0, harsh=harsh)
    i, o, tr = os.path.join(TMP, "i.raw"), os.path.join(TMP, "o.raw"), os.path.join(TMP, "tr.txt")
    x.astype(np.float32).tofile(i)
    subprocess.check_call([CLI, i, o, "trace=" + tr] + kv + args)
    T = np.loadtxt(tr)
    t, v, f, rho = T[:, 0] - 128 / FS, T[:, 1], T[:, 2], T[:, 3]
    inspan = np.zeros(len(t), bool)
    for (a, b) in spans:
        inspan |= (t > a + 0.05) & (t < b - 0.03)
    drops = 0
    for (a, b) in spans:
        m = (t > a + 0.05) & (t < b - 0.03)
        drops += int(np.sum(np.diff(v[m]) < 0))
    on = np.mean(v[inspan]) * 100
    want = 12 * np.log2(2 ** (pitch_st / 12))
    lr = 12 * np.log2(np.maximum(rho, 1e-3))
    dips = int(np.sum((lr[inspan] < pitch_st - 1.0) if pitch_st > 0 else (lr[inspan] > pitch_st + 1.0))) if abs(pitch_st) > 1.5 else 0
    # output pitch vs target
    y = np.fromfile(o, np.float32)
    y = y[delay:]
    hop = int(0.01 * FS)
    good = tot = 0
    for (a, b) in spans:
        for t0 in np.arange(a + 0.06, b - 0.05, 0.01):
            tt = t0
            tc = tt + 0.0145                      # the analysis window's centre
            # skip windows that straddle a note change
            if max(abs(12 * np.log2(max(f0t(tc + dd), 1) / max(f0t(tc), 1))) for dd in (-0.0145, 0.0145)) > 0.25:
                continue
            n0 = int(tt * FS)
            seg = y[n0:n0 + 1280]
            if len(seg) < 1280:
                continue
            fo = f0_auto(seg)[0]
            tot += 1
            tgt = f0t(tc) * 2 ** (pitch_st / 12)
            if fo and abs(1200 * np.log2(fo / tgt)) < 50:
                good += 1
    # tracker accuracy against the truth at the tracker window's centre (8.7 ms before the block end): within 50 cents, and octave-type errors (> 500 cents)
    ok = oct_ = tot2 = 0
    for (a, b) in spans:
        for j in np.where((t > a + 0.06) & (t < b - 0.05))[0]:
            tc = t[j] - 0.0087
            if max(abs(12 * np.log2(max(f0t(tc + d), 1) / max(f0t(tc), 1))) for d in (-0.0145, 0.0145)) > 0.25:
                continue
            tot2 += 1
            if f[j] > 0:
                e = abs(1200 * np.log2(f[j] / f0t(tc)))
                ok += e < 50
                oct_ += e > 500
            else:
                oct_ += 1
    lag = 100.0 * oct_ / max(tot2, 1)
    lk = lock(np.fromfile(o, np.float32), delay, 2 ** (pitch_st / 12), f0t, spans) / max(lock(x, 0, 1.0, f0t, spans), 1e-9)
    trk = 100.0 * ok / max(tot2, 1)
    print("  %-26s voiced %5.1f %% | drop-outs %3d | out within 50c %5.1f %% | ratio dips %3d | tracker within 50c %5.1f %% | octave errors %5.1f %% | harmonic lock %.2f" % (name, on, drops, 100.0 * good / max(tot, 1), dips, trk, lag, lk))
    return dict(on=on, drops=drops, good=100.0 * good / max(tot, 1), dips=dips, trk=trk, octave=lag, lock=lk)


if __name__ == "__main__":
    print("== fast runs / trills / leaps (known f0)")
    r = {}
    for hs, tag in ((0.0, "clean"), (0.5, "rough")):
        print(" " + tag + " phrase")
        for name, kv, st in [("pitch +5", ["pitch=5"], 5), ("MtF +6 / formant +3", ["pitch=6", "formant=3"], 6), ("FtM -5 / formant -2.5", ["pitch=-5", "formant=-2.5"], -5),
                             ("octave +12", ["pitch=12"], 12), ("formant only +3", ["formant=3"], 0)]:
            r[tag + " " + name] = evaluate(name, kv, st, harsh=hs)
    # clean phrase: the tracker must hold the pitch through quick notes (before the fix: 41 % octave-type errors, lock 0.66-0.75);
    # rough phrase (jitter, shimmer, level dips, missing pulses at every note change; even a full-rate YIN scores < 50 % on it): only a floor
    bad = [k for k, v in r.items() if (k.startswith("clean") and (v["octave"] > 20 or v["trk"] < 70 or v["lock"] < 0.7 or v["dips"] > 4 or v["drops"] > 2))
           or (k.startswith("rough") and (v["octave"] > 55 or v["lock"] < 0.55 or v["drops"] > 6))]
    for k in bad:
        print("FAIL", k)
    sys.exit(1 if bad else 0)
