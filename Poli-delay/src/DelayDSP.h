// Poli-delay DSP: a stereo delay with a regenerating, filtered, saturated, modulated feedback loop.
//
//   in -> [ delay line L / R (Time, + Offset) ] -> Drive (tape/tube saturation) -> High Pass -> Low Pass -> Tone
//             ^                                                                                           |
//             +------------- Feedback (straight, or crossed L<->R for Ping-Pong) <------------------------+--> wet -> Spread -> Mix
//
// The filters and the saturation sit INSIDE the loop, so every repeat is darker / warmer than the one before.
// Modulation (Rate, Depth) wobbles the read position of both lines (90 degrees apart): chorus at low depth, pitch
// warble at high depth. Time changes glide like a tape head (the repeats bend in pitch, nothing clicks).
//
// Knob positions are 0..1000 everywhere (the MPC knob travel); DelayMap maps them to Hz / ms / % so the engine's value
// text, the DSP and the tests read the same numbers.
#pragma once
#include <cmath>
#include <cstdint>

namespace delay {

enum Pid {
    P_TIME, P_OFFSET, P_FEEDBACK, P_HP, P_LP, P_MIX,       // upper section
    P_RATE, P_DEPTH, P_DRIVE, P_TONE, P_SPREAD,            // lower section
    P_COUNT
};

static const int kNumDivs = 13;   // tempo-synced note values for Time

// the knob position (0..1000) of every parameter's default (= the "Init" preset)
static const float kDefaultPos[P_COUNT] = {
    683, 500, 350, 200, 850, 300,      // Time ~300 ms, Offset 0 %, Feedback 35 %, HP 50 Hz, LP ~11 kHz, Mix 30 %
    450, 0, 0, 500, 500                // Rate ~0.5 Hz, Depth 0 %, Drive 0 %, Tone 0 %, Spread 100 %
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

struct DelayMap {
    static float timeMs(float p) { return 5.0f * powf(400.0f, p * 0.001f); }                 // 5 ms .. 2 s (log)
    static float posOfTimeMs(float ms) { return 1000.0f * logf(clampf(ms, 5.0f, 2000.0f) / 5.0f) / logf(400.0f); }
    static int divIndex(float p) { return (int)lrintf(clampf(p, 0, 1000) * (kNumDivs - 1) / 1000.0f); }
    static float offsetPct(float p) { return (p - 500.0f) * 0.2f; }                          // -100 .. +100 %
    static float feedbackPct(float p) { return p * 0.1f; }                                   // 0 .. 100 %
    static float hpHz(float p) { return 20.0f * powf(100.0f, p * 0.001f); }                  // 20 Hz .. 2 kHz
    static float lpHz(float p) { return 500.0f * powf(40.0f, p * 0.001f); }                  // 500 Hz .. 20 kHz
    static float mixPct(float p) { return p * 0.1f; }
    static float rateHz(float p) { return 0.05f * powf(200.0f, p * 0.001f); }                // 0.05 .. 10 Hz
    static float depthPct(float p) { return p * 0.1f; }
    static float drivePct(float p) { return p * 0.1f; }
    static float tonePct(float p) { return (p - 500.0f) * 0.2f; }                            // -100 .. +100 % (= -6 .. +6 dB tilt)
    static float spreadPct(float p) { return p * 0.2f; }                                     // 0 .. 200 %
};

// note value in beats (a beat = a quarter note) and the label the Time knob shows when Sync is on
static const float kDivBeats[kNumDivs] = {0.125f, 1.0f / 6, 0.25f, 1.0f / 3, 0.375f, 0.5f, 2.0f / 3, 0.75f, 1.0f, 4.0f / 3, 1.5f, 2.0f, 4.0f};
static const char *const kDivName[kNumDivs] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/1"};

class DelayDSP {
public:
    static const int kBlock = 128;
    static const int kBufLog = 18;                       // 262144 samples = 5.9 s per side
    static const int kBufLen = 1 << kBufLog;

    DelayDSP();
    ~DelayDSP();
    void init(float sampleRate);                         // also clears every buffer
    void clear();

    void setParam(int id, float pos);                    // 0..1000
    void setSync(bool on);
    void setOffsetOn(bool on);
    void setPingPong(bool on);
    void setTempo(double bpm);                           // host tempo for Sync; 0 = unknown (120 is used)

    // any number of frames (float -1..1 in/out; in and out may not alias)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);

    // ---- read-outs for the engine and the tests ----
    float delaySamples(int ch) const;                    // the delay each side is heading for (after sync and offset), samples
    float timeMsNow() const;                             // the base Time in ms (after sync)
    bool synced() const { return sync_; }

private:
    struct Impl;
    Impl *d;
    DelayDSP(const DelayDSP &);
    DelayDSP &operator=(const DelayDSP &);
    float pos_[P_COUNT];
    bool sync_ = false, offsetOn_ = false, pingPong_ = false;
    double bpm_ = 0;
    float fs_ = 44100.0f;
    friend struct Impl;
};

}  // namespace delay
