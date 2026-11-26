// Poli-verb DSP: a stereo reverb = pre-delay -> early reflections (with a probability gate) + diffused 8-line feedback
// delay network (late tail) -> wet EQ -> width -> mix. Everything the knobs do is here; engine.cpp only maps strings
// to these setters and formats the readouts.
//
// Knob positions are 0..1000 everywhere (the MPC knob travel); the mapping to physical units (dB, ms, s, %) lives in
// VerbMap below, so the engine's value text, the DSP and the tests read the same numbers.
#pragma once
#include <cmath>
#include <cstdint>

namespace verb {

// ---- parameters ------------------------------------------------------------------------------------------------
enum Pid {
    P_MIX, P_INPUT, P_OUTPUT, P_DRIVE, P_LOW, P_TILT, P_HIGH,                    // General
    P_SIZE, P_PREDELAY, P_WIDTH, P_ERLVL, P_ERPROB,                              // Space
    P_DIFFUSION, P_DAMPING, P_MODULATION, P_DENSITY, P_SPREAD, P_BASSMULT,       // Tail
    P_ATTACK, P_RELEASE, P_DECAY,
    P_COUNT
};

static const int kNumTypes = 14;
static const int kNumDivs = 12;   // tempo-synced pre-delay note values

// the knob position (0..1000) of every parameter's default (= the "Init" preset)
static const float kDefaultPos[P_COUNT] = {
    300, 500, 500, 0, 500, 500, 500,          // Mix 30 %, Input/Output 0 dB, Drive 0, EQ flat
    500, 120, 500, 600, 1000,                 // Size 50 %, Pre-Delay, Width 100 %, ER Lvl 60 %, Probability 100 %
    800, 500, 300, 600, 700, 500,             // Diffusion, Damping, Modulation, Density, Spread, Bass Mult 1.0x
    0, 1000, 500                              // Attack 0, Release inf (natural), Decay
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- position <-> physical value -------------------------------------------------------------------------------------
struct VerbMap {
    static float pct(float p) { return p * 0.1f; }                                   // 0..100 %
    static float db24(float p) { return (p - 500.0f) * (24.0f / 500.0f); }          // Input / Output -24..+24 dB
    static float db12(float p) { return (p - 500.0f) * (12.0f / 500.0f); }          // Low / High
    static float db6(float p) { return (p - 500.0f) * (6.0f / 500.0f); }            // Tilt
    static float widthPct(float p) { return p * 0.2f; }                               // 0..200 %
    static float predelayMs(float p) { float x = p * 0.001f; return 500.0f * x * x; }   // 0..500 ms, fine at the short end
    static float posOfPredelayMs(float ms) { return 1000.0f * sqrtf(clampf(ms, 0, 500) / 500.0f); }
    static float bassMult(float p) { return 0.25f * powf(16.0f, p * 0.001f); }       // 0.25x .. 4x, 1.0x at 500
    static float attackMs(float p) { float x = p * 0.001f; return 1500.0f * x * x; } // 0..1500 ms
    static bool releaseInf(float p) { return p >= 995.0f; }
    static float releaseS(float p) { return 0.02f * powf(500.0f, p * 0.001f / 0.995f); }   // 20 ms .. 10 s, then "inf"
    static float decayS(float p) { return 0.2f * powf(150.0f, p * 0.001f); }          // 0.2 s .. 30 s
    static float posOfDecayS(float s) { return 1000.0f * logf(clampf(s, 0.2f, 30.0f) / 0.2f) / logf(150.0f); }
    static float posOfReleaseS(float s) { return 995.0f * logf(clampf(s, 0.02f, 10.0f) / 0.02f) / logf(500.0f); }
    static float posOfBassMult(float m) { return 1000.0f * logf(clampf(m, 0.25f, 4.0f) / 0.25f) / logf(16.0f); }
    static int divIndex(float p) { return (int)lrintf(clampf(p, 0, 1000) * (kNumDivs - 1) / 1000.0f); }
};

// tempo-synced pre-delay: note value in beats (a beat = a quarter note), the label the knob shows
static const float kDivBeats[kNumDivs] = {0.125f, 1.0f / 6, 0.25f, 1.0f / 3, 0.375f, 0.5f, 2.0f / 3, 0.75f, 1.0f, 4.0f / 3, 1.5f, 2.0f};
static const char *const kDivName[kNumDivs] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"};

// ---- the 14 spaces ---------------------------------------------------------------------------------------------------
// Each space is a voicing: how big it is, what its early reflections look like, how fast it decays and how dark it is.
// The knobs (Size, Decay, Damping, ...) scale and offset these, so every space still answers every knob. Decay is the
// RT60 itself (what the knob says is what you get) in every space.
struct Space {
    const char *name;
    float sizeMul;      // scales the late field's delay lines and the reflection times
    float erMin, erMax; // reflection times at sizeMul 1, Size knob mid (ms)
    int erN;            // reflections per channel
    float erTau;        // reflection level falls by 1/e every erTau ms
    float erEven;       // 0 = random spacing, 1 = evenly spaced (tunnel flutter)
    float erBright;     // reflection low-pass (Hz)
    float dampBias, bassBias, diffBias, modBias, densBias;   // added to the knobs (positions / 1000), bassBias is x
    int spring;         // 1: dispersive spring in the loop
    float lateLevel;    // the tail's level for this space (dB)
    uint32_t seed;
};
extern const Space kSpaces[kNumTypes];

class VerbDSP {
public:
    static const int kN = 8;          // delay lines in the late field
    static const int kMaxEr = 28;     // reflections per channel
    static const int kBlock = 128;

    VerbDSP();
    ~VerbDSP();
    void init(float sampleRate);      // also clears every buffer
    void clear();

    void setParam(int id, float pos);             // 0..1000
    void setType(int t);                          // glides (a short wet dip), then clears the old tail
    void setFreeze(bool on);
    void setSync(bool on);
    void setWetEnable(bool l, bool r);
    void setTempo(double bpm);                    // host tempo for Sync; 0 = unknown (120 is used)

    // one block of up to kBlock frames (float -1..1 in/out; in and out may not alias)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);

    // ---- read-outs for the engine and the tests ----
    float effectiveSize() const;                  // Size knob x the space's size, ~0.1 .. 9
    int roomClass() const;                        // 0 S, 1 M, 2 L, 3 XL, 4 XXL
    float decaySeconds() const;                   // the RT60 the loop is tuned to (before Bass Mult / Damping)
    float predelaySamples() const;                // what the pre-delay is right now (after sync)
    float erSpanMs() const;                       // reflection pattern length now (after Size / sync)
    int type() const { return type_; }
    bool frozen() const { return freeze_; }

private:
    struct Impl;
    Impl *d;
    int type_ = 0;
    bool freeze_ = false;
    VerbDSP(const VerbDSP &);
    VerbDSP &operator=(const VerbDSP &);
    float pos_[P_COUNT];
    bool sync_ = false, wetL_ = true, wetR_ = true;
    double bpm_ = 0;
    float fs_ = 44100.0f;
    friend struct Impl;
};

const char *roomClassName(int c);

}  // namespace verb
