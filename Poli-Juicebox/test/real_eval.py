#!/usr/bin/env python3
"""Real-voice check: raw float32 mono 44.1 kHz files in test/real/ go through the shifter; a reference f0 contour (offline YIN + Viterbi, 2048 window,
5 ms hop, strongly continuous) is compared with the plugin's tracker trace, and the output's harmonic lock at the target pitch is measured.
   python3 test/real_eval.py [juice_cli] [key=value ...]            (the key=values go to juice_cli, e.g. pitch=5 formant=2)"""
import os, subprocess, sys, tempfile, glob
import numpy as np

FS = 44100.0
HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.expanduser("~/scratch/juice_cli")
args = []
for a in sys.argv[1:]:
    (args.append(a) if "=" in a else None)
    if "=" not in a:
        CLI = a
LAT = int(os.environ.get("JB_LAT", "512"))
TMP = tempfile.mkdtemp()


def ref_f0(x, hop=220, win=2048, fmin=70, fmax=800):
    """offline YIN candidates per frame + Viterbi over the lag (log) with a jump cost -> (times, f0 or 0, confidence)"""
    n = (len(x) - win - 2 * int(FS / fmin)) // hop
    lmin, lmax = int(FS / fmax), int(FS / fmin)
    lags = np.arange(lmin, lmax)
    cost = np.zeros((n, len(lags)), np.float32)
    rms = np.zeros(n)
    X = np.fft.rfft  # FFT based difference function
    for i in range(n):
        seg = x[i * hop: i * hop + win + lmax]
        a = seg[:win]
        rms[i] = np.sqrt(np.mean(a * a))
        nf = 1 << int(np.ceil(np.log2(win + lmax)))
        fa = np.fft.rfft(seg, nf)
        fb = np.fft.rfft(a, nf)
        r = np.fft.irfft(fa * np.conj(fb), nf)[:lmax + 1]          # cross-correlation of seg with a
        e = np.cumsum(seg * seg)
        ew = e[win - 1]
        el = e[win - 1 + np.arange(lmax + 1)] - np.concatenate(([0], e[np.arange(lmax)]))
        d = ew + el - 2 * r
        d[0] = 0
        cm = np.ones(lmax + 1)
        run = np.cumsum(d[1:])
        cm[1:] = d[1:] * np.arange(1, lmax + 1) / np.maximum(run, 1e-12)
        cost[i] = cm[lags]
    # Viterbi
    ll = np.log(lags.astype(np.float64))
    jump = 0.5 * np.abs(ll[:, None] - ll[None, :]) / np.log(2) * 4.0      # cost per octave of lag change
    D = cost[0].astype(np.float64)
    bp = np.zeros((n, len(lags)), np.int32)
    for i in range(1, n):
        M = D[None, :] + jump
        bp[i] = np.argmin(M, axis=1)
        D = cost[i] + M[np.arange(len(lags)), bp[i]]
    path = np.zeros(n, int)
    path[-1] = np.argmin(D)
    for i in range(n - 1, 0, -1):
        path[i - 1] = bp[i, path[i]]
    conf = 1 - cost[np.arange(n), path]
    f0 = FS / lags[path]
    t = (np.arange(n) * hop + win / 2) / FS
    f0[(conf < 0.55) | (rms < 0.01)] = 0
    return t, f0, conf


def lock(sig, t, f0, ratio, delay, win=1024):
    fr = np.fft.rfftfreq(win, 1 / FS)
    sc = []
    for tt, f in zip(t, f0):
        if f <= 0:
            continue
        n0 = int(tt * FS) - win // 2 + delay
        if n0 < 0 or n0 + win > len(sig):
            continue
        X = np.abs(np.fft.rfft(sig[n0:n0 + win] * np.hanning(win))) ** 2
        m = np.zeros(len(fr), bool)
        for h in range(1, 14):
            if h * f * ratio < 2000:
                m |= np.abs(fr - h * f * ratio) < 0.05 * f * ratio + 6
        sc.append(X[m].sum() / (X[(fr > 60) & (fr < 2000)].sum() + 1e-12))
    return float(np.mean(sc)) if sc else 0.0


def evaluate(path, kv, pitch_st, verbose=True):
    x = np.fromfile(path, np.float32)
    i, o, tr = os.path.join(TMP, "i.raw"), os.path.join(TMP, "o.raw"), os.path.join(TMP, "tr.txt")
    x.tofile(i)
    subprocess.check_call([CLI, i, o, "trace=" + tr] + kv + args)
    y = np.fromfile(o, np.float32)
    T = np.loadtxt(tr)
    tt, v, f, rho = T[:, 0] - 128 / FS, T[:, 1], T[:, 2], T[:, 3]
    rt, rf, rc = ref_f0(x)
    # the tracker's value at the reference frame time (the tracker looks ~9 ms back)
    ok = oct_ = tot = flips = 0
    j = np.searchsorted(tt - 0.0087, rt)
    j = np.clip(j, 0, len(tt) - 1)
    for k in range(len(rt)):
        if rf[k] <= 0:
            continue
        tot += 1
        fk = f[j[k]]
        if fk <= 0:
            oct_ += 1; continue
        e = abs(1200 * np.log2(fk / rf[k]))
        ok += e < 60
        oct_ += e > 500
    ref_voiced = np.interp(tt, rt, (rf > 0).astype(float)) > 0.5
    drops = int(np.sum((np.diff(v) < 0) & ref_voiced[1:]))
    lr = 12 * np.log2(np.maximum(rho, 1e-3))
    steps = np.abs(np.diff(lr))
    big = int(np.sum(steps[(ref_voiced[1:] & ref_voiced[:-1])] > 1.0))
    lk_o = lock(y, rt, rf, 2 ** (pitch_st / 12), LAT)
    lk_i = lock(x, rt, rf, 1.0, 0)
    print("  %-14s ref voiced %4.1f %% | tracker within 60c %5.1f %% | octave-type errors %5.1f %% | voicing drop-outs %3d | ratio jumps>1st %3d | harmonic lock %.2f" %
          (os.path.basename(path)[:-4], 100.0 * np.mean(rf > 0), 100.0 * ok / max(tot, 1), 100.0 * oct_ / max(tot, 1), drops, big, lk_o / max(lk_i, 1e-9)))
    return dict(oct=100.0 * oct_ / max(tot, 1), lock=lk_o / max(lk_i, 1e-9), ok=100.0 * ok / max(tot, 1))


if __name__ == "__main__":
    files = sorted(glob.glob(os.path.join(HERE, "real", "*.raw")))
    for name, kv, st in [("pitch 0 / formant +3", ["formant=3"], 0), ("pitch +5 / formant +2", ["pitch=5", "formant=2"], 5), ("pitch -5 / formant -2", ["pitch=-5", "formant=-2"], -5),
                         ("octave +12", ["pitch=12"], 12), ("octave -12", ["pitch=-12"], -12)]:
        print(name)
        for f in files:
            evaluate(f, kv, st)
