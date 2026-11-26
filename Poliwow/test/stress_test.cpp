// Poliwow stress tests: the hiccup hunt. Build WITHOUT -ffast-math (so NaN / denormal checks mean something), with
// sanitizers; test/run_tests.sh does. Each block prints ok / FAIL.
//   A  loud input (0 dBFS square) at Wow 0: still bit-exact (the limiter must not touch the dry signal)
//   B  after a full build-up, Wow back to 0: exact passthrough again once the tails have died
//   C  60 s worst case per preset (full-scale noise, then a full-scale square), Wow 100: finite, bounded, no runaway
//   D  denormals: loud signal, then 30 s of silence, per preset: no subnormal floats left in the engine's state
//   E  hostile parameter text ("nan", "inf", "", "abc", 1e999, ...) and broken saved states: output stays finite
//   F  fuzz: 20 000 random blocks of random sizes, random parameter changes, tempo / song position jumps, transport
//      on/off: finite, bounded
//   G  clicks: instant Wow / Mix / preset / tempo jumps on a sine: the worst sample-to-sample step stays small
//   H  instances don't share state
//   I  speed: real-time factor of the worst preset on this machine
//   K  pump speed hysteresis at a boundary
//   J  safety net: NaN poured into the state -> the effect restarts itself instead of going silent
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <cstddef>
#include <vector>
#include <string>
extern "C" {
#include "engine.h"
int mpc_engine_changed(void *inst, char *key, int len);
}
#define private public   // the denormal check (D) reads the DSP's float state by name
#include "WowDSP.h"
#undef private

static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++fails; } while (0)

static const int kFs = 44100;
static const mpc_engine_t *E;

static void set(void *p, const char *k, double v) { char b[48]; snprintf(b, sizeof b, "%.6g", v); E->set_param(p, k, b); }
static uint32_t rs = 1;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static double frand() { return rnd() / 4294967296.0; }

// run n frames through p (in blocks of `blk`), input from gen(i, ch), track the output
struct Run { int peak = 0; double energy = 0; bool bad = false; };
template <class G> static Run run(void *p, int n, G gen, std::vector<int16_t> *keepOut = nullptr, int blk = 128,
                                  bool song = false, double bpm = 128, double *ppq = nullptr) {
    Run r;
    std::vector<int16_t> in(2 * 512), out(2 * 512);
    static long long t = 0;
    for (int done = 0; done < n;) {
        int m = std::min(blk, n - done);
        for (int i = 0; i < m; ++i) { in[2 * i] = gen(t + i, 0); in[2 * i + 1] = gen(t + i, 1); }
        if (song && ppq) { E->song_pos(p, *ppq, bpm, 1, m); *ppq += m * bpm / 60.0 / kFs; }
        E->process(p, in.data(), out.data(), m);
        for (int i = 0; i < 2 * m; ++i) {
            r.peak = std::max(r.peak, abs((int)out[i]));
            r.energy += (double)out[i] * out[i];
            if (keepOut) keepOut->push_back(out[i]);
        }
        done += m; t += m;
    }
    r.energy /= std::max(1, 2 * n);
    return r;
}

static int16_t sq(long long i, int) { return (i / 50) % 2 ? 32767 : -32768; }
static int16_t sine(long long i, int ch) { return (int16_t)lrint(20000 * sin(2 * M_PI * (ch ? 330.0 : 220.0) * i / kFs)); }
static int16_t noise(long long, int) { return (int16_t)(rnd() >> 16); }
static int16_t zero(long long, int) { return 0; }

// count subnormal floats in the DSP's float state (every filter, delay, reverb, grain and smoother value, by name)
static size_t subnormals(const WowDSP &d) {
    size_t n = 0;
    auto chk = [&](const float *f, size_t count) {
        for (size_t i = 0; i < count; ++i) if (f[i] != 0.0f && std::fpclassify(f[i]) == FP_SUBNORMAL) ++n;
    };
    auto svf = [&](const WowDSP::SVF &f) { chk(&f.ic1, 1); chk(&f.ic2, 1); };
    for (int c = 0; c < 2; ++c) {
        svf(d.hpf[c]); svf(d.tiltHp[c]); svf(d.airBp[c]); svf(d.hfHp[c]); svf(d.resoHp[c]);
        svf(d.loopHp[c]); svf(d.loopLp[c]); svf(d.shimHp[c]); svf(d.shimLp[c]);
        chk(d.apState[c], 4); chk(&d.phFb[c], 1); chk(&d.hfEnv[c], 1); chk(&d.shimFeed[c], 1);
        chk(d.resoBuf[c], WowDSP::kResoLen); chk(d.shim[c].buf, WowDSP::kShiftLen);
        const WowDSP::Hilbert &h = d.hil[c];
        chk(h.x1, 8); chk(h.x2, 8); chk(h.y1, 8); chk(h.y2, 8); chk(&h.delayed, 1);
    }
    for (int k = 0; k < WowDSP::kFdnLines; ++k) chk(d.fdn[k], WowDSP::kFdnLen);
    chk(d.fdnLp, WowDSP::kFdnLines);
    chk(d.delayBufL, WowDSP::kDelayLen); chk(d.delayBufR, WowDSP::kDelayLen);
    chk(d.gBufL, WowDSP::kGrainLen); chk(d.gBufR, WowDSP::kGrainLen);
    chk(&d.wow, 1); chk(&d.mix, 1); chk(&d.pumpGain, 1); chk(&d.limGain, 1); chk(&d.decay, 1);
    return n;
}

// a preset's number by its name (the list order can change; the tests shouldn't care)
static int S(const char *name) {
    for (int i = 0; i < kNumPresets; ++i) if (!strcmp(kPresets[i].name, name)) return i;
    printf("FAIL no preset called %s\n", name);
    exit(1);
}

int main() {
    E = mpc_engine();

    // ---- A: loud input, Wow 0 ----
    {
        void *p = E->create(nullptr);
        std::vector<int16_t> out;
        int diff = 0;
        for (int s = 0; s < kNumPresets; ++s) {
            set(p, "style", s);
            out.clear();
            long long base = 0;
            std::vector<int16_t> in;
            for (int k = 0; k < 44100; k += 128) {
                int16_t ib[256], ob[256];
                for (int i = 0; i < 128; ++i) { ib[2 * i] = sq(base + i, 0); ib[2 * i + 1] = sq(base + i + 25, 1); }
                E->process(p, ib, ob, 128);
                for (int i = 0; i < 256; ++i) diff += ob[i] != ib[i];
                base += 128;
            }
        }
        CHECK(diff == 0, "A  0 dBFS square at Wow 0, every preset: bit-exact (%d samples differ)", diff);
        E->destroy(p);
    }

    // ---- B: back to exact passthrough after a build-up ----
    for (int s = 0; s < kNumPresets; ++s) {
        void *p = E->create(nullptr);
        set(p, "style", s);
        set(p, "wow", 100);
        run(p, kFs * 4, sine);
        set(p, "wow", 0);
        // find when output == input for good
        long long base = 0; int lastDiff = -1;
        for (int k = 0; k < kFs * 20; k += 128) {
            int16_t ib[256], ob[256];
            for (int i = 0; i < 128; ++i) { ib[2 * i] = sine(base + i, 0); ib[2 * i + 1] = sine(base + i, 1); }
            E->process(p, ib, ob, 128);
            for (int i = 0; i < 256; ++i) if (ob[i] != ib[i]) lastDiff = k;
            base += 128;
        }
        CHECK(lastDiff < kFs * 15, "B  %-12s exact passthrough again %.1f s after Wow -> 0", kPresets[s].name,
              lastDiff < 0 ? 0.0 : (lastDiff + 128.0) / kFs);
        E->destroy(p);
    }

    // ---- C: worst case, 60 s per preset ----
    for (int s = 0; s < kNumPresets; ++s) {
        void *p = E->create(nullptr);
        set(p, "style", s); set(p, "wow", 100); set(p, "mix", 100);
        Run a = run(p, kFs * 30, noise), b = run(p, kFs * 30, sq);
        Run h = run(p, kFs * 10, [](long long i, int ch) { return (int16_t)(noise(i, ch) / 2); });   // -6 dBFS noise
        Run c = run(p, kFs * 2, sine);   // and still sane on normal material afterwards
        // full-scale input: never louder than the input itself (32767); quieter input: never past -0.4 dBFS
        CHECK(a.peak <= 32768 && b.peak <= 32768 && h.peak <= 31294 && c.peak <= 31294 && c.energy > 1e5,
              "C  %-12s 60 s full-scale noise + square (peaks %d / %d), -6 dBFS noise (peak %d <= 31294), then a sine (peak %d, rms %.0f)",
              kPresets[s].name, a.peak, b.peak, h.peak, c.peak, sqrt(c.energy));
        E->destroy(p);
    }

    // ---- D: denormals after silence ----
    {
        size_t worst = 0;
        for (int s = 0; s < kNumPresets; ++s)
            for (double w : {0.0, 30.0, 100.0}) {
                void *p = E->create(nullptr);
                set(p, "style", s); set(p, "wow", 100);
                run(p, kFs * 3, sine);
                set(p, "wow", w);
                run(p, kFs * 30, zero);
                size_t n = subnormals(*static_cast<WowDSP *>(p));   // the DSP is the engine object's first member
                worst = std::max(worst, n);
                if (n) printf("     %s at Wow %.0f: %zu subnormals\n", kPresets[s].name, w, n);
                E->destroy(p);
            }
        CHECK(worst == 0, "D  30 s of silence after a build-up: subnormal floats left in the state: %zu", worst);
    }

    // ---- E: hostile parameter text and states ----
    {
        const char *vals[] = {"nan", "NaN", "inf", "-inf", "1e999", "-1e999", "", "abc", "1e-50", "-5", "100000",
                              "50abc", "  42", "0x10", "-nan"};
        const char *keys[] = {"wow", "mix", "style", "style_prev", "style_next", "state", "unknown", "wow_display"};
        const char *states[] = {"PWW1;", "PWW1;wow=nan;mix=inf;style=-3", "PWW1;wow=50;mix=50;style=99999999999",
                                "PWW1;wow", "PWW1;;;;;", "garbage", "", "PWW1;style=nan", "PWW1;wow=1e999;style=-1e999"};
        void *p = E->create(nullptr);
        bool sane = true;
        char buf[256];
        for (const char *k : keys)
            for (const char *v : vals) {
                E->set_param(p, k, v);
                Run r = run(p, 2048, sine);
                if (r.energy != r.energy || r.peak > 32767) sane = false;
                for (const char *g : {"wow", "mix", "style", "wow_display", "mix_display", "state"}) {
                    int n = E->get_param(p, g, buf, sizeof buf);
                    if (n <= 0 || strstr(buf, "nan") || strstr(buf, "inf")) { sane = false; printf("     %s=%s -> %s=\"%s\"\n", k, v, g, buf); }
                }
            }
        for (const char *st : states) {
            E->set_param(p, "state", st);
            Run r = run(p, 4096, sine);
            if (r.energy != r.energy) sane = false;
            for (const char *g : {"wow", "mix", "style", "state"}) {
                int n = E->get_param(p, g, buf, sizeof buf);
                if (n <= 0 || strstr(buf, "nan") || strstr(buf, "inf")) { sane = false; printf("     state \"%s\" -> %s=\"%s\"\n", st, g, buf); }
            }
        }
        // style must still be a valid index after all that
        E->get_param(p, "style", buf, sizeof buf);
        int sty = atoi(buf);
        CHECK(sane && sty >= 0 && sty < kNumPresets, "E  hostile parameter text and broken states: values and audio stay sane (style %d)", sty);
        // still makes sound after recovery
        set(p, "wow", 60); set(p, "mix", 100); set(p, "style", S("Bouncy Smile"));
        Run r = run(p, kFs, sine);
        CHECK(r.energy > 1e5, "E  ... and still works afterwards (rms %.0f)", sqrt(r.energy));
        // a tiny buffer for get_param must not overflow (ASan watches)
        char tiny[4];
        E->get_param(p, "state", tiny, sizeof tiny);
        E->get_param(p, "wow_display", tiny, sizeof tiny);
        E->get_param(p, "state", tiny, 1);
        CHECK(true, "E  get_param into 1- and 4-byte buffers: no overflow");
        E->destroy(p);
    }

    // ---- F: fuzz ----
    {
        void *p = E->create(nullptr);
        double ppq = 0, bpm = 120;
        bool sane = true;
        int maxPeak = 0;
        std::vector<int16_t> in(1024), out(1024);
        for (int b = 0; b < 20000; ++b) {
            int r = rnd() % 16;
            if (r == 0) set(p, "wow", frand() * 120 - 10);
            else if (r == 1) set(p, "mix", frand() * 120 - 10);
            else if (r == 2) set(p, "style", (int)(rnd() % (kNumPresets + 2)) - 1);
            else if (r == 3) E->set_param(p, rnd() % 2 ? "style_prev" : "style_next", rnd() % 2 ? "1" : "0");
            else if (r == 4) bpm = 10 + frand() * 500;
            else if (r == 5) ppq = frand() * 1000 - 50;
            else if (r == 6) { char st[256]; E->get_param(p, "state", st, sizeof st); E->set_param(p, "state", st); }
            else if (r == 7) { static const char *gk[] = {"g_level", "g_size", "g_density", "g_speed", "g_split", "g_retrace", "g_rettime", "g_retlen", "g_var", "g_amount"};
                               set(p, gk[rnd() % 10], rnd() % 5 == 0 ? (rnd() % 2 ? 0 : 100) : frand() * 130 - 15); }   // 1.2: the GRAINS knobs, ends included
            else if (r == 8) set(p, rnd() % 2 ? "g_sync" : "g_retmode", rnd() % 2);
            else if (r == 9) { char qk[8]; snprintf(qk, sizeof qk, "q%d", 1 + (int)(rnd() % 12)); set(p, qk, frand() * 120 - 10); }   // Q-Link slots on either page
            else if (r == 10) set(p, rnd() % 3 ? "page" : (rnd() % 2 ? "tab1" : "tab2"), rnd() % 3);
            else if (r == 11) set(p, rnd() % 2 ? "help_btn" : "help_next", rnd() % 2);
            else if (r == 12) { char bk[12]; static const char *const fk[] = {"cat_prev", "cat_next", "cat", "style_list", "bpage_next", "bpage_prev", "browsing", "bview", "pre_hl"};
                               if (rnd() % 3) set(p, fk[rnd() % 9], rnd() % 2); else { snprintf(bk, sizeof bk, "%s%d", rnd() % 2 ? "pick" : "cat", 1 + (int)(rnd() % 14)); set(p, bk, rnd() % 2); } }   // 1.3: the preset browser   // the ? window
            int m = 1 + rnd() % 512;
            int kind = (b / 500) % 4;
            for (int i = 0; i < 2 * m; ++i)
                in[i] = kind == 0 ? (int16_t)(rnd() >> 16) : kind == 1 ? (int16_t)(i % 100 < 50 ? 32767 : -32768)
                      : kind == 2 ? 0 : (int16_t)lrint(15000 * sin(i * 0.01));
            int playing = (b / 300) % 2;
            if (rnd() % 3) E->song_pos(p, rnd() % 7 ? ppq : -1.0, rnd() % 9 ? bpm : 0.0, playing, m);
            E->process(p, in.data(), out.data(), m);
            ppq += m * bpm / 60.0 / kFs;
            for (int i = 0; i < 2 * m; ++i) maxPeak = std::max(maxPeak, abs((int)out[i]));
        }
        char buf[256];
        for (const char *g : {"wow", "mix", "style", "g_level", "g_size", "g_density", "g_speed", "g_split", "g_retrace", "g_rettime", "g_retlen", "g_var", "g_amount", "cat", "q1", "q5"}) { E->get_param(p, g, buf, sizeof buf); if (strstr(buf, "nan")) sane = false; }
        set(p, "wow", 50); set(p, "mix", 100);
        Run r = run(p, kFs, sine);
        CHECK(sane && r.energy > 1e5, "F  20000 fuzzed blocks (sizes 1-512, random params, tempo 10-510, song jumps): sane, works after (peak %d)", maxPeak);
        E->destroy(p);
    }

    // ---- G: clicks ----
    {
        auto worstStep = [](const std::vector<int16_t> &o, size_t from, size_t to) {
            int w = 0;
            for (size_t i = from + 2; i < to && i < o.size(); i += 2) w = std::max(w, abs(o[i] - o[i - 2]));
            return w;
        };
        // a 220 Hz sine at 20000 moves at most 2*pi*220/44100*20000 = 627 per sample
        struct Case { const char *what; int style; double w0, w1, m0, m1, bpm0, bpm1; int s1; };
        Case cases[] = {
            {"Wow 0 -> 100 instantly (Hard Grin)", S("Hard Grin"), 0, 100, 100, 100, 128, 128, S("Hard Grin")},
            {"Wow 100 -> 0 instantly (the drop, Hard Grin)", S("Hard Grin"), 100, 0, 100, 100, 128, 128, S("Hard Grin")},
            {"Wow 0 -> 100 instantly (Cloud Nine)", S("Cloud Nine"), 0, 100, 100, 100, 128, 128, S("Cloud Nine")},
            {"Wow 100 -> 0 instantly (the drop, Halo)", S("Halo"), 100, 0, 100, 100, 128, 128, S("Halo")},
            {"Mix 0 -> 100 at Wow 100 (Face Melter)", S("Face Melter"), 100, 100, 0, 100, 128, 128, S("Face Melter")},
            {"preset Heaven Lift -> Face Melter at Wow 100", S("Heaven Lift"), 100, 100, 100, 100, 128, 128, S("Face Melter")},
            {"preset Face Melter -> Heaven Lift at Wow 100", S("Face Melter"), 100, 100, 100, 100, 128, 128, S("Heaven Lift")},
            {"preset Grain Storm -> Tape Rise at Wow 100", S("Grain Storm"), 100, 100, 100, 100, 128, 128, S("Tape Rise")},
            {"tempo 60 -> 180 bpm at Wow 100 (Barber Pole)", S("Barber Pole"), 100, 100, 100, 100, 60, 180, S("Barber Pole")},
            {"tempo 60 -> 180 bpm at Wow 100 (Rising Echoes)", S("Rising Echoes"), 100, 100, 100, 100, 60, 180, S("Rising Echoes")},
        };
        auto steady = [&](int style, double w, double m, double bpm) {   // worst step once a setting has settled
            void *q = E->create(nullptr);
            set(q, "style", style); set(q, "wow", w); set(q, "mix", m);
            std::vector<int16_t> o;
            double ppq = 0;
            run(q, kFs * 4, sine, &o, 128, true, bpm, &ppq);
            E->destroy(q);
            return worstStep(o, o.size() - 2 * kFs * 2, o.size());
        };
        for (auto &c : cases) {
            void *p = E->create(nullptr);
            set(p, "style", c.style); set(p, "wow", c.w0); set(p, "mix", c.m0);
            std::vector<int16_t> o;
            double ppq = 0;
            run(p, kFs * 3, sine, &o, 128, true, c.bpm0, &ppq);
            size_t at = o.size();
            set(p, "style", c.s1); set(p, "wow", c.w1); set(p, "mix", c.m1);
            run(p, kFs, sine, &o, 128, true, c.bpm1, &ppq);
            int after = worstStep(o, at, at + 2 * 4410);   // the first 100 ms after the jump
            int calm = std::max(steady(c.style, c.w0, c.m0, c.bpm0), steady(c.s1, c.w1, c.m1, c.bpm1));
            // a click = a step well beyond anything either setting does on its own
            CHECK(after <= calm * 13 / 10 + 300, "G  %-46s worst step %5d (settled: up to %5d)", c.what, after, calm);
            E->destroy(p);
        }
    }

    // ---- J: the safety net: poison the state with NaN, the effect must recover on its own ----
    {
        WowDSP *d = new WowDSP();
        d->init(); d->setStyle(S("Endless Wow")); d->setWow(1); d->setMix(1);
        float a[128], b[128], oL[128], oR[128];
        for (int i = 0; i < 128; ++i) a[i] = b[i] = 0.5f * sinf(i * 0.05f);
        for (int k = 0; k < 100; ++k) d->process(a, b, oL, oR, 128);
        // reach in through the bytes: overwrite every float in the object's first 4 KB with NaN (filter states etc.)
        float qnan; uint32_t nb = 0x7fc00000u; memcpy(&qnan, &nb, 4);
        unsigned char *raw = (unsigned char *)d;
        size_t poisonFrom = offsetof(WowDSP, resets) + sizeof(unsigned) + 64;   // keep the style pointer etc. intact
        for (size_t o = poisonFrom; o + 4 <= poisonFrom + 4096 && o + 4 <= sizeof(WowDSP); o += 4) memcpy(raw + o, &qnan, 4);
        bool finite = true;
        double e = 0;
        for (int k = 0; k < 400; ++k) {
            d->process(a, b, oL, oR, 128);
            for (int i = 0; i < 128; ++i) {
                if (!(oL[i] == oL[i]) || !(oR[i] == oR[i])) finite = false;
                if (k > 300) e += oL[i] * oL[i];
            }
        }
        CHECK(finite && d->resets >= 1 && e > 1.0, "J  NaN poured into the state: no NaN out, the effect restarts itself (%u restart%s), sound continues",
              d->resets, d->resets == 1 ? "" : "s");
        delete d;
    }

    // ---- H: instances ----
    {
        void *a = E->create(nullptr), *b = E->create(nullptr);
        set(a, "style", S("Endless Wow")); set(a, "wow", 100);
        std::vector<int16_t> oa, ob;
        run(a, kFs, sine, &oa);
        run(b, kFs, sine, &ob);
        int diff = 0;
        // b ran later on the generator's clock than a fresh c will, so compare energies: b (Wow 0) must be the plain sine
        void *c = E->create(nullptr);
        std::vector<int16_t> oc;
        run(c, kFs, sine, &oc);   // Wow 0
        // b and c both at Wow 0 over different time offsets of a sine: both must equal their input; check b is untouched
        // by a: b's energy must equal the plain sine's
        double eb = 0, ec = 0;
        for (int16_t v : ob) eb += (double)v * v;
        for (int16_t v : oc) ec += (double)v * v;
        diff = fabs(eb - ec) / ec > 0.01;
        char sa[64], sb[64];
        E->get_param(a, "style", sa, sizeof sa); E->get_param(b, "style", sb, sizeof sb);
        CHECK(!diff && atoi(sa) == S("Endless Wow") && atoi(sb) == 0, "H  two instances keep their own settings and sound");
        E->destroy(a); E->destroy(b); E->destroy(c);
    }

    // ---- I: speed, every preset at Wow 100 ----
    {
        double worst = 0; int worstS = 0;
        for (int s = 0; s < kNumPresets; ++s) {
            void *p = E->create(nullptr);
            set(p, "style", s); set(p, "wow", 100);
            std::vector<int16_t> in(256), out(256);
            for (int i = 0; i < 256; ++i) in[i] = (int16_t)(rnd() >> 17);
            const int blocks = 44100 * 5 / 128;
            auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b) E->process(p, in.data(), out.data(), 128);
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (sec > worst) { worst = sec; worstS = s; }
            E->destroy(p);
        }
        printf("info I  heaviest preset %s at Wow 100: %.1f%% of one core here (this build's flags)\n", kPresets[worstS].name, worst / 5 * 100);
    }

    // ---- K: pump speed at a boundary: Wow jittering around it must not flip between two speeds ----
    {
        WowDSP *d = new WowDSP();
        d->init(); d->setStyle(S("Bouncy Smile"));   // Bouncy Smile: 1 -> 1/8 beat over the turn, boundaries at w = 1/6, 1/2, 5/6
        float a[128], b[128], oL[128], oR[128];
        for (int i = 0; i < 128; ++i) a[i] = b[i] = 0.3f;
        int flips = 0;
        float last = -99;
        for (int k = 0; k < 2000; ++k) {
            d->setWow(0.5f + ((k & 1) ? 0.004f : -0.004f));   // +-0.4 % around the 1/8 <-> 1/16 boundary
            d->process(a, b, oL, oR, 128);
            if (k > 50 && d->pumpExp != last) ++flips;
            last = d->pumpExp;
        }
        CHECK(flips == 0, "K  Wow jittering on a pump-speed boundary: the pump speed changed %d times", flips);
        delete d;
    }

    printf(fails ? "stress_test: %d FAILED\n" : "stress_test: PASSED\n", fails);
    return fails ? 1 : 0;
}
