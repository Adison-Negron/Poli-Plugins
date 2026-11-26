// Poli-tea DSP tests (offline): flatness, latency, ceiling / true peak, loudness across Amount, compression, mono lock, widening,
// Input drive, Limiter, sidechain switch, stability under extremes, every preset bounded, clicks while knobs move, silence.
#include "TeaDSP.h"
#include "presets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
using namespace tea;
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static const double fs = 44100;
typedef std::vector<float> V;
static void run(TeaDSP &d, const V &iL, const V &iR, V &oL, V &oR) {
    size_t n = iL.size(); oL.resize(n); oR.resize(n);
    for (size_t i = 0; i < n; i += 128) { int c = (int)std::min<size_t>(128, n - i); d.process(&iL[i], &iR[i], &oL[i], &oR[i], c); }
}
static bool finite_bounded(const V &a, float lim) { for (float x : a) if (!(x == x) || x > lim || x < -lim) return false; return true; }
static float peak(const V &a, size_t from = 0) { float p = 0; for (size_t i = from; i < a.size(); i++) p = std::max(p, fabsf(a[i])); return p; }
static double rms(const V &a, size_t from, size_t to = (size_t)-1) { double s = 0; size_t n = 0; for (size_t i = from; i < to && i < a.size(); i++) { s += (double)a[i] * a[i]; n++; } return n ? sqrt(s / n) : 0; }
static double db(double x) { return 20 * log10(x + 1e-12); }
static V sine(size_t n, double hz, float amp) { V v(n); for (size_t i = 0; i < n; i++) v[i] = amp * (float)sin(2 * M_PI * hz * i / fs); return v; }
static V noise(size_t n, float amp, unsigned seed) { V v(n); srand(seed); for (auto &x : v) x = ((rand() / (float)RAND_MAX) - 0.5f) * 2 * amp; return v; }
// "music-like" test signal: low sine + mid noise bursts + hiss, with a decaying kick envelope
static V program(size_t n, float amp, unsigned seed = 1) {
    V v(n); srand(seed);
    for (size_t i = 0; i < n; i++) {
        double t = (double)i / fs, env = exp(-fmod(t, 0.5) * 6.0);
        double s = 0.6 * env * sin(2 * M_PI * 55 * t) + 0.25 * sin(2 * M_PI * 440 * t) * (0.5 + 0.5 * sin(2 * M_PI * 0.7 * t))
                 + 0.08 * (((rand() / (float)RAND_MAX) - 0.5) * 2) + 0.1 * sin(2 * M_PI * 3100 * t);
        v[i] = amp * (float)s;
    }
    return v;
}
static double bin(const V &a, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < a.size(); i++) { double ph = 2 * M_PI * hz * (double)i / fs; re += a[i] * cos(ph); im += a[i] * sin(ph); }
    size_t n = std::min(to, a.size()) - from; return 2 * sqrt(re * re + im * im) / n;
}
// neutral = transparent: no saturation, no compression (Amount 0), flat EQ, limiter far away, 0 dB out
static void neutral(TeaDSP &d) {
    d.setParam(P_INPUT, 500); d.setParam(P_DRIVE, 0); d.setParam(P_LOW, 500); d.setParam(P_MID, 500); d.setParam(P_HIGH, 500);
    d.setParam(P_AMOUNT, 0); d.setParam(P_LDRIVE, 0); d.setParam(P_LTHRESH, 1000); d.setParam(P_OUTPUT, 1000);
}
static void setAll(TeaDSP &d, const float *p) { for (int i = 0; i < P_COUNT; i++) d.setParam(i, p[i]); }
static void settle(TeaDSP &d, int blocks = 200) { V z(128, 0.f), o(128); for (int i = 0; i < blocks; i++) d.process(z.data(), z.data(), o.data(), o.data(), 128); }

int main() {
    // 1. neutral setting is flat and has the documented latency
    {
        TeaDSP d; d.init((float)fs); neutral(d); settle(d);
        double worst = 0;
        for (double hz : {40.0, 100.0, 120.0, 400.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0}) {
            TeaDSP e; e.init((float)fs); neutral(e); settle(e);
            V in = sine(44100, hz, 0.25f), oL, oR; run(e, in, in, oL, oR);
            worst = std::max(worst, fabs(db(rms(oL, 20000)) - db(rms(in, 20000))));
        }
        CHECK(worst < 0.15, "neutral: flat within %.3f dB from 40 Hz to 16 kHz", worst);
        V in(8192, 0.f); in[100] = 0.5f; V oL, oR; TeaDSP e; e.init((float)fs); neutral(e); settle(e); run(e, in, in, oL, oR);
        size_t pk = 0; for (size_t i = 0; i < oL.size(); i++) if (fabsf(oL[i]) > fabsf(oL[pk])) pk = i;
        CHECK(abs((int)pk - 100 - TeaDSP::latencySamples()) <= 2, "latency: impulse comes out %d samples later (reported %d)", (int)pk - 100, TeaDSP::latencySamples());
    }
    // 2. the Output knob is a ceiling: hot program never exceeds it (sample peak; true-peak overshoot checked on a 4x grid)
    for (float outPos : {1000.f, 800.f, 500.f}) {
        TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i];
        p[P_INPUT] = 900; p[P_AMOUNT] = 700; p[P_LDRIVE] = 800; p[P_LTHRESH] = 600; p[P_OUTPUT] = outPos; setAll(d, p);
        V in = program(44100 * 6, 0.9f); V oL, oR; run(d, in, in, oL, oR);
        const double ceil = pow(10.0, TeaMap::outputDb(outPos) / 20.0);
        float pk = peak(oL, 20000);
        CHECK(pk <= ceil * 1.001 && pk > ceil * 0.6, "ceiling %.1f dB: peak %.3f dB (%.3f)", TeaMap::outputDb(outPos), db(pk), pk);
        // true peak: 4x linear-interp-free check via sinc-lite: evaluate by 8-point windowed sinc at 3 inter-sample phases
        double tp = 0;
        for (size_t i = 20000; i + 16 < oL.size(); i += 1) {
            for (int ph = 1; ph < 4; ph++) {
                double f = ph / 4.0, s = 0;
                for (int k = -7; k <= 8; k++) { double x = k - f, w = 0.5 + 0.5 * cos(M_PI * x / 8.5); s += oL[i + 8 - k] * (fabs(x) < 1e-9 ? 1 : sin(M_PI * x) / (M_PI * x)) * w; }
                tp = std::max(tp, fabs(s));
            }
        }
        CHECK(tp <= ceil * 1.05, "ceiling %.1f dB: true peak %.2f dB over the ceiling", TeaMap::outputDb(outPos), db(tp / ceil));
    }
    // 3. loudness stays steady when Amount moves (auto-makeup)
    {
        double lo = 1e9, hi = -1e9;
        for (float amt : {0.f, 250.f, 500.f, 750.f, 1000.f}) {
            TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i];
            p[P_AMOUNT] = amt; p[P_LTHRESH] = 1000; p[P_LDRIVE] = 0; p[P_OUTPUT] = 1000; setAll(d, p);
            V in = program(44100 * 8, 0.3f); V oL, oR; run(d, in, in, oL, oR);
            double l = db(rms(oL, 44100 * 3)); lo = std::min(lo, l); hi = std::max(hi, l);
        }
        CHECK(hi - lo < 3.0, "loudness across Amount 0..200 %%: spread %.2f dB (program at -21 dBFS rms, makeup is tuned for -18)", hi - lo);
    }
    // 4. Amount compresses: the level difference between loud bursts and the quiet bed shrinks as Amount rises
    {
        double r[2];
        for (int k = 0; k < 2; k++) {
            TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i];
            p[P_AMOUNT] = k ? 1000 : 0; p[P_DRIVE] = 0; p[P_LTHRESH] = 1000; p[P_LDRIVE] = 0; p[P_OUTPUT] = 1000; setAll(d, p);
            V in = noise(44100 * 8, 0.12f, 5); for (size_t i = 0; i < in.size(); i++) { double t = fmod((double)i / fs, 0.5); if (t < 0.15) in[i] *= 5.0f; }
            V oL, oR; run(d, in, in, oL, oR);
            double sb = 0, sq = 0; size_t nb = 0, nq = 0;
            for (size_t i = 44100 * 3; i < oL.size(); i++) { double t = fmod((double)i / fs, 0.5); if (t > 0.05 && t < 0.14) { sb += oL[i] * oL[i]; nb++; } else if (t > 0.3 && t < 0.49) { sq += oL[i] * oL[i]; nq++; } }
            r[k] = db(sqrt(sb / nb)) - db(sqrt(sq / nq));
        }
        CHECK(r[1] < r[0] - 1.5, "Amount 200 %% narrows burst-to-bed difference: %.1f dB -> %.1f dB", r[0], r[1]);
    }
    // 5. mono lock below 120 Hz, widening above
    {
        TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i];
        p[P_AMOUNT] = 1000; p[P_DRIVE] = 0; p[P_LTHRESH] = 1000; p[P_LDRIVE] = 0; p[P_OUTPUT] = 1000; setAll(d, p);
        // opposite-phase 60 Hz and 6 kHz plus a correlated 1 kHz
        size_t n = 44100 * 4; V l(n), r(n);
        V a = sine(n, 60, 0.2f), b = sine(n, 6000, 0.1f), c = sine(n, 1000, 0.2f);
        for (size_t i = 0; i < n; i++) { l[i] = a[i] + b[i] + c[i]; r[i] = -a[i] + 0.5f * b[i] + c[i]; }
        V oL, oR; run(d, l, r, oL, oR);
        V side(n), mid(n); for (size_t i = 0; i < n; i++) { side[i] = 0.5f * (oL[i] - oR[i]); mid[i] = 0.5f * (oL[i] + oR[i]); }
        V sideIn(n); for (size_t i = 0; i < n; i++) sideIn[i] = 0.5f * (l[i] - r[i]);
        double s60 = bin(side, 44100, n, 60), s60in = bin(sideIn, 44100, n, 60);
        CHECK(s60 < s60in * 0.25, "mono lock: 60 Hz side %.3f -> %.3f (%.1f dB)", s60in, s60, db(s60 / s60in));
        double s6 = bin(side, 44100, n, 6000), s6in = bin(sideIn, 44100, n, 6000);
        CHECK(s6 > s6in * 1.05, "widening: 6 kHz side grows %.3f -> %.3f (%+.1f dB)", s6in, s6, db(s6 / s6in));
        double s1 = bin(side, 44100, n, 1000);
        CHECK(s1 < 0.02, "a centred 1 kHz stays centred (side %.4f)", s1);
        // Amount 0 is plain stereo
        TeaDSP e; e.init((float)fs); p[P_AMOUNT] = 0; setAll(e, p); run(e, l, r, oL, oR);
        for (size_t i = 0; i < n; i++) side[i] = 0.5f * (oL[i] - oR[i]);
        double z6 = bin(side, 44100, n, 6000);
        CHECK(fabs(db(z6 / s6in)) < 1.0, "Amount 0: no widening (%.2f dB)", db(z6 / s6in));
    }
    // 6. Input and Drive push: higher drive adds harmonics (3rd harmonic of 1 kHz up), Drive 0 doesn't
    {
        double h3[2];
        for (int k = 0; k < 2; k++) {
            TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i];
            p[P_DRIVE] = k ? 1000 : 0; p[P_AMOUNT] = 1000; p[P_LTHRESH] = 1000; p[P_LDRIVE] = 0; p[P_OUTPUT] = 1000; setAll(d, p);
            V in = sine(44100 * 3, 1000, 0.35f), oL, oR; run(d, in, in, oL, oR);
            h3[k] = bin(oL, 44100, 44100 * 3, 3000) + bin(oL, 44100, 44100 * 3, 2000);
        }
        CHECK(h3[1] > h3[0] * 3 && db(h3[1] / bin(sine(44100, 1000, 0.35f), 0, 44100, 1000)) > -50, "Drive adds harmonics: %.5f -> %.5f", h3[0], h3[1]);
    }
    // 7. Focus EQ: Low +100 raises 80 Hz, High +100 raises 10 kHz, Mid -100 cuts 1 kHz
    {
        struct T { int id; double hz; float pos; double lo, hi; } ts[] = {{P_LOW, 80, 1000, 3.0, 8.0}, {P_HIGH, 10000, 1000, 3.0, 8.0}, {P_MID, 1000, 0, -6.0, -1.0}};
        for (auto &t : ts) {
            TeaDSP d; d.init((float)fs); neutral(d); d.setParam(t.id, t.pos); settle(d);
            V in = sine(44100 * 2, t.hz, 0.2f), oL, oR; run(d, in, in, oL, oR);
            double g = db(rms(oL, 30000)) - db(rms(in, 30000));
            CHECK(g > t.lo && g < t.hi, "EQ %d at %.0f Hz moves %.2f dB", t.id, t.hz, g);
        }
    }
    // 8. Limiter drive makes it louder at the same ceiling
    {
        double l[2];
        for (int k = 0; k < 2; k++) {
            TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i];
            p[P_LDRIVE] = k ? 900 : 0; p[P_LTHRESH] = 900; p[P_OUTPUT] = 988; setAll(d, p);
            V in = program(44100 * 6, 0.5f); V oL, oR; run(d, in, in, oL, oR); l[k] = db(rms(oL, 44100 * 2));
        }
        CHECK(l[1] > l[0] + 1.5, "Limiter Drive 90 %% is %.1f dB louder than 0 %%", l[1] - l[0]);
    }
    // 9. sidechain switch: no NaN, no big click when toggled mid-stream, the key follows the raw input
    {
        TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = kDefaultPos[i]; setAll(d, p);
        V in = program(44100 * 4, 0.4f); V oL(in.size()), oR(in.size());
        for (size_t i = 0; i < in.size(); i += 128) { if (i == 44100) d.setExternalSidechain(true); if (i == 44100 * 2) d.setExternalSidechain(false);
            d.process(&in[i], &in[i], &oL[i], &oR[i], (int)std::min<size_t>(128, in.size() - i)); }
        double mx = 0; for (size_t i = 1; i < oL.size(); i++) mx = std::max(mx, (double)fabsf(oL[i] - oL[i - 1]));
        CHECK(finite_bounded(oL, 1.0f) && mx < 0.35, "sidechain toggled while playing: finite, biggest step %.3f", mx);
        // a separate host key: bass-heavy key makes the compressor work on a quiet signal
        TeaDSP e; e.init((float)fs); setAll(e, p); e.setExternalSidechain(true);
        V sig = sine(44100 * 4, 1000, 0.05f), key = sine(44100 * 4, 1000, 0.9f); V a(sig.size()), b(sig.size());
        for (size_t i = 0; i < sig.size(); i += 128) e.process(&sig[i], &sig[i], &a[i], &b[i], (int)std::min<size_t>(128, sig.size() - i), &key[i], &key[i]);
        Meters m; e.takeMeters(m);
        CHECK(m.grMid < -1.0f, "external key drives the compressor (mid GR %.1f dB with a quiet signal)", m.grMid);
    }
    // 10. extremes stay finite and bounded
    {
        for (int k = 0; k < 6; k++) {
            TeaDSP d; d.init((float)fs); float p[P_COUNT];
            srand(k + 3); for (int i = 0; i < P_COUNT; i++) p[i] = (k == 0) ? 0 : (k == 1) ? 1000 : (float)(rand() % 1001); setAll(d, p);
            V in = noise(44100 * 2, 1.0f, k + 7); V big = in; for (auto &x : big) x *= 4.0f;
            V oL, oR; run(d, big, in, oL, oR);
            CHECK(finite_bounded(oL, 1.5f) && finite_bounded(oR, 1.5f), "extremes #%d finite, peak %.3f", k, std::max(peak(oL), peak(oR)));
        }
    }
    // 11. all presets: bounded by their ceiling and not silent / not blown up
    {
        int bad = 0; double worstOver = -99, quiet = 99, loud = -99;
        for (int i = 0; i < kNumPresets; i++) {
            TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int k = 0; k < P_COUNT; k++) p[k] = kPresets[i].pos[k]; setAll(d, p); d.setExternalSidechain(false);
            V in = program(44100 * 5, 0.5f); V oL, oR; run(d, in, in, oL, oR);
            double ceil = pow(10.0, TeaMap::outputDb(p[P_OUTPUT]) / 20.0), pk = peak(oL, 20000), l = db(rms(oL, 44100 * 2));
            worstOver = std::max(worstOver, db(pk / ceil)); quiet = std::min(quiet, l); loud = std::max(loud, l);
            if (!finite_bounded(oL, 1.0f) || pk > ceil * 1.01 || l < -40) { bad++; printf("   preset %d %s: peak %.3f ceil %.3f rms %.1f dB\n", i, kPresets[i].name, pk, ceil, l); }
        }
        CHECK(bad == 0, "%d presets: all finite, under their ceiling (worst %+.2f dB), loudness %.1f .. %.1f dB", kNumPresets, worstOver, quiet, loud);
    }
    // 12. no clicks while knobs move (sweep every knob with a steady tone through)
    for (int id = 0; id < P_COUNT; id++) {
        TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int k = 0; k < P_COUNT; k++) p[k] = kDefaultPos[k]; p[P_LTHRESH] = 1000; setAll(d, p); settle(d);
        V in = sine(44100 * 3, 220, 0.3f), oL(in.size()), oR(in.size());
        for (size_t i = 0; i < in.size(); i += 128) {
            if (i > 44100) d.setParam(id, 500.0f + 500.0f * sinf((float)i / 44100.0f * 6.0f));
            d.process(&in[i], &in[i], &oL[i], &oR[i], (int)std::min<size_t>(128, in.size() - i));
        }
        double mx = 0; for (size_t i = 44100 + 1; i < oL.size(); i++) mx = std::max(mx, (double)fabsf(oL[i] - oL[i - 1]));
        CHECK(finite_bounded(oL, 1.5f) && mx < 0.12, "sweeping knob %d: largest sample step %.3f (tone alone 0.01)", id, mx);
    }
    // 13. silence in, silence out (no denormal tails, no idle noise)
    {
        TeaDSP d; d.init((float)fs); float p[P_COUNT]; for (int k = 0; k < P_COUNT; k++) p[k] = kDefaultPos[k]; setAll(d, p);
        V in = program(44100, 0.5f); V oL, oR; run(d, in, in, oL, oR);
        V z(44100 * 3, 0.f); run(d, z, z, oL, oR);
        CHECK(peak(oL, 44100) < 1e-5f && peak(oR, 44100) < 1e-5f, "silence after the program: residual %.2e", std::max(peak(oL, 44100), peak(oR, 44100)));
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
