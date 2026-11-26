// Poli-tea DSP (see TeaDSP.h for the signal flow). Plain float C++, no allocation after construction, no libm calls per sample
// (fast log2 / exp2 / tanh approximations; the filters re-tune every 16 samples).
#include "TeaDSP.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace tea {

// ---- fast math ---------------------------------------------------------------------------------------------------------------
static inline float fastLog2(float x) {   // x > 0, error < 1e-4
    union { float f; uint32_t i; } u;
    u.f = x;
    const float e = (float)((int)(u.i >> 23) - 127);
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    const float m = u.f;
    return e + (-2.50569595f + m * (4.04981194f + m * (-2.09957074f + m * (0.63557273f + m * -0.08001883f))));
}
static inline float fastExp2(float x) {   // relative error < 1.1e-4
    x = clampf(x, -60.0f, 60.0f);
    const float fl = floorf(x);
    const float f = x - fl;
    const float p = 0.99989745f + f * (0.69638666f + f * (0.22451972f + f * 0.07908629f));
    union { float f; uint32_t i; } u;
    u.i = (uint32_t)((int)fl + 127) << 23;
    return u.f * p;
}
static inline float dbToLin(float db) { return fastExp2(db * 0.16609640f); }
static inline float ptanh(float x) {      // Pade tanh, C1-continuous clamp at +-3 (slope 0 there), error < 2 %
    x = clampf(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float flush1(float v) { return (v < 1e-15f && v > -1e-15f) ? 0.0f : v; }

static inline float bq(const float b0, const float b1, const float b2, const float a1, const float a2, float &z1, float &z2, float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

// band constants (tuned with test/tune.py against pink noise at the sweet spot, -18 dBFS RMS)
static const float kThrDb[3] = {-23.0f, -29.0f, -31.0f};      // compressor thresholds on the band's RMS-like level
static const float kMkK[3] = {6.3f, 5.0f, 6.0f};              // auto-makeup: dB of makeup per unit of (1 - 1/ratio), fitted on pink noise at the sweet spot
static const float kAtkMs[3] = {18.0f, 7.0f, 2.5f};
static const float kRelMs[3] = {200.0f, 130.0f, 80.0f};
static const float kKneeDb = 8.0f;

TeaDSP::TeaDSP() { init(44100.0f); }

void TeaDSP::init(float sr) {
    fs_ = sr;
    sm_ = 1.0f - expf(-1.0f / (0.010f * fs_));
    for (int i = 0; i < P_COUNT; ++i) pos_[i] = kDefaultPos[i];
    // half-band odd taps h[1], h[3], ... (Kaiser beta 8, 31 taps; designed offline, DC gain 1)
    static const float h[kHb] = {0.313045527f, -0.0912248081f, 0.0415367047f, -0.0192276379f, 0.00802032401f, -0.00273444142f, 0.000642254026f, -4.96298786e-05f};
    for (int i = 0; i < kHb; ++i) hb_[i] = h[i];
    scTarget_ = 0.0f;
    reset();
    updateTargets();
    // snap every smoothed value to its target
    gIn_ = gInT_; for (int b = 0; b < 3; ++b) { eqDb_[b] = eqDbT_[b]; eqDbDone_[b] = 1e9f; wid_[b] = widT_[b]; }
    exM_ = exMT_; exG_ = exGT_; amt_ = amtT_; s2_ = s2T_; push_ = pushT_; clipW_ = clipWT_; ceil_ = ceilT_;
    scMix_ = scTarget_;
    for (int b = 0; b < 3; ++b) { comp_[b].thr = thrT_[b]; comp_[b].mk = mkT_[b]; }
    ratio_ = ratioT_;
    designXover();
    retune();
}

void TeaDSP::reset() {
    memset(eqS_, 0, sizeof eqS_);
    xs_[0] = XoverState(); xs_[1] = XoverState(); xk_ = XoverState();
    memset(exOs_, 0, sizeof exOs_);
    memset(clOs_, 0, sizeof clOs_);
    memset(detOs_, 0, sizeof detOs_);
    memset(det2Os_, 0, sizeof det2Os_);
    memset(dry_, 0, sizeof dry_);
    dryPos_ = 0;
    memset(dcT_, 0, sizeof dcT_);
    memset(lpT_, 0, sizeof lpT_);
    memset(dcMid_, 0, sizeof dcMid_);
    for (int b = 0; b < 3; ++b) { Comp c; comp_[b] = c; }
    memset(dlL_, 0, sizeof dlL_);
    memset(dlR_, 0, sizeof dlR_);
    for (int i = 0; i < kLook; ++i) mring_[i] = 1.0f;
    msum_ = (double)kLook;
    lpos_ = 0;
    mpos_ = 0;
    dqHead_ = dqTail_ = 0;
    lcount_ = 0;
    lenv_ = 1.0f;
    lAvgGr_ = 0.0f;
    lrel_ = 0.001f;
    mPeak_ = 0; mSumSq_ = 0; mCount_ = 0;
    for (int i = 0; i < 4; ++i) mGr_[i] = 0;
    scMix_ = scTarget_;
}

void TeaDSP::setParam(int id, float pos) {
    if (id < 0 || id >= P_COUNT) return;
    pos_[id] = clampf(pos, 0.0f, 1000.0f);
    dirty_ = true;
}

// ---- parameter -> targets ----------------------------------------------------------------------------------------------------
void TeaDSP::updateTargets() {
    dirty_ = false;
    gInT_ = dbToLin(TeaMap::inputDb(pos_[P_INPUT]));
    eqDbT_[0] = TeaMap::focusPct(pos_[P_LOW]) * 0.06f;     // +-6 dB low shelf
    eqDbT_[1] = TeaMap::focusPct(pos_[P_MID]) * 0.04f;     // +-4 dB body bell
    eqDbT_[2] = TeaMap::focusPct(pos_[P_HIGH]) * 0.06f;    // +-6 dB air shelf
    const float amtS = TeaMap::amountPct(pos_[P_AMOUNT]) * 0.01f;     // 0 .. 2
    const float drv = TeaMap::drivePct(pos_[P_DRIVE]) * 0.01f;        // 0 .. 1
    amtT_ = amtS;
    exMT_ = drv * clampf(0.6f * amtS, 0.0f, 1.0f);
    exGT_ = 1.0f + 5.0f * drv * (0.4f + 0.3f * amtS);
    s2T_ = clampf(0.5f * amtS, 0.0f, 1.0f);
    widT_[0] = 0.0f;
    widT_[1] = 0.12f * amtS;
    widT_[2] = 0.30f * amtS;
    ratioT_ = 1.0f + 1.6f * amtS;
    const float over = amtS > 1.0f ? amtS - 1.0f : 0.0f;
    for (int b = 0; b < 3; ++b) {
        thrT_[b] = kThrDb[b] - 2.0f * over;
        mkT_[b] = kMkK[b] * (1.0f - 1.0f / ratioT_) * (1.0f + 0.3f * over);
    }
    ldr_ = TeaMap::ldrivePct(pos_[P_LDRIVE]) * 0.01f;
    pushT_ = dbToLin(-TeaMap::lthreshDb(pos_[P_LTHRESH]) + 6.0f * ldr_);
    clipWT_ = 0.85f * ldr_;
    ceilT_ = dbToLin(TeaMap::outputDb(pos_[P_OUTPUT]));
}

// ---- filter designs ----------------------------------------------------------------------------------------------------------
void TeaDSP::setLP(Biquad &b, float fc, float fs, float q) {
    const float w = 6.2831853f * fc / fs, c = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    b.b0 = (1.0f - c) * 0.5f / a0; b.b1 = (1.0f - c) / a0; b.b2 = b.b0; b.a1 = -2.0f * c / a0; b.a2 = (1.0f - al) / a0;
}
void TeaDSP::setHP(Biquad &b, float fc, float fs, float q) {
    const float w = 6.2831853f * fc / fs, c = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    b.b0 = (1.0f + c) * 0.5f / a0; b.b1 = -(1.0f + c) / a0; b.b2 = b.b0; b.a1 = -2.0f * c / a0; b.a2 = (1.0f - al) / a0;
}
void TeaDSP::setAP(Biquad &b, float fc, float fs, float q) {
    const float w = 6.2831853f * fc / fs, c = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    b.b0 = (1.0f - al) / a0; b.b1 = -2.0f * c / a0; b.b2 = 1.0f; b.a1 = -2.0f * c / a0; b.a2 = (1.0f - al) / a0;
    b.b2 = (1.0f + al) / a0;
}
void TeaDSP::setShelf(Biquad &b, bool high, float fc, float fs, float dB) {
    const float A = powf(10.0f, dB / 40.0f), w = 6.2831853f * fc / fs, c = cosf(w), s = sinf(w);
    const float al = s * 0.5f * sqrtf((A + 1.0f / A) * (1.0f / 0.8f - 1.0f) + 2.0f);   // shelf slope S = 0.8
    const float t = 2.0f * sqrtf(A) * al;
    float b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = A * ((A + 1) - (A - 1) * c + t); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - t);
        a0 = (A + 1) + (A - 1) * c + t; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - t;
    } else {
        b0 = A * ((A + 1) + (A - 1) * c + t); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - t);
        a0 = (A + 1) - (A - 1) * c + t; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - t;
    }
    b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
}
void TeaDSP::setPeak(Biquad &b, float fc, float fs, float q, float dB) {
    const float A = powf(10.0f, dB / 40.0f), w = 6.2831853f * fc / fs, c = cosf(w), al = sinf(w) / (2.0f * q);
    const float a0 = 1.0f + al / A;
    b.b0 = (1.0f + al * A) / a0; b.b1 = -2.0f * c / a0; b.b2 = (1.0f - al * A) / a0; b.a1 = -2.0f * c / a0; b.a2 = (1.0f - al / A) / a0;
}

void TeaDSP::designEq(int band, float dB) {
    if (band == 0) setShelf(eq_[0], false, 110.0f, fs_, dB);
    else if (band == 1) setPeak(eq_[1], 700.0f, fs_, 0.6f, dB);
    else setShelf(eq_[2], true, 9000.0f, fs_, dB);
}

void TeaDSP::designXover() {
    const float q = 0.70710678f;
    setLP(xc_.lp1, kXover1Hz, fs_, q); setHP(xc_.hp1, kXover1Hz, fs_, q);
    setLP(xc_.lp2, kXover2Hz, fs_, q); setHP(xc_.hp2, kXover2Hz, fs_, q);
    setAP(xc_.ap, kXover2Hz, fs_, q);
}

// Linkwitz-Riley 4th order (two cascaded Butterworths) 3-way split; the low band gets the allpass of the upper split so the bands sum flat
void TeaDSP::split(const XoverCoef &c, XoverState &s, float x, float &lo, float &mid, float &hi) {
    float l = bq(c.lp1.b0, c.lp1.b1, c.lp1.b2, c.lp1.a1, c.lp1.a2, s.lp1[0].z1, s.lp1[0].z2, x);
    l = bq(c.lp1.b0, c.lp1.b1, c.lp1.b2, c.lp1.a1, c.lp1.a2, s.lp1[1].z1, s.lp1[1].z2, l);
    float r = bq(c.hp1.b0, c.hp1.b1, c.hp1.b2, c.hp1.a1, c.hp1.a2, s.hp1[0].z1, s.hp1[0].z2, x);
    r = bq(c.hp1.b0, c.hp1.b1, c.hp1.b2, c.hp1.a1, c.hp1.a2, s.hp1[1].z1, s.hp1[1].z2, r);
    float m = bq(c.lp2.b0, c.lp2.b1, c.lp2.b2, c.lp2.a1, c.lp2.a2, s.lp2[0].z1, s.lp2[0].z2, r);
    m = bq(c.lp2.b0, c.lp2.b1, c.lp2.b2, c.lp2.a1, c.lp2.a2, s.lp2[1].z1, s.lp2[1].z2, m);
    float h = bq(c.hp2.b0, c.hp2.b1, c.hp2.b2, c.hp2.a1, c.hp2.a2, s.hp2[0].z1, s.hp2[0].z2, r);
    h = bq(c.hp2.b0, c.hp2.b1, c.hp2.b2, c.hp2.a1, c.hp2.a2, s.hp2[1].z1, s.hp2[1].z2, h);
    lo = bq(c.ap.b0, c.ap.b1, c.ap.b2, c.ap.a1, c.ap.a2, s.ap.z1, s.ap.z2, l);
    mid = m;
    hi = h;
}

// every 16 samples: EQ re-tune (only while a gain moves) and the compressors' program-dependent ballistics
void TeaDSP::retune() {
    eqOn_ = false;
    for (int b = 0; b < 3; ++b) {
        if (fabsf(eqDb_[b] - eqDbDone_[b]) > 0.005f) { designEq(b, eqDb_[b]); eqDbDone_[b] = eqDb_[b]; }
        if (fabsf(eqDb_[b]) > 0.004f || fabsf(eqDbT_[b]) > 0.004f) eqOn_ = true;
    }
    const float inv = 1.0f / fs_;
    for (int b = 0; b < 3; ++b) {
        Comp &c = comp_[b];
        const float crestDb = 3.0103f * (fastLog2(c.pk * c.pk + 1e-12f) - fastLog2(c.pw + 1e-12f));
        const float ct = clampf((crestDb - 3.0f) * (1.0f / 12.0f), 0.0f, 1.0f);      // 0 = sustained, 1 = very transient
        c.crest += 0.25f * (ct - c.crest);
        const float w = clampf(-c.avgGr * 0.2f, 0.0f, 1.0f);                          // deeper, longer reduction -> slower release
        const float atkMs = kAtkMs[b] * (0.5f + 1.5f * c.crest);
        const float relMs = kRelMs[b] * (0.6f + 0.9f * (1.0f - c.crest)) * (0.8f + 0.9f * w);
        c.atk = 1.0f - expf(-inv * 1000.0f / atkMs);
        c.rel = 1.0f - expf(-inv * 1000.0f / relMs);
    }
    // limiter release: fast for transients, slow once it has been working hard for a while; Limiter Drive makes it snappier
    const float w = clampf(lAvgGr_ * 0.25f, 0.0f, 1.0f);
    const float fastMs = 30.0f - 18.0f * ldr_, slowMs = 350.0f - 150.0f * ldr_;
    lrel_ = 1.0f - expf(-inv * 1000.0f / (fastMs + (slowMs - fastMs) * w));
}

void TeaDSP::flush() {
    for (int c = 0; c < 2; ++c) {
        for (int b = 0; b < 3; ++b) { eqS_[c][b].z1 = flush1(eqS_[c][b].z1); eqS_[c][b].z2 = flush1(eqS_[c][b].z2); }
        BqState *p = reinterpret_cast<BqState *>(&xs_[c]);
        for (size_t i = 0; i < sizeof(XoverState) / sizeof(BqState); ++i) { p[i].z1 = flush1(p[i].z1); p[i].z2 = flush1(p[i].z2); }
        dcT_[c] = flush1(dcT_[c]); lpT_[c] = flush1(lpT_[c]); dcMid_[c] = flush1(dcMid_[c]);
    }
    BqState *p = reinterpret_cast<BqState *>(&xk_);
    for (size_t i = 0; i < sizeof(XoverState) / sizeof(BqState); ++i) { p[i].z1 = flush1(p[i].z1); p[i].z2 = flush1(p[i].z2); }
    for (int b = 0; b < 3; ++b) { Comp &c = comp_[b]; if (c.pw < 1e-20f) c.pw = 0; if (c.pk < 1e-10f) c.pk = 0; }
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < 2 * kHb; ++i) { exOs_[c].up[i] = flush1(exOs_[c].up[i]); clOs_[c].up[i] = flush1(clOs_[c].up[i]); detOs_[c].up[i] = flush1(detOs_[c].up[i]); det2Os_[c].up[i] = flush1(det2Os_[c].up[i]); }
        for (int i = 0; i < 4 * kHb - 1; ++i) { exOs_[c].dn[i] = flush1(exOs_[c].dn[i]); clOs_[c].dn[i] = flush1(clOs_[c].dn[i]); }
    }
}

void TeaDSP::takeMeters(Meters &m) {
    m.inPeak = mPeak_;
    m.inRms = mCount_ ? sqrtf(mSumSq_ / (float)mCount_) : 0.0f;
    m.grLow = mGr_[0]; m.grMid = mGr_[1]; m.grHigh = mGr_[2]; m.grLim = mGr_[3];
    mPeak_ = 0; mSumSq_ = 0; mCount_ = 0;
    for (int i = 0; i < 4; ++i) mGr_[i] = 0;
}

// ---- 2x oversampling helpers (residual only: the linear signal never goes through them, so the response stays flat) ------------------
// up: two samples per input; returns even (the delayed input) and odd (the interpolated one)
static inline void up2(float *hist, const float *hfull, int n, int center, float x, float &even, float &odd) {
    for (int k = n - 1; k > 0; --k) hist[k] = hist[k - 1];
    hist[0] = x;
    even = hist[center];
    float s = 0;
    for (int m = 0; m < n; ++m) s += hfull[m] * hist[m];
    odd = s;
}
static inline float dn2(float *hist, const float *h, int kHb, float a, float b) {
    const int n = 4 * kHb - 1;
    for (int k = n - 1; k > 1; --k) hist[k] = hist[k - 2];
    hist[1] = a;
    hist[0] = b;
    const int c = 2 * kHb - 1;
    float z = 0.5f * hist[c];
    for (int j = 0; j < kHb; ++j) { const int k = 2 * j + 1; z += h[j] * (hist[c - k] + hist[c + k]); }
    return z;
}

// ---- process -----------------------------------------------------------------------------------------------------------------
void TeaDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n, const float *keyL, const float *keyR) {
    // full up-sampling kernel for the odd phase: 2 * h[2(m-kHb)+1] for m = 0..2kHb-1
    float hfull[2 * kHb];
    for (int m = 0; m < 2 * kHb; ++m) {
        const int k = 2 * (m - kHb) + 1;
        hfull[m] = 2.0f * hb_[(std::abs(k) - 1) / 2];
    }
    const float kTape = 1.0f - expf(-6.2831853f * 12000.0f / (2.0f * fs_));
    const float dcC = 1.0f - expf(-6.2831853f * 10.0f / (2.0f * fs_));
    const float dcMidC = 1.0f - expf(-6.2831853f * 10.0f / fs_);
    const float rmsA = 1.0f - expf(-1.0f / (0.015f * fs_));
    const float pkDec = expf(-1.0f / (0.05f * fs_));
    const float avgC = 1.0f / (0.4f * fs_);
    const float lAvgC = 1.0f / (0.25f * fs_);
    const float tubeB = 0.3f, tubeNorm = 1.0f / 0.915f;
    const float tubeOff = ptanh(tubeB);

    if (dirty_) updateTargets();

    for (int i = 0; i < n; ++i) {
        if ((tick_++ & 15) == 0) retune();
        // glide
        gIn_ += sm_ * (gInT_ - gIn_);
        for (int b = 0; b < 3; ++b) { eqDb_[b] += sm_ * (eqDbT_[b] - eqDb_[b]); wid_[b] += sm_ * (widT_[b] - wid_[b]); }
        exM_ += sm_ * (exMT_ - exM_); exG_ += sm_ * (exGT_ - exG_);
        amt_ += sm_ * (amtT_ - amt_); s2_ += sm_ * (s2T_ - s2_);
        push_ += sm_ * (pushT_ - push_); clipW_ += sm_ * (clipWT_ - clipW_); ceil_ += sm_ * (ceilT_ - ceil_);
        scMix_ += sm_ * (scTarget_ - scMix_);
        ratio_ += sm_ * (ratioT_ - ratio_);
        for (int b = 0; b < 3; ++b) { comp_[b].thr += sm_ * (thrT_[b] - comp_[b].thr); comp_[b].mk += sm_ * (mkT_[b] - comp_[b].mk); }

        const float rawL = inL[i], rawR = inR[i];
        float x[2] = {rawL * gIn_, rawR * gIn_};

        // input meter (after Input gain)
        {
            const float a = fabsf(x[0]), b = fabsf(x[1]);
            const float pk = a > b ? a : b;
            if (pk > mPeak_) mPeak_ = pk;
            mSumSq_ += 0.5f * (x[0] * x[0] + x[1] * x[1]);
            ++mCount_;
        }

        // key = the raw input (or the host's sidechain pair), mono, split into the three bands
        const float kl = keyL ? keyL[i] : rawL, kr = keyR ? keyR[i] : rawR;
        float klo, kmid, khi;
        split(xc_, xk_, 0.5f * (kl + kr), klo, kmid, khi);

        // ---- Focus EQ ----
        if (eqOn_) {
            for (int c = 0; c < 2; ++c) {
                float v = x[c];
                for (int b = 0; b < 3; ++b) v = bq(eq_[b].b0, eq_[b].b1, eq_[b].b2, eq_[b].a1, eq_[b].a2, eqS_[c][b].z1, eqS_[c][b].z2, v);
                x[c] = v;
            }
        }

        // ---- parallel tape + tube excitation (2x oversampled residual) ----
        float dry[2];
        for (int c = 0; c < 2; ++c) {
            dry_[c][dryPos_] = x[c];
            dry[c] = dry_[c][(dryPos_ + 16 - (2 * kHb - 1)) & 15];
            float ev, od;
            up2(exOs_[c].up, hfull, 2 * kHb, kHb, x[c], ev, od);
            float r2[2];
            const float v2[2] = {ev, od};
            for (int p = 0; p < 2; ++p) {
                const float v = v2[p], gv = exG_ * v;
                const float tape = ptanh(gv) / exG_;
                const float tube = (ptanh(gv + tubeB) - tubeOff) * tubeNorm / exG_;
                float tr = tape - v;
                lpT_[c] += kTape * (tr - lpT_[c]);               // tape's high-frequency loss, on the distortion only
                float ur = tube - v;
                dcT_[c] += dcC * (ur - dcT_[c]);                  // the bias' DC
                r2[p] = 0.5f * (lpT_[c] + (ur - dcT_[c]));
            }
            const float resid = dn2(exOs_[c].dn, hb_, kHb, r2[0], r2[1]);
            x[c] = dry[c] + exM_ * resid;
        }
        dryPos_ = (dryPos_ + 1) & 15;

        // ---- crossovers ----
        float lo[2], mid[2], hi[2];
        for (int c = 0; c < 2; ++c) split(xc_, xs_[c], x[c], lo[c], mid[c], hi[c]);
        float *band[3] = {lo, mid, hi};
        const float key[3] = {klo, kmid, khi};

        // ---- compressors (stereo linked), auto-makeup, band saturation ----
        for (int b = 0; b < 3; ++b) {
            Comp &c = comp_[b];
            const float a0 = fabsf(band[b][0]), a1 = fabsf(band[b][1]);
            const float dInt = a0 > a1 ? a0 : a1;
            const float dExt = fabsf(key[b]);
            const float d = dInt + scMix_ * (dExt - dInt);
            c.pw += rmsA * (d * d - c.pw);
            c.pk = d > c.pk ? d : c.pk * pkDec;
            const float lvl = 3.0103f * fastLog2(c.pw + 1e-12f);
            const float over = lvl - c.thr;
            const float slope = 1.0f / ratio_ - 1.0f;
            float tg;
            if (2.0f * over < -kKneeDb) tg = 0.0f;
            else if (2.0f * fabsf(over) <= kKneeDb) { const float t = over + 0.5f * kKneeDb; tg = slope * t * t / (2.0f * kKneeDb); }
            else tg = slope * over;
            c.gr += (tg < c.gr ? c.atk : c.rel) * (tg - c.gr);
            c.avgGr += avgC * (c.gr - c.avgGr);
            if (c.gr < mGr_[b]) mGr_[b] = c.gr;
            const float g = dbToLin(c.gr + c.mk);
            band[b][0] *= g;
            band[b][1] *= g;
        }
        // band saturation after compression: low = tape, mid = tube (both stay inside the bands, so the harmonics stay below Nyquist)
        {
            const float g2 = 1.5f + 1.5f * amt_;
            for (int c = 0; c < 2; ++c) {
                const float xl = lo[c];
                lo[c] = xl + s2_ * (ptanh(g2 * xl) / g2 - xl);
                const float xm = mid[c];
                float y = (ptanh(g2 * xm + tubeB) - tubeOff) * tubeNorm / g2;
                dcMid_[c] += dcMidC * (y - dcMid_[c]);
                mid[c] = xm + 0.8f * s2_ * ((y - dcMid_[c]) - xm);
            }
        }
        // ---- stereo matrix: low band mono (sub-120 Hz lock), mid and highs widened by Amount; M untouched so the mono sum is unchanged ----
        {
            const float ml = 0.5f * (lo[0] + lo[1]);
            lo[0] = lo[1] = ml;
            for (int b = 1; b < 3; ++b) {
                const float m = 0.5f * (band[b][0] + band[b][1]), s = 0.5f * (band[b][0] - band[b][1]) * (1.0f + wid_[b]);
                band[b][0] = m + s;
                band[b][1] = m - s;
            }
        }
        float y[2] = {(lo[0] + mid[0] + hi[0]) * push_, (lo[1] + mid[1] + hi[1]) * push_};

        // ---- soft clipper in front of the limiter (2x oversampled residual, only above its knee) ----
        {
            const float cc = ceil_, knee = 0.6f * cc, span = cc - knee, invSpan = 1.0f / span;
            float dyd[2];
            for (int c = 0; c < 2; ++c) {
                // keep the linear signal exactly as it is: delay it by the same amount
                dyd[c] = y[c];
            }
            for (int c = 0; c < 2; ++c) {
                float ev, od;
                up2(clOs_[c].up, hfull, 2 * kHb, kHb, y[c], ev, od);
                float r2[2];
                const float v2[2] = {ev, od};
                for (int p = 0; p < 2; ++p) {
                    const float v = v2[p], a = fabsf(v);
                    float f = v;
                    if (a > knee) { const float sh = knee + span * ptanh((a - knee) * invSpan); f = v < 0 ? -sh : sh; }
                    r2[p] = f - v;
                }
                const float resid = dn2(clOs_[c].dn, hb_, kHb, r2[0], r2[1]);
                // the delayed linear signal
                float *dl = dry_[c] + 16;   // second 16-slot ring lives behind the first (see header: dry_ is [2][32])
                dl[dryPos_ & 15] = dyd[c];
                const float lin = dl[(dryPos_ + 16 - (2 * kHb - 1)) & 15];
                y[c] = lin + clipW_ * resid;
            }
        }

        // ---- look-ahead brickwall limiter ----
        {
            // true-peak estimate: the half-band's 2x stream of the limiter input, and that stream's own 2x (four points per sample)
            float pk = 0;
            for (int c = 0; c < 2; ++c) {
                float ev, od, a, b;
                up2(detOs_[c].up, hfull, 2 * kHb, kHb, y[c], ev, od);
                up2(det2Os_[c].up, hfull, 2 * kHb, kHb, ev, a, b);
                float v = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
                if (v > pk) pk = v;
                up2(det2Os_[c].up, hfull, 2 * kHb, kHb, od, a, b);
                v = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
                if (v > pk) pk = v;
            }
#ifndef TEA_MARGIN
#define TEA_MARGIN 0.985f
#endif
            const float cd = ceil_ * TEA_MARGIN;
            const float r = pk > cd ? cd / pk : 1.0f;
            float e = lenv_ + lrel_ * (1.0f - lenv_);
            if (r < e) e = r;
            lenv_ = e;
            lAvgGr_ += lAvgC * (-20.0f * 0.30103f * fastLog2(e) - lAvgGr_);
            // sliding minimum over the last kLook values of e (monotonic deque)
            while (dqTail_ != dqHead_ && dqVal_[(dqTail_ - 1) & 255] >= e) --dqTail_;
            dqVal_[dqTail_ & 255] = e;
            dqIdx_[dqTail_ & 255] = lcount_;
            ++dqTail_;
            while ((uint32_t)(lcount_ - dqIdx_[dqHead_ & 255]) >= (uint32_t)kLook) ++dqHead_;
            const float m = dqVal_[dqHead_ & 255];
            ++lcount_;
            // moving average of the minimum
            msum_ += (double)m - (double)mring_[mpos_];
            mring_[mpos_] = m;
            mpos_ = mpos_ + 1 == kLook ? 0 : mpos_ + 1;
            const float g = (float)(msum_ * (1.0 / kLook));
            // audio delayed by kLook-1 + kHb (the 4x detector looks at the input as it was kDetLag samples ago)
            dlL_[lpos_] = y[0];
            dlR_[lpos_] = y[1];
            const int rp = lpos_ + 1 == kDl ? 0 : lpos_ + 1;
            lpos_ = rp;
            float oL = dlL_[rp] * g, oR = dlR_[rp] * g;
#ifdef TEA_NO_CLAMP
            oL = clampf(oL, -1.0f, 1.0f);
            oR = clampf(oR, -1.0f, 1.0f);
#else
            oL = clampf(oL, -ceil_, ceil_);
            oR = clampf(oR, -ceil_, ceil_);
#endif
            outL[i] = oL;
            outR[i] = oR;
            const float grDb = 20.0f * 0.30103f * fastLog2(g > 1e-6f ? g : 1e-6f);
            if (grDb < mGr_[3]) mGr_[3] = grDb;
        }
    }
    flush();
}

}  // namespace tea
