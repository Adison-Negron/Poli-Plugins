// Shhhhh DSP. See ShhDSP.h for the design.
#include "ShhDSP.h"

#include <cmath>
#include <cstring>

namespace shh {

// Default node frequencies: the mockup's 1..6 (on 100, 200, 500, between 500 and 1k, between 1k and 5k, on 10k).
const float kNodeDefaultHz[kNodes] = {100.0f, 200.0f, 500.0f, 800.0f, 3500.0f, 10000.0f};

static const float kPi = 3.14159265358979f;
static const float kGlide = 0.2516f;               // 1 - exp(-128 / (10 ms * 44.1 kHz)): ~10 ms per-hop glide
static const float kLearnBias = 1.29f;             // quietest-quarter estimate reads 1.1 dB low on steady noise (dsp_test)
static const float kMinPow = 1e-14f;               // -140 dB: profile floor (digital silence learned)
static const float kXfade = 1.0f / 441.0f;         // 10 ms linear crossfades (Pass, Diff)

static inline float fclamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float dbToPow(float db) { return expf(0.230258509f * db); }
static inline float powToDb(float p) { return 4.34294482f * logf(p > 1e-30f ? p : 1e-30f); }
static inline bool finitef(float v) { uint32_t u; memcpy(&u, &v, 4); return (u & 0x7f800000u) != 0x7f800000u; }

float ShhDSP::bandLoHz(int j) { return 20.0f * powf(1000.0f, (float)j / kBands); }

void ShhDSP::init() {
    // Mauler & Martin low-delay windows. Analysis: the rising half of a 768-sample sqrt-Hann, then the falling half of
    // a 256-sample sqrt-Hann. Synthesis: zero for the first 256, then Hann256 / analysis, so that wa * ws is a
    // Hann256 over the frame's last 256 samples, which overlap-adds to exactly 1 at a 128 hop.
    const int M = kHop, N = kN;
    double sum = 0;
    for (int n = 0; n < N; ++n) {
        float a, h;
        if (n < N - M) a = sinf(kPi * n / (2.0f * (N - M)));
        else a = sinf(kPi * (n - (N - 2 * M)) / (2.0f * M));
        wa_[n] = a;
        sum += a;
        if (n < N - 2 * M) ws_[n] = 0.0f;
        else {
            h = 0.5f - 0.5f * cosf(2.0f * kPi * (n - (N - 2 * M)) / (2.0f * M));
            ws_[n] = a > 1e-9f ? h / a : 0.0f;
        }
    }
    norm_ = (float)(4.0 / (sum * sum));
    for (int k = 0; k < N / 2; ++k) { cosT_[k] = cosf(2.0f * kPi * k / N); sinT_[k] = -sinf(2.0f * kPi * k / N); }
    int bits = 0;
    while ((1 << bits) < N) ++bits;
    for (int i = 0; i < N; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[i] = (uint16_t)r;
    }

    // node candidate positions: inside the node's segment (boundaries halfway, in octaves, between the defaults),
    // 0.22 octave from each edge so neighbours never overlap on screen; index 2 is the default itself
    for (int i = 0; i < kNodes; ++i) {
        const float d = kNodeDefaultHz[i];
        float lo = i == 0 ? 20.0f : sqrtf(kNodeDefaultHz[i - 1] * d);
        float hi = i == kNodes - 1 ? 20000.0f : sqrtf(kNodeDefaultHz[i + 1] * d);
        lo *= powf(2.0f, 0.22f);
        hi /= powf(2.0f, 0.22f);
        if (lo > d) lo = d;
        if (hi < d) hi = d;
        candHz_[i][0] = lo;
        candHz_[i][1] = sqrtf(lo * d);
        candHz_[i][2] = d;
        candHz_[i][3] = sqrtf(d * hi);
        candHz_[i][4] = hi;
    }
    // spectrum columns: 32 log bands 20 Hz..20 kHz; a band without a bin centre reads the interpolated level
    const float binHz = kSR / kN;
    for (int j = 0; j < kBands; ++j) {
        const float lo = bandLoHz(j), hi = bandLoHz(j + 1);
        int k0 = (int)ceilf(lo / binHz), k1 = (int)ceilf(hi / binHz);
        if (k1 > kBins) k1 = kBins;
        if (k1 > k0) { bandK0_[j] = k0; bandK1_[j] = k1; bandW_[j] = 0; }
        else {
            const float kf = sqrtf(lo * hi) / binHz;
            bandK0_[j] = (int)kf; bandK1_[j] = -1; bandW_[j] = kf - (int)kf;
        }
    }

    for (int k = 0; k < kBins; ++k) profPow_[k] = dbToPow(kDefaultProfileDb);
    learned_ = false;
    thrT_ = thrS_ = 0;
    redT_ = redS_ = 12;
    for (int i = 0; i < kNodes; ++i) { offT_[i] = offS_[i] = 0; nodePos_[i] = 2; }
    bypass_ = diff_ = false;
    aPass_ = aDiff_ = 0;
    outT_ = outS_ = 1;
    learning_ = false;
    previewValid_ = false;
    // critical bands (Zwicker) for masking; smoothing widths for wideHf
    for (int k = 0; k < kBins; ++k) {
        const float f = k * kSR / kN;
        const float z = 13.0f * atanf(0.00076f * f) + 3.5f * atanf((f / 7500.0f) * (f / 7500.0f));
        bark_[k] = z < 31 ? (int)z : 31;
        if (bark_[k] + 1 > nBark_) nBark_ = bark_[k] + 1;
        int w = (int)(k * 0.058f);   // +-1/12 octave
        smW_[k] = w < 1 ? 1 : w > 12 ? 12 : w;
    }
    for (int b = 0; b < 32; ++b)
        for (int j = 0; j < 32; ++j) {
            const float d = (float)(b - j) + 0.474f;
            spreadTab_[b][j] = dbToPow(15.81f + 7.5f * d - 17.5f * sqrtf(1.0f + d * d));
        }
    rebuildNodeInterp();
    clearState();
    resets = 0;
}

void ShhDSP::clearState() {
    memset(histL_, 0, sizeof histL_);
    memset(histR_, 0, sizeof histR_);
    memset(olaL_, 0, sizeof olaL_);
    memset(olaR_, 0, sizeof olaR_);
    for (int k = 0; k < kBins; ++k) { gPrev_[k] = 1; gamPrev_[k] = 1; gRel_[k] = 1; gain_[k] = 1; spp_[k] = 0.5f; relS_[k] = 1; }
    for (int k = 0; k < kBins; ++k) { noise_[k] = profPow_[k]; sppT_[k] = 0; }
    for (int j = 0; j < kBands; ++j) { bandPeak_[j] = 0; dispDb_[j] = kGridBotDb; }
    dispHop_ = 0;
    curveDirty_ = true;
}

void ShhDSP::setThreshold(float db) { if (finitef(db)) thrT_ = fclamp(db, -30.0f, 30.0f); }
void ShhDSP::setReduction(float db) { if (finitef(db)) redT_ = fclamp(db, 0.0f, 100.0f); }
void ShhDSP::setOutput(float db) { if (finitef(db)) outT_ = dbToPow(0.5f * fclamp(db, -24.0f, 24.0f)); }

void ShhDSP::setNodePos(int i, int p) {
    if (i < 0 || i >= kNodes) return;
    nodePos_[i] = p < 0 ? 0 : p >= kNodePos ? kNodePos - 1 : p;
    rebuildNodeInterp();
}

void ShhDSP::setNodeOffset(int i, float db) {
    if (i < 0 || i >= kNodes || !finitef(db)) return;
    offT_[i] = fclamp(db, -150.0f, 150.0f);
    curveDirty_ = true;
}

float ShhDSP::localDb(const float *pow, float hz, float halfOct) const {
    const float binHz = kSR / kN;
    const float f = halfOct > 0 ? powf(2.0f, halfOct) : 1.0f;
    int k0 = (int)ceilf(hz / f / binHz), k1 = (int)floorf(hz * f / binHz);
    if (k0 < 1) k0 = 1;
    if (k1 > kBins - 1) k1 = kBins - 1;
    double s = 0;
    if (k1 >= k0) {
        for (int k = k0; k <= k1; ++k) s += pow[k];
        s /= (k1 - k0 + 1);
    } else {   // narrower than a bin: interpolate between the two bins around hz
        float kf = hz / binHz;
        if (kf < 1) kf = 1;
        if (kf > kBins - 1) kf = kBins - 1;
        const int a = (int)kf;
        const float w = kf - a;
        s = pow[a] * (1 - w) + pow[a + 1 < kBins ? a + 1 : a] * w;
    }
    return powToDb((float)s);
}

float ShhDSP::nodeLevel(int i) const {
    if (i < 0 || i >= kNodes) return kDefaultProfileDb;
    const float *p = learning_ && previewValid_ ? previewPow_ : profPow_;
    const float off = learning_ && previewValid_ ? 0.0f : offT_[i];
    return fclamp(localDb(p, nodeHz(i), 1.0f / 6.0f) + thrT_ + off, kGridBotDb, kGridTopDb);
}

void ShhDSP::setNodeLevel(int i, float db) {
    if (i < 0 || i >= kNodes || !finitef(db) || learning_) return;
    db = fclamp(db, kGridBotDb, kGridTopDb);
    setNodeOffset(i, db - (localDb(profPow_, nodeHz(i), 1.0f / 6.0f) + thrT_));
}

float ShhDSP::profileDb(int k) const { return k >= 0 && k < kBins ? powToDb(profPow_[k]) : kDefaultProfileDb; }
void ShhDSP::setProfileDb(int k, float db) {
    if (k < 0 || k >= kBins || !finitef(db)) return;
    profPow_[k] = dbToPow(fclamp(db, -140.0f, 20.0f));
    noise_[k] = profPow_[k];
    curveDirty_ = true;
}

void ShhDSP::rebuildNodeInterp() {
    const float binHz = kSR / kN;
    float lf[kNodes];
    for (int i = 0; i < kNodes; ++i) lf[i] = logf(nodeHz(i));
    for (int k = 0; k < kBins; ++k) {
        const float l = logf((k < 1 ? 0.5f : (float)k) * binHz);
        if (l <= lf[0]) { interpA_[k] = interpB_[k] = 0; interpW_[k] = 1; continue; }
        if (l >= lf[kNodes - 1]) { interpA_[k] = interpB_[k] = kNodes - 1; interpW_[k] = 1; continue; }
        int i = 0;
        while (i < kNodes - 2 && l > lf[i + 1]) ++i;
        interpA_[k] = i;
        interpB_[k] = i + 1;
        interpW_[k] = 1.0f - (l - lf[i]) / (lf[i + 1] - lf[i]);
    }
    curveDirty_ = true;
}

void ShhDSP::computeThresholdCurve() {
    for (int k = 0; k < kBins; ++k) {
        const float off = offS_[interpA_[k]] * interpW_[k] + offS_[interpB_[k]] * (1.0f - interpW_[k]);
        thrScale_[k] = dbToPow(thrS_ + off);
        thrPow_[k] = (tuning.track ? noise_[k] : profPow_[k]) * thrScale_[k];
    }
}

// Adaptive mode: the noise estimate follows the audio (Gerkmann & Hendriks 2012: a-posteriori speech presence with a
// fixed 15 dB a-priori SNR, E[|N|^2 | y] = (1-p)|Y|^2 + p*noise, a 0.99 cap against stagnation), but stays within
// +-trackDb of the learned profile, so speech can never become "noise" and a quiet room never pulls it to nothing
void ShhDSP::trackNoise(const float *P) {
    const Tuning &t = tuning;
    const float a = expf(-kHop / (t.trackMs * 0.001f * kSR));
    const float xiH1 = 31.62f, k1 = xiH1 / (1.0f + xiH1), c = 1.0f + xiH1;
    const float lim = dbToPow(t.trackDb), ilim = 1.0f / lim;
    for (int k = 0; k < kBins; ++k) {
        const float nk = noise_[k];
        float p = 1.0f / (1.0f + c * expf(-(P[k] / nk) * k1));
        if (sppT_[k] > 0.99f) p = p < 0.99f ? p : 0.99f;
        sppT_[k] = 0.9f * sppT_[k] + 0.1f * p;
        float n2 = a * nk + (1.0f - a) * ((1.0f - p) * P[k] + p * nk);
        const float lo = learned_ ? profPow_[k] * ilim : 1e-13f, hi = learned_ ? profPow_[k] * lim : 1e-4f;
        noise_[k] = n2 < lo ? lo : n2 > hi ? hi : n2;
        thrPow_[k] = noise_[k] * thrScale_[k];
    }
}

void ShhDSP::maskFloor(const float *P, float *g) const {
    // voice estimate per bin = |Y|^2 * g^2; Bark-band energies, spread (Schroeder), offset, back to per-bin power;
    // the residual noise needs to go no lower than that: g >= sqrt(mask / noise)
    float band[32] = {0}, cnt[32] = {0}, spread[32];
    for (int k = 1; k < kBins; ++k) { band[bark_[k]] += P[k] * g[k] * g[k]; cnt[bark_[k]] += 1; }
    for (int b = 0; b < nBark_; ++b) {
        double s = 0;
        for (int j = 0; j < nBark_; ++j) s += band[j] * spreadTab_[b][j];
        spread[b] = cnt[b] > 0 ? (float)s / cnt[b] * dbToPow(-tuning.maskOffDb) : 0.0f;
    }
    for (int k = 1; k < kBins; ++k) {
        const float nz = thrPow_[k];
        if (nz <= 0) continue;
        float m = spread[bark_[k]] / nz;
        if (m >= 1.0f) { g[k] = 1.0f; continue; }
        m = sqrtf(m);
        if (g[k] < m) g[k] = m;
    }
}

void ShhDSP::startLearn() {
    learning_ = true;
    subHop_ = 0;
    subsDone_ = 0;
    previewValid_ = false;
    memset(subAcc_, 0, sizeof subAcc_);
}

void ShhDSP::stopLearn() { if (learning_) finishLearn(subsDone_ >= 2); }

void ShhDSP::estimateProfile(float *dst) const {
    const int S = subsDone_;
    if (S < 1) return;
    const int q = S / 4 > 1 ? S / 4 : 1;
    float v[kSubs];
    for (int k = 0; k < kBins; ++k) {
        for (int s = 0; s < S; ++s) v[s] = subMean_[s][k];
        double sum = 0;
        for (int j = 0; j < q; ++j) {   // the q quietest sub-windows (partial selection sort)
            int m = j;
            for (int s = j + 1; s < S; ++s) if (v[s] < v[m]) m = s;
            const float t = v[j]; v[j] = v[m]; v[m] = t;
            sum += v[j];
        }
        const float p = (float)(sum / q) * kLearnBias;
        dst[k] = p > kMinPow ? p : kMinPow;
    }
}

void ShhDSP::placeNodes(const float *pow) {
    // each node goes to the most prominent noise peak inside its segment (a hum line, a whine), snapped to the
    // nearest of its 5 positions; with no peak at least 3 dB above its surroundings it stays at its default
    const float binHz = kSR / kN;
    float db[kBins];
    for (int k = 0; k < kBins; ++k) db[k] = powToDb(pow[k]);
    for (int i = 0; i < kNodes; ++i) {
        const float lo = candHz_[i][0] / powf(2.0f, 0.22f), hi = candHz_[i][kNodePos - 1] * powf(2.0f, 0.22f);
        int k0 = (int)ceilf(lo / binHz), k1 = (int)floorf(hi / binHz);
        if (k0 < 1) k0 = 1;
        if (k1 > kBins - 2) k1 = kBins - 2;
        float bestProm = 3.0f, bestHz = 0;
        for (int k = k0; k <= k1; ++k) {
            // local peaks only (bin 1 isn't compared with DC: a 50/60 Hz hum lands between bins 0 and 1)
            if (!((k == 1 || db[k] >= db[k - 1]) && db[k] >= db[k + 1])) continue;
            // surroundings: bins within 2/3 octave, without the peak's own 3 bins (at least bins k-3, k+3)
            const float f = k * binHz;
            int a = (int)ceilf(f / 1.587f / binHz), b = (int)floorf(f * 1.587f / binHz);
            if (a > k - 3) a = k - 3;
            if (b < k + 3) b = k + 3;
            if (a < 1) a = 1;
            if (b > kBins - 1) b = kBins - 1;
            double s = 0; int n = 0;
            for (int j = a; j <= b; ++j) if (j < k - 1 || j > k + 1) { s += db[j]; ++n; }
            if (!n) continue;
            const float prom = db[k] - (float)(s / n);
            if (prom > bestProm) {
                // parabolic peak position between the bins
                const float y0 = db[k - 1], y1 = db[k], y2 = db[k + 1], den = y0 - 2 * y1 + y2;
                const float d = fabsf(den) > 1e-6f ? fclamp(0.5f * (y0 - y2) / den, -0.5f, 0.5f) : 0.0f;
                bestProm = prom;
                bestHz = (k + d) * binHz;
            }
        }
        int pos = 2;
        if (bestHz > 0) {
            float bestD = 1e9f;
            for (int c = 0; c < kNodePos; ++c) {
                const float dd = fabsf(logf(bestHz / candHz_[i][c]));
                if (dd < bestD) { bestD = dd; pos = c; }
            }
        }
        nodePos_[i] = pos;
    }
    rebuildNodeInterp();
}

void ShhDSP::finishLearn(bool commit) {
    if (commit) {
        estimateProfile(profPow_);
        for (int k = 0; k < kBins; ++k) noise_[k] = profPow_[k];
        learned_ = true;
        placeNodes(profPow_);
        for (int i = 0; i < kNodes; ++i) offT_[i] = 0;   // a fresh learn starts from the fresh profile
        curveDirty_ = true;
    }
    learning_ = false;
    previewValid_ = false;
}

// in-place radix-2 DIT complex FFT (forward: e^-i); call with (im, re) swapped for the unscaled inverse
void ShhDSP::fft(float *re, float *im) const {
    const int N = kN;
    for (int i = 0; i < N; ++i) {
        const int j = rev_[i];
        if (j > i) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= N; len <<= 1) {
        const int half = len >> 1, step = N / len;
        for (int i = 0; i < N; i += len) {
            for (int j = 0; j < half; ++j) {
                const float wr = cosT_[j * step], wi = sinT_[j * step];
                const int a = i + j, b = a + half;
                const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
            }
        }
    }
}

// E1(x), the exponential integral (MMSE-LSA gain): series below 1, Abramowitz & Stegun 5.1.56 above
static inline float expint1(float x) {
    if (x < 1e-6f) x = 1e-6f;
    if (x < 1.0f) return -0.5772157f - logf(x) + x * (1.0f - x * (0.25f - x * (0.0555556f - x * 0.0104167f)));
    if (x > 30.0f) return 0.0f;
    const float n = x * x + 2.334733f * x + 0.250621f, d = x * x + 3.330657f * x + 1.681534f;
    return expf(-x) / x * n / d;
}

void ShhDSP::gainStage(const float *P, float redK) {
    const Tuning &t = tuning;
    const float al = t.alpha;
    const float rel = t.relMs > 0 ? expf(-kHop / (t.relMs * 0.001f * kSR)) : 0.0f;
    const float xiH1 = dbToPow(t.xiH1Db), sppK = xiH1 / (1.0f + xiH1);
    float g[kBins];
    for (int k = 0; k < kBins; ++k) {
        const float gam = P[k] / thrPow_[k];
        float xi = al * gPrev_[k] * gPrev_[k] * gamPrev_[k] + (1.0f - al) * (gam > 1.0f ? gam - 1.0f : 0.0f);
        if (xi < 1e-12f) xi = 0;
        float gw = xi / (1.0f + xi);
        gPrev_[k] = gw;
        gamPrev_[k] = gam < 1e12f ? gam : 1e12f;
        if (t.tsnr) { xi = gw * gw * gam; gw = xi / (1.0f + xi); }   // second step: DD's one-frame lag removed
        if (t.rule == 1 && xi > 0) {
            const float v = gw * gam;
            gw = fclamp(gw * expf(0.5f * expint1(v)), 0.0f, 1.0f);
        }
        if (t.presence) {   // a-posteriori speech presence probability, fixed xi_H1, prior 1/2, smoothed against stagnation
            float p = 1.0f / (1.0f + (1.0f + xiH1) * expf(-gam * sppK));
            if (spp_[k] > 0.99f && p > 0.99f) p = 0.99f;
            spp_[k] = 0.9f * spp_[k] + 0.1f * p;   // Gerkmann & Hendriks' smoothing of p for the stagnation check
            g[k] = p;
            gRel_[k] = gw;   // keep the amplitude gain for OM-LSA
        } else g[k] = gw;
    }
    float s[kBins];
    for (int k = 0; k < kBins; ++k) {
        float r = relS_[k];
        r = g[k] > r ? g[k] : r * rel + g[k] * (1.0f - rel);
        relS_[k] = r < 1e-12f ? 0.0f : r;
    }
    if (t.wideHf) {   // box of +-smW bins (prefix sums), averaged half-and-half with the bin: wider up high
        float cs[kBins + 1];
        cs[0] = 0;
        for (int k = 0; k < kBins; ++k) cs[k + 1] = cs[k] + relS_[k];
        for (int k = 0; k < kBins; ++k) {
            const int a = k - smW_[k] < 0 ? 0 : k - smW_[k], b = k + smW_[k] > kBins - 1 ? kBins - 1 : k + smW_[k];
            const float box = (cs[b + 1] - cs[a]) / (b - a + 1);
            s[k] = 0.5f * relS_[k] + 0.5f * box;
        }
    } else for (int k = 0; k < kBins; ++k) {
        if (t.freqSmooth) {
            const float a = relS_[k > 0 ? k - 1 : 1], b = relS_[k], c = relS_[k < kBins - 1 ? k + 1 : kBins - 2];
            s[k] = 0.25f * a + 0.5f * b + 0.25f * c;
        } else s[k] = relS_[k];
    }
    for (int k = 0; k < kBins; ++k) {
        // Reduction in dB, scaled by how sure the bin is noise: full Reduction at s = 0, none at s = 1
        float lg = redK * (1.0f - s[k]);
        if (t.omlsa && t.presence) lg += s[k] * logf(gRel_[k] > 1e-6f ? gRel_[k] : 1e-6f);
        gain_[k] = expf(lg);
    }
    if (t.mask) maskFloor(P, gain_);
}

void ShhDSP::process(const float *inL, const float *inR, float *outL, float *outR) {
    const int N = kN, M = kHop;
    memmove(histL_, histL_ + M, sizeof(float) * (N - M));
    memmove(histR_, histR_ + M, sizeof(float) * (N - M));
    memcpy(histL_ + N - M, inL, sizeof(float) * M);
    memcpy(histR_ + N - M, inR, sizeof(float) * M);

    for (int n = 0; n < N; ++n) { re_[n] = histL_[n] * wa_[n]; im_[n] = histR_[n] * wa_[n]; }
    fft(re_, im_);

    // linked power per bin: (|L|^2 + |R|^2) / 2, from the packed spectrum Z = L + iR
    float P[kBins];
    for (int k = 0; k < kBins; ++k) {
        const int m = (N - k) & (N - 1);
        P[k] = (re_[k] * re_[k] + im_[k] * im_[k] + re_[m] * re_[m] + im_[m] * im_[m]) * 0.25f * norm_;
    }

    // Learn
    if (learning_) {
        for (int k = 0; k < kBins; ++k) subAcc_[k] += P[k];
        if (++subHop_ == kSubHops) {
            for (int k = 0; k < kBins; ++k) { subMean_[subsDone_][k] = subAcc_[k] * (1.0f / kSubHops); subAcc_[k] = 0; }
            ++subsDone_;
            subHop_ = 0;
            estimateProfile(previewPow_);
            previewValid_ = true;
            if (subsDone_ >= kSubs) finishLearn(true);
        }
    }

    // spectrum preview (input): 23 ms average per column (refreshed every 8 hops), rising at once, falling 65 dB/s
    for (int j = 0; j < kBands; ++j) {
        float p;
        if (bandK1_[j] < 0) {
            const int a = bandK0_[j];
            p = P[a] * (1 - bandW_[j]) + P[a + 1 < kBins ? a + 1 : a] * bandW_[j];
        } else {   // mean power of the column's bins: noise reads at its true level, against the nodes
            float s = 0;
            for (int k = bandK0_[j]; k < bandK1_[j]; ++k) s += P[k];
            p = s / (bandK1_[j] - bandK0_[j]);
        }
        bandPeak_[j] += p;   // summed over the 8 hops of a display step (an average, not a peak: no noise spikes)
    }
    if (++dispHop_ >= 8) {
        dispHop_ = 0;
        for (int j = 0; j < kBands; ++j) {
            const float l = powToDb(bandPeak_[j] * 0.125f);
            const float fall = dispDb_[j] - 1.5f;
            dispDb_[j] = fclamp(l > fall ? l : fall, kGridBotDb, kGridTopDb);
            bandPeak_[j] = 0;
        }
    }

    // parameter glides (~10 ms)
    bool moving = curveDirty_;
    if (fabsf(thrT_ - thrS_) > 1e-3f) { thrS_ += (thrT_ - thrS_) * kGlide; moving = true; }
    else if (thrS_ != thrT_) { thrS_ = thrT_; moving = true; }
    for (int i = 0; i < kNodes; ++i) {
        if (fabsf(offT_[i] - offS_[i]) > 1e-3f) { offS_[i] += (offT_[i] - offS_[i]) * kGlide; moving = true; }
        else if (offS_[i] != offT_[i]) { offS_[i] = offT_[i]; moving = true; }
    }
    if (moving) { computeThresholdCurve(); curveDirty_ = false; }
    redS_ += (redT_ - redS_) * kGlide;
    if (fabsf(redT_ - redS_) < 1e-4f) redS_ = redT_;
    const float redK = -0.115129255f * redS_ * tuning.redScale;   // ln(10)/20 * -Reduction

    if (tuning.track && (learned_ || tuning.track == 2) && !learning_) trackNoise(P);
    gainStage(P, redK);

    // apply (same real gain to Z[k] and Z[N-k] keeps L and R separable), inverse FFT
    re_[0] *= gain_[0]; im_[0] *= gain_[0];
    re_[N / 2] *= gain_[N / 2]; im_[N / 2] *= gain_[N / 2];
    for (int k = 1; k < N / 2; ++k) {
        const float g = gain_[k];
        re_[k] *= g; im_[k] *= g;
        re_[N - k] *= g; im_[N - k] *= g;
    }
    fft(im_, re_);   // swapped = inverse (unscaled)

    // overlap-add the last 2*M samples; out = frame samples [N-2M, N-M) = the input delayed by M
    const float inv = 1.0f / N;
    float aP = aPass_, aD = aDiff_, og = outS_;
    const float ogT = outT_;
    const float tP = bypass_ ? 1.0f : 0.0f, tD = diff_ ? 1.0f : 0.0f;
    bool bad = false;
    for (int i = 0; i < M; ++i) {
        const int n = N - 2 * M + i, n2 = N - M + i;
        const float wl = re_[n] * ws_[n] * inv + olaL_[i];
        const float wr = im_[n] * ws_[n] * inv + olaR_[i];
        olaL_[i] = re_[n2] * ws_[n2] * inv;
        olaR_[i] = im_[n2] * ws_[n2] * inv;
        const float dl = histL_[n], dr = histR_[n];
        aP = aP < tP ? (aP + kXfade < tP ? aP + kXfade : tP) : (aP - kXfade > tP ? aP - kXfade : tP);
        aD = aD < tD ? (aD + kXfade < tD ? aD + kXfade : tD) : (aD - kXfade > tD ? aD - kXfade : tD);
        const float sD = aD * aD * (3.0f - 2.0f * aD), sP = aP * aP * (3.0f - 2.0f * aP);   // S-curve fades: no corners
        const float pl = wl + sD * (dl - 2.0f * wl), pr = wr + sD * (dr - 2.0f * wr);   // (1-d)*wet + d*(dry-wet)
        og += (ogT - og) * 0.00226f;   // ~10 ms glide (1 - exp(-1 / 441))
        outL[i] = og * pl + sP * (dl - og * pl);   // Output acts on the processed sound; Pass stays untouched
        outR[i] = og * pr + sP * (dr - og * pr);
        if (!finitef(wl) || !finitef(wr)) bad = true;
    }
    aPass_ = aP;
    outS_ = fabsf(ogT - og) < 1e-6f ? ogT : og;
    aDiff_ = aD;

    if (bad) {   // never seen in testing; if anything non-finite ever got in, pass the dry signal and restart cleanly
        for (int i = 0; i < M; ++i) { outL[i] = histL_[N - 2 * M + i]; outR[i] = histR_[N - 2 * M + i]; }
        float keepL[kN], keepR[kN];
        memcpy(keepL, histL_, sizeof keepL);
        memcpy(keepR, histR_, sizeof keepR);
        clearState();
        memcpy(histL_, keepL, sizeof keepL);
        memcpy(histR_, keepR, sizeof keepR);
        for (int k = 0; k < kBins; ++k) if (!finitef(profPow_[k]) || profPow_[k] < kMinPow) profPow_[k] = kMinPow;
        ++resets;
    }
}

}  // namespace shh
