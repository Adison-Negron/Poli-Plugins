#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    // LP and HP at the same corner share every term but the numerator: compute both at once
    static void designPair(float fc, float fs, Biquad &lp, Biquad &hp, float q = 0.70710678f) {
        float omega = 6.2831853f * fc / fs;
        float alpha = sinf(omega) / (2.0f * q);
        float cosw = cosf(omega);
        float a0_inv = 1.0f / (1.0f + alpha);
        float a1 = (-2.0f * cosw) * a0_inv, a2 = (1.0f - alpha) * a0_inv;
        lp.b0 = ((1.0f - cosw) * 0.5f) * a0_inv; lp.b1 = (1.0f - cosw) * a0_inv; lp.b2 = lp.b0;
        hp.b0 = ((1.0f + cosw) * 0.5f) * a0_inv; hp.b1 = -(1.0f + cosw) * a0_inv; hp.b2 = hp.b0;
        lp.a1 = hp.a1 = a1; lp.a2 = hp.a2 = a2;
    }
    void copyCoefs(const Biquad &o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    inline float process(float in) noexcept {
        float out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return out;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }
    // After a long fade the states shrink toward subnormal floats, which are very slow on some CPUs; nothing that
    // small is audible in int16, so snap it to zero (called once per block).
    void flushTiny() noexcept { if (fabsf(z1) < 1e-15f) z1 = 0.0f; if (fabsf(z2) < 1e-15f) z2 = 0.0f; }
};

// Every setting the DSP needs, in real units (engine.cpp converts the knob positions).
struct DeEsserSettings {
    float threshDb  = -20.0f;   // -40..0
    float freqHz    = 5000.0f;  // crossover, 2k..10k
    float intensity = 0.5f;     // 0..1 (knob position; mapped to a compression strength below)
    bool  listen    = false;    // Diff: output only what is removed
    float attackMs  = 0.8f;     // 0.1..10
    float releaseMs = 35.0f;    // 5..500
    float kneeDb    = 0.0f;     // 0..12 (0 = hard knee)
    float mix       = 1.0f;     // 0..1: how much of the reduction is applied (parallel de-essing)
};

class DeEsserEngine {
public:
    // Gain reduction that reads as a full meter (the brown bar reaches the bottom).
    static constexpr float kMeterRangeDb = 24.0f;

    // Intensity knob (0..1) -> compression strength s: reduction = s x (dB over the threshold).
    // s = 1 - 1/ratio below 1 (s 0.5 = 2:1, 0.9375 = 16:1), s = 1 a limiter, s > 1 pushes sibilants BELOW the threshold.
    // 1.4 (1 - (1 - x)^1.6): gentle start, 50 % ~ the 1.0.4 sound (0.93), 100 % = 1.4 (about 1.5x the old maximum).
    static float intensityToStrength(float x) noexcept {
        x = std::max(0.0f, std::min(1.0f, x));
        return 1.4f * (1.0f - powf(1.0f - x, 1.6f));
    }

    void init(float sampleRate);
    // Targets: the engine glides to them (no zipper noise or clicks when a knob or Q-Link moves).
    void setParameters(const DeEsserSettings &s);
    void process(float** inputs, float** outputs, int numSamples) noexcept;

    // 0 = no reduction, 1 = kMeterRangeDb or more of reduction (fast attack, slow fall). Includes Mix.
    float getReduction() const noexcept { return m_smoothedReduction; }

private:
    void retune(float freqHz) noexcept;

    float m_fs = 44100.0f;

    // targets (set from the parameters) and the values actually in use (gliding toward them)
    float m_threshDbT = -20.0f, m_threshDb = -20.0f;
    float m_strengthT = 0.93f,  m_strength = 0.93f;
    float m_kneeT = 0.0f,       m_knee = 0.0f;
    float m_mixT = 1.0f,        m_mix = 1.0f;
    float m_freqT = 5000.0f,    m_freq = 5000.0f, m_freqTuned = -1.0f;
    float m_listenT = 0.0f,     m_listenMix = 0.0f;   // 0 = normal output, 1 = Diff (what is removed)
    float m_glide = 0.0f;       // per-sample one-pole coefficient (~10 ms)
    float m_mixStep = 0.0f;     // per-sample step of the Diff crossfade (~10 ms)

    Biquad lp1L, lp2L, hp1L, hp2L;
    Biquad lp1R, lp2R, hp1R, hp2R;

    float env = 0.0f;
    float attCoef = 0.0f;
    float relCoef = 0.0f;

    // Meter tracking
    float m_smoothedReduction = 0.0f;
    float m_meterFall = 0.0f;   // per-block multiplier, set in init()
};
