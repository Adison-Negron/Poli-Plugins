#include "DelayDSP.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace delay {

namespace {
const double kPi = 3.14159265358979323846;

inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }

// 2-pole trapezoidal state-variable filter (Butterworth), re-tuned every few samples
struct Svf {
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.41421356f;
    void tune(float fc, float fs) {
        fc = clampf(fc, 10.0f, 0.45f * fs);
        const float g = tanf((float)kPi * fc / fs);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void run(float x, float &lo, float &hi) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lo = v2;
        hi = x - k * v1 - v2;
    }
    void flushState() { ic1 = flush(ic1); ic2 = flush(ic2); }
    void reset() { ic1 = ic2 = 0; }
};

// the loop can never run away: linear up to 0.9, then a smooth ceiling at 1.0
inline float loopLimit(float x) {
    const float a = fabsf(x);
    if (a < 0.9f) return x;
    const float t = 0.1f * tanhf((a - 0.9f) * 10.0f);
    return x < 0 ? -(0.9f + t) : 0.9f + t;
}

// soft ceiling for the wet bus: linear to 0.8, then a smooth approach to 1.0
inline float wetCeiling(float x) {
    const float a = fabsf(x);
    if (a < 0.8f) return x;
    const float t = 0.2f * tanhf((a - 0.8f) * 5.0f);
    return x < 0 ? -(0.8f + t) : 0.8f + t;
}

inline float hermite(float xm1, float x0, float x1, float x2, float t) {
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
}  // namespace

struct DelayDSP::Impl {
    float *buf[2];
    int wi = 0;
    double dCur[2] = {0, 0};          // smoothed delay (samples), the second of two cascaded one-poles
    double dMid[2] = {0, 0};          // ... and the first (two stages keep the pitch bend free of kinks when the knob steps)
    Svf hp[2], lp[2];
    float tiltLo[2] = {0, 0};
    float phase = 0;                  // LFO, 0..1
    // smoothed parameters
    float mk = 1, fb = 0, mixDry = 1, mixWet = 0, drv = 1, bias = 0, tilt = 0, width = 1, pp = 0, depthS = 0, rateS = 0.5f;
    float hpHz = 50, lpHz = 11000;
    float gl = 1, gh = 1;             // tilt gains (updated with the filters)
    float offL = 0, offR = 0;         // smoothed offset factors (delay = Time x (1 + off))
    int sub = 0;

    Impl() { buf[0] = (float *)calloc(kBufLen, sizeof(float)); buf[1] = (float *)calloc(kBufLen, sizeof(float)); }
    ~Impl() { free(buf[0]); free(buf[1]); }
};

DelayDSP::DelayDSP() : d(new Impl) {
    for (int i = 0; i < P_COUNT; ++i) pos_[i] = kDefaultPos[i];
}
DelayDSP::~DelayDSP() { delete d; }

void DelayDSP::init(float sampleRate) {
    fs_ = sampleRate > 8000.0f ? sampleRate : 44100.0f;
    clear();
    // start every smoothed value on its target, so a fresh instance doesn't glide in from nothing
    Impl &s = *d;
    s.fb = DelayMap::feedbackPct(pos_[P_FEEDBACK]) * 0.01f;
    s.hpHz = DelayMap::hpHz(pos_[P_HP]);
    s.lpHz = DelayMap::lpHz(pos_[P_LP]);
    for (int c = 0; c < 2; ++c) { s.hp[c].tune(s.hpHz, fs_); s.lp[c].tune(s.lpHz, fs_); }
    const float m = DelayMap::mixPct(pos_[P_MIX]) * 0.01f;
    s.mixDry = cosf(0.5f * (float)kPi * m);
    s.mixWet = sinf(0.5f * (float)kPi * m);
    s.width = DelayMap::spreadPct(pos_[P_SPREAD]) * 0.01f;
    s.depthS = DelayMap::depthPct(pos_[P_DEPTH]) * 0.01f;
    s.rateS = DelayMap::rateHz(pos_[P_RATE]);
    const float dr = DelayMap::drivePct(pos_[P_DRIVE]) * 0.01f;
    s.drv = 1.0f + 11.0f * powf(dr, 1.3f);
    s.bias = 0.25f * dr;
    s.mk = sqrtf(s.drv);
    s.tilt = DelayMap::tonePct(pos_[P_TONE]) * 0.01f * 6.0f;
    s.pp = pingPong_ ? 1.0f : 0.0f;
    for (int c = 0; c < 2; ++c) s.dCur[c] = s.dMid[c] = delaySamples(c);
    s.offL = s.offR = 0;
    if (offsetOn_) {
        const float o = DelayMap::offsetPct(pos_[P_OFFSET]) * 0.01f;
        s.offR = o > 0 ? o : 0; s.offL = o < 0 ? -o : 0;
        for (int c = 0; c < 2; ++c) s.dCur[c] = s.dMid[c] = delaySamples(c);
    }
}

void DelayDSP::clear() {
    Impl &s = *d;
    memset(s.buf[0], 0, sizeof(float) * kBufLen);
    memset(s.buf[1], 0, sizeof(float) * kBufLen);
    for (int c = 0; c < 2; ++c) { s.hp[c].reset(); s.lp[c].reset(); s.tiltLo[c] = 0; }
    s.wi = 0; s.phase = 0; s.sub = 0;
}

void DelayDSP::setParam(int id, float pos) {
    if (id < 0 || id >= P_COUNT) return;
    pos_[id] = clampf(pos, 0.0f, 1000.0f);
}
void DelayDSP::setSync(bool on) { sync_ = on; }
void DelayDSP::setOffsetOn(bool on) { offsetOn_ = on; }
void DelayDSP::setPingPong(bool on) { pingPong_ = on; }
void DelayDSP::setTempo(double bpm) { bpm_ = (bpm > 20.0 && bpm < 400.0) ? bpm : 0.0; }

float DelayDSP::timeMsNow() const {
    if (!sync_) return DelayMap::timeMs(pos_[P_TIME]);
    const double bpm = bpm_ > 0 ? bpm_ : 120.0;
    return (float)(kDivBeats[DelayMap::divIndex(pos_[P_TIME])] * 60000.0 / bpm);
}

float DelayDSP::delaySamples(int ch) const {
    float t = timeMsNow() * 0.001f * fs_;
    if (offsetOn_) {
        const float o = DelayMap::offsetPct(pos_[P_OFFSET]) * 0.01f;   // + = right side later, - = left side later
        t *= 1.0f + (ch == 1 ? (o > 0 ? o : 0) : (o < 0 ? -o : 0));
    }
    return clampf(t, 8.0f, (float)(kBufLen - 8 - 600));
}

void DelayDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n) {
    Impl &s = *d;
    const float fs = fs_;
    const float mask = (float)(kBufLen - 1);
    (void)mask;
    // per-sample one-pole coefficients
    const float aFast = 1.0f - expf(-1.0f / (0.010f * fs));    // 10 ms: levels, switches
    const float aTime = 1.0f - expf(-1.0f / (0.060f * fs));    // 2 x 60 ms: Time glides like a tape head
    const float aSlow = 1.0f - expf(-1.0f / (0.030f * fs));
    const float tiltA = 1.0f - expf(-2.0f * (float)kPi * 800.0f / fs);
    const float tMk = sqrtf(1.0f + 11.0f * powf(DelayMap::drivePct(pos_[P_DRIVE]) * 0.01f, 1.3f));
    const float lfoMaxDevSamples = 0.010f * fs;                 // +-10 ms at 100 % depth

    // targets (block rate; the one-poles below do the gliding)
    const float tFb = DelayMap::feedbackPct(pos_[P_FEEDBACK]) * 0.01f;
    const float tHp = DelayMap::hpHz(pos_[P_HP]), tLp = DelayMap::lpHz(pos_[P_LP]);
    const float m = DelayMap::mixPct(pos_[P_MIX]) * 0.01f;
    const float tDry = cosf(0.5f * (float)kPi * m), tWet = sinf(0.5f * (float)kPi * m);
    const float tW = DelayMap::spreadPct(pos_[P_SPREAD]) * 0.01f;
    const float tDepth = DelayMap::depthPct(pos_[P_DEPTH]) * 0.01f;
    const float tRate = DelayMap::rateHz(pos_[P_RATE]);
    const float dr = DelayMap::drivePct(pos_[P_DRIVE]) * 0.01f;
    const float tDrv = 1.0f + 11.0f * powf(dr, 1.3f), tBias = 0.25f * dr;
    const float tTilt = DelayMap::tonePct(pos_[P_TONE]) * 0.01f * 6.0f;
    const float tPp = pingPong_ ? 1.0f : 0.0f;
    float tOffL = 0, tOffR = 0;
    if (offsetOn_) {
        const float o = DelayMap::offsetPct(pos_[P_OFFSET]) * 0.01f;
        tOffR = o > 0 ? o : 0; tOffL = o < 0 ? -o : 0;
    }
    const double baseSamples = (double)timeMsNow() * 0.001 * fs;
    const double maxD = (double)(kBufLen - 8 - 600);

    for (int i = 0; i < n; ++i) {
        // ---- glide everything the sound depends on ----
        s.fb += aFast * (tFb - s.fb);
        s.mixDry += aFast * (tDry - s.mixDry);
        s.mixWet += aFast * (tWet - s.mixWet);
        s.width += aFast * (tW - s.width);
        s.pp += aFast * (tPp - s.pp);
        s.drv += aFast * (tDrv - s.drv);
        s.bias += aFast * (tBias - s.bias);
        s.mk += aFast * (tMk - s.mk);   // makeup for the output only (the loop keeps unity small-signal gain)
        s.tilt += aFast * (tTilt - s.tilt);
        s.depthS += aSlow * (tDepth - s.depthS);
        s.rateS += aSlow * (tRate - s.rateS);
        s.offL += aTime * (tOffL - s.offL);
        s.offR += aTime * (tOffR - s.offR);
        s.hpHz += aSlow * (tHp - s.hpHz);
        s.lpHz += aSlow * (tLp - s.lpHz);
        if ((s.sub++ & 15) == 0) {
            for (int c = 0; c < 2; ++c) { s.hp[c].tune(s.hpHz, fs); s.lp[c].tune(s.lpHz, fs); }
            s.gl = powf(10.0f, -s.tilt * 0.05f);
            s.gh = powf(10.0f, s.tilt * 0.05f);
        }
        // ---- LFO: quadrature pair ----
        s.phase += s.rateS / fs;
        if (s.phase >= 1.0f) s.phase -= 1.0f;
        float lfo[2] = {0.0f, 0.0f};
        if (s.depthS > 1e-5f || tDepth > 0.0f) { lfo[0] = sinf(2.0f * (float)kPi * s.phase); lfo[1] = cosf(2.0f * (float)kPi * s.phase); }
        const float depth = s.depthS * sqrtf(s.depthS);           // 100 % = +-10 ms, finer at the low end
        // ---- read both lines ----
        float y[2];
        for (int c = 0; c < 2; ++c) {
            const double target = std::min(maxD, std::max(8.0, baseSamples * (1.0 + (c ? s.offR : s.offL))));
            // note: offL/offR already glide; the base glides here
            // glide like a tape head, but never faster than an octave up / down (a preset jump from 1.4 s to 90 ms sweeps, it doesn't shriek)
            s.dMid[c] += (double)aTime * (target - s.dMid[c]);
            double step = (double)aTime * (s.dMid[c] - s.dCur[c]);
            s.dCur[c] += step < -0.9 ? -0.9 : step > 0.45 ? 0.45 : step;
            double dd = s.dCur[c] + (double)(depth * lfoMaxDevSamples * lfo[c]);
            if (dd < 4.0) dd = 4.0;
            if (dd > maxD) dd = maxD;
            const int id = (int)dd;
            const float fr = (float)(dd - id);
            const float *b = s.buf[c];
            const int w = s.wi;
            const float xm1 = b[(w - (id - 1)) & (kBufLen - 1)], x0 = b[(w - id) & (kBufLen - 1)];
            const float x1 = b[(w - (id + 1)) & (kBufLen - 1)], x2 = b[(w - (id + 2)) & (kBufLen - 1)];
            float v = hermite(xm1, x0, x1, x2, fr);
            // ---- Drive: tube/tape saturation (bias adds even harmonics), unity small-signal gain ----
            if (s.drv > 1.0005f) {
                const float g = s.drv, bi = s.bias;
                const float tb = tanhf(bi);
                v = (tanhf(g * v + bi) - tb) / (g * (1.0f - tb * tb));   // small-signal gain exactly 1: the loop stays stable
            }
            // ---- High Pass, Low Pass, Tone (tilt around 800 Hz) ----
            float lo, hi;
            s.hp[c].run(v, lo, hi);
            v = hi;
            s.lp[c].run(v, lo, hi);
            v = lo;
            s.tiltLo[c] += tiltA * (v - s.tiltLo[c]);
            v = s.tiltLo[c] * s.gl + (v - s.tiltLo[c]) * s.gh;
            y[c] = v;
        }
        // ---- feedback (straight or crossed) and write ----
        const float xL = inL[i], xR = inR[i];
        const float fbL = (1.0f - s.pp) * y[0] + s.pp * y[1];
        const float fbR = (1.0f - s.pp) * y[1] + s.pp * y[0];
        const float monoIn = 0.5f * (xL + xR);
        const float wL = (1.0f - s.pp) * xL + s.pp * monoIn + s.fb * loopLimit(fbL);
        const float wR = (1.0f - s.pp) * xR + s.fb * loopLimit(fbR);
        s.buf[0][s.wi] = flush(wL);
        s.buf[1][s.wi] = flush(wR);
        s.wi = (s.wi + 1) & (kBufLen - 1);
        // ---- Spread (mid/side) and Mix (equal power) ----
        const float mid = 0.5f * (y[0] + y[1]), side = 0.5f * (y[0] - y[1]) * s.width;
        outL[i] = s.mixDry * xL + s.mixWet * wetCeiling(s.mk * (mid + side));
        outR[i] = s.mixDry * xR + s.mixWet * wetCeiling(s.mk * (mid - side));
    }
    for (int c = 0; c < 2; ++c) { s.hp[c].flushState(); s.lp[c].flushState(); s.tiltLo[c] = flush(s.tiltLo[c]); }
}

}  // namespace delay
