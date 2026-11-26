/* Shhhhh mock MPC host: links the real vst2_wrap.c the way MPC loads it and checks what the screen would see:
 * Learn turning itself off, nodes following Threshold and Learn, the spectrum moving without screen rebuilds,
 * Q-Link precision, Pass/Diff, save + reload through the chunk. Build: test/run_tests.sh */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
extern AEffect* VSTPluginMain(cb);
static int automated[NPARAMS], updates, fails;
static float autoval[NPARAMS];
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    (void)e; (void)v; (void)p;
    if (op == 0 && i >= 0 && i < NPARAMS) { automated[i]++; autoval[i] = o; }
    if (op == 42) updates++;
    if (op == 1) return 2400;
    return 0;
}
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static uint32_t rng = 1;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) * (1.0f / 16777216.0f)) * 2 - 1; }
static double ph = 0;
static void run(AEffect *a, int blocks, float noiseAmp, float toneAmp) {
    float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
    for (int k = 0; k < blocks; k++) {
        for (int n = 0; n < 128; n++) { I0[n] = I1[n] = noiseAmp * noise() + toneAmp * (float)sin(ph); ph += 6.283185307 * 1000 / 44100; }
        a->pr(a, in, o, 128);
    }
}
static void reset(void) { memset(automated, 0, sizeof automated); updates = 0; }
static const char *disp(AEffect *a, const char *k) { static char b[16][64]; static int n; char *r = b[n++ % 16]; r[0] = 0; a->d(a, 7, idx(k), 0, r, 0); return r; }
static float nodeDb(AEffect *a, int i) { char k[8]; snprintf(k, sizeof k, "n%d", i); return -120.0f + 120.0f * a->getP(a, idx(k)); }

int main(void) {
    AEffect *a = VSTPluginMain(host);
    run(a, 40, 0, 0);
    CHECK(!strcmp(disp(a, "threshold"), "+0.0 dB") && !strcmp(disp(a, "reduction"), "12 dB") && !strcmp(disp(a, "output"), "+0.0 dB"),
          "insert: Threshold \"%s\" (fader in the middle: %.3f), Reduction \"%s\", Output \"%s\"", disp(a, "threshold"),
          a->getP(a, idx("threshold")), disp(a, "reduction"), disp(a, "output"));
    CHECK(a->getP(a, idx("threshold")) == 0.5f, "Threshold 0 = the middle of its fader");
    a->setP(a, idx("reduction"), 1.0f);
    CHECK(!strcmp(disp(a, "reduction"), "100 dB"), "Reduction fader at the top reads \"%s\"", disp(a, "reduction"));
    a->setP(a, idx("reduction"), 0.0f);
    CHECK(!strcmp(disp(a, "reduction"), "0 dB"), "... and at the bottom \"%s\"", disp(a, "reduction"));
    a->setP(a, idx("reduction"), 0.12f);
    a->setP(a, idx("threshold"), 0.25f);
    CHECK(!strcmp(disp(a, "threshold"), "-15.0 dB"), "Threshold a quarter up reads \"%s\"", disp(a, "threshold"));
    a->setP(a, idx("threshold"), 0.5f);
    {   /* Output knob on a Q-Link: 10 events of 1/128 = 3.75 dB, text follows */
        float o = a->getP(a, idx("output"));
        for (int k = 0; k < 10; k++) { a->setP(a, idx("output"), o + 1.0f / 128); o = a->getP(a, idx("output")); }
        CHECK(fabsf((o - 0.5f) * 48 - 3.75f) < 0.01f && !strcmp(disp(a, "output"), "+3.8 dB"),
              "Output: 10 Q-Link events = +%.3f dB, the knob reads \"%s\"", (o - 0.5f) * 48, disp(a, "output"));
        a->setP(a, idx("output"), 0.5f);
    }
    CHECK(!strcmp(disp(a, "n3"), "") && fabsf(nodeDb(a, 3) + 60) < 0.01f, "nodes at -60 dB, no text under them");
    CHECK(a->getP(a, idx("n1x")) == 0.5f, "nodes at their default positions (P3)");

    /* tap Learn on 3.5 s of hiss (-50 dBFS) with a 3 kHz whine... here a 1 kHz whine */
    reset();
    a->setP(a, idx("learn"), 1.0f);
    run(a, 300, 0.005f, 0.0f);
    int progSeen = automated[idx("learn_prog")];
    run(a, 900, 0.005f, 0.0f);
    CHECK(progSeen > 0, "while learning: the progress bar is reported (%d reports in the first 0.9 s)", progSeen);
    CHECK(a->getP(a, idx("learn")) == 0.0f && automated[idx("learn")] > 0 && autoval[idx("learn")] == 0.0f,
          "Learn turned itself off after ~3 s and MPC was told (button back to grey)");
    CHECK(updates > 0, "one UpdateDisplay for the Learn switch (%d)", updates);
    CHECK(automated[idx("n1")] > 0 && automated[idx("n6")] > 0 && nodeDb(a, 3) < -70,
          "the nodes moved to the learned hiss and were reported (n3 %.1f dB)", nodeDb(a, 3));

    /* Threshold Q-Link: 10 events of +1/128; every node rises with it */
    float before[7];
    for (int i = 1; i <= 6; i++) before[i] = nodeDb(a, i);
    reset();
    float t = a->getP(a, idx("threshold"));
    for (int k = 0; k < 10; k++) { a->setP(a, idx("threshold"), t + 1.0f / 128); t = a->getP(a, idx("threshold")); run(a, 3, 0.005f, 0); }
    run(a, 40, 0.005f, 0);
    const float dthr = (a->getP(a, idx("threshold")) - 0.5f) * 60;
    float worst = 0;
    for (int i = 1; i <= 6; i++) worst = fmaxf(worst, fabsf(nodeDb(a, i) - before[i] - dthr));
    CHECK(fabsf(dthr - 10 * 60.0f / 128) < 0.01f, "10 Q-Link events = %.3f dB of Threshold (exactly 10 x 60/128)", dthr);
    CHECK(worst < 0.01f && automated[idx("n2")] > 0, "all six nodes followed by the same %.2f dB and were reported (worst %.3f)", dthr, worst);
    CHECK(fabsf(autoval[idx("n2")] - a->getP(a, idx("n2"))) < 1e-4f, "MPC's n2 (%.4f) = the engine's (%.4f)", autoval[idx("n2")], a->getP(a, idx("n2")));
    CHECK(updates == 0, "a Threshold turn rebuilds no screen");

    /* a node drag, fine moves */
    float n4 = a->getP(a, idx("n4"));
    a->setP(a, idx("n4"), n4 - 0.1f);
    CHECK(fabsf(a->getP(a, idx("n4")) - (n4 - 0.1f)) < 1e-4f, "drag node 4: it lands where MPC put it");
    float f = a->getP(a, idx("n4"));
    for (int k = 0; k < 5; k++) { a->setP(a, idx("n4"), f + 0.001f); f = a->getP(a, idx("n4")); }
    CHECK(fabsf(f - (n4 - 0.1f + 0.005f)) < 2e-4f, "5 fine moves of 1/1000 on node 4 land (%.4f)", f - (n4 - 0.1f));

    /* spectrum moves on its own, without UpdateDisplay; silence -> nothing reported after it settles */
    reset();
    run(a, 200, 0.005f, 0.3f);
    int specReports = 0;
    for (int j = 1; j <= 32; j++) { char k[8]; snprintf(k, sizeof k, "sp%02d", j); specReports += automated[idx(k)]; }
    CHECK(specReports > 0 && updates == 0, "spectrum preview: %d column reports in 0.6 s, 0 screen rebuilds (%d)", specReports, updates);
    CHECK(automated[idx("threshold")] == 0 && automated[idx("n1")] == 0, "a moving spectrum doesn't re-report the controls");
    run(a, 1200, 0, 0);
    reset();
    run(a, 400, 0, 0);
    int quiet = 0; for (int i = 0; i < NPARAMS; i++) quiet += automated[i];
    CHECK(quiet == 0, "silence (after the columns fell): nothing reported to MPC (%d)", quiet);

    /* Pass / Diff */
    a->setP(a, idx("diff"), 1.0f);
    CHECK(!strcmp(disp(a, "diff"), "On"), "Diff tapped: on (green)");
    reset();
    float p = a->getP(a, idx("bypass"));
    for (int k = 0; k < 4; k++) { a->setP(a, idx("bypass"), p + 1.0f / 128); p = a->getP(a, idx("bypass")); }
    CHECK(a->getP(a, idx("bypass")) == 1.0f, "Pass on a Q-Link: 4 events flip it (qlink_ticks), not 1");
    run(a, 10, 0, 0);
    CHECK(updates > 0 && a->getP(a, idx("diff")) == 1.0f, "Pass on: MPC re-reads (Diff panel turns grey via when=bypass), Diff keeps its setting");
    {
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        float hist[128 * 40]; int ok = 1;
        for (int k = 0; k < 40; k++) {
            for (int n = 0; n < 128; n++) { I0[n] = I1[n] = (float)((int)(noise() * 3000)) / 32768.0f; hist[k * 128 + n] = I0[n]; }
            a->pr(a, in, o, 128);
            if (k > 5) for (int n = 0; n < 128; n++) if (L[n] != hist[(k - 1) * 128 + n]) ok = 0;
        }
        CHECK(ok, "Pass: the input untouched through the real wrapper (2.9 ms late, bit-exact), Diff ignored");
    }
    a->setP(a, idx("bypass"), 0.0f);

    /* save + reload */
    a->setP(a, idx("output"), 0.4f);
    void *chunk = 0;
    intptr_t len = a->d(a, 23, 0, 0, &chunk, 0);
    AEffect *b = VSTPluginMain(host);
    b->d(b, 24, 0, len, chunk, 0);
    int same = 1;
    const char *keys[] = {"threshold", "reduction", "output", "bypass", "diff", "n1", "n2", "n3", "n4", "n5", "n6", "n1x", "n6x"};
    for (unsigned k = 0; k < sizeof keys / sizeof *keys; k++)
        if (fabsf(a->getP(a, idx(keys[k])) - b->getP(b, idx(keys[k]))) > 2e-4f) { same = 0; printf("     %s %.4f vs %.4f\n", keys[k], a->getP(a, idx(keys[k])), b->getP(b, idx(keys[k]))); }
    CHECK(len > 100 && same, "save + reload through the chunk (%d bytes): every control and the learned profile", (int)len);
    b->d(b, 1, 0, 0, 0, 0);

    /* presets (1.0.3): MPC's PRESET menu and the selector on screen (shared/preset_nav.h). Same-button taps are 260 ms
     * apart: the wrapper ignores a second press within 250 ms. */
    {
        struct timespec gap = {0, 260 * 1000000L};
#define TAP(k) do { nanosleep(&gap, NULL); a->setP(a, idx(k), 1.0f); run(a, 20, 0, 0); } while (0)
        char nm[64] = "";
        a->d(a, 29, 5, 0, nm, 0);
        CHECK(a->np == 12 && !strcmp(nm, "Dialogue Clean"), "PRESET menu: %d programs, #5 \"%s\"", a->np, nm);
        float prof[6]; for (int i = 0; i < 6; i++) prof[i] = nodeDb(a, i + 1) - 120.0f * 0;   /* learned nodes, to compare */
        float thr0 = a->getP(a, idx("threshold"));
        a->setP(a, idx("diff"), 1.0f); run(a, 4, 0, 0);
        reset();
        a->d(a, 2, 0, 5, 0, 0); run(a, 20, 0, 0);   /* Dialogue Clean from the menu */
        CHECK(!strcmp(disp(a, "threshold"), "+2.0 dB") && !strcmp(disp(a, "reduction"), "15 dB") && a->getP(a, idx("diff")) == 0.0f &&
              automated[idx("threshold")] > 0 && automated[idx("reduction")] > 0 && automated[idx("diff")] > 0,
              "a preset sets Threshold %s / Reduction %s, turns Diff off, and MPC is told", disp(a, "threshold"), disp(a, "reduction"));
        float moved = 0; for (int i = 0; i < 6; i++) moved = fmaxf(moved, fabsf((nodeDb(a, i + 1) - prof[i]) - (a->getP(a, idx("threshold")) - thr0) * 60.0f));
        CHECK(moved < 0.6f, "the learned profile is kept: the nodes only follow the new Threshold (off by %.2f dB)", moved);
        TAP("preset_next");
        a->d(a, 5, 0, 0, nm, 0);
        CHECK(a->d(a, 3, 0, 0, 0, 0) == 6 && !strcmp(nm, "Podcast Voice") && !strcmp(disp(a, "output"), "+0.5 dB"), "> : \"%s\", Output %s", nm, disp(a, "output"));
        TAP("preset_list");
        CHECK(a->getP(a, idx("browsing")) == 1.0f && lroundf(a->getP(a, idx("pre_hl")) * 12) == 7, "the name opens the browser on the current preset");
        TAP("pick12");
        a->d(a, 5, 0, 0, nm, 0);
        CHECK(a->d(a, 3, 0, 0, 0, 0) == 11 && !strcmp(nm, "Heavy Noise") && a->getP(a, idx("browsing")) == 0.0f, "a pick loads \"%s\" and closes the browser", nm);
        a->setP(a, idx("reduction"), 0.5f); run(a, 4, 0, 0);
        void *ch2 = 0; intptr_t l2 = a->d(a, 23, 0, 0, &ch2, 0);
        AEffect *c = VSTPluginMain(host);
        c->d(c, 24, 0, l2, ch2, 0);
        c->d(c, 2, 0, c->d(c, 3, 0, 0, 0, 0), 0, 0);   /* JUCE re-selects the current program after a load */
        run(c, 4, 0, 0);
        c->d(c, 5, 0, 0, nm, 0);
        CHECK(!strcmp(nm, "Heavy Noise") && !strcmp(disp(c, "reduction"), "50 dB"), "reload: \"%s\" with the tweak kept (Reduction %s)", nm, disp(c, "reduction"));
        c->d(c, 24, 0, 11, "SHH1;pre=99", 0); c->setP(c, idx("preset"), NAN); run(c, 4, 0, 0);
        CHECK(c->d(c, 3, 0, 0, 0, 0) >= 0 && c->d(c, 3, 0, 0, 0, 0) < 12, "garbage preset values are clamped");
        c->d(c, 1, 0, 0, 0, 0);
#undef TAP
    }
    a->d(a, 1, 0, 0, 0, 0);
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
