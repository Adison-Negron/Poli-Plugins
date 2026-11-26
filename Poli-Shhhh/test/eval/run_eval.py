#!/usr/bin/env python3
"""Objective comparison of Shhhhh gain-stage variants (results: src/ShhDSP.h, above struct Tuning) on real speech and music with added noise.

    python3 test/eval/run_eval.py <corpus dir> <eval binary> [config-name ...]

Corpus: three LibriSpeech readings and a song with vocals (CC-BY, github.com/librosa/data), plus five clean
readings and five real noise recordings from Microsoft's MS-SNSD (github.com/microsoft/MS-SNSD; $SNSD points at it).
Each condition = 3.2 s of noise alone (Learn runs on it, like RX), then the clip with the same noise, at 5 and 15 dB
SNR. Noises: white, pink, mains hum (50 Hz + harmonics), fan (rumble + 120 Hz + 2.4 kHz whine), tape hiss, and the
recorded air conditioner, car, vacuum cleaner, office and cafe. Run with PYTHONHASHSEED=0 for repeatable noise.
Metrics: PESQ-WB and STOI against the clean clip (speech only), NR = noise removed (dB), SDR = speech kept /
speech distortion (dB) and LKR = log kurtosis ratio of the residual noise (musical noise: 0 = none added, Uemura et
al. 2009); the last three use shadow filtering (the plugin's gains applied to clean and noise separately)."""
import itertools, json, os, subprocess, sys, tempfile
from multiprocessing import Pool
import numpy as np, soundfile as sf
from scipy.signal import resample_poly, stft
from pesq import pesq
from pystoi import stoi

SR, LEAD, LAT = 44100, 3.2, 128
CORPUS, EVAL = sys.argv[1], sys.argv[2]

OLD = ["alpha=0.98", "rule=0", "tsnr=0", "wideHf=0", "mask=0", "redScale=1"]   # 1.0.1's gain stage
CONFIGS = {   # each is applied on top of the shipped defaults (src/ShhDSP.h Tuning)
    "1.0.2":       [],
    "v1.0.1":      OLD,
    "lsa":         OLD + ["rule=1"],
    "tsnr":        OLD + ["tsnr=1"],
    "spp":         OLD + ["presence=1"],
    "spp+omlsa":   OLD + ["presence=1", "omlsa=1", "rule=1"],
    "no mask":     ["mask=0"],
    "no wideHf":   ["wideHf=0"],
    "mask12":      ["maskOffDb=12"],
    "track":       ["track=1"],
    "free":        ["track=2"],
}


def clips():
    out = []
    for f in ["198-209-0000.hq.ogg", "3436-172162-0000.hq.ogg", "5703-47212-0000.hq.ogg"]:
        x, sr = sf.read(os.path.join(CORPUS, f))
        x = resample_poly(x, 441, 160)[: int(12 * SR)]
        out.append(("speech:" + f[:4], x / np.max(np.abs(x)) * 0.5, True))
    import glob
    for f in sorted(glob.glob(os.environ.get("SNSD", "/home/claude/eval/data/MS-SNSD") + "/clean_test/*.wav"))[:5]:
        x, sr = sf.read(f)
        x = resample_poly(x, 441, 160)[: int(9 * SR)]
        out.append(("snsd:" + os.path.basename(f)[5:9], x / np.max(np.abs(x)) * 0.5, True))
    x, sr = sf.read(os.path.join(CORPUS, "Karissa_Hobbs_-_Let's_Go_Fishin'.hq.ogg"))
    x = x.mean(1)[int(28 * SR): int(43 * SR)]
    out.append(("song", x / np.max(np.abs(x)) * 0.5, False))
    return out


def pinkish(n, rng, slope):
    X = np.fft.rfft(rng.standard_normal(n))
    f = np.maximum(np.fft.rfftfreq(n, 1 / SR), 20)
    y = np.fft.irfft(X / f ** (slope / 2), n)
    return y / np.std(y)


def noise(kind, n, seed):
    rng = np.random.default_rng(seed)
    t = np.arange(n) / SR
    if kind == "white": y = rng.standard_normal(n)
    elif kind == "pink": y = pinkish(n, rng, 1.0)
    elif kind == "hum":
        y = sum((0.6 ** h) * np.sin(2 * np.pi * 50 * (h + 1) * t + h) for h in range(12)) + 0.05 * pinkish(n, rng, 1.0)
    elif kind == "fan":
        y = pinkish(n, rng, 1.6) + 0.4 * np.sin(2 * np.pi * 120 * t) + 0.15 * np.sin(2 * np.pi * 2400 * t)
    elif kind in REAL:
        import glob
        f = glob.glob(os.environ.get("SNSD", "/home/claude/eval/data/MS-SNSD") + "/*/%s.wav" % REAL[kind])[0]
        v, _ = sf.read(f)
        v = resample_poly(v, 441, 160)
        st = rng.integers(0, max(1, len(v) - n))
        y = np.resize(v[st:], n)
    elif kind == "hiss":
        X = np.fft.rfft(rng.standard_normal(n)); f = np.fft.rfftfreq(n, 1 / SR)
        y = np.fft.irfft(X * np.clip(f / 3000, 0.05, 1.0), n)
    return y / np.std(y)


REAL = {"aircon": "AirConditioner_1", "car": "Car_1", "vacuum": "VacuumCleaner_1", "office": "Office_1", "cafe": "Cafe_1"}
NOISES = tuple(os.environ.get("NOISES", "white,pink,hum,fan,hiss,aircon,car,vacuum,office,cafe").split(","))


def active_rms(x):
    fr = x[: len(x) // 1024 * 1024].reshape(-1, 1024)
    e = (fr ** 2).mean(1)
    return np.sqrt(e[e > 0.05 * e.max()].mean())


def kurt(p):
    return np.mean(p ** 2) / np.mean(p) ** 2


def spec_pow(x):
    _, _, Z = stft(x, SR, nperseg=512, noverlap=384)
    f = np.fft.rfftfreq(512, 1 / SR)
    return np.abs(Z[(f > 100) & (f < 16000)]) ** 2


def one(job):
    cname, args, clip_name, snr, nkind, red, idx = job
    clip = CLIPS[clip_name]
    x, is_speech = clip
    lead = int(LEAD * SR)
    clean = np.concatenate([np.zeros(lead), x])
    nz = noise(nkind, len(clean) + LAT * 4, idx)[: len(clean)]
    nz = nz * active_rms(x) / 10 ** (snr / 20)
    noisy = clean + nz
    with tempfile.TemporaryDirectory() as td:
        for nm, v in (("noisy", noisy), ("clean", clean), ("noise", nz)):
            v.astype(np.float32).tofile(os.path.join(td, nm + ".f32"))
        subprocess.run([EVAL, td + "/noisy.f32", td + "/clean.f32", td + "/noise.f32", td, str(LEAD if os.environ.get("LEARN", "1") == "1" else 0), str(red), "0"] + args, check=True)
        out = np.fromfile(td + "/out.f32", np.float32).astype(float)
        sc = np.fromfile(td + "/sclean.f32", np.float32).astype(float)
        sn = np.fromfile(td + "/snoise.f32", np.float32).astype(float)
    L = len(out) - LAT
    out, sc, sn = out[LAT:], sc[LAT:], sn[LAT:]
    c, n, ny = clean[:L], nz[:L], noisy[:L]
    a = lead + int(0.2 * SR)     # after the lead (and the first ramp)
    r = dict(nr=10 * np.log10(np.sum(n[a:] ** 2) / np.sum(sn[a:] ** 2)),
             sdr=10 * np.log10(np.sum(c[a:] ** 2) / np.sum((sc[a:] - c[a:]) ** 2)),
             lkr=float(np.log(kurt(spec_pow(sn[a:])) / kurt(spec_pow(n[a:])))))
    if is_speech:
        c16, o16, y16 = (resample_poly(v[a:], 160, 441) for v in (c, out, ny))
        r["pesq"] = pesq(16000, c16, o16, "wb"); r["pesq_in"] = pesq(16000, c16, y16, "wb")
        r["stoi"] = stoi(c16, o16, 16000); r["stoi_in"] = stoi(c16, y16, 16000)
    return cname, red, r


CLIPS = {}
REDS = tuple(int(v) for v in os.environ.get("REDS", "12,30").split(","))
if __name__ == "__main__":
    for nm, x, sp in clips(): CLIPS[nm] = (x, sp)
    names = sys.argv[3:] or ["1.0.2", "v1.0.1"]
    jobs = []
    for i, (cn, cl, snr, nk, red) in enumerate(itertools.product(names, CLIPS, (5, 15), NOISES, REDS)):
        jobs.append((cn, CONFIGS[cn], cl, snr, nk, red, hash((cl, snr, nk)) & 0xffff))
    with Pool(os.cpu_count()) as p:
        res = p.map(one, jobs)
    agg = {}
    for cn, red, r in res:
        for k, v in r.items(): agg.setdefault((cn, red), {}).setdefault(k, []).append(v)
    print("%-12s %3s  %6s %6s %6s %6s  %6s %6s %6s" % ("config", "red", "PESQ", "(in)", "STOI", "(in)", "NR dB", "SDR dB", "LKR"))
    table = {}
    for (cn, red), d in sorted(agg.items(), key=lambda kv: (kv[0][1], names.index(kv[0][0]))):
        m = {k: float(np.mean(v)) for k, v in d.items()}
        table["%s@%d" % (cn, red)] = m
        print("%-12s %3d  %6.3f %6.3f %6.3f %6.3f  %6.2f %6.2f %6.3f" % (cn, red, m["pesq"], m["pesq_in"], m["stoi"], m["stoi_in"], m["nr"], m["sdr"], m["lkr"]))
    json.dump(res, open(os.environ.get("OUT", "eval_results.json"), "w"))
