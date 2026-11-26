// Poli-Multiband offline checks: links the engine directly (no wrapper). Build: test/run_dsp_test.sh
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
extern "C" {
#include "engine.h"
int mpc_engine_changed(void *inst, char *key, int len);
}
#include "presets.h"
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++fails; } while (0)
static const mpc_engine_t *E;
static void set(void *s, const char *k, double v) { char b[64]; snprintf(b, sizeof b, "%g", v); E->set_param(s, k, b); }
static std::string get(void *s, const char *k) { char b[512] = {0}; E->get_param(s, k, b, sizeof b); return b; }
static double getf(void *s, const char *k) { return atof(get(s, k).c_str()); }
// run a sine of amplitude a (dBFS) at f Hz for sec seconds; return output rms of the last half, in dB, relative to input rms
static double run(void *s, double f, double dbfs, double sec = 1.0) {
    const int N = (int)(44100 * sec);
    std::vector<int16_t> in(2 * 128), out(2 * 128);
    double a = pow(10, dbfs / 20) * 32767, si = 0, so = 0; long cnt = 0;
    for (int n0 = 0; n0 < N; n0 += 128) {
        for (int i = 0; i < 128; ++i) { double x = a * sin(2 * M_PI * f * (n0 + i) / 44100.0); in[2*i] = in[2*i+1] = (int16_t)lrint(x); }
        E->process(s, in.data(), out.data(), 128);
        if (n0 > N / 2) for (int i = 0; i < 128; ++i) { si += (double)in[2*i] * in[2*i]; so += (double)out[2*i] * out[2*i]; ++cnt; }
    }
    return 10 * log10((so + 1e-9) / (si + 1e-9));
}
// noise (seeded) at a peak level for sec seconds; returns the output's largest sample and whether it stayed sane
static int noisePeak(void *s, double dbfs, double sec, bool &sane) {
    std::vector<int16_t> in(256), out(256);
    uint32_t r = 12345;
    const double a = pow(10, dbfs / 20) * 32767;
    int pk = 0;
    sane = true;
    for (int n0 = 0; n0 < 44100 * sec; n0 += 128) {
        for (int i = 0; i < 256; ++i) { r = r * 1664525u + 1013904223u; in[i] = (int16_t)lrint(a * ((int32_t)(r >> 8) / 8388608.0 - 1.0)); }
        E->process(s, in.data(), out.data(), 128);
        for (int i = 0; i < 256; ++i) pk = std::max(pk, abs((int)out[i]));
    }
    char b[64] = {0};
    E->get_param(s, "m_out", b, sizeof b);
    if (!(atof(b) >= 0 && atof(b) <= 1)) sane = false;
    return pk;
}
static std::vector<std::string> drain(void *s) {
    std::vector<std::string> v; char k[64];
    while (mpc_engine_changed(s, k, sizeof k) > 0) v.push_back(k);
    return v;
}
int main() {
    E = mpc_engine();
    void *s = E->create(nullptr);
    // 1. neutral: the three bands sum back flat
    double worst = 0;
    for (double f : {40.0, 100.0, 300.0, 500.0, 800.0, 2000.0, 4000.0, 6000.0, 12000.0}) worst = std::max(worst, fabs(run(s, f, -12)));
    CHECK(worst < 0.1, "neutral settings: flat within %.3f dB from 40 hz to 12k", worst);
    // 2. display text
    CHECK(get(s, "x_freq_display") == "500 hz", "band 1 Split (Up To) shows \"%s\"", get(s, "x_freq_display").c_str());
    set(s, "x_freq", 1000.0 * log(1000.0 / 20) / log(1000.0));
    CHECK(get(s, "x_freq_display") == "1k", "1000 hz shows \"%s\"", get(s, "x_freq_display").c_str());
    set(s, "x_freq", 1000.0 * log(1500.0 / 20) / log(1000.0));
    CHECK(get(s, "x_freq_display") == "1.5k", "1500 hz shows \"%s\"", get(s, "x_freq_display").c_str());
    set(s, "x_freq", 1000.0 * log(500.0 / 20) / log(1000.0));
    CHECK(get(s, "c_attack_display") == "3 ms" && get(s, "c_release_display") == "100 ms" && get(s, "c_ratio_display") == "1:1"
          && get(s, "c_input_display") == "0 db", "defaults show %s / %s / %s / %s", get(s, "c_attack_display").c_str(),
          get(s, "c_release_display").c_str(), get(s, "c_ratio_display").c_str(), get(s, "c_input_display").c_str());
    // 3. downward comp on band 1 only
    set(s, "c_thresh", -30); set(s, "c_ratio", 1000.0 * sqrt(3.0 / 19)); // 4:1
    CHECK(get(s, "c_ratio_display") == "4:1", "ratio shows \"%s\"", get(s, "c_ratio_display").c_str());
    double lo = run(s, 100, -6), hi = run(s, 8000, -6);
    CHECK(lo < -15 && lo > -22 && fabs(hi) < 0.3, "band 1 comp (-30 db, 4:1): 100 hz %.1f db (want ~-18), 8k %.2f db", lo, hi);
    auto ch = drain(s);
    // 4. band switch: proxies follow, the knobs get reported
    set(s, "sel3", 1); ch = drain(s);
    CHECK(getf(s, "band") == 2 && getf(s, "c_thresh") == 0 && ch.size() == 11, "sel3 -> band 3, c_thresh %.1f, %zu keys reported", getf(s, "c_thresh"), ch.size());
    CHECK(get(s, "x_freq_display") == "4k" && get(s, "range_text") == "4k - 20k", "band 3: Split (From) %s, range %s",
          get(s, "x_freq_display").c_str(), get(s, "range_text").c_str());
    set(s, "x_freq", 1000.0 * log(200.0 / 20) / log(1000.0)); ch = drain(s);
    CHECK(get(s, "x_freq_display") == "625 hz" && ch.size() == 1 && ch[0] == "x_freq", "band 3 Split can't go below Band 1's (500 hz x 1.25): %s, snapped back",
          get(s, "x_freq_display").c_str());
    set(s, "x_freq", 1000.0 * log(4000.0 / 20) / log(1000.0)); drain(s);
    // 5. upward comp on band 3
    set(s, "u_thresh", -20); set(s, "u_ratio", 1000.0 * sqrt(3.0 / 9)); // 4:1
    double q = run(s, 8000, -40, 1.5), qlo = run(s, 100, -40, 1.5);
    CHECK(q > 12 && q < 18 && fabs(qlo) < 0.3, "band 3 upward (-20 db, 4:1) on a -40 db tone: 8k %+.1f db (want ~+15), 100 hz (band 1, under its thresh) %+.2f db", q, qlo);
    double sil = run(s, 8000, -100, 1.0);
    CHECK(sil < 3, "no boost of near-silence (-100 db tone: %+.1f db)", sil);
    // 6. thresh rule: Upward Thresh can't go above Comp Thresh
    set(s, "u_thresh", 0); ch = drain(s);   // comp thresh is 0 on band 3
    set(s, "c_thresh", -30); ch = drain(s);
    CHECK(getf(s, "u_thresh") == -30 && ch.size() == 1 && ch[0] == "u_thresh", "lowering Comp Thresh pushes Upward Thresh down (%.1f)", getf(s, "u_thresh"));
    set(s, "u_thresh", -10); ch = drain(s);
    CHECK(getf(s, "u_thresh") == -30 && ch.size() == 1, "raising Upward Thresh past Comp Thresh snaps back (%.1f)", getf(s, "u_thresh"));
    // 7. crossover rule on band 2
    set(s, "band", 1); drain(s);
    CHECK(get(s, "range_text") == "500 hz - 4k" && get(s, "x_freq_display") == "500 hz - 4k", "band 2: range %s (no Split of its own)",
          get(s, "range_text").c_str());
    double before = getf(s, "x_freq");
    set(s, "x_freq", 900); ch = drain(s);
    CHECK(getf(s, "x_freq") == before && get(s, "range_text") == "500 hz - 4k" && ch.size() == 1, "band 2 Split is locked: a turn snaps back");
    // 8. meter: band 1 compressing -> green (out) below the grey line (in); lines at the thresholds
    set(s, "band", 0); drain(s); run(s, 100, -6, 0.5);
    double mo = getf(s, "m_out"), mi = getf(s, "m_in"), ct = getf(s, "m_ct");
    CHECK(mi > mo && fabs(ct - 14.0 / 29) < 1e-3, "meter band 1: in %.2f > out %.2f, comp line %.3f (-30 db)", mi, mo, ct);
    set(s, "band", 2); drain(s); run(s, 8000, -40, 0.5);
    CHECK(getf(s, "m_out") > getf(s, "m_in"), "meter band 3 (upward): out %.2f > in %.2f", getf(s, "m_out"), getf(s, "m_in"));
    // 9. bypass
    set(s, "bypass", 1); double byp = run(s, 100, -6, 0.5);
    CHECK(fabs(byp) < 0.05, "Pass on (bypassed): 100 hz %.3f db", byp);
    set(s, "bypass", 0);
    // 10. state round trip
    std::string st = get(s, "state");
    void *t = E->create(nullptr);
    E->set_param(t, "state", st.c_str());
    CHECK(get(t, "state") == st, "state round trip (%zu bytes)", st.size());
    set(t, "band", 0);
    CHECK(fabs(getf(t, "c_thresh") + 30) < 1e-3 && get(t, "c_ratio_display") == "4:1", "restored band 1: thresh %.1f, ratio %s", getf(t, "c_thresh"), get(t, "c_ratio_display").c_str());
    // 11. factory presets
    {
        void *p = E->create(nullptr);
        set(p, "band", 1); set(p, "bypass", 0); drain(p);
        bool allOk = true, allSane = true, allQuiet = true;
        int worstPk = 0;
        for (int i = 0; i < kNumPresets; ++i) {
            set(p, "preset", i);
            auto ch = drain(p);
            const PresetBand &mb = kPresets[i].band[1];
            // 10 knobs + the selector's name and highlight; Init is already current on a fresh insert: re-picking it
            // through the host (PRESET menu) changes nothing, so a project's edits survive JUCE's re-select at load
            const size_t want = i == 0 ? 0 : 12;
            const bool ok = getf(p, "preset") == i && getf(p, "band") == 1 && ch.size() == want &&
                            fabs(getf(p, "c_thresh") - mb.thresh) < 1e-3 && fabs(getf(p, "u_thresh") - std::min(mb.upThresh, mb.thresh)) < 1e-3;
            if (!ok) { allOk = false; printf("     preset %d (%s): thresh %s, %zu keys reported\n", i, kPresets[i].name, get(p, "c_thresh").c_str(), ch.size()); }
            bool sane;
            const int pk = noisePeak(p, -1, 0.5, sane);
            worstPk = std::max(worstPk, pk);
            if (!sane) allSane = false;
            if (pk > 32400) allQuiet = false;
        }
        CHECK(allOk, "all %d presets load: Band 2's knobs show the preset's values, the band on screen stays, 10 knobs + the name reported", kNumPresets);
        CHECK(allSane && allQuiet, "noise at -1 dBFS through every preset: output never over the -0.1 dBFS ceiling (worst %d of 32767)", worstPk);
        set(p, "preset", 1); drain(p);
        CHECK(get(p, "x_freq_display") == "150 hz - 6k" && get(p, "c_ratio_display") == "4:1", "Vox: Pop Lead on Band 2: range %s, ratio %s",
              get(p, "x_freq_display").c_str(), get(p, "c_ratio_display").c_str());
        set(p, "band", 0); drain(p);
        CHECK(get(p, "x_freq_display") == "150 hz" && get(p, "c_thresh_display") == "-25.5 db", "... Band 1: Split %s, Thresh %s",
              get(p, "x_freq_display").c_str(), get(p, "c_thresh_display").c_str());
        set(p, "bypass", 1); set(p, "preset", 13); drain(p);
        CHECK(getf(p, "bypass") == 1 && getf(p, "band") == 0, "picking a preset leaves Pass and the band on screen alone");
        set(p, "bypass", 0);
        // a tweak after a pick is kept through save/reload, and the menu still names the preset
        set(p, "c_thresh", -12); drain(p);
        std::string st2 = get(p, "state");
        void *q = E->create(nullptr);
        E->set_param(q, "state", st2.c_str());
        CHECK(getf(q, "preset") == 13 && getf(q, "c_thresh") == -12 && get(q, "state") == st2,
              "reload after a tweak: preset %g (NY Smash) and the tweak (%s) both come back", getf(q, "preset"), get(q, "c_thresh_display").c_str());
        // a 1.1 project (no preset= in its state) loads as before
        std::string old = st2;
        size_t at = old.find(";preset=13");
        old.erase(at, 10);
        E->set_param(q, "state", old.c_str());
        CHECK(getf(q, "preset") == 0 && getf(q, "c_thresh") == -12, "a 1.1 state (no preset) loads: values kept, menu on Init");
        // garbage never reaches the DSP
        std::string before = get(q, "state");
        const char *bad[] = {"nan", "inf", "-inf", "", "abc", "1e40", "-1e40"};
        const char *keys[] = {"c_input", "c_output", "c_thresh", "c_attack", "c_release", "c_knee", "c_ratio", "x_freq", "u_thresh",
                              "u_ratio", "band", "bypass", "preset", "sel1"};
        for (const char *k : keys) for (const char *v : bad) E->set_param(q, k, v);
        drain(q);
        bool sane;
        const int pk = noisePeak(q, -6, 0.5, sane);
        CHECK(sane && pk > 1000 && get(q, "state").find("nan") == std::string::npos && get(q, "state").find("inf") == std::string::npos,
              "nan / inf / garbage / huge text on every key: still sane (peak %d), state clean", pk);
        E->set_param(q, "state", "PMB1;band=99999999999;bypass=-5;preset=1e30;x1=nan;x2=inf;b0=nan,1,2,3,4,5,6,7,8,9");
        CHECK(getf(q, "band") == 2 && getf(q, "preset") == kNumPresets - 1 && get(q, "state").find("nan") == std::string::npos,
              "a broken state is clamped (band %g, preset %g), no nan gets in", getf(q, "band"), getf(q, "preset"));
        (void)before;
        // the ceiling: +24 db on every band output, full-scale noise
        set(q, "preset", 0); drain(q);
        for (int b = 0; b < 3; ++b) { set(q, "band", b); set(q, "c_output", 24); }
        const int hot = noisePeak(q, 0, 0.5, sane);
        CHECK(hot <= 32400, "Init with +24 db outputs on full-scale noise: peak %d (ceiling 32392 = -0.1 dBFS)", hot);
        E->destroy(q);
        E->destroy(p);
    }
    // 11b. live mic: the upward stage must not lift room noise / monitor bleed (it fed back on the device, 2026-11-05).
    // Open loop: hiss at about -70 dBFS rms gets no more than the band makeup. Closed loop: the output comes back into
    // the input 10 ms later at a loop gain of 0.5 / makeup (stable for any plain gain); the level must stay put.
    {
        int worstPreset = -1; double worstOpen = -99, worstLoop = -99;
        for (int i = 0; i < kNumPresets; ++i) {
            bool up = false; double mk = 0;
            for (int b = 0; b < 3; ++b) { up |= kPresets[i].band[b].upRatio > 1.0f; mk = fmax(mk, kPresets[i].band[b].input + kPresets[i].band[b].output); }
            if (!up) continue;
            void *m = E->create(nullptr);
            set(m, "preset", i); drain(m);
            const double amp = pow(10, -66 / 20.0) * 32767, A = 0.5 / pow(10, mk / 20);
            std::vector<int16_t> in(256), out(256);
            std::vector<double> ring(8192, 0.0);
            uint32_t r = 777;
            double si = 0, so = 0, sl = 0, sl0 = 0;
            for (int loop = 0; loop < 2; ++loop) {
                long w = 0;
                std::fill(ring.begin(), ring.end(), 0.0);
                for (int n0 = 0; n0 < 44100 * 4; n0 += 128) {
                    for (int k = 0; k < 128; ++k) {
                        r = r * 1664525u + 1013904223u;
                        double x = amp * ((int32_t)(r >> 8) / 8388608.0 - 1.0);
                        if (loop) x += A * ring[(w + k + 8192 - 441) & 8191];
                        x = fmax(-32768, fmin(32767, x));
                        in[2*k] = in[2*k+1] = (int16_t)lrint(x);
                        if (!loop && n0 > 44100 * 2) si += x * x;
                    }
                    E->process(m, in.data(), out.data(), 128);
                    for (int k = 0; k < 128; ++k) {
                        ring[(w + k) & 8191] = out[2*k];
                        if (n0 > 44100 * 2) { if (loop) sl += (double)out[2*k] * out[2*k]; else so += (double)out[2*k] * out[2*k]; }
                    }
                    w += 128;
                }
                if (!loop) sl0 = so;
            }
            const double open = 10 * log10((so + 1e-9) / (si + 1e-9)) - mk, grow = 10 * log10((sl + 1e-9) / (sl0 + 1e-9));
            if (open > worstOpen) { worstOpen = open; worstPreset = i; }
            worstLoop = fmax(worstLoop, grow);
            if (open > 1.0 || grow > 6.0)
                printf("     %s: hiss gets %+.1f dB over its makeup, the loop grows %+.1f dB\n", kPresets[i].name, open, grow);
            E->destroy(m);
        }
        CHECK(worstOpen <= 1.0, "room noise at -70 dBFS: no upward boost on top of the makeup (worst %+.1f dB, %s)", worstOpen,
              worstPreset >= 0 ? kPresets[worstPreset].name : "-");
        CHECK(worstLoop <= 6.0, "mic + monitor loop at half the makeup's gain stays stable on every upward preset (worst %+.1f dB)", worstLoop);
    }
    // 12. CPU
    std::vector<int16_t> in(256, 1000), out(256);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 44100 * 10 / 128; ++i) E->process(s, in.data(), out.data(), 128);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("info 10 s of audio in %.3f s on this x86 host (%.2f%% of real time)\n", secs, secs * 10);
    E->destroy(t); E->destroy(s);
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
