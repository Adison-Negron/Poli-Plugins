// Poli-q DSP (see PoliQ.h).
#include "PoliQ.h"

#include <cmath>
#include <cstring>

namespace pq {

const char *const kTypeNames[kTypes] = {"Bell", "Low Shelf", "High Shelf", "Low Cut 6", "Low Cut 12", "Low Cut 24",
                                        "Low Cut 48", "High Cut 6", "High Cut 12", "High Cut 24", "High Cut 48",
                                        "Notch", "Band Pass", "Tilt"};

static const float kPi = 3.14159265358979f;
static const float kLnF = 6.90775528f;     // ln(1000): the frequency span
static const float kLnQ = 5.19295685f;     // ln(180):  the Q span

float posToHz(float p) { return kFLo * expf(kLnF * p * 0.01f); }
float hzToPos(float hz) { return 100.0f * logf(hz / kFLo) / kLnF; }
float posToQ(float p) { return kQLo * expf(kLnQ * p * 0.01f); }
float qToPos(float q) { return 100.0f * logf(q / kQLo) / kLnQ; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- design ---------------------------------------------------------------------------------------------------------
static void svf(Coefs &c, float g, float k, float m0, float m1, float m2) {
    c.order = 2;
    c.g = g;
    c.k = k;
    c.a1 = 1.0f / (1.0f + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    c.m0 = m0;
    c.m1 = m1;
    c.m2 = m2;
}
static void onePole(Coefs &c, float g, bool high) {
    c.order = 1;
    c.g = g;
    c.a1 = g / (1.0f + g);   // trapezoidal one-pole gain
    c.k = 0;
    c.m0 = high ? 1.0f : 0.0f;
    c.m1 = 0;
    c.m2 = high ? -1.0f : 1.0f;
}

// Butterworth stage Qs (2nd-order sections) for 4th and 8th order
static const float kBw4[2] = {0.54119610f, 1.30656296f};
static const float kBw8[4] = {0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f};

void design(int type, float f, float gainDb, float q, Design &d) {
    f = clampf(f, kFLo, kFHi);
    q = clampf(q, kQLo, kQHi);
    gainDb = clampf(gainDb, -kGainMax, kGainMax);
    const float g = tanf(kPi * f / kFs);
    d.n = 1;
    d.post = 1.0f;
    const float A = powf(10.0f, gainDb / 40.0f);       // sqrt of the linear gain
    const float qs = q / 0.70710678f;                   // resonance factor for the Butterworth cascades
    switch (type) {
    case kBell: svf(d.st[0], g, 1.0f / (q * A), 1, (1.0f / (q * A)) * (A * A - 1), 0); break;
    case kLowShelf: {
        const float k = 1.0f / q;
        svf(d.st[0], g / sqrtf(A), k, 1, k * (A - 1), A * A - 1);
        break;
    }
    case kHighShelf: {
        const float k = 1.0f / q;
        svf(d.st[0], g * sqrtf(A), k, A * A, k * (1 - A) * A, 1 - A * A);
        break;
    }
    case kTilt: {   // a high shelf of twice the gain, centred: highs +gain, lows -gain, unchanged at f
        const float A2 = A * A;                         // sqrt of the shelf's linear gain (2 x gainDb)
        const float k = 1.0f / q;
        svf(d.st[0], g * sqrtf(A2), k, A2 * A2, k * (1 - A2) * A2, 1 - A2 * A2);
        d.post = 1.0f / A2;
        break;
    }
    case kLowCut6: onePole(d.st[0], g, true); break;
    case kHighCut6: onePole(d.st[0], g, false); break;
    case kLowCut12: { const float k = 1.0f / q; svf(d.st[0], g, k, 1, -k, -1); break; }
    case kHighCut12: svf(d.st[0], g, 1.0f / q, 0, 0, 1); break;
    case kLowCut24: case kHighCut24: case kLowCut48: case kHighCut48: {
        const bool high = type == kLowCut24 || type == kLowCut48;   // a high-pass
        const bool eight = type == kLowCut48 || type == kHighCut48;
        const int n = eight ? 4 : 2;
        const float *bq = eight ? kBw8 : kBw4;
        d.n = n;
        for (int i = 0; i < n; ++i) {
            float sq = bq[i];
            if (i == n - 1) sq = clampf(sq * qs, 0.1f, 30.0f);   // resonance on the last (sharpest) stage
            const float k = 1.0f / sq;
            if (high) svf(d.st[i], g, k, 1, -k, -1);
            else svf(d.st[i], g, k, 0, 0, 1);
        }
        break;
    }
    case kNotch: { const float k = 1.0f / q; svf(d.st[0], g, k, 1, -k, 0); break; }
    case kBandPass: {   // unity at the centre, times the gain
        const float k = 1.0f / q;
        svf(d.st[0], g, k, 0, k, 0);
        d.post = A * A;
        break;
    }
    default: d.n = 0; break;
    }
}

float responseDb(const Design &d, float hz) {
    const float t = tanf(kPi * clampf(hz, 1.0f, 22000.0f) / kFs);
    double mag2 = (double)d.post * d.post;
    for (int i = 0; i < d.n; ++i) {
        const Coefs &c = d.st[i];
        const double w = t / c.g;   // s = j w (normalised, prewarped)
        double hr, hi;
        if (c.order == 1) {   // low = 1 / (1 + s)
            const double den = 1 + w * w;
            const double lr = 1 / den, li = -w / den;
            hr = c.m0 + c.m2 * lr;
            hi = c.m2 * li;
        } else {             // H = m0 + (m1 s + m2) / (s^2 + k s + 1)
            const double dr = 1 - w * w, di = c.k * w;
            const double nr = c.m2, ni = c.m1 * w;
            const double den = dr * dr + di * di;
            hr = c.m0 + (nr * dr + ni * di) / den;
            hi = (ni * dr - nr * di) / den;
        }
        mag2 *= hr * hr + hi * hi;
    }
    return (float)(10.0 * log10(mag2 + 1e-30));
}

float bandDb(int type, float f, float gainDb, float q, float hz) {
    Design d;
    design(type, f, gainDb, q, d);
    return responseDb(d, hz);
}

// ---- band -----------------------------------------------------------------------------------------------------------
static const int kFade = 441;                    // 10 ms
static const float kGlide = 0.0700f;             // 1 - exp(-16 / 220.5): 5 ms per stage at a 16-sample tick; two
                                                 // stages in a row (critically damped) start without a corner

void Band::reset() {
    slot_[0] = Slot();
    slot_[1] = Slot();
    cur_ = 0;
    fade_ = 0;
    first_ = true;
}

// New coefficients for the slot. ramp: reach them over the next 16 samples by linear steps (the stages' structure is
// the same while a type stays, so every coefficient interpolates); otherwise take them at once.
void Band::retune(Slot &s, bool ramp) {
    if (!s.on) { s.d.n = 0; s.d.post = 1; s.ramp = 0; return; }
    design(s.type, expf(lf_), gain_ + dynApplied_, expf(lq_), s.to);
    if (!ramp || s.to.n != s.d.n) { s.d = s.to; s.ramp = 0; return; }
    const float inv = 1.0f / 16;
    for (int i = 0; i < s.d.n; ++i) {
        const Coefs &a = s.d.st[i], &b = s.to.st[i];
        Coefs &st = s.step[i];
        st.a1 = (b.a1 - a.a1) * inv; st.a2 = (b.a2 - a.a2) * inv; st.a3 = (b.a3 - a.a3) * inv;
        st.m0 = (b.m0 - a.m0) * inv; st.m1 = (b.m1 - a.m1) * inv; st.m2 = (b.m2 - a.m2) * inv;
        s.d.st[i].g = b.g; s.d.st[i].k = b.k; s.d.st[i].order = b.order;
    }
    s.postStep = (s.to.post - s.d.post) * inv;
    s.ramp = 16;
}

inline void Band::advance(Slot &s) {
    if (s.ramp <= 0) return;
    if (--s.ramp == 0) { s.d = s.to; return; }
    for (int i = 0; i < s.d.n; ++i) {
        Coefs &c = s.d.st[i];
        const Coefs &st = s.step[i];
        c.a1 += st.a1; c.a2 += st.a2; c.a3 += st.a3; c.m0 += st.m0; c.m1 += st.m1; c.m2 += st.m2;
    }
    s.d.post += s.postStep;
}

void Band::set(const BandSettings &in) {
    BandSettings s = in;
    s.type = s.type < 0 ? 0 : s.type >= kTypes ? kTypes - 1 : s.type;
    s.hz = clampf(s.hz, kFLo, kFHi);
    s.gain = clampf(s.gain, -kGainMax, kGainMax);
    s.q = clampf(s.q, kQLo, kQHi);
    const bool structural = first_ || s.type != tgt_.type || s.on != tgt_.on;
    tgt_ = s;
    if (first_) {
        lf_ = lf1_ = logf(s.hz);
        gain_ = gain1_ = s.gain;
        lq_ = lq1_ = logf(s.q);
        Slot &a = slot_[cur_];
        a.type = s.type;
        a.on = s.on;
        a.clear();
        retune(a, false);
        first_ = false;
        return;
    }
    if (structural) {
        // a change during a crossfade: drop the old slot, fade from the one fading in
        const int nw = 1 - cur_;
        Slot &b = slot_[nw];
        b.type = s.type;
        b.on = s.on;
        b.clear();
        retune(b, false);
        cur_ = nw;
        fade_ = kFade;
    }
}

void Band::tick16() {
    const float tf = logf(tgt_.hz), tq = logf(tgt_.q);
    bool moved = false;
    auto glide = [&moved](float &v1, float &v, float t, float eps) {
        if (v == t && v1 == t) return;
        moved = true;
        if (fabsf(t - v1) < eps && fabsf(t - v) < eps) { v1 = v = t; return; }
        v1 += (t - v1) * kGlide;
        v += (v1 - v) * kGlide;
    };
    glide(lf1_, lf_, tf, 1e-4f);
    glide(gain1_, gain_, tgt_.gain, 1e-3f);
    glide(lq1_, lq_, tq, 1e-4f);
    if (fabsf(dyn_ - dynApplied_) > 0.01f || (dyn_ == 0 && dynApplied_ != 0)) { dynApplied_ = dyn_; moved = true; }
    if (moved) {
        retune(slot_[cur_], true);
        if (fade_ > 0) retune(slot_[1 - cur_], true);
    }
}

inline float Band::run(Slot &s, float x, int ch) {
    for (int i = 0; i < s.d.n; ++i) {
        const Coefs &c = s.d.st[i];
        float &ic1 = s.s1[i][ch], &ic2 = s.s2[i][ch];
        if (c.order == 1) {
            const float v = (x - ic1) * c.a1;
            const float lp = v + ic1;
            ic1 = lp + v;
            x = c.m0 * x + c.m2 * lp;
        } else {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            x = c.m0 * x + c.m1 * v1 + c.m2 * v2;
        }
    }
    return x * s.d.post;
}

inline void Band::process(float &l, float &r) {
    Slot &a = slot_[cur_];
    if (fade_ <= 0) {
        if (a.d.n == 0 && a.d.post == 1.0f) return;   // off: a straight wire
        l = run(a, l, 0);
        r = run(a, r, 1);
        advance(a);
        return;
    }
    Slot &o = slot_[1 - cur_];
    const float nl = run(a, l, 0), nr = run(a, r, 1);
    const float ol = run(o, l, 0), orr = run(o, r, 1);
    advance(a);
    advance(o);
    const float t = 1.0f - (float)fade_ / kFade;
    const float w = t * t * (3 - 2 * t);
    l = ol + (nl - ol) * w;
    r = orr + (nr - orr) * w;
    --fade_;
}

void Band::flushDenormals() {
    for (Slot &s : slot_)
        for (int i = 0; i < kMaxStages; ++i)
            for (int c = 0; c < 2; ++c) {
                if (fabsf(s.s1[i][c]) < 1e-15f) s.s1[i][c] = 0;
                if (fabsf(s.s2[i][c]) < 1e-15f) s.s2[i][c] = 0;
            }
}

// ---- eq -------------------------------------------------------------------------------------------------------------
void Eq::init() {
    for (Band &b : band_) b.reset();
    for (Det &d : det_) d = Det();
    bypass_ = false;
    wet_ = 1.0f;
    sub_ = 0;
}

// the detector of a dynamic band: a Simper SVF on the input (mono), its response chosen by the band's type
void Eq::detDesign(int b) {
    Det &d = det_[b];
    const BandSettings &s = band_[b].target();
    d.active = d.s.rangeDb != 0.0f && s.on && typeHasDyn(s.type);
    if (!d.active) { d.delta = 0; d.env = 0; d.ic1 = d.ic2 = 0; band_[b].setDyn(0); return; }
    const int mode = s.type == kLowShelf ? 2 : s.type == kHighShelf ? 3 : s.type == kTilt ? 4 : 1;
    const float q = mode == 1 ? clampf(s.q, 0.5f, 4.0f) : 0.7071f;
    const float g = tanf(kPi * clampf(s.hz, kFLo, kFHi) / kFs);
    d.k = 1.0f / q;
    d.a1 = 1.0f / (1.0f + g * (g + d.k));
    d.a2 = g * d.a1;
    d.a3 = g * d.a2;
    if (mode != d.mode) { d.ic1 = d.ic2 = 0; d.mode = mode; }
    d.atk = 1.0f - expf(-1.0f / (clampf(d.s.attMs, 0.5f, 200.0f) * 0.001f * kFs));
    d.rel = 1.0f - expf(-1.0f / (clampf(d.s.relMs, 10.0f, 2000.0f) * 0.001f * kFs));
}

void Eq::setDynamics(int b, const DynSettings &in) {
    DynSettings s = in;
    s.thrDb = clampf(s.thrDb, -60.0f, 0.0f);
    s.rangeDb = clampf(s.rangeDb, -kGainMax, kGainMax);
    s.attMs = clampf(s.attMs, 0.5f, 200.0f);
    s.relMs = clampf(s.relMs, 10.0f, 2000.0f);
    det_[b].s = s;
    detDesign(b);
}

void Eq::process(const float *inL, const float *inR, float *outL, float *outR, int n) {
    const float step = 1.0f / kFade;
    bool anyDyn = false;
    for (const Det &d : det_) anyDyn |= d.active;
    for (int i = 0; i < n; ++i) {
        if (sub_ <= 0) {
            if (anyDyn)   // the dynamic gains, from the detectors' levels, every 16 samples
                for (int b = 0; b < kBands; ++b) {
                    Det &d = det_[b];
                    if (!d.active) continue;
                    const float lev = 20.0f * log10f(d.env + 1e-9f);
                    const float over = clampf((lev - d.s.thrDb) / kDynKneeDb, 0.0f, 1.0f);
                    d.delta = d.s.rangeDb * over;
                    band_[b].setDyn(d.delta);
                }
            for (Band &b : band_) b.tick16();
            sub_ = 16;
        }
        --sub_;
        float l = inL[i], r = inR[i];
        if (anyDyn) {
            const float x = 0.5f * (l + r);
            for (Det &d : det_) {
                if (!d.active) continue;
                float y;
                if (d.mode == 4) y = x;
                else {
                    const float v3 = x - d.ic2;
                    const float v1 = d.a1 * d.ic1 + d.a2 * v3;
                    const float v2 = d.ic2 + d.a2 * d.ic1 + d.a3 * v3;
                    d.ic1 = 2 * v1 - d.ic1;
                    d.ic2 = 2 * v2 - d.ic2;
                    y = d.mode == 1 ? d.k * v1 : d.mode == 2 ? v2 : x - d.k * v1 - v2;
                }
                const float a = fabsf(y);
                d.env += (a > d.env ? d.atk : d.rel) * (a - d.env);
            }
        }
        const float dl = l, dr = r;
        for (Band &b : band_) b.process(l, r);
        const float tw = bypass_ ? 0.0f : 1.0f;
        if (wet_ != tw) wet_ = fabsf(tw - wet_) <= step ? tw : wet_ + (tw > wet_ ? step : -step);
        if (wet_ >= 1.0f) { outL[i] = l; outR[i] = r; }
        else if (wet_ <= 0.0f) { outL[i] = dl; outR[i] = dr; }
        else {
            const float w = wet_ * wet_ * (3 - 2 * wet_);
            outL[i] = dl + (l - dl) * w;
            outR[i] = dr + (r - dr) * w;
        }
    }
    for (Band &b : band_) b.flushDenormals();
    for (Det &d : det_) {
        if (fabsf(d.ic1) < 1e-15f) d.ic1 = 0;
        if (fabsf(d.ic2) < 1e-15f) d.ic2 = 0;
        if (d.env < 1e-12f) d.env = 0;
    }
}

// ---- analyzer -------------------------------------------------------------------------------------------------------
void Analyzer::init(int cols) {
    cols_ = cols > 64 ? 64 : cols;
    for (int i = 0; i < kN; ++i) win_[i] = 0.5f - 0.5f * cosf(2 * kPi * i / kN);
    for (int i = 0; i < kN / 2; ++i) { cosT_[i] = cosf(2 * kPi * i / kN); sinT_[i] = -sinf(2 * kPi * i / kN); }
    int bits = 0;
    while ((1 << bits) < kN) ++bits;
    for (int i = 0; i < kN; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[i] = r;
    }
    const float df = kFs / kN;
    for (int j = 0; j < cols_; ++j) {
        const float e0 = kFLo * powf(1000.0f, (float)j / cols_), e1 = kFLo * powf(1000.0f, (float)(j + 1) / cols_);
        fc_[j] = sqrtf(e0 * e1);
        lo_[j] = (int)ceilf(e0 / df);
        hi_[j] = (int)floorf(e1 / df);
    }
    clear();
}

void Analyzer::clear() {
    memset(buf_, 0, sizeof buf_);
    memset(lev_, 0, sizeof lev_);
    wpos_ = 0;
    sinceHop_ = 0;
}

void Analyzer::setRunning(bool on) {
    if (on == running_) return;
    running_ = on;
    if (on) { memset(buf_, 0, sizeof buf_); wpos_ = 0; sinceHop_ = 0; }
}

void Analyzer::push(const float *l, const float *r, int n) {
    if (!running_) return;
    for (int i = 0; i < n; ++i) {
        buf_[wpos_] = 0.5f * (l[i] + r[i]);
        wpos_ = (wpos_ + 1) & (kN - 1);
        if (++sinceHop_ >= kHop) { sinceHop_ = 0; analyse(); }
    }
}

void Analyzer::analyse() {
    for (int i = 0; i < kN; ++i) {
        const int j = rev_[i];
        re_[j] = buf_[(wpos_ + i) & (kN - 1)] * win_[i];
        im_[j] = 0;
    }
    for (int len = 2; len <= kN; len <<= 1) {
        const int half = len >> 1, step = kN / len;
        for (int i = 0; i < kN; i += len)
            for (int k = 0; k < half; ++k) {
                const float wr = cosT_[k * step], wi = sinT_[k * step];
                const int a = i + k, b = a + half;
                const float tr = re_[b] * wr - im_[b] * wi, ti = re_[b] * wi + im_[b] * wr;
                re_[b] = re_[a] - tr;
                im_[b] = im_[a] - ti;
                re_[a] += tr;
                im_[a] += ti;
            }
    }
    const float norm = 16.0f / ((float)kN * kN);   // a full-scale sine reads 0 dB (Hann: peak |X| = N/4)
    const float df = kFs / kN;
    const float fall = 30.0f * kHop / kFs;            // dB per analysis
    for (int j = 0; j < cols_; ++j) {
        float p = 0;
        if (hi_[j] >= lo_[j]) {
            for (int k = lo_[j]; k <= hi_[j] && k < kN / 2; ++k) {
                const float v = re_[k] * re_[k] + im_[k] * im_[k];
                if (v > p) p = v;
            }
        } else {   // narrower than a bin (the lowest columns): interpolate at the centre
            const float x = fc_[j] / df;
            const int k = (int)x;
            const float t = x - k;
            const float a = re_[k] * re_[k] + im_[k] * im_[k], b = re_[k + 1] * re_[k + 1] + im_[k + 1] * im_[k + 1];
            p = a + (b - a) * t;
        }
        float db = 10.0f * log10f(p * norm + 1e-20f) + 4.5f * log2f(fc_[j] / 1000.0f);
        float lev = (db - kSpecLo) / (kSpecHi - kSpecLo);
        lev = clampf(lev, 0, 1);
        const float prev = lev_[j];
        const float down = fall / (kSpecHi - kSpecLo);
        lev_[j] = lev >= prev ? lev : (prev - down > lev ? prev - down : lev);
    }
}

}  // namespace pq
