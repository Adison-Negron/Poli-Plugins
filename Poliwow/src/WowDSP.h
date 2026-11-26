// Poliwow DSP: one "Wow" knob that builds a track up, Endless Smile style.
//
// 1.1: everything is made FROM the track. No riser is played on top of it: the "air" is noise shaped by the track's own
// highs, the rising tone is a resonance the track excites, and the pads are grains of the track and a shimmer reverb.
//
//   in -> drive -> high-pass sweep -> bright shelf ("tilt") -> phaser
//      + air (noise that follows the track's highs) + rising resonance (a comb the track rings, its pitch climbing)
//      -> width -> auto gain (level-matched to the dry track above 300 Hz) + the preset's lift
//      -> dry cut (lets the pad take over at the top)                         = the "core", faded in over 0-4 % of Wow
//   grains of the input (granular cloud: size, density, spray, octave / reverse grains, pitch climbing with Wow)
//   sends -> stereo tempo delay (L and R times, a frequency shifter in the loop so echoes climb)
//         -> shimmer reverb (8-line FDN, modulated; an octave-up pitch shifter in its feedback; decay grows with Wow,
//            up to "infinite" in the Halo-style presets)                    = the "tails"
//   all of it ducked by the pump (tempo-synced, speeding up 1/4 .. 1/32), then
//   out = dry * (1 - mix) + (core + cloud + tails) * mix, and a peak limiter that never touches the dry signal.
//
// 1.2: the granular cloud became a room (GrainCtl, the GRAINS tab): grains move through the stereo room at a speed, bounce
// off its walls and each other and may SPLIT into a child at a new harmonic and size; a grain may RETRACE (jump back
// in time, or reverse); the grain clock can follow the song tempo (SYNC) with a VARIATION that moves grains off the grid.
// Bright reverb for build-ups: the reverb send's high-pass climbs with Wow (verbHpTo) and the return gets a high shelf.
// Every GrainCtl value defaults to neutral: the 1.1 presets sound as before until a GRAINS knob is touched.
//
// Wow 0 = the input, bit for bit (once any tail has died away). Each preset ("style") is one WowStyle, built by name in
// WowDSP.cpp.
#pragma once
#include <cstdint>

struct WowStyle {
    const char *name;
    // tone
    float tiltDb;                         // high shelf at 2.5 kHz at full Wow (the "brighter" lift)
    float hpfTo, hpfQ, hpfCurve;          // high-pass: 10 Hz -> hpfTo, Q 0.7 -> hpfQ (late in the turn), travel = w^hpfCurve
    float driveDb;                        // tanh drive at full Wow
    float phaser, phRate0, phRate1;       // phaser mix at full Wow, LFO rate (Hz) at the start / end of the turn
    // rhythm
    float pump, pumpBeats0, pumpBeats1;   // duck depth; one duck every pumpBeats0 .. pumpBeats1 beats (whole note values)
    // stereo delay
    float dly, dlyFb, dlyBeatsL, dlyBeatsR, shiftHz;   // send, feedback, L / R time (beats), echo climb (Hz per pass)
    // reverb
    float verb, decay0, decay1;           // send at full Wow; decay (s, -60 dB) at Wow 0 / 100
    float shimmer, shimmerSemi, verbDamp; // shimmer feedback 0..0.6, its interval (12 = octave), reverb low-pass (Hz)
    float verbHpTo, verbTiltDb;           // bright sweeps: the reverb send's high-pass climbs 20 Hz -> verbHpTo (0 = off); the return's high shelf (dB)
    // granular cloud (grains of the input)
    float cloud, grainMs, density0, density1;   // level at full Wow, grain length, grains per second at the start / end
    float spray, riseSemi;                // how far back grains reach (s); pitch climb at full Wow (semitones)
    float octaveProb, reverseProb, spread;      // share of grains an octave up, share played backwards, stereo spread
    // risers made of the track
    float air;                            // noise shaped by the track's highs
    float reso, resoLo, resoHi;           // the rising resonance: level, start / end pitch (Hz)
    // output
    float width;                          // extra side signal at full Wow (1 = twice as wide)
    float makeupDb;                       // lift at full Wow, on top of the auto gain (which keeps the core level)
    float dryCutDb;                       // the core drops this much at full Wow, so a pad or cloud can take over
};

static const int kNumStyles = 21;
extern const WowStyle kStyles[kNumStyles];

// The granulator controls (the GRANULES page). A preset (kPresets, below) sets them all when it is picked; after that they are
// the user's. All 0..1 except the two switches.
struct GrainCtl {
    float level = 0.5f;     // gain of the whole cloud, preset's own + added: 0 = silent, 0.5 = as the preset (x1), 1 = x2
    float amount = 0;       // extra grains on top of the preset's cloud (0 = the preset's own; adds grains even to presets with none)
    float size = 0.5f;      // 0.5 = the preset's grain length; x0.25 .. x4
    float density = 0.5f;   // 0.5 = the preset's grains per second; x0.25 .. x4
    float speed = 0;        // how fast grains move through the room (0 = they stay where they were born)
    float split = 0;        // probability that a bounce splits a grain (a child at another harmonic and size)
    float retrace = 0;      // probability that a grain goes back in time
    float retTime = 0.5f;   // how far back: 50 ms .. 2 s
    float retLen = 0.5f;    // how long the retraced piece plays: 40 ms .. 1 s
    float variation = 0;    // sync on: how far grains land off the beat grid; sync off: how uneven their spacing is
    bool retReverse = false;   // retrace as a reversed grain (false: a jump back, played forwards)
    bool sync = false;         // grain clock on the song tempo
};

// Presets: a base sound (kStyles above) plus the granulator settings it starts with, sorted into categories.
struct WowPreset {
    const char *name;
    int cat;          // index into kCats
    int base;         // index into kStyles
    GrainCtl g;
};
static const int kNumCats = 7;
extern const char *const kCats[kNumCats];
static const int kNumPresets = 39;
extern const WowPreset kPresets[kNumPresets];
int wowFirstPreset(int cat);   // first preset of a category

class WowDSP {
public:
    void init();
    // callers pass finite values (engine.cpp checks the text it is given)
    void setWow(float w01) { wowTarget = w01 < 0 ? 0 : w01 > 1 ? 1 : w01; }
    void setMix(float m01) { mixTarget = m01 < 0 ? 0 : m01 > 1 ? 1 : m01; }
    void setStyle(int s);
    void setGrainCtl(const GrainCtl &g);
    // host tempo and song position (ppq < 0: none; then the pump free-runs at bpm, or 120)
    void setSongPos(double ppq, double bpm, bool playing);
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);
    unsigned resets = 0;   // times the safety net restarted the effect (process(); 0 in every test)
#ifdef POLIWOW_PROBE
    // test builds only: energy of each part of the wet signal, and the limiter's gain, summed per sample
    struct Probe { double core = 0, cloud = 0, echo = 0, verb = 0, lim = 0, limited = 0; long n = 0; } probe;
#endif

    static const int kDelayLen = 1 << 17;   // 2.97 s at 44.1 kHz
    static const int kGrainLen = 1 << 17;   // the granular cloud's memory of the input: 2.97 s
    static const int kMaxGrains = 32;   // 1.2: room for children (splits, retraces)
    static const int kFdnLines = 8, kFdnLen = 4096;
    static const int kShiftLen = 4096, kResoLen = 4096;

private:
    struct SVF {   // TPT state-variable filter (Zavalishin), one channel
        float ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        // returns high-pass; band-pass in bp, low-pass in lp
        inline float tick(float x, float g, float k, float &bp, float &lp) {
            float a1 = 1.0f / (1.0f + g * (g + k));
            float v3 = x - ic2;
            float v1 = a1 * ic1 + g * a1 * v3;
            float v2 = ic2 + g * v1;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            if (ic1 < 1e-20f && ic1 > -1e-20f) ic1 = 0;   // no denormals once the input stops (they cost CPU)
            if (ic2 < 1e-20f && ic2 > -1e-20f) ic2 = 0;
            bp = v1;
            lp = v2;
            return x - k * v1 - v2;
        }
    };
    struct Hilbert {   // two allpass chains 90 degrees apart (Olli Niemitalo's coefficients)
        float x1[8], x2[8], y1[8], y2[8];
        float delayed = 0;
        void reset();
        void tick(float x, float &re, float &im);
    };
    struct Shifter {   // delay-line pitch shifter: two read taps 180 degrees apart, sin^2 crossfade
        float buf[kShiftLen];
        int w = 0;
        float ph = 0;   // tap A's delay / window, 0..1
    };
    struct Grain {
        bool on = false;
        double pos = 0;     // read position in the cloud's buffer
        float inc = 1;      // samples per sample (negative = backwards)
        int age = 0, len = 0;
        float gl = 0, gr = 0;
        // 1.2: the room
        float x = 0.5f, vx = 0;     // place in the room (0 = left wall .. 1 = right wall) and speed (room widths per second)
        float amp = 1;              // level (children are a little quieter)
        int cool = 0;               // sub-blocks before it may bounce again
        int retAt = -1;             // age at which it goes back in time (-1 = never)
        unsigned char gen = 0;      // 0 = born from the clock, 1 = a split child, 2 = a retraced piece or a split of a split (neither splits again)
    };

    float smooth(float cur, float target, float coef) { return cur + (target - cur) * coef; }
    float noiseSample();
    float rand01() { return 0.5f + 0.5f * noiseSample(); }
    float shift(Shifter &s, float x, float ratio);
    void spawnGrain(float w, float sizeScale, float syncLen);
    int allocGrain();
    void setPan(Grain &g);
    void stepRoom(float dt);
    void onBounce(int k);
    void splitGrain(int k);
    void retraceGrain(int k);

    const WowStyle *st = &kStyles[0];
    float wowTarget = 0, wow = 0, mixTarget = 1, mix = 1;

    // tempo
    double ppq = -1, bpm = 120;
    bool playing = false;
    double freePhase = 0;   // pump phase in beats when the host gives no position

    // tone
    SVF hpf[2], tiltHp[2];
    float apState[2][4] = {};
    float phFb[2] = {};
    double phLfo = 0;

    // pump
    float pumpGain = 1;
    float pumpExp = 0;      // log2 of the pump's note length in beats (0 = 1/4, -1 = 1/8 ...)

    // air + resonance
    SVF airBp[2], hfHp[2], resoHp[2];
    float hfEnv[2] = {};
    uint32_t rng = 0x12345678u;
    float resoBuf[2][kResoLen];
    int resoPos = 0;

    // granular cloud
    float gBufL[kGrainLen], gBufR[kGrainLen];
    int gPos = 0;
    Grain grains[kMaxGrains];
    SVF cloudTilt[2];
    float spawnAcc = 0;
    float hann[1025];
    GrainCtl gc;
    float levelS = 0;                // gc.amount, glided
    float lvlS = 1;                  // gc.level x 2, glided (the cloud's gain)
    float roomV = 0;                 // speed given to new grains (room widths per second)
    long long syncK = 0;             // sync: index of the next grid slot
    double syncJit = 0, syncPrev = 0;
    float syncN = 0;                 // grid slots per beat
    bool syncSeated = false;

    // delay + shifter
    float delayBufL[kDelayLen], delayBufR[kDelayLen];
    int dPos = 0;
    float dTimeL = 16537, dTimeR = 11025;   // samples, smoothed
    Hilbert hil[2];
    double shPh = 0;
    SVF loopHp[2], loopLp[2];

    // shimmer reverb
    float fdn[kFdnLines][kFdnLen];
    int fdnPos = 0;
    float fdnLp[kFdnLines] = {};
    float decay = 2.5f;                     // seconds, smoothed
    double fdnLfo = 0;
    Shifter shim[2];
    SVF shimHp[2], shimLp[2];
    SVF vSendHp[2], vRetHp[2];       // 1.2: the reverb send's rising high-pass, the return's high shelf
    float shimFeed[2] = {};

    // auto gain: the core matched to the input above 300 Hz
    SVF lvlHpIn, lvlHpCore;
    float lvlIn = 0, lvlCore = 0, match = 1;

    // output limiter
    float limGain = 1;
};
