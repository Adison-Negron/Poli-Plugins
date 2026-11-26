// Poli-Chorus DSP tests (offline): clean pass at Mix 0, the comb of the taps, level per mode, pitch wobble of Vibrato, the LFO the display
// draws, Width, LP Filter, Warmth, Feedback stability and decay, mode switches and sweeps without clicks, extremes, silence, denormals,
// every preset bounded and level-matched, CPU.
#include "ChoDSP.h"
#include "presets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <ctime>
#include <cmath>
#include <cstring>
using namespace cho;
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static const double fs = 44100;
typedef std::vector<float> V;
static void run(ChoDSP &d, const V &iL, const V &iR, V &oL, V &oR) {
    size_t n = iL.size(); oL.resize(n); oR.resize(n);
    for (size_t i = 0; i < n; i += 128) { int c = (int)std::min<size_t>(128, n - i); d.process(&iL[i], &iR[i], &oL[i], &oR[i], c); }
}
static bool bounded(const V &a, float lim) { for (float x : a) { if (!(x == x) || x > lim || x < -lim) return false; } return true; }
static float peak(const V &a, size_t from = 0, size_t to = (size_t)-1) { float p = 0; for (size_t i = from; i < a.size() && i < to; i++) p = std::max(p, fabsf(a[i])); return p; }
static double rms(const V &a, size_t from, size_t to) { double s = 0; size_t n = 0; for (size_t i = from; i < to && i < a.size(); i++) { s += (double)a[i] * a[i]; n++; } return n ? sqrt(s / n) : 0; }
static double bin(const V &a, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < a.size(); i++) { double ph = 2 * M_PI * hz * (double)i / fs; re += a[i] * cos(ph); im += a[i] * sin(ph); }
    return 2 * sqrt(re * re + im * im) / (std::min(to, a.size()) - from);
}
static V sine(size_t n, double hz, float amp) { V v(n); for (size_t i = 0; i < n; i++) v[i] = amp * (float)sin(2 * M_PI * hz * i / fs); return v; }
static V noise(size_t n, float level, unsigned seed) {   // pinkish noise scaled to an RMS of `level`
    V v(n); srand(seed); double lp = 0;
    for (auto &x : v) { double g = 0; for (int k = 0; k < 12; k++) g += rand() / (double)RAND_MAX; g -= 6; lp += 0.2 * (g - lp); x = (float)(g * 0.5 + lp * 1.5); }
    double r = 0; for (float x : v) r += x * x; r = sqrt(r / n); for (auto &x : v) x *= level / (float)r; return v;
}
static double db(double x) { return 20 * log10(std::max(x, 1e-12)); }
static double third_diff_peak(const V &a, size_t from) {   // the largest |3rd difference|: a click shows up as a spike
    double p = 0; for (size_t i = from + 3; i < a.size(); i++) p = std::max(p, (double)fabsf(a[i] - 3 * a[i - 1] + 3 * a[i - 2] - a[i - 3])); return p;
}
static void setAll(ChoDSP &d, const float *pos, int mode) { for (int i = 0; i < P_COUNT; ++i) d.setParam(i, pos[i]); d.setMode(mode); }
// a plain wet-only setup: Mix 100 %, Output 0 dB, no feedback, no warmth, LP open, amount as given
static void wetOnly(ChoDSP &d, int mode, float amount, float width = 1000) {
    d.setParam(P_MIX, 1000); d.setParam(P_OUTPUT, 667); d.setParam(P_FEEDBACK, 0); d.setParam(P_WARMTH, 0); d.setParam(P_LPF, 1000);
    d.setParam(P_AMOUNT, amount); d.setParam(P_WIDTH, width); d.setMode(mode);
}

int main() {
    // 0. the map: defaults, ranges, LFO shape
    {
        CHECK(fabs(ChoMap::rateHz(kDefaultPos[P_RATE]) - 0.8) < 0.01, "default Rate = %.3f Hz", ChoMap::rateHz(kDefaultPos[P_RATE]));
        CHECK(fabs(ChoMap::rateHz(0) - 0.05) < 1e-4 && fabs(ChoMap::rateHz(1000) - 10.0) < 1e-3, "Rate range %.3f .. %.3f Hz", ChoMap::rateHz(0), ChoMap::rateHz(1000));
        CHECK(fabs(ChoMap::outputDb(667)) < 0.02 && fabs(ChoMap::outputDb(0) + 24) < 1e-4 && fabs(ChoMap::outputDb(1000) - 12) < 1e-3, "Output -24 .. 0 (at 667) .. +12 dB");
        CHECK(fabs(ChoMap::lpfHz(0) - 800) < 0.1 && fabs(ChoMap::lpfHz(1000) - 20000) < 1, "LP Filter %.0f .. %.0f Hz", ChoMap::lpfHz(0), ChoMap::lpfHz(1000));
        double mx = 0, mn = 0;
        for (int m = 0; m < 3; m++) for (int i = 0; i < 1000; i++) { float v = ChoMap::lfo(m, i / 1000.0f * 3.7f); mx = std::max(mx, (double)v); mn = std::min(mn, (double)v); }
        CHECK(mx <= 1.0001 && mn >= -1.0001, "LFO stays within +-1 (%.3f .. %.3f)", mn, mx);
        CHECK(fabs(ChoMap::lfo(0, 0.25f) - 1.0f) < 1e-5 && fabs(ChoMap::lfo(0, 0.0f)) < 1e-6 && fabs(ChoMap::lfo(0, 1.25f) - 1.0f) < 1e-5, "Classic LFO is a sine, periodic");
    }
    // 1. Mix 0 % at 0 dB: clean pass (a bit-exact path up to the ceiling), whatever the other knobs do
    {
        ChoDSP d; float p[P_COUNT] = {900, 1000, 1000, 1000, 100, 1000, 667, 0}; setAll(d, p, M_ENSEMBLE); d.init(fs); setAll(d, p, M_ENSEMBLE);
        V in = noise(44100, 0.2f, 1), oL, oR; run(d, in, in, oL, oR);
        double worst = 0; for (size_t i = 22050; i < in.size(); i++) worst = std::max(worst, (double)fabsf(oL[i] - in[i]));
        CHECK(worst < 1e-5, "Mix 0 %%: output = input (max error %.2e)", worst);
        V s = sine(44100, 1000, 0.3f); run(d, s, s, oL, oR);
        CHECK(fabs(db(bin(oL, 22050, 44100, 1000) / 0.3)) < 0.01, "Mix 0 %%: 1 kHz at %.3f dB", db(bin(oL, 22050, 44100, 1000) / 0.3));
    }
    // 2. the Classic comb: amount 0, wet only: two taps at 7.5 / 11.5 ms (gain 0.55 each, trim 1.25)
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_CLASSIC, 0); d.init(fs); wetOnly(d, M_CLASSIC, 0);
        double worst = 0, worstHz = 0;
        for (double hz : {100.0, 125.0, 250.0, 400.0, 1000.0, 2000.0, 4800.0}) {
            V in = sine(44100, hz, 0.2f), oL, oR; run(d, in, in, oL, oR);
            const double g = bin(oL, 22050, 44100, hz) / 0.2;
            const double w = 2 * M_PI * hz;
            const double re = cos(w * 0.0075) + cos(w * 0.0115), im = sin(w * 0.0075) + sin(w * 0.0115);
            const double want = 0.55 * 1.25 * sqrt(re * re + im * im);
            const double e = fabs(db(g + 1e-9) - db(want + 1e-9));
            if (hz != 125.0 && e > worst) { worst = e; worstHz = hz; }
        }
        CHECK(worst < 0.3, "Classic comb matches the 7.5 / 11.5 ms taps within %.2f dB (worst at %.0f Hz)", worst, worstHz);
        V in = sine(44100, 125, 0.2f), oL, oR; run(d, in, in, oL, oR);
        CHECK(db(bin(oL, 22050, 44100, 125) / 0.2) < -25, "Classic null at 125 Hz (%.1f dB)", db(bin(oL, 22050, 44100, 125) / 0.2));
    }
    // 3. level per mode: noise at -18 dBFS RMS, wet only, default-ish knobs -> within +-2 dB of the input
    for (int m = 0; m < 3; m++) {
        ChoDSP d; d.init(fs); wetOnly(d, m, 500); d.setParam(P_RATE, 523); d.setParam(P_WARMTH, 250);
        V in = noise(44100 * 4, 0.126f, 3 + m), oL, oR; run(d, in, in, oL, oR);
        const double g = db(rms(oL, 44100, in.size()) / rms(in, 44100, in.size()));
        CHECK(fabs(g) < 2.0, "%s wet level on noise: %+.2f dB", kModeName[m], g);
        d.setParam(P_WARMTH, 1000); run(d, in, in, oL, oR);
        const double g2 = db(rms(oL, 44100, in.size()) / rms(in, 44100, in.size()));
        CHECK(fabs(g2) < 3.0, "%s wet level at Warmth 100 %%: %+.2f dB", kModeName[m], g2);
    }
    // 4. Vibrato pitch wobble: 1 kHz in, Rate 5 Hz, Amount 50 %: the output frequency swings by +-f * D * 2 pi * rate (D = 4 ms * amount)
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_VIBRATO, 500); d.setParam(P_RATE, 1000.0f * logf(5.0f / 0.05f) / logf(200.0f)); d.setParam(P_WIDTH, 0);
        V in = sine(44100 * 3, 1000, 0.3f), oL, oR; run(d, in, in, oL, oR);
        // instantaneous frequency from positive-going zero crossings (linear interpolation)
        std::vector<double> t; for (size_t i = 44100 + 1; i < oL.size(); i++) if (oL[i - 1] < 0 && oL[i] >= 0) t.push_back((i - 1) + (-oL[i - 1]) / (oL[i] - oL[i - 1]));
        double fmin = 1e9, fmax = 0; for (size_t i = 1; i < t.size(); i++) { double f = fs / (t[i] - t[i - 1]); fmin = std::min(fmin, f); fmax = std::max(fmax, f); }
        const double want = 1000 * 0.004 * 0.5 * 2 * M_PI * 5.0;   // +-62.8 Hz
        const double got = 0.5 * (fmax - fmin);
        CHECK(fabs(got - want) / want < 0.15, "Vibrato: pitch swing +-%.1f Hz (expected +-%.1f)", got, want);
        // and at Amount 0 there is no wobble at all
        d.setParam(P_AMOUNT, 0); run(d, in, in, oL, oR); t.clear();
        for (size_t i = 44100 + 1; i < oL.size(); i++) if (oL[i - 1] < 0 && oL[i] >= 0) t.push_back((i - 1) + (-oL[i - 1]) / (oL[i] - oL[i - 1]));
        fmin = 1e9; fmax = 0; for (size_t i = 1; i < t.size(); i++) { double f = fs / (t[i] - t[i - 1]); fmin = std::min(fmin, f); fmax = std::max(fmax, f); }
        CHECK(fmax - fmin < 1.0, "Vibrato at Amount 0: pitch steady (%.3f Hz spread)", fmax - fmin);
    }
    // 5. the display draws the same LFO the DSP uses: the right channel runs `theta` behind (Width 100 % = half a cycle: opposite)
    {
        CHECK(fabs(ChoMap::thetaCycles(1000) - 0.5) < 1e-6 && fabs(ChoMap::thetaCycles(0)) < 1e-6, "Width 100 %% = 180 deg, 0 %% = 0 deg");
        ChoDSP d; d.init(fs); wetOnly(d, M_VIBRATO, 1000, 1000);
        V in = noise(44100 * 4, 0.2f, 9), oL, oR; run(d, in, in, oL, oR);
        const double l = rms(oL, 22050, in.size()), r = rms(oR, 22050, in.size());
        CHECK(fabs(db(l / r)) < 1.0 && l > 0.1, "Width 100 %%: both channels carry the same level of wet (%.3f / %.3f)", l, r);
    }
    // 6. Width: L-only input; Width 0 = mono wet (L == R), Width 100 % = a real stereo wet
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_CLASSIC, 700, 0);
        V in = noise(44100 * 2, 0.2f, 5), z(in.size(), 0.0f), oL, oR; run(d, in, z, oL, oR);
        double diff = 0; for (size_t i = 22050; i < in.size(); i++) diff = std::max(diff, (double)fabsf(oL[i] - oR[i]));
        CHECK(diff < 1e-4, "Width 0 %%: wet is mono (max L-R %.2e)", diff);
        d.setParam(P_WIDTH, 1000); run(d, in, z, oL, oR);
        double e = 0, dd = 0; for (size_t i = 44100; i < in.size(); i++) { e += oL[i] * oL[i]; dd += (oL[i] - oR[i]) * (oL[i] - oR[i]); }
        CHECK(dd / e > 0.2, "Width 100 %%: wet is wide (side/total energy %.2f)", dd / e);
        // a mono input through a wide chorus becomes stereo; through Width 0 it stays mono
        V m = noise(44100 * 2, 0.2f, 6); run(d, m, m, oL, oR);
        dd = 0; e = 0; for (size_t i = 44100; i < m.size(); i++) { e += oL[i] * oL[i]; dd += (oL[i] - oR[i]) * (oL[i] - oR[i]); }
        CHECK(dd / e > 0.05, "mono in, Width 100 %%: stereo out (side/total %.2f)", dd / e);
        d.setParam(P_WIDTH, 0); run(d, m, m, oL, oR); run(d, m, m, oL, oR);
        dd = 0; for (size_t i = 44100; i < m.size(); i++) dd += (oL[i] - oR[i]) * (oL[i] - oR[i]);
        CHECK(dd < 1e-6, "mono in, Width 0 %%: mono out");
    }
    // 7. LP Filter: wet only, amount 0: 2-pole low-pass at the knob's frequency; Off at the top
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_VIBRATO, 0); d.setParam(P_LPF, 500);   // 4 kHz
        auto gain = [&](double hz) { V in = sine(44100, hz, 0.2f), oL, oR; run(d, in, in, oL, oR); return db(bin(oL, 22050, 44100, hz) / 0.2); };
        const double g500 = gain(500), g4k = gain(4000), g8k = gain(8000);
        CHECK(fabs(g4k - g500 + 3.0) < 1.0 && g8k - g500 < -9.5, "LP Filter at 4 kHz: -3 dB at the cut-off (%.1f), %.1f dB at 8 kHz", g4k - g500, g8k - g500);
        d.setParam(P_LPF, 1000);
        const double h = gain(8000), l = gain(500);
        CHECK(fabs(h - l) < 0.6, "LP Filter Off: flat to 8 kHz (%.2f dB; the delay's interpolation rolls off the top a little, as in every chorus)", h - l);
    }
    // 8. Warmth: unity small-signal gain, more harmonics when hot
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_VIBRATO, 0); d.setParam(P_WARMTH, 1000);
        V in = sine(44100, 1000, 0.01f), oL, oR; run(d, in, in, oL, oR);
        const double g = db(bin(oL, 22050, 44100, 1000) / 0.01);
        CHECK(fabs(g - 20 * log10(1.0 + 0.12 * 2.5) + 0.45 * 0 ) < 1.5, "Warmth 100 %%: small signal %+.2f dB (makeup only)", g);
        V hot = sine(44100, 1000, 0.8f); run(d, hot, hot, oL, oR);
        const double h3 = db(bin(oL, 22050, 44100, 3000) / bin(oL, 22050, 44100, 1000));
        CHECK(h3 > -40 && h3 < -6, "Warmth 100 %%: 3rd harmonic %.1f dB at a hot level", h3);
        d.setParam(P_WARMTH, 0); run(d, hot, hot, oL, oR);
        CHECK(db(bin(oL, 22050, 44100, 3000) / bin(oL, 22050, 44100, 1000)) < -80, "Warmth 0: no harmonics");
    }
    // 9. Feedback: stable and decaying at the maximum, echoes at the tap delays; silence in = silence out
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_ENSEMBLE, 1000); d.setParam(P_FEEDBACK, 1000); d.setParam(P_WARMTH, 500);
        V in(44100 * 8, 0.0f); for (int i = 0; i < 200; i++) in[i] = 0.9f * (i < 100 ? 1.0f : -1.0f);
        V oL, oR; run(d, in, in, oL, oR);
        CHECK(bounded(oL, 1.0f) && bounded(oR, 1.0f), "Feedback 100 %%: bounded (peak %.2f)", std::max(peak(oL), peak(oR)));
        CHECK(peak(oL, 44100 * 6) < 1e-4f, "Feedback 100 %%: the tail is gone after 6 s (%.2e)", peak(oL, 44100 * 6));
        CHECK(peak(oL, 44100 / 20, 44100 / 5) > 0.01f, "Feedback 100 %%: the loop rings (%.3f at 50-200 ms)", peak(oL, 44100 / 20, 44100 / 5));
        V sil(44100 * 3, 0.f); run(d, sil, sil, oL, oR);
        CHECK(peak(oL) == 0.0f && peak(oR) == 0.0f || peak(oL, 44100 * 2) == 0.0f, "silence in = silence out after the tail");
    }
    // 10. clicks: mode switches, knob sweeps in Q-Link steps (a step every block), against the same signal with nothing moving
    {
        auto tri = [](int blk, int step) { int v = (blk * step) % 2000; return (float)(v < 1000 ? v : 2000 - v); };
        auto thirdOf = [&](int what, double *fix) {
            ChoDSP d; d.init(fs); wetOnly(d, M_CLASSIC, 500); d.setParam(P_MIX, 600); d.setParam(P_FEEDBACK, 300);
            V in = sine(128 * 1380, 700, 0.3f), oL(in.size()), oR(in.size());
            for (size_t i = 0; i < in.size(); i += 128) {
                const int blk = (int)(i / 128);
                if (what == 1 && blk % 200 == 100) d.setMode((d.mode() + 1) % 3);
                if (what == 2) d.setParam(P_RATE, 100 + tri(blk, 8) * 0.9f);
                if (what == 3) d.setParam(P_AMOUNT, tri(blk, 8));
                if (what == 4) d.setParam(P_LPF, 200 + tri(blk, 8) * 0.8f);
                if (what == 5) d.setParam(P_MIX, tri(blk, 8));
                if (what == 6) d.setParam(P_WIDTH, tri(blk, 8));
                if (what == 7) d.setParam(P_FEEDBACK, tri(blk, 8));
                if (what == 8) d.setParam(P_WARMTH, tri(blk, 8));
                if (what == 9) d.setParam(P_OUTPUT, tri(blk, 8));
                d.process(&in[i], &in[i], &oL[i], &oR[i], 128);
            }
            *fix = third_diff_peak(oL, 22050);
            return 0;
        };
        double base; thirdOf(0, &base);
        const char *names[] = {"", "mode switch", "Rate sweep", "Amount sweep", "LP Filter sweep", "Mix sweep", "Width sweep", "Feedback sweep", "Warmth sweep", "Output sweep"};
        for (int w = 1; w <= 9; w++) {
            double t; thirdOf(w, &t);
            // sweeping Amount / Rate legitimately moves the pitch; the bar is "no step much bigger than the smooth movement": 6x the static comb signal
            CHECK(t < base * 6.0 + 0.02, "%s: no clicks (3rd difference %.4f vs %.4f still)", names[w], t, base);
        }
    }
    // 11. extremes: random settings at all modes stay finite and within the ceiling; L=R stays bounded
    {
        srand(7); bool all = true; float worst = 0;
        for (int t = 0; t < 120; t++) {
            ChoDSP d; d.init(fs); float p[P_COUNT]; for (int i = 0; i < P_COUNT; i++) p[i] = (float)(rand() % 1001);
            if (t % 3 == 0) { p[P_FEEDBACK] = 1000; p[P_AMOUNT] = 1000; }
            setAll(d, p, t % 3);
            V in = noise(44100, 0.7f, 100 + t), oL, oR; run(d, in, in, oL, oR);
            if (t % 5 == 0) { V loud(44100, 1.0f); for (size_t i = 0; i < loud.size(); i++) loud[i] = (i / 40) % 2 ? 1.0f : -1.0f; run(d, loud, loud, oL, oR); }
            if (!bounded(oL, 1.0001f) || !bounded(oR, 1.0001f)) all = false;
            worst = std::max(worst, std::max(peak(oL), peak(oR)));
        }
        CHECK(all, "120 random extremes: finite and bounded (peak %.3f)", worst);
        ChoDSP d; d.init(fs); V in = sine(44100, 100, 0.5f), oL, oR; for (auto &x : in) x += 0.4f; run(d, in, in, oL, oR);
        CHECK(bounded(oL, 1.0001f), "DC offset input stays bounded");
    }
    // 12. denormals: a loud burst then silence must not slow the tail down
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_ENSEMBLE, 800); d.setParam(P_FEEDBACK, 800); d.setParam(P_MIX, 500);
        V in = noise(44100, 0.5f, 3), z(44100 * 10, 0.0f), oL, oR; run(d, in, in, oL, oR);
        clock_t t0 = clock(); run(d, z, z, oL, oR); double tail = (double)(clock() - t0) / CLOCKS_PER_SEC;
        ChoDSP e; e.init(fs); wetOnly(e, M_ENSEMBLE, 800); e.setParam(P_FEEDBACK, 800); e.setParam(P_MIX, 500);
        V n10 = noise(44100 * 10, 0.5f, 4); t0 = clock(); run(e, n10, n10, oL, oR); double live = (double)(clock() - t0) / CLOCKS_PER_SEC;
        CHECK(tail < live * 2.0 + 0.02, "decaying tail costs %.3f s vs %.3f s for live audio (denormals flushed)", tail, live);
    }
    // 13. every preset: bounded, and the level on noise stays within -6 .. +4 dB of the input
    {
        bool ok = true; double lo = 99, hi = -99; const char *wl = "", *wh = "";
        for (int i = 0; i < kNumPresets; i++) {
            ChoDSP d; d.init(fs); float p[P_COUNT]; for (int k = 0; k < P_COUNT; k++) p[k] = kPresets[i].pos[k]; setAll(d, p, kPresets[i].mode);
            V in = noise(44100 * 3, 0.126f, 20 + i), oL, oR; run(d, in, in, oL, oR);
            if (!bounded(oL, 1.0001f) || !bounded(oR, 1.0001f)) ok = false;
            const double g = db(rms(oL, 44100, in.size()) / rms(in, 44100, in.size()));
            if (g < lo) { lo = g; wl = kPresets[i].name; }
            if (g > hi) { hi = g; wh = kPresets[i].name; }
        }
        CHECK(ok, "all %d presets bounded", kNumPresets);
        CHECK(lo > -6.0 && hi < 4.0, "preset levels on noise %+.1f dB (%s) .. %+.1f dB (%s)", lo, wl, hi, wh);
    }
    // 14. a state-less restart: init() clears everything (no leftovers from the loud signal)
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_CLASSIC, 500); d.setParam(P_FEEDBACK, 900);
        V in = noise(44100, 0.5f, 3), oL, oR; run(d, in, in, oL, oR); d.init(fs); wetOnly(d, M_CLASSIC, 500);
        V z(4096, 0.0f); run(d, z, z, oL, oR);
        CHECK(peak(oL) == 0.0f, "init() clears the delay lines");
    }
    // 15. CPU: Ensemble with feedback, 10 s of stereo audio
    {
        ChoDSP d; d.init(fs); wetOnly(d, M_ENSEMBLE, 800); d.setParam(P_FEEDBACK, 500); d.setParam(P_WARMTH, 700); d.setParam(P_LPF, 600); d.setParam(P_MIX, 600);
        V in = noise(44100 * 10, 0.2f, 8), oL, oR; clock_t t0 = clock(); run(d, in, in, oL, oR); double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        CHECK(10.0 / secs > 20.0, "CPU: Ensemble + feedback runs %.0fx real time", 10.0 / secs);
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
