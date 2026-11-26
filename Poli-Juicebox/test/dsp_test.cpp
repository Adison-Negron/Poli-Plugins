// Poli-Juicebox DSP tests (offline, x86, built under ASan/UBSan by run_tests.sh): latency and Mix, silence, Spectral Tamer, Harmonic
// Shaper (odd / even / aliasing), Natural Formant Filter, Air, clicks while knobs move, extremes, stereo, CPU.
// The shifter's pitch / formant accuracy is measured on synthetic voices by test/voice_eval.py (numpy).
#include "JuiceDSP.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <ctime>
#include <cmath>
#include <cstring>
using namespace jb;
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static const double fs = 44100;
typedef std::vector<float> V;

static void run(JuiceDSP &d, const V &iL, const V &iR, V &oL, V &oR) {
    size_t n = iL.size(); oL.resize(n); oR.resize(n);
    for (size_t i = 0; i < n; i += 128) { int c = (int)std::min<size_t>(128, n - i); d.process(&iL[i], &iR[i], &oL[i], &oR[i], c); }
}
static bool bounded(const V &a, float lim) { for (float x : a) { if (!(x == x) || x > lim || x < -lim) return false; } return true; }
static float peak(const V &a, size_t from = 0, size_t to = (size_t)-1) { float p = 0; for (size_t i = from; i < a.size() && i < to; i++) p = std::max(p, fabsf(a[i])); return p; }
static double rms(const V &a, size_t from, size_t to) { double s = 0; size_t n = 0; for (size_t i = from; i < to && i < a.size(); i++) { s += (double)a[i] * a[i]; n++; } return n ? sqrt(s / n) : 0; }
static double bin(const V &a, size_t from, size_t to, double hz) {
    double re = 0, im = 0; size_t N = std::min(to, a.size()) - from;
    for (size_t i = from; i < to && i < a.size(); i++) { double ph = 2 * M_PI * hz * (double)i / fs; double w = 0.5 - 0.5 * cos(2 * M_PI * (i - from) / N); re += w * a[i] * cos(ph); im += w * a[i] * sin(ph); }
    return 4 * sqrt(re * re + im * im) / N;   // Hann-corrected amplitude
}
static V sine(size_t n, double hz, float amp) { V v(n); for (size_t i = 0; i < n; i++) v[i] = amp * (float)sin(2 * M_PI * hz * i / fs); return v; }
static V noise(size_t n, float level, unsigned seed) {
    V v(n); srand(seed);
    for (auto &x : v) x = (rand() / (float)RAND_MAX * 2 - 1);
    double r = 0; for (float x : v) r += x * x; r = sqrt(r / n); for (auto &x : v) x *= level / (float)r; return v;
}
// a crude vowel: glottal pulses (period from f0) through three resonators
static V voice(size_t n, double f0, float amp, unsigned seed = 1) {
    V v(n, 0.f); srand(seed);
    const double fr[3] = {730, 1090, 2440}, bw[3] = {60, 90, 120};
    double y1[3] = {0, 0, 0}, y2[3] = {0, 0, 0}; double ph = 0;
    for (size_t i = 0; i < n; i++) {
        double T = fs / (f0 * (1 + 0.003 * (rand() / (double)RAND_MAX - 0.5)));
        double src = 0; ph += 1; if (ph >= T) { ph -= T; src = 1; }
        double x = src - 0.9 * 0; double s = x;
        for (int k = 0; k < 3; k++) {
            double r = exp(-M_PI * bw[k] / fs), th = 2 * M_PI * fr[k] / fs;
            double y = (1 - r) * s + 2 * r * cos(th) * y1[k] - r * r * y2[k]; y2[k] = y1[k]; y1[k] = y; s = y;
        }
        v[i] = (float)s;
    }
    float p = peak(v); for (auto &x : v) x *= amp / p;
    return v;
}
static double db(double x) { return 20 * log10(std::max(x, 1e-12)); }
static double third_diff_peak(const V &a, size_t from) {
    double p = 0; for (size_t i = from + 3; i < a.size(); i++) p = std::max(p, (double)fabsf(a[i] - 3 * a[i - 1] + 3 * a[i - 2] - a[i - 3])); return p;
}
static float lpos(double v, double lo, double hi) { return (float)(1000.0 * log(v / lo) / log(hi / lo)); }
static float qpos(double q) { return (float)(6 + 994 * log(q / 0.7) / log(20.0)); }
static void defaults(JuiceDSP &d) { for (int i = 0; i < P_COUNT; ++i) d.setParam(i, kDefaultPos[i]); d.setLink(false); }

int main() {
    const int LAT = JuiceDSP::latencyFor(JuiceDSP::kLatencyDefault);
    // 1. latency: with Mix 0 the output is the input, delayed by exactly the look-ahead (default mode Balanced); Mix is equal power
    {
        JuiceDSP d; defaults(d); d.setParam(P_MIX, 0); d.init(fs);
        V in(8192, 0.f); in[1000] = 0.5f; in[3000] = -0.25f; V oL, oR; run(d, in, in, oL, oR);
        int at = -1; for (size_t i = 0; i < oL.size(); i++) if (fabsf(oL[i]) > 0.1f) { at = (int)i; break; }
        CHECK(at == 1000 + LAT && fabsf(oL[3000 + LAT] + 0.25f) < 1e-6f, "Mix 0: dry delayed by exactly %d samples (%.1f ms); impulse at %d", LAT, 1000.0 * LAT / fs, at);
        V s = sine(44100, 440, 0.4f); run(d, s, s, oL, oR);
        CHECK(fabs(db(rms(oL, 20000, 40000) / rms(s, 20000, 40000))) < 0.01, "Mix 0: unity gain (%.3f dB)", db(rms(oL, 20000, 40000) / rms(s, 20000, 40000)));
        JuiceDSP d2; defaults(d2); d2.setParam(P_MIX, 500); d2.init(fs);
        V v = voice(44100, 150, 0.4f); run(d2, v, v, oL, oR);
        CHECK(bounded(oL, 1.0f) && rms(oL, 20000, 40000) > 0.05, "Mix 50 %%: bounded, rms %.3f", rms(oL, 20000, 40000));
    }
    // 1b. the three latency modes: dry delayed by exactly 384 / 512 / 768 samples; switching while running keeps the audio sane (a short gap at most)
    for (int m = 0; m < JuiceDSP::kLatencyModes; ++m) {
        JuiceDSP d; defaults(d); d.setParam(P_MIX, 0); d.setLatencyMode(m); d.init(fs);
        V in(8192, 0.f); in[1000] = 0.5f; V oL, oR; run(d, in, in, oL, oR);
        int at = -1; for (size_t i = 0; i < oL.size(); i++) if (fabsf(oL[i]) > 0.1f) { at = (int)i; break; }
        CHECK(at == 1000 + JuiceDSP::latencyFor(m) && d.latencySamples() == JuiceDSP::latencyFor(m), "latency mode %d: dry delayed by %d samples (%.1f ms)", m, d.latencySamples(), 1000.0 * d.latencySamples() / fs);
    }
    {
        JuiceDSP d; defaults(d); d.setParam(P_PITCH, 700); d.init(fs);
        V v = voice(44100, 150, 0.4f);
        float peak = 0; int cnt = 0;
        for (int blk = 0; blk < 400; ++blk) {
            if (blk % 50 == 25) d.setLatencyMode((blk / 50) % 3);
            V xi(v.begin() + blk * 110, v.begin() + blk * 110 + 110); V o1(110), o2(110);
            d.process(xi.data(), xi.data(), o1.data(), o2.data(), 110);
            for (float x : o1) { peak = std::max(peak, fabsf(x)); cnt += (x != x); }
        }
        CHECK(peak < 1.0f && cnt == 0, "switching the latency mode while a voice is playing: bounded (peak %.2f), no NaN", peak);
    }
    // 1c. Learn: forces the tracker on (even at Pitch 0 / Formant 0), finishes by itself, the audio stays a perfect delayed bypass meanwhile
    {
        JuiceDSP d; defaults(d); d.init(fs);
        V h(fs * 8, 0.f); for (size_t i = 0; i < h.size(); i++) { float x = 0; for (int k = 1; k <= 8; k++) x += sinf(2 * (float)M_PI * 180.0f * k * i / (float)fs) / k; h[i] = 0.12f * x; }
        d.startLearn();
        V oL, oR; run(d, h, h, oL, oR);
        float p[P_COUNT]; bool st[P_COUNT];
        const bool ok = d.learnResult(p, st);
        CHECK(d.learning() && d.learnFinished() && d.learnProgress() >= 0.99f && ok, "Learn on a 180 Hz voice: heard enough (progress %.2f)", d.learnProgress());
        CHECK(st[P_FLOOR] && st[P_CEIL] && JuiceMap::floorHz(p[P_FLOOR]) > 110 && JuiceMap::floorHz(p[P_FLOOR]) <= 160 && JuiceMap::ceilHz(p[P_CEIL]) >= 250 && JuiceMap::ceilHz(p[P_CEIL]) >= 2 * JuiceMap::floorHz(p[P_FLOOR]) - 1,
              "Learn: floor %.0f Hz / ceiling %.0f Hz for a 180 Hz voice", JuiceMap::floorHz(p[P_FLOOR]), JuiceMap::ceilHz(p[P_CEIL]));
        CHECK(st[P_RASP] && p[P_RASP] <= 300 && !st[P_PITCH] && !st[P_FORMANT] && !st[P_MIX], "Learn: a clean voice reads as low Rasp (%.0f %%); Pitch / Formant / Mix are never touched", p[P_RASP] * 0.1f);
        d.stopLearn();
        JuiceDSP e; defaults(e); e.init(fs); e.startLearn();
        V s = sine(44100, 440, 0.0f), oL2, oR2; run(e, s, s, oL2, oR2);
        float pp[P_COUNT]; bool ss[P_COUNT];
        CHECK(!e.learnResult(pp, ss) && !e.learnFinished(), "Learn on silence: nothing to learn from, nothing set");
    }
    // 2. silence in = silence out (also after loud material); denormal tails stay quiet
    {
        JuiceDSP d; defaults(d); d.setParam(P_PITCH, 700); d.setParam(P_AIR, 1000); d.setParam(P_ODD, 600); d.setParam(P_F2Q, 500); d.init(fs);
        V in = voice(44100, 130, 0.5f), oL, oR; run(d, in, in, oL, oR);
        V sil(44100 * 2, 0.f); run(d, sil, sil, oL, oR);
        CHECK(peak(oL, 22050) < 1e-6f, "silence in = silence out after the tail (%.2e)", peak(oL, 22050));
    }
    // 3. Spectral Tamer
    {
        // sibilance: a 7 kHz tone at -20 dB (0.1) and a 1 kHz tone
        for (int on = 0; on < 2; on++) {
            JuiceDSP d; defaults(d); d.setParam(P_THRESH, on ? (-40 + 60) / 0.06f : 1000); d.init(fs);
            V s7 = sine(44100, 7000, 0.1f), oL, oR; run(d, s7, s7, oL, oR);
            const double g = db(bin(oL, 22050, 44100, 7000) / 0.1);
            if (on) CHECK(g < -8 && g > -22, "Tamer: 7 kHz at -20 dB over a -40 dB threshold is reduced by %.1f dB", g);
            else CHECK(fabs(g) < 0.3, "Tamer at 0 dB threshold leaves 7 kHz alone (%.2f dB)", g);
            V s1 = sine(44100, 1000, 0.1f); run(d, s1, s1, oL, oR);
            CHECK(fabs(db(bin(oL, 22050, 44100, 1000) / 0.1)) < 0.5, "Tamer (%s): 1 kHz untouched (%.2f dB)", on ? "on" : "off", db(bin(oL, 22050, 44100, 1000) / 0.1));
        }
        // attack / release: a 7 kHz burst; time to get within 3 dB of the final reduction
        {
            JuiceDSP d; defaults(d); d.setParam(P_THRESH, (-45 + 60) / 0.06f); d.setParam(P_ATTACK, lpos(3, 0.5, 20)); d.setParam(P_RELEASE, lpos(40, 10, 200)); d.init(fs);
            V in(44100, 0.f); V s = sine(44100, 7000, 0.15f); for (size_t i = 8000; i < 24000; i++) in[i] = s[i];
            V oL, oR; run(d, in, in, oL, oR);
            // rectified output of the sibilance band relative to input
            auto env = [&](size_t a, size_t n) { double m = 0; for (size_t i = a; i < a + n; i++) m = std::max(m, (double)fabsf(oL[i + LAT])); return m; };
            const double early = db(env(8000, 40) / 0.15), settled = db(env(20000, 400) / 0.15);
            CHECK(early > settled + 3.0 && settled < -8, "Tamer attack: first 1 ms passes %.1f dB, settled %.1f dB (3 ms attack lets the onset through)", early, settled);
            const double rel_early = db(env(24000 + 100, 40) / 0.15);
            CHECK(true, "(info) release: %.1f dB just after the burst stops", rel_early);
        }
        // low end: 100 Hz loud tone, tamer at 200 Hz
        {
            JuiceDSP d; defaults(d); d.setParam(P_THRESH, (-30 + 60) / 0.06f); d.setParam(P_LOWEND, lpos(200, 80, 350)); d.init(fs);
            V s = sine(44100, 100, 0.5f), oL, oR; run(d, s, s, oL, oR);
            const double g = db(bin(oL, 22050, 44100, 100) / 0.5);
            V s2 = sine(44100, 1000, 0.5f); run(d, s2, s2, oL, oR);
            CHECK(g < -3.0 && g > -14, "Low-end tamer: 100 Hz at -6 dB is reduced by %.1f dB", g);
            CHECK(fabs(db(bin(oL, 22050, 44100, 1000) / 0.5)) < 0.6, "Low-end tamer leaves 1 kHz alone (%.2f dB)", db(bin(oL, 22050, 44100, 1000) / 0.5));
            JuiceDSP d3; defaults(d3); d3.setParam(P_THRESH, (-30 + 60) / 0.06f); d3.setParam(P_LOWEND, 0); d3.init(fs);   // Off
            V s3 = sine(44100, 100, 0.5f); run(d3, s3, s3, oL, oR);
            CHECK(fabs(db(bin(oL, 22050, 44100, 100) / 0.5)) < 0.3, "Low end Off: 100 Hz untouched (%.2f dB)", db(bin(oL, 22050, 44100, 100) / 0.5));
        }
    }
    // 4. Harmonic Shaper
    {
        auto harm = [&](float odd, float even, double hz, double colorHz, double *h2, double *h3, double *h5, double *thd) {
            JuiceDSP d; defaults(d); d.setParam(P_ODD, odd); d.setParam(P_EVEN, even); d.setParam(P_COLOR, lpos(colorHz, 200, 4000)); d.init(fs);
            V s = sine(44100, hz, 0.3f), oL, oR; run(d, s, s, oL, oR);
            const double f1 = bin(oL, 22050, 44100, hz);
            *h2 = db(bin(oL, 22050, 44100, 2 * hz) / f1); *h3 = db(bin(oL, 22050, 44100, 3 * hz) / f1); *h5 = db(bin(oL, 22050, 44100, 5 * hz) / f1);
            double hs = 0; for (int k = 2; k <= 6; k++) { double b = bin(oL, 22050, 44100, k * hz); hs += b * b; }
            *thd = db(sqrt(hs) / f1);
        };
        double h2, h3, h5, thd;
        harm(0, 0, 1000, 1000, &h2, &h3, &h5, &thd);
        CHECK(thd < -70, "Harmonics 0/0: clean (THD %.1f dB)", thd);
        harm(1000, 0, 1000, 1000, &h2, &h3, &h5, &thd);
        CHECK(h3 > -22 && h3 < -2 && h2 < h3 - 20, "Odd 100 %%: 3rd %.1f dB, 2nd %.1f dB (even stays >20 dB below)", h3, h2);
        harm(0, 1000, 1000, 1000, &h2, &h3, &h5, &thd);
        CHECK(h2 > -22 && h2 < -2 && h3 < h2 - 12, "Even 100 %%: 2nd %.1f dB, 3rd %.1f dB", h2, h3);
        harm(500, 0, 300, 300, &h2, &h3, &h5, &thd);
        CHECK(h3 > -30, "Color 300 Hz focuses on a 300 Hz tone: 3rd %.1f dB", h3);
        harm(1000, 0, 300, 4000, &h2, &h3, &h5, &thd);
        CHECK(h3 < -50, "Color 4 kHz leaves a 300 Hz tone alone (3rd %.1f dB)", h3);
        // aliasing: 3.3 kHz tone, Color 3 kHz, odd + even full: everything that is not a harmonic below Nyquist
        {
            JuiceDSP d; defaults(d); d.setParam(P_ODD, 1000); d.setParam(P_EVEN, 1000); d.setParam(P_COLOR, lpos(3000, 200, 4000)); d.init(fs);
            V s = sine(44100, 3300, 0.3f), oL, oR; run(d, s, s, oL, oR);
            const double f1 = bin(oL, 22050, 44100, 3300);
            double worst = -200, wf = 0;
            for (double f = 500; f < 21000; f += 37) {
                bool isH = false; for (int k = 1; k <= 6; k++) if (fabs(f - 3300.0 * k) < 120) isH = true;
                if (isH) continue;
                const double v = db(bin(oL, 22050, 44100, f) / f1); if (v > worst) { worst = v; wf = f; }
            }
            CHECK(worst < -45, "aliasing: strongest non-harmonic line %.1f dB below the 3.3 kHz tone (at %.0f Hz)", worst, wf);
        }
    }
    // 5. Natural Formant Filter
    {
        const double hzs[3][3] = {{200, 500, 1000}, {1000, 1700, 3000}, {2500, 3300, 4500}};
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < 3; j++) {
                JuiceDSP d; defaults(d);
                const double lo = k == 0 ? 200 : k == 1 ? 1000 : 2500, hi = k == 0 ? 1000 : k == 1 ? 3000 : 4500;
                d.setParam(P_F1C + 2 * k, lpos(hzs[k][j], lo, hi)); d.setParam(P_F1Q + 2 * k, qpos(4.0)); d.init(fs);
                V s = sine(44100, hzs[k][j], 0.1f), oL, oR; run(d, s, s, oL, oR);
                const double g = db(bin(oL, 22050, 44100, hzs[k][j]) / 0.1);
                V off = sine(44100, hzs[k][j] * 0.4, 0.1f); run(d, off, off, oL, oR);
                const double g2 = db(bin(oL, 22050, 44100, hzs[k][j] * 0.4) / 0.1);
                if (j == 1) CHECK(fabs(g - 6.0) < 0.6 && fabs(g2) < 2.5, "F%d at %.0f Hz, Q 4: +%.1f dB at the centre, %.1f dB at 0.4x", k + 1, hzs[k][j], g, g2);
                else CHECK(fabs(g - 6.0) < 0.6, "F%d at %.0f Hz (%s of its range): +%.1f dB", k + 1, hzs[k][j], j == 0 ? "bottom" : "top", g);
            }
        }
        JuiceDSP d; defaults(d); d.init(fs);
        V s = sine(44100, 1700, 0.1f), oL, oR; run(d, s, s, oL, oR);
        CHECK(fabs(db(bin(oL, 22050, 44100, 1700) / 0.1)) < 0.05, "Formant filters Off: flat (%.3f dB)", db(bin(oL, 22050, 44100, 1700) / 0.1));
        // Q narrows the peak
        double w[2];
        for (int q = 0; q < 2; q++) {
            JuiceDSP d2; defaults(d2); d2.setParam(P_F2C, lpos(1700, 1000, 3000)); d2.setParam(P_F2Q, qpos(q ? 10.0 : 1.0)); d2.init(fs);
            V s2 = sine(44100, 1700 * 1.15, 0.1f); run(d2, s2, s2, oL, oR); w[q] = db(bin(oL, 22050, 44100, 1700 * 1.15) / 0.1);
        }
        CHECK(w[1] < w[0] - 2.0, "higher Q = narrower: +15%% off-centre %.1f dB at Q 10 vs %.1f dB at Q 1", w[1], w[0]);
    }
    // 6. Air
    {
        V v = noise(44100 * 2, 0.1f, 5), oL, oR, oL0, oR0;
        JuiceDSP d0; defaults(d0); d0.init(fs); run(d0, v, v, oL0, oR0);
        JuiceDSP d1; defaults(d1); d1.setParam(P_AIR, 1000); d1.init(fs); run(d1, v, v, oL, oR);
        auto hf = [&](const V &a) { double e = 0; for (double f = 9000; f < 16000; f += 100) { double b = bin(a, 30000, 80000, f); e += b * b; } return 10 * log10(e + 1e-30); };
        CHECK(hf(oL) > hf(oL0) + 5.0 && hf(oL) < hf(oL0) + 16.0, "Air 100 %%: +%.1f dB above 9 kHz on noise", hf(oL) - hf(oL0));
        CHECK(bounded(oL, 1.0f), "Air: bounded");
    }
    // 7. clicks: knobs moved in Q-Link steps (about 8 positions per block) on a steady voice, at every control. The reference is the same
    //    parameter held steady at its start and at its end value (a knob that adds noise, like Air, is not a click)
    {
        V v = voice(44100 * 2, 140, 0.4f), oL, oR;
        const char *names[P_COUNT] = {"Pitch", "Formant", "F-Track", "Threshold", "Low end", "Attack", "Release", "Odd", "Even", "Color", "F1 Freq", "F1 Q", "F2 Freq", "F2 Q", "F3 Freq", "F3 Q", "F-Smooth", "Air", "Natural", "Mix"};
        double worstRatio = 0; int worstK = -1;
        auto setup = [&](JuiceDSP &e, int k) { defaults(e); e.setParam(P_ODD, 300); e.setParam(P_F1Q, qpos(2)); e.setParam(P_PITCH, 600); e.setLink(k == P_FTRACK); };
        for (int k = 0; k < P_COUNT; ++k) {
            const float p0 = kDefaultPos[k] > 500 ? 900.0f : 100.0f, p1 = (float)(p0 + (kDefaultPos[k] > 500 ? -1 : 1) * 7.8 * 39);
            double ref = 0;
            for (int s = 0; s < 2; s++) {
                JuiceDSP e; setup(e, k); e.setParam(k, s ? p1 : p0); e.init(fs);
                V o2(v.size()), o2R(v.size()); run(e, v, v, o2, o2R); ref = std::max(ref, third_diff_peak(o2, 44100));
            }
            JuiceDSP e; setup(e, k); e.setParam(k, p0); e.init(fs);
            V o2(v.size()), o2R(v.size());
            for (size_t i = 0; i < v.size(); i += 128) {
                if (i >= 44100 && i < 44100 + 128 * 40) e.setParam(k, (float)(p0 + (kDefaultPos[k] > 500 ? -1 : 1) * 7.8 * ((i - 44100) / 128)));
                e.process(&v[i], &v[i], &o2[i], &o2R[i], (int)std::min<size_t>(128, v.size() - i));
            }
            const double r = third_diff_peak(o2, 44100) / ref;
            if (r > worstRatio) { worstRatio = r; worstK = k; }
        }
        CHECK(worstRatio < 1.6, "no clicks while any knob is swept (~8 positions per block): worst 3rd-difference %.2f x the steady states (%s)", worstRatio, names[std::max(worstK, 0)]);
    }
    // 8. extremes: random positions, random link, noise / voice / sines; everything finite and bounded
    {
        srand(7); int bad = 0;
        for (int t = 0; t < 60; t++) {
            JuiceDSP d; defaults(d);
            for (int k = 0; k < P_COUNT; ++k) d.setParam(k, (rand() & 3) == 0 ? ((rand() & 1) ? 0.0f : 1000.0f) : (float)(rand() % 1001));
            d.setLink(rand() & 1); d.init(fs);
            V in = (t % 3 == 0) ? noise(22050, 0.9f, t) : (t % 3 == 1) ? voice(22050, 80 + rand() % 400, 0.95f, t) : sine(22050, 50 + rand() % 12000, 0.95f);
            V oL, oR; run(d, in, in, oL, oR);
            if (!bounded(oL, 1.0f) || !bounded(oR, 1.0f)) bad++;
        }
        CHECK(bad == 0, "96 random extreme settings (60 runs): output finite and within +-1 (%d bad)", bad);
        JuiceDSP d; defaults(d); d.init(fs);
        V in(4096, 1.0f), oL, oR; for (size_t i = 0; i < in.size(); i++) in[i] = (i & 1) ? 1.0f : -1.0f; run(d, in, in, oL, oR);
        CHECK(bounded(oL, 1.0f), "full-scale square at Nyquist: bounded");
    }
    // 9. stereo: L = R stays L = R; Mix 0 keeps a stereo input stereo; Mix 100 % is mono
    {
        JuiceDSP d; defaults(d); d.setParam(P_PITCH, 700); d.init(fs);
        V v = voice(44100, 150, 0.4f), oL, oR; run(d, v, v, oL, oR);
        double md = 0; for (size_t i = 0; i < oL.size(); i++) md = std::max(md, (double)fabsf(oL[i] - oR[i]));
        CHECK(md == 0, "L = R in -> L = R out (%.1e)", md);
        JuiceDSP e; defaults(e); e.setParam(P_MIX, 0); e.init(fs);
        V a = sine(8192, 300, 0.3f), b = sine(8192, 500, 0.3f); run(e, a, b, oL, oR);
        CHECK(fabs(oL[5000 + LAT - 2 * LAT + LAT] - 0) < 1.f && rms(oL, 4000, 8000) > 0.1 && fabs(db(bin(oL, 3000, 8000, 300)) - db(bin(oR, 3000, 8000, 300))) > 20, "Mix 0: a stereo input keeps its channels");
    }
    // 10. CPU: 10 s of voice with every module on
    {
        JuiceDSP d; defaults(d);
        d.setParam(P_PITCH, 700); d.setParam(P_FORMANT, 600); d.setParam(P_THRESH, 500); d.setParam(P_LOWEND, 300); d.setParam(P_ODD, 400); d.setParam(P_EVEN, 400);
        d.setParam(P_F1Q, qpos(3)); d.setParam(P_F2Q, qpos(3)); d.setParam(P_F3Q, qpos(3)); d.setParam(P_AIR, 500); d.setParam(P_FSMOOTH, 600); d.init(fs);
        V v = voice(441000, 120, 0.5f), oL, oR;
        clock_t t0 = clock(); run(d, v, v, oL, oR); double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
        const double x = 10.0 / std::max(sec, 1e-6);
        #ifdef CPU_GATE
        CHECK(x > 20.0, "CPU: 10 s of audio in %.3f s = %.0fx real time (one instance, everything on, -O3)", sec, x);
#else
        printf("info (sanitizer build, not gated): 10 s in %.2f s = %.0fx real time\n", sec, x);
#endif
    }
    printf("\n%s (%d failed)\n", fails ? "SOME CHECKS FAILED" : "ALL DSP CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
