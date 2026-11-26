/* Poli-Saturator mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see: read-outs, Type < > with wrap, knob
 * extremes, Q-Link names, the preset arrows and browser, saved state, audio through the wrapper. Build and run: test/run_tests.sh */
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
int main(void) {
    AEffect *a = VSTPluginMain(host);
    run(a, 40, 0, 100);
    printf("info defaults: sat \"%s\" smash \"%s\" crunch \"%s\" soften \"%s\" low \"%s\" mid \"%s\" high \"%s\" type %d\n",
           disp(a, "sat"), disp(a, "smash"), disp(a, "crunch"), disp(a, "soften"), disp(a, "low"), disp(a, "mid"), disp(a, "high"), opt(a, "type", 4));
    CHECK(!strcmp(disp(a, "sat"), "50 %") && !strcmp(disp(a, "smash"), "0 %") && !strcmp(disp(a, "crunch"), "40 %") && !strcmp(disp(a, "soften"), "25 %"),
          "read-outs: sat \"%s\", smash \"%s\", crunch \"%s\", soften \"%s\"", disp(a, "sat"), disp(a, "smash"), disp(a, "crunch"), disp(a, "soften"));
    CHECK(!strcmp(disp(a, "low"), "0 dB") && !strcmp(disp(a, "mid"), "0 dB") && !strcmp(disp(a, "high"), "0 dB") && opt(a, "type", 4) == 0,
          "bands at 0 dB, Type = Tube");
    CHECK(!strcmp(pname(a, "sat"), "Saturation") && !strcmp(pname(a, "smash"), "Smash") && !strcmp(pname(a, "crunch"), "Crunch") &&
          !strcmp(pname(a, "soften"), "Soften") && !strcmp(pname(a, "low"), "Low") && !strcmp(pname(a, "type"), "Type"),
          "names for the Q-Links: %s / %s / %s / %s / %s / %s", pname(a, "sat"), pname(a, "smash"), pname(a, "crunch"), pname(a, "soften"), pname(a, "low"), pname(a, "type"));
    /* knob extremes read out sensibly */
    a->setP(a, idx("sat"), 1.0f); CHECK(!strcmp(disp(a, "sat"), "100 %"), "Saturation at max reads \"%s\"", disp(a, "sat"));
    a->setP(a, idx("sat"), 0.0f); CHECK(!strcmp(disp(a, "sat"), "0 %"), "Saturation at min reads \"%s\"", disp(a, "sat"));
    a->setP(a, idx("low"), 0.0f); CHECK(!strcmp(disp(a, "low"), "Off"), "Low at min reads \"%s\"", disp(a, "low"));
    a->setP(a, idx("low"), 1.0f); CHECK(!strcmp(disp(a, "low"), "+12 dB"), "Low at max reads \"%s\"", disp(a, "low"));
    a->setP(a, idx("mid"), 0.5f); CHECK(!strcmp(disp(a, "mid"), "0 dB"), "Mid at centre reads \"%s\"", disp(a, "mid"));
    a->setP(a, idx("high"), 0.25f); CHECK(strstr(disp(a, "high"), "-") != NULL && strstr(disp(a, "high"), " dB") != NULL, "High at 25 %% reads \"%s\"", disp(a, "high"));
    a->setP(a, idx("smash"), 1.0f); CHECK(!strcmp(disp(a, "smash"), "100 %"), "Smash at max reads \"%s\"", disp(a, "smash"));
    /* back to the defaults for the rest */
    a->setP(a, idx("sat"), 0.5f); a->setP(a, idx("low"), 0.5f); a->setP(a, idx("high"), 0.5f); a->setP(a, idx("smash"), 0.0f); run(a, 10, 0, 100);
    /* Type: < > step and wrap */
    reset();
    tap(a, "type_next");
    CHECK(opt(a, "type", 4) == 1 && automated[idx("type")] > 0 && updates > 0, "Type > = Tape (%d), reported to MPC", opt(a, "type", 4));
    tap(a, "type_next"); tap(a, "type_next");
    CHECK(opt(a, "type", 4) == 3, "Type >>> = Fold (%d)", opt(a, "type", 4));
    tap(a, "type_next");
    CHECK(opt(a, "type", 4) == 0, "Type > wraps from Fold to Tube");
    tap(a, "type_prev");
    CHECK(opt(a, "type", 4) == 3, "Type < wraps from Tube to Fold");
    a->setP(a, idx("type"), 2.0f / 3.0f); run(a, 4, 0, 100);
    CHECK(opt(a, "type", 4) == 2, "Type set directly (Q-Link) = Clip");
    a->setP(a, idx("type"), 0.0f); run(a, 4, 0, 100);
    /* audio through the wrapper: Saturation 0 passes clean, Saturation 100 % adds harmonics but stays bounded and level-matched */
    {
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        for (int pass = 0; pass < 2; pass++) {
            a->setP(a, idx("sat"), pass ? 1.0f : 0.0f); run(a, 100, 0, 100);
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
            if (!pass) CHECK(nonfinite == 0 && fabs(db) < 0.5, "Saturation 0: clean pass-through (%.2f dB)", db);
            /* a pure 1 kHz tone is the worst case for the Gaussian level match (noise-like material lands within +-3 dB, see dsp_test) */
            else CHECK(nonfinite == 0 && pk <= 1.0f && db > -8.0 && db < 3.0, "Saturation 100 %%: bounded (peak %.2f), a pure tone stays within -8..+3 dB (%+.2f dB)", pk, db);
        }
        a->setP(a, idx("sat"), 0.5f); run(a, 100, 0, 100);
    }
    /* preset arrows and browser */
    reset();
    tap(a, "preset_next");
    CHECK(opt(a, "preset", 65) == 1, "preset > = program 1 (%s)", disp(a, "preset"));
    CHECK(automated[idx("sat")] > 0 && automated[idx("crunch")] > 0 && automated[idx("type")] > 0 && updates > 0, "preset load reports the knobs, Type and asks for UpdateDisplay");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 65) == 0, "preset < = back to Init");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 65) == 64, "preset < wraps to the last preset (%s)", disp(a, "preset"));
    tap(a, "preset_next");
    /* pick Drums > second preset (Kick Punch: Clip, sat 45 %): the Type follows */
    reset();
    tap(a, "preset_list");
    CHECK(a->getP(a, idx("browsing")) == 1.0f && opt(a, "bview", 12) >= 1, "browser opens (bview %d)", opt(a, "bview", 12));
    tap(a, "cat2");   /* Drums */
    CHECK(opt(a, "bview", 12) > 1, "category Drums view %d", opt(a, "bview", 12));
    tap(a, "pick2");
    CHECK(a->getP(a, idx("browsing")) == 0.0f && opt(a, "bview", 12) == 0, "pick closes the browser");
    printf("info picked preset: %d \"%s\"\n", opt(a, "preset", 65), disp(a, "preset"));
    CHECK(opt(a, "preset", 65) == 2 && opt(a, "type", 4) == 2 && !strcmp(disp(a, "sat"), "45 %") && !strcmp(disp(a, "smash"), "35 %"),
          "that preset set Type Clip, sat %s, smash %s", disp(a, "sat"), disp(a, "smash"));
    CHECK(automated[idx("type")] > 0 && automated[idx("sat")] > 0, "...and reported them to MPC");
    /* every category button opens a non-empty view; Init from All */
    for (int c = 1; c <= 7; c++) {
        char k[8]; snprintf(k, sizeof k, "cat%d", c);
        tap(a, "preset_list"); tap(a, k);
        CHECK(opt(a, "bview", 12) >= 1 && a->getP(a, idx("browsing")) == 1.0f, "%s opens view %d", k, opt(a, "bview", 12));
        if (c == 1) { tap(a, "pick1"); CHECK(opt(a, "preset", 65) == 0 && opt(a, "type", 4) == 0, "Init from the browser: Tube, defaults"); }
        else { tap(a, "pick1"); }
    }
    /* state round trip */
    tap(a, "preset_list"); tap(a, "cat7"); tap(a, "pick4");
    a->setP(a, idx("crunch"), 0.61f);
    {
        char buf[1024]; void *chunk = NULL;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        CHECK(n > 0 && chunk, "state chunk %d bytes", (int)n);
        if (n > 0 && chunk) {
            memcpy(buf, chunk, n < 1024 ? n : 1024);
            AEffect *b = VSTPluginMain(host);
            b->d(b, 24, 0, (intptr_t)n, buf, 0);
            CHECK(opt(b, "preset", 65) == opt(a, "preset", 65) && opt(b, "type", 4) == opt(a, "type", 4) &&
                  fabsf(b->getP(b, idx("crunch")) - a->getP(a, idx("crunch"))) < 1e-3 && fabsf(b->getP(b, idx("sat")) - a->getP(a, idx("sat"))) < 1e-3,
                  "state reload keeps preset, Type and knobs (preset %d, type %d)", opt(b, "preset", 65), opt(b, "type", 4));
        }
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
