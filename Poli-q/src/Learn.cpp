// Poli-q Learn (see Learn.h).
#include "Learn.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pq {

static const float kPiL = 3.14159265358979f;
static float clampL(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

float Learner::binHz(int i) { return 40.0f * powf(2.0f, (i + 0.5f) / 6.0f); }
static int binOf(float hz) { return (int)floorf(6.0f * log2f(hz / 40.0f)); }
static const float kLoHz = 40.0f, kHiHz = 40.0f * 256.0f * 1.7817974f;   // the bins' outer edges (2^(52/6) x 40)

void Learner::start() {
    if (!tables_) {
        double s = 0;
        for (int i = 0; i < kN; ++i) {
            win_[i] = 0.5f - 0.5f * cosf(2 * kPiL * i / kN);
            s += (double)win_[i] * win_[i];
        }
        winPow_ = (float)s;
        for (int i = 0; i < kN / 2; ++i) { cosT_[i] = cosf(2 * kPiL * i / kN); sinT_[i] = -sinf(2 * kPiL * i / kN); }
        int bits = 0;
        while ((1 << bits) < kN) ++bits;
        for (int i = 0; i < kN; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            rev_[i] = r;
        }
        tables_ = true;
    }
    memset(ring_, 0, sizeof ring_);
    wpos_ = sinceHop_ = hops_ = nStored_ = 0;
    hopsSig_ = onsets_ = 0;
    prevLev_ = -120.0f;
    active_ = true;
}

void Learner::push(const float *l, const float *r, int n) {
    if (!active_) return;
    for (int i = 0; i < n; ++i) {
        ring_[wpos_] = 0.5f * (l[i] + r[i]);
        wpos_ = (wpos_ + 1) & (kN - 1);
        if (++sinceHop_ >= kHop) {
            sinceHop_ = 0;
            frame();
        }
    }
}

// YIN (de Cheveigne & Kawahara 2002) on the frame decimated by 4 (11025 Hz): 61 Hz .. 1 kHz
float Learner::yin(const float *x) const {
    const int W = 320, tMin = 11, tMax = 180;
    float d[tMax + 2];
    d[0] = 1;
    float run = 0;
    int best = -1;
    for (int t = 1; t <= tMax + 1; ++t) {
        float s = 0;
        for (int j = 0; j < W; ++j) { const float e = x[j] - x[j + t]; s += e * e; }
        run += s;
        d[t] = run > 0 ? s * t / run : 1;   // cumulative mean normalised difference
    }
    for (int t = tMin; t <= tMax; ++t)
        if (d[t] < 0.15f) {
            while (t + 1 <= tMax && d[t + 1] < d[t]) ++t;
            best = t;
            break;
        }
    if (best < 0) return 0;
    float tt = (float)best;
    const float a = d[best - 1], b = d[best], c = d[best + 1], den = a - 2 * b + c;
    if (den > 1e-9f) tt += 0.5f * (a - c) / den;   // parabolic interpolation
    return 11025.0f / tt;
}

void Learner::frame() {
    ++hops_;
    float *x = frame_;
    for (int i = 0; i < kN; ++i) x[i] = ring_[(wpos_ + i) & (kN - 1)];
    double ms = 0;
    for (int i = 0; i < kN; ++i) ms += (double)x[i] * x[i];
    ms /= kN;
    const float lev = 10.0f * log10f((float)ms + 1e-20f);
    // how often a new loud sound starts (drums: often; a held note, a pad, a voice: rarely), per hop with sound in it.
    // Counted on every hop, stored or not, so a pause before a kick makes it an onset.
    if (lev > -45.0f) ++hopsSig_;
    if (lev > -42.0f && lev - prevLev_ > 8.0f) ++onsets_;
    prevLev_ = lev;
    if (lev < -50.0f || nStored_ >= kMaxFrames) return;   // silence (or full): only the wall clock counts it
    // spectrum
    for (int i = 0; i < kN; ++i) { re_[rev_[i]] = x[i] * win_[i]; im_[rev_[i]] = 0; }
    for (int len = 2; len <= kN; len <<= 1) {
        const int half = len >> 1, step = kN / len;
        for (int i = 0; i < kN; i += len)
            for (int j = 0; j < half; ++j) {
                const float c = cosT_[j * step], s = sinT_[j * step];
                const int a = i + j, b = a + half;
                const float tr = re_[b] * c - im_[b] * s, ti = re_[b] * s + im_[b] * c;
                re_[b] = re_[a] - tr; im_[b] = im_[a] - ti;
                re_[a] += tr; im_[a] += ti;
            }
    }
    float *P = pow_[nStored_];
    memset(P, 0, sizeof(float) * kLBins);
    const float norm = 2.0f / (kN * winPow_);   // one-sided power -> mean square (Parseval, Hann window)
    double hf = 0, all = 0;
    for (int k = 1; k < kN / 2; ++k) {
        const float hz = k * kFs / kN, p = (re_[k] * re_[k] + im_[k] * im_[k]) * norm;
        all += p;
        if (hz > 4000) hf += p;
        // spread the FFT bin over the 1/6-octave bins it overlaps (below ~250 Hz they are narrower than an FFT bin)
        const float f0 = hz - 0.5f * kFs / kN, f1 = hz + 0.5f * kFs / kN;
        if (f1 <= kLoHz || f0 >= kHiHz) continue;
        int b = std::max(0, binOf(std::max(f0, kLoHz)));
        for (; b < kLBins; ++b) {
            const float lo = kLoHz * powf(2.0f, b / 6.0f), hi = kLoHz * powf(2.0f, (b + 1) / 6.0f);
            if (lo >= f1) break;
            const float ov = std::min(hi, f1) - std::max(lo, f0);
            if (ov > 0) P[b] += p * ov / (f1 - f0);
        }
    }
    // pitch on the frame decimated by 4 (a 4-sample average is enough of a low pass for 61 Hz .. 1 kHz)
    float *dec = dec_;
    for (int i = 0; i < 512; ++i) dec[i] = 0.25f * (x[4 * i] + x[4 * i + 1] + x[4 * i + 2] + x[4 * i + 3]);
    const float f0 = lev > -45.0f ? yin(dec) : 0.0f;
    const bool sib = f0 == 0 && all > 0 && hf / all > 0.35;
    f0_[nStored_] = f0;
    kind_[nStored_] = f0 > 0 ? 1 : sib ? 2 : 3;
    ++nStored_;
}

static float percentile(float *v, int n, float p) {
    if (n <= 0) return 0;
    std::sort(v, v + n);
    const float k = p * (n - 1);
    const int i = (int)k;
    return i + 1 < n ? v[i] + (v[i + 1] - v[i]) * (k - i) : v[n - 1];
}

static float meanIn(const float *v, float lo, float hi) {
    const int b0 = std::max(0, binOf(lo)), b1 = std::min(kLBins - 1, binOf(hi));
    float s = 0;
    int n = 0;
    for (int b = b0; b <= b1; ++b) { s += v[b]; ++n; }
    return n ? s / n : 0;
}

void Learner::analyse(LearnResult &r, int hint) const {
    r = LearnResult();
    double sv[kLBins] = {}, su[kLBins] = {}, sa[kLBins] = {};
    float f0s[kMaxFrames];
    int nf = 0;
    for (int i = 0; i < nStored_; ++i) {
        ++r.framesSignal;
        for (int b = 0; b < kLBins; ++b) sa[b] += pow_[i][b];
        if (kind_[i] == 1) {
            ++r.framesVoiced;
            f0s[nf++] = f0_[i];
            for (int b = 0; b < kLBins; ++b) sv[b] += pow_[i][b];
        } else if (kind_[i] == 2) {
            ++r.framesUnvoiced;
            for (int b = 0; b < kLBins; ++b) su[b] += pow_[i][b];
        }
    }
    for (int b = 0; b < kLBins; ++b) {
        r.ltasV[b] = r.framesVoiced ? 10.0f * log10f((float)(sv[b] / r.framesVoiced) + 1e-20f) : -200.0f;
        r.ltasU[b] = r.framesUnvoiced ? 10.0f * log10f((float)(su[b] / r.framesUnvoiced) + 1e-20f) : -200.0f;
        r.ltasA[b] = r.framesSignal ? 10.0f * log10f((float)(sa[b] / r.framesSignal) + 1e-20f) : -200.0f;
    }
    // each bin against the mean of the octave around it (+-3 bins): resonances > 0, dips < 0
    for (int b = 0; b < kLBins; ++b) {
        float a = 0, u = 0, w = 0;
        int n = 0;
        for (int k = b - 3; k <= b + 3; ++k) {
            if (k < 0 || k >= kLBins) continue;
            a += r.ltasV[k]; u += r.ltasU[k]; w += r.ltasA[k]; ++n;
        }
        r.devV[b] = r.framesVoiced ? r.ltasV[b] - a / n : 0.0f;
        r.devU[b] = r.framesUnvoiced ? r.ltasU[b] - u / n : 0.0f;
        r.devA[b] = r.framesSignal ? r.ltasA[b] - w / n : 0.0f;
    }
    if (nf >= 12) {   // about 0.3 s of pitch
        r.f0Lo = percentile(f0s, nf, 0.05f);
        r.f0Med = percentile(f0s, nf, 0.5f);
        r.f0Hi = percentile(f0s, nf, 0.95f);
    }
    if (r.framesSignal < 30) return;   // under ~0.7 s of sound: not enough to tell what it is
    // ---- what is it? ----
    const int sig = r.framesSignal;
    r.pitchedFrac = (float)nf / sig;
    r.sibFrac = (float)r.framesUnvoiced / sig;
    r.onsetRate = hopsSig_ > 0 ? onsets_ / (hopsSig_ * (float)kHop / kFs) : 0.0f;
    double num = 0, den = 0, peak = -1e9;
    for (int b = 0; b < kLBins; ++b) {
        const double p = pow(10.0, r.ltasA[b] / 10.0);
        num += binHz(b) * p; den += p;
        peak = std::max(peak, (double)r.ltasA[b]);
    }
    r.centroid = den > 0 ? (float)(num / den) : 0.0f;
    r.lowHz = binHz(0);
    for (int b = 0; b < kLBins; ++b)
        if (r.ltasA[b] >= peak - 30.0) { r.lowHz = b == 0 ? 40.0f : binHz(b); break; }
    r.bright = meanIn(r.ltasA, 6000, 12000) - meanIn(r.ltasA, 1000, 3000);
    const bool pitched = r.pitchedFrac >= 0.30f && nf >= 12;
    const bool rangeOk = r.f0Med >= 70.0f && r.f0Med <= 1000.0f;
    // drums: mostly unpitched, and a new loud sound starts every so often (a voice is mostly pitched; a pad or a held
    // note starts rarely). With a Vox preset loaded, only clearly unpitched sound counts.
    const float pitchedMax = hint == kHintVoice ? 0.15f : 0.30f;
    const float onsetMin = hint == kHintDrums ? 0.8f : 1.2f;
    // (an 808 / sub bass line hits as often as a kick does: with a Bass preset loaded, low-heavy sound is a bass)
    const bool bassPreset = hint == kHintBass && r.centroid < 250.0f;
    if (r.pitchedFrac < pitchedMax && r.onsetRate >= onsetMin && !bassPreset) {
        r.src = kSrcDrums;
    } else {
        bool voice = false;
        if (pitched && rangeOk) {
            if (hint == kHintVoice) voice = true;
            else if (hint == kHintNone) voice = r.sibFrac >= 0.03f;   // s / t / sh between the vowels
            else voice = r.sibFrac >= 0.08f;                          // a non-voice preset: only when it is plainly a voice
        }
        const bool low = (pitched && r.f0Med < 110.0f) || r.centroid < 180.0f ||
                         (hint == kHintBass && (!pitched || r.f0Med < 200.0f));
        if (voice && nf >= 40) r.src = kSrcVoice;
        else if (low && hint != kHintVoice) r.src = kSrcBass;
        else r.src = kSrcInstrument;
    }
    r.ok = true;
}

// detector magnitude squared at hz (the shapes of Eq's detector filters)
static float detPow(int mode, float f0, float q, float hz) {
    const float x = hz / f0;
    switch (mode) {
    case 1: { const float y = q * (x - 1.0f / x); return 1.0f / (1.0f + y * y); }
    case 2: return 1.0f / (1.0f + x * x * x * x);
    case 3: { const float x4 = x * x * x * x; return x4 / (1.0f + x4); }
    default: return 1.0f;
    }
}

int Learner::bandLevels(int mode, float hz, float q, int which, float *out, int max) const {
    float w[kLBins];
    for (int b = 0; b < kLBins; ++b) w[b] = detPow(mode, hz, mode == 1 ? clampL(q, 0.5f, 4.0f) : 0.7071f, binHz(b));
    int n = 0;
    for (int i = 0; i < nStored_ && n < max; ++i) {
        if (which == 1 && kind_[i] != 1) continue;
        if (which == 2 && kind_[i] != 2) continue;
        double s = 0;
        for (int b = 0; b < kLBins; ++b) s += pow_[i][b] * w[b];
        out[n++] = 10.0f * log10f((float)s + 1e-20f) + 3.0f;   // the detector follows peaks: a sine's is 3 dB over its rms
    }
    return n;
}

// the strongest resonance (largest dev) in [lo, hi] Hz: its frequency (log-parabolic between bins) and height
static float peakIn(const float *dev, float lo, float hi, float *height, bool dip = false) {
    int b0 = std::max(0, binOf(lo)), b1 = std::min(kLBins - 1, binOf(hi));
    int best = -1;
    float bv = 0;
    for (int b = b0; b <= b1; ++b) {
        const float v = dip ? -dev[b] : dev[b];
        if (best < 0 || v > bv) { best = b; bv = v; }
    }
    if (best < 0) { *height = 0; return 0; }
    float off = 0;
    if (best > 0 && best < kLBins - 1) {
        const float a = dip ? -dev[best - 1] : dev[best - 1], c = dip ? -dev[best + 1] : dev[best + 1];
        const float den = a - 2 * bv + c;
        if (den < -1e-6f) off = clampL(0.5f * (a - c) / den, -0.5f, 0.5f);
    }
    *height = bv;
    return clampL(40.0f * powf(2.0f, (best + 0.5f + off) / 6.0f), lo, hi);
}

static float round_hz(float hz) {   // tidy numbers on the knob: 2 significant digits under 1 kHz-ish
    const float step = hz < 200 ? 1 : hz < 1000 ? 5 : hz < 5000 ? 10 : 50;
    return roundf(hz / step) * step;
}

void Learner::adapt(AdaptBand b[kBands], const LearnResult &r, const Learner &L, char *note, int len) {
    if (!r.ok) {
        snprintf(note, len, "Not enough sound: play it, then Learn again");
        return;
    }
    const Source src = r.src;
    const bool pitched = r.pitchedFrac >= 0.30f && r.f0Lo > 0;
    const char *tone = r.bright > (src == kSrcDrums ? -10.0f : -13.0f) ? "bright" : r.bright < (src == kSrcDrums ? -18.0f : -22.0f) ? "dark" : "balanced";
    switch (src) {
    case kSrcVoice: {
        const char *kind = r.f0Med < 130 ? "Low male" : r.f0Med < 260 ? "Alto/Tenor" : "Soprano";
        snprintf(note, len, "%s  %.0f-%.0f Hz", kind, r.f0Lo, r.f0Hi);
        break;
    }
    case kSrcDrums:
        snprintf(note, len, "Drums  low end %.0f Hz  %s", r.lowHz, tone);
        break;
    case kSrcBass:
        if (pitched) snprintf(note, len, "Bass  %.0f-%.0f Hz", r.f0Lo, r.f0Hi);
        else snprintf(note, len, "Bass  low end %.0f Hz", r.lowHz);
        break;
    default:
        if (pitched) snprintf(note, len, "Instrument  %.0f-%.0f Hz  %s", r.f0Lo, r.f0Hi, tone);
        else snprintf(note, len, "Instrument  low end %.0f Hz  %s", r.lowHz, tone);
        break;
    }
    // what each kind of source is measured on, and where its problem regions sit
    const bool voiceSrc = src == kSrcVoice;
    const float *dev = voiceSrc ? r.devV : r.devA;
    const float *ltas = voiceSrc ? r.ltasV : r.ltasA;
    float boomLo = 40, boomHi = 200, mudLo = 160, mudHi = 650, boxLo = 600, boxHi = 1800, harshLo = 2000, harshHi = 5000;
    float fizzLo = 4500, fizzHi = 11000, presLo = 2000, presHi = 5000, airBase = -16.0f, lowBase = 3.0f;
    switch (src) {
    case kSrcVoice:
        boomLo = std::max(70.0f, 0.9f * r.f0Lo); boomHi = 180; mudLo = std::max(160.0f, 1.3f * r.f0Med); airBase = -18.0f;
        break;
    case kSrcDrums:   // kick boom, snare / tom box and ring, cymbal harshness
        boomLo = 40; boomHi = 200; mudLo = 150; mudHi = 600; boxLo = 500; boxHi = 1600; harshLo = 2000; harshHi = 5500;
        fizzLo = 5000; fizzHi = 12000; presLo = 2000; presHi = 6000; airBase = -14.0f; lowBase = 10.0f;
        break;
    case kSrcBass:
        boomLo = std::max(35.0f, 0.9f * r.lowHz); boomHi = 140; mudLo = 120; mudHi = 450; boxLo = 400; boxHi = 1200;
        harshLo = 1500; harshHi = 4000; fizzLo = 3000; fizzHi = 8000; presLo = 800; presHi = 3000; airBase = -32.0f; lowBase = 12.0f;
        break;
    default:
        boomLo = pitched ? std::max(40.0f, 0.9f * r.f0Lo) : std::max(40.0f, r.lowHz);
        break;
    }
    // how much low end there is against the low mids: more than the class expects -> less boost, and the other way round
    const float lowBal = meanIn(ltas, 40, 120) - meanIn(ltas, 200, 800);
    float lv[kMaxFrames];
    for (int i = 0; i < kBands; ++i) {
        AdaptBand &x = b[i];
        if (!x.on) continue;
        const int t = x.type;
        if (t >= kLowCut6 && t <= kLowCut48) {
            if (voiceSrc) {   // the low cut sits just under the lowest notes
                x.hz = round_hz(clampL(0.9f * r.f0Lo, 70.0f, 200.0f));
            } else {          // just under the lowest content (the lowest note when it is pitched), within a factor 2 of the preset
                const float target = (pitched && src != kSrcDrums) ? 0.85f * r.f0Lo : 0.8f * r.lowHz;
                x.hz = round_hz(clampL(clampL(target, 0.5f * x.hz, 2.0f * x.hz), 20.0f, 400.0f));
            }
            continue;
        }
        const bool cut = x.gain < -0.25f || (x.gain <= 0.25f && x.dyn.rangeDb < 0);
        const bool boost = !cut && (x.gain > 0.25f || x.dyn.rangeDb > 0);
        if (t == kBell || t == kBandPass) {
            if (cut) {
                float lo, hi;
                const float *d = dev;
                if (x.hz < 160) { lo = boomLo; hi = boomHi; }                                       // boom
                else if (x.hz < 700) { lo = mudLo; hi = mudHi; }                                    // mud
                else if (x.hz < 1800) { lo = boxLo; hi = boxHi; }                                   // box / nasal
                else if (x.hz < 4500) { lo = harshLo; hi = harshHi; }                               // harsh
                else { lo = fizzLo; hi = fizzHi; if (voiceSrc && r.framesUnvoiced >= 5) d = r.devU; }   // sibilance / fizz
                if (lo < hi) {
                    float h;
                    const float f = peakIn(d, lo, hi, &h);
                    if (h >= 1.0f) x.hz = round_hz(f);
                    const float sc = clampL(0.5f + h / 4.0f, 0.5f, 1.6f);   // bigger resonance, deeper cut
                    x.gain = clampL(x.gain * sc, -12.0f, 0.0f);
                    x.dyn.rangeDb = clampL(x.dyn.rangeDb * sc, -12.0f, 0.0f);
                }
            } else if (boost) {
                if (voiceSrc ? x.hz < 500 : (x.hz >= 160 && x.hz < 500)) {   // body / warmth
                    if (voiceSrc || (pitched && src != kSrcDrums)) {
                        // around the fundamentals
                        x.hz = round_hz(clampL(1.5f * r.f0Med, 100.0f, 450.0f));
                    } else {   // drums, chords: the strongest bump in the body's region (a snare's shell, a pad's body)
                        float h;
                        const float f = peakIn(dev, 150, 400, &h);
                        if (h >= 1.0f) x.hz = round_hz(f);
                    }
                    const float d = meanIn(dev, x.hz / 1.26f, x.hz * 1.26f);
                    x.gain = clampL(x.gain * clampL(1.0f - d / 4.0f, 0.6f, 1.4f), 0.0f, 9.0f);
                } else if (!voiceSrc && x.hz < 160) {   // sub / punch: at the strongest low peak (a kick's fundamental)
                    int bb = binOf(40.0f), best = bb;
                    for (int k = bb; k <= binOf(140.0f) && k < kLBins; ++k) if (ltas[k] > ltas[best]) best = k;
                    if (t == kBell) x.hz = round_hz(clampL(binHz(best), 35.0f, 140.0f));
                    x.gain = clampL(x.gain * clampL(1.0f + (lowBase - lowBal) / 15.0f, 0.5f, 1.5f), 0.0f, 9.0f);
                } else if (x.hz > 1500 && x.hz < 6500) {   // presence: where the sound is missing some
                    float h;
                    const float f = peakIn(dev, presLo, presHi, &h, true);
                    if (h >= 1.0f) x.hz = round_hz(f);
                    x.gain = clampL(x.gain * clampL(1.0f + h / 4.0f, 0.6f, 1.5f), 0.0f, 9.0f);
                }
            }
        } else if (t == kLowShelf && !voiceSrc && boost && x.hz < 250) {   // low shelf boost: by how much low end there is
            x.gain = clampL(x.gain * clampL(1.0f + (lowBase - lowBal) / 15.0f, 0.5f, 1.5f), 0.0f, 9.0f);
        } else if (t == kHighShelf && x.gain > 0.25f && (voiceSrc ? r.framesVoiced > 0 : r.framesSignal > 0)) {   // air: duller sources get more
            const float bright = voiceSrc ? meanIn(r.ltasV, 6000, 12000) - meanIn(r.ltasV, 1000, 3000) : r.bright;
            x.gain = clampL(x.gain * clampL(1.0f + (airBase - bright) / 8.0f, 0.5f, 1.6f), 0.0f, 9.0f);
        }
        // a dynamic band's Threshold where the loud part of its own region starts
        if (x.dyn.rangeDb != 0 && typeHasDyn(t)) {
            const int mode = t == kLowShelf ? 2 : t == kHighShelf ? 3 : t == kTilt ? 4 : 1;
            const bool sib = voiceSrc && x.hz >= 4500 && x.dyn.rangeDb < 0 && r.framesUnvoiced >= 5;
            if (sib) {   // a de-esser: between the vowels' loudest in its band and the s's
                const int nu = L.bandLevels(mode, x.hz, x.q, 2, lv, kMaxFrames);
                const float u = percentile(lv, nu, 0.6f);
                const int nv = L.bandLevels(mode, x.hz, x.q, 1, lv, kMaxFrames);
                const float v = nv >= 10 ? percentile(lv, nv, 0.95f) : -90.0f;
                // nearer the s's than the vowels: a voiced sound's peaks sit higher over its rms than a sine's (the
                // +3 dB bandLevels assumes), so the vowels' own peaks reach further up than v
                const float thr = u > v + 3.0f ? v + 0.75f * (u - v) : u - 2.0f;
                x.dyn.thrDb = clampL(roundf(thr), -60.0f, 0.0f);
            } else {
                const int n = L.bandLevels(mode, x.hz, x.q, 0, lv, kMaxFrames);
                if (n >= 10) x.dyn.thrDb = clampL(roundf(percentile(lv, n, x.dyn.rangeDb < 0 ? 0.7f : 0.3f)), -60.0f, 0.0f);
            }
        }
    }
}

}  // namespace pq
