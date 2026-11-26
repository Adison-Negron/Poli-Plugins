// Poli-tea DSP: a God Particle-style master bus / mixing processor.
//
//   in -> Input gain -> Focus EQ (Low / Mid / High) -> parallel tape + tube excitation (2x oversampled)
//      -> fixed Linkwitz-Riley crossovers (120 Hz / 2.5 kHz) -> 3 program-dependent compressors (key: internal or external)
//      -> auto-makeup -> band saturation (low = tape, mid = tube) -> stereo matrix (sub-120 Hz mono lock, widened highs)
//      -> Limiter Drive / Threshold push -> oversampled soft clipper -> look-ahead brickwall limiter -> Output (= ceiling)
//
// Amount (0..200 %) scales the saturation, the compression (ratio, threshold, makeup) and the widening together.
// Input pushes the saturators and compressors harder (they sit after it); the input meter's sweet spot is where the fixed
// thresholds were tuned. Everything the audio depends on glides (10 ms one-pole), so Q-Link steps and preset jumps don't click.
//
// Knob positions are 0..1000 everywhere (MPC knob travel); TeaMap maps them to dB / % so the engine's value text, the DSP and
// the tests read the same numbers.
#pragma once
#include <cmath>
#include <cstdint>

namespace tea {

enum Pid {
    P_INPUT, P_DRIVE, P_LOW, P_MID, P_HIGH, P_AMOUNT, P_LDRIVE, P_LTHRESH, P_OUTPUT,
    P_COUNT
};

// the knob position of every parameter's default (= the "Init" preset)
static const float kDefaultPos[P_COUNT] = {
    500,    // Input 0 dB
    300,    // Drive 30 %
    500, 500, 500,   // Low / Mid / High 0 %
    500,    // Amount 100 %
    200,    // Limiter Drive 20 %
    889,    // Limiter Threshold -2 dB
    988     // Output -0.3 dB
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

struct TeaMap {
    static float inputDb(float p) { return -18.0f + p * 0.036f; }              // -18 .. +18 dB
    static float drivePct(float p) { return p * 0.1f; }                         // 0 .. 100 %
    static float focusPct(float p) { return (p - 500.0f) * 0.2f; }              // -100 .. +100 %  (Low / Mid / High)
    static float amountPct(float p) { return p * 0.2f; }                        // 0 .. 200 %
    static float ldrivePct(float p) { return p * 0.1f; }                        // 0 .. 100 %
    static float lthreshDb(float p) { return -18.0f + p * 0.018f; }             // -18 .. 0 dB
    static float outputDb(float p) { return -24.0f + p * 0.024f; }              // -24 .. 0 dB (the ceiling)
    static float posOfInputDb(float d) { return clampf((d + 18.0f) / 0.036f, 0, 1000); }
    static float posOfLthreshDb(float d) { return clampf((d + 18.0f) / 0.018f, 0, 1000); }
    static float posOfOutputDb(float d) { return clampf((d + 24.0f) / 0.024f, 0, 1000); }
};

// what the engine shows (taken once per block by the engine)
struct Meters {
    float inPeak, inRms;               // linear, after Input gain (peak = block max; rms = block power mean, sqrt)
    float grLow, grMid, grHigh, grLim; // gain reduction in dB (<= 0): the block's deepest value
};

static const float kXover1Hz = 120.0f;    // weight | body  (also the mono-lock edge)
static const float kXover2Hz = 2500.0f;   // body | air

class TeaDSP {
public:
    static const int kBlock = 128;
    static const int kHb = 8;                    // half-band half length: 4*kHb-1 = 31 taps, latency 2*kHb-1 samples
    static const int kLook = 64;                 // limiter look-ahead (1.45 ms at 44.1 kHz)

    TeaDSP();
    void init(float sampleRate);
    void reset();
    void setParam(int id, float pos);            // knob position 0..1000 (the DSP glides to it)
    void setExternalSidechain(bool on) { scTarget_ = on ? 1.0f : 0.0f; }
    bool externalSidechain() const { return scTarget_ > 0.5f; }

    // key: optional host sidechain pair. Without it, "External" keys from the raw input (before Input gain, EQ and saturation).
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, const float *keyL = nullptr, const float *keyR = nullptr);

    void takeMeters(Meters &m);                  // block maxima since the last call, then reset
    static int latencySamples() { return (2 * kHb - 1) * 2 + (kLook - 1) + kDetLag; }

    // ---- exposed for tests -------------------------------------------------------------------------------------------------
    float compRatio() const { return ratio_; }

private:
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct BqState { float z1 = 0, z2 = 0; };
    struct XoverCoef { Biquad lp1, hp1, lp2, hp2, ap; };
    struct XoverState { BqState lp1[2], hp1[2], lp2[2], hp2[2], ap; };
    struct Os2 { float up[2 * kHb]; float dn[4 * kHb - 1]; };   // oversampling history per channel
    struct Comp {
        float pw = 0, pk = 0;             // detector power (RMS-like) and peak envelope
        float gr = 0;                     // current gain reduction in dB (<= 0)
        float avgGr = 0, crest = 0;       // slow averages for the program-dependent ballistics
        float atk = 0.01f, rel = 0.001f;  // per-sample coefficients (updated every 16 samples)
        float mk = 0;                     // makeup, dB (smoothed)
        float thr = -24;                  // threshold, dB (smoothed)
    };

    float fs_ = 44100.0f;
    float pos_[P_COUNT];
    bool dirty_ = true;
    int tick_ = 0;

    // smoothed controls
    float sm_;                    // per-sample glide coefficient (10 ms)
    float gIn_, gInT_;
    float eqDb_[3], eqDbT_[3], eqDbDone_[3];
    float exM_, exMT_, exG_, exGT_;          // excitation mix and drive gain
    float amt_, amtT_;                        // Amount / 100 (0..2)
    float s2_, s2T_;                          // band saturation mix
    float wid_[3], widT_[3];                  // stereo widening of low / mid / high (low is 0: mono)
    float push_, pushT_;                      // linear gain into the limiter section
    float clipW_, clipWT_;                    // soft clipper blend
    float ceil_, ceilT_;                      // linear ceiling
    float ldr_;                               // limiter drive 0..1 (for release character)
    float scMix_, scTarget_;
    float ratio_, ratioT_;
    float thrT_[3], mkT_[3];

    Biquad eq_[3];
    BqState eqS_[2][3];
    bool eqOn_;

    XoverCoef xc_;
    XoverState xs_[2];            // L, R
    XoverState xk_;               // mono key

    float hb_[kHb];               // half-band odd taps h[1], h[3], ...
    Os2 exOs_[2], clOs_[2], detOs_[2], det2Os_[2];   // excitation, clipper, and the limiter's 4x true-peak detector (two stages)
    float dry_[2][32];            // dry delay of the excitation block (2*kHb-1 samples, ring)
    int dryPos_;
    float dcT_[2];                // tube DC blockers
    float lpT_[2];                // tape loss one-poles (2x rate)

    Comp comp_[3];
    float dcMid_[2];              // mid-band tube DC blockers

    // limiter
    static const int kDetLag = kHb + kHb / 2;     // the 4x peak detector sees the input this many samples late
    static const int kDl = kLook + kDetLag;       // audio delay ring: look-ahead + that lag
    float dlL_[kDl], dlR_[kDl];
    float mring_[kLook];
    double msum_;
    int lpos_, mpos_;
    float dqVal_[256];
    uint32_t dqIdx_[256];
    uint32_t dqHead_, dqTail_, lcount_;
    float lenv_;                  // release envelope (instant attack)
    float lAvgGr_;
    float lrel_;

    // meters
    float mPeak_, mSumSq_; int mCount_;
    float mGr_[4];

    void updateTargets();
    void retune();
    void flush();
    void designEq(int band, float dB);
    void designXover();
    static void setLP(Biquad &b, float fc, float fs, float q);
    static void setHP(Biquad &b, float fc, float fs, float q);
    static void setAP(Biquad &b, float fc, float fs, float q);
    static void setShelf(Biquad &b, bool high, float fc, float fs, float dB);
    static void setPeak(Biquad &b, float fc, float fs, float q, float dB);
    void split(const XoverCoef &c, XoverState &s, float x, float &lo, float &mid, float &hi);
};

}  // namespace tea
