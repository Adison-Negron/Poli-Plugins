// Poli-Juicebox DSP: a low-latency, real-time voice shaper (pitch + formant shifting with a polish chain).
//
//   in (mono sum) -> PSOLA shifter (pitch-synchronous grains, own pitch tracker, formant = grain re-sampling)
//      -> transient preserve (dry attack blended back) -> Spectral Tamer (5-10 kHz sibilance + 80-350 Hz low end)
//      -> Harmonic Shaper (odd / even, fed from a Color-tuned band) -> Natural Formant Filter (3 resonant peaks)
//      -> Air (8 kHz shelf + breath noise that follows the voice) -> equal-power Dry / Wet mix -> soft ceiling
//
// The dry path is delayed by exactly the shifter's latency (kLatency samples), so Mix never combs.
//
// Knob positions are 0..1000 everywhere (the MPC knob travel); JuiceMap maps them to semitones / Hz / dB / % so the engine's
// value text, the DSP and the tests read the same numbers. 44.1 kHz only (the MPC's rate).
#pragma once
#include <cmath>
#include <cstdint>

namespace jb {

enum Pid {
    P_PITCH, P_FORMANT, P_FTRACK,                       // Shift
    P_THRESH, P_LOWEND, P_ATTACK, P_RELEASE,            // Spectral Tamer
    P_ODD, P_EVEN, P_COLOR,                             // Harmonic Shape
    P_F1C, P_F1Q, P_F2C, P_F2Q, P_F3C, P_F3Q,           // Natural Formant Filter
    P_FSMOOTH, P_AIR, P_NATURAL, P_MIX,                 // Smoothen
    P_HOLD, P_RASP, P_GLIDE, P_GUARD,                   // Tracking page: voice hold, rasp tolerance, pitch glide, octave guard
    P_MORPH, P_FLOOR, P_CEIL, P_OVERLAP,                // Tracking page: transition morph, lowest / highest pitch, grain overlap
    P_COUNT
};

// the knob position (0..1000) of every parameter's default (= the "Init" preset)
static const float kDefaultPos[P_COUNT] = {
    500, 500, 500,              // pitch 0 st, formant 0 st, F-Track 50 %
    1000, 0, 486, 463,          // threshold 0 dB (tamer idle), low end Off, attack 3 ms, release 40 ms
    0, 0, 537,                  // odd 0, even 0, color 1 kHz
    569, 0, 483, 0, 473, 0,     // F1 500 Hz, F2 1.7 kHz, F3 3.3 kHz, all Off
    300, 0, 400, 1000,          // F-Smooth 30 %, Air 0, Natural 40 %, Mix 100 %
    400, 500, 400, 600,         // Hold 60 ms, Rasp 50 %, Glide 40 %, Octave guard 60 %
    486, 0, 790, 300            // Morph 30 ms, Floor 70 Hz, Ceiling 700 Hz, Overlap 30 %
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

struct JuiceMap {
    static float semis(float p) { return (clampf(p, 0.0f, 1000.0f) - 500.0f) * 0.024f; }        // Pitch / Formant: -12 .. +12 st
    static float pct(float p) { return clampf(p, 0.0f, 1000.0f) * 0.1f; }                         // 0 .. 100 %
    static float thresholdDb(float p) { return clampf(p, 0.0f, 1000.0f) * 0.06f - 60.0f; }        // -60 .. 0 dB
    static bool lowEndOff(float p) { return p < 6.0f; }                                           // 80 .. 350 Hz, Off at the very left
    static float lowEndHz(float p) { return 80.0f * powf(350.0f / 80.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }
    static float attackMs(float p) { return 0.5f * powf(40.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }     // 0.5 .. 20 ms
    static float releaseMs(float p) { return 10.0f * powf(20.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }   // 10 .. 200 ms
    static float colorHz(float p) { return 200.0f * powf(20.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }    // 200 Hz .. 4 kHz
    static float f1Hz(float p) { return 200.0f * powf(5.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }        // 200 Hz .. 1 kHz
    static float f2Hz(float p) { return 1000.0f * powf(3.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }       // 1 .. 3 kHz
    static float f3Hz(float p) { return 2500.0f * powf(1.8f, clampf(p, 0.0f, 1000.0f) * 0.001f); }       // 2.5 .. 4.5 kHz
    static bool qOff(float p) { return p < 6.0f; }                                                // Q 0.7 .. 14, Off at the very left
    static float q(float p) { return 0.7f * powf(20.0f, (clampf(p, 6.0f, 1000.0f) - 6.0f) / 994.0f); }
    static float holdMs(float p) { return clampf(p, 0.0f, 1000.0f) * 0.15f; }                       // 0 .. 150 ms
    static float morphMs(float p) { return 5.0f * powf(40.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }  // 5 .. 200 ms
    static float floorHz(float p) { return 70.0f * powf(200.0f / 70.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); }   // 70 .. 200 Hz
    static float ceilHz(float p) { return 250.0f * powf(920.0f / 250.0f, clampf(p, 0.0f, 1000.0f) * 0.001f); } // 250 .. 920 Hz
    static float qGainDb(float p) { return 6.0f * clampf(p / 40.0f, 0.0f, 1.0f); }                // the peak's gain fades in over the first 4 %
};

class JuiceDSP {
public:
    // The shifter's look-ahead, in samples at 44.1 kHz; the dry path is delayed to match. Three modes: Fast 384 (8.7 ms), Balanced 512 (11.6 ms,
    // the default), Smooth 768 (17.4 ms: the grain length of deep voices / big formant moves has the most room). kLatency is the largest.
    static const int kLatency = 768;
    static const int kLatencyModes = 3, kLatencyDefault = 1;
    static int latencyFor(int mode) { return mode <= 0 ? 384 : mode == 1 ? 512 : 768; }

    JuiceDSP();
    ~JuiceDSP();
    void init(float sampleRate);                         // also clears every state
    void clear();

    void setParam(int id, float pos);                    // 0..1000
    void setLatencyMode(int mode);                       // 0 Fast, 1 Balanced, 2 Smooth: a short gap in the output when it changes
    int latencySamples() const;                          // the look-ahead now in force
    // Learn: listens to the input (the tracker runs wide open), ~6 s of voice at most; learnFinished() turns true when it has heard enough
    // (the caller then reads learnResult and calls stopLearn). learnResult fills pos[k] for every knob k with set[k] true; false = too little voice.
    void startLearn();
    void stopLearn();
    bool learning() const;
    float learnProgress() const;                         // 0..1
    bool learnFinished();
    bool learnResult(float *pos, bool *set) const;
    void setLink(bool on);                               // Pitch / Formant link: the formant follows the pitch by F-Track

    // any number of frames (float -1..1 in/out; in and out may not alias)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);

    // ---- read-outs for the tests / the UI ----
    float pitchHz() const;                               // the tracker's current input pitch (0 = unvoiced / silent)
    float grSibDb() const;                               // current gain reduction of the sibilance band (<= 0)
    float grLowDb() const;                               // ... of the low-end band
    float rhoEffective() const;                          // the pitch ratio the shifter is applying right now (1 while unvoiced)
    int voicedState() const;                             // 1 = the tracker treats the input as voiced (hold included), 0 = unvoiced / silent

private:
    struct Impl;
    Impl *d;
    JuiceDSP(const JuiceDSP &);
    JuiceDSP &operator=(const JuiceDSP &);
};

}  // namespace jb
