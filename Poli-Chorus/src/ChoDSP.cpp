#include "ChoDSP.h"
#include <cstring>
#include <algorithm>

namespace cho {

namespace {
const float kPi = 3.14159265358979f;
const int kLen = 4096, kMask = kLen - 1;      // delay line per channel (93 ms; the longest tap is ~16 ms)
const int kCtl = 16;                          // controls are re-derived every 16 samples (the knobs themselves are smoothed per sample)
const int kVoices[M_COUNT] = {2, 3, 1};
const float kBaseMs[M_COUNT][3] = {{7.5f, 11.5f, 0.0f}, {9.5f, 11.0f, 12.5f}, {6.0f, 0.0f, 0.0f}};
const float kDepthMs[M_COUNT] = {3.5f, 4.5f, 4.0f};
const float kVoiceGain[M_COUNT] = {0.55f, 0.36f, 1.0f};   // inside the loop (keeps the loop gain under 1 at Feedback 100 %)
const float kWetTrim[M_COUNT] = {1.25f, 1.6f, 1.0f};      // after the loop: brings noise-like wet material back to the dry level
const int kFadeLen = 441;                                 // 10 ms mode crossfade

inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }
inline float softCeiling(float x) {   // linear to 0.9, then a smooth approach to 1.0
    const float a = fabsf(x);
    if (a < 0.9f) return x;
    const float t = 0.1f * tanhf((a - 0.9f) * 10.0f);
    return x < 0 ? -(0.9f + t) : 0.9f + t;
}
inline float loopLimit(float x) { return x / (1.0f + fabsf(x) * (1.0f / 1.5f)); }   // 1 for small signals, bounded at +-1.5

// 4-point Catmull-Rom read, d samples behind the write position w (d >= 3)
inline float readCubic(const float *b, int w, float d) {
    const float p = (float)w - d;
    const float fl = floorf(p);
    const float f = p - fl;
    const int i = (int)fl + kLen;
    const float y0 = b[(i - 1) & kMask], y1 = b[i & kMask], y2 = b[(i + 1) & kMask], y3 = b[(i + 2) & kMask];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}
}  // namespace

struct ChoDSP::Impl {
    float fs = 44100.0f;
    float tgt[P_COUNT], sm[P_COUNT];
    float smK = 0.002f;
    int mode = M_CLASSIC, prev = M_CLASSIC, fade = 0;
    int tick = 0;

    float buf[2][kLen];
    int w = 0;
    float ph = 0.0f;

    // derived every kCtl samples
    float rateInc = 0, amt = 0, fb = 0, theta = 0, side = 1, drive = 1, makeup = 1, warm = 0, lpAmt = 0, dryG = 1, wetG = 0, outG = 1;
    float lg = 0, la1 = 0, la2 = 0, la3 = 0;     // low-pass (TPT state-variable, Butterworth)
    float toneA = 0.4f, hpR = 0.99f;
    float ic1[2], ic2[2], tone[2], hpX[2], hpY[2];

    void clear() {
        memset(buf, 0, sizeof buf);
        for (int c = 0; c < 2; ++c) ic1[c] = ic2[c] = tone[c] = hpX[c] = hpY[c] = 0;
        w = 0; ph = 0; fade = 0; tick = 0;
    }
    void init(float sr, const float *pos, int m) {
        fs = sr;
        smK = 1.0f - expf(-1.0f / (0.012f * fs));
        toneA = 1.0f - expf(-2.0f * kPi * 4500.0f / fs);
        hpR = 1.0f - 2.0f * kPi * 60.0f / fs;
        for (int i = 0; i < P_COUNT; ++i) tgt[i] = sm[i] = pos[i];
        mode = prev = m;
        clear();
        derive();
    }

    void derive() {
        rateInc = ChoMap::rateHz(sm[P_RATE]) / fs;
        amt = ChoMap::norm(sm[P_AMOUNT]);
        fb = ChoMap::feedbackGain(sm[P_FEEDBACK]);
        theta = ChoMap::thetaCycles(sm[P_WIDTH]);
        side = ChoMap::sideGain(sm[P_WIDTH]);
        warm = ChoMap::norm(sm[P_WARMTH]);
        drive = 1.0f + 2.5f * warm;
        makeup = 1.0f + 0.12f * (drive - 1.0f);
        const float fc = fminf(ChoMap::lpfHz(sm[P_LPF]), 0.45f * fs);
        lg = tanf(kPi * fc / fs);
        const float k = 1.41421356f;
        la1 = 1.0f / (1.0f + lg * (lg + k));
        la2 = lg * la1;
        la3 = lg * la2;
        lpAmt = clampf((995.0f - sm[P_LPF]) * (1.0f / 25.0f), 0.0f, 1.0f);
        const float m = ChoMap::norm(sm[P_MIX]) * 1.5707963f;
        dryG = cosf(m);
        wetG = sinf(m);
        outG = powf(10.0f, ChoMap::outputDb(sm[P_OUTPUT]) * 0.05f);
    }

    // the sum of one channel's taps for a mode (before the loop's nonlinearity)
    float taps(int md, int ch) const {
        const int n = kVoices[md];
        const float cyc0 = ph - (ch ? theta : 0.0f);   // the right channel runs `theta` cycles behind
        const float depth = kDepthMs[md] * 0.001f * fs * amt;
        float s = 0;
        for (int k = 0; k < n; ++k) {
            const float off = md == M_CLASSIC ? 0.5f * k : md == M_ENSEMBLE ? (float)k * (1.0f / 3.0f) : 0.0f;
            const float d = kBaseMs[md][k] * 0.001f * fs + depth * ChoMap::lfo(md, cyc0 + off);
            s += readCubic(buf[ch], w, d);
        }
        return s * kVoiceGain[md];
    }

    void run(const float *inL, const float *inR, float *outL, float *outR, int n) {
        const float *in[2] = {inL, inR};
        float *out[2] = {outL, outR};
        for (int i = 0; i < n; ++i) {
            if (tick == 0) derive();
            if (++tick == kCtl) tick = 0;
            for (int p = 0; p < P_COUNT; ++p) sm[p] += (tgt[p] - sm[p]) * smK;
            ph += rateInc;
            if (ph >= 1.0f) ph -= 1.0f;
            float a = 1.0f, trim = kWetTrim[mode];
            if (fade > 0) {
                a = 1.0f - (float)fade * (1.0f / kFadeLen);
                trim = a * kWetTrim[mode] + (1.0f - a) * kWetTrim[prev];
                --fade;
            }
            float wet[2], dry[2];
            for (int c = 0; c < 2; ++c) {
                float raw = taps(mode, c);
                if (fade > 0 || a < 1.0f) raw = a * raw + (1.0f - a) * taps(prev, c);
                // Warmth: soft saturation with unity small-signal gain, then a little high-frequency softening
                float y = raw + warm * (tanhf(drive * raw) / drive - raw);   // Warmth 0 = linear
                tone[c] += toneA * (y - tone[c]);
                y += warm * 0.45f * (tone[c] - y);
                // LP Filter (2-pole), crossfaded to nothing at the top of the knob
                const float v3 = y - ic2[c];
                const float v1 = la1 * ic1[c] + la2 * v3;
                const float v2 = ic2[c] + la2 * ic1[c] + la3 * v3;
                ic1[c] = 2.0f * v1 - ic1[c];
                ic2[c] = 2.0f * v2 - ic2[c];
                const float f = y + lpAmt * (v2 - y);
                // feedback: high-passed (60 Hz), limited
                const float hp = hpR * (hpY[c] + f - hpX[c]);
                hpX[c] = f; hpY[c] = hp;
                dry[c] = in[c][i];
                buf[c][w] = flush(dry[c] + loopLimit(fb * hp));
                wet[c] = f * makeup * trim;
            }
            ++w; w &= kMask;
            const float M = 0.5f * (wet[0] + wet[1]), S = 0.5f * (wet[0] - wet[1]) * side;
            const float wl = M + S, wr = M - S;
            out[0][i] = softCeiling(outG * (dryG * dry[0] + wetG * wl));
            out[1][i] = softCeiling(outG * (dryG * dry[1] + wetG * wr));
        }
        // denormals: flush the filter states once per call
        for (int c = 0; c < 2; ++c) { ic1[c] = flush(ic1[c]); ic2[c] = flush(ic2[c]); tone[c] = flush(tone[c]); hpX[c] = flush(hpX[c]); hpY[c] = flush(hpY[c]); }
    }
};

ChoDSP::ChoDSP() : d(new Impl) {
    for (int i = 0; i < P_COUNT; ++i) pos_[i] = kDefaultPos[i];
    d->init(fs_, pos_, mode_);
}
ChoDSP::~ChoDSP() { delete d; }
void ChoDSP::init(float sampleRate) {
    fs_ = sampleRate;
    d->init(fs_, pos_, mode_);
}
void ChoDSP::clear() { d->clear(); }
void ChoDSP::setParam(int id, float pos) {
    if (id < 0 || id >= P_COUNT) return;
    pos_[id] = clampf(pos, 0.0f, 1000.0f);
    d->tgt[id] = pos_[id];
}
void ChoDSP::setMode(int m) {
    m = m < 0 ? 0 : m >= M_COUNT ? M_COUNT - 1 : m;
    if (m == mode_) return;
    d->prev = mode_;
    mode_ = m;
    d->mode = m;
    d->fade = kFadeLen;
}
void ChoDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n) {
    for (int done = 0; done < n;) {
        const int c = n - done < 128 ? n - done : 128;
        d->run(inL + done, inR + done, outL + done, outR + done, c);
        done += c;
    }
}

}  // namespace cho
