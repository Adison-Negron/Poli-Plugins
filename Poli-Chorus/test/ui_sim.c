/* Poli-Chorus mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see: read-outs, the Mode buttons (and that the
 * lit one follows the engine), knob extremes, Q-Link names, the display box columns (they follow Rate / Amount / Width / Mode and MPC is told),
 * the preset arrows and browser, saved state, audio through the wrapper. Build and run: test/run_tests.sh */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
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
    if (op == 0 && i >= 0 && i < NPARAMS) { automated[i]++; autoval[i] = o; }
    if (op == 42) updates++;
    return 0;
}
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static float tone = 0;
static void run(AEffect *a, int blocks, float amp, float hz) {
    float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
    for (int k = 0; k < blocks; k++) {
        for (int n = 0; n < 128; n++) { I0[n] = I1[n] = amp * sinf(tone); tone += 2 * 3.14159265f * hz / 44100; }
        a->pr(a, in, o, 128);
    }
}
static void reset(void) { memset(automated, 0, sizeof automated); updates = 0; }
static const char *disp(AEffect *a, const char *k) { static char b[16][64]; static int n; char *r = b[n++ % 16]; r[0] = 0; a->d(a, 7, idx(k), 0, r, 0); return r; }
static const char *pname(AEffect *a, const char *k) { static char b[16][64]; static int n; char *r = b[n++ % 16]; r[0] = 0; a->d(a, 8, idx(k), 0, r, 0); return r; }
/* momentary params ignore a press within 250 ms of the last one (wrapper debounce), so the sim waits like a finger would */
static void tap(AEffect *a, const char *k) { usleep(270000); a->setP(a, idx(k), 1.0f); run(a, 12, 0, 100); a->setP(a, idx(k), 0.0f); run(a, 12, 0, 100); }
static int opt(AEffect *a, const char *k, int n) { return (int)lrintf(a->getP(a, idx(k)) * (n - 1)); }
/* the display box: column values 0..1 of vl001.. / vr001.. */
#define COLS 128
static void cols(AEffect *a, float *l, float *r) {
    char k[16];
    for (int j = 0; j < COLS; j++) {
        snprintf(k, sizeof k, "vl%03d", j + 1); l[j] = a->getP(a, idx(k));
        snprintf(k, sizeof k, "vr%03d", j + 1); r[j] = a->getP(a, idx(k));
    }
}
static float spread(const float *v) { float lo = 1, hi = 0; for (int j = 0; j < COLS; j++) { if (v[j] < lo) lo = v[j]; if (v[j] > hi) hi = v[j]; } return hi - lo; }
static float maxdiff(const float *x, const float *y) { float m = 0; for (int j = 0; j < COLS; j++) if (fabsf(x[j] - y[j]) > m) m = fabsf(x[j] - y[j]); return m; }
int main(void) {
    AEffect *a = VSTPluginMain(host);
    run(a, 40, 0, 100);
    printf("info defaults: rate \"%s\" amount \"%s\" feedback \"%s\" width \"%s\" lpf \"%s\" warmth \"%s\" output \"%s\" mix \"%s\" mode %d\n",
           disp(a, "rate"), disp(a, "amount"), disp(a, "feedback"), disp(a, "width"), disp(a, "lpf"), disp(a, "warmth"), disp(a, "output"), disp(a, "mix"), opt(a, "mode", 3));
    CHECK(!strcmp(disp(a, "rate"), "0.80 Hz") && !strcmp(disp(a, "amount"), "50 %") && !strcmp(disp(a, "feedback"), "0 %") && !strcmp(disp(a, "width"), "60 %"),
          "read-outs: rate \"%s\", amount \"%s\", feedback \"%s\", width \"%s\"", disp(a, "rate"), disp(a, "amount"), disp(a, "feedback"), disp(a, "width"));
    CHECK(!strcmp(disp(a, "lpf"), "Off") && !strcmp(disp(a, "warmth"), "25 %") && !strcmp(disp(a, "output"), "0.0 dB") && !strcmp(disp(a, "mix"), "50 %") && opt(a, "mode", 3) == 0,
          "read-outs: lpf \"%s\", warmth \"%s\", output \"%s\", mix \"%s\", mode Classic", disp(a, "lpf"), disp(a, "warmth"), disp(a, "output"), disp(a, "mix"));
    CHECK(!strcmp(pname(a, "rate"), "Rate") && !strcmp(pname(a, "amount"), "Amount") && !strcmp(pname(a, "feedback"), "Feedback") && !strcmp(pname(a, "width"), "Width") &&
          !strcmp(pname(a, "lpf"), "LP Filter") && !strcmp(pname(a, "warmth"), "Warmth") && !strcmp(pname(a, "output"), "Output") && !strcmp(pname(a, "mix"), "Mix") &&
          !strcmp(pname(a, "mode"), "Mode"), "names for the Q-Links");
    /* knob extremes read out sensibly and fit under the knob (7 characters at most) */
    {
        struct { const char *k; float v; const char *want; } t[] = {
            {"rate", 0.0f, "0.05 Hz"}, {"rate", 1.0f, "10.0 Hz"}, {"amount", 1.0f, "100 %"}, {"amount", 0.0f, "0 %"}, {"feedback", 1.0f, "100 %"},
            {"width", 1.0f, "100 %"}, {"lpf", 0.0f, "800 Hz"}, {"lpf", 1.0f, "Off"}, {"lpf", 0.996f, "Off"}, {"lpf", 0.99f, "19 kHz"}, {"lpf", 0.5f, "4.0 kHz"}, {"lpf", 0.85f, "12 kHz"},
            {"output", 0.0f, "-24 dB"}, {"output", 1.0f, "+12 dB"}, {"output", 0.667f, "0.0 dB"}, {"warmth", 1.0f, "100 %"}, {"mix", 1.0f, "100 %"}, {"mix", 0.0f, "0 %"}};
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            a->setP(a, idx(t[i].k), t[i].v);
            const char *got = disp(a, t[i].k);
            CHECK(!strcmp(got, t[i].want) && strlen(got) <= 7, "%s at %.3f reads \"%s\" (want \"%s\")", t[i].k, t[i].v, got, t[i].want);
        }
        a->setP(a, idx("lpf"), 1.0f); a->setP(a, idx("output"), 0.667f); a->setP(a, idx("rate"), 0.523f); a->setP(a, idx("amount"), 0.5f);
        a->setP(a, idx("feedback"), 0.0f); a->setP(a, idx("width"), 0.6f); a->setP(a, idx("warmth"), 0.25f); a->setP(a, idx("mix"), 0.5f); run(a, 10, 0, 100);
    }
    /* Mode buttons: set the mode, are reported to MPC (so the lit button follows), wrap nothing */
    reset();
    tap(a, "mode_ensemble");
    CHECK(opt(a, "mode", 3) == 1 && automated[idx("mode")] > 0, "Ensemble button sets Mode %d and reports it to MPC", opt(a, "mode", 3));
    tap(a, "mode_vibrato");
    CHECK(opt(a, "mode", 3) == 2, "Vibrato button sets Mode %d", opt(a, "mode", 3));
    tap(a, "mode_classic");
    CHECK(opt(a, "mode", 3) == 0, "Classic button sets Mode %d", opt(a, "mode", 3));
    a->setP(a, idx("mode"), 1.0f); run(a, 4, 0, 100);
    CHECK(opt(a, "mode", 3) == 2, "Mode set directly (Q-Link) = Vibrato");
    a->setP(a, idx("mode"), 0.0f); run(a, 4, 0, 100);
    /* the display box follows Rate / Amount / Width / Mode (and MPC is told through display_rev) */
    {
        float l0[COLS], r0[COLS], l1[COLS], r1[COLS];
        a->setP(a, idx("amount"), 0.0f); run(a, 2, 0, 100);
        cols(a, l0, r0);
        CHECK(spread(l0) < 1e-3f && fabsf(l0[0] - 0.5f) < 0.02f && maxdiff(l0, r0) < 1e-3f, "Amount 0 %%: both waves are the flat centre line (spread %.4f, at %.3f)", spread(l0), l0[0]);
        a->setP(a, idx("amount"), 1.0f); run(a, 2, 0, 100);
        cols(a, l0, r0);
        CHECK(spread(l0) > 0.5f, "Amount 100 %%: the wave swings %.2f of the box", spread(l0));
        a->setP(a, idx("width"), 0.0f); run(a, 2, 0, 100); cols(a, l0, r0);
        CHECK(maxdiff(l0, r0) < 1e-3f, "Width 0 %%: the right wave sits exactly on the left one");
        a->setP(a, idx("width"), 1.0f); run(a, 2, 0, 100); cols(a, l0, r0);
        CHECK(maxdiff(l0, r0) > 0.5f, "Width 100 %%: the right wave is half a cycle behind (max difference %.2f)", maxdiff(l0, r0));
        a->setP(a, idx("width"), 0.5f); a->setP(a, idx("rate"), 0.0f); run(a, 2, 0, 100); cols(a, l0, r0);
        a->setP(a, idx("rate"), 1.0f); run(a, 2, 0, 100); cols(a, l1, r1);
        int cross0 = 0, cross1 = 0; for (int j = 1; j < COLS; j++) { cross0 += (l0[j - 1] - 0.5f) * (l0[j] - 0.5f) < 0; cross1 += (l1[j - 1] - 0.5f) * (l1[j] - 0.5f) < 0; }
        CHECK(cross1 > cross0 + 2, "Rate up: more cycles across the box (%d -> %d zero crossings)", cross0, cross1);
        a->setP(a, idx("rate"), 0.5f); a->setP(a, idx("mode"), 0.0f); run(a, 2, 0, 100); cols(a, l0, r0);
        a->setP(a, idx("mode"), 0.5f); run(a, 2, 0, 100); cols(a, l1, r1);
        CHECK(maxdiff(l0, l1) > 0.02f, "Ensemble draws a different shape than Classic (max difference %.3f)", maxdiff(l0, l1));
        int in01 = 1; for (int j = 0; j < COLS; j++) if (l1[j] < 0 || l1[j] > 1 || r1[j] < 0 || r1[j] > 1) in01 = 0;
        CHECK(in01, "every column stays inside 0..1");
        /* back to the defaults */
        a->setP(a, idx("mode"), 0.0f); a->setP(a, idx("amount"), 0.5f); a->setP(a, idx("width"), 0.6f); a->setP(a, idx("rate"), 0.523f); run(a, 10, 0, 100);
        /* a knob move reports moved columns to MPC without a screen rebuild (the display_rev poll, every 100 ms) */
        reset();
        a->setP(a, idx("amount"), 0.9f); run(a, 40, 0, 100);   /* 40 blocks = 116 ms */
        int moved = 0; for (int j = 0; j < COLS; j++) { char k[16]; snprintf(k, sizeof k, "vl%03d", j + 1); moved += automated[idx(k)] > 0; }
        CHECK(moved > 20, "Amount move: MPC was told about %d display columns", moved);
        a->setP(a, idx("amount"), 0.5f); run(a, 40, 0, 100);
    }
    /* audio through the wrapper: Mix 0 passes clean, Mix 100 % stays bounded and level-matched */
    {
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        for (int pass = 0; pass < 2; pass++) {
            a->setP(a, idx("mix"), pass ? 1.0f : 0.0f); run(a, 100, 0, 100);
            double ei = 0, eo = 0; float pk = 0; int nonfinite = 0;
            for (int blk = 0; blk < 200; blk++) {
                for (int n = 0; n < 128; n++) { I0[n] = I1[n] = 0.25f * sinf(tone); tone += 2 * 3.14159265f * 1000 / 44100; }
                a->pr(a, in, o, 128);
                for (int n = 0; n < 128; n++) {
                    if (!(fabsf(L[n]) < 100.0f)) nonfinite++;
                    ei += I0[n] * I0[n]; eo += L[n] * L[n]; if (fabsf(L[n]) > pk) pk = fabsf(L[n]);
                }
            }
            double db = 10 * log10(eo / ei);
            if (!pass) CHECK(nonfinite == 0 && fabs(db) < 0.1, "Mix 0: clean pass-through (%.2f dB)", db);
            else CHECK(nonfinite == 0 && pk <= 1.0f && db > -6.0 && db < 3.0, "Mix 100 %%: bounded (peak %.2f), a 1 kHz tone stays within -6..+3 dB (%+.2f dB)", pk, db);
        }
        a->setP(a, idx("mix"), 0.5f); run(a, 100, 0, 100);
    }
    /* preset arrows and browser */
    reset();
    tap(a, "preset_next");
    CHECK(opt(a, "preset", 51) == 1, "preset > = program 1 (%s)", disp(a, "preset"));
    CHECK(automated[idx("rate")] > 0 && automated[idx("mix")] > 0 && automated[idx("mode")] > 0 && updates > 0, "preset load reports the knobs, Mode and asks for UpdateDisplay");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 51) == 0, "preset < = back to Init");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 51) == 50, "preset < wraps to the last preset (%s)", disp(a, "preset"));
    tap(a, "preset_next");
    /* pick Vibrato > first preset (Gentle Vibrato: Vibrato mode, mix 100 %): the Mode follows and the lit button with it */
    reset();
    tap(a, "preset_list");
    CHECK(a->getP(a, idx("browsing")) == 1.0f && opt(a, "bview", 12) >= 1, "browser opens (bview %d)", opt(a, "bview", 12));
    tap(a, "cat4");   /* Vibrato */
    CHECK(opt(a, "bview", 12) > 1, "category Vibrato view %d", opt(a, "bview", 12));
    tap(a, "pick1");
    CHECK(a->getP(a, idx("browsing")) == 0.0f && opt(a, "bview", 12) == 0, "pick closes the browser");
    printf("info picked preset: %d \"%s\"\n", opt(a, "preset", 51), disp(a, "preset"));
    CHECK(opt(a, "mode", 3) == 2 && !strcmp(disp(a, "mix"), "100 %") && !strcmp(disp(a, "rate"), "5.2 Hz") && !strcmp(disp(a, "amount"), "18 %"),
          "that preset set Mode Vibrato, mix %s, rate %s, amount %s", disp(a, "mix"), disp(a, "rate"), disp(a, "amount"));
    CHECK(automated[idx("mode")] > 0 && automated[idx("rate")] > 0, "...and reported them to MPC");
    /* every category button opens a non-empty view; Init from All */
    for (int c = 1; c <= 7; c++) {
        char k[8]; snprintf(k, sizeof k, "cat%d", c);
        tap(a, "preset_list"); tap(a, k);
        CHECK(opt(a, "bview", 12) >= 1 && a->getP(a, idx("browsing")) == 1.0f, "%s opens view %d", k, opt(a, "bview", 12));
        if (c == 1) { tap(a, "pick1"); CHECK(opt(a, "preset", 51) == 0 && opt(a, "mode", 3) == 0, "Init from the browser: Classic, defaults"); }
        else { tap(a, "pick1"); }
    }
    /* state round trip */
    tap(a, "preset_list"); tap(a, "cat3"); tap(a, "pick4");
    a->setP(a, idx("feedback"), 0.61f);
    {
        char buf[1024]; void *chunk = NULL;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        CHECK(n > 0 && chunk, "state chunk %d bytes", (int)n);
        if (n > 0 && chunk) {
            memcpy(buf, chunk, n < 1024 ? n : 1024);
            AEffect *b = VSTPluginMain(host);
            b->d(b, 24, 0, (intptr_t)n, buf, 0);
            float la[COLS], ra[COLS], lb[COLS], rb[COLS]; cols(a, la, ra); cols(b, lb, rb);
            CHECK(opt(b, "preset", 51) == opt(a, "preset", 51) && opt(b, "mode", 3) == opt(a, "mode", 3) &&
                  fabsf(b->getP(b, idx("feedback")) - a->getP(a, idx("feedback"))) < 1e-3 && fabsf(b->getP(b, idx("rate")) - a->getP(a, idx("rate"))) < 1e-3,
                  "state reload keeps preset, Mode and knobs (preset %d, mode %d)", opt(b, "preset", 51), opt(b, "mode", 3));
            CHECK(maxdiff(la, lb) < 1e-3f && maxdiff(ra, rb) < 1e-3f, "...and the display box shows the same waves");
        }
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
