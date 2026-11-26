// Poli-delay DSP tests (offline): echo timing, feedback decay, Offset, Ping-Pong, Sync, filters in the loop, saturation,
// modulation, Spread, stability under extremes, every preset bounded, clicks while knobs move, silence.
#include "DelayDSP.h"
#include "presets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <ctime>
#include <cmath>
using namespace delay;
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static const double fs = 44100;
typedef std::vector<float> V;
static void run(DelayDSP &v, const V &iL, const V &iR, V &oL, V &oR) {
    size_t n = iL.size(); oL.resize(n); oR.resize(n);
    for (size_t i = 0; i < n; i += 128) { int c = (int)std::min<size_t>(128, n - i); v.process(&iL[i], &iR[i], &oL[i], &oR[i], c); }
}
static bool finite_bounded(const V &a, float lim) { for (float x : a) { if (!(x == x) || x > lim || x < -lim) return false; } return true; }
static float peak(const V &a, size_t from = 0, size_t to = (size_t)-1) { float p = 0; for (size_t i = from; i < a.size() && i < to; i++) p = std::max(p, fabsf(a[i])); return p; }
static V noise(size_t n, float amp, unsigned seed) { V v(n); srand(seed); for (auto &x : v) x = ((rand() / (float)RAND_MAX) - 0.5f) * 2 * amp; return v; }
static size_t argmax(const V &a, size_t from, size_t to) { size_t b = from; for (size_t i = from; i < to && i < a.size(); i++) if (fabsf(a[i]) > fabsf(a[b])) b = i; return b; }
// a clean "wet only" delay: mix 100 %, filters open, no drive, no modulation
static void wetOnly(DelayDSP &d) {
    d.setParam(P_MIX, 1000); d.setParam(P_HP, 0); d.setParam(P_LP, 1000); d.setParam(P_DRIVE, 0); d.setParam(P_DEPTH, 0); d.setParam(P_TONE, 500);
    d.setParam(P_SPREAD, 500); d.setParam(P_FEEDBACK, 0);
}
static double rms(const V &a, size_t from, size_t to) { double s = 0; size_t n = 0; for (size_t i = from; i < to && i < a.size(); i++) { s += (double)a[i] * a[i]; n++; } return n ? sqrt(s / n) : 0; }
// amplitude of one frequency (single-bin DFT)
static double bin(const V &a, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < a.size(); i++) { double ph = 2 * M_PI * hz * (double)i / fs; re += a[i] * cos(ph); im += a[i] * sin(ph); }
    size_t n = std::min(to, a.size()) - from; return 2 * sqrt(re * re + im * im) / n;
}
static V sine(size_t n, double hz, float amp) { V v(n); for (size_t i = 0; i < n; i++) v[i] = amp * (float)sin(2 * M_PI * hz * i / fs); return v; }

int main() {
    // 1. echo timing: an impulse at Time 300 ms comes back after exactly 300 ms (and again after 600 ms at feedback)
    {
        DelayDSP d; d.init(fs); wetOnly(d);
        d.setParam(P_TIME, DelayMap::posOfTimeMs(300)); d.setParam(P_FEEDBACK, 500);
        V in(44100 * 3, 0.f); in[0] = 0.5f; V oL, oR;
        run(d, in, in, oL, oR);
        const size_t e1 = argmax(oL, 5000, 20000), e2 = argmax(oL, 20000, 33000);
        const double want = 0.3 * fs;
        CHECK(fabs((double)e1 - want) < 3, "first echo at %zu samples (want %.0f)", e1, want);
        CHECK(fabs((double)(e2 - e1) - want) < 3, "second echo %zu samples later (want %.0f)", e2 - e1, want);
        CHECK(fabs(oL[e1] - oR[e1]) < 1e-6, "no Offset: left and right echoes align");
        // regeneration: a 1 kHz burst comes back at half the level each time at feedback 50 %
        DelayDSP b; b.init(fs); wetOnly(b); b.setParam(P_TIME, DelayMap::posOfTimeMs(300)); b.setParam(P_FEEDBACK, 500);
        V bin(44100 * 3, 0.f); V sb = sine(882, 1000, 0.4f); std::copy(sb.begin(), sb.end(), bin.begin());
        run(b, bin, bin, oL, oR);
        const size_t T = (size_t)want;
        const double r1 = rms(oL, T + 100, T + 782), r2 = rms(oL, 2 * T + 100, 2 * T + 782), r3 = rms(oL, 3 * T + 100, 3 * T + 782);
        CHECK(fabs(r2 / r1 - 0.5) < 0.03 && fabs(r3 / r2 - 0.5) < 0.03, "feedback 50 %%: repeats at %.3f, %.3f of the one before", r2 / r1, r3 / r2);
    }
    // 2. Offset: + = right side later; - = left side later; ratio of the times
    {
        DelayDSP d; d.init(fs); wetOnly(d); d.setOffsetOn(true); d.setParam(P_TIME, DelayMap::posOfTimeMs(200)); d.setParam(P_OFFSET, 500 + 50 * 5);
        d.init(fs); wetOnly(d); d.setOffsetOn(true); d.setParam(P_TIME, DelayMap::posOfTimeMs(200)); d.setParam(P_OFFSET, 750);
        V in(44100 * 2, 0.f); in[0] = 0.5f; V oL, oR; run(d, in, in, oL, oR);
        const size_t l = argmax(oL, 3000, 40000), r = argmax(oR, 3000, 40000);
        CHECK(fabs((double)l - 0.2 * fs) < 3 && fabs((double)r - 0.3 * fs) < 3, "Offset +50 %%: L %zu, R %zu samples (want %.0f, %.0f)", l, r, 0.2 * fs, 0.3 * fs);
        DelayDSP e; e.init(fs); wetOnly(e); e.setOffsetOn(true); e.setParam(P_TIME, DelayMap::posOfTimeMs(200)); e.setParam(P_OFFSET, 100);
        e.init(fs); wetOnly(e); e.setOffsetOn(true); e.setParam(P_TIME, DelayMap::posOfTimeMs(200)); e.setParam(P_OFFSET, 100);
        run(e, in, in, oL, oR);
        const size_t l2 = argmax(oL, 3000, 40000), r2 = argmax(oR, 3000, 40000);
        CHECK(r2 > l2 || l2 > r2, "Offset -80 %%: sides differ (L %zu, R %zu)", l2, r2);
        CHECK(l2 > r2, "Offset negative: LEFT is the later side");
        DelayDSP f; f.init(fs); wetOnly(f); f.setOffsetOn(false); f.setParam(P_TIME, DelayMap::posOfTimeMs(200)); f.setParam(P_OFFSET, 1000);
        f.init(fs); wetOnly(f); f.setOffsetOn(false); f.setParam(P_TIME, DelayMap::posOfTimeMs(200)); f.setParam(P_OFFSET, 1000);
        run(f, in, in, oL, oR);
        CHECK(argmax(oL, 3000, 40000) == argmax(oR, 3000, 40000), "Offset knob at +100 %% does nothing while the Offset toggle is off");
    }
    // 3. Ping-Pong: mono hit -> echo 1 left, 2 right, 3 left ...
    {
        DelayDSP d; d.init(fs); wetOnly(d); d.setPingPong(true); d.setParam(P_TIME, DelayMap::posOfTimeMs(250)); d.setParam(P_FEEDBACK, 700);
        d.init(fs); wetOnly(d); d.setPingPong(true); d.setParam(P_TIME, DelayMap::posOfTimeMs(250)); d.setParam(P_FEEDBACK, 700);
        V in(44100 * 3, 0.f); in[0] = 0.5f; V oL, oR; run(d, in, in, oL, oR);
        const size_t T = (size_t)(0.25 * fs);
        auto at = [&](const V &a, int k) { return peak(a, k * T - 50, k * T + 50); };
        CHECK(at(oL, 1) > 0.2f && at(oR, 1) < 0.01f, "echo 1 is left only (L %.3f R %.3f)", at(oL, 1), at(oR, 1));
        CHECK(at(oR, 2) > 0.1f && at(oL, 2) < 0.01f, "echo 2 is right only (L %.3f R %.3f)", at(oL, 2), at(oR, 2));
        CHECK(at(oL, 3) > 0.05f && at(oR, 3) < 0.01f, "echo 3 is left again (L %.3f R %.3f)", at(oL, 3), at(oR, 3));
    }
    // 4. Sync: tempo and note value set the time
    {
        DelayDSP d; d.init(fs); wetOnly(d); d.setSync(true); d.setTempo(120); d.setParam(P_TIME, 1000.f * 8 / 12);   // index 8 = 1/4
        d.init(fs); wetOnly(d); d.setSync(true); d.setTempo(120); d.setParam(P_TIME, 1000.f * 8 / 12);
        CHECK(fabs(d.timeMsNow() - 500.0) < 0.01, "1/4 at 120 bpm = %.2f ms", d.timeMsNow());
        d.setTempo(60); CHECK(fabs(d.timeMsNow() - 1000.0) < 0.01, "1/4 at 60 bpm = %.2f ms", d.timeMsNow());
        d.setParam(P_TIME, 1000.f * 7 / 12); d.setTempo(120); CHECK(fabs(d.timeMsNow() - 375.0) < 0.01, "1/8. at 120 bpm = %.2f ms", d.timeMsNow());
        d.setTempo(0); CHECK(fabs(d.timeMsNow() - 375.0) < 0.01, "no host tempo: falls back to 120 bpm");
        V in(44100 * 2, 0.f); in[0] = 0.5f; V oL, oR; d.setTempo(120); d.setParam(P_TIME, 1000.f * 8 / 12);
        run(d, in, in, oL, oR);
        CHECK(fabs((double)argmax(oL, 5000, 40000) - 0.5 * fs) < 3, "synced echo lands on the beat (%zu)", argmax(oL, 5000, 40000));
    }
    // 5. High Pass / Low Pass sit in the loop: a low-passed delay darkens each repeat
    {
        const float lo = 200, hi = 8000;
        double lev[2][2];
        for (int t = 0; t < 2; t++) {
            DelayDSP d; d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(100)); d.setParam(P_FEEDBACK, 600);
            d.setParam(P_LP, t ? 1000.f * logf(1000.f / 500.f) / logf(40.f) : 1000);
            d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(100)); d.setParam(P_FEEDBACK, 600);
            d.setParam(P_LP, t ? 1000.f * logf(1000.f / 500.f) / logf(40.f) : 1000);   // 1 kHz or open
            for (int f = 0; f < 2; f++) {
                DelayDSP e; e.init(fs); wetOnly(e); e.setParam(P_TIME, DelayMap::posOfTimeMs(100)); e.setParam(P_FEEDBACK, 600);
                e.setParam(P_LP, t ? 1000.f * logf(1000.f / 500.f) / logf(40.f) : 1000);
                V in = sine(44100, f ? hi : lo, 0.3f); V z(44100 * 2, 0.f); in.insert(in.end(), z.begin(), z.end());
                V oL, oR; run(e, in, in, oL, oR);
                lev[t][f] = rms(oL, 44100 + 4410 * 3, 44100 + 4410 * 6) + 1e-9;   // the repeats after the input stopped
            }
        }
        CHECK(lev[1][1] < 0.05 * lev[0][1], "Low Pass 1 kHz kills 8 kHz repeats (%.5f vs %.5f open)", lev[1][1], lev[0][1]);
        CHECK(lev[1][0] > 0.7 * lev[0][0], "...and leaves 200 Hz alone (%.5f vs %.5f)", lev[1][0], lev[0][0]);
        double lv[2];
        for (int t = 0; t < 2; t++) {
            DelayDSP e; e.init(fs); wetOnly(e); e.setParam(P_TIME, DelayMap::posOfTimeMs(100)); e.setParam(P_FEEDBACK, 600);
            e.setParam(P_HP, t ? 1000.f * logf(1000.f / 20.f) / logf(100.f) : 0);
            V in = sine(44100, lo, 0.3f); V z(44100 * 2, 0.f); in.insert(in.end(), z.begin(), z.end());
            V oL, oR; run(e, in, in, oL, oR); lv[t] = rms(oL, 44100 + 4410 * 3, 44100 + 4410 * 6) + 1e-9;
        }
        CHECK(lv[1] < 0.1 * lv[0], "High Pass 1 kHz kills 200 Hz repeats (%.5f vs %.5f open)", lv[1], lv[0]);
    }
    // 6. Drive adds harmonics (and Drive 0 is clean); Tone tilts
    {
        double h3[2], h2[2];
        for (int t = 0; t < 2; t++) {
            DelayDSP d; d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(50)); d.setParam(P_DRIVE, t ? 800 : 0);
            d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(50)); d.setParam(P_DRIVE, t ? 800 : 0);
            V in = sine(44100, 500, 0.3f), oL, oR; run(d, in, in, oL, oR);
            h3[t] = bin(oL, 20000, 40000, 1500) / bin(oL, 20000, 40000, 500);
            h2[t] = bin(oL, 20000, 40000, 1000) / bin(oL, 20000, 40000, 500);
        }
        CHECK(h3[0] < 0.002, "Drive 0: 3rd harmonic %.5f (clean)", h3[0]);
        CHECK(h3[1] > 0.03, "Drive 80 %%: 3rd harmonic %.4f", h3[1]);
        CHECK(h2[1] > 0.01 && h2[1] > 5 * h2[0], "Drive 80 %%: 2nd harmonic %.4f (tube-style, even)", h2[1]);
        double g[3];
        for (int t = 0; t < 3; t++) {
            double r[2];
            for (int f = 0; f < 2; f++) {
                DelayDSP d; d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(50)); d.setParam(P_TONE, t == 0 ? 0 : t == 1 ? 500 : 1000);
                V in = sine(44100, f ? 6000 : 150, 0.3f), oL, oR; run(d, in, in, oL, oR); r[f] = rms(oL, 20000, 40000);
            }
            g[t] = 20 * log10(r[1] / r[0]);
        }
        CHECK(g[0] < g[1] - 8 && g[2] > g[1] + 8, "Tone tilts the highs vs lows: dark %.1f dB, flat %.1f dB, bright %.1f dB (6 kHz re 150 Hz)", g[0], g[1], g[2]);
    }
    // 7. Modulation moves the pitch of the echo; depth 0 does not
    {
        double dev[2];
        for (int t = 0; t < 2; t++) {
            DelayDSP d; d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(200)); d.setParam(P_RATE, 1000.f * logf(4.f / 0.05f) / logf(200.f)); d.setParam(P_DEPTH, t ? 1000 : 0);
            d.init(fs); wetOnly(d); d.setParam(P_TIME, DelayMap::posOfTimeMs(200)); d.setParam(P_RATE, 1000.f * logf(4.f / 0.05f) / logf(200.f)); d.setParam(P_DEPTH, t ? 1000 : 0);
            V in = sine(44100 * 3, 1000, 0.3f), oL, oR; run(d, in, in, oL, oR);
            // instantaneous frequency from zero crossings over 5 ms windows
            double lo = 1e9, hi = 0; const size_t w = 882;
            for (size_t s0 = 20000; s0 + w < 100000; s0 += w) {
                int z = 0; for (size_t i = s0 + 1; i < s0 + w; i++) if (oL[i - 1] < 0 && oL[i] >= 0) z++;
                double f = z * fs / (double)w; lo = std::min(lo, f); hi = std::max(hi, f);
            }
            dev[t] = (hi - lo) / 1000.0;
        }
        CHECK(dev[0] < 0.08 && dev[1] > 0.3, "Modulation: pitch swing %.0f %% at depth 0, %.0f %% at depth 100 %%", dev[0] * 100, dev[1] * 100);
    }
    // 8. Spread: 0 = mono wet, 200 = wider
    {
        double w[3];
        for (int t = 0; t < 3; t++) {
            DelayDSP d; d.init(fs); wetOnly(d); d.setOffsetOn(true); d.setParam(P_OFFSET, 700); d.setParam(P_TIME, DelayMap::posOfTimeMs(30)); d.setParam(P_SPREAD, t == 0 ? 0 : t == 1 ? 500 : 1000);
            d.init(fs); wetOnly(d); d.setOffsetOn(true); d.setParam(P_OFFSET, 700); d.setParam(P_TIME, DelayMap::posOfTimeMs(30)); d.setParam(P_SPREAD, t == 0 ? 0 : t == 1 ? 500 : 1000);
            V in = noise(44100, 0.3f, 5), oL, oR; run(d, in, in, oL, oR);
            double s = 0, m = 0; for (size_t i = 5000; i < oL.size(); i++) { s += pow(oL[i] - oR[i], 2); m += pow(oL[i] + oR[i], 2); }
            w[t] = sqrt(s / (m + 1e-12));
        }
        CHECK(w[0] < 1e-4 && w[1] > 0.05 && w[2] > 1.7 * w[1], "Spread: side/mid %.5f (0 %%), %.3f (100 %%), %.3f (200 %%)", w[0], w[1], w[2]);
    }
    // 9. Mix: 0 = dry only (bit exact), 100 = wet only
    {
        DelayDSP d; d.init(fs); d.setParam(P_MIX, 0);
        d.init(fs); d.setParam(P_MIX, 0);
        V in = noise(44100, 0.5f, 9), oL, oR; run(d, in, in, oL, oR);
        double e = 0; for (size_t i = 0; i < in.size(); i++) e = std::max(e, (double)fabsf(oL[i] - in[i]));
        CHECK(e < 1e-6, "Mix 0 %%: output = input (max error %.2g)", e);
        DelayDSP w; w.init(fs); w.setParam(P_MIX, 1000); w.init(fs); w.setParam(P_MIX, 1000);
        V a, b; run(w, in, in, a, b);
        CHECK(peak(a, 0, 2000) < 1e-3, "Mix 100 %%: no dry in the first 45 ms (peak %.5f)", peak(a, 0, 2000));
    }
    // 10. extremes: every parameter at 0 / 1000 in many combinations, loud noise: finite, bounded
    {
        int bad = 0; float worst = 0;
        for (int combo = 0; combo < 64; combo++) {
            DelayDSP d; d.init(fs);
            for (int p = 0; p < P_COUNT; p++) d.setParam(p, ((combo >> (p % 6)) & 1) ? 1000.f : 0.f);
            d.setParam(P_FEEDBACK, 1000); d.setParam(P_MIX, (combo & 1) ? 1000 : 500);
            d.setSync(combo & 2); d.setOffsetOn(combo & 4); d.setPingPong(combo & 8); d.setTempo(combo & 16 ? 40 : 300);
            V in = noise(44100 * 4, 1.0f, 40 + combo), oL, oR; run(d, in, in, oL, oR);
            if (!finite_bounded(oL, 2.1f) || !finite_bounded(oR, 2.1f)) bad++;
            worst = std::max(worst, std::max(peak(oL), peak(oR)));
        }
        CHECK(bad == 0, "64 extreme combinations with feedback 100 %%: finite, bounded (worst peak %.3f)", worst);
    }
    // 11. feedback 100 % with loud input cannot run away: the wet signal stays <= ~1.0 (the loop ceiling)
    {
        DelayDSP d; d.init(fs); d.setParam(P_MIX, 1000); d.setParam(P_FEEDBACK, 1000); d.setParam(P_DRIVE, 0);
        V in = noise(44100 * 20, 1.0f, 3), oL, oR; run(d, in, in, oL, oR);
        CHECK(finite_bounded(oL, 1.0001f) && finite_bounded(oR, 1.0001f), "feedback 100 %%, 20 s of full-scale noise: peak %.3f", std::max(peak(oL), peak(oR)));
    }
    // 12. every preset: loud noise stays finite, tail ends (or sustains boundedly)
    {
        int bad = 0; float worst = 0;
        for (int i = 0; i < kNumPresets; i++) {
            DelayDSP w; w.setSync(kPresets[i].sync); w.setOffsetOn(kPresets[i].offset); w.setPingPong(kPresets[i].pingpong);
            for (int p = 0; p < P_COUNT; p++) w.setParam(p, kPresets[i].pos[p]);
            w.init(fs); w.setTempo(120);
            auto in = noise(44100 * 3, 0.9f, 100 + i); V oL, oR; run(w, in, in, oL, oR);
            float pk = std::max(peak(oL), peak(oR)); worst = std::max(worst, pk);
            if (!finite_bounded(oL, 1.45f) || !finite_bounded(oR, 1.45f)) bad++;
        }
        CHECK(bad == 0, "all %d presets: noise at -1 dBFS stays finite and under 1.45 (worst %.3f, %d bad)", kNumPresets, worst, bad);
    }
    // 13. silence in -> silence out; echoes die away at feedback < 100 %
    {
        DelayDSP d; d.init(fs); V z(44100 * 2, 0.f), oL, oR; run(d, z, z, oL, oR);
        CHECK(peak(oL) == 0 && peak(oR) == 0, "silence in, silence out");
        DelayDSP e; e.init(fs); e.setParam(P_FEEDBACK, 600); e.setParam(P_TIME, DelayMap::posOfTimeMs(100));
        V in(44100 * 30, 0.f); in[0] = 0.8f; run(e, in, in, oL, oR);
        CHECK(peak(oL, 44100 * 25) < 1e-6f, "feedback 60 %%: echoes gone after 25 s (peak %.2g)", peak(oL, 44100 * 25));
    }
    // 14. clicks: 3rd difference of the output while Time, Feedback, Mix, filters move in Q-Link steps (compare to a steady tone)
    {
        auto third = [](const V &a, size_t from, size_t to) { double m = 0; for (size_t i = from + 3; i < to; i++) m = std::max(m, (double)fabsf(a[i] - 3 * a[i - 1] + 3 * a[i - 2] - a[i - 3])); return m; };
        const int params[] = {P_TIME, P_FEEDBACK, P_MIX, P_HP, P_LP, P_DRIVE, P_TONE, P_SPREAD, P_OFFSET, P_DEPTH, P_RATE};
        auto sweep = [&](int pi, bool moving, float held) {
            DelayDSP d; d.init(fs); d.setOffsetOn(true); d.setParam(P_DEPTH, 0);
            if (!moving) d.setParam(pi, held);
            V in = sine(44100 * 4, 330, 0.5f), oL(in.size()), oR(in.size());
            float val = 500, dirn = 1;
            for (size_t i = 0; i < in.size(); i += 128) {
                if (moving && i > 44100 && (i / 128) % 8 == 0) { val += 8 * dirn; if (val > 800) dirn = -1; if (val < 300) dirn = 1; d.setParam(pi, val); }
                d.process(&in[i], &in[i], &oL[i], &oR[i], (int)std::min<size_t>(128, in.size() - i));
            }
            return third(oL, 44100 * 2, 44100 * 4);
        };
        // Q-Link steps (8 of 1000, one every 23 ms) on every knob, 330 Hz tone at 0.5: the roughest 3rd difference must stay under
        // 1.2e-3 (-52 dB re the tone; an audible click is 10x that or more). A steady tone is 5e-5; the saturator's own harmonics are
        // allowed for by also looking at the knob held still at each end of the sweep.
        int badParams = 0; double worstAbs = 0, wref = 0; int worstP = -1;
        for (int pi : params) {
            const double ref = std::max(sweep(pi, false, 300), sweep(pi, false, 800));
            const double t = sweep(pi, true, 0);
            if (t > std::max(1.2e-3, 1.1 * ref)) badParams++;
            if (t > worstAbs) { worstAbs = t; worstP = pi; wref = ref; }
        }
        CHECK(badParams == 0, "knob sweeps in Q-Link steps: worst 3rd difference %.5f (param %d; held still %.5f); %d knobs rougher than allowed", worstAbs, worstP, wref, badParams);
    }
    // 14b. a preset-sized jump (1.4 s -> 90 ms, and back) glides, never clicks or shrieks past an octave
    {
        auto third = [](const V &a, size_t from, size_t to) { double m = 0; for (size_t i = from + 3; i < to; i++) m = std::max(m, (double)fabsf(a[i] - 3 * a[i - 1] + 3 * a[i - 2] - a[i - 3])); return m; };
        DelayDSP d; d.init(fs); d.setParam(P_TIME, DelayMap::posOfTimeMs(1400)); d.setParam(P_FEEDBACK, 500);
        V in = sine(44100 * 8, 330, 0.5f), oL(in.size()), oR(in.size());
        double base = 0.5 * pow(2 * M_PI * 330 / fs, 3), worst = 0;
        for (size_t i = 0; i < in.size(); i += 128) {
            if (i == 44100 * 3) d.setParam(P_TIME, DelayMap::posOfTimeMs(90));
            if (i == 44100 * 5) d.setParam(P_TIME, DelayMap::posOfTimeMs(1400));
            d.process(&in[i], &in[i], &oL[i], &oR[i], (int)std::min<size_t>(128, in.size() - i));
        }
        worst = third(oL, 44100 * 2, in.size());
        CHECK(worst < 1.2e-3, "preset-sized Time jumps (1.4 s <-> 90 ms): worst 3rd difference %.5f (steady tone %.5f; octave-limited glide)", worst, base);
    }
    // 15. denormal timing: after loud audio the long quiet tail costs no more than normal
    {
        DelayDSP d; d.init(fs); d.setParam(P_FEEDBACK, 900); d.setParam(P_LP, 600);
        V in = noise(44100, 1.0f, 2), z(44100 * 40, 0.f), oL, oR; run(d, in, in, oL, oR);
        clock_t t0 = clock(); run(d, z, z, oL, oR); double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("info 40 s of decaying tail rendered in %.2f s (x86, %.0fx real time)\n", secs, 40.0 / std::max(secs, 1e-6));
        CHECK(secs < 8.0, "tail render time %.2f s for 40 s of audio", secs);
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
