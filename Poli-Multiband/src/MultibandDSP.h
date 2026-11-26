// Poli-Multiband DSP: 3-band compressor with a downward ("Comp", Ableton's "Above") and an upward ("Upwards Comp",
// Ableton's "Below") stage per band. 44.1 kHz float stereo, any block length.
//
// Crossovers: Linkwitz-Riley 4th order (two cascaded Butterworth biquads). The low band also goes through an LR4
// allpass at the upper crossover, so low + mid + high sums back to a flat (allpass) response when nothing compresses.
//   X1 = low/mid crossover  (Band 1's top edge, Band 2's bottom edge)
//   X2 = mid/high crossover (Band 2's top edge, Band 3's bottom edge)
//
// Per band, stereo-linked: input gain -> envelope (attack/release on the peak of both channels) -> static curve
// (downward above Thresh with Ratio and a soft Knee; upward below the upward Thresh with its own Ratio, the same knee,
// at most +24 dB of boost and none on silence) -> comp Output and upward Output gains.
// The three bands are summed and go through a safety ceiling (a peak limiter at -0.1 dBFS, instant attack, 50 ms
// release) so makeup gain can't clip the 16-bit output; it does nothing to anything that stays under the ceiling.
#pragma once
#include <cstdint>

struct BandSettings {
    float inputDb = 0.0f;      // Comp: Input
    float outputDb = 0.0f;     // Comp: Output
    float attackMs = 3.0f;     // Comp: Attack (shared by both stages: one envelope)
    float releaseMs = 100.0f;  // Comp: Release
    float kneeDb = 0.0f;       // Comp: Knee (both stages)
    float ratio = 1.0f;        // Comp: Ratio (downward, >= 1)
    float threshDb = 0.0f;     // Comp: Thresh
    float upRatio = 1.0f;      // Upwards Comp: Ratio (>= 1)
    float upThreshDb = -40.0f; // Upwards Comp: Thresh (always <= threshDb)
    float upOutputDb = 0.0f;   // Upwards Comp: Output
};

class Biquad {
public:
    void setLowpass(float fc, float fs);
    void setHighpass(float fc, float fs);
    void reset() { z1 = z2 = 0.0f; }
    inline float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
private:
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
};

// One Linkwitz-Riley 4th order section (two identical Butterworth biquads)
struct LR4 {
    Biquad s1, s2;
    void setLowpass(float fc, float fs) { s1.setLowpass(fc, fs); s2.setLowpass(fc, fs); }
    void setHighpass(float fc, float fs) { s1.setHighpass(fc, fs); s2.setHighpass(fc, fs); }
    inline float process(float x) { return s2.process(s1.process(x)); }
};

struct BandState {
    float env = 0.0f;          // linear peak envelope
    float gIn = 1.0f, gOut = 1.0f;   // smoothed static gains
    // meter (selected band only is shown, but every band keeps its own)
    float meterIn = 0.0f, meterOut = 0.0f;   // block peaks, linear
#ifdef MB_STATS
    float lastDown = 0.0f, lastUp = 0.0f, lastLevel = -120.0f;   // dB, last sample (offline preset tuning only)
#endif
};

class MultibandDSP {
public:
    static constexpr int kBands = 3;
    void init(float sampleRate);
    void setCrossovers(float x1, float x2) { tx1 = x1; tx2 = x2; }
    void setBand(int b, const BandSettings &s) { if (b >= 0 && b < kBands) bands[b] = s; }
    void setBypass(bool on) { bypass = on; }
    // in/out: two channels of n samples each (out may alias in)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);
    // peaks since the last call, linear (meter source), for one band
    void takeMeter(int b, float &in, float &out);
#ifdef MB_STATS
    const BandState &state(int b) const { return st[b]; }
    float limitGain() const { return limG; }
#endif

private:
    void updateFilters(bool force);
    float fs = 44100.0f;
    float tx1 = 500.0f, tx2 = 4000.0f;   // targets
    float cx1 = 0.0f, cx2 = 0.0f;        // smoothed (what the filters use)
    bool bypass = false;
    float wet = 1.0f;                    // bypass crossfade: 1 = processed, 0 = dry
    float limG = 1.0f;                   // safety ceiling's gain (1 = not limiting)
    static constexpr float kCeiling = 0.98855f;   // -0.1 dBFS
    BandSettings bands[kBands];
    BandState st[kBands];
    // per channel: lp1/hp1 at X1, lp2/hp2 at X2 (rest -> mid/high), ap2 (low band allpass at X2)
    LR4 lp1[2], hp1[2], lp2[2], hp2[2], apLp[2], apHp[2];
};
