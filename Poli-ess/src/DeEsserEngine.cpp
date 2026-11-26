#include "DeEsserEngine.h"

static const int kSub = 16;   // samples between filter re-tunes while Freq glides

static float timeCoef(float ms, float fs) { return expf(-1.0f / (std::max(0.01f, ms) * 0.001f * fs)); }

void DeEsserEngine::init(float sampleRate) {
    m_fs = (sampleRate > 1000.0f) ? sampleRate : 44100.0f;
    m_glide = expf(-1.0f / (0.010f * m_fs));             // knob moves glide over ~10 ms
    m_mixStep = 1.0f / (0.010f * m_fs);                  // Diff crossfades over 10 ms
    // meter falls back with a ~300 ms time constant, evaluated once per 128-frame block
    m_meterFall = expf(-128.0f / (0.300f * m_fs));

    lp1L.reset(); lp2L.reset(); hp1L.reset(); hp2L.reset();
    lp1R.reset(); lp2R.reset(); hp1R.reset(); hp2R.reset();
    env = 0.0f;
    m_smoothedReduction = 0.0f;

    setParameters(DeEsserSettings());
    // start exactly on the targets: no glide from arbitrary values at creation
    m_threshDb = m_threshDbT; m_strength = m_strengthT; m_knee = m_kneeT; m_mix = m_mixT;
    m_freq = m_freqT; m_listenMix = m_listenT;
    m_freqTuned = -1.0f;
    retune(m_freq);
}

void DeEsserEngine::setParameters(const DeEsserSettings &s) {
    m_threshDbT = std::max(-40.0f, std::min(0.0f, s.threshDb));
    m_freqT = std::max(2000.0f, std::min(10000.0f, s.freqHz));
    m_strengthT = intensityToStrength(s.intensity);
    m_kneeT = std::max(0.0f, std::min(12.0f, s.kneeDb));
    m_mixT = std::max(0.0f, std::min(1.0f, s.mix));
    m_listenT = s.listen ? 1.0f : 0.0f;
    // the envelope follower's own times: a jump in a time constant can't click (the envelope itself is continuous)
    attCoef = timeCoef(std::max(0.1f, std::min(10.0f, s.attackMs)), m_fs);
    relCoef = timeCoef(std::max(5.0f, std::min(500.0f, s.releaseMs)), m_fs);
}

void DeEsserEngine::retune(float freqHz) noexcept {
    // skip when the change is far below anything audible (0.01 %)
    if (m_freqTuned > 0.0f && fabsf(freqHz - m_freqTuned) < m_freqTuned * 1e-4f) return;
    m_freqTuned = freqHz;
    Biquad::designPair(freqHz, m_fs, lp1L, hp1L);
    lp2L.copyCoefs(lp1L); hp2L.copyCoefs(hp1L);
    lp1R.copyCoefs(lp1L); lp2R.copyCoefs(lp1L);
    hp1R.copyCoefs(hp1L); hp2R.copyCoefs(hp1L);
}

void DeEsserEngine::process(float** inputs, float** outputs, int numSamples) noexcept {
    const float* inL = inputs[0];
    const float* inR = inputs[1];
    float* outL = outputs[0];
    float* outR = outputs[1];

    const float aCoef = attCoef;
    const float rCoef = relCoef;
    const float g = m_glide;

    float currentEnv = env;
    float minGain = 1.0f;   // deepest gain applied to the high band in this block (after Mix), for the meter

    for (int start = 0; start < numSamples; start += kSub) {
        const int end = std::min(numSamples, start + kSub);

        // Freq glides in sub-blocks (re-tuning the crossover costs a sin/cos), everything else per sample
        if (m_freq != m_freqT) {
            m_freq = m_freqT + (m_freq - m_freqT) * powf(g, (float)(end - start));
            if (fabsf(m_freq - m_freqT) < 0.01f) m_freq = m_freqT;
            retune(m_freq);
        }

        for (int i = start; i < end; ++i) {
            m_threshDb = m_threshDbT + (m_threshDb - m_threshDbT) * g;
            m_strength = m_strengthT + (m_strength - m_strengthT) * g;
            m_knee     = m_kneeT     + (m_knee     - m_kneeT)     * g;
            m_mix      = m_mixT      + (m_mix      - m_mixT)      * g;
            if (m_listenMix < m_listenT) m_listenMix = std::min(m_listenT, m_listenMix + m_mixStep);
            else if (m_listenMix > m_listenT) m_listenMix = std::max(m_listenT, m_listenMix - m_mixStep);

            float xL = inL[i];
            float xR = inR[i];

            float lowL  = lp2L.process(lp1L.process(xL));
            float highL = hp2L.process(hp1L.process(xL));
            float lowR  = lp2R.process(lp1R.process(xR));
            float highR = hp2R.process(hp1R.process(xR));

            float peak = std::max(fabsf(highL), fabsf(highR)) + 1e-25f;

            currentEnv = (peak > currentEnv)
                ? (aCoef * currentEnv + (1.0f - aCoef) * peak)
                : (rCoef * currentEnv + (1.0f - rCoef) * peak);

            // gain computer in dB (soft knee: quadratic over the knee width, centred on the threshold)
            const float over = 20.0f * log10f(currentEnv) - m_threshDb;
            const float w = m_knee;
            float redDb = 0.0f;
            if (w > 0.01f) {
                if (over >= 0.5f * w) redDb = m_strength * over;
                else if (over > -0.5f * w) { const float t = over + 0.5f * w; redDb = m_strength * t * t / (2.0f * w); }
            } else if (over > 0.0f) {
                redDb = m_strength * over;
            }
            float gr = (redDb > 0.0f) ? powf(10.0f, -0.05f * redDb) : 1.0f;
            // Mix: how much of that reduction is applied. Done on the high band's gain, so the dry part stays
            // phase-aligned with the crossover (mixing the raw input back in would comb-filter around Freq).
            gr = 1.0f - m_mix + m_mix * gr;
            minGain = std::min(minGain, gr);

            float compHighL = highL * gr;
            float compHighR = highR * gr;

            // normal = low + reduced high; Diff = what was removed. Crossfaded so switching never clicks.
            const float m = m_listenMix;
            outL[i] = (1.0f - m) * (lowL + compHighL) + m * (highL - compHighL);
            outR[i] = (1.0f - m) * (lowR + compHighR) + m * (highR - compHighR);
        }
    }

    env = currentEnv;
    lp1L.flushTiny(); lp2L.flushTiny(); hp1L.flushTiny(); hp2L.flushTiny();
    lp1R.flushTiny(); lp2R.flushTiny(); hp1R.flushTiny(); hp2R.flushTiny();

    // Meter: reduction in dB scaled to 0..1, instant rise, smooth fall.
    float grDb = -20.0f * log10f(std::max(minGain, 1e-6f));
    float target = std::min(1.0f, grDb / kMeterRangeDb);
    m_smoothedReduction = (target > m_smoothedReduction)
        ? target
        : m_smoothedReduction * m_meterFall;
    if (m_smoothedReduction < 1e-4f) m_smoothedReduction = 0.0f;
}
