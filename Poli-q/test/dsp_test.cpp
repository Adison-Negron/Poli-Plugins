// Poli-q engine checks (x86; run_tests.sh builds this under ASan + UBSan and again at release flags for the CPU).
// The engine alone, through mpc_engine() as the wrapper calls it, plus the DSP classes directly.
#include <cmath>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

extern "C" {
#include "engine.h"
int mpc_engine_changed(void *inst, char *key, int len);
}
#include "PoliQ.h"
#include "layout_gen.h"
#include "presets_gen.h"
#include "params.h"

using namespace pq;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) ++fails; } while (0)

static const mpc_engine_t *E;
struct Inst {
    void *p;
    Inst() { p = E->create(nullptr); }
    ~Inst() { E->destroy(p); }
    void set(const char *k, const char *v) { E->set_param(p, k, v); }
    void set(const char *k, double v) { char b[64]; snprintf(b, sizeof b, "%.6f", v); E->set_param(p, k, b); }
    double get(const char *k) { char b[8192]; return E->get_param(p, k, b, sizeof b) > 0 ? atof(b) : NAN; }
    std::string text(const char *k) { char b[8192]; return E->get_param(p, k, b, sizeof b) > 0 ? std::string(b) : std::string("<none>"); }
    void process(const int16_t *in, int16_t *out, int frames) { E->process(p, in, out, frames); }
    std::vector<std::string> changed() {
        std::vector<std::string> v;
        char k[64];
        while (mpc_engine_changed(p, k, sizeof k) > 0) v.push_back(k);
        return v;
    }
};
static bool has(const std::vector<std::string> &v, const char *k) {
    for (auto &s : v) if (s == k) return true;
    return false;
}

static uint32_t rng = 12345;
static float noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) * (1.0f / 16777216.0f)) * 2 - 1; }
static int16_t s16(double v) { v = v * 32768.0; return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : lrint(v)); }

// run n blocks of a signal through an instance; gen(i) gives the sample (mono -> both channels)
template <class G> static std::vector<float> run(Inst &e, int blocks, G gen, int64_t *t0 = nullptr) {
    static int64_t t = 0;
    int64_t &tt = t0 ? *t0 : t;
    std::vector<float> out;
    out.reserve(blocks * 128);
    int16_t in[256], o[256];
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 128; ++i) in[2 * i] = in[2 * i + 1] = s16(gen(tt++));
        e.process(in, o, 128);
        for (int i = 0; i < 128; ++i) out.push_back(o[2 * i] / 32768.0f);
    }
    return out;
}

// |FFT| in dB of a float impulse response at frequency hz (direct DFT at that frequency)
static double dftDb(const std::vector<double> &h, double hz) {
    double re = 0, im = 0;
    const double w = 2 * M_PI * hz / 44100.0;
    for (size_t n = 0; n < h.size(); ++n) { re += h[n] * cos(w * n); im -= h[n] * sin(w * n); }
    return 10 * log10(re * re + im * im + 1e-30);
}
static std::vector<double> impulse(int type, float hz, float g, float q, int len = 65536) {
    BandSettings s;
    s.type = type; s.on = true; s.hz = hz; s.gain = g; s.q = q;
    std::vector<double> h(len);
    // the band's process() is private to Eq: run it through an Eq with only band 0 on
    Eq eq;
    eq.init();
    eq.setBand(0, s);   // the first setting of a band is taken at once (no crossfade)
    for (int i = 1; i < kBands; ++i) { BandSettings off; off.on = false; eq.setBand(i, off); }
    std::vector<float> x(len, 0.0f), y(len), y2(len);
    x[0] = 1.0f;
    for (int i = 0; i < len; i += 128) eq.process(&x[i], &x[i], &y[i], &y2[i], 128);
    for (int i = 0; i < len; ++i) h[i] = y[i];
    return h;
}

static int tri(int k) { k %= 256; return k < 128 ? k : 255 - k; }

static double thirdDiffMax(const std::vector<float> &y, size_t from) {
    double m = 0;
    for (size_t i = from + 3; i < y.size(); ++i) m = fmax(m, fabs(y[i] - 3 * y[i - 1] + 3 * y[i - 2] - y[i - 3]));
    return m;
}

int main() {
    E = mpc_engine();

    // ---- 1. the filters are what the display draws ------------------------------------------------------------
    {
        const float fs[] = {40, 180, 1000, 4700, 14000};
        double worst = 0;
        int worstType = -1;
        for (int t = 0; t < kTypes; ++t)
            for (float f : fs)
                for (float g : {-12.0f, 6.0f})
                    for (float q : {0.5f, 2.0f}) {
                        auto h = impulse(t, f, g, q);
                        Design d;
                        design(t, f, g, q, d);
                        for (double hz : {25.0, 63.0, 150.0, 410.0, 1000.0, 2600.0, 6000.0, 11000.0, 18000.0}) {
                            const double m = dftDb(h, hz), a = responseDb(d, (float)hz);
                            if (a < -60) continue;   // deep stop band: float noise floor
                            const double e = fabs(m - a);
                            if (e > worst) { worst = e; worstType = t; }
                        }
                    }
        CHECK(worst < 0.05, "every type: the audio's response = the drawn curve within %.4f dB (worst: %s)", worst,
              worstType >= 0 ? kTypeNames[worstType] : "-");
    }
    {
        CHECK(fabs(bandDb(kBell, 1000, 9, 1.0f, 1000) - 9) < 0.01, "Bell +9 dB at 1 kHz reads %.3f dB there",
              bandDb(kBell, 1000, 9, 1.0f, 1000));
        CHECK(fabs(bandDb(kLowShelf, 200, 6, 0.71f, 20) - 6) < 0.3 && fabs(bandDb(kLowShelf, 200, 6, 0.71f, 200) - 3) < 0.05,
              "Low Shelf +6 @200: %.2f dB at 20 Hz, %.2f at 200", bandDb(kLowShelf, 200, 6, 0.71f, 20),
              bandDb(kLowShelf, 200, 6, 0.71f, 200));
        CHECK(fabs(bandDb(kHighShelf, 5000, -8, 0.71f, 19000) + 8) < 0.5 && fabs(bandDb(kHighShelf, 5000, -8, 0.71f, 5000) + 4) < 0.05,
              "High Shelf -8 @5k: %.2f dB at 19 kHz, %.2f at 5k", bandDb(kHighShelf, 5000, -8, 0.71f, 19000),
              bandDb(kHighShelf, 5000, -8, 0.71f, 5000));
        const struct { int t; double slope; } cuts[] = {{kLowCut6, 6}, {kLowCut12, 12}, {kLowCut24, 24}, {kLowCut48, 48}};
        for (auto c : cuts) {
            const double at = bandDb(c.t, 800, 0, 0.7071f, 800), lo = bandDb(c.t, 800, 0, 0.7071f, 100);
            CHECK(fabs(at + 3.01) < 0.05 && fabs(lo + 3 * c.slope) < 2.0, "%s @800 Hz: %.2f dB at 800, %.1f dB 3 octaves down (~%.0f)",
                  kTypeNames[c.t], at, lo, -3 * c.slope);
        }
        const struct { int t; double slope; } hcs[] = {{kHighCut6, 6}, {kHighCut12, 12}, {kHighCut24, 24}, {kHighCut48, 48}};
        for (auto c : hcs) {
            const double at = bandDb(c.t, 500, 0, 0.7071f, 500), hi = bandDb(c.t, 500, 0, 0.7071f, 4000);
            CHECK(fabs(at + 3.01) < 0.05 && fabs(hi + 3 * c.slope) < 2.5, "%s @500 Hz: %.2f dB at 500, %.1f dB 3 octaves up",
                  kTypeNames[c.t], at, hi);
        }
        CHECK(bandDb(kNotch, 1000, 0, 4, 1000) < -60 && fabs(bandDb(kNotch, 1000, 0, 4, 4000)) < 0.3, "Notch @1k: %.0f dB at 1k, %.2f at 4k",
              bandDb(kNotch, 1000, 0, 4, 1000), bandDb(kNotch, 1000, 0, 4, 4000));
        CHECK(fabs(bandDb(kBandPass, 1000, 0, 1, 1000)) < 0.01 && bandDb(kBandPass, 1000, 0, 1, 100) < -18,
              "Band Pass @1k: %.2f dB at 1k, %.1f at 100 Hz", bandDb(kBandPass, 1000, 0, 1, 1000), bandDb(kBandPass, 1000, 0, 1, 100));
        CHECK(fabs(bandDb(kTilt, 1000, 4, 0.5f, 1000)) < 0.05 && fabs(bandDb(kTilt, 1000, 4, 0.5f, 20) + 4) < 0.6 &&
              fabs(bandDb(kTilt, 1000, 4, 0.5f, 20000) - 4) < 0.6,
              "Tilt +4 @1k: 0 at 1k (%.2f), %.2f at 20 Hz, %.2f at 20 kHz", bandDb(kTilt, 1000, 4, 0.5f, 1000),
              bandDb(kTilt, 1000, 4, 0.5f, 20), bandDb(kTilt, 1000, 4, 0.5f, 20000));
        CHECK(bandDb(kLowCut12, 300, 0, 4, 300) > 11, "Low Cut 12 with Q 4: a resonant peak (%.1f dB at f)", bandDb(kLowCut12, 300, 0, 4, 300));
        CHECK(bandDb(kLowCut48, 300, 0, 3, 330) > 3, "Low Cut 48 with Q 3: the last stage resonates (%.1f dB near f)",
              bandDb(kLowCut48, 300, 0, 3, 330));
    }

    // ---- 2. the engine: Init is transparent, silence stays silent ---------------------------------------------
    {
        Inst e;
        bool same = true;
        int16_t in[256], o[256];
        int worst = 0;
        for (int b = 0; b < 300; ++b) {
            for (int i = 0; i < 256; ++i) in[i] = (int16_t)(noise() * 30000);
            e.process(in, o, 128);
            for (int i = 0; i < 256; ++i) { const int d = abs(o[i] - in[i]); if (d > worst) worst = d; }
        }
        same = worst <= 1;
        CHECK(same, "Init (every gain 0, the cuts off): output = input within %d LSB", worst);
        int16_t z[256] = {}, oz[256];
        bool silent = true;
        Inst s;
        s.set("preset", "1");
        for (int b = 0; b < 400; ++b) { s.process(z, oz, 128); for (int i = 0; i < 256; ++i) if (oz[i]) silent = false; }
        CHECK(silent, "silence in = silence out (Pop Vocal)");
    }
    {   // full scale + 18 dB: clipped, never wrapped
        Inst e;
        e.set("g4", 18.0);
        e.set("q4", 0.0);
        bool wrap = false;
        auto y = run(e, 400, [](int64_t t) { return 0.99 * sin(2 * M_PI * 500 * t / 44100.0); });
        for (size_t i = 1; i < y.size(); ++i) if (fabs(y[i] - y[i - 1]) > 1.2) wrap = true;
        CHECK(!wrap, "full scale through +18 dB: clipped at the int16 edge, no wrap-around");
    }

    // ---- 3. garbage from the host --------------------------------------------------------------------------------
    {
        Inst e;
        const char *junk[] = {"nan", "inf", "-inf", "1e40", "-1e40", "abc", "", "  ", "1e999", "0x10"};
        for (int i = 0; i < NPARAMS; ++i)
            for (const char *j : junk) e.set(PARAMS[i].key, j);
        e.set("state", "PQ1;b=nan,inf,1e30,-1e30,abc/99999,1,2;byp=nan;qb=9;pre=1e30");
        e.set("state", "garbage");
        bool fin = true;
        for (int i = 0; i < NPARAMS; ++i) {
            const double v = e.get(PARAMS[i].key);
            if (PARAMS[i].momentary) continue;
            const double lo = PARAMS[i].nopts ? 0 : PARAMS[i].min, hi = PARAMS[i].nopts ? PARAMS[i].nopts - 1 : PARAMS[i].max;
            if (!std::isfinite(v) || v < lo - 1e-6 || v > hi + 1e-6) { fin = false; printf("     %s = %g\n", PARAMS[i].key, v); }
        }
        auto y = run(e, 50, [](int64_t) { return 0.5 * noise(); });
        bool ok = true;
        for (float v : y) if (!std::isfinite(v)) ok = false;
        CHECK(fin && ok, "nan / inf / 1e40 / text on every key and a broken state: every value stays finite and in range");
    }

    // ---- 4. clicks: Q-Link-sized steps while a tone plays -----------------------------------------------------
    {
        // a quiet tone, so +18 dB never clips (a clipped tone has corners of its own)
        auto tone = [](int64_t t) { return 0.12 * sin(2 * M_PI * 997 * t / 44100.0); };
        Inst ref;
        auto base = run(ref, 200, tone);
        const double clean = thirdDiffMax(base, 1000);
        struct Case { const char *what; void (*move)(Inst &, int); };
        const Case cases[] = {
            // Q-Link turns: one 1/128 step every other block, up across the whole range and back down
            {"Gain (Q-Link 1/128 steps, Bell at 1 kHz)", [](Inst &e, int k) { e.set("qg4", -18 + 36.0 * tri(k / 2) / 128.0); }},
            {"Freq (Q-Link steps, Bell +12)", [](Inst &e, int k) { if (k == 0) e.set("g4", 12.0); e.set("qf4", 100.0 * tri(k / 2) / 128.0); }},
            {"Q (Q-Link steps, Bell +12)", [](Inst &e, int k) { if (k == 0) e.set("g4", 12.0); e.set("qq4", 100.0 * tri(k / 2) / 128.0); }},
            {"Gain jumping +-18 dB every 40 ms (a preset's worth)", [](Inst &e, int k) { if (k % 14 == 0) e.set("g4", (k / 14) % 2 ? "18" : "-18"); }},
            {"type Bell +12 <-> High Shelf every 40 ms", [](Inst &e, int k) { if (k == 0) e.set("g4", 12.0); if (k % 14 == 0) e.set("t4", (k / 14) % 2 ? "2" : "0"); }},
            {"band on/off every 40 ms (Bell +12)", [](Inst &e, int k) { if (k == 0) e.set("g4", 12.0); if (k % 14 == 0) e.set("on4", (k / 14) % 2 ? "0" : "1"); }},
            {"Pass on/off every 40 ms (Bell +12)", [](Inst &e, int k) { if (k == 0) e.set("g4", 12.0); if (k % 14 == 0) e.set("bypass", (k / 14) % 2 ? "1" : "0"); }},
            {"presets switched every 60 ms", [](Inst &e, int k) { if (k % 20 == 0) { char b[16]; snprintf(b, sizeof b, "%d", (k / 20) % 8); e.set("preset", b); } }},
        };
        for (auto &c : cases) {
            Inst e;
            e.set("f4", hzToPos(1000));
            std::vector<float> y;
            int64_t t = 0;
            for (int k = 0; k < 560; ++k) {
                c.move(e, k);
                auto part = run(e, 1, tone, &t);
                y.insert(y.end(), part.begin(), part.end());
            }
            // the tone's own 3rd difference scales with its level: compare with the loudest it gets (+18 dB max)
            double peak = 0;
            for (size_t i = 1000; i < y.size(); ++i) peak = fmax(peak, fabs(y[i]));
            const double ratio = thirdDiffMax(y, 1000) / (clean * fmax(1.0, peak / 0.12));
            CHECK(ratio < 1.6, "no click: %s (3rd difference %.2fx a clean tone at that level)", c.what, ratio);
        }
    }

    // ---- 5. parameters, Q-Links, display ---------------------------------------------------------------------------
    {
        Inst e;
        e.changed();
        CHECK(e.text("f3_display") == "200Hz" && e.text("g3_display") == "0.0dB" && e.text("q3_display") == "1.0" &&
              e.text("g1_display") == "--", "Init: band 3 reads 200 / 0.0 / 1.0 in its knobs, the Low Cut's Gain reads --");
        e.set("g3", 3.0);
        CHECK(e.text("g3_display") == "+3.0dB" && e.text("qg3_display") == "+3.0 dB", "Gain +3: knob \"%s\", Q-Link \"%s\"",
              e.text("g3_display").c_str(), e.text("qg3_display").c_str());
        e.set("f6", hzToPos(1520));
        CHECK(e.text("f6_display") == "1.5kHz" && e.text("f5_display") == "1.5kHz", "Freq 1.52 kHz reads \"%s\"", e.text("f6_display").c_str());
        auto c = e.changed();
        CHECK(has(c, "qg3") && !has(c, "g3") && !has(c, "f6"), "a knob turn reports its Q-Link twin (qg3), not itself, nor a band outside the bank");
        e.set("qf2", 70.0);
        c = e.changed();
        CHECK(fabs(e.get("f2") - 70) < 1e-4 && has(c, "f2") && !has(c, "qf2"), "Q-Link 2 Freq moves band 2 and reports f2");
        e.set("qbank", "1");
        c = e.changed();
        CHECK(e.text("qf1_name") == "5 Freq" && e.text("qt4_name") == "8 Type" && has(c, "qf1") && has(c, "qt4"),
              "bank 5-8: Q-Links named \"%s\" .. \"%s\", all 16 reported", e.text("qf1_name").c_str(), e.text("qt4_name").c_str());
        e.set("qg2", -6.0);
        CHECK(fabs(e.get("g6") + 6) < 1e-4 && fabs(e.get("g2") - 0) < 1e-4, "bank 5-8: Q-Link 2 Gain sets band 6 (band 2 untouched)");
        CHECK(fabs(e.get("qg2") + 6) < 1e-4 && fabs(e.get("qf1") - e.get("f5")) < 1e-4, "proxies read the bank's bands back");
        e.set("qt3", "11");
        CHECK(e.get("t7") == 11 && e.text("g7_display") == "--", "Q-Link 3 Type -> band 7 Notch (its Gain reads --)");
        // display values
        Inst d;
        d.set("g4", 12.0);
        d.set("f4", hzToPos(1000));
        d.set("q4", qToPos(1.0f));
        run(d, 2, [](int64_t) { return 0.0; });
        int jc = -1;
        for (int j = 0; j < kCurveCols; ++j) {
            const double lo = 20 * pow(1000.0, (double)j / kCurveCols), hi = 20 * pow(1000.0, (j + 1.0) / kCurveCols);
            if (lo <= 1000 && 1000 < hi) jc = j;
        }
        char k[16];
        snprintf(k, sizeof k, "cv%02d", jc + 1);
        CHECK(fabs(d.get(k) - 12) < 0.6, "curve: the column at 1 kHz reads %.2f dB (Bell +12)", d.get(k));
        auto drawn = [&d](const char *k) { const long f = lrint((d.get(k) + 18) / 36 * (kDotFrames - 1)); return -18 + 36.0 * f / (kDotFrames - 2); };
        CHECK(fabs(drawn("dy4") - 12) < 0.5 && (int)d.get("dx4") == 1 + (int)lrint(hzToPos(1000) / 100 * (kDotPos - 1)),
              "dot 4 at place %d, drawn at %.2f dB", (int)d.get("dx4"), drawn("dy4"));
        CHECK(d.get("dx1") > 0 && d.get("dy1") == 18 && d.get("dy8") == 18, "the two cuts are off at Init: their dots are hollow rings (last frame)");
        d.set("on1", "1");
        run(d, 2, [](int64_t) { return 0.0; });
        CHECK(fabs(drawn("dy1") + 3) < 0.5, "band 1 on: a filled dot at its -3 dB point (drawn at %.2f)", drawn("dy1"));
        d.set("cv10", 5.0);
        d.set("dx2", "7");
        CHECK(fabs(d.get("cv10") - d.get("cv10")) < 1e-9 && d.get("dx2") != 7, "display values ignore host writes");
    }

    // ---- 6. the type browser and the preset browser ------------------------------------------------------------
    {
        Inst e;
        e.changed();
        e.set("topen3", "1");
        auto c = e.changed();
        CHECK(e.get("tb_open") == 1 && e.get("tsel") == 3 && e.get("tb_hl") == 1 && has(c, "tb_open") && has(c, "tsel"),
              "tap band 3's type: the browser opens for band 3, Bell highlighted");
        e.set("tpick6", "1");
        c = e.changed();
        CHECK(e.get("t3") == 5 && e.get("tb_open") == 0 && has(c, "t3") && has(c, "qt3"), "pick Low Cut 24: band 3 changes, the browser closes");
        e.set("topen2", "1");
        e.set("preset_list", "1");
        CHECK(e.get("browsing") == 1 && e.get("tb_open") == 0, "opening the preset browser closes the type browser");
        e.set("topen5", "1");
        CHECK(e.get("browsing") == 0 && e.get("tb_open") == 1 && e.get("tsel") == 5, "... and the other way round");
        e.set("topen5", "1");
        CHECK(e.get("tb_open") == 0, "tapping the same type name again closes it");
        e.set("topen1", "1");
        e.set("tb_close", "1");
        CHECK(e.get("tb_open") == 0, "a tap on the browser's background closes it");
        e.set("tpick2", "1");
        CHECK(e.get("t1") == 4, "a pick with the browser closed does nothing");
    }

    // ---- 7. presets --------------------------------------------------------------------------------------------
    {
        bool allOk = true;
        for (int i = 0; i < kNumPresets; ++i) {
            Inst e;
            char b[16];
            snprintf(b, sizeof b, "%d", i);
            e.set("preset", "5");
            e.set("preset", b);
            for (int k = 0; k < 8; ++k) {
                const PresetBand &pb = kPresets[i].band[k];
                char key[16];
                snprintf(key, sizeof key, "t%d", k + 1);
                bool ok = e.get(key) == pb.type;
                snprintf(key, sizeof key, "on%d", k + 1); ok = ok && e.get(key) == pb.on;
                snprintf(key, sizeof key, "f%d", k + 1); ok = ok && fabs(posToHz(e.get(key)) - pb.hz) < pb.hz * 1e-3;
                snprintf(key, sizeof key, "g%d", k + 1); ok = ok && fabs(e.get(key) - pb.gain) < 1e-3;
                snprintf(key, sizeof key, "q%d", k + 1); ok = ok && fabs(posToQ(e.get(key)) - pb.q) < pb.q * 1e-3;
                if (!ok) { allOk = false; printf("     preset %s band %d differs\n", kPresets[i].name, k + 1); }
            }
            auto y = run(e, 100, [](int64_t) { return 0.25 * noise(); });
            for (float v : y) if (!std::isfinite(v)) allOk = false;
        }
        CHECK(allOk, "all %d presets load exactly (type, on, Hz, dB, Q per band) and play finite audio", kNumPresets);
        Inst e;
        bool def = true;
        for (int i = 0; i < NPARAMS; ++i) {
            if (PARAMS[i].momentary || !strncmp(PARAMS[i].key, "cv", 2) || !strncmp(PARAMS[i].key, "sp", 2) ||
                !strncmp(PARAMS[i].key, "dx", 2) || !strncmp(PARAMS[i].key, "dy", 2))
                continue;
            const double v = e.get(PARAMS[i].key);
            const double want = PARAMS[i].nopts ? lrint(PARAMS[i].def * (PARAMS[i].nopts - 1))
                                                : PARAMS[i].min + PARAMS[i].def * (PARAMS[i].max - PARAMS[i].min);
            if (fabs(v - want) > 1e-3) { def = false; printf("     %s: engine %g, params.json %g\n", PARAMS[i].key, v, want); }
        }
        CHECK(def, "a fresh insert = params.json's defaults = preset 0 (Init)");
        e.set("g3", 5.0);
        e.set("preset", "0");
        CHECK(fabs(e.get("g3") - 5) < 1e-4, "re-picking the current preset from the menu keeps a tweak (MPC re-selects it at load)");
        e.set("pick1", "1");
        CHECK(fabs(e.get("g3")) < 1e-4, "picking it in the browser restores it");
    }

    // ---- 8. save + reload ----------------------------------------------------------------------------------------
    {
        Inst a;
        a.set("preset", "12");
        a.set("g5", -7.25);
        a.set("t2", "13");
        a.set("on8", "1");
        a.set("bypass", "1");
        a.set("qbank", "1");
        a.set("analyzer", "0");
        char st[8192];
        E->get_param(a.p, "state", st, sizeof st);
        Inst b;
        b.set("state", st);
        bool same = true;
        for (int i = 0; i < NPARAMS; ++i) {
            if (PARAMS[i].momentary || !strncmp(PARAMS[i].key, "sp", 2)) continue;
            if (fabs(a.get(PARAMS[i].key) - b.get(PARAMS[i].key)) > 1e-3) { same = false; printf("     %s differs\n", PARAMS[i].key); }
        }
        CHECK(same && b.text("preset") == "12", "save + reload: every value, Pass, the bank, Analyzer and the preset's name (%zu bytes)", strlen(st));
        auto c = b.changed();
        CHECK(has(c, "g5") && has(c, "bypass") && has(c, "analyzer") && has(c, "qf1"), "after a reload everything is reported to MPC");
    }

    // ---- 9. the analyser: shows the signal, pauses with the screen closed, the switch -------------------------------
    {
        auto sp = [](Inst &e) { std::vector<double> v; char k[16]; for (int j = 0; j < kSpecCols; ++j) { snprintf(k, sizeof k, "sp%02d", j + 1); v.push_back(e.get(k)); } return v; };
        auto sum = [](const std::vector<double> &v) { double s = 0; for (double x : v) s += x; return s; };
        Inst e;
        int64_t t = 0;
        auto tone = [](int64_t tt) { return 0.3 * sin(2 * M_PI * 1000 * tt / 44100.0); };
        run(e, 60, tone, &t);
        auto v = sp(e);
        int jmax = 0;
        for (int j = 0; j < kSpecCols; ++j) if (v[j] > v[jmax]) jmax = j;
        const double lo = 20 * pow(1000.0, (double)jmax / kSpecCols), hi = 20 * pow(1000.0, (jmax + 1.0) / kSpecCols);
        CHECK(lo <= 1000 && 1000 <= hi * 1.001 && v[jmax] > 0.6, "a 1 kHz tone peaks in the 1 kHz column (%.0f-%.0f Hz, level %.2f)", lo, hi, v[jmax]);
        // MPC polls the knobs' text while the screen is up: every 100 ms for 4 s
        auto poll = [&](int blocks, bool screen, double amp) {
            for (int b = 0; b < blocks; ++b) {
                run(e, 1, [amp](int64_t) { return amp * noise(); }, &t);
                if (screen && b % 34 == 0) e.text("f1_display");
            }
        };
        poll(1400, true, 0.2);
        const double open = sum(sp(e));
        poll(100, true, 0.02);
        const double quieter = sum(sp(e));
        CHECK(open > 5 && quieter < open * 0.9, "screen up (MPC polling): the spectrum follows the signal (%.1f -> %.1f)", open, quieter);
        poll(1100, false, 0.001);   // screen closed for 3.2 s (the columns fall, then stop)
        const double closedA = sum(sp(e));
        poll(400, false, 0.2);      // a loud part while it is closed
        const double closedB = sum(sp(e));
        CHECK(fabs(closedA - closedB) < 1e-9, "screen closed (no polling for 2.5 s): the analyser stops (frozen, %.2f = %.2f)", closedA, closedB);
        poll(40, true, 0.2);
        const double back = sum(sp(e));
        CHECK(back > closedB + 3, "screen opened again: it runs again at once (%.1f -> %.1f within 0.12 s)", closedB, back);
        e.set("analyzer", "0");
        poll(20, true, 0.2);
        CHECK(sum(sp(e)) == 0, "Analyzer off: every column empty (and the skin hides them)");
        poll(200, true, 0.2);
        CHECK(sum(sp(e)) == 0, "... and they stay empty while it is off");
        e.set("analyzer", "1");
        poll(20, true, 0.2);
        const double on1 = sum(sp(e));
        bool stays = true;
        for (int k = 0; k < 20; ++k) { poll(170, true, 0.2); if (sum(sp(e)) < on1 * 0.5) stays = false; }
        CHECK(on1 > 5 && stays, "Analyzer on again: it shows up (%.1f) and keeps showing for 10 s of audio", on1);
        Inst never;   // a host that never polls the value text: the analyser must not stall
        int64_t t2 = 0;
        run(never, 1500, [](int64_t) { return 0.2 * noise(); }, &t2);
        const double nv = sum(sp(never));
        run(never, 150, [](int64_t) { return 0.01 * noise(); }, &t2);
        CHECK(nv > 5 && sum(sp(never)) < nv * 0.8, "no polling ever seen: the analyser keeps running (%.1f -> %.1f)", nv, sum(sp(never)));
        Inst touch;   // polling seen, then only host writes (Q-Link turns) keep it alive
        int64_t t3 = 0;
        for (int b = 0; b < 1400; ++b) { run(touch, 1, [](int64_t) { return 0.2 * noise(); }, &t3); if (b % 34 == 0) touch.text("g1_display"); }
        double mid = 0;
        for (int b = 0; b < 1600; ++b) {
            run(touch, 1, [b](int64_t) { return (b < 1000 ? 0.2 : 0.01) * noise(); }, &t3);
            if (b % 50 == 0) touch.set("g3", (b % 100) ? "1" : "0");
            if (b == 999) mid = sum(sp(touch));
        }
        CHECK(sum(sp(touch)) < mid * 0.6, "no polling for 4.6 s but Q-Link turns: still running (%.1f -> %.1f)", mid, sum(sp(touch)));
    }

    // ---- 10. odd block sizes, denormals, CPU -------------------------------------------------------------------
    {
        Inst a, b;
        a.set("preset", "2");
        b.set("preset", "2");
        std::vector<int16_t> x(128 * 200 * 2);
        for (auto &v : x) v = (int16_t)(noise() * 20000);
        std::vector<int16_t> ya(x.size()), yb(x.size());
        for (size_t i = 0; i < x.size() / 2; i += 128) a.process(&x[2 * i], &ya[2 * i], 128);
        size_t i = 0;
        const int sizes[] = {37, 1, 128, 200, 91, 128, 64, 5};
        for (int k = 0; i < x.size() / 2; ++k) {
            int n = sizes[k % 8];
            if (i + n > x.size() / 2) n = (int)(x.size() / 2 - i);
            b.process(&x[2 * i], &yb[2 * i], n);
            i += n;
        }
        bool same = true;
        for (size_t j = 0; j + 256 < ya.size(); ++j) if (abs(ya[j] - yb[j + 256]) > 0) { same = false; break; }
        CHECK(same, "odd block sizes: the same audio, one 128-frame block later");
    }
    {
        Inst e;
        e.set("preset", "1");
        for (int b = 1; b <= 8; ++b) { char k[16]; snprintf(k, sizeof k, "on%d", b); e.set(k, "1"); }
        int16_t in[256], o[256];
        auto timeit = [&](bool loud, int blocks) {
            clock_t c0 = clock();
            for (int k = 0; k < blocks; ++k) {
                for (int i = 0; i < 256; ++i) in[i] = loud ? (int16_t)(noise() * 30000) : 0;
                e.process(in, o, 128);
            }
            return (double)(clock() - c0) / CLOCKS_PER_SEC / blocks * 1000;
        };
        const double loud = timeit(true, 3000);
        const double quiet = timeit(false, 3000);
        CHECK(quiet < loud * 2.0, "after loud audio, silence costs no more (denormals flushed): %.4f vs %.4f ms per block", quiet, loud);
        // worst case: every band on, the steepest types, the analyser running
        for (int b = 1; b <= 8; ++b) { char k[16]; snprintf(k, sizeof k, "t%d", b); e.set(k, b % 2 ? "6" : "10"); }
        const double worst = timeit(true, 5000);
        printf("info CPU: %.4f ms per 128-frame block = %.2f %% of one x86 core (8 bands, all 48 dB/oct cuts, analyser on)\n",
               worst, worst / (128 / 44.1) * 100);
    }

    // ---- 11. dynamics (1.1) -------------------------------------------------------------------------------------
    {
        auto rmsDb = [](const std::vector<float> &y, size_t from) {
            double s = 0; for (size_t i = from; i < y.size(); ++i) s += (double)y[i] * y[i];
            return 10 * log10(s / (y.size() - from) + 1e-30);
        };
        auto toneGain = [&](Inst &e, double hz, double peakDb, int blocks = 200) {
            const double a = pow(10, peakDb / 20);
            int64_t t = 0;
            std::vector<float> y = run(e, blocks, [&](int64_t i) { return a * sin(2 * M_PI * hz * i / 44100.0); }, &t);
            return rmsDb(y, y.size() / 2) - 20 * log10(a / sqrt(2.0));
        };
        auto setupDeEss = [](Inst &e) {   // band 6 (Init: Bell 3.5k, 0 dB) as a de-esser at 6.5 kHz
            e.set("dsel", 5);
            e.set("f6", (double)hzToPos(6500));
            e.set("q6", (double)qToPos(3.0f));
            e.set("dthr", -30.0);
            e.set("drng", -6.0);
            e.set("datt", 100.0 * log(1.0 / kAttLo) / log(kAttHi / kAttLo));
            e.set("drel", 100.0 * log(60.0 / kRelLo) / log(kRelHi / kRelLo));
        };
        Inst a;
        setupDeEss(a);
        const double gq = toneGain(a, 6500, -40), gm = toneGain(a, 6500, -25), gl = toneGain(a, 6500, -10);
        CHECK(fabs(gq) < 0.3 && fabs(gl + 6) < 0.6 && fabs(gm + 3) < 0.8,
              "dynamic bell (6.5 kHz, Range -6, Threshold -30): -40 dBFS %+.2f, -25 %+.2f, -10 %+.2f dB (want 0 / -3 / -6)", gq, gm, gl);
        CHECK(fabs(a.get("dact") + 6) < 1.01 && a.text("drng_display") == "-6.0dB" && a.text("dthr_display") == "-30dB" &&
              a.text("datt_display") == "1.0ms" && a.text("drel_display") == "60ms",
              "panel: gain now %.0f dB, texts \"%s\" \"%s\" \"%s\" \"%s\"", a.get("dact"), a.text("dthr_display").c_str(),
              a.text("drng_display").c_str(), a.text("datt_display").c_str(), a.text("drel_display").c_str());
        const double low = toneGain(a, 300, -6);
        CHECK(fabs(low) < 0.3, "a loud 300 Hz tone doesn't trigger the 6.5 kHz band (%+.2f dB)", low);
        toneGain(a, 6500, -10, 50);
        // Release is the detector's time constant: its level falls ~8.7 dB per Release time, here 20 dB under the
        // threshold's top takes ~140 ms (60 ms Release); 300 ms later it is all back
        const double back = toneGain(a, 6500, -40, 100);
        CHECK(fabs(back) < 0.6, "released after the loud part (%+.2f dB, 300 ms window)", back);
        // the dot and the curve follow the dynamic gain (Init's curve is flat; band 6 dips while acting)
        toneGain(a, 6500, -10, 60);
        const double dy6 = a.get("dy6");
        CHECK(dy6 < -4.5, "band 6's dot follows the dynamic cut (%.1f dB)", dy6);
        // Range 0 = static: a changed Threshold alone changes nothing (no detector runs)
        Inst s1, s2;
        s2.set("dsel", 3); s2.set("dthr", -50.0);
        int64_t t1 = 0, t2 = 0;
        rng = 12345;
        std::vector<float> y1 = run(s1, 100, [](int64_t) { return 0.5 * noise(); }, &t1);
        rng = 12345;
        std::vector<float> y2 = run(s2, 100, [](int64_t) { return 0.5 * noise(); }, &t2);
        double dmax = 0; for (size_t i = 0; i < y1.size(); ++i) dmax = fmax(dmax, fabs(y1[i] - y2[i]));
        CHECK(dmax == 0, "Range 0 = a static band: a Threshold alone changes nothing (largest difference %g)", dmax);
        // boost when loud (Range > 0) on a low shelf
        Inst b, bs;
        b.set("dsel", 1); b.set("drng", 4.0); b.set("dthr", -30.0);
        bs.set("g2", 4.0);   // the same shelf at a static +4 dB: what full Range gives at 60 Hz
        const double gb = toneGain(b, 60, -6), gbs = toneGain(bs, 60, -6), gbq = toneGain(b, 60, -50);
        CHECK(fabs(gb - gbs) < 0.3 && fabs(gbq) < 0.3,
              "Range +4 on the low shelf: a loud 60 Hz tone boosted %+.2f dB (static +4 shelf: %+.2f), a quiet one %+.2f", gb, gbs, gbq);
        // a gain riding up and down doesn't click: 1 kHz steady + 6.5 kHz bursts with soft edges, compare to no dynamics
        Inst c, d0;
        setupDeEss(c);
        auto burst = [](int64_t i) {
            const double ph = fmod(i / 44100.0, 0.2) / 0.2, env = ph < 0.5 ? 0.5 - 0.5 * cos(2 * M_PI * ph * 2) : 0;
            return 0.2 * sin(2 * M_PI * 1000 * i / 44100.0) + 0.4 * env * sin(2 * M_PI * 6500 * i / 44100.0);
        };
        int64_t tc = 0, td = 0;
        std::vector<float> yc = run(c, 400, burst, &tc), yd = run(d0, 400, burst, &td);
        const double kc = thirdDiffMax(yc, 20000), kd = thirdDiffMax(yd, 20000);
        CHECK(kc < kd * 1.3, "dynamic de-essing on bursts: no clicks (3rd difference %.4f vs %.4f without)", kc, kd);
    }

    // ---- 12. Learn (1.1) ---------------------------------------------------------------------------------------
    {
        // a synthetic singer: pitch 110 -> 220 Hz every 4 s (vibrato), harmonics -6 dB/oct (-12 over 3 kHz), a resonance
        // +9.5 dB at 350 Hz, a dip at 3 kHz,
        // an "s" (noise ringing at 7 kHz) for 120 ms every 0.8 s; level breathing 0.6..1
        struct Voice {
            double ph = 0, z1 = 0, z2 = 0; uint32_t r = 99;
            bool sib(int64_t n) const { return fmod(n / 44100.0, 0.8) < 0.12; }
            double operator()(int64_t n) {
                const double t = n / 44100.0, f0 = 110 * pow(2.0, fmod(t, 4.0) / 4.0) * (1 + 0.01 * sin(2 * M_PI * 5 * t));
                ph += 2 * M_PI * f0 / 44100;
                double v = 0;
                for (int h = 1; h * f0 < 16000; ++h) {
                    const double fh = h * f0;
                    double a = 1.0 / h;
                    a *= 1 + 2.0 * exp(-pow(log2(fh / 350), 2) / 0.08);
                    a *= 1 - 0.6 * exp(-pow(log2(fh / 3000), 2) / 0.1);
                    if (fh > 3000) a *= 3000 / fh;   // a real voice's harmonics fall ~12 dB/oct up there
                    v += a * sin(h * ph);
                }
                r = r * 1664525u + 1013904223u;
                const double nz = ((r >> 8) / 8388608.0 - 1.0) * 0.05;
                const double w = 2 * M_PI * 7000 / 44100, rr = 0.9;
                const double y = nz + 2 * rr * cos(w) * z1 - rr * rr * z2;
                z2 = z1; z1 = y;
                return (sib(n) ? 0.3 * y : 0.08 * v) * (0.6 + 0.4 * sin(2 * M_PI * 0.3 * t));
            }
        };
        int pop = -1, init = 0;
        for (int i = 0; i < kNumPresets; ++i) if (!strcmp(kPresets[i].name, "Vox: Pop Vocal")) pop = i;
        auto learn = [&](Inst &e, int preset, double maxSec, Voice &v, int64_t &t) {
            e.set("preset", (double)preset);
            e.changed();
            e.set("learn", 1.0);
            int blocks = 0;
            while (e.get("learn") > 0.5 && blocks < maxSec * 44100 / 128) { run(e, 1, [&](int64_t i) { return v(i); }, &t); ++blocks; }
            return blocks * 128 / 44100.0;
        };
        Inst e;
        Voice v; int64_t t = 0;
        const clock_t c0 = clock();
        const double secs = learn(e, pop, 12, v, t);
        const double cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;
        auto ch = e.changed();
        const double lc = posToHz((float)e.get("f1")), mud = posToHz((float)e.get("f3")), sib = posToHz((float)e.get("f6"));
        const double pres = posToHz((float)e.get("f5"));
        CHECK(secs > 5 && secs < 8 && e.get("learn") < 0.5 && has(ch, "learn") && has(ch, "voice") && has(ch, "f1"),
              "Learn listens ~6 s of voice (%.1f s), switches itself off and reports the moved knobs", secs);
        printf("info Learn: \"%s\"; low cut %.0f Hz, mud band %.0f Hz, presence %.0f Hz, de-esser %.0f Hz; %.2f s CPU for %.1f s\n",
               e.text("voice").c_str(), lc, mud, pres, sib, cpu, secs);
        CHECK(e.text("voice").find("Alto/Tenor") == 0 && e.text("voice").find("Hz") != std::string::npos,
              "it names the voice and its range: \"%s\" (sung 110-220 Hz)", e.text("voice").c_str());
        CHECK(lc > 90 && lc < 115, "the low cut moves to just under the lowest notes: %.0f Hz (lowest ~110)", lc);
        CHECK(mud > 320 && mud < 440, "the mud band finds the 350 Hz resonance: %.0f Hz", mud);
        CHECK(pres > 2700 && pres < 3400, "the presence boost goes where the voice is missing some (3 kHz dip): %.0f Hz", pres);
        CHECK(sib > 6200 && sib < 7800, "the de-esser finds the 7 kHz 's': %.0f Hz", sib);
        // the de-esser's Threshold: it acts on the s's and leaves the vowels alone
        e.set("dsel", 5);
        const double thr = e.get("dthr");
        double onS = 0, onV = 0; int nS = 0, nV = 0;
        for (int blk = 0; blk < 700; ++blk) {
            const bool sb = v.sib(t + 64);
            const double ph = fmod((t + 64) / 44100.0, 0.8);   // vowels: from 0.4 s after an "s" (its release is over)
            run(e, 1, [&](int64_t i) { return v(i); }, &t);
            const double g = e.get("dact");
            if (sb) { onS += g; ++nS; } else if (ph > 0.4) { onV += g; ++nV; }
        }
        onS /= nS; onV /= nV;
        CHECK(onS < -1.5 && onV > -0.7, "de-esser Threshold %.0f dB: %.1f dB on the s's, %.1f dB on the vowels", thr, onS, onV);
        // Learn again: the same result (it starts from the preset, not from the last Learn)
        const double f3a = e.get("f3"), g3a = e.get("g3"), f6a = e.get("f6");
        Voice v2; int64_t t2 = 0;
        learn(e, pop, 12, v2, t2);
        CHECK(fabs(e.get("f3") - f3a) < 0.5 && fabs(e.get("g3") - g3a) < 0.3 && fabs(e.get("f6") - f6a) < 0.5,
              "Learning twice doesn't drift (mud band %.1f / %.1f)", e.get("f3"), f3a);
        // saved and reloaded: the dynamics, the band shown, the text
        char st[8192];
        E->get_param(e.p, "state", st, sizeof st);
        Inst r;
        r.set("state", st);
        r.set("dsel", 5);
        CHECK(fabs(r.get("dthr") - thr) < 0.01 && fabs(r.get("drng") - e.get("drng")) < 0.01 && r.text("voice") == e.text("voice"),
              "save + reload keeps the dynamics and Learn's result (\"%s\")", r.text("voice").c_str());
        // nothing to hear: Learn stopped early changes nothing
        Inst q;
        q.set("preset", (double)pop);
        const double f3q = q.get("f3");
        q.set("learn", 1.0);
        run(q, 200, [](int64_t) { return 0.0; });
        q.set("learn", 0.0);
        CHECK(q.get("learn") < 0.5 && q.text("voice").find("Not enough") == 0 && q.get("f3") == f3q,
              "Learn on silence, stopped: \"%s\", nothing moved", q.text("voice").c_str());
        // Init has nothing to move but the (off) cuts: it says so
        Inst z; Voice v3; int64_t t3 = 0;
        learn(z, init, 12, v3, t3);
        CHECK(z.text("voice").find("pick a preset") != std::string::npos, "Learn on Init: \"%s\"", z.text("voice").c_str());
        // what Learn costs while it listens (only then): noise at -20 dBFS, the same 4 s with Learn off and on
        {
            Inst off, on;
            auto nz = [](int64_t) { return 0.1 * noise(); };
            clock_t a0 = clock();
            run(off, 1378, nz);
            const double toff = (double)(clock() - a0) / CLOCKS_PER_SEC;
            on.set("learn", 1.0);
            a0 = clock();
            run(on, 1378, nz);
            const double ton = (double)(clock() - a0) / CLOCKS_PER_SEC;
            printf("info CPU: 4 s of audio %.1f ms with Learn off, %.1f ms while Learn listens (+%.2f %% of one x86 core)\n",
                   toff * 1000, ton * 1000, (ton - toff) / 4 * 100);
            Inst dyn;
            dyn.set("preset", (double)pop);   // two dynamic bands
            a0 = clock();
            run(dyn, 1378, nz);
            const double td = (double)(clock() - a0) / CLOCKS_PER_SEC;
            printf("info CPU: Vox: Pop Vocal (2 dynamic bands) %.1f ms for 4 s (%.2f %% of one x86 core)\n", td * 1000, td / 4 * 100);
        }
        // idle: Learn off means nothing analysed (progress 0, no reports)
        Inst w;
        run(w, 400, [](int64_t i) { return 0.3 * sin(2 * M_PI * 200 * i / 44100.0); });
        CHECK(w.get("learn_prog") == 0 && w.text("voice") == " ", "Learn off: nothing listened to");
    }


    // ---- 13. Learn on drums, instruments, bass (1.1.2) -----------------------------------------------------------
    {
        auto presetIdx = [&](const char *name) {
            for (int i = 0; i < kNumPresets; ++i) if (!strcmp(kPresets[i].name, name)) return i;
            return -1;
        };
        auto learnOn = [&](Inst &e, int preset, double maxSec, std::function<double(int64_t)> gen) {
            int64_t t = 0;
            e.set("preset", (double)preset);
            e.changed();
            e.set("learn", 1.0);
            int blocks = 0;
            while (e.get("learn") > 0.5 && blocks < maxSec * 44100 / 128) { run(e, 1, gen, &t); ++blocks; }
            return blocks * 128 / 44100.0;
        };
        // a drum loop at 120 BPM: kick (sweep 120 -> 50 Hz) on every beat, snare (190 Hz body + noise + a 520 Hz ring) on 2 and 4,
        // closed hats on every 8th (noise above 6 kHz)
        struct Drums {
            double kph = 0, lp = 0;
            double operator()(int64_t n) {
                const double t = n / 44100.0, bt = fmod(t, 2.0);
                double v = 0;
                const double ks = fmod(bt, 0.5);
                const double kf = 50 + 70 * exp(-ks / 0.03);
                kph += 2 * M_PI * kf / 44100;
                v += 0.9 * sin(kph) * exp(-ks / 0.12);
                if (bt >= 0.5) {
                    const double ss = fmod(bt - 0.5, 1.0);
                    v += exp(-ss / 0.09) * (0.35 * sin(2 * M_PI * 190 * ss) + 0.25 * noise() + 0.7 * sin(2 * M_PI * 520 * ss) * exp(-ss / 0.1));
                }
                const double hs = fmod(bt, 0.25), nz = noise();
                lp += 0.57 * (nz - lp);                 // one pole at ~6 kHz: hats = noise - its low pass
                v += 0.25 * (nz - lp) * exp(-hs / 0.03);
                return 0.4 * v;
            }
        };
        // a synth lead: saw notes 220 - 660 Hz every 0.25 s, -6 dB/oct
        struct Lead {
            double ph = 0;
            double operator()(int64_t n) {
                static const double notes[8] = {220, 247, 262, 330, 392, 440, 523, 659};
                const double t = n / 44100.0, ns = fmod(t, 0.25);
                const double f0 = notes[(int)(t / 0.25) % 8];
                ph += 2 * M_PI * f0 / 44100;
                double v = 0;
                for (int h = 1; h * f0 < 16000; ++h) v += sin(h * ph) / h;
                return 0.25 * v * (ns < 0.005 ? ns / 0.005 : exp(-(ns - 0.005) / 0.4));
            }
        };
        // a pad: a four-note chord (130, 164, 196, 247 Hz, saws) swelling in over 1.5 s, changing every 4 s
        struct Pad {
            double ph[4] = {0, 0, 0, 0};
            double operator()(int64_t n) {
                const double t = n / 44100.0, ct = fmod(t, 4.0);
                const int ch = (int)(t / 4.0) % 2;
                static const double fr[2][4] = {{130.8, 164.8, 196.0, 246.9}, {110.0, 164.8, 220.0, 277.2}};
                double v = 0;
                for (int k = 0; k < 4; ++k) {
                    ph[k] += 2 * M_PI * fr[ch][k] / 44100;
                    for (int h = 1; h * fr[ch][k] < 12000; ++h) v += sin(h * ph[k]) / (h * h * 0.5 + 0.5);
                }
                return 0.08 * v * (ct < 1.5 ? ct / 1.5 : 1.0);
            }
        };
        // a sub bass: 55 / 49 / 41 Hz notes (sine + weak 2nd / 3rd) every 0.5 s with a decay, like an 808
        struct Bass {
            double ph = 0;
            double operator()(int64_t n) {
                static const double notes[4] = {55, 55, 49, 41.2};
                const double t = n / 44100.0, ns = fmod(t, 0.5);
                ph += 2 * M_PI * notes[(int)(t / 0.5) % 4] / 44100;
                return 0.6 * (sin(ph) + 0.4 * sin(2 * ph) + 0.2 * sin(3 * ph)) * exp(-ns / 0.35) * (ns < 0.004 ? ns / 0.004 : 1.0);
            }
        };

        // -- drums
        const int snare = presetIdx("Drm: Snare Crack");
        Inst d;
        Drums dr;
        const double sd = learnOn(d, snare, 30, [&](int64_t i) { return dr(i); });
        auto chd = d.changed();
        const double dlc = posToHz((float)d.get("f1")), dring = posToHz((float)d.get("f3"));
        printf("info Learn drums: \"%s\" (%.1f s); low cut %.0f Hz (preset 90), band 3 %.0f Hz (preset 450)\n", d.text("voice").c_str(), sd,
               dlc, dring);
        CHECK(d.get("learn") < 0.5 && d.text("voice").find("Drums") == 0 && has(chd, "voice") && has(chd, "f1"),
              "Learn on a drum loop names it: \"%s\" and switches itself off", d.text("voice").c_str());
        CHECK(dlc > 38 && dlc < 75, "drums: the low cut goes just under the kick (it has content from ~40 Hz): %.0f Hz", dlc);
        CHECK(dring > 470 && dring < 575, "drums: the box cut finds the snare's 520 Hz ring: %.0f Hz", dring);
        CHECK(d.text("voice").find("pick a preset") == std::string::npos, "drums: a drum preset has something to move");
        // the same loop with a Vox preset loaded is still drums (clearly unpitched); and Init has nothing to move
        {
            Inst e; Drums d2;
            learnOn(e, presetIdx("Vox: Pop Vocal"), 30, [&](int64_t i) { return d2(i); });
            CHECK(e.text("voice").find("Drums") == 0, "drums under a Vox preset are still drums: \"%s\"", e.text("voice").c_str());
        }
        // Learning twice gives the same result
        {
            const double f3a = d.get("f3"), g3a = d.get("g3"), f1a = d.get("f1");
            Drums d3;
            learnOn(d, snare, 30, [&](int64_t i) { return d3(i); });
            CHECK(fabs(d.get("f3") - f3a) < 2.0 && fabs(d.get("g3") - g3a) < 0.5 && fabs(d.get("f1") - f1a) < 2.0,
                  "drums: learning twice doesn't drift (%.1f / %.1f)", d.get("f3"), f3a);
        }

        // -- a lead
        Inst l;
        Lead ld;
        const double sl = learnOn(l, presetIdx("Inst: Synth Lead"), 30, [&](int64_t i) { return ld(i); });
        const double llc = posToHz((float)l.get("f1"));
        printf("info Learn lead: \"%s\" (%.1f s); low cut %.0f Hz (preset 150)\n", l.text("voice").c_str(), sl, llc);
        CHECK(l.get("learn") < 0.5 && l.text("voice").find("Instrument") == 0 && l.text("voice").find("Hz") != std::string::npos,
              "Learn on a synth lead names it and its range: \"%s\"", l.text("voice").c_str());
        CHECK(llc > 165 && llc < 215, "lead: the low cut goes just under the lowest note (220 Hz): %.0f Hz", llc);

        // -- the same lead with a Vox preset loaded (no s's in it): a non-voice would still follow the voice rules when
        // the preset says it is a voice
        {
            Inst e; Lead l2;
            learnOn(e, presetIdx("Vox: Pop Vocal"), 30, [&](int64_t i) { return l2(i); });
            CHECK(e.text("voice").find("Drums") != 0 && e.text("voice").find("Not enough") != 0,
                  "a pitched sound under a Vox preset is not drums: \"%s\"", e.text("voice").c_str());
        }

        // -- a pad (chords: not a single pitch)
        Inst pd;
        Pad pp;
        const double spd = learnOn(pd, presetIdx("Inst: Pads & Strings"), 30, [&](int64_t i) { return pp(i); });
        const double plc = posToHz((float)pd.get("f1"));
        printf("info Learn pad: \"%s\" (%.1f s); low cut %.0f Hz (preset 150)\n", pd.text("voice").c_str(), spd, plc);
        CHECK(pd.get("learn") < 0.5 && pd.text("voice").find("Instrument") == 0, "Learn on a pad: \"%s\"", pd.text("voice").c_str());
        CHECK(plc > 70 && plc < 140, "pad: the low cut goes under the chord's lowest note (110 Hz): %.0f Hz", plc);

        // -- a bass (Bass preset loaded)
        Inst bs;
        Bass bb;
        const double sb = learnOn(bs, presetIdx("Bass: 808 Clean"), 30, [&](int64_t i) { return bb(i); });
        const double blc = posToHz((float)bs.get("f1"));
        printf("info Learn bass: \"%s\" (%.1f s); low cut %.0f Hz (preset 28)\n", bs.text("voice").c_str(), sb, blc);
        CHECK(bs.get("learn") < 0.5 && bs.text("voice").find("Bass") == 0, "Learn on a sub bass: \"%s\"", bs.text("voice").c_str());
        CHECK(blc > 20 && blc < 50, "bass: the low cut stays under the lowest note (41 Hz): %.0f Hz", blc);

        // -- too little: a single hit
        {
            Inst e;
            int64_t t = 0;
            e.set("preset", (double)snare);
            e.changed();
            const double f3e = e.get("f3");
            e.set("learn", 1.0);
            Drums d4;
            run(e, 60, [&](int64_t i) { return d4(i); }, &t);   // 0.17 s: one kick
            run(e, 300, [](int64_t) { return 0.0; }, &t);
            e.set("learn", 0.0);
            CHECK(e.text("voice").find("Not enough") == 0 && e.get("f3") == f3e, "a single kick is not enough: \"%s\"", e.text("voice").c_str());
        }
    }

    printf(fails ? "FAILED: %d of %d checks\n" : "PASSED: %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
