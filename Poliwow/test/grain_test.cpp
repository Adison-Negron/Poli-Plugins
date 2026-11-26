// Poliwow 1.2 grain-room tests (WowDSP directly, no -ffast-math, sanitizers on when run_tests.sh builds it):
//   L  every preset x extreme GRAINS settings at Wow 100: finite, bounded, no runaway
//   M  Wow 0: the GRAINS controls change nothing (bit-exact through the engine)
//   N  split: grains of generation 1 appear only when Split > 0 (and there is room motion), never more than the pool
//   O  retrace: retraced pieces appear only when Retrace > 0; Jump plays forwards, Reverse plays backwards
//   P  sync: with Sync on and Variation 0, grains are born on the tempo grid; Variation moves them off it; Sync off: free
//   Q  speed: Speed 0 = grains don't move; Speed 100 = they do and stay in the room (0..1)
//   R  Level: adds a cloud to a preset without one; 0 = the preset's own
//   S  CPU: worst preset with everything on, as a share of one core here
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <vector>
#include <algorithm>
extern "C" {
#include "engine.h"
}
#define private public
#include "WowDSP.h"
#undef private

static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++fails; } while (0)
static const int kFs = 44100, kB = 128;
static uint32_t rs = 7;
static float frand() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs / 4294967296.0f; }

static int styleOf(const char *n) { for (int i = 0; i < kNumStyles; ++i) if (!strcmp(kStyles[i].name, n)) return i; return 0; }

struct Rig {
    WowDSP *d = new WowDSP();
    long long t = 0;
    double ppq = 0, bpm = 120;
    bool song = true;
    float inL[kB], inR[kB], oL[kB], oR[kB];
    double energy = 0; float peak = 0; bool bad = false;
    Rig(int style, float wow) { d->init(); d->setStyle(style); d->setWow(wow); d->setMix(1); }
    ~Rig() { delete d; }
    void block(bool noise = true) {
        for (int i = 0; i < kB; ++i) {
            const float s = noise ? 0.25f * (frand() * 2 - 1) + 0.3f * sinf((float)(t + i) * 0.05f) : 0.0f;
            inL[i] = s; inR[i] = 0.8f * s;
        }
        if (song) { d->setSongPos(ppq, bpm, true); ppq += kB * bpm / 60.0 / kFs; }
        d->process(inL, inR, oL, oR, kB);
        for (int i = 0; i < kB; ++i) {
            const float a = fabsf(oL[i]), b = fabsf(oR[i]);
            if (!(a < 1e6f) || !(b < 1e6f)) bad = true;
            peak = std::max(peak, std::max(a, b));
            energy += (double)oL[i] * oL[i] + (double)oR[i] * oR[i];
        }
        t += kB;
    }
    void run(double sec) { const int n = (int)(sec * kFs / kB); for (int k = 0; k < n; ++k) block(); }
};

static GrainCtl ctl(float level, float size, float dens, float speed, float split, float ret, float rt, float rl, float var, bool rev, bool sync) {
    GrainCtl c; c.level = 0.5f; c.amount = level; c.size = size; c.density = dens; c.speed = speed; c.split = split; c.retrace = ret; c.retTime = rt;
    c.retLen = rl; c.variation = var; c.retReverse = rev; c.sync = sync; return c;
}

// births of grains matching `gen` (-1 = any): sample positions, from ages at the end of each block
static std::vector<long long> births(Rig &r, double sec, int gen) {
    std::vector<long long> v;
    const int n = (int)(sec * kFs / kB);
    for (int k = 0; k < n; ++k) {
        r.block();
        for (int g = 0; g < WowDSP::kMaxGrains; ++g) {
            const WowDSP::Grain &gr = r.d->grains[g];
            if (gr.on && gr.age < kB && (gen < 0 || gr.gen == gen)) v.push_back(r.t - gr.age);
        }
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

int main() {
    // ---- L: extremes, every preset ----
    {
        const GrainCtl combos[] = {
            ctl(1, 1, 1, 1, 1, 1, 0, 0, 1, false, false), ctl(1, 0, 1, 1, 1, 1, 1, 1, 1, true, true), ctl(1, 1, 0, 1, 1, 1, 0.5f, 1, 0, true, false),
            ctl(1, 0, 0, 0, 1, 1, 1, 0, 0.5f, false, true), ctl(0, 1, 1, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, false, true), ctl(1, 0.5f, 1, 1, 1, 0, 0, 0, 1, false, true)};
        bool allOk = true; float worst = 0; int worstS = 0, worstC = 0;
        for (int s = 0; s < kNumStyles; ++s)
            for (int c = 0; c < 6; ++c) {
                Rig r(s, 1.0f);
                r.d->setGrainCtl(combos[c]);
                r.run(6);
                if (r.bad) { allOk = false; printf("   bad: %s combo %d\n", kStyles[s].name, c); }
                if (r.peak > worst) { worst = r.peak; worstS = s; worstC = c; }
            }
        CHECK(allOk && worst < 2.0f, "L  21 base sounds x 6 extreme GRAINS settings, Wow 100: finite and bounded (worst peak %.2f: %s, combo %d)", worst, kStyles[worstS].name, worstC);
    }
    // ---- M: Wow 0 untouched ----
    {
        const mpc_engine_t *E = mpc_engine();
        void *p = E->create(nullptr);
        const char *k[] = {"g_level", "g_size", "g_density", "g_speed", "g_split", "g_retrace", "g_rettime", "g_retlen", "g_var", "g_retmode", "g_sync"};
        for (const char *key : k) E->set_param(p, key, "100");
        E->set_param(p, "wow", "0");
        long diff = 0;
        int16_t in[256], out[256];
        for (int b = 0; b < 400; ++b) {
            for (int i = 0; i < 256; ++i) in[i] = (int16_t)((int)(frand() * 65535) - 32768);
            E->process(p, in, out, 128);
            for (int i = 0; i < 256; ++i) diff += in[i] != out[i];
        }
        CHECK(diff == 0, "M  Wow 0 with every GRAINS knob at 100: audio bit-exact (%ld samples differ)", diff);
        E->destroy(p);
    }
    // ---- N: split ----
    {
        Rig off(styleOf("Grain Storm"), 1.0f); off.d->setGrainCtl(ctl(0, 0.5f, 0.8f, 1, 0, 0, .5f, .5f, 0, false, false));
        Rig on(styleOf("Grain Storm"), 1.0f);  on.d->setGrainCtl(ctl(0, 0.5f, 0.8f, 1, 1, 0, .5f, .5f, 0, false, false));
        off.run(1); on.run(1);
        const size_t a = births(off, 10, 1).size(), b = births(on, 10, 1).size();
        CHECK(a == 0 && b > 5, "N  Split 0: no children (%zu); Split 100 at Speed 100: children appear (%zu in 10 s)", a, b);
        Rig still(styleOf("Grain Storm"), 1.0f); still.d->setGrainCtl(ctl(0, 0.5f, 0.8f, 0, 1, 0, .5f, .5f, 0, false, false));
        still.run(1);
        const size_t c = births(still, 10, 1).size();
        CHECK(c > 0, "N  Split 100 at Speed 0: grains still drift slowly and can split (%zu in 10 s)", c);
        // children of children are the last generation: nothing above gen 2 ever exists
        int maxGen = 0;
        for (int k = 0; k < 10 * kFs / kB; ++k) { on.block(); for (int g = 0; g < WowDSP::kMaxGrains; ++g) if (on.d->grains[g].on) maxGen = std::max(maxGen, (int)on.d->grains[g].gen); }
        CHECK(maxGen <= 2, "N  splitting stops after two generations (max generation %d)", maxGen);
    }
    // ---- O: retrace ----
    {
        Rig off(styleOf("Grain Storm"), 1.0f); off.d->setGrainCtl(ctl(0, 0.5f, 0.8f, 0, 0, 0, .5f, .5f, 0, false, false));
        off.run(1);
        const size_t n0 = births(off, 8, 2).size();
        Rig jump(styleOf("Grain Storm"), 1.0f); jump.d->setGrainCtl(ctl(0, 0.5f, 0.8f, 0, 0, 1, .5f, .5f, 0, false, false));
        Rig rev(styleOf("Grain Storm"), 1.0f);  rev.d->setGrainCtl(ctl(0, 0.5f, 0.8f, 0, 0, 1, .5f, .5f, 0, true, false));
        jump.run(1); rev.run(1);
        int fwd = 0, back = 0, pieces = 0;
        for (int k = 0; k < 8 * kFs / kB; ++k) {
            jump.block(); rev.block();
            for (int g = 0; g < WowDSP::kMaxGrains; ++g) {
                const WowDSP::Grain &a = jump.d->grains[g], &b = rev.d->grains[g];
                if (a.on && a.gen == 2) { ++pieces; fwd += a.inc > 0; }
                if (b.on && b.gen == 2) { ++pieces; back += b.inc < 0; }
            }
        }
        CHECK(n0 == 0 && pieces > 0, "O  Retrace 0: no retraced pieces (%zu); Retrace 100: they appear (%d grain-blocks)", n0, pieces);
        CHECK(fwd > 0 && back > 0, "O  Jump mode plays forwards (%d), Reverse plays backwards (%d)", fwd, back);
    }
    // ---- P: sync grid ----
    {
        auto spread = [](float var, bool sync, double bpm) {
            Rig r(styleOf("Grain Storm"), 1.0f); r.bpm = bpm;
            r.d->setGrainCtl(ctl(0, 0.5f, 0.5f, 0, 0, 0, .5f, .5f, var, false, sync));
            r.run(1);
            std::vector<long long> v = births(r, 20, 0);
            // phase of each birth against the grid the engine chose (syncN slots per beat)
            const double beat = kFs * 60.0 / bpm, n = r.d->syncN > 0 ? r.d->syncN : 8.0;
            std::vector<double> ph;
            for (long long b : v) ph.push_back(fmod((double)b / beat * n, 1.0));
            double mx = 0, my = 0;
            for (double p : ph) { mx += cos(2 * M_PI * p); my += sin(2 * M_PI * p); }
            return ph.empty() ? -1.0 : sqrt(mx * mx + my * my) / ph.size();   // 1 = all on the grid, ~0 = scattered
        };
        const double lock = spread(0, true, 120), lock2 = spread(0, true, 93), loose = spread(1, true, 120), freeRun = spread(0, false, 120);
        CHECK(lock > 0.95 && lock2 > 0.95, "P  Sync on, Variation 0: grains are born on the tempo grid (lock %.3f at 120 bpm, %.3f at 93 bpm)", lock, lock2);
        CHECK(loose < lock - 0.2, "P  Sync on, Variation 100: they land off the grid (lock %.3f)", loose);
        CHECK(freeRun < 0.8, "P  Sync off: free-running (lock %.3f)", freeRun);
    }
    // ---- Q: speed ----
    {
        Rig still(styleOf("Grain Storm"), 1.0f); still.d->setGrainCtl(ctl(0, 0.5f, 0.5f, 0, 0, 0, .5f, .5f, 0, false, false));
        Rig fast(styleOf("Grain Storm"), 1.0f);  fast.d->setGrainCtl(ctl(0, 0.5f, 0.5f, 1, 0, 0, .5f, .5f, 0, false, false));
        still.run(1); fast.run(1);
        double moveStill = 0, moveFast = 0; bool inRoom = true;
        float lx[WowDSP::kMaxGrains] = {0}, fx[WowDSP::kMaxGrains] = {0};
        int la[WowDSP::kMaxGrains] = {0}, fa[WowDSP::kMaxGrains] = {0};   // age last block: the same grain if it grew by one block
        for (int k = 0; k < 6 * kFs / kB; ++k) {
            still.block(); fast.block();
            for (int g = 0; g < WowDSP::kMaxGrains; ++g) {
                const auto &a = still.d->grains[g], &b = fast.d->grains[g];
                if (a.on && a.age == la[g] + kB) moveStill += fabs(a.x - lx[g]);
                if (b.on && b.age == fa[g] + kB) moveFast += fabs(b.x - fx[g]);
                if (b.on && (b.x < -0.001f || b.x > 1.001f)) inRoom = false;
                lx[g] = a.x; fx[g] = b.x; la[g] = a.on ? a.age : -999999; fa[g] = b.on ? b.age : -999999;
            }
        }
        CHECK(moveStill < 1e-6 && moveFast > 0.05 && inRoom, "Q  Speed 0: grains stay put (%.4f); Speed 100: they move (%.3f) and stay inside the room (%s)", moveStill, moveFast, inRoom ? "yes" : "NO");
    }
    // ---- R: level ----
    {
        Rig a(styleOf("Heaven Lift"), 1.0f), b(styleOf("Heaven Lift"), 1.0f);
        a.d->setGrainCtl(ctl(0, .5f, .5f, 0, 0, 0, .5f, .5f, 0, false, false));
        b.d->setGrainCtl(ctl(1, .5f, .5f, 0, 0, 0, .5f, .5f, 0, false, false));
        a.run(1); b.run(1);
        const size_t na = births(a, 6, -1).size(), nb = births(b, 6, -1).size();
        CHECK(nb > na, "R  Amount 100 adds grains to a preset (%zu -> %zu births in 6 s)", na, nb);
    }
    // ---- S: CPU ----
    {
        double worst = 0; int ws = 0;
        for (int s = 0; s < kNumStyles; ++s) {
            Rig r(s, 1.0f);
            r.d->setGrainCtl(ctl(1, 0.5f, 1, 1, 1, 1, .5f, .5f, 0.5f, false, true));
            r.run(1);
            auto t0 = std::chrono::steady_clock::now();
            r.run(5);
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (sec > worst) { worst = sec; ws = s; }
        }
        printf("info S  heaviest preset with every GRAINS control on: %s, %.1f%% of one core here (this build's flags)\n", kStyles[ws].name, worst / 5 * 100);
    }
    printf(fails ? "grain_test: %d FAILED\n" : "grain_test: PASSED\n", fails);
    return fails ? 1 : 0;
}
