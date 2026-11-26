// Poli-Chorus DSP: a three-mode chorus after Ableton's Chorus-Ensemble (Classic, Ensemble, Vibrato) with Feedback, Width,
// LP Filter, Warmth, Output and Mix.
//
//   in --+--------------------------------------------------------------+
//        |                                                              |
//        +-> delay line (per channel) -> modulated taps (2 / 3 / 1) -> Warmth -> LP Filter -> wet -> Width (M/S) -> Mix -> Output
//                 ^                                                     |
//                 +------------------- Feedback (high-passed, soft-clipped) <-+
//
//   Classic   two voices per channel (LFO 180 deg apart), 7.5 / 11.5 ms, sine LFO: the "juno" chorus.
//   Ensemble  three voices per channel (LFO 120 deg apart), 9.5 / 11 / 12.5 ms, a slow sine with a faster ripple on it: strings.
//   Vibrato   one voice per channel, 6 ms: the pitch wobbles, no fixed delay to comb against the dry.
//   Rate      LFO 0.05 .. 10 Hz (log).        Amount  modulation depth (Classic +-3.5 ms, Ensemble +-4.5 ms, Vibrato +-4 ms at 100 %).
//   Feedback  0 .. 100 % of travel (= 0 .. 0.8 loop gain).
//   Width     the right channel's LFO runs 0 .. 180 deg behind the left one, and the wet side signal grows from mono to full.
//   LP Filter 2-pole low-pass on the wet signal and in the loop, 800 Hz .. 20 kHz (Off at the top).
//   Warmth    soft saturation (unity small-signal gain) plus a little high-frequency softening on the wet signal.
//   Output    -24 .. +12 dB on the whole output (0 dB at 667).        Mix  equal-power dry/wet.
//
// Knob positions are 0..1000 everywhere (the MPC knob travel); ChoMap maps them to Hz / % / dB so the engine's value text, the DSP,
// the display and the tests read the same numbers. The LFO shape is shared with the display (ChoMap::lfo).
#pragma once
#include <cmath>
#include <cstdint>

namespace cho {

enum Pid { P_RATE, P_AMOUNT, P_FEEDBACK, P_WIDTH, P_LPF, P_WARMTH, P_OUTPUT, P_MIX, P_COUNT };
enum Mode { M_CLASSIC, M_ENSEMBLE, M_VIBRATO, M_COUNT };

static const char *const kModeName[M_COUNT] = {"Classic", "Ensemble", "Vibrato"};

// the knob position (0..1000) of every parameter's default (= the "Init" preset)
static const float kDefaultPos[P_COUNT] = {523, 500, 0, 600, 1000, 250, 667, 500};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

struct ChoMap {
    static float norm(float p) { return clampf(p, 0.0f, 1000.0f) * 0.001f; }
    static float rateHz(float p) { return 0.05f * powf(200.0f, norm(p)); }                 // 0.05 .. 10 Hz
    static float amountPct(float p) { return p * 0.1f; }
    static float feedbackPct(float p) { return p * 0.1f; }                                // 0..100 % of the travel
    static float feedbackGain(float p) { return 0.8f * norm(p); }                         // loop gain
    static float widthPct(float p) { return p * 0.1f; }
    static float warmthPct(float p) { return p * 0.1f; }
    static float mixPct(float p) { return p * 0.1f; }
    static float lpfHz(float p) { return 800.0f * powf(25.0f, norm(p)); }                 // 800 Hz .. 20 kHz
    static bool lpfOff(float p) { return p >= 995.0f; }
    static float outputDb(float p) { p = clampf(p, 0.0f, 1000.0f); return p <= 667.0f ? -24.0f + 24.0f * p / 667.0f : 12.0f * (p - 667.0f) / 333.0f; }
    // Width: how far (in LFO cycles) the right channel's LFO runs behind the left one, and the wet side-signal gain
    static float thetaCycles(float p) { return 0.5f * norm(p); }
    static float sideGain(float p) { return sqrtf(norm(p)); }

    // the LFO: phase in cycles (any real); -1..1. Classic and Vibrato = sine; Ensemble = a slow sine with a faster ripple on it
    static float lfo(int mode, float cyc) {
        cyc -= floorf(cyc);
        const float s = sinf(6.2831853f * cyc);
        if (mode != M_ENSEMBLE) return s;
        return (s + 0.18f * sinf(6.2831853f * 4.0f * cyc + 0.6f)) * (1.0f / 1.18f);
    }
};

class ChoDSP {
public:
    ChoDSP();
    ~ChoDSP();
    void init(float sampleRate);                         // also clears every state
    void clear();

    void setParam(int id, float pos);                    // 0..1000
    void setMode(int mode);                              // 0..2, crossfaded over ~10 ms

    // any number of frames (float -1..1 in/out; in and out may not alias)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);

    int mode() const { return mode_; }

private:
    struct Impl;
    Impl *d;
    ChoDSP(const ChoDSP &);
    ChoDSP &operator=(const ChoDSP &);
    float pos_[P_COUNT];
    int mode_ = M_CLASSIC;
    float fs_ = 44100.0f;
    friend struct Impl;
};

}  // namespace cho
