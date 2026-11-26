#!/usr/bin/env python3
"""Synthetic voices with a KNOWN pitch and formants (ground truth for voice_eval.py). numpy only.
Source: Liljencrants-Fant-like glottal pulse train (with jitter / shimmer / aspiration), filtered by a cascade of
second-order formant resonators (a source-filter vocal tract). Writes raw float32 mono at 44.1 kHz."""
import numpy as np

FS = 44100.0
VOWELS = {   # male-ish formants (Hz); a woman's are ~ x1.17 (Peterson & Barney averages)
    "a": (730, 1090, 2440), "i": (270, 2290, 3010), "u": (300, 870, 2240), "e": (530, 1840, 2480), "o": (570, 840, 2410),
}


def resonator(x, f, bw, fs=FS):
    r = np.exp(-np.pi * bw / fs)
    th = 2 * np.pi * f / fs
    a1, a2 = -2 * r * np.cos(th), r * r
    g = 1 - r
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    for n in range(len(x)):
        yn = g * x[n] - a1 * y1 - a2 * y2
        y2, y1 = y1, yn
        y[n] = yn
    return y


def glottal_pulse(T, open_frac=0.6):
    """one period (T samples, float) of a derivative-glottal-flow-like pulse: rising flow then sharp closure"""
    n = max(8, int(round(T)))
    t = np.arange(n) / n
    to = open_frac
    flow = np.where(t < to, 0.5 * (1 - np.cos(np.pi * t / to)), 0.0)
    # sharper closing: skew the flow so the closure is fast
    flow = np.where(t < to, np.sin(0.5 * np.pi * (t / to)) ** 2 * (1 - 0.35 * (t / to) ** 3), 0.0)
    d = np.diff(np.concatenate(([0.0], flow, [0.0])))[:n]
    return d


def voice(f0_curve, formants, dur, fs=FS, jitter=0.004, shimmer=0.03, breath=0.015, seed=1, bw=(60, 90, 120), tilt=True, alt=0.0, dropout=0.0):
    """f0_curve: callable t(s)->Hz (or constant); formants: callable t->(f1,f2,f3) or tuple. returns float32 array"""
    rng = np.random.default_rng(seed)
    n = int(dur * fs)
    src = np.zeros(n)
    t = 0.0
    while t < n:
        ti = t / fs
        f0 = f0_curve(ti) if callable(f0_curve) else f0_curve
        T = fs / f0
        T *= 1 + jitter * rng.standard_normal()
        amp = 1 + shimmer * rng.standard_normal()
        k_pulse = k_pulse + 1 if 'k_pulse' in dir() else 0
        if alt:
            amp *= (1.0 - alt) if (k_pulse & 1) else 1.0     # period doubling (vocal fry / diplophonia)
        if dropout and rng.random() < dropout:
            amp *= 0.08                                      # a glottal pulse that did not happen
        g = glottal_pulse(T) * amp
        i0 = int(t)
        frac = t - i0
        L = len(g)
        # fractional placement by linear interpolation
        gg = np.concatenate((g * (1 - frac), [0.0])) + np.concatenate(([0.0], g * frac))
        e = min(n, i0 + len(gg))
        src[i0:e] += gg[:e - i0]
        t += T
    src += breath * rng.standard_normal(n)
    if not callable(formants):
        fm = formants
        y = src
        for f, b in zip(fm, bw):
            y = resonator(y, f, b, fs)
    else:   # time-varying: crossfade blocks (fine for tests)
        y = np.zeros(n)
        blk = 512
        for s in range(0, n, blk):
            fm = formants(s / fs)
            seg = src[max(0, s - 2048):s + blk]
            z = seg
            for f, b in zip(fm, bw):
                z = resonator(z, f, b, fs)
            y[s:s + blk] = z[len(z) - min(blk, n - s):][:min(blk, n - s)]
    y = y / (np.max(np.abs(y)) + 1e-9) * 0.5
    return y.astype(np.float32)


def fricative(dur, fc=6500, fs=FS, seed=3, level=0.2):
    rng = np.random.default_rng(seed)
    n = int(dur * fs)
    x = rng.standard_normal(n)
    # band-limited noise around fc via two resonators
    y = resonator(x, fc, 1200, fs)
    y = resonator(y, fc * 1.1, 1500, fs)
    y /= np.max(np.abs(y)) + 1e-9
    w = np.hanning(n) ** 0.5
    return (y * w * level).astype(np.float32)


def sentence(fs=FS, f0=120.0, scale=1.0, seed=5):
    """~3.5 s: vowel glide, silence, 's' burst, vowel with vibrato, plosive-like click train, vowel"""
    parts = []
    def f0c(base):
        return lambda t: base * (1 + 0.12 * np.sin(2 * np.pi * 0.7 * t) + 0.012 * np.sin(2 * np.pi * 5.2 * t))
    fm = lambda v: tuple(f * scale for f in VOWELS[v])
    parts.append(voice(f0c(f0), fm("a"), 0.7, seed=seed))
    parts.append(np.zeros(int(0.1 * fs), np.float32))
    parts.append(fricative(0.18, 6500 * scale ** 0.5, seed=seed))
    parts.append(voice(f0c(f0 * 1.1), fm("i"), 0.6, seed=seed + 1))
    parts.append(np.zeros(int(0.08 * fs), np.float32))
    # plosive: a short noise burst then a vowel onset
    burst = (np.random.default_rng(seed).standard_normal(int(0.012 * fs)) * np.hanning(int(0.012 * fs)) * 0.35).astype(np.float32)
    parts.append(burst)
    parts.append(voice(f0c(f0 * 0.95), fm("o"), 0.7, seed=seed + 2))
    parts.append(np.zeros(int(0.1 * fs), np.float32))
    parts.append(voice(lambda t: f0 * (1.25 - 0.4 * t), fm("e"), 0.6, seed=seed + 3))
    parts.append(np.zeros(int(0.2 * fs), np.float32))
    return np.concatenate(parts).astype(np.float32)


if __name__ == "__main__":
    import sys
    out = sys.argv[1]
    f0 = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0
    sc = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    sentence(f0=f0, scale=sc).tofile(out)


def raspy_sentence(fs=FS, f0=130.0, seed=11):
    """~4 s of a RASPY voice with a known f0 curve: strong jitter / shimmer, a lot of aspiration noise, period doubling and missing
    pulses in stretches (vocal fry), with a fall in pitch. Returns (audio, f0_of_t(t) or 0 where there is no voice, list of voiced spans)."""
    segs = []
    spans = []
    t0 = 0.0

    def add(x, voiced):
        nonlocal t0
        segs.append(x)
        if voiced:
            spans.append((t0, t0 + len(x) / fs))
        t0 += len(x) / fs
    curves = []
    for k, (vow, dur, base, kw) in enumerate([
            ("a", 0.9, f0, dict(jitter=0.012, shimmer=0.10, breath=0.03)),
            ("e", 0.8, f0 * 1.05, dict(jitter=0.015, shimmer=0.12, breath=0.04, alt=0.3)),
            ("o", 0.9, f0 * 0.9, dict(jitter=0.02, shimmer=0.15, breath=0.04, alt=0.4, dropout=0.06)),
            ("i", 0.8, f0 * 1.1, dict(jitter=0.015, shimmer=0.12, breath=0.07))]):
        fc = (lambda b, d_: (lambda t: b * (1 - 0.18 * t / d_)))(base, dur)
        x = voice(fc, VOWELS[vow], dur, seed=seed + k, **kw)
        curves.append((t0, dur, fc))
        add(x, True)
        add(np.zeros(int(0.06 * fs), np.float32), False)
    def f0_of(t):
        for a, d, fc in curves:
            if a <= t < a + d:
                return fc(t - a)
        return 0.0
    return np.concatenate(segs).astype(np.float32), f0_of, spans


def note_curve(notes, dur_note, glide, f0):
    """f0(t) stepping through `notes` (semitones relative to f0), each dur_note s, with a glide of `glide` s between them (smoothstep)"""
    def f(t):
        k = int(t / dur_note)
        k = min(k, len(notes) - 1)
        tt = t - k * dur_note
        cur = notes[k]
        prev = notes[k - 1] if k > 0 else cur
        if tt < glide:
            u = tt / glide
            u = u * u * (3 - 2 * u)
            cur = prev + (cur - prev) * u
        return f0 * 2 ** (cur / 12)
    return f


def runs_trills(fs=FS, f0=190.0, seed=21, harsh=0.0):
    """a sung phrase with fast notes: a run up and down (10 notes / s, steps of 2 st), a trill (7 Hz, +-1.5 st), a wide leap sequence (5 st).
    returns (audio, f0_of_t, spans) like raspy_sentence; a little jitter / aspiration, not raspy."""
    parts, spans, curves, t0 = [], [], [], 0.0
    plan = [("a", [0, 2, 4, 5, 7, 9, 7, 5, 4, 2, 0, -2], 0.10, 0.012),          # run
            ("e", [0, 1.5] * 8, 0.071, 0.010),                                    # trill
            ("o", [0, 5, -2, 7, 0, 4, -3, 5], 0.16, 0.015),                       # leaps
            ("i", [0, 3, 1, 4, 2, 5, 3, 6, 4, 2, 0], 0.085, 0.010)]               # quick run
    for k, (vow, notes, dn, gl) in enumerate(plan):
        d = dn * len(notes)
        fc = note_curve(notes, dn, gl, f0)
        x = voice(fc, VOWELS[vow], d, seed=seed + k, jitter=0.006 + 0.008 * harsh, shimmer=0.05 + 0.07 * harsh, breath=0.02 + 0.03 * harsh,
                  alt=0.12 * harsh, dropout=0.015 * harsh)
        if harsh:   # articulation: the level dips and the voice roughens at every note change (la-la-la, a trill's glottal flutter)
            n_ = len(x); env = np.ones(n_, np.float32)
            for j in range(1, len(notes)):
                c = int((j * dn + 0.004) * fs)
                w = int(0.016 * fs)
                lo, hi = max(c - w, 0), min(c + w, n_)
                env[lo:hi] *= (1 - (0.6 * harsh) * np.hanning(hi - lo)).astype(np.float32)
            x = x * env
        curves.append((t0, d, fc))
        parts.append(x); spans.append((t0, t0 + d)); t0 += d
        parts.append(np.zeros(int(0.08 * fs), np.float32)); t0 += 0.08

    def f0_of(t):
        for a, d, fc in curves:
            if a <= t < a + d:
                return fc(t - a)
        return 0.0
    return np.concatenate(parts).astype(np.float32), f0_of, spans
