#include "SatDSP.h"
#include "halfband.h"
#include <cstring>
#include <algorithm>

namespace sat {

namespace {
const double kPi = 3.14159265358979323846;
#ifndef SAT_OS
#define SAT_OS 4
#endif
const int kOS = SAT_OS;                    // oversampling factor inside the shaper: 2 or 4
const int kCtl = 16;                        // controls (shaper constants, level match, ramps) are re-derived every 16 samples
const float kRefRms = 0.12f;                // the input level (about -18 dBFS RMS) the level match is aimed at
const float kGamma = 0.70f;                 // 1 = keep that level exactly, 0 = no level match
const float kF1 = 220.0f, kF2 = 2500.0f;    // band edges
const float kLn2db = 6.0205999f;            // dB per octave of amplitude

inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }

// 16-point Gauss-Hermite rule for a unit Gaussian (nodes, weights summing to 1): the level estimate of a shaper
const float kGhx[16] = {-6.63087820f, -5.47222571f, -4.49295530f, -3.60087362f, -2.76024505f, -1.95198035f, -1.16382910f, -0.38676060f,
                        0.38676060f, 1.16382910f, 1.95198035f, 2.76024505f, 3.60087362f, 4.49295530f, 5.47222571f, 6.63087820f};
const float kGhw[16] = {0.00000000f, 0.00000013f, 0.00001530f, 0.00052598f, 0.00726694f, 0.04728475f, 0.15833837f, 0.28656852f,
                        0.28656852f, 0.15833837f, 0.04728475f, 0.00726694f, 0.00052598f, 0.00001530f, 0.00000013f, 0.00000000f};

// ---- polyphase all-pass half-band filter (halfband.h) and the 4x up / down chains built from it ----------------------
template <int N>
struct HalfBand {
    static const int B0 = (N + 1) / 2, B1 = N / 2;
    float c0[B0], c1[B1], x0[B0], y0[B0], x1[B1], y1[B1], prevOdd;
    void init(const float *a) {
        for (int i = 0; i < B0; ++i) c0[i] = a[2 * i];
        for (int i = 0; i < B1; ++i) c1[i] = a[2 * i + 1];
        reset();
    }
    void reset() {
        memset(x0, 0, sizeof x0); memset(y0, 0, sizeof y0); memset(x1, 0, sizeof x1); memset(y1, 0, sizeof y1);
        prevOdd = 0;
    }
    inline float run0(float x) {
        for (int i = 0; i < B0; ++i) { const float y = c0[i] * (x - y0[i]) + x0[i]; x0[i] = x; y0[i] = y; x = y; }
        return x;
    }
    inline float run1(float x) {
        for (int i = 0; i < B1; ++i) { const float y = c1[i] * (x - y1[i]) + x1[i]; x1[i] = x; y1[i] = y; x = y; }
        return x;
    }
    inline void up(float x, float &e, float &o) { e = run0(x); o = run1(x); }
    inline float down(float e, float o) { const float y = 0.5f * (run0(e) + run1(prevOdd)); prevOdd = o; return y; }
    void flushState() {
        for (int i = 0; i < B0; ++i) { x0[i] = flush(x0[i]); y0[i] = flush(y0[i]); }
        for (int i = 0; i < B1; ++i) { x1[i] = flush(x1[i]); y1[i] = flush(y1[i]); }
        prevOdd = flush(prevOdd);
    }
};

struct Up4 {
    HalfBand<kHb1N> a;
    HalfBand<kHb2N> b;
    void init() { a.init(kHb1); b.init(kHb2); }
    inline void run(float x, float *o) {
        float e, od;
        a.up(x, e, od);
        if (kOS == 2) { o[0] = e; o[1] = od; return; }
        b.up(e, o[0], o[1]);
        b.up(od, o[2], o[3]);
    }
    void flushState() { a.flushState(); b.flushState(); }
};
struct Down4 {
    HalfBand<kHb1N> a;
    HalfBand<kHb2N> b;
    void init() { a.init(kHb1); b.init(kHb2); }
    inline float run(const float *i) {
        if (kOS == 2) return a.down(i[0], i[1]);
        const float e = b.down(i[0], i[1]);
        const float od = b.down(i[2], i[3]);
        return a.down(e, od);
    }
    void flushState() { a.flushState(); b.flushState(); }
};

// ---- 2-pole trapezoidal state-variable filter (Butterworth) and the Linkwitz-Riley 4 split built from three of them ----
struct Svf {
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.41421356f;
    void tune(float fc, float fs) {
        const float g = tanf((float)kPi * fc / fs);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void run(float x, float &lo, float &hi) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lo = v2;
        hi = x - k * v1 - v2;
    }
    // 2nd-order all-pass with the same poles as the LR4 sum (low + high of an LR4 at this corner): x - 2 k v1
    inline float allpass(float x) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return x - 2.0f * k * v1;
    }
    void flushState() { ic1 = flush(ic1); ic2 = flush(ic2); }
    void reset() { ic1 = ic2 = 0; }
};
struct Lr4 {   // low = LP(LP(x)), high = HP(HP(x)): the two sum to an all-pass
    Svf a, b, c;
    void tune(float fc, float fs) { a.tune(fc, fs); b.tune(fc, fs); c.tune(fc, fs); }
    inline void run(float x, float &lo, float &hi) {
        float l1, h1, t;
        a.run(x, l1, h1);
        b.run(l1, lo, t);
        c.run(h1, t, hi);
    }
    void flushState() { a.flushState(); b.flushState(); c.flushState(); }
    void reset() { a.reset(); b.reset(); c.reset(); }
};
struct Split {   // Low / Mid / High; the sum of the three is an all-pass of the input (flat magnitude)
    Lr4 a, b;
    Svf c;   // the f1 all-pass of the high branch: LR4 low + high = x - 2 k v1 of one SVF (see allpass())
    void tune(float f1, float f2, float fs) { a.tune(f2, fs); b.tune(f1, fs); c.tune(f1, fs); }
    inline void run(float x, float &low, float &mid, float &high) {
        float l2, h2;
        a.run(x, l2, h2);
        b.run(l2, low, mid);
        high = c.allpass(h2);   // the high branch gets the f1 all-pass the low branch got from its split (phase-aligned sum)
    }
    void flushState() { a.flushState(); b.flushState(); c.flushState(); }
    void reset() { a.reset(); b.reset(); c.reset(); }
};

// ---- the wave-shapers: every one has unity small-signal gain, y = f(u) with u = drive * x ----------------------------
struct Shape {
    float kT, bT, t0, invSlopeT;        // Tube: tanh(kT (u + bT)) - t0, normalised
    float tapeMix, b0, a0, invSlopeA;   // Tape: algebraic <-> tanh blend around a small bias
    float clipA, clipInv4W, clipTwoW;   // Clip: linear up to clipA, then a quadratic knee (2w wide) to 1
    float fold;                         // Fold: 0 = sine fold, 1 = triangle fold
};

inline float fastTanh(float x) {   // [7/6] Pade, clamped where it is within 1e-4 of 1
    x = x < -4.97f ? -4.97f : x > 4.97f ? 4.97f : x;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}
inline float algSig(float v) { return v / sqrtf(1.0f + v * v); }

void deriveShape(float c, Shape &s) {
    s.kT = 1.0f + 1.5f * c;
    s.bT = 0.10f + 0.35f * c;
    s.t0 = fastTanh(s.kT * s.bT);
    s.invSlopeT = 1.0f / (s.kT * (1.0f - s.t0 * s.t0));
    s.tapeMix = c;
    s.b0 = 0.04f + 0.16f * c;   // more bias (even harmonics) with Crunch
    s.a0 = algSig(s.b0);
    const float th = fastTanh(s.b0);
    s.invSlopeA = 1.0f / ((1.0f - c) * (1.0f / ((1.0f + s.b0 * s.b0) * sqrtf(1.0f + s.b0 * s.b0))) + c * (1.0f - th * th));
    const float w = 1.0f - 0.98f * powf(c, 0.7f);   // knee width: 1 (soft) .. 0.02 (nearly hard)
    s.clipA = 1.0f - w;
    s.clipInv4W = 0.25f / w;
    s.clipTwoW = 2.0f * w;
    s.fold = c;
}

inline float shapeAt(int type, const Shape &s, float u) {
    switch (type) {
        case T_TUBE: return (fastTanh(s.kT * (u + s.bT)) - s.t0) * s.invSlopeT;
        case T_TAPE: {
            const float v = u + s.b0;
            const float y = (1.0f - s.tapeMix) * algSig(v) + s.tapeMix * fastTanh(v);
            const float y0 = (1.0f - s.tapeMix) * s.a0 + s.tapeMix * fastTanh(s.b0);
            return (y - y0) * s.invSlopeA;
        }
        case T_CLIP: {
            const float t = fabsf(u);
            float y;
            if (t <= s.clipA) y = t;
            else {
                const float e = t - s.clipA;
                y = e < s.clipTwoW ? t - e * e * s.clipInv4W : 1.0f;
            }
            return u < 0 ? -y : y;
        }
        default: {   // T_FOLD
            const float sn = 0.63661977f * sinf(1.57079633f * u);
            float t = u + 1.0f;
            t -= 4.0f * floorf(t * 0.25f);
            const float tri = (t < 2.0f ? t : 4.0f - t) - 1.0f;
            return sn + s.fold * (tri - sn);
        }
    }
}

// Tube and Tape start a little softer on their own (their harmonics are rounded before Soften does anything)
const float kSoftenBase[T_COUNT] = {0.10f, 0.30f, 0.0f, 0.0f};

// the loudness correction k >= 1 that keeps a reference Gaussian signal at its own RMS through drive g
float levelMatch(int type, const Shape &s, float g) {
    float m1 = 0, m2 = 0;
    const float a = g * kRefRms;
    for (int i = 0; i < 16; ++i) {
        const float y = shapeAt(type, s, a * kGhx[i]);
        m1 += kGhw[i] * y;
        m2 += kGhw[i] * y * y;
    }
    const float var = m2 - m1 * m1;
    if (var < 1e-12f) return 1.0f;
    const float ratio = a / sqrtf(var);
    return ratio <= 1.0f ? 1.0f : powf(ratio, kGamma);
}

inline float softCeiling(float x) {   // linear to 0.9, then a smooth approach to 1.0
    const float a = fabsf(x);
    if (a < 0.9f) return x;
    const float t = 0.1f * fastTanh((a - 0.9f) * 10.0f);
    return x < 0 ? -(0.9f + t) : 0.9f + t;
}
}  // namespace

struct SatDSP::Impl {
    float fs = 44100.0f, fsO = 44100.0f * kOS;
    float tgt[P_COUNT], cur[P_COUNT];
    float smA = 0.01f;
    int ctl = 0;

    // type + crossfade
    int typeFrom = T_TUBE, typeTo = T_TUBE;
    float xf = 0, xfStep = 0;
    bool xfading = false;

    // Smash
    float env = 0, grDb = 0;
    float cT = -4.0f, cR = 1.0f, cW = 9.0f, cMk = 0.0f, aA = 0.01f, aR = 0.001f;
    bool smashOn = false;

    // oversampling, bands, shaping
    Up4 up[2];
    Down4 dn[2];
    Split split[2];
    Shape shp;
    float g[3], dg[3], gT[3], m[3], dm[3], mT[3], c[2][3], dc[2][3], cTgt[2][3];

    // Soften + DC
    float lpA = 1.0f, ws = 0, wsStep = 0, lp1[2], lp2[2];
    float dcR = 0.999f, dcx[2], dcy[2];

    Impl() { memset(tgt, 0, sizeof tgt); memset(cur, 0, sizeof cur); clearStates(); }

    void clearStates() {
        env = 0; grDb = 0;
        for (int ch = 0; ch < 2; ++ch) {
            up[ch].init(); dn[ch].init();
            split[ch].reset();
            lp1[ch] = lp2[ch] = 0; dcx[ch] = dcy[ch] = 0;
        }
        memset(g, 0, sizeof g); memset(dg, 0, sizeof dg); memset(gT, 0, sizeof gT);
        memset(m, 0, sizeof m); memset(dm, 0, sizeof dm); memset(mT, 0, sizeof mT);
        memset(c, 0, sizeof c); memset(dc, 0, sizeof dc); memset(cTgt, 0, sizeof cTgt);
    }

    float grStatic(float lv) const {   // soft-knee gain change in dB for an envelope level in dB
        const float over = lv - cT, k = 1.0f / cR - 1.0f;
        if (2.0f * over < -cW) return 0.0f;
        if (2.0f * fabsf(over) <= cW) { const float t = over + 0.5f * cW; return k * t * t / (2.0f * cW); }
        return k * over;
    }

    // re-derive everything that depends on the smoothed knobs; snap = start from the new values (no ramp)
    void controls(bool snap) {
        // Smash
        {
            const float s = cur[P_SMASH] * 0.001f;
            smashOn = s > 1e-4f;
            cR = 1.0f + 11.0f * s;
            cT = -4.0f - 26.0f * s;
            const float tA = 0.003f - 0.0025f * s, tR = 0.12f - 0.08f * s;
            aA = 1.0f - expf(-1.0f / (tA * fs));
            aR = 1.0f - expf(-1.0f / (tR * fs));
            cMk = -grStatic(-14.0f);   // level-neutral for a signal whose peaks sit around -14 dBFS
        }
        deriveShape(cur[P_CRUNCH] * 0.001f, shp);
        // drive and wet weight per band
        const float sat01 = cur[P_SAT] * 0.001f;
        const float drive = SatMap::driveDb(cur[P_SAT]);
        const float wetAll = clampf(sat01 * 25.0f, 0.0f, 1.0f);
        for (int b = 0; b < 3; ++b) {
            const float p = cur[P_LOW + b];
            gT[b] = exp2f((drive + SatMap::bandDb(p)) * 0.16609640474f);   // dB -> linear
            mT[b] = wetAll * clampf(p * (1.0f / 12.0f), 0.0f, 1.0f);
            cTgt[0][b] = levelMatch(typeFrom, shp, gT[b]) / gT[b];
            cTgt[1][b] = xfading ? levelMatch(typeTo, shp, gT[b]) / gT[b] : cTgt[0][b];
        }
        // Soften
        {
            const float base = kSoftenBase[typeFrom] + xf * (kSoftenBase[typeTo] - kSoftenBase[typeFrom]);
            const float s = base + (1.0f - base) * cur[P_SOFTEN] * 0.001f;
            const float fc = 20000.0f * powf(1.0f / 13.3333f, s);
            lpA = 1.0f - expf(-2.0f * (float)kPi * fc / fsO);
            wsTarget = clampf(s * 20.0f, 0.0f, 1.0f);
        }
        const float inv = 1.0f / kCtl;
        for (int b = 0; b < 3; ++b) {
            if (snap) { g[b] = gT[b]; m[b] = mT[b]; c[0][b] = cTgt[0][b]; c[1][b] = cTgt[1][b]; }
            dg[b] = (gT[b] - g[b]) * inv;
            dm[b] = (mT[b] - m[b]) * inv;
            dc[0][b] = (cTgt[0][b] - c[0][b]) * inv;
            dc[1][b] = (cTgt[1][b] - c[1][b]) * inv;
        }
        if (snap) ws = wsTarget;
        wsStep = (wsTarget - ws) * inv;
    }
    float wsTarget = 0;

    void flushAll() {
        for (int ch = 0; ch < 2; ++ch) {
            up[ch].flushState(); dn[ch].flushState(); split[ch].flushState();
            lp1[ch] = flush(lp1[ch]); lp2[ch] = flush(lp2[ch]); dcx[ch] = flush(dcx[ch]); dcy[ch] = flush(dcy[ch]);
        }
        env = flush(env);
    }
};

SatDSP::SatDSP() : d(new Impl()) {
    for (int i = 0; i < P_COUNT; ++i) pos_[i] = kDefaultPos[i];
}
SatDSP::~SatDSP() { delete d; }

void SatDSP::init(float sampleRate) {
    fs_ = sampleRate;
    Impl &s = *d;
    s.fs = sampleRate;
    s.fsO = (float)kOS * sampleRate;
    s.smA = 1.0f - expf(-1.0f / (0.012f * sampleRate));
    s.dcR = 1.0f - 2.0f * (float)kPi * 4.0f / sampleRate;   // DC blocker corner 4 Hz (-0.04 dB at 40 Hz)
    for (int ch = 0; ch < 2; ++ch) s.split[ch].tune(kF1, kF2, s.fsO);
    clear();
}

void SatDSP::clear() {
    Impl &s = *d;
    s.clearStates();
    for (int i = 0; i < P_COUNT; ++i) s.tgt[i] = s.cur[i] = pos_[i];
    s.typeFrom = s.typeTo = type_;
    s.xf = 0; s.xfading = false;
    s.controls(true);
    s.ctl = kCtl;
}

void SatDSP::setParam(int id, float pos) {
    if (id < 0 || id >= P_COUNT) return;
    pos_[id] = clampf(pos, 0.0f, 1000.0f);
    d->tgt[id] = pos_[id];
}

void SatDSP::setType(int type) {
    type = type < 0 ? 0 : type >= T_COUNT ? T_COUNT - 1 : type;
    if (type == type_) return;
    Impl &s = *d;
    if (s.xfading && s.xf > 0.5f) s.typeFrom = s.typeTo;   // a switch during a switch: carry on from the nearer one
    s.typeTo = type;
    s.xf = 0;
    s.xfStep = 1.0f / (0.010f * s.fs);
    s.xfading = true;
    type_ = type;
}

float SatDSP::grDbNow() const { return d->grDb; }

void SatDSP::process(const float *inL, const float *inR, float *outL, float *outR, int frames) {
    Impl &s = *d;
    const float *in[2] = {inL, inR};
    float *out[2] = {outL, outR};
    for (int n = 0; n < frames; ++n) {
        if (s.ctl == 0) { s.controls(false); s.ctl = kCtl; }
        --s.ctl;
        for (int i = 0; i < P_COUNT; ++i) s.cur[i] += (s.tgt[i] - s.cur[i]) * s.smA;
        if (s.xfading) {
            s.xf += s.xfStep;
            if (s.xf >= 1.0f) { s.xf = 0; s.xfading = false; s.typeFrom = s.typeTo; for (int b = 0; b < 3; ++b) { s.c[0][b] = s.c[1][b]; s.dc[0][b] = s.dc[1][b]; } }
        }
        // ---- Smash: a stereo-linked soft-knee compressor with level-neutral makeup
        float x[2] = {in[0][n], in[1][n]};
        {
            const float det = fmaxf(fabsf(x[0]), fabsf(x[1]));
            s.env += (det > s.env ? s.aA : s.aR) * (det - s.env);
            if (s.smashOn) {
                const float lv = kLn2db * log2f(s.env + 1e-6f);
                s.grDb = s.grStatic(lv) + s.cMk;
                const float gl = exp2f(s.grDb * 0.16609640474f);
                x[0] *= gl; x[1] *= gl;
            } else {
                s.grDb = 0.0f;
            }
        }
        for (int b = 0; b < 3; ++b) {   // ramp the per-band constants
            s.g[b] += s.dg[b]; s.m[b] += s.dm[b]; s.c[0][b] += s.dc[0][b]; s.c[1][b] += s.dc[1][b];
        }
        s.ws += s.wsStep;
        const bool act[3] = {s.m[0] > 1e-5f || s.mT[0] > 1e-5f, s.m[1] > 1e-5f || s.mT[1] > 1e-5f, s.m[2] > 1e-5f || s.mT[2] > 1e-5f};
        const float xfade = s.xf;
        const bool xfading = s.xfading;
        // ---- the oversampled core
        float o4[2][kOS];
        for (int ch = 0; ch < 2; ++ch) {
            float u4[kOS];
            s.up[ch].run(x[ch], u4);
            for (int j = 0; j < kOS; ++j) {
                float band[3];
                s.split[ch].run(u4[j], band[0], band[1], band[2]);
                float base = 0, delta = 0;
                for (int b = 0; b < 3; ++b) {
                    base += band[b];
                    if (!act[b]) continue;
                    const float u = s.g[b] * band[b];
                    float y = s.c[0][b] * shapeAt(s.typeFrom, s.shp, u);
                    if (xfading) y += xfade * (s.c[1][b] * shapeAt(s.typeTo, s.shp, u) - y);
                    delta += s.m[b] * (y - band[b]);
                }
                // Soften: low-pass only what the shaper added
                s.lp1[ch] += s.lpA * (delta - s.lp1[ch]);
                s.lp2[ch] += s.lpA * (s.lp1[ch] - s.lp2[ch]);
                o4[ch][j] = base + delta + s.ws * (s.lp2[ch] - delta);
            }
        }
        for (int ch = 0; ch < 2; ++ch) {
            const float y = s.dn[ch].run(o4[ch]);
            const float z = y - s.dcx[ch] + s.dcR * s.dcy[ch];   // DC blocker
            s.dcx[ch] = y;
            s.dcy[ch] = z;
            out[ch][n] = softCeiling(z);
        }
    }
    s.flushAll();
}

}  // namespace sat
