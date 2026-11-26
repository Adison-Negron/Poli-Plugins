#include "MultibandDSP.h"
#include <cmath>
#include <algorithm>

static const float kPi = 3.14159265358979f;

static inline float dbToLin(float db) { return expf(db * 0.11512925465f); }      // ln(10)/20
static inline float linToDb(float x) { return 8.68588963807f * logf(x); }         // 20/ln(10)

void Biquad::setLowpass(float fc, float fsr) {
    const float w = 2.0f * kPi * fc / fsr;
    const float c = cosf(w), s = sinf(w);
    const float alpha = s / (2.0f * 0.70710678f);
    const float a0 = 1.0f + alpha;
    b0 = (1.0f - c) * 0.5f / a0;
    b1 = (1.0f - c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
}

void Biquad::setHighpass(float fc, float fsr) {
    const float w = 2.0f * kPi * fc / fsr;
    const float c = cosf(w), s = sinf(w);
    const float alpha = s / (2.0f * 0.70710678f);
    const float a0 = 1.0f + alpha;
    b0 = (1.0f + c) * 0.5f / a0;
    b1 = -(1.0f + c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
}

void MultibandDSP::init(float sampleRate) {
    fs = sampleRate;
    for (int b = 0; b < kBands; ++b) st[b] = BandState();
    limG = 1.0f;
    cx1 = tx1;
    cx2 = tx2;
    updateFilters(true);
}

void MultibandDSP::updateFilters(bool force) {
    // glide the crossovers in the log domain (about 20 ms) so a knob turn doesn't click
    if (!force && cx1 == tx1 && cx2 == tx2) return;
    const float k = 0.35f;   // per 128-frame block
    const float d1 = logf(tx1) - logf(cx1), d2 = logf(tx2) - logf(cx2);
    cx1 = (force || fabsf(d1) < 0.002f) ? tx1 : expf(logf(cx1) + k * d1);
    cx2 = (force || fabsf(d2) < 0.002f) ? tx2 : expf(logf(cx2) + k * d2);
    for (int c = 0; c < 2; ++c) {
        lp1[c].setLowpass(cx1, fs);  hp1[c].setHighpass(cx1, fs);
        lp2[c].setLowpass(cx2, fs);  hp2[c].setHighpass(cx2, fs);
        apLp[c].setLowpass(cx2, fs); apHp[c].setHighpass(cx2, fs);
    }
}

// the upward stage's floor (band envelope, dBFS): quieter than this is room noise, hiss or monitor bleed, never lifted
static const float kUpFloorDb = -62.0f, kUpFloorFadeDb = 12.0f;

// static curve: gain in dB for an envelope level L (dB)
static inline float gainComputer(float L, const BandSettings &s, float *downOut = nullptr, float *upOut = nullptr) {
    const float W = s.kneeDb;
    float g = 0.0f;
    // downward, above Thresh
    const float slopeD = 1.0f / s.ratio - 1.0f;   // <= 0
    const float dd = L - s.threshDb;
    if (W > 0.01f && fabsf(dd) <= 0.5f * W) {
        const float t = dd + 0.5f * W;
        g += slopeD * t * t / (2.0f * W);
    } else if (dd > 0.0f) {
        g += slopeD * dd;
    }
    // upward, below the upward Thresh
    const float slopeU = 1.0f - 1.0f / s.upRatio;   // >= 0
    const float du = s.upThreshDb - L;
    float gu = 0.0f;
    if (W > 0.01f && fabsf(du) <= 0.5f * W) {
        const float t = du + 0.5f * W;
        gu = slopeU * t * t / (2.0f * W);
    } else if (du > 0.0f) {
        gu = slopeU * du;
    }
    // Upward boost only for real signal: none below kUpFloorDb, full from kUpFloorDb + kUpFloorFadeDb, never over +24 dB.
    // 1.2.1 faded it out only between -75 and -90 dB, so a live mic's room noise and the speakers / headphones bleeding
    // back into it (-60..-75 dB in a band) got up to +24 dB: a feedback loop on the device while talking into the mic,
    // stronger the higher the upward ratio (user report 2026-11-05; test 11b reproduces it: the loop grew up to +32 dB).
    if (L < kUpFloorDb + kUpFloorFadeDb) gu *= std::max(0.0f, (L - kUpFloorDb) / kUpFloorFadeDb);
    gu = std::min(gu, 24.0f);
    if (downOut) { *downOut = g; *upOut = gu; }
    return g + gu;
}

void MultibandDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n) {
    updateFilters(false);

    float coefA[kBands], coefR[kBands], gInT[kBands], gOutT[kBands];
    for (int b = 0; b < kBands; ++b) {
        const BandSettings &s = bands[b];
        coefA[b] = expf(-1.0f / (std::max(0.05f, s.attackMs) * 0.001f * fs));
        coefR[b] = expf(-1.0f / (std::max(1.0f, s.releaseMs) * 0.001f * fs));
        gInT[b] = dbToLin(s.inputDb);
        gOutT[b] = dbToLin(s.outputDb + s.upOutputDb);
    }
    const float gs = 1.0f - expf(-1.0f / (0.01f * fs));   // 10 ms glide for the static gains
    const float wetT = bypass ? 0.0f : 1.0f;
    const float wetStep = 1.0f / (0.01f * fs);
    const float limRel = 1.0f - expf(-1.0f / (0.05f * fs));

    for (int i = 0; i < n; ++i) {
        const float dryL = inL[i], dryR = inR[i];
        float band[kBands][2];
        for (int c = 0; c < 2; ++c) {
            const float x = c ? dryR : dryL;
            const float lo = lp1[c].process(x);
            const float rest = hp1[c].process(x);
            band[1][c] = lp2[c].process(rest);
            band[2][c] = hp2[c].process(rest);
            band[0][c] = apLp[c].process(lo) + apHp[c].process(lo);   // LR4 allpass at X2: keeps the sum flat
        }
        float yL = 0.0f, yR = 0.0f;
        for (int b = 0; b < kBands; ++b) {
            BandState &t = st[b];
            t.gIn += gs * (gInT[b] - t.gIn);
            t.gOut += gs * (gOutT[b] - t.gOut);
            const float l = band[b][0] * t.gIn, r = band[b][1] * t.gIn;
            const float pk = std::max(fabsf(l), fabsf(r));
            t.env = pk > t.env ? coefA[b] * t.env + (1.0f - coefA[b]) * pk
                               : coefR[b] * t.env + (1.0f - coefR[b]) * pk;
            const float L = linToDb(t.env + 1e-9f);
#ifdef MB_STATS   // offline preset tuning (test/preset_tool.cpp): the gain computer's two stages, per sample
            const float g = dbToLin(gainComputer(L, bands[b], &t.lastDown, &t.lastUp)) * t.gOut;
            t.lastLevel = L;
#else
            const float g = dbToLin(gainComputer(L, bands[b])) * t.gOut;
#endif
            const float ol = l * g, orr = r * g;
            t.meterIn = std::max(t.meterIn, pk);
            t.meterOut = std::max(t.meterOut, std::max(fabsf(ol), fabsf(orr)));
            yL += ol;
            yR += orr;
        }
        // safety ceiling: the output is 16-bit, so anything over 0 dBFS would clip. A peak limiter at -0.1 dBFS
        // (instant attack, 50 ms release) catches what makeup gain and slow attacks let through; it never acts on a
        // signal that stays under the ceiling, and the dry (bypassed) path doesn't go through it.
        {
            const float pk = std::max(fabsf(yL), fabsf(yR));
            limG += limRel * (1.0f - limG);              // release first, then check (no overshoot)
            if (limG > 0.9999f) limG = 1.0f;             // land on exactly 1 (float steps stall just below it)
            if (pk * limG > kCeiling) limG = kCeiling / pk;
            yL *= limG;
            yR *= limG;
        }
        if (wet != wetT) wet = wetT > wet ? std::min(wetT, wet + wetStep) : std::max(wetT, wet - wetStep);
        outL[i] = dryL + wet * (yL - dryL);
        outR[i] = dryR + wet * (yR - dryR);
    }
    // keep the envelopes out of denormals when the input goes silent
    for (int b = 0; b < kBands; ++b)
        if (st[b].env < 1e-12f) st[b].env = 0.0f;
}

void MultibandDSP::takeMeter(int b, float &in, float &out) {
    if (b < 0 || b >= kBands) { in = out = 0.0f; return; }
    in = st[b].meterIn;
    out = st[b].meterOut;
    st[b].meterIn = st[b].meterOut = 0.0f;
}
