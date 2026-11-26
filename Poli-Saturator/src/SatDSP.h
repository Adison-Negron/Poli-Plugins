// Poli-Saturator DSP: a four-flavour saturator with a compressor in front, a three-band drive and a harmonic softener.
//
//   in -> Smash (soft-knee compressor) -> x4 oversampling -> split Low / Mid / High (Linkwitz-Riley 4, 220 Hz / 2.5 kHz)
//      -> per band: drive -> wave-shaper (Type, Crunch) -> level match -> Soften (low-pass on the added harmonics only)
//      -> sum -> /4 -> DC block -> soft ceiling -> out
//
//   Saturation  the amount: input drive 0..+36 dB (a curve, not a line) and the wet weight, so 0 % is a clean pass.
//   Type        Tube (asymmetric tanh: even + odd harmonics), Tape (soft algebraic knee blended to tanh, darker),
//               Clip (quadratic soft clipper, knee from wide to nearly hard), Fold (sine fold to triangle fold).
//   Smash       compression INTO the shaper (threshold -4..-30 dB, ratio 1..12:1, faster with more Smash, level-neutral
//               makeup) so the shaper sees an even level: density instead of just louder peaks.
//   Crunch      the curve itself: bias and knee sharpness (Tube), tanh-ness (Tape), knee width (Clip), fold shape (Fold).
//   Soften      low-passes only the distortion products (not the dry highs) from 20 kHz down to 1.5 kHz; Tube and Tape
//               start a little softer on their own.
//   Low/Mid/High  how much of the Saturation each band gets: Off .. -24 dB .. 0 dB (the same as Saturation) .. +12 dB.
//
// Every shaper has unity small-signal gain, and a Gauss-Hermite estimate of its output level at a reference input keeps the
// loudness roughly constant as the drive goes up (gamma < 1, so quiet material still gets quieter than loud material).
//
// Knob positions are 0..1000 everywhere (the MPC knob travel); SatMap maps them to dB / % so the engine's value text, the
// DSP and the tests read the same numbers.
#pragma once
#include <cmath>
#include <cstdint>

namespace sat {

enum Pid { P_SAT, P_SMASH, P_CRUNCH, P_SOFTEN, P_LOW, P_MID, P_HIGH, P_COUNT };
enum Type { T_TUBE, T_TAPE, T_CLIP, T_FOLD, T_COUNT };

static const char *const kTypeName[T_COUNT] = {"Tube", "Tape", "Clip", "Fold"};

// the knob position (0..1000) of every parameter's default (= the "Init" preset)
static const float kDefaultPos[P_COUNT] = {500, 0, 400, 250, 500, 500, 500};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

struct SatMap {
    static float satPct(float p) { return p * 0.1f; }
    static float smashPct(float p) { return p * 0.1f; }
    static float crunchPct(float p) { return p * 0.1f; }
    static float softenPct(float p) { return p * 0.1f; }
    static float driveDb(float p) { return 36.0f * powf(clampf(p, 0.0f, 1000.0f) * 0.001f, 1.2f); }   // input drive, 0 .. 36 dB
    static bool bandOff(float p) { return p < 1.5f; }
    // a band's drive relative to Saturation: -24 dB .. 0 dB (knob centre) .. +12 dB (Off at the very bottom)
    static float bandDb(float p) { return p <= 500.0f ? (p - 500.0f) * 0.048f : (p - 500.0f) * 0.024f; }
};

class SatDSP {
public:
    SatDSP();
    ~SatDSP();
    void init(float sampleRate);                         // also clears every state
    void clear();

    void setParam(int id, float pos);                    // 0..1000
    void setType(int type);                              // 0..3, crossfaded over ~10 ms

    // any number of frames (float -1..1 in/out; in and out may not alias)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);

    // ---- read-outs for the tests ----
    float grDbNow() const;                               // the compressor's current gain change in dB (<= 0 at Smash > 0)
    int type() const { return type_; }

private:
    struct Impl;
    Impl *d;
    SatDSP(const SatDSP &);
    SatDSP &operator=(const SatDSP &);
    float pos_[P_COUNT];
    int type_ = T_TUBE;
    float fs_ = 44100.0f;
    friend struct Impl;
};

}  // namespace sat
