// Shhhhh offline checks: links the engine directly (no wrapper). Build + run: test/run_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
extern "C" {
#include "engine.h"
int mpc_engine_changed(void *inst, char *key, int len);
}
#include "ShhDSP.h"

static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++fails; } while (0)
static const mpc_engine_t *E;
static void set(void *s, const char *k, double v) { char b[64]; snprintf(b, sizeof b, "%.6f", v); E->set_param(s, k, b); }
static void sets(void *s, const char *k, const char *v) { E->set_param(s, k, v); }
static std::string get(void *s, const char *k) { char b[8192] = {0}; E->get_param(s, k, b, sizeof b); return b; }
static double getf(void *s, const char *k) { return atof(get(s, k).c_str()); }
static std::vector<std::string> drain(void *s) {
    std::vector<std::string> v; char k[64];
    while (mpc_engine_changed(s, k, sizeof k) > 0) v.push_back(k);
    return v;
}
static bool has(const std::vector<std::string> &v, const char *k) { return std::find(v.begin(), v.end(), k) != v.end(); }

// deterministic noise
static uint32_t rng = 12345;
static float white() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) * (1.0f / 16777216.0f)) * 2.0f - 1.0f; }   // uniform, rms 1/sqrt(3)
static float gauss() { float s = 0; for (int i = 0; i < 4; ++i) s += white(); return s * 0.866f; }                       // rms ~1

typedef float (*Gen)(long n, int ch);
// run `blocks` blocks through engine s; gen gives the input sample (-1..1); collects output into out (interleaved floats)
static void run(void *s, long &t, int blocks, Gen gen, std::vector<float> *in = nullptr, std::vector<float> *out = nullptr) {
    int16_t bi[256], bo[256];
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 128; ++i) {
            for (int c = 0; c < 2; ++c) {
                float x = gen(t + i, c) * 32768.0f;
                x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
                bi[2 * i + c] = (int16_t)lrintf(x);
            }
        }
        E->process(s, bi, bo, 128);
        for (int i = 0; i < 256; ++i) {
            if (in) in->push_back(bi[i] / 32768.0f);
            if (out) out->push_back(bo[i] / 32768.0f);
        }
        t += 128;
    }
}
static double rmsDb(const std::vector<float> &v, size_t from, size_t to, int ch = 0) {
    double s = 0; long n = 0;
    for (size_t i = from * 2 + ch; i < to * 2 && i < v.size(); i += 2) { s += (double)v[i] * v[i]; ++n; }
    return 10 * log10(s / (n ? n : 1) + 1e-30);
}

static float g_noiseAmp = 0.003f;   // -50 dBFS-ish
static float genNoise(long, int) { return g_noiseAmp * gauss(); }
static float genSilence(long, int) { return 0; }
static float g_toneHz = 1000, g_toneAmp = 0.3f;
static float genTone(long n, int) { return (float)(g_toneAmp * sin(6.283185307179586 * g_toneHz * (double)n / 44100.0)); }
// a "voice": 220 Hz harmonic tone in 250 ms syllables with 250 ms gaps, over noise
static bool syllable(long n) { return (n / 11025) % 2 == 0; }
static float voice(long n) {
    float v = 0;
    for (int h = 1; h <= 8; ++h) v += (float)(sin(6.283185307179586 * 220.0 * h * (double)n / 44100.0) / h);
    return 0.12f * v;
}
static float genVoiceNoise(long n, int) { return (syllable(n) ? voice(n) : 0.0f) + g_noiseAmp * gauss(); }
static float genVoiceOnly(long n, int) { return syllable(n) ? voice(n) : 0.0f; }
static float genHumNoise(long n, int) { return g_noiseAmp * gauss() + 0.02f * (float)sin(6.283185307179586 * 3000.0 * (double)n / 44100.0); }
static float genImpulse(long n, int c) { return n == 1000 ? (c ? -0.5f : 0.5f) : 0.0f; }
static float genStereo(long n, int c) { return c ? 0.25f * sinf(6.2831853f * 300.0f * n / 44100.0f) : 0.25f * sinf(6.2831853f * 2000.0f * n / 44100.0f); }
static float genFull(long n, int) { return (n / 50) % 2 ? 1.0f : -1.0f; }

static void learnFor(void *s, long &t, int blocks, Gen g) {
    sets(s, "learn", "1");
    run(s, t, blocks, g);
}

int main() {
    E = mpc_engine();
    printf("info engine state: %zu bytes per instance (no allocation after create)\n", sizeof(shh::ShhDSP) + 2048);

    // 1. reconstruction and latency: Reduction 0 -> input delayed by exactly 128 samples
    {
        void *s = E->create(nullptr);
        set(s, "reduction", 0);
        long t = 0;
        std::vector<float> in, out;
        run(s, t, 40, genSilence);   // let Reduction glide to 0
        t = 0;
        run(s, t, 40, genImpulse, &in, &out);
        int peak = 0; float pv = 0;
        for (size_t i = 0; i < out.size() / 2; ++i) if (fabsf(out[2 * i]) > pv) { pv = fabsf(out[2 * i]); peak = (int)i; }
        CHECK(peak == 1128 && fabsf(out[2 * peak] - 0.5f) < 1e-3f && fabsf(out[2 * peak + 1] + 0.5f) < 1e-3f,
              "latency: impulse at 1000 comes out at %d (128 samples = 2.9 ms), L %.4f R %.4f", peak, out[2 * peak], out[2 * peak + 1]);
        t = 0; in.clear(); out.clear();
        rng = 1;
        run(s, t, 400, genNoise, &in, &out);
        double err = 0;
        for (size_t i = 2000; i + 128 < in.size() / 2; ++i) err = std::max(err, (double)fabsf(out[2 * (i + 128)] - in[2 * i]));
        CHECK(err <= 1.5 / 32768, "Reduction 0: output = input delayed 128 (worst %.1f LSB)", err * 32768);
        E->destroy(s);
    }

    // 2. silence in -> silence out (exact), and no denormal slowdown after loud audio
    {
        void *s = E->create(nullptr);
        long t = 0;
        std::vector<float> out;
        run(s, t, 200, genSilence, nullptr, &out);
        bool zero = true;
        for (float v : out) if (v != 0) zero = false;
        CHECK(zero, "silence in -> exact silence out");
        g_toneAmp = 0.9f;
        run(s, t, 400, genTone);
        auto t0 = std::chrono::steady_clock::now();
        run(s, t, 4000, genSilence);
        auto t1 = std::chrono::steady_clock::now();
        run(s, t, 4000, genTone);
        auto t2 = std::chrono::steady_clock::now();
        const double ms_sil = std::chrono::duration<double, std::milli>(t1 - t0).count(), ms_tone = std::chrono::duration<double, std::milli>(t2 - t1).count();
        CHECK(ms_sil < ms_tone * 2.0, "denormals: silence after loud runs at %.2fx the loud speed", ms_sil / ms_tone);
        g_toneAmp = 0.3f;
        E->destroy(s);
    }

    // 3. Learn on noise alone: profile level, calibration, reduction amount
    {
        void *s = E->create(nullptr);
        long t = 0;
        rng = 7;
        g_noiseAmp = 0.003f;
        run(s, t, 100, genNoise);
        learnFor(s, t, 1100, genNoise);   // 3.2 s: Learn stops by itself at 2.97 s
        auto ch = drain(s);
        CHECK(get(s, "learn") == "0" && has(ch, "learn"), "Learn turns itself off after ~3 s and reports it (learn=%s)", get(s, "learn").c_str());
        // expected per-bin level of gaussian noise rms a: a^2 * sum(wa^2) * norm, with norm = 4/(sum wa)^2
        shh::ShhDSP ref; ref.init();
        double sw = 0, sw2 = 0;
        for (int n = 0; n < shh::kN; ++n) {
            float a = n < 384 ? sinf(3.14159265f * n / 768) : sinf(3.14159265f * (n - 256) / 256);
            sw += a; sw2 += a * a;
        }
        const double expect = 10 * log10(g_noiseAmp * g_noiseAmp * sw2 * 4 / (sw * sw));
        double sum = 0; int n = 0;
        std::string st = get(s, "state");
        const char *p = strstr(st.c_str(), "prof=");
        std::vector<double> prof;
        for (p = p ? p + 5 : nullptr; p && *p; ) { prof.push_back(atof(p)); p = strchr(p, ','); if (p) ++p; }
        for (int k = 4; k < 250 && k < (int)prof.size(); ++k) { sum += prof[k]; ++n; }
        const double mean = n ? sum / n : -999;
        printf("info learned noise floor %.2f dB per bin, expected %.2f (bias %.2f dB)\n", mean, expect, mean - expect);
        CHECK((int)prof.size() == shh::kBins && fabs(mean - expect) < 1.0, "profile within 1 dB of the true noise level (%.2f vs %.2f)", mean, expect);
        // after learning: noise reduced by about the Reduction (12 dB default)
        std::vector<float> in, out;
        run(s, t, 600, genNoise, &in, &out);
        const double red = rmsDb(out, 20000, in.size() / 2) - rmsDb(in, 20000, in.size() / 2);
        CHECK(red < -9.0 && red > -13.5, "learned noise turned down %.1f dB (Reduction 12)", red);
        set(s, "reduction", 30);
        in.clear(); out.clear();
        run(s, t, 600, genNoise, &in, &out);
        const double red30 = rmsDb(out, 20000, in.size() / 2) - rmsDb(in, 20000, in.size() / 2);
        CHECK(red30 < -24.0, "Reduction 30: noise down %.1f dB", red30);
        set(s, "reduction", 100);
        in.clear(); out.clear();
        run(s, t, 600, genNoise, &in, &out);
        const double red100 = rmsDb(out, 20000, in.size() / 2) - rmsDb(in, 20000, in.size() / 2);
        CHECK(red100 < -40.0 && getf(s, "reduction") == 100, "Reduction 100 (the top of the fader): noise down %.1f dB", red100);
        set(s, "reduction", 12);
        // nodes sit on the learned level (Threshold 0) at the default positions (no peaks in white noise)
        bool atDefault = true; double worst = 0;
        for (int i = 1; i <= 6; ++i) {
            char k[16]; snprintf(k, sizeof k, "n%dx", i);
            if (get(s, k) != "2") atDefault = false;
            snprintf(k, sizeof k, "n%d", i);
            worst = std::max(worst, fabs(getf(s, k) - expect));
        }
        CHECK(atDefault, "white noise: every node stays at its default position");
        CHECK(worst < 2.5, "nodes drawn at the learned level (worst %.1f dB off)", worst);
        E->destroy(s);
    }

    // 4. the voice survives, the noise between syllables goes
    {
        void *s = E->create(nullptr);
        long t = 0;
        rng = 9;
        learnFor(s, t, 1100, genNoise);
        set(s, "reduction", 20);
        std::vector<float> in, out, clean;
        long t2 = t;
        run(s, t, 1400, genVoiceNoise, &in, &out);   // 4 s
        // compare with the voice alone, aligned (output is 128 late)
        double sv = 0, se = 0, sg = 0, sgo = 0;
        for (long i = 44100; i < (long)in.size() / 2 - 200; ++i) {
            const long n = t2 + i;
            if (syllable(n) && syllable(n - 2000) && syllable(n + 2000)) {
                const float v = voice(n), y = out[2 * (i + 128)];
                sv += v * v; se += (y - v) * (y - v);
            } else if (!syllable(n) && !syllable(n - 3000) && !syllable(n + 3000)) {
                sg += in[2 * i] * in[2 * i]; sgo += out[2 * (i + 128)] * out[2 * (i + 128)];
            }
        }
        const double snrIn = 10 * log10(sv / (g_noiseAmp * g_noiseAmp * (sv / (0.12 * 0.12 * 0.8) + 1e-9) + 1e-30));
        (void)snrIn;
        const double err = 10 * log10(se / sv), gapRed = 10 * log10(sgo / sg);
        CHECK(err < -18, "voice comes through: error vs the clean voice %.1f dB", err);
        CHECK(gapRed < -15, "between syllables the noise is down %.1f dB (Reduction 20)", gapRed);
        for (double r : {40.0, 70.0, 100.0}) {   // the trade-off at the top of the fader (info only)
            set(s, "reduction", r);
            std::vector<float> i2, o2; long t3 = t;
            run(s, t, 1400, genVoiceNoise, &i2, &o2);
            double v2 = 0, e2 = 0, g2 = 0, go2 = 0;
            for (long i = 44100; i < (long)i2.size() / 2 - 200; ++i) {
                const long n = t3 + i;
                if (syllable(n) && syllable(n - 2000) && syllable(n + 2000)) { const float v = voice(n), y = o2[2 * (i + 128)]; v2 += v * v; e2 += (y - v) * (y - v); }
                else if (!syllable(n) && !syllable(n - 3000) && !syllable(n + 3000)) { g2 += i2[2 * i] * i2[2 * i]; go2 += o2[2 * (i + 128)] * o2[2 * (i + 128)]; }
            }
            printf("info Reduction %3.0f: voice error %.1f dB, noise between syllables %.1f dB\n", r, 10 * log10(e2 / v2), 10 * log10((go2 + 1e-30) / g2));
        }
        E->destroy(s);
    }

    // 5. Learn with the voice talking over the noise: the profile still finds the noise floor
    {
        void *a = E->create(nullptr), *b = E->create(nullptr);
        long ta = 0, tb = 0;
        rng = 11; learnFor(a, ta, 1100, genNoise);
        rng = 13; learnFor(b, tb, 1100, genVoiceNoise);
        std::string sa = get(a, "state"), sb = get(b, "state");
        auto prof = [](const std::string &st) { std::vector<double> v; const char *p = strstr(st.c_str(), "prof="); for (p = p ? p + 5 : nullptr; p && *p;) { v.push_back(atof(p)); p = strchr(p, ','); if (p) ++p; } return v; };
        auto pa = prof(sa), pb = prof(sb);
        double worst = 0, mean = 0; int n = 0;
        for (int k = 4; k < 250 && k < (int)pa.size() && k < (int)pb.size(); ++k) { worst = std::max(worst, pb[k] - pa[k]); mean += pb[k] - pa[k]; ++n; }
        mean /= n ? n : 1;
        CHECK(n > 200 && mean < 1.5 && worst < 6, "learned over the voice: noise profile %.1f dB above noise-only on average (worst bin %.1f dB, on a harmonic)", mean, worst);
        E->destroy(a); E->destroy(b);
    }

    // 6. Learn stops early / too early
    {
        void *s = E->create(nullptr);
        long t = 0; rng = 3;
        const double before = getf(s, "n3");
        learnFor(s, t, 40, genNoise);   // 116 ms: one sub-window, not enough
        sets(s, "learn", "0");
        CHECK(fabs(getf(s, "n3") - before) < 1e-3 && get(s, "learn") == "0", "Learn off after 0.1 s: nothing changes (n3 %.1f)", getf(s, "n3"));
        learnFor(s, t, 200, genNoise);   // 0.58 s
        CHECK(getf(s, "learn_prog") > 0.1 && get(s, "learn") == "1", "while learning: progress bar %.2f", getf(s, "learn_prog"));
        sets(s, "learn", "0");
        CHECK(strstr(get(s, "state").c_str(), "lrn=1") && fabs(getf(s, "n3") + 70.6) < 3, "Learn off after 0.6 s: what was heard is used (n3 %.1f dB, noise -70.6)", getf(s, "n3"));
        CHECK(getf(s, "learn_prog") == 0, "progress bar back to empty");
        E->destroy(s);
    }

    // 7. Threshold moves every node by the same amount; dragging a node keeps its offset
    {
        void *s = E->create(nullptr);
        long t = 0; rng = 5;
        learnFor(s, t, 1100, genNoise);
        drain(s);
        double n0[6], n1[6];
        char k[16];
        for (int i = 0; i < 6; ++i) { snprintf(k, sizeof k, "n%d", i + 1); n0[i] = getf(s, k); }
        set(s, "threshold", 10);
        auto ch = drain(s);
        double worst = 0;
        for (int i = 0; i < 6; ++i) { snprintf(k, sizeof k, "n%d", i + 1); n1[i] = getf(s, k); worst = std::max(worst, fabs(n1[i] - n0[i] - 10)); }
        CHECK(worst < 1e-3 && has(ch, "n1") && has(ch, "n6"), "Threshold +10: all six nodes rise 10 dB and are reported (worst %.4f)", worst);
        set(s, "n4", n1[3] - 15);
        CHECK(fabs(getf(s, "n4") - (n1[3] - 15)) < 1e-3, "drag node 4 down 15 dB: it stays there (%.2f)", getf(s, "n4"));
        set(s, "threshold", 0);
        CHECK(fabs(getf(s, "n4") - (n0[3] - 15)) < 1e-3 && fabs(getf(s, "n1") - n0[0]) < 1e-3,
              "Threshold back to 0: node 4 keeps its -15 offset (%.2f), node 1 back home", getf(s, "n4") - n0[3]);
        // more reduction where the node is lower? no: a LOWER node = lower threshold = LESS reduction there
        set(s, "n6", -120);
        set(s, "n5", -120);
        std::vector<float> in, out;
        run(s, t, 600, genNoise, &in, &out);
        CHECK(fabs(getf(s, "n6") + 120) < 1e-3, "a node can go to the bottom of the grid");
        set(s, "n1", 5);
        CHECK(getf(s, "n1") <= 0 && getf(s, "n1") > -1, "a node can't go above the grid (%.2f)", getf(s, "n1"));
        // a new Learn resets the offsets
        learnFor(s, t, 1100, genNoise);
        CHECK(strstr(get(s, "state").c_str(), "off=0.00,0.00,0.00,0.00,0.00,0.00") != nullptr, "a new Learn resets the node offsets");
        E->destroy(s);
    }

    // 8. a whine at 3 kHz pulls node 5 onto it
    {
        void *s = E->create(nullptr);
        long t = 0; rng = 17;
        learnFor(s, t, 1100, genHumNoise);
        const int p5 = atoi(get(s, "n5x").c_str());
        CHECK(p5 != 2 && get(s, "n4x") == "2", "3 kHz whine in the learn: node 5 moves off its default 3.5k (position %d), node 4 stays", p5);
        std::vector<float> in, out;
        run(s, t, 400, genHumNoise, &in, &out);
        const double red = rmsDb(out, 20000, in.size() / 2) - rmsDb(in, 20000, in.size() / 2);
        CHECK(red < -10, "whine + hiss learned: both turned down %.1f dB", red);
        E->destroy(s);
    }

    // 9. Pass, Diff
    {
        void *s = E->create(nullptr), *s2 = E->create(nullptr);
        long t = 0, t2 = 0; rng = 19;
        learnFor(s, t, 1100, genNoise);
        rng = 19; learnFor(s2, t2, 1100, genNoise);   // two identical instances: one plays wet, the other Diff
        set(s, "reduction", 20); set(s2, "reduction", 20);
        sets(s2, "diff", "1");
        std::vector<float> inW, outW, inD, outD;
        rng = 100; long tw = t; run(s, tw, 300, genVoiceNoise, &inW, &outW);
        rng = 100; long td = t2; run(s2, td, 300, genVoiceNoise, &inD, &outD);
        E->destroy(s2);
        // compare after the crossfade: wet + diff = dry (delayed)
        double worst = 0;
        for (size_t i = 5000; i + 128 < inW.size() / 2; ++i) {
            const double sum = outW[2 * (i + 128)] + outD[2 * (i + 128)] - inW[2 * i];
            worst = std::max(worst, fabs(sum));
        }
        CHECK(worst < 3.0 / 32768, "Diff = exactly what is removed: wet + diff = input (worst %.1f LSB)", worst * 32768);
        sets(s, "diff", "1");
        sets(s, "bypass", "1");
        std::vector<float> inP, outP;
        run(s, t, 300, genVoiceNoise, &inP, &outP);
        bool exact = true;
        for (size_t i = 3000; i + 128 < inP.size() / 2; ++i) if (outP[2 * (i + 128)] != inP[2 * i] || outP[2 * (i + 128) + 1] != inP[2 * i + 1]) exact = false;
        CHECK(exact, "Pass (with Diff still on): the input untouched, bit-exact, same 2.9 ms delay");
        CHECK(get(s, "diff") == "1", "Diff keeps its setting under Pass (comes back when Pass goes off)");
        E->destroy(s);
    }

    // 10. clicks: Pass / Diff / Threshold / Reduction / node moves on a steady tone (the gain is 1 on a loud tone)
    {
        void *s = E->create(nullptr);
        long t = 0; rng = 23;
        learnFor(s, t, 1100, genNoise);
        g_toneHz = 200; g_toneAmp = 0.4f;
        std::vector<float> ref, mov;
        auto d3 = [](const std::vector<float> &v, size_t from) { double m = 0; for (size_t i = from + 3; i < v.size() / 2; ++i) m = std::max(m, (double)fabsf(v[2*i] - 3*v[2*(i-1)] + 3*v[2*(i-2)] - v[2*(i-3)])); return m; };
        run(s, t, 200, genTone, nullptr, &ref);
        const double base = d3(ref, 1000);
        const char *sw[][2] = {{"bypass", "1"}, {"bypass", "0"}, {"diff", "1"}, {"diff", "0"}};
        double worst = 0;
        for (auto &k : sw) {
            mov.clear(); sets(s, k[0], k[1]); run(s, t, 20, genTone, nullptr, &mov);
            std::vector<float> st; run(s, t, 40, genTone, nullptr, &st);
            printf("info   %s=%s: switching %.5f, settled %.5f\n", k[0], k[1], d3(mov, 0), d3(st, 0));
            worst = std::max(worst, d3(mov, 0) / std::max(base, d3(st, 0)));
        }
        worst *= base;
        run(s, t, 40, genTone);
        CHECK(worst < base * 2.0, "Pass/Diff on a 200 Hz tone: 3rd difference %.4f vs %.4f steady (no click)", worst, base);
        // Diff on a loud tone = nearly silence: switching it is a fade, not a step
        worst = 0;
        for (int q = 0; q < 64; ++q) {   // Q-Link steps of 1/128 on Threshold and Reduction, one per block
            mov.clear();
            set(s, "threshold", -30 + q * 60.0 / 128);
            set(s, "reduction", q * 40.0 / 128);
            run(s, t, 1, genTone, nullptr, &mov);
            worst = std::max(worst, d3(mov, 0));
        }
        CHECK(worst < base * 3.0, "Threshold/Reduction swept in Q-Link steps: 3rd difference %.4f vs %.4f (baseline = int16 rounding)", worst, base);
        g_toneHz = 1000; g_toneAmp = 0.3f;
        E->destroy(s);
    }

    // 10b. Output volume: exact dB on the processed sound, nothing in Pass, no clicks while it moves
    {
        void *s = E->create(nullptr);
        set(s, "reduction", 0);
        long t = 0;
        g_toneHz = 440; g_toneAmp = 0.05f;   // +24 dB still under full scale
        auto gainOf = [&](double db) {
            set(s, "output", db);
            run(s, t, 40, genTone);
            std::vector<float> in, out;
            run(s, t, 200, genTone, &in, &out);
            return rmsDb(out, 1000, in.size() / 2) - rmsDb(in, 1000, in.size() / 2);
        };
        const double g6 = gainOf(6), gm24 = gainOf(-24), g24 = gainOf(24), g0 = gainOf(0);
        CHECK(fabs(g6 - 6) < 0.02 && fabs(gm24 + 24) < 0.05 && fabs(g24 - 24) < 0.05 && fabs(g0) < 0.01,
              "Output +6 / -24 / +24 / 0 dB measure %.2f / %.2f / %.2f / %.2f dB", g6, gm24, g24, g0);
        CHECK(get(s, "output_display") == "+0.0 dB", "Output text \"%s\"", get(s, "output_display").c_str());
        set(s, "output", 6);
        sets(s, "bypass", "1");
        std::vector<float> in, out;
        run(s, t, 100, genTone);
        run(s, t, 100, genTone, &in, &out);
        bool exact = true;
        for (size_t i = 0; i + 128 < in.size() / 2; ++i) if (out[2 * (i + 128)] != in[2 * i]) exact = false;
        CHECK(exact, "Pass ignores Output (+6 dB set): the input untouched");
        sets(s, "bypass", "0");
        run(s, t, 100, genTone);
        g_toneHz = 200; g_toneAmp = 0.4f;
        set(s, "output", 0);
        run(s, t, 100, genTone);
        std::vector<float> ref, mov;
        auto d3 = [](const std::vector<float> &v, size_t from) { double m = 0; for (size_t i = from + 3; i < v.size() / 2; ++i) m = std::max(m, (double)fabsf(v[2*i] - 3*v[2*(i-1)] + 3*v[2*(i-2)] - v[2*(i-3)])); return m; };
        run(s, t, 100, genTone, nullptr, &ref);
        double worst = 0;
        for (int q = 0; q < 64; ++q) { mov.clear(); set(s, "output", -6 + q * 48.0 / 128 / 4); run(s, t, 1, genTone, nullptr, &mov); worst = std::max(worst, d3(mov, 0)); }
        CHECK(worst < d3(ref, 0) * 3.0, "Output turned in Q-Link steps on a 200 Hz tone: 3rd difference %.5f vs %.5f steady", worst, d3(ref, 0));
        mov.clear(); set(s, "output", -24); run(s, t, 4, genTone, nullptr, &mov);
        CHECK(d3(mov, 0) < d3(ref, 0) * 3.0, "Output jumped +6 -> -24 dB: a 10 ms glide, no click (%.5f)", d3(mov, 0));
        g_toneHz = 1000; g_toneAmp = 0.3f;
        E->destroy(s);
    }

    // 11. full scale doesn't wrap; garbage values are refused; two instances are independent
    {
        void *s = E->create(nullptr);
        long t = 0;
        std::vector<float> in, out;
        run(s, t, 300, genFull, &in, &out);
        bool wrap = false;
        for (size_t i = 2000; i + 128 < in.size() / 2; ++i) if (in[2 * i] * out[2 * (i + 128)] < -0.25f) wrap = true;
        CHECK(!wrap, "full-scale square: no wrap-around");
        const char *bad[] = {"nan", "inf", "-inf", "1e999", "", "abc", "-1e30"};
        for (const char *b : bad) { sets(s, "threshold", b); sets(s, "reduction", b); sets(s, "n2", b); sets(s, "n2x", b); sets(s, "learn", b); }
        CHECK(fabs(getf(s, "threshold")) <= 30 && getf(s, "reduction") >= 0 && getf(s, "reduction") <= 40 && std::isfinite(getf(s, "n2")),
              "garbage text (nan, inf, '', abc) never sticks: threshold %s reduction %s n2 %s", get(s, "threshold").c_str(), get(s, "reduction").c_str(), get(s, "n2").c_str());
        sets(s, "threshold", "99"); sets(s, "reduction", "-5");
        CHECK(getf(s, "threshold") == 30 && getf(s, "reduction") == 0, "out-of-range values clamp (30, 0)");
        for (const char *b : bad) sets(s, "output", b);
        CHECK(fabs(getf(s, "output")) <= 24, "garbage on Output never sticks (%s)", get(s, "output").c_str());
        sets(s, "output", "99"); sets(s, "reduction", "500");
        CHECK(getf(s, "output") == 24 && getf(s, "reduction") == 100, "Output clamps to +24, Reduction to 100");
        sets(s, "output", "0");
        sets(s, "nope", "1"); sets(s, "n7", "1"); sets(s, "n0x", "3"); sets(s, "sp99", "1");
        CHECK(get(s, "n7").empty() && get(s, "sp99").empty() && get(s, "sp33").empty(), "unknown keys are ignored");
        void *b = E->create(nullptr);
        set(s, "threshold", 20);
        CHECK(getf(b, "threshold") == 0, "two instances are independent");
        E->destroy(b);
        run(s, t, 50, genNoise);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        CHECK(fin, "output always finite");
        E->destroy(s);
    }

    // 12. saved state: round trip, broken states
    {
        void *a = E->create(nullptr);
        long t = 0; rng = 29;
        learnFor(a, t, 1100, genHumNoise);
        set(a, "threshold", -7.5); set(a, "reduction", 64); sets(a, "diff", "1"); set(a, "output", -3.5);
        set(a, "n2", getf(a, "n2") + 6);
        std::string st = get(a, "state");
        printf("info state: %zu bytes (wrapper chunk 8192)\n", st.size());
        void *b = E->create(nullptr);
        sets(b, "state", st.c_str());
        bool same = true;
        const char *keys[] = {"threshold", "reduction", "output", "diff", "bypass", "n1", "n2", "n3", "n4", "n5", "n6", "n1x", "n2x", "n3x", "n4x", "n5x", "n6x"};
        for (const char *k : keys) if (fabs(getf(a, k) - getf(b, k)) > 0.06) { same = false; printf("     %s %s vs %s\n", k, get(a, k).c_str(), get(b, k).c_str()); }
        CHECK(same && st.size() < 8000, "save + reload restores every control and the learned profile");
        std::vector<float> oa, ob; long ta = t, tb = t;
        rng = 77; run(a, ta, 300, genHumNoise, nullptr, &oa);
        rng = 77; run(b, tb, 300, genHumNoise, nullptr, &ob);
        double d = 0; for (size_t i = 20000; i < oa.size(); ++i) d = std::max(d, (double)fabsf(oa[i] - ob[i]));
        CHECK(d < 4.0 / 32768, "reloaded instance sounds the same (worst %.1f LSB, profile saved at 0.01 dB)", d * 32768);
        const char *broken[] = {"SHH1;", "SHH1;prof=1,2,3", "SHH1;thr=nan;red=inf;off=nan,inf;pos=99;prof=nan", "XXXX;thr=5", "SHH1;pos=;off=", "SHH1;lrn=1;prof=-999,999"};
        for (const char *bs : broken) sets(b, "state", bs);
        rng = 3; std::vector<float> ob2; run(b, tb, 200, genNoise, nullptr, &ob2);
        bool fin = true; for (float v : ob2) if (!std::isfinite(v)) fin = false;
        CHECK(fin && fabs(getf(b, "threshold")) <= 30, "broken states are survived (threshold %s)", get(b, "threshold").c_str());
        E->destroy(a); E->destroy(b);
    }

    // 13. spectrum preview: a 1 kHz tone lights its column
    {
        void *s = E->create(nullptr);
        long t = 0;
        g_toneHz = 1090; g_toneAmp = 0.5f;   // the middle of a column
        unsigned r0 = atoi(get(s, "display_rev").c_str());
        run(s, t, 100, genTone);
        int best = 0; double bv = -1;
        for (int j = 1; j <= 32; ++j) { char k[16]; snprintf(k, sizeof k, "sp%02d", j); if (getf(s, k) > bv) { bv = getf(s, k); best = j; } }
        const float lo = shh::ShhDSP::bandLoHz(best - 1), hi = shh::ShhDSP::bandLoHz(best);
        CHECK(lo <= 1090 && hi >= 1090 && bv > 0.8, "1.09 kHz tone: column %d (%.0f-%.0f Hz) is the tallest (%.2f)", best, lo, hi, bv);
        CHECK((unsigned)atoi(get(s, "display_rev").c_str()) != r0, "display_rev moves so the wrapper reports it");
        run(s, t, 1000, genSilence);
        bool down = true;
        for (int j = 1; j <= 32; ++j) { char k[16]; snprintf(k, sizeof k, "sp%02d", j); if (getf(s, k) > 0) down = false; }
        unsigned r1 = atoi(get(s, "display_rev").c_str());
        run(s, t, 100, genSilence);
        CHECK(down && (unsigned)atoi(get(s, "display_rev").c_str()) == r1, "after the tone: columns fall to empty, then display_rev stops moving (no reports in silence)");
        E->destroy(s);
    }

    // 14. odd block sizes (not MPC, but harmless): continuous output
    {
        void *s = E->create(nullptr);
        set(s, "reduction", 0);
        int16_t in[2 * 300], out[2 * 300];
        long n = 0; std::vector<float> o, ii;
        const int sizes[] = {1, 7, 128, 64, 200, 128, 3, 300};
        for (int r = 0; r < 60; ++r) for (int sz : sizes) {
            for (int i = 0; i < sz; ++i) { int16_t v = (int16_t)lrintf(8000 * sinf(6.2831853f * 440 * (n + i) / 44100.0f)); in[2*i] = in[2*i+1] = v; ii.push_back(v); }
            E->process(s, in, out, sz);
            for (int i = 0; i < sz; ++i) o.push_back(out[2 * i]);
            n += sz;
        }
        int lag = -1;
        for (int L = 0; L < 400 && lag < 0; ++L) { bool ok = true; for (size_t i = 3000; i < 6000; ++i) if (fabs(o[i + L] - ii[i]) > 2) { ok = false; break; } if (ok) lag = L; }
        CHECK(lag >= 128 && lag <= 256, "uneven block sizes: still a clean, continuous copy (lag %d)", lag);
        E->destroy(s);
    }

    // 15. CPU (x86 here; see BENCH for the device)
    {
        void *s = E->create(nullptr);
        long t = 0; rng = 31;
        learnFor(s, t, 1100, genNoise);
        std::vector<double> ms;
        int16_t bi[256], bo[256];
        for (int b = 0; b < 20000; ++b) {
            for (int i = 0; i < 256; ++i) bi[i] = (int16_t)(gauss() * 3000);
            if (b % 500 == 0) set(s, "threshold", (b / 500) % 2 ? 6 : -6);
            if (b == 10000) sets(s, "learn", "1");
            auto a0 = std::chrono::steady_clock::now();
            E->process(s, bi, bo, 128);
            auto a1 = std::chrono::steady_clock::now();
            ms.push_back(std::chrono::duration<double, std::milli>(a1 - a0).count());
        }
        std::vector<double> sorted = ms; std::sort(sorted.begin(), sorted.end());
        double avg = 0; for (double v : ms) avg += v; avg /= ms.size();
        printf("info CPU per 128-frame block (2.902 ms budget): mean %.4f ms (%.2f %%), p99 %.4f ms, p99.9 %.4f ms\n",
               avg, avg / 2.902 * 100, sorted[sorted.size() * 99 / 100], sorted[sorted.size() * 999 / 1000]);
        CHECK(avg < 0.5, "CPU mean %.4f ms per block on x86 (sanitizer build is slower than the device's -O3)", avg);
        E->destroy(s);
    }

    // 15b. the gain stage directly: every gain stays in [10^(-Reduction/20), 1] and finite, hop after hop
    {
        static shh::ShhDSP d;
        d.init();
        d.setReduction(40);
        float L[128], R[128], oL[128], oR[128];
        long n = 0; rng = 41;
        d.startLearn();
        for (int h = 0; h < 1100; ++h) { for (int i = 0; i < 128; ++i, ++n) L[i] = R[i] = genNoise(n, 0); d.process(L, R, oL, oR); }
        const float lo = powf(10.0f, -40.0f / 20.0f) * 0.999f;
        bool okg = true; float gmin = 1, gmax = 0;
        for (int h = 0; h < 3000; ++h) {
            for (int i = 0; i < 128; ++i, ++n) { L[i] = genVoiceNoise(n, 0); R[i] = h % 700 < 350 ? L[i] : 0.5f * L[i]; }
            d.process(L, R, oL, oR);
            for (int k = 0; k < shh::kBins; ++k) {
                const float g = d.gains()[k];
                if (!(g >= lo && g <= 1.0f)) okg = false;
                gmin = std::min(gmin, g); gmax = std::max(gmax, g);
            }
        }
        CHECK(okg, "gain stage (LSA + two-step SNR + masking + wide smoothing): every gain within [-40 dB, 0 dB] (%.1f .. %.2f dB)",
              20 * log10(gmin), 20 * log10(gmax));
    }

    // 16. create / destroy many times (ASan reports leaks at exit)
    for (int i = 0; i < 200; ++i) { void *s = E->create(nullptr); long t = 0; run(s, t, 2, genNoise); E->destroy(s); }
    CHECK(true, "200 create/destroy cycles (LeakSanitizer checks at exit)");

    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
