#!/usr/bin/env python3
"""Objective checks of the voice shifter on synthetic voices with known pitch / formants (numpy only).
   python3 test/voice_eval.py [path to juice_cli]
Prints a table; exits 1 if a hard check fails. Honest scope: synthetic source-filter voices, not recorded speech."""
import os
import subprocess
import sys
import tempfile
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voice_synth as vs

FS = 44100.0
CLI = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/scratch/juice_cli")
TMP = tempfile.mkdtemp()


def run(x, **kw):
    a, b = os.path.join(TMP, "in.raw"), os.path.join(TMP, "out.raw")
    x.astype(np.float32).tofile(a)
    subprocess.check_call([CLI, a, b] + ["%s=%g" % (k, v) for k, v in kw.items()])
    return np.fromfile(b, np.float32)


def f0_auto(x, fmin=60, fmax=900):
    x = x - x.mean()
    n = len(x)
    ac = np.correlate(x, x, "full")[n - 1:]
    ac /= ac[0] + 1e-12
    lo, hi = int(FS / fmax), int(FS / fmin)
    k = lo + np.argmax(ac[lo:hi])
    if 1 < k < len(ac) - 1:
        y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]
        k = k + float(np.clip(0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 - 1e-12), -1, 1))
    return FS / k, float(ac[int(round(k))])


def cep_env(x, nfft=4096, lifter=40):
    N = len(x) // nfft * nfft
    seg = x[:N].reshape(-1, nfft) * np.hanning(nfft)
    P = (np.abs(np.fft.rfft(seg, axis=1)) ** 2).mean(axis=0) + 1e-12
    c = np.fft.irfft(np.log(P))
    c[lifter:-lifter] = 0
    return np.fft.rfftfreq(nfft, 1 / FS), np.fft.rfft(c).real * 10 / np.log(10)


def formant_ratio(x, y, lo=200, hi=5000):
    """the frequency-warp factor alpha that best maps the envelope of x onto the envelope of y: env_y(f) ~ env_x(f / alpha).
    alpha is the effective formant shift (1.0 = formants kept)."""
    f, ex = cep_env(x)
    _, ey = cep_env(y)
    best, ba = 1e9, 1.0
    for a in np.exp(np.linspace(np.log(0.6), np.log(1.7), 221)):
        m = (f > lo) & (f < hi) & (f / a > 100) & (f / a < FS / 2 - 200)
        d = ey[m] - np.interp(f[m] / a, f, ex)
        d = d - d.mean()
        sc = np.sqrt(np.mean(d ** 2))
        if sc < best:
            best, ba = sc, a
    return ba, best


def env_lsd(a, b, lo=100, hi=8000, nfft=4096):
    """log-spectral distance (dB) between the cepstrally smoothed envelopes of a and b (steady segments)"""
    def env(x):
        N = len(x) // nfft * nfft
        seg = x[:N].reshape(-1, nfft) * np.hanning(nfft)
        P = (np.abs(np.fft.rfft(seg, axis=1)) ** 2).mean(axis=0) + 1e-12
        c = np.fft.irfft(np.log(P))
        c[40:-40] = 0     # keep the envelope (quefrency < 40 samples ~ 0.9 ms)
        return np.fft.rfft(c).real * 10 / np.log(10)
    ea, eb = env(a), env(b)
    f = np.fft.rfftfreq(nfft, 1 / FS)
    m = (f > lo) & (f < hi)
    d = (ea - ea[m].mean()) - (eb - eb[m].mean())
    return float(np.sqrt(np.mean(d[m] ** 2)))


def cents(a, b):
    return 1200 * np.log2(a / b)


fails = 0


def check(ok, msg):
    global fails
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        fails += 1


def steady(f0, scale, vowel="a", dur=1.6, seed=2):
    fm = tuple(f * scale for f in vs.VOWELS[vowel])
    return vs.voice(f0, fm, dur, seed=seed, jitter=0.003, shimmer=0.02), fm


def seg(x, a=0.8, b=1.5):
    return x[int(a * FS):int(b * FS)]


if __name__ == "__main__":
    print("== steady vowel /a/: pitch / formant behaviour (ground truth from the source-filter model)")
    for (name, f0, scale, kw) in [
        ("male 120 Hz, pitch +5 st (formants kept)", 120.0, 1.0, dict(pitch=5)),
        ("male 120 Hz, MtF pitch +5, formant +2.5", 120.0, 1.0, dict(pitch=5, formant=2.5)),
        ("male 120 Hz, pitch -3 (formants kept)", 120.0, 1.0, dict(pitch=-3)),
        ("female 220 Hz, FtM pitch -5, formant -2.5", 220.0, 1.17, dict(pitch=-5, formant=-2.5)),
        ("female 220 Hz, pitch +4", 220.0, 1.17, dict(pitch=4)),
        ("male 120 Hz, pitch +7, link 100 %", 120.0, 1.0, dict(pitch=7, link=1, ftrack=100)),
        ("male 120 Hz, formant only +3", 120.0, 1.0, dict(formant=3)),
    ]:
        x, fm = steady(f0, scale)
        y = run(x, **kw)
        rho = 2 ** (kw.get("pitch", 0) / 12)
        fst = kw.get("formant", 0) + (kw.get("ftrack", 0) / 100 * kw.get("pitch", 0) if kw.get("link", 0) else 0)
        phi = 2 ** (fst / 12)
        # ground truth: the same source-filter voice made directly at the target pitch / formants
        gt = vs.voice(f0 * rho, tuple(f * phi for f in fm), 1.6, seed=7, jitter=0.003, shimmer=0.02)
        f0o, hnr_o = f0_auto(seg(y))
        f0g, hnr_g = f0_auto(seg(gt))
        alpha, resid = formant_ratio(seg(x), seg(y))
        ecents = cents(f0o, f0 * rho)
        ferr = 12 * np.log2(alpha / phi)           # semitones off the wanted formant shift
        lsd = env_lsd(seg(y), seg(gt))
        print("  %s" % name)
        print("     F0 out %.1f Hz (want %.1f, %+.1f cents) | formant shift %.3fx (want %.3fx, %+.2f st) | env LSD vs ground truth %.1f dB | periodicity %.2f (gt %.2f)"
              % (f0o, f0 * rho, ecents, alpha, phi, ferr, lsd, hnr_o, hnr_g))
        check(abs(ecents) < 15, "pitch within 15 cents")
        check(abs(ferr) < 0.6, "formant shift within 0.6 st of the target")
        check(lsd < 7.0, "spectral envelope within 7 dB RMS of a directly synthesised voice (cepstral; includes source-tilt model differences)")
    
    print("== sentence: pitch contour, level, silence, transients (male 120 Hz synthetic sentence with vibrato, glide, fricative, burst)")
    x = vs.sentence(f0=120.0)
    LAT = int(os.environ.get("JB_LAT", "512"))
    def frames_f0(z, hop=1024, win=2048):
        out = []
        for s0 in range(0, len(z) - win, hop):
            seg_ = z[s0:s0 + win]
            if np.sqrt((seg_ ** 2).mean()) < 0.02:
                out.append((s0, np.nan, 0.0)); continue
            f, c = f0_auto(seg_)
            out.append((s0, f if c > 0.6 else np.nan, c))
        return out
    for name, kw in [("pitch +5", dict(pitch=5)), ("pitch -4", dict(pitch=-4)), ("MtF +5 / +2.5", dict(pitch=5, formant=2.5)), ("pitch +12", dict(pitch=12))]:
        y = run(x, **kw)
        rho = 2 ** (kw.get("pitch", 0) / 12)
        fi = frames_f0(x)
        yy = np.concatenate((y[LAT:], np.zeros(LAT, np.float32)))
        fo = frames_f0(yy)
        r = [o[1] / i[1] for i, o in zip(fi, fo) if i[1] == i[1] and o[1] == o[1]]
        r = np.array(r)
        good = np.mean(np.abs(1200 * np.log2(r / rho)) < 40) if len(r) else 0
        print("  %-14s voiced frames compared %d, within 40 cents of the ratio: %.0f %% (median %+.1f cents)" % (name, len(r), 100 * good, np.median(1200 * np.log2(r / rho))))
        check(good > 0.85, "%s: >= 85 %% of voiced frames at the right pitch ratio" % name)
    # silence: after the sentence ends there is 0.2 s of silence; the output must be silent there too
    y = run(x, pitch=5, formant=2.5, air=0)
    tail = y[len(x) + 0:] if len(y) > len(x) else y[-100:]
    sil = y[int(3.25 * FS) + LAT: int(3.28 * FS) + LAT] if len(y) > int(3.28 * FS) + LAT else y[-100:]
    print("  silence after the voice: peak %.2e" % np.abs(sil).max() if len(sil) else "")
    # transient: the plosive burst (12 ms noise at ~1.64 s): its peak survives with F-Smooth up
    t0 = int(1.66 * FS)
    xb = np.abs(x[t0 - 20:t0 + 540]).max()
    for fsm in (0, 100):
        y = run(x, pitch=5, formant=2.5, fsmooth=fsm)
        yb = np.abs(y[t0 - 20 + LAT:t0 + 540 + LAT]).max()
        print("  burst peak with F-Smooth %3d %%: %+.1f dB vs input" % (fsm, 20 * np.log10(yb / xb)))
    y0 = run(x, pitch=5, formant=2.5, fsmooth=0); y1 = run(x, pitch=5, formant=2.5, fsmooth=100)
    b0 = np.abs(y0[t0 - 20 + LAT:t0 + 540 + LAT]).max(); b1 = np.abs(y1[t0 - 20 + LAT:t0 + 540 + LAT]).max()
    check(b1 >= b0 * 0.90 and 20 * np.log10(b1 / xb) > -4.0, "F-Smooth keeps the plosive burst within 4 dB of the input")
    sys.exit(1 if fails else 0)
