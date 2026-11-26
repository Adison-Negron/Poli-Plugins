// Poli-verb DSP tests (offline): decay accuracy, stability under extremes, every preset bounded, sync, probability, freeze.
#include "VerbDSP.h"
#include "presets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace verb;
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static const double fs = 44100;
static void run(VerbDSP &v, const std::vector<float> &iL, const std::vector<float> &iR, std::vector<float> &oL, std::vector<float> &oR) {
    size_t n = iL.size(); oL.resize(n); oR.resize(n);
    for (size_t i = 0; i < n; i += 128) { int c = (int)std::min<size_t>(128, n - i); v.process(&iL[i], &iR[i], &oL[i], &oR[i], c); }
}
static double rt60(const std::vector<float> &x, int start) {
    size_t n = x.size(); std::vector<double> e(n); double s = 0;
    for (size_t i = n; i-- > 0;) { s += (double)x[i] * x[i]; e[i] = s; }
    double e0 = e[start], t5 = -1, t25 = -1;
    for (size_t i = start; i < n; i++) { double db = 10 * log10(e[i] / e0 + 1e-30); if (t5 < 0 && db < -5) t5 = i; if (t25 < 0 && db < -25) { t25 = i; break; } }
    return (t5 < 0 || t25 < 0) ? -1 : (t25 - t5) / fs * 3.0;
}
static bool finite_bounded(const std::vector<float> &a, float lim) { for (float x : a) { if (!(x == x) || x > lim || x < -lim) return false; } return true; }
static float peak(const std::vector<float> &a) { float p = 0; for (float x : a) p = std::max(p, fabsf(x)); return p; }
static std::vector<float> noise(size_t n, float amp, unsigned seed) { std::vector<float> v(n); srand(seed); for (auto &x : v) x = ((rand() / (float)RAND_MAX) - 0.5f) * 2 * amp; return v; }

int main() {
    std::vector<float> z(44100, 0), a, b;
    // 1. Decay knob vs measured RT60, every space (er off, mix 100 %). Hall/Cathedral/Arena/Tunnel track within ~10 %; the space voicings (damping, bass) move Plate/Booth/Ambience up to +60 %, Canyon -32 %
    for (int type = 0; type < kNumTypes; type++) {
        VerbDSP w; w.init(fs); w.setType(type); run(w, z, z, a, b);
        w.setParam(P_MIX, 1000); w.setParam(P_ERLVL, 0); w.setParam(P_DECAY, 500);
        std::vector<float> in(44100 * 30, 0.f); in[0] = 0.5f; std::vector<float> oL, oR; run(w, in, in, oL, oR);
        double rt = rt60(oL, 2000), want = w.decaySeconds();
        CHECK(rt > 0 && fabs(rt - want) / want < 0.7, "%s: decay knob %.2f s, measured RT60 %.2f s", kSpaces[type].name, want, rt);
    }
    // 2. every space x extremes: finite, under the limiter ceiling
    for (int type = 0; type < kNumTypes; type++) {
        VerbDSP w; w.init(fs); w.setType(type);
        for (int p = 0; p < P_COUNT; p++) w.setParam(p, (p % 2) ? 1000.f : 0.f);
        w.setParam(P_MIX, 1000); w.setParam(P_INPUT, 1000); w.setParam(P_DRIVE, 1000); w.setParam(P_DECAY, 1000); w.setParam(P_RELEASE, 1000); w.setParam(P_ATTACK, 0);
        auto in = noise(44100 * 6, 1.0f, 7 + type); std::vector<float> oL, oR; run(w, in, in, oL, oR);
        CHECK(finite_bounded(oL, 1.0f) && finite_bounded(oR, 1.0f), "%s extremes: finite, peak %.4f", kSpaces[type].name, std::max(peak(oL), peak(oR)));
    }
    // 3. every preset: loud noise stays under the ceiling, tail is finite
    int bad = 0; float worst = 0;
    for (int i = 0; i < kNumPresets; i++) {
        VerbDSP w; w.init(fs); w.setType(kPresets[i].type); w.setSync(kPresets[i].sync != 0); w.setTempo(120);
        for (int p = 0; p < P_COUNT; p++) w.setParam(p, kPresets[i].pos[p]);
        auto in = noise(44100 * 3, 0.9f, 100 + i); std::vector<float> oL, oR; run(w, in, in, oL, oR);
        float pk = std::max(peak(oL), peak(oR)); worst = std::max(worst, pk);
        if (!finite_bounded(oL, 0.9886f) || !finite_bounded(oR, 0.9886f)) bad++;
    }
    CHECK(bad == 0, "all %d presets: noise at -1 dBFS stays <= ceiling (worst %.4f, %d bad)", kNumPresets, worst, bad);
    // 4. tempo sync: pre-delay follows tempo and note
    {
        VerbDSP w; w.init(fs); w.setSync(true); w.setParam(P_PREDELAY, 1000.f * 8 / 11); w.setTempo(120);   // 1/4 note
        std::vector<float> in(44100, 0.f); in[0] = 0.5f; std::vector<float> oL, oR; run(w, in, in, oL, oR);
        double ms120 = w.predelaySamples() / fs * 1000.0;
        w.setTempo(60); run(w, in, in, oL, oR);
        double ms60 = w.predelaySamples() / fs * 1000.0;
        CHECK(fabs(ms120 - 500) < 40 && fabs(ms60 - 1000) < 80, "sync 1/4: %.0f ms at 120 bpm, %.0f ms at 60 bpm", ms120, ms60);
    }
    // 5. reflection probability: 0 % has fewer reflections than 100 %
    {
        double e[2];
        for (int k = 0; k < 2; k++) {
            VerbDSP w; w.init(fs); w.setParam(P_MIX, 1000); w.setParam(P_ERLVL, 1000); w.setParam(P_ERPROB, k ? 1000 : 0); w.setParam(P_DECAY, 0);
            std::vector<float> in(8820, 0.f); in[0] = 0.5f; std::vector<float> oL, oR; run(w, in, in, oL, oR);
            e[k] = 0; for (size_t i = 0; i < 4410; i++) e[k] += oL[i] * oL[i];
        }
        CHECK(e[0] < e[1], "probability 0 %% (%.2e) < 100 %% (%.2e) early energy", e[0], e[1]);
    }
    // 6. freeze holds the tail
    {
        VerbDSP w; w.init(fs); w.setParam(P_MIX, 1000); w.setParam(P_DECAY, 100);
        std::vector<float> in(44100, 0.f); in[0] = 0.5f; std::vector<float> oL, oR; run(w, in, in, oL, oR);
        w.setFreeze(true); std::vector<float> zz(44100 * 6, 0.f), l2, r2; run(w, zz, zz, l2, r2);
        double e1 = 0, e2 = 0; for (int i = 0; i < 44100; i++) { e1 += l2[i] * l2[i]; e2 += l2[44100 * 5 + i] * l2[44100 * 5 + i]; }
        CHECK(e1 > 0 && e2 > 0.3 * e1, "freeze: tail energy after 5 s is %.0f %% of the first second", 100 * e2 / (e1 + 1e-30));
    }
    printf(fails ? "FAILED (%d)\n" : "DSP PASSED\n", fails);
    return fails != 0;
}
