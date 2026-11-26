// Poli-verb DSP. See VerbDSP.h for the signal flow.
//
//   in -> Input -> [dry] ------------------------------------------------------------------------------> Mix -> Output
//              \-> Drive -> pre-delay line --+-> reflections (24 taps/ch, Probability gate, Lvl) -------+
//                                            \-> 4+4 input allpasses (Diffusion) -> 8-line FDN (Decay,  +-> wet EQ
//                                                Damping, Bass Mult, Density, Modulation, Spread) ------+   -> Width
//                                                gated by Attack / Release                                   -> L/R on
//
// Tail: 8 delay lines + an energy-preserving Householder matrix. Each line has a 2-band loss filter (a low band that
// decays Bass Mult x as long, a one-pole damping filter that makes the highs decay faster), a short allpass and,
// for the Spring space, a chain of dispersive allpasses. Line gains come from the decay time: g = 10^(-3 len / (fs RT)).
#include "VerbDSP.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace verb {

const Space kSpaces[kNumTypes] = {
    //  name         size  erMin erMax N  erTau even bright damp  bass  diff  mod   dens  spr lvl   seed
    {"Hall", 1.00f, 8, 90, 22, 110, 0.0f, 7000, 0.00f, 1.0f, 0.00f, 0.00f, 0.00f, 0, 0.0f, 0xA1u},
    {"Room", 0.45f, 3, 36, 18, 40, 0.0f, 6500, 0.10f, 0.9f, 0.05f, 0.00f, 0.05f, 0, 0.0f, 0xB2u},
    {"Chamber", 0.65f, 4, 60, 20, 70, 0.0f, 8000, 0.00f, 1.0f, 0.10f, 0.02f, 0.10f, 0, 0.0f, 0xC3u},
    {"Plate", 0.40f, 1, 22, 14, 30, 0.0f, 12000, -0.20f, 0.8f, 0.20f, 0.10f, 0.20f, 0, 1.0f, 0xD4u},
    {"Spring", 0.75f, 6, 50, 10, 60, 0.0f, 5000, 0.10f, 0.7f, -0.30f, 0.00f, -0.30f, 1, 0.0f, 0xE5u},
    {"Cathedral", 1.80f, 20, 200, 24, 160, 0.0f, 6000, 0.05f, 1.3f, 0.00f, 0.00f, -0.10f, 0, 0.0f, 0xF6u},
    {"Arena", 2.30f, 40, 300, 14, 250, 0.0f, 6500, 0.05f, 1.0f, -0.10f, 0.00f, -0.25f, 0, -1.0f, 0x17u},
    {"Booth", 0.20f, 1, 14, 12, 20, 0.0f, 4500, 0.35f, 0.6f, 0.00f, 0.00f, 0.10f, 0, -2.0f, 0x28u},
    {"Canyon", 2.80f, 120, 800, 10, 500, 0.0f, 5500, 0.15f, 0.8f, -0.30f, 0.00f, -0.45f, 0, 0.0f, 0x39u},
    {"Ambience", 0.30f, 2, 28, 22, 25, 0.0f, 10000, -0.10f, 0.8f, 0.00f, 0.00f, 0.25f, 0, -2.0f, 0x4Au},
    {"Cave", 1.30f, 15, 140, 16, 200, 0.0f, 3800, 0.30f, 1.4f, -0.20f, 0.10f, -0.20f, 0, 0.0f, 0x5Bu},
    {"Tunnel", 1.10f, 10, 260, 20, 300, 0.9f, 6000, 0.10f, 1.0f, -0.20f, 0.00f, -0.30f, 0, 0.0f, 0x6Cu},
    {"Garage", 0.70f, 6, 70, 18, 80, 0.0f, 7500, -0.05f, 0.8f, 0.10f, 0.00f, 0.05f, 0, 0.0f, 0x7Du},
    {"Stairwell", 0.90f, 8, 120, 16, 150, 0.2f, 7000, 0.00f, 1.0f, -0.10f, 0.45f, -0.05f, 0, 0.0f, 0x8Eu},
};

static const char *const kRoomClass[5] = {"S", "M", "L", "XL", "XXL"};
const char *roomClassName(int c) { return kRoomClass[c < 0 ? 0 : c > 4 ? 4 : c]; }

namespace {

const int kTick = 16;                       // control rate: every 16 samples
const int kPdBits = 18, kPdSize = 1 << kPdBits, kPdMask = kPdSize - 1;   // pre-delay + reflections: 5.9 s
const int kLineBits = 16, kLineSize = 1 << kLineBits, kLineMask = kLineSize - 1;
const float kMaxPd = 140000.0f;             // longest pre-delay (3.2 s)
const float kMaxTap = 70000.0f;             // longest reflection tap (1.6 s)
const float kPi = 3.14159265358979f;
const int kSpringStages = 36;

inline float kOf(float fs, float tauS) { return 1.0f - expf(-1.0f / (fs * tauS)); }
inline float fastTanh(float x) {            // good to ~2 % up to |x| = 3, then flat
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float dbToLin(float db) { return expf(db * 0.115129255f); }
inline float sinTurns(float t) {            // t in turns; parabola approximation, error < 1 %
    t -= floorf(t);
    float x = t * 2.0f - 1.0f;              // -1..1
    float s = 4.0f * x * (1.0f - fabsf(x)); // sin(pi x) shape: -> value of sin(2 pi (t - 0.5))
    return -s;
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) * (1.0f / 16777216.0f); }
};

struct Smooth {
    float v = 0;
    void snap(float x) { v = x; }
    float step(float target, float k) { v += k * (target - v); return v; }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void flush() { if (fabsf(z1) < 1e-15f) z1 = 0; if (fabsf(z2) < 1e-15f) z2 = 0; }
    // RBJ shelves, slope 1
    void shelf(bool high, float fs, float f0, float gainDb) {
        const float A = powf(10.0f, gainDb / 40.0f);
        const float w = 2.0f * kPi * f0 / fs, cw = cosf(w), sw = sinf(w);
        const float alpha = sw * 0.5f * sqrtf(2.0f);   // S = 1
        const float sA = 2.0f * sqrtf(A) * alpha;
        float a0;
        if (!high) {
            b0 = A * ((A + 1) - (A - 1) * cw + sA);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sA);
            a0 = (A + 1) + (A - 1) * cw + sA;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sA;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cw + sA);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sA);
            a0 = (A + 1) - (A - 1) * cw + sA;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sA;
        }
        const float r = 1.0f / a0;
        b0 *= r; b1 *= r; b2 *= r; a1 *= r; a2 *= r;
    }
};

// Schroeder allpass with its own delay buffer
struct Allpass {
    float *buf = nullptr;
    int len = 1, w = 0;
    void alloc(int n) { len = n; buf = (float *)calloc((size_t)n, sizeof(float)); w = 0; }
    void release() { free(buf); buf = nullptr; }
    void clear() { if (buf) memset(buf, 0, sizeof(float) * (size_t)len); w = 0; }
    inline float run(float x, float g) {
        const float d = buf[w];
        const float v = x - g * d;
        buf[w] = v;
        if (++w >= len) w = 0;
        return g * v + d;
    }
};

}  // namespace

struct Space_ER {
    float t[VerbDSP::kMaxEr];   // ms at scale 1
    float g[VerbDSP::kMaxEr];
    float u[VerbDSP::kMaxEr];   // probability threshold in (0,1)
};

struct VerbDSP::Impl {
    float fs = 44100.0f;
    // buffers
    float *pd[2] = {nullptr, nullptr};
    int pdW = 0;
    float *line[kN];
    int lineW = 0;
    Allpass inAp[2][4];
    Allpass lineAp[kN], lineAp2[kN];
    float springZ[kN][kSpringStages];
    // reflections
    Space_ER er[2];
    int erN = 0;
    float erLp[2] = {0, 0};
    float erLpK = 0.5f;
    // late field state
    float lenCur[kN], lenTgt[kN];
    float lpLow[kN], dmp[kN];
    float lfoPh[kN];
    float gLow[kN], gMid[kN], aD[kN], bD[kN];   // per-line loss filter, set every tick
    float wL[kN], wR[kN];
    float inSign[kN];
    // smoothed positions
    Smooth sp[P_COUNT];
    Smooth smPd, smEr;
    float erScaleTgt = 1.0f;
    bool firstTick = true;
    float tickSmooth = 0.1f;
    float k10, k40, k100;
    // derived control values (per tick)
    float dryG = 1, wetG = 0, inG = 1, outG = 1, driveAmt = 0, driveGain = 1;
    float width = 1, erGain = 0, prob = 1, apG = 0.5f, lineApG = 0.3f, modDepth = 0, lateK = 1;
    float modRate[kN];
    float attackK = 1, releaseF = 1;
    bool releaseInf = true, attackOn = false;
    float shelfDb[4] = {0, 0, 0, 0};
    Biquad eq[2][4];
    // wet channel enables, freeze, type duck
    Smooth smWetL, smWetR, smFreeze, smDuck, smFeed;
    // dynamics of the feed
    float envF = 0, envS = 0, swell = 1, gate = 1;
    bool dipping = false, armed = true;
    float limG = 1;
    float dither = 1e-15f;
    int tickCount = 0;
    int pendingType = -1;
    bool needClear = false;
    float erSpanNow = 0, pdNow = 0;

    void alloc() {
        pd[0] = (float *)calloc((size_t)kPdSize, sizeof(float));
        pd[1] = (float *)calloc((size_t)kPdSize, sizeof(float));
        for (int i = 0; i < kN; ++i) line[i] = (float *)calloc((size_t)kLineSize, sizeof(float));
        static const int apL[4] = {113, 163, 241, 337}, apR[4] = {127, 181, 257, 353};
        for (int i = 0; i < 4; ++i) { inAp[0][i].alloc(apL[i]); inAp[1][i].alloc(apR[i]); }
        static const int la[kN] = {43, 53, 61, 71, 83, 97, 107, 131};
        static const int lb[kN] = {157, 193, 211, 233, 277, 307, 331, 367};
        for (int i = 0; i < kN; ++i) { lineAp[i].alloc(la[i]); lineAp2[i].alloc(lb[i]); }
    }
    void release() {
        free(pd[0]); free(pd[1]);
        pd[0] = pd[1] = nullptr;
        for (int i = 0; i < kN; ++i) { free(line[i]); line[i] = nullptr; lineAp[i].release(); lineAp2[i].release(); }
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4; ++i) inAp[c][i].release();
    }
    void clearTail() {
        for (int i = 0; i < kN; ++i) {
            memset(line[i], 0, sizeof(float) * (size_t)kLineSize);
            lineAp[i].clear();
            lineAp2[i].clear();
            lpLow[i] = dmp[i] = 0;
            for (int s = 0; s < kSpringStages; ++s) springZ[i][s] = 0;
        }
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4; ++i) inAp[c][i].clear();
        erLp[0] = erLp[1] = 0;
    }
    void clearAll() {
        memset(pd[0], 0, sizeof(float) * (size_t)kPdSize);
        memset(pd[1], 0, sizeof(float) * (size_t)kPdSize);
        clearTail();
        for (int c = 0; c < 2; ++c) for (int b = 0; b < 4; ++b) { eq[c][b].z1 = eq[c][b].z2 = 0; }
        envF = envS = 0;
        swell = 1; gate = 1; dipping = false; armed = true; limG = 1;
        pdW = lineW = 0;
    }
    void buildReflections(int type) {
        const Space &sp_ = kSpaces[type];
        erN = sp_.erN > kMaxEr ? kMaxEr : sp_.erN;
        for (int c = 0; c < 2; ++c) {
            Rng r(sp_.seed * 7u + (uint32_t)c * 131u + 5u);
            // a random permutation of the probability thresholds: thinning keeps the pattern spread over time
            int perm[kMaxEr];
            for (int i = 0; i < erN; ++i) perm[i] = i;
            for (int i = erN - 1; i > 0; --i) { int j = (int)(r.next() * (i + 1)); if (j > i) j = i; std::swap(perm[i], perm[j]); }
            float norm = 0;
            for (int i = 0; i < erN; ++i) {
                const float uRand = (i + 0.15f + 0.7f * r.next()) / erN;
                const float uEven = (i + 1.0f) / erN;
                const float u = uRand + (uEven - uRand) * sp_.erEven;
                const float shape = powf(u, 1.25f);
                const float t = sp_.erMin + (sp_.erMax - sp_.erMin) * shape;
                float g = expf(-t / sp_.erTau) * (0.7f + 0.6f * r.next());
                if (r.next() < 0.3f) g = -g;
                er[c].t[i] = t;
                er[c].g[i] = g;
                er[c].u[i] = (perm[i] + 0.5f) / erN;
                norm += g * g;
            }
            const float k = 1.0f / sqrtf(norm > 1e-9f ? norm : 1.0f);
            for (int i = 0; i < erN; ++i) er[c].g[i] *= k;
        }
        erLpK = 1.0f - expf(-2.0f * kPi * sp_.erBright / fs);
    }
};

VerbDSP::VerbDSP() : d(new Impl) { for (int i = 0; i < P_COUNT; ++i) pos_[i] = kDefaultPos[i]; }
VerbDSP::~VerbDSP() { d->release(); delete d; }

void VerbDSP::init(float sampleRate) {
    fs_ = sampleRate;
    d->fs = sampleRate;
    if (!d->pd[0]) d->alloc();
    d->k10 = kOf(fs_, 0.010f);
    d->k40 = kOf(fs_, 0.040f);
    d->k100 = kOf(fs_, 0.100f);
    d->tickSmooth = 1.0f - powf(1.0f - d->k10, (float)kTick);
    clear();
}

void VerbDSP::clear() {
    Impl &m = *d;
    m.clearAll();
    for (int i = 0; i < P_COUNT; ++i) m.sp[i].snap(pos_[i]);
    for (int i = 0; i < kN; ++i) {
        m.lfoPh[i] = i * 0.1237f;
        m.modRate[i] = 0.23f + 0.071f * i;
        m.inSign[i] = (i == 2 || i == 4 || i == 5 || i == 7) ? -1.0f : 1.0f;
        m.gLow[i] = m.gMid[i] = 0.9f; m.aD[i] = 0; m.bD[i] = 0.9f;
        m.lenCur[i] = m.lenTgt[i] = 1500.0f;
    }
    m.buildReflections(type_);
    m.smWetL.snap(wetL_ ? 1.0f : 0.0f);
    m.smWetR.snap(wetR_ ? 1.0f : 0.0f);
    m.smFreeze.snap(freeze_ ? 1.0f : 0.0f);
    m.smDuck.snap(1.0f);
    m.smFeed.snap(1.0f);
    m.pendingType = -1;
    m.pdNow = 0;
    m.smPd.snap(0);
    m.smEr.snap(1.0f);
    m.tickCount = 0;
    m.firstTick = true;
    // snap the derived values once so the first block is already at the knob positions
    m.shelfDb[0] = m.shelfDb[1] = m.shelfDb[2] = m.shelfDb[3] = 0;
    for (int c = 0; c < 2; ++c) { m.eq[c][0].shelf(false, fs_, 200, 0); m.eq[c][1].shelf(true, fs_, 5000, 0); m.eq[c][2].shelf(false, fs_, 900, 0); m.eq[c][3].shelf(true, fs_, 900, 0); }
}

void VerbDSP::setParam(int id, float pos) {
    if (id < 0 || id >= P_COUNT) return;
    pos_[id] = clampf(pos, 0.0f, 1000.0f);
}
void VerbDSP::setType(int t) {
    t = t < 0 ? 0 : t >= kNumTypes ? kNumTypes - 1 : t;
    if (t == type_ && d->pendingType < 0) return;
    if (t == type_) { d->pendingType = -1; return; }
    d->pendingType = t;   // applied inside process(): a short wet dip, then the new space
}
void VerbDSP::setFreeze(bool on) { freeze_ = on; }
void VerbDSP::setSync(bool on) { sync_ = on; }
void VerbDSP::setWetEnable(bool l, bool r) { wetL_ = l; wetR_ = r; }
void VerbDSP::setTempo(double bpm) { bpm_ = (bpm > 20.0 && bpm < 400.0) ? bpm : 0.0; }

float VerbDSP::effectiveSize() const {
    const float s = pos_[P_SIZE] * 0.001f;
    return (0.25f + 3.0f * powf(s, 1.6f)) * kSpaces[type_].sizeMul;
}
int VerbDSP::roomClass() const {
    const float e = effectiveSize();
    return e < 0.6f ? 0 : e < 1.2f ? 1 : e < 2.4f ? 2 : e < 4.5f ? 3 : 4;
}
float VerbDSP::decaySeconds() const { return VerbMap::decayS(pos_[P_DECAY]); }
float VerbDSP::predelaySamples() const { return d->pdNow; }
float VerbDSP::erSpanMs() const { return d->erSpanNow; }

// ------------------------------------------------------------------------------------------------------------------
void VerbDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n) {
    Impl &m = *d;
    const float fs = fs_;
    const double bpm = bpm_ > 0 ? bpm_ : 120.0;
    static const float lineBase[kN] = {1021, 1229, 1453, 1699, 1951, 2237, 2539, 2851};
    static const float polOut[kN] = {1, -1, 1, 1, -1, 1, -1, -1};
    const float lpSplit = 1.0f - expf(-2.0f * kPi * 300.0f / fs);

    for (int i = 0; i < n; ++i) {
        // ---------------- control tick ----------------
        if (m.tickCount == 0) {
            m.tickCount = kTick;
            float p[P_COUNT];
            for (int k = 0; k < P_COUNT; ++k) p[k] = m.sp[k].step(pos_[k], m.tickSmooth);
            const Space &S = kSpaces[type_];
            const float mix = p[P_MIX] * 0.001f;
            m.dryG = cosf(mix * kPi * 0.5f);
            m.wetG = sinf(mix * kPi * 0.5f);
            m.inG = dbToLin(VerbMap::db24(p[P_INPUT]));
            m.outG = dbToLin(VerbMap::db24(p[P_OUTPUT]));
            m.driveAmt = p[P_DRIVE] * 0.001f;
            m.driveGain = 1.0f + 7.0f * m.driveAmt;
            m.width = VerbMap::widthPct(p[P_WIDTH]) * 0.01f;
            m.erGain = p[P_ERLVL] * 0.001f * 0.63f;
            m.prob = p[P_ERPROB] * 0.001f;
            const float diff = clampf(p[P_DIFFUSION] * 0.001f + S.diffBias, 0.0f, 1.0f);
            m.apG = 0.7f * diff;
            m.lineApG = 0.55f * diff;
            const float damp = clampf(p[P_DAMPING] * 0.001f + S.dampBias, 0.0f, 1.0f);
            const float dens = clampf(p[P_DENSITY] * 0.001f + S.densBias, 0.0f, 1.0f);
            const float mod = clampf(p[P_MODULATION] * 0.001f + S.modBias, 0.0f, 1.0f);
            m.modDepth = mod * 10.0f;
            const float spread = p[P_SPREAD] * 0.001f;
            {   // stereo weights of the lines
                const float norm = 1.0f / sqrtf(8.0f * (1.0f + spread * spread));
                for (int k = 0; k < kN; ++k) {
                    const float sg = (k & 1) ? -1.0f : 1.0f;
                    m.wL[k] = (1.0f + spread * sg) * norm * polOut[k];
                    m.wR[k] = (1.0f - spread * sg) * norm * polOut[k];
                }
            }
            // sizes
            const float sizeKnob = 0.25f + 3.0f * powf(p[P_SIZE] * 0.001f, 1.6f);
            const float densMul = 1.45f - 0.9f * dens;
            float lenScale = sizeKnob * S.sizeMul * densMul;
            if (lenScale < 0.12f) lenScale = 0.12f;
            for (int k = 0; k < kN; ++k) {
                float L = lineBase[k] * lenScale;
                if (S.spring) L = 520.0f + 150.0f * k * (0.5f + 0.5f * sizeKnob);   // a spring: ~12..40 ms round trips
                m.lenTgt[k] = clampf(L, 64.0f, (float)(kLineSize - 64));
            }
            // decay
            float RT = VerbMap::decayS(p[P_DECAY]);
            if (RT < 0.05f) RT = 0.05f;
            const float hiRatio = 1.0f / (1.0f + 9.0f * damp * damp);
            const float bass = clampf(VerbMap::bassMult(p[P_BASSMULT]) * S.bassBias, 0.1f, 8.0f);
            const float freeze = m.smFreeze.v;   // glides in per sample below; the loop gains follow it per tick
            for (int k = 0; k < kN; ++k) {
                const float len = m.lenCur[k];
                const float e = -6.9078f * len / fs;
                float gMid = expf(e / RT), gHi = expf(e / (RT * hiRatio)), gLo = expf(e / (RT * bass));
                gMid += (1.0f - gMid) * freeze; gHi += (1.0f - gHi) * freeze; gLo += (1.0f - gLo) * freeze;
                if (gHi > gMid) gHi = gMid;
                const float a = (gMid - gHi) / (gMid + gHi + 1e-9f);
                m.gLow[k] = gLo; m.gMid[k] = gMid; m.aD[k] = a; m.bD[k] = gMid * (1.0f - a);
            }
            // tail level: a long decay stacks up more energy, so ease the send off a little (re 2.4 s)
            m.lateK = 1.1f * powf(2.45f / RT, 0.38f) * dbToLin(S.lateLevel);
            // attack / release of the feed and the tail
            const float atk = VerbMap::attackMs(p[P_ATTACK]);
            m.attackOn = atk > 1.0f;
            m.attackK = 1.0f / (atk * 0.001f * fs / 3.0f + 1.0f);
            m.releaseInf = VerbMap::releaseInf(p[P_RELEASE]);
            m.releaseF = m.releaseInf ? 1.0f : expf(-6.9078f / (VerbMap::releaseS(p[P_RELEASE]) * fs));
            // pre-delay and reflection scale (with tempo sync)
            float pdSamples, erScale;
            const float beatMs = 60000.0f / (float)bpm;
            if (sync_) {
                pdSamples = kDivBeats[VerbMap::divIndex(p[P_PREDELAY])] * beatMs * 0.001f * fs;
                // the reflection pattern lasts exactly one straight note value: the one nearest its natural length
                const float natural = S.erMax * (sizeKnob / 1.25f);
                static const float notes[6] = {0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f};
                float best = notes[0] * beatMs, bd = 1e9f;
                for (int q = 0; q < 6; ++q) {
                    const float c = notes[q] * beatMs;
                    const float dist = fabsf(logf(c / natural));
                    if (dist < bd) { bd = dist; best = c; }
                }
                erScale = best / S.erMax;
            } else {
                pdSamples = VerbMap::predelayMs(p[P_PREDELAY]) * 0.001f * fs;
                erScale = sizeKnob / 1.25f;
            }
            pdSamples = clampf(pdSamples, 0.0f, kMaxPd);
            m.erSpanNow = S.erMax * erScale;
            m.pdNow = pdSamples;
            m.erScaleTgt = erScale;
            if (m.firstTick) { m.firstTick = false; m.smEr.v = erScale; m.smPd.v = pdSamples; }
            // wet EQ
            const float lowDb = VerbMap::db12(p[P_LOW]), highDb = VerbMap::db12(p[P_HIGH]), tiltDb = VerbMap::db6(p[P_TILT]);
            const float want[4] = {lowDb, highDb, tiltDb, -tiltDb};
            const float freq[4] = {200.0f, 5000.0f, 900.0f, 900.0f};
            for (int b = 0; b < 4; ++b) {
                if (fabsf(want[b] - m.shelfDb[b]) > 0.005f) {
                    m.shelfDb[b] = want[b];
                    for (int c = 0; c < 2; ++c) m.eq[c][b].shelf(b == 1 || b == 3, fs, freq[b], want[b]);
                }
            }
            // type change: dip the wet, swap at the bottom
            if (m.pendingType >= 0) {
                if (m.smDuck.v < 0.02f) {
                    type_ = m.pendingType;
                    m.pendingType = -1;
                    m.buildReflections(type_);
                    m.clearTail();
                    for (int k = 0; k < kN; ++k) m.lenCur[k] = m.lenTgt[k];
                    m.smEr.v = erScale;
                }
            }
            // flush tiny filter states (-ffast-math does not set flush-to-zero in a shared library)
            for (int c = 0; c < 2; ++c) for (int b = 0; b < 4; ++b) m.eq[c][b].flush();
            for (int k = 0; k < kN; ++k) {
                if (fabsf(m.lpLow[k]) < 1e-15f) m.lpLow[k] = 0;
                if (fabsf(m.dmp[k]) < 1e-15f) m.dmp[k] = 0;
            }
        }
        --m.tickCount;

        // ---------------- per sample ----------------
        const float duckTarget = m.pendingType >= 0 ? 0.0f : 1.0f;
        const float duck = m.smDuck.step(duckTarget, m.k10 * 1.5f);
        const float freezeV = m.smFreeze.step(freeze_ ? 1.0f : 0.0f, m.k100);
        const float feedAmt = m.smFeed.step(freeze_ ? 0.0f : 1.0f, m.k40);
        const float wl = m.smWetL.step(wetL_ ? 1.0f : 0.0f, m.k10);
        const float wr = m.smWetR.step(wetR_ ? 1.0f : 0.0f, m.k10);
        (void)freezeV;

        const float xl = inL[i] * m.inG, xr = inR[i] * m.inG;
        const float dryL = xl, dryR = xr;

        // activity, onset detection, swell (Attack) and gate (Release)
        const float a = fabsf(xl) > fabsf(xr) ? fabsf(xl) : fabsf(xr);
        m.envF = a > m.envF ? a : m.envF * 0.99887f;   // fast: 20 ms release
        m.envS += 1.5e-4f * (a - m.envS);
        if (m.attackOn) {
            if (m.armed && m.envF > 2.2f * m.envS + 0.004f) { m.armed = false; m.dipping = true; }
            else if (!m.armed && m.envF < 1.3f * m.envS + 0.002f) m.armed = true;
            if (m.dipping) {
                m.swell += 0.045f * (0.0f - m.swell);   // ~0.5 ms
                if (m.swell < 0.03f) m.dipping = false;
            } else {
                m.swell += m.attackK * (1.0f - m.swell);
            }
        } else {
            m.swell += 0.01f * (1.0f - m.swell);
            m.dipping = false;
        }
        if (m.releaseInf) m.gate += 0.01f * (1.0f - m.gate);
        else if (m.envF > 0.002f) m.gate += 0.003f * (1.0f - m.gate);
        else m.gate *= m.releaseF;

        // drive into the reverb (not the dry path)
        float sL = fastTanh(xl * m.driveGain) / m.driveGain * (1.0f + 2.5f * m.driveAmt);
        float sR = fastTanh(xr * m.driveGain) / m.driveGain * (1.0f + 2.5f * m.driveAmt);
        sL *= feedAmt; sR *= feedAmt;

        // pre-delay line
        m.pd[0][m.pdW & kPdMask] = sL;
        m.pd[1][m.pdW & kPdMask] = sR;
        const float pdTarget = m.pdNow;
        const float pdS = m.smPd.step(pdTarget, m.k40);
        const float erS = m.smEr.step(m.erScaleTgt, m.k40);
        auto readPd = [&](int c, float back) {
            if (back < 0) back = 0;
            if (back > (float)(kPdSize - 4)) back = (float)(kPdSize - 4);
            const int bi = (int)back;
            const float f = back - (float)bi;
            const float x0 = m.pd[c][(m.pdW - bi) & kPdMask], x1 = m.pd[c][(m.pdW - bi - 1) & kPdMask];
            return x0 + (x1 - x0) * f;
        };
        const float dL = readPd(0, pdS), dR = readPd(1, pdS);

        // early reflections: each tap fades in/out with the Probability gate
        float erL = 0, erR = 0;
        {
            const float w = 0.06f;
            const float pr = m.prob * (1.0f + w);
            const float msToS = fs * 0.001f;
            for (int c = 0; c < 2; ++c) {
                float acc = 0;
                for (int t = 0; t < m.erN; ++t) {
                    float g = (pr - m.er[c].u[t]) * (1.0f / w);
                    if (g <= 0.0f) continue;
                    if (g > 1.0f) g = 1.0f;
                    float back = pdS + m.er[c].t[t] * erS * msToS;
                    if (back > kMaxPd + kMaxTap) continue;
                    acc += g * m.er[c].g[t] * readPd(c, back);
                }
                (c ? erR : erL) = acc;
            }
            // reflections from the other side, a little: mono sources should still sit in the middle
            m.erLp[0] += m.erLpK * (erL - m.erLp[0]);
            m.erLp[1] += m.erLpK * (erR - m.erLp[1]);
            erL = m.erLp[0] * m.erGain;
            erR = m.erLp[1] * m.erGain;
        }

        // late field: input allpasses, then the FDN
        float fl = dL * m.swell, fr = dR * m.swell;
        for (int k = 0; k < 4; ++k) { fl = m.inAp[0][k].run(fl, m.apG); fr = m.inAp[1][k].run(fr, m.apG); }
        const float kin = m.lateK * 0.5f;
        float y[kN], sum = 0, oL = 0, oR = 0;
        const bool spring = kSpaces[type_].spring != 0;
        for (int k = 0; k < kN; ++k) {
            m.lfoPh[k] += m.modRate[k] / fs;
            if (m.lfoPh[k] > 1.0f) m.lfoPh[k] -= 1.0f;
            m.lenCur[k] += m.k100 * (m.lenTgt[k] - m.lenCur[k]);
            float dpos = m.lenCur[k] + m.modDepth * sinTurns(m.lfoPh[k]);
            if (dpos < 2.0f) dpos = 2.0f;
            const int bi = (int)dpos;
            const float f = dpos - (float)bi;
            const float *ln = m.line[k];
            const float v0 = ln[(m.lineW - bi) & kLineMask], v1 = ln[(m.lineW - bi - 1) & kLineMask];
            const float v = v0 + (v1 - v0) * f;
            oL += m.wL[k] * v;
            oR += m.wR[k] * v;
            // loss filter: low band (Bass Mult) + damped band
            m.lpLow[k] += lpSplit * (v - m.lpLow[k]);
            const float hi = v - m.lpLow[k];
            m.dmp[k] = m.bD[k] * hi + m.aD[k] * m.dmp[k];
            float o = m.gLow[k] * m.lpLow[k] + m.dmp[k];
            o = m.lineAp[k].run(o, m.lineApG);
            o = m.lineAp2[k].run(o, m.lineApG * 0.8f);
            if (spring && (k & 1) == 0) {
                for (int s = 0; s < kSpringStages; ++s) {
                    const float yy = 0.62f * o + m.springZ[k][s];
                    m.springZ[k][s] = o - 0.62f * yy;
                    o = yy;
                }
            }
            y[k] = o;
            sum += o;
        }
        {
            const float half = 0.25f * sum;   // Householder: I - (2/N) 11^T
            m.dither = -m.dither;
            for (int k = 0; k < kN; ++k) {
                const float inj = kin * m.inSign[k] * ((k & 1) ? (0.3f * fl + 0.7f * fr) : (0.7f * fl + 0.3f * fr));
                m.line[k][m.lineW & kLineMask] = inj + (y[k] - half) + m.dither;
            }
        }
        ++m.lineW;
        ++m.pdW;
        const float lateL = oL * m.gate, lateR = oR * m.gate;

        // wet: reflections + tail, EQ, width, L/R enables, type dip
        float wL_ = erL + lateL, wR_ = erR + lateR;
        for (int b = 0; b < 4; ++b) { wL_ = m.eq[0][b].run(wL_); wR_ = m.eq[1][b].run(wR_); }
        {
            const float M = 0.5f * (wL_ + wR_), Sd = 0.5f * (wL_ - wR_) * m.width;
            wL_ = (M + Sd) * wl;
            wR_ = (M - Sd) * wr;
        }
        wL_ *= duck; wR_ *= duck;

        float oLv = (dryL * m.dryG + wL_ * m.wetG) * m.outG;
        float oRv = (dryR * m.dryG + wR_ * m.wetG) * m.outG;

        // safety ceiling at -0.1 dBFS: instant attack, 50 ms release (the output is 16-bit)
        {
            const float pk = fabsf(oLv) > fabsf(oRv) ? fabsf(oLv) : fabsf(oRv);
            const float need = pk > 0.98855f ? 0.98855f / pk : 1.0f;
            if (need < m.limG) m.limG = need;
            else if (need >= 1.0f) m.limG += (1.0f - m.limG) * 0.00045f;
            if (m.limG > 0.9999f) m.limG = 1.0f;
            oLv *= m.limG; oRv *= m.limG;
        }
        outL[i] = oLv;
        outR[i] = oRv;
    }
}

}  // namespace verb
