// Poliwow offline checks: the engine alone (x86, run under sanitizers by test/run_tests.sh).
//   1. Wow 0: output == input, sample for sample
//   2. every preset, Wow swept 0 -> 100 over a drum loop: no NaN, no clipping past the limiter, gets louder in the
//      top end and wider, the pump ducks on the beat
//   3. Wow snapped back to 0 (the drop): the reverb/delay tail keeps ringing
//   4. silence in, Wow 100: silence out (the risers are gated by the input)
//   5. state save / load, preset arrows wrap, "%" value text
// With an output folder (dsp_test <dir>) it also writes one 16-bar build-up WAV per preset to listen to.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
extern "C" {
#include "engine.h"
int mpc_engine_changed(void *inst, char *key, int len);
}
#include "WowDSP.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const double kBpm = 128.0;
static const int kFs = 44100;

// a 4-on-the-floor loop: kick, offbeat hat, a chord pad
static void makeLoop(std::vector<int16_t> &buf, int frames) {
    buf.assign(frames * 2, 0);
    uint32_t r = 1;
    const double spb = 60.0 / kBpm * kFs;
    for (int i = 0; i < frames; ++i) {
        double beat = i / spb, ph = beat - floor(beat);
        double tb = ph * spb / kFs;   // seconds since the beat
        double kick = sin(2 * M_PI * (50 + 120 * exp(-tb * 30)) * tb) * exp(-tb * 7) * 0.55;
        double ho = fmod(beat + 0.5, 1.0) * spb / kFs;
        r = r * 1664525u + 1013904223u;
        double hat = ((int32_t)r / 2147483648.0) * exp(-ho * 60) * 0.12;
        double t = (double)i / kFs, pad = 0;
        const double notes[3] = {220.0, 277.18, 329.63};
        for (double f : notes) pad += sin(2 * M_PI * f * t) * 0.06 + sin(2 * M_PI * f * 2.003 * t) * 0.02;
        double l = kick + hat + pad, rr = kick + hat * 0.8 + pad * 0.9;
        buf[2 * i] = (int16_t)lrint(fmax(-32768, fmin(32767, l * 32767)));
        buf[2 * i + 1] = (int16_t)lrint(fmax(-32768, fmin(32767, rr * 32767)));
    }
}

static void writeWav(const std::string &path, const std::vector<int16_t> &s) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t data = (uint32_t)(s.size() * 2), riff = 36 + data, sr = kFs, br = kFs * 4, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s.data(), 2, s.size(), f);
    fclose(f);
}

struct Stats { double rms = 0, hi = 0, mid = 0, side = 0; int peak = 0; bool nan = false; };

// one-pole high-pass at ~3 kHz for a "top end" measure
static Stats stats(const int16_t *s, int frames) {
    Stats st;
    double hpL = 0, prevL = 0, a = exp(-2 * M_PI * 3000.0 / kFs), sum = 0, hsum = 0, ssum = 0;
    double m1 = 0, m2 = 0, mp = 0, am = exp(-2 * M_PI * 300.0 / kFs), msum = 0;   // 2nd-order-ish high-pass at 300 Hz
    for (int i = 0; i < frames; ++i) {
        double l = s[2 * i] / 32768.0, r = s[2 * i + 1] / 32768.0;
        if (std::isnan(l) || std::isnan(r)) st.nan = true;
        st.peak = std::max(st.peak, std::max(abs(s[2 * i]), abs(s[2 * i + 1])));
        hpL = a * (hpL + l - prevL);
        m1 = am * (m1 + l - mp); mp = l;
        msum += m1 * m1;
        prevL = l;
        sum += l * l + r * r; hsum += hpL * hpL; ssum += (l - r) * (l - r) * 0.25;
    }
    st.rms = sqrt(sum / (2.0 * frames)); st.hi = sqrt(hsum / frames); st.mid = sqrt(msum / frames); st.side = sqrt(ssum / frames);
    return st;
}

static void set(const mpc_engine_t *e, void *in, const char *k, double v) {
    char b[32]; snprintf(b, sizeof b, "%g", v); e->set_param(in, k, b);
}

int main(int argc, char **argv) {
    const mpc_engine_t *e = mpc_engine();
    const char *outdir = argc > 1 ? argv[1] : nullptr;
    const int bars = 16, frames = (int)(bars * 4 * 60.0 / kBpm * kFs);
    std::vector<int16_t> in, out(frames * 2);
    makeLoop(in, frames);
    const double spb = 60.0 / kBpm * kFs;

    // 1. Wow 0 = bit-exact passthrough (after its first block, every style)
    for (int s = 0; s < kNumPresets; ++s) {
        void *p = e->create(nullptr);
        set(e, p, "style", s);
        int diff = 0;
        for (int i = 0; i < kFs * 2; i += 128) {
            e->song_pos(p, i / spb, kBpm, 1, 128);
            e->process(p, &in[2 * i], &out[2 * i], 128);
            for (int j = 0; j < 256; ++j) diff += out[2 * i + j] != in[2 * i + j];
        }
        CHECK(diff == 0, "style %d: Wow 0 changed %d samples", s, diff);
        e->destroy(p);
    }

    // 2 + 3. the build-up: Wow 0 -> 100 over 12 bars, hold 2, snap to 0 for the last 2 (the drop)
    const int upEnd = (int)(12 * 4 * spb), dropAt = (int)(14 * 4 * spb);
    for (int s = 0; s < kNumPresets; ++s) {
        void *p = e->create(nullptr);
        set(e, p, "style", s);
        for (int i = 0; i < frames; i += 128) {
            double w = i < upEnd ? 100.0 * i / upEnd : i < dropAt ? 100.0 : 0.0;
            set(e, p, "wow", w);
            e->song_pos(p, i / spb, kBpm, 1, 128);
            e->process(p, &in[2 * i], &out[2 * i], std::min(128, frames - i));
        }
        const int bar = (int)(4 * spb);
        Stats a = stats(&in[0], 2 * bar), b = stats(&out[0], 2 * bar);              // bars 1-2: Wow ~0
        Stats c = stats(&out[2 * (12 * bar)], 2 * bar), ci = stats(&in[2 * (12 * bar)], 2 * bar);   // bars 13-14: 100 %
        Stats all = stats(&out[0], frames);
        CHECK(!all.nan, "style %d: NaN", s);
        CHECK(all.peak <= 31300, "style %d: peak %d past the limiter", s, all.peak);
        CHECK(fabs(b.rms / a.rms - 1) < 0.25, "style %d: early bars level x%.2f", s, b.rms / a.rms);
        CHECK(c.hi / c.rms > 1.2 * ci.hi / ci.rms, "style %d: top end at 100%% only x%.2f", s, (c.hi / c.rms) / (ci.hi / ci.rms));
        // above the bass (the kick is meant to go): the build-up should keep its level or grow, not vanish or explode
        // (deep-pump presets sit lower on average: Bouncy Smile ducks 80 % of every 1/32)
        CHECK(c.mid > 0.6 * ci.mid && c.mid < 4.0 * ci.mid, "style %d: level above 300 hz at 100%% x%.2f", s, c.mid / ci.mid);
        CHECK(c.side > ci.side, "style %d: not wider (%.4f vs %.4f)", s, c.side, ci.side);
        // tail: the first 50 ms after the drop must still differ from the dry input (reverb/delay ringing)
        double tail = 0;
        for (int i = dropAt + 2205; i < dropAt + 4410; ++i) tail += fabs(out[2 * i] - (double)in[2 * i]);
        tail /= 2205.0 * 32768.0;
        CHECK(tail > 0.002, "style %d: no tail after the drop (%.5f)", s, tail);
        printf("%-13s early x%.2f | 100%%: level x%.2f (>300hz x%.2f), top x%.2f, side x%.1f | peak %d | tail %.4f\n",
               kPresets[s].name, b.rms / a.rms, c.rms / ci.rms, c.mid / ci.mid, (c.hi / c.rms) / (ci.hi / ci.rms), c.side / ci.side,
               all.peak, tail);
        if (outdir) {
            std::string name = kPresets[s].name;
            for (auto &ch : name) if (ch == ' ') ch = '-';
            char num[16]; snprintf(num, sizeof num, "%d-", s + 1);
            writeWav(std::string(outdir) + "/" + num + name + ".wav", out);
        }
        e->destroy(p);
    }
    if (outdir) writeWav(std::string(outdir) + "/0-dry-loop.wav", in);

    // 4. silence in, Wow 100: silence out (every preset; the long-decay and shimmer ones included)
    for (int s = 0; s < kNumPresets; ++s) {
        void *p = e->create(nullptr);
        set(e, p, "style", s);
        set(e, p, "wow", 100);
        std::vector<int16_t> z(256, 0), o(256);
        int nz = 0;
        for (int i = 0; i < kFs * 3; i += 128) { e->process(p, z.data(), o.data(), 128); for (int16_t v : o) nz += v != 0; }
        CHECK(nz == 0, "%s: silence in at 100%%: %d non-zero samples out", kPresets[s].name, nz);
        e->destroy(p);
    }

    // 5. state, arrows, text
    {
        void *p = e->create(nullptr);
        char buf[128], key[32];
        e->set_param(p, "style_prev", "1");
        CHECK(e->get_param(p, "style", buf, sizeof buf) > 0 && atoi(buf) == kNumPresets - 1, "prev from 0 should wrap to the last preset, got %s", buf);
        {   // a preset change reports the preset, its category, the Q-Link slot and the granulator knobs the preset moved
            bool gotStyle = false, gotCat = false, gotQ3 = false, gotAmount = false; int n = 0, dup = 0;
            char seen[96][32];
            while (mpc_engine_changed(p, key, sizeof key) == 1 && n < 96) {
                for (int i = 0; i < n; ++i) if (!strcmp(seen[i], key)) ++dup;
                snprintf(seen[n++], 32, "%s", key);
                gotStyle |= !strcmp(key, "style"); gotCat |= !strcmp(key, "cat"); gotQ3 |= !strcmp(key, "q3"); gotAmount |= !strcmp(key, "g_amount");
            }
            CHECK(gotStyle && gotCat && gotQ3 && gotAmount, "arrow reports style, cat, q3 and g_amount (%d %d %d %d)", gotStyle, gotCat, gotQ3, gotAmount);
            CHECK(dup == 0 && n < 96, "no key reported twice (%d keys, %d dup)", n, dup);
        }
        e->set_param(p, "style_next", "1");
        e->get_param(p, "style", buf, sizeof buf);
        CHECK(atoi(buf) == 0, "next from the last preset should wrap to 0, got %s", buf);
        set(e, p, "wow", 57.4); set(e, p, "mix", 100); set(e, p, "style", 4);
        e->get_param(p, "wow_display", buf, sizeof buf); CHECK(!strcmp(buf, "57%"), "wow text %s", buf);
        e->get_param(p, "mix_display", buf, sizeof buf); CHECK(!strcmp(buf, "100%"), "mix text %s", buf);
        set(e, p, "mix", 0);
        e->get_param(p, "mix_display", buf, sizeof buf); CHECK(!strcmp(buf, "0%"), "mix text %s", buf);
        e->get_param(p, "state", buf, sizeof buf);
        void *q = e->create(nullptr);
        e->set_param(q, "state", buf);
        char b2[128];
        e->get_param(q, "state", b2, sizeof b2);
        CHECK(!strcmp(buf, b2), "state %s -> %s", buf, b2);
        e->destroy(q);
        e->destroy(p);
    }
    printf(fails ? "dsp_test: %d FAILED\n" : "dsp_test: PASSED\n", fails);
    return fails ? 1 : 0;
}
