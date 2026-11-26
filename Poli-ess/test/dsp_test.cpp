// Poli-Ess DSP tests: drives the engine through its mpc_engine() contract exactly as the wrapper does
// (44.1 kHz, int16 interleaved stereo, 128-frame blocks). Build + run: test/run_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <chrono>
#include <string>
#include <cstdarg>
#include <algorithm>
extern "C" {
#include "engine.h"
}

static const double FS = 44100.0;
static const int B = 128;
static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char *name, const char *fmt = "", ...) {
    char buf[256] = "";
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    printf("%s  %-58s %s\n", ok ? "ok  " : "FAIL", name, buf);
    (ok ? g_pass : g_fail)++;
}

struct Inst {
    const mpc_engine_t *e; void *s;
    Inst() { e = mpc_engine(); s = e->create(nullptr); }
    ~Inst() { e->destroy(s); }
    void set(const char *k, double v) { char b[64]; snprintf(b, sizeof b, "%.6g", v); e->set_param(s, k, b); }
    void sets(const char *k, const char *v) { e->set_param(s, k, v); }
    std::string get(const char *k) { char b[128] = ""; int n = e->get_param(s, k, b, sizeof b); return n > 0 ? b : ""; }
    double getf(const char *k) { return atof(get(k).c_str()); }
    // process a mono-or-stereo float signal (+-1), returns output as float L/R
    void run(const std::vector<float> &L, const std::vector<float> &R, std::vector<float> &oL, std::vector<float> &oR) {
        size_t n = L.size(); oL.assign(n, 0); oR.assign(n, 0);
        int16_t in[2 * B], out[2 * B];
        for (size_t p = 0; p < n; p += B) {
            int m = (int)std::min<size_t>(B, n - p);
            for (int i = 0; i < m; i++) {
                in[2 * i] = (int16_t)lrint(std::max(-1.0, std::min(32767.0 / 32768.0, (double)L[p + i])) * 32768.0);
                in[2 * i + 1] = (int16_t)lrint(std::max(-1.0, std::min(32767.0 / 32768.0, (double)R[p + i])) * 32768.0);
            }
            for (int i = m; i < B; i++) in[2 * i] = in[2 * i + 1] = 0;
            e->process(s, in, out, B);
            for (int i = 0; i < m; i++) { oL[p + i] = out[2 * i] / 32768.0f; oR[p + i] = out[2 * i + 1] / 32768.0f; }
        }
    }
    void run(const std::vector<float> &x, std::vector<float> &o) { std::vector<float> oR; run(x, x, o, oR); }
};

static std::vector<float> sine(double f, double dbfs, double sec) {
    std::vector<float> v((size_t)(sec * FS)); double a = pow(10, dbfs / 20);
    for (size_t i = 0; i < v.size(); i++) v[i] = (float)(a * sin(2 * M_PI * f * i / FS));
    return v;
}
static uint32_t rng = 12345;
static float white() { rng = rng * 1664525u + 1013904223u; return (float)((rng >> 8) / 8388608.0 - 1.0); }
static std::vector<float> noise(double dbfs_rms, double sec) {
    std::vector<float> v((size_t)(sec * FS)); double a = pow(10, dbfs_rms / 20) * sqrt(3.0);
    for (auto &x : v) x = (float)(a * white());
    return v;
}
static double rms_db(const std::vector<float> &v, size_t from, size_t to) {
    double s = 0; for (size_t i = from; i < to; i++) s += (double)v[i] * v[i];
    return 10 * log10(s / (to - from) + 1e-30);
}
static double tail_db(const std::vector<float> &v) { return rms_db(v, v.size() / 2, v.size()); }   // after settling

// ---- tests -------------------------------------------------------------------------------------------------------
static void defaults_and_ranges() {
    Inst a;
    check(a.get("intensity") != "" && fabs(a.getf("intensity") - 50) < 1e-3, "default Intensity 50", "%s", a.get("intensity").c_str());
    check(fabs(a.getf("thresh") + 20) < 1e-3, "default Threshold -20 dB", "%s", a.get("thresh").c_str());
    check(fabs(a.getf("freq") - 5000) < 1e-3, "default Freq 5000 Hz", "%s", a.get("freq").c_str());
    check(a.get("diff") == "0", "default Diff Off", "%s", a.get("diff").c_str());
    check(a.getf("meter") == 0, "meter starts at 0", "%s", a.get("meter").c_str());
    // the new knobs default to 1.0.x's fixed behaviour
    check(a.get("attack_display") == "0.80 ms", "default Attack 0.80 ms (1.0.x's fixed attack)", "%s", a.get("attack_display").c_str());
    check(a.get("release_display") == "35 ms", "default Release 35 ms (1.0.x's fixed release)", "%s", a.get("release_display").c_str());
    check(a.getf("knee") == 0 && a.getf("mix") == 100, "default Knee 0 (hard), Mix 100 %", "%s / %s", a.get("knee").c_str(), a.get("mix").c_str());
    a.set("attack", 0); a.set("release", 100);
    check(a.get("attack_display") == "0.10 ms" && a.get("release_display") == "500 ms", "Attack / Release ends: 0.10 ms, 500 ms",
          "%s / %s", a.get("attack_display").c_str(), a.get("release_display").c_str());
    a.set("attack", 100); a.set("release", 0);
    check(a.get("attack_display") == "10 ms" && a.get("release_display") == "5.0 ms", "Attack / Release other ends: 10 ms, 5.0 ms",
          "%s / %s", a.get("attack_display").c_str(), a.get("release_display").c_str());
    a.set("attack", 50); a.set("release", 50);
    check(a.get("attack_display") == "1.0 ms" && a.get("release_display") == "50 ms", "log knobs: the middle is the geometric mean (1 ms, 50 ms)",
          "%s / %s", a.get("attack_display").c_str(), a.get("release_display").c_str());
    // the wrapper reads the knob position back from this text: it must keep sub-unit precision
    a.set("intensity", 37.42); a.set("thresh", -19.84); a.set("freq", 6543.21);
    check(fabs(a.getf("intensity") - 37.42) < 0.01, "Intensity read-back keeps precision", "37.42 -> %s", a.get("intensity").c_str());
    check(fabs(a.getf("thresh") + 19.84) < 0.01, "Threshold read-back keeps precision", "-19.84 -> %s", a.get("thresh").c_str());
    check(fabs(a.getf("freq") - 6543.21) < 0.1, "Freq read-back keeps precision", "6543.21 -> %s", a.get("freq").c_str());
    a.set("attack", 12.345); a.set("release", 67.891); a.set("knee", 3.217); a.set("mix", 71.13);
    check(fabs(a.getf("attack") - 12.345) < 0.001 && fabs(a.getf("release") - 67.891) < 0.001 && fabs(a.getf("knee") - 3.217) < 0.001 && fabs(a.getf("mix") - 71.13) < 0.001,
          "Attack/Release/Knee/Mix read-back keeps precision", "%s %s %s %s", a.get("attack").c_str(), a.get("release").c_str(), a.get("knee").c_str(), a.get("mix").c_str());
    // garbage / out of range from the host must be clamped, never stored as NaN
    const char *bad[] = {"nan", "inf", "-inf", "1e30", "-1e30", "", "abc"};
    bool finite = true; std::string worst;
    for (const char *b : bad) for (const char *k : {"intensity", "thresh", "freq", "attack", "release", "knee", "mix"}) {
        a.sets(k, b); double v = a.getf(k);
        if (!std::isfinite(v) || a.get(k).find("nan") != std::string::npos || a.get(k).find("inf") != std::string::npos) { finite = false; worst = std::string(k) + "=" + b + " -> " + a.get(k); }
    }
    check(finite, "garbage values are rejected or clamped", "%s", worst.c_str());
    a.set("intensity", 150); a.set("thresh", -99); a.set("freq", 50);
    check(a.getf("intensity") <= 100 && a.getf("thresh") >= -40 && a.getf("freq") >= 2000, "out-of-range values clamp to the param range",
          "%s / %s / %s", a.get("intensity").c_str(), a.get("thresh").c_str(), a.get("freq").c_str());
    a.set("attack", 150); a.set("release", -5); a.set("knee", 50); a.set("mix", 150);
    check(a.getf("attack") == 100 && a.getf("release") == 0 && a.getf("knee") == 12 && a.getf("mix") == 100, "new knobs clamp to their ranges",
          "%s / %s / %s / %s", a.get("attack").c_str(), a.get("release").c_str(), a.get("knee").c_str(), a.get("mix").c_str());
    std::vector<float> o; a.run(noise(-6, 0.5), o);
    bool ok = true; for (float x : o) ok &= std::isfinite(x);
    check(ok, "audio stays finite after garbage params");
}

static void flat_when_idle() {
    // threshold at 0 dB and quiet input: nothing compresses, so LP + HP (LR4) must sum flat
    double worst = 0, wf = 0;
    for (double f : {50.0, 200.0, 1000.0, 3000.0, 4500.0, 5000.0, 5500.0, 8000.0, 12000.0, 18000.0}) {
        Inst a; a.set("thresh", 0); a.set("intensity", 100);
        auto x = sine(f, -30, 0.5); std::vector<float> o; a.run(x, o);
        double d = tail_db(o) - tail_db(x);
        if (fabs(d) > fabs(worst)) { worst = d; wf = f; }
    }
    check(fabs(worst) < 0.05, "flat response when nothing is reduced (LR4 sum)", "worst %+.3f dB at %.0f Hz", worst, wf);
}

static double reduction_db(double f, double level, std::initializer_list<std::pair<const char *, double>> ps, bool diff = false) {
    Inst a; for (auto &p : ps) a.set(p.first, p.second); if (diff) a.set("diff", 1);
    auto x = sine(f, level, 1.0); std::vector<float> o; a.run(x, o);
    return tail_db(o) - tail_db(x);
}

static void reduces_sibilance_only() {
    double hi = reduction_db(7000, -6, {});
    double lo = reduction_db(200, -6, {});
    double mid = reduction_db(1000, -6, {});
    check(hi < -6, "7 kHz at -6 dBFS is reduced (defaults)", "%+.2f dB", hi);
    check(fabs(lo) < 0.05, "200 Hz at -6 dBFS untouched", "%+.3f dB", lo);
    check(fabs(mid) < 0.3, "1 kHz at -6 dBFS (well below Freq) ~untouched", "%+.3f dB", mid);
    double i0 = reduction_db(7000, -6, {{"intensity", 0}});
    check(fabs(i0) < 0.05, "Intensity 0 = no reduction", "%+.3f dB", i0);
    double prev = 1; bool mono = true; char s[160] = ""; size_t at = 0;
    for (double t : {0.0, -10.0, -20.0, -30.0, -40.0}) {
        double r = reduction_db(7000, -6, {{"thresh", t}});
        mono &= r <= prev + 0.01; prev = r; at += snprintf(s + at, sizeof s - at, "%g:%+.1f ", t, r);
    }
    check(mono, "lower Threshold = more reduction", "%s", s);
    prev = 1; mono = true; at = 0; s[0] = 0;
    for (double in : {0.0, 25.0, 50.0, 75.0, 100.0}) {
        double r = reduction_db(7000, -6, {{"intensity", in}});
        mono &= r <= prev + 0.01; prev = r; at += snprintf(s + at, sizeof s - at, "%g:%+.1f ", in, r);
    }
    check(mono, "more Intensity = more reduction", "%s", s);
    // 1.0.5: the knob works over its whole travel (1.0.4 did 90 % of its work in the first quarter), and goes further
    double r25 = reduction_db(7000, -6, {{"intensity", 25}}), r50 = reduction_db(7000, -6, {{"intensity", 50}});
    double r75 = reduction_db(7000, -6, {{"intensity", 75}}), r100 = reduction_db(7000, -6, {{"intensity", 100}});
    check(r25 - r50 > 1.5 && r50 - r75 > 1.0 && r75 - r100 > 0.3, "Intensity keeps adding reduction across its travel",
          "25:%+.1f 50:%+.1f 75:%+.1f 100:%+.1f", r25, r50, r75, r100);
    check(fabs(r50 + 7.6) < 0.6, "Intensity 50 % sounds like 1.0.4's 50 % (-7.4 dB)", "%+.2f dB", r50);
    check(r100 < -7.77 - 2.0, "Intensity 100 % de-esses >= 2 dB more than 1.0.4's max", "%+.2f dB", r100);
    double f2 = reduction_db(3000, -6, {{"freq", 2000}}), f10 = reduction_db(3000, -6, {{"freq", 10000}});
    check(f2 < -3 && fabs(f10) < 0.5, "Freq picks the band (3 kHz: Freq 2k vs 10k)", "%+.2f vs %+.2f dB", f2, f10);
}

static void diff_mode() {
    // Diff = only what is removed: silent on low tones, and normal + diff = the unprocessed (allpassed) signal
    double lo = reduction_db(200, -6, {}, true);
    check(lo < -60, "Diff: 200 Hz gives (near) silence", "%+.1f dB", lo);
    Inst n, d, ref; d.set("diff", 1); ref.set("thresh", 0);  // ref: nothing compresses at -6 dBFS? use intensity 0
    ref.set("intensity", 0);
    auto x = sine(7000, -6, 1.0); std::vector<float> on, od, orf;
    n.run(x, on); d.run(x, od); ref.run(x, orf);
    double err = 0, sig = 0;
    for (size_t i = x.size() / 2; i < x.size(); i++) { double e = on[i] + od[i] - orf[i]; err += e * e; sig += (double)orf[i] * orf[i]; }
    double snr = 10 * log10(sig / (err + 1e-30));
    check(snr > 60, "Diff: normal + diff == unprocessed", "match %.1f dB", snr);
}

// level (dB, RMS) of a window of the output
static double win_db(const std::vector<float> &o, double t0, double t1) { return rms_db(o, (size_t)(t0 * FS), (size_t)(t1 * FS)); }

static void attack_release_knee_mix() {
    // Attack: how much of a sibilant's first millisecond gets through. 7 kHz burst after silence.
    auto burst = [](double sec_on) { auto x = sine(7350, -6, 0.3); for (size_t i = 0; i < x.size(); i++) if (i < 4410 || i >= 4410 + (size_t)(sec_on * FS)) x[i] = 0; return x; };
    double first_fast, first_slow;
    { Inst a; a.set("attack", 0); std::vector<float> o; a.run(burst(0.15), o); first_fast = win_db(o, 0.1, 0.101); }
    { Inst a; a.set("attack", 100); std::vector<float> o; a.run(burst(0.15), o); first_slow = win_db(o, 0.1, 0.101); }
    check(first_slow - first_fast > 3, "Attack: a slow attack lets the start of an S through", "first ms: %.1f dB (0.1 ms) vs %.1f dB (10 ms)", first_fast, first_slow);
    // Release: a quiet 7 kHz tone (below threshold) right after a loud burst stays ducked longer with a long release
    auto probe = []() { auto x = sine(7350, -6, 0.6); for (size_t i = (size_t)(0.25 * FS); i < x.size(); i++) x[i] *= 0.05f; return x; };
    double rec_fast, rec_slow;
    { Inst a; a.set("release", 0); std::vector<float> o; a.run(probe(), o); rec_fast = win_db(o, 0.30, 0.35); }
    { Inst a; a.set("release", 100); std::vector<float> o; a.run(probe(), o); rec_slow = win_db(o, 0.30, 0.35); }
    check(rec_fast - rec_slow > 3, "Release: a long release keeps ducking after the S", "50 ms later: %.1f dB (5 ms) vs %.1f dB (500 ms)", rec_fast, rec_slow);
    // Knee: a tone just under the threshold is untouched with a hard knee and gently reduced with a soft one;
    // far above it, both reduce the same
    double at_hard, at_soft, far_hard, far_soft;
    // the high band's envelope of a -26 dBFS sine peaks at -26 dB: 2 dB under a -24 dB threshold (inside a 12 dB knee)
    at_hard = reduction_db(7000, -26, {{"thresh", -24}, {"knee", 0}});
    at_soft = reduction_db(7000, -26, {{"thresh", -24}, {"knee", 12}});
    far_hard = reduction_db(7000, -3, {{"thresh", -35}, {"knee", 0}});
    far_soft = reduction_db(7000, -3, {{"thresh", -35}, {"knee", 12}});
    check(fabs(at_hard) < 0.02 && at_soft < -0.05, "Knee: soft knee starts reducing below the threshold", "hard %+.2f dB, soft %+.2f dB", at_hard, at_soft);
    check(fabs(far_hard - far_soft) < 0.3, "Knee: far above the threshold both reduce the same", "hard %+.2f dB, soft %+.2f dB", far_hard, far_soft);
    // Mix: 0 = nothing removed, 50 = in between, 100 = full; low frequencies never touched
    double m0 = reduction_db(7000, -6, {{"mix", 0}}), m50 = reduction_db(7000, -6, {{"mix", 50}}), m100 = reduction_db(7000, -6, {{"mix", 100}});
    check(fabs(m0) < 0.05 && m50 < -1 && m50 > m100 + 1, "Mix: 0 % = no reduction, 50 % in between, 100 % full", "%+.2f / %+.2f / %+.2f dB", m0, m50, m100);
    // Mix must stay phase-coherent: a 1 kHz + 7 kHz mix at Mix 50 keeps the 1 kHz part exactly (no comb filtering)
    {
        auto x = sine(1000, -12, 1.0), y = sine(7000, -6, 1.0); for (size_t i = 0; i < x.size(); i++) x[i] += y[i];
        Inst a; a.set("mix", 50); a.set("freq", 4000); std::vector<float> o; a.run(x, o);
        Inst r; r.set("intensity", 0); r.set("freq", 4000); std::vector<float> ro; r.run(x, ro);   // nothing removed, same crossover
        // compare after a 2-pole low-pass at ~1.5 kHz (removes most of the 7 kHz)
        auto lp = [](const std::vector<float> &v) { std::vector<float> r(v.size()); double a1 = 0, a2 = 0, k = 1 - exp(-2 * M_PI * 1500 / FS);
                                                   for (size_t i = 0; i < v.size(); i++) { a1 += k * (v[i] - a1); a2 += k * (a1 - a2); r[i] = (float)a2; } return r; };
        auto lo = lp(o), lr = lp(ro); double err = 0, sig = 0;
        for (size_t i = x.size() / 2; i < x.size(); i++) { err += (lo[i] - lr[i]) * (lo[i] - lr[i]); sig += lr[i] * lr[i]; }
        check(10 * log10(sig / (err + 1e-30)) > 15, "Mix 50 %: the low part is untouched (phase-coherent mix)", "%.1f dB", 10 * log10(sig / (err + 1e-30)));
    }
    // Diff stays "what is removed" with any Mix: normal + diff == unprocessed at Mix 50
    {
        Inst n, d, ref; n.set("mix", 50); d.set("mix", 50); d.set("diff", 1); ref.set("intensity", 0);
        auto x = sine(7000, -6, 1.0); std::vector<float> on, od, orf; n.run(x, on); d.run(x, od); ref.run(x, orf);
        double err = 0, sig = 0;
        for (size_t i = x.size() / 2; i < x.size(); i++) { double e = on[i] + od[i] - orf[i]; err += e * e; sig += (double)orf[i] * orf[i]; }
        check(10 * log10(sig / (err + 1e-30)) > 60, "Mix 50 %: normal + Diff == unprocessed", "match %.1f dB", 10 * log10(sig / (err + 1e-30)));
    }
    // meter shows what is actually removed: half the Mix, less on the meter
    { Inst a, b; a.set("mix", 100); b.set("mix", 30); std::vector<float> o; a.run(sine(7000, -6, 1.0), o); b.run(sine(7000, -6, 1.0), o);
      check(b.getf("meter") < a.getf("meter") && b.getf("meter") > 0, "meter follows Mix", "Mix 100: %.3f, Mix 30: %.3f", a.getf("meter"), b.getf("meter")); }
}

static void silence_and_tails() {
    Inst a;
    std::vector<float> z(44100, 0.f), o; a.run(z, o);
    bool zero = true; for (float v : o) zero &= v == 0.f;
    check(zero, "digital silence in -> exact silence out");
    auto x = noise(-3, 0.5); x.resize(x.size() + 3 * 44100, 0.f); a.run(x, o);
    float last = 0; for (size_t i = o.size() - 44100; i < o.size(); i++) last = std::max(last, fabsf(o[i]));
    check(last == 0.f, "tail after loud noise decays to exact silence", "peak in last second %.2g", last);
    check(a.getf("meter") == 0, "meter back to 0 after 3 s of silence", "%s", a.get("meter").c_str());
}

static void full_scale_no_wrap() {
    Inst a; a.set("thresh", 0); a.set("intensity", 0);
    std::vector<float> x(44100), o;
    for (size_t i = 0; i < x.size(); i++) x[i] = (i / 20) % 2 ? 1.0f : -1.0f;   // full-scale square: overshoot through the crossover
    a.run(x, o);
    // a wrapped int16 shows as a jump of nearly 2.0 between neighbours where the input did not jump
    int wraps = 0; for (size_t i = 1; i < o.size(); i++) if (fabsf(o[i] - o[i - 1]) > 1.5f && x[i] == x[i - 1]) wraps++;
    float pk = 0; for (float v : o) pk = std::max(pk, fabsf(v));
    check(wraps == 0, "full-scale square: clipped, never wrapped", "%d wraps, peak %.3f", wraps, pk);
}

// largest sample-to-sample jump in (out - smooth reference) = audible click measure
// Click detector: the 3rd difference of a smooth signal is tiny (a sine scales by w^3); a step or kink in the waveform
// makes it spike. Returns the largest |d3| in [from, to).
static double d3max(const std::vector<float> &o, size_t from, size_t to) {
    double m = 0;
    for (size_t i = std::max<size_t>(from, 3); i < to && i < o.size(); i++)
        m = std::max(m, fabs((double)o[i] - 3.0 * o[i - 1] + 3.0 * o[i - 2] - o[i - 3]));
    return m;
}
// run x through one instance, calling change(inst, chunk_index) every `chunk` samples
template <class F> static std::vector<float> run_with(std::vector<float> x, size_t chunk, F change, Inst &a) {
    std::vector<float> o(x.size()), t;
    for (size_t p = 0, k = 0; p < x.size(); p += chunk, k++) {
        std::vector<float> seg(x.begin() + p, x.begin() + std::min(x.size(), p + chunk));
        a.run(seg, t); std::copy(t.begin(), t.end(), o.begin() + p);
        change(a, k);
    }
    return o;
}

static void zipper_and_clicks() {
    // 1) Freq moved in Q-Link steps (1/128 of the range) every 23 ms while a 1 kHz tone plays (nothing compresses).
    {
        auto x = sine(1000, -12, 2.0);
        Inst r; r.set("thresh", 0); std::vector<float> ref; r.run(x, ref);
        Inst a; a.set("thresh", 0); double f = 2000; int dir = 1;
        auto o = run_with(x, 1024, [&](Inst &in, size_t) {
            f += dir * 8000.0 * 4 / 128; if (f > 10000 || f < 2000) { dir = -dir; f += 2 * dir * 8000.0 * 4 / 128; }
            in.set("freq", f); }, a);
        double c = d3max(o, 4410, o.size()), base = d3max(ref, 4410, ref.size());
        check(c < base * 2, "Freq moved during a tone: no zipper clicks", "click %.5f vs clean %.5f", c, base);
    }
    // 2) Threshold / Intensity moved in Q-Link steps while sibilance is reduced. 7350 Hz = exactly 6 samples per cycle,
    //    so the peak of each 6-sample cycle is the true level; a step in the gain shows as a jump between cycles.
    struct { const char *k; double v, lo, hi; } knobs[] = {{"thresh", -20, -40, 0}, {"intensity", 50, 0, 100},
        {"attack", 45, 0, 100}, {"release", 42, 0, 100}, {"knee", 0, 0, 12}, {"mix", 100, 0, 100}};
    for (auto &kn : knobs) {
        const char *k = kn.k;
        auto y = sine(7350, -6, 2.0);
        Inst c; c.set("knee", 6);   // a soft knee, so a Knee move changes the gain too
        double v = kn.v, lo = kn.lo, hi = kn.hi, stepv = (hi - lo) * 4 / 128; int d = 1;
        auto oy = run_with(y, 1024, [&](Inst &in, size_t) { v += d * stepv; if (v > hi || v < lo) { d = -d; v += 2 * d * stepv; } in.set(k, v); }, c);
        std::vector<double> pk;
        for (size_t i = 8820; i + 6 <= oy.size(); i += 6) { double m = 0; for (int j = 0; j < 6; j++) m = std::max(m, (double)fabsf(oy[i + j])); pk.push_back(m); }
        double worst = 0; for (size_t i = 1; i < pk.size(); i++) if (pk[i - 1] > 1e-3) worst = std::max(worst, fabs(pk[i] - pk[i - 1]) / pk[i - 1]);
        char name[80]; snprintf(name, sizeof name, "%s moved during sibilance: gain glides", k);
        check(20 * log10(worst + 1e-12) < -40, name, "largest level change between cycles %.1f dB", 20 * log10(worst + 1e-12));
    }
    // 3) Diff switched while a loud 200 Hz tone plays under the sibilance: the low tone leaves the output at the switch.
    {
        std::vector<float> y = sine(200, -6, 1.0), y2 = sine(7350, -12, 1.0);
        for (size_t i = 0; i < y.size(); i++) y[i] += y2[i];
        size_t half = 172 * 128;   // MPC changes params between 128-frame blocks: switch on a block edge
        Inst n, dd; n.set("diff", 0); dd.set("diff", 1); std::vector<float> on, od; n.run(y, on); dd.run(y, od);
        Inst b; auto o = run_with(y, half, [&](Inst &in, size_t k) { if (k == 0) in.set("diff", 1); }, b);
        double c = d3max(o, half - 64, half + 2048);
        double base = std::max(d3max(on, half - 64, half + 2048), d3max(od, half - 64, half + 2048));
        check(c < base * 1.5, "Diff switched with a low tone underneath: no click", "around the switch %.4f vs steady %.4f", c, base);
    }
}

static void meter_behaviour() {
    Inst a;
    std::vector<float> o; a.run(sine(200, -3, 1.0), o);
    check(a.getf("meter") == 0, "meter stays 0 on a loud low tone", "%s", a.get("meter").c_str());
    std::string rev0 = a.get("display_rev");
    a.run(sine(7000, -6, 1.0), o);
    double m = a.getf("meter");
    check(m > 0.2 && m <= 1, "meter shows reduction on sibilance", "%s", a.get("meter").c_str());
    check(a.get("display_rev") != rev0, "display_rev changes when the meter moves");
    // steady tone: the meter step must not flicker (display_rev constant once settled)
    a.run(sine(7000, -6, 0.5), o); std::string r1 = a.get("display_rev");
    a.run(sine(7000, -6, 0.5), o); std::string r2 = a.get("display_rev");
    check(r1 == r2, "steady sibilance: meter does not flicker", "rev %s -> %s", r1.c_str(), r2.c_str());
    // the reported value lands exactly on one of the 32 frames
    double steps = m * 31; check(fabs(steps - lround(steps)) < 0.01, "meter value sits on a strip frame (32 steps)", "%.4f*31=%.3f", m, steps);
    a.set("meter", 0.9); check(a.getf("meter") == m, "host writes to the meter are ignored");
    // 24 dB of reduction = full scale
    Inst f; f.set("thresh", -40); f.set("intensity", 100); f.run(sine(7000, 0, 1.0), o);
    check(f.getf("meter") == 1, "deep reduction fills the meter", "%s", f.get("meter").c_str());
    // fall time: from full to 0 within ~1.5 s of silence
    std::vector<float> z((size_t)(0.5 * FS), 0.f); f.run(z, o); double after05 = f.getf("meter");
    f.run(z, o); f.run(z, o); double after15 = f.getf("meter");
    check(after05 > 0 && after05 < 1 && after15 == 0, "meter falls smoothly, empty within 1.5 s", "0.5 s: %.3f, 1.5 s: %.3f", after05, after15);
}

static void diff_param_values() {
    Inst a;
    for (const char *v : {"1", "1.0", "0.6", "On"}) { a.sets("diff", "0"); a.sets("diff", v); }
    a.sets("diff", "1"); check(a.get("diff") == "1", "Diff \"1\" -> On");
    a.sets("diff", "0"); check(a.get("diff") == "0", "Diff \"0\" -> Off");
}

static void stereo_and_instances() {
    // a sibilant on the left only: linked detection reduces both sides equally; right gets no extra content
    Inst a; auto l = sine(7000, -6, 1.0); std::vector<float> r(l.size(), 0.f), oL, oR; a.run(l, r, oL, oR);
    double rR = tail_db(oR);
    check(rR < -90, "left-only sibilant: right channel stays silent", "%.1f dBFS", rR);
    Inst b, c; std::vector<float> o1, o2; b.run(sine(7000, -6, 0.5), o1); c.run(sine(200, -6, 0.5), o2);
    check(b.getf("meter") > 0 && c.getf("meter") == 0, "two instances are independent");
}

static void cpu() {
    // best of 3 for each timing, so a busy build machine can't fail the check
    auto timed = [](Inst &a, const std::vector<float> &x) {
        std::vector<float> o; auto t0 = std::chrono::steady_clock::now(); a.run(x, o);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    auto x = noise(-6, 10.0); std::vector<float> z((size_t)(10 * FS), 0.f);
    double sec = 1e9, sec2 = 1e9;
    for (int k = 0; k < 3; k++) {
        Inst a; a.set("thresh", -40); a.set("intensity", 100); a.set("knee", 6); a.set("mix", 70);
        sec = std::min(sec, timed(a, x));          // worst case: everything compressing
        sec2 = std::min(sec2, timed(a, z));        // then silence: the filter tails decay toward subnormals
    }
    printf("info  CPU, worst case (all compressing, this x86 build): %.2f%% of one core (%.1f us per 128-frame block)\n",
           100.0 * sec / 10.0, sec / (10.0 * FS / B) * 1e6);
    check(sec2 < sec * 1.5, "no denormal slowdown in silence after signal", "%.1f ms vs %.1f ms per 10 s", sec2 * 1e3, sec * 1e3);
}

int main() {
    defaults_and_ranges();
    flat_when_idle();
    reduces_sibilance_only();
    attack_release_knee_mix();
    diff_mode();
    diff_param_values();
    silence_and_tails();
    full_scale_no_wrap();
    zipper_and_clicks();
    meter_behaviour();
    stereo_and_instances();
    cpu();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
