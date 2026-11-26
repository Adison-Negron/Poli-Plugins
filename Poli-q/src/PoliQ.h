// Poli-q DSP: an 8-band equalizer for the MPC (44.1 kHz, stereo, float inside).
//
// Every band is built from trapezoidal state-variable filters (Andrew Simper, Cytomic, "SvfLinearTrapOptimised2"):
// they stay clean while their coefficients move, so a Q-Link turn glides instead of zippering. A band's type is one of
// kTypes (see TypeId). Cuts steeper than 12 dB/oct are Butterworth cascades; Q then sets the resonance of the last stage.
// The display uses the exact transfer function of the same stages (responseDb), so the curve on screen is the filter.
//
// Changes that would click are smoothed: freq / gain / Q glide (~10 ms, coefficients re-tuned every 16 samples), a type
// change or a band switched on/off crossfades the old and the new filter over 10 ms, and Pass crossfades dry/wet.
#pragma once
#include <cstdint>

namespace pq {

constexpr int kBands = 8;
constexpr float kFs = 44100.0f;
constexpr float kFLo = 20.0f, kFHi = 20000.0f;      // Freq knob: 20 Hz .. 20 kHz, log (position 0..100)
constexpr float kQLo = 0.1f, kQHi = 18.0f;          // Q knob: 0.1 .. 18, log (position 0..100)
constexpr float kGainMax = 18.0f;                   // Gain knob: -18 .. +18 dB (the grid's range)
constexpr int kMaxStages = 4;

enum TypeId {
    kBell = 0, kLowShelf, kHighShelf,
    kLowCut6, kLowCut12, kLowCut24, kLowCut48,
    kHighCut6, kHighCut12, kHighCut24, kHighCut48,
    kNotch, kBandPass, kTilt,
    kTypes
};
extern const char *const kTypeNames[kTypes];

inline bool typeHasGain(int t) { return t == kBell || t == kLowShelf || t == kHighShelf || t == kBandPass || t == kTilt; }
inline bool typeHasQ(int t) { return t != kLowCut6 && t != kHighCut6; }

float posToHz(float pos);     // 0..100 -> 20..20000
float hzToPos(float hz);
float posToQ(float pos);      // 0..100 -> 0.1..18
float qToPos(float q);

// One filter section: an SVF (order 2) or a trapezoidal one-pole (order 1); y = m0*x + m1*band + m2*low.
struct Coefs {
    int order = 2;
    float g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
};
struct Design {
    int n = 0;                 // stages (0 = a straight wire)
    Coefs st[kMaxStages];
    float post = 1.0f;         // output gain (band pass level, tilt centring)
};
// The stages for one band. f in Hz, gain in dB, q as the knob shows it.
void design(int type, float f, float gainDb, float q, Design &d);
// |H| in dB of a design at frequency hz (exact for the stages above, bilinear with prewarping).
float responseDb(const Design &d, float hz);
// One band's own response in dB at hz, computed from its settings (the display's dot and curve).
float bandDb(int type, float f, float gainDb, float q, float hz);

struct BandSettings {
    int type = kBell;
    bool on = true;
    float hz = 1000, gain = 0, q = 0.7071f;
};

// Dynamic EQ (1.1): a band's gain moves by up to Range dB while its own part of the input is over Threshold (the
// band's frequency region, picked out by a detector filter: band pass for Bell / Band Pass, low pass for a Low Shelf,
// high pass for a High Shelf, the whole signal for Tilt). Range < 0 cuts when loud (a de-esser, taming a boomy note),
// Range > 0 boosts when loud. Range is reached 10 dB over Threshold. Range 0 = a static band (no detector runs).
// Cuts and Notch have no gain, so no dynamics.
struct DynSettings {
    float thrDb = -30.0f;     // -60 .. 0 dBFS (the detector's peak level)
    float rangeDb = 0.0f;     // -18 .. +18
    float attMs = 10.0f;      // 0.5 .. 200
    float relMs = 150.0f;     // 10 .. 2000
};
constexpr float kDynKneeDb = 10.0f;
inline bool typeHasDyn(int t) { return typeHasGain(t); }

class Band {
public:
    void reset();
    // target settings; a change of type or on/off starts a crossfade, the rest glides
    void set(const BandSettings &s);
    void tick16();                                      // glide one 16-sample step, re-tune
    void setDyn(float db) { dyn_ = db; }                // the dynamic part of the gain (dB), applied at the next tick
    inline void process(float &l, float &r);            // one stereo sample
    void flushDenormals();
    const BandSettings &target() const { return tgt_; }

private:
    struct Slot {
        int type = kBell;
        bool on = false;
        Design d;              // the coefficients in use (they step towards `to` sample by sample while gliding)
        Design to;             // where they will be at the next 16-sample tick
        Coefs step[kMaxStages];
        float postStep = 0;
        int ramp = 0;          // samples of stepping left
        float s1[kMaxStages][2] = {}, s2[kMaxStages][2] = {};
        void clear() {
            for (int i = 0; i < kMaxStages; ++i) s1[i][0] = s1[i][1] = s2[i][0] = s2[i][1] = 0;
        }
    };
    static inline float run(Slot &s, float x, int ch);
    static inline void advance(Slot &s);
    void retune(Slot &s, bool ramp);
    BandSettings tgt_;
    float lf_ = 6.9078f, gain_ = 0, lq_ = -0.3466f;     // glided: ln(hz), dB, ln(q)
    float lf1_ = 6.9078f, gain1_ = 0, lq1_ = -0.3466f;  // the glide's first stage (two stages: it starts gently)
    bool first_ = true;
    float dyn_ = 0, dynApplied_ = 0;                    // dynamic gain (dB) asked for / in the coefficients
    Slot slot_[2];
    int cur_ = 0;
    int fade_ = 0;                                      // samples left of a crossfade into slot_[cur_]
};

class Analyzer;

class Eq {
public:
    void init();
    void setBand(int b, const BandSettings &s) { band_[b].set(s); detDesign(b); }
    const BandSettings &band(int b) const { return band_[b].target(); }
    void setDynamics(int b, const DynSettings &d);
    const DynSettings &dynamics(int b) const { return det_[b].s; }
    float dynGain(int b) const { return det_[b].active ? det_[b].delta : 0.0f; }   // dB the band is moved by now
    bool dynActive(int b) const { return det_[b].active; }
    void setBypass(bool on) { bypass_ = on; }
    bool bypass() const { return bypass_; }
    // in/out: 128 frames (any count <= 128 works), float -1..1
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);

private:
    struct Det {
        DynSettings s;
        bool active = false;
        int mode = 0;          // 1 band pass, 2 low pass, 3 high pass, 4 the whole signal
        float k = 1, a1 = 0, a2 = 0, a3 = 0;
        float ic1 = 0, ic2 = 0, env = 0, atk = 0, rel = 0, delta = 0;
    };
    void detDesign(int b);
    Det det_[kBands];
    Band band_[kBands];
    bool bypass_ = false;
    float wet_ = 1.0f;          // 1 = EQ heard, 0 = dry (Pass); glides
    int sub_ = 0;               // samples until the next 16-sample tick
};

// Spectrum of the output for the display: 2048-point Hann FFT every 1024 samples, kCols log columns 20 Hz..20 kHz,
// a +4.5 dB/oct tilt (pink noise reads flat), instant rise, falls 30 dB/s. level(j) is 0..1.
class Analyzer {
public:
    static constexpr int kN = 2048, kHop = 1024;
    static constexpr float kSpecLo = -90.0f, kSpecHi = -12.0f;   // dB (after the tilt) at the grid's bottom / top
    void init(int cols);
    void push(const float *l, const float *r, int n);   // returns quickly when not running
    void setRunning(bool on);
    bool running() const { return running_; }
    float level(int j) const { return lev_[j]; }
    void clear();
    int cols() const { return cols_; }

private:
    void analyse();
    bool running_ = true;
    int cols_ = 0;
    float buf_[kN] = {};
    int wpos_ = 0, sinceHop_ = 0;
    float win_[kN] = {};
    float re_[kN] = {}, im_[kN] = {};
    float cosT_[kN / 2] = {}, sinT_[kN / 2] = {};
    int rev_[kN] = {};
    float lev_[64] = {};
    int lo_[64] = {}, hi_[64] = {};
    float fc_[64] = {};
};

}  // namespace pq
