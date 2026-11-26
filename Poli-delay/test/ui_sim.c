/* Poli-delay mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see: read-outs, Sync / Offset /
 * Ping-Pong toggles (and the Offset knob's when=offset:On), Q-Link names, the preset browser, saved state. Build and run: test/run_tests.sh */
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
    printf("info defaults: time \"%s\" offset \"%s\" feedback \"%s\" hp \"%s\" lp \"%s\" mix \"%s\" rate \"%s\" depth \"%s\" drive \"%s\" tone \"%s\" spread \"%s\"\n",
           disp(a, "time"), disp(a, "offset_amt"), disp(a, "feedback"), disp(a, "hp"), disp(a, "lp"), disp(a, "mix"), disp(a, "rate"),
           disp(a, "depth"), disp(a, "drive"), disp(a, "tone"), disp(a, "spread"));
    CHECK(strstr(disp(a, "time"), " ms") != NULL && !strcmp(disp(a, "feedback"), "35 %") && !strcmp(disp(a, "mix"), "30 %"), "read-outs: time \"%s\", feedback \"%s\", mix \"%s\"", disp(a, "time"), disp(a, "feedback"), disp(a, "mix"));
    CHECK(!strcmp(disp(a, "hp"), "50 Hz") && !strcmp(disp(a, "offset_amt"), "0 %") && !strcmp(disp(a, "tone"), "0 %"), "hp \"%s\", offset \"%s\", tone \"%s\"", disp(a, "hp"), disp(a, "offset_amt"), disp(a, "tone"));
    CHECK(!strcmp(pname(a, "offset_amt"), "Offset") && !strcmp(pname(a, "hp"), "High Pass") && !strcmp(pname(a, "lp"), "Low Pass"), "names for the Q-Links: %s / %s / %s", pname(a, "offset_amt"), pname(a, "hp"), pname(a, "lp"));
    /* knob extremes read out sensibly */
    a->setP(a, idx("hp"), 0.0f); CHECK(!strcmp(disp(a, "hp"), "Off"), "High Pass at 0 reads \"%s\"", disp(a, "hp"));
    a->setP(a, idx("lp"), 1.0f); CHECK(!strcmp(disp(a, "lp"), "Off"), "Low Pass at max reads \"%s\"", disp(a, "lp"));
    a->setP(a, idx("lp"), 0.0f); CHECK(!strcmp(disp(a, "lp"), "500 Hz"), "Low Pass at min reads \"%s\"", disp(a, "lp"));
    a->setP(a, idx("hp"), 1.0f); CHECK(!strcmp(disp(a, "hp"), "2 kHz"), "High Pass at max reads \"%s\"", disp(a, "hp"));
    a->setP(a, idx("time"), 1.0f); CHECK(!strcmp(disp(a, "time"), "2 s"), "Time at max reads \"%s\"", disp(a, "time"));
    a->setP(a, idx("time"), 0.0f); CHECK(!strcmp(disp(a, "time"), "5 ms"), "Time at min reads \"%s\"", disp(a, "time"));
    a->setP(a, idx("offset_amt"), 1.0f); CHECK(!strcmp(disp(a, "offset_amt"), "+100 %"), "Offset at max reads \"%s\"", disp(a, "offset_amt"));
    a->setP(a, idx("offset_amt"), 0.0f); CHECK(!strcmp(disp(a, "offset_amt"), "-100 %"), "Offset at min reads \"%s\"", disp(a, "offset_amt"));
    a->setP(a, idx("rate"), 1.0f); CHECK(!strcmp(disp(a, "rate"), "10.0 Hz"), "Rate at max reads \"%s\"", disp(a, "rate"));
    a->setP(a, idx("spread"), 1.0f); CHECK(!strcmp(disp(a, "spread"), "200 %"), "Spread at max reads \"%s\"", disp(a, "spread"));
    /* back to the defaults for the rest */
    a->setP(a, idx("hp"), 0.2f); a->setP(a, idx("lp"), 0.85f); a->setP(a, idx("time"), 0.683f); a->setP(a, idx("offset_amt"), 0.5f);
    a->setP(a, idx("rate"), 0.45f); a->setP(a, idx("spread"), 0.5f); run(a, 10, 0, 100);
    /* Sync */
    reset();
    a->setP(a, idx("sync"), 1.0f); run(a, 40, 0, 100);
    printf("info synced time \"%s\"\n", disp(a, "time"));
    CHECK(strchr(disp(a, "time"), '/') != NULL && updates > 0, "Sync on: Time reads a note value (\"%s\") and MPC is asked to redraw", disp(a, "time"));
    a->setP(a, idx("sync"), 0.0f); run(a, 10, 0, 100);
    CHECK(strchr(disp(a, "time"), '/') == NULL, "Sync off: milliseconds again (\"%s\")", disp(a, "time"));
    /* Offset toggle: its own parameter drives the skin's when=offset:On */
    reset();
    a->setP(a, idx("offset"), 1.0f); run(a, 10, 0, 100);
    CHECK(a->getP(a, idx("offset")) == 1.0f && !strcmp(disp(a, "offset"), "On"), "Offset on (the skin shows the Offset knob): \"%s\"", disp(a, "offset"));
    a->setP(a, idx("offset"), 0.0f); run(a, 10, 0, 100);
    CHECK(a->getP(a, idx("offset")) == 0.0f, "Offset off again");
    a->setP(a, idx("pingpong"), 1.0f); run(a, 10, 0, 100);
    CHECK(a->getP(a, idx("pingpong")) == 1.0f && !strcmp(disp(a, "pingpong"), "On"), "Ping-Pong on");
    a->setP(a, idx("pingpong"), 0.0f); run(a, 40, 0, 100);
    /* a mono hit through the real wrapper: echo comes back (the engine is wired to the DSP) */
    {
        a->setP(a, idx("mix"), 1.0f); a->setP(a, idx("feedback"), 0.0f); a->setP(a, idx("time"), 0.5f);
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        float pk[2] = {0, 0}; double ms = 0.2; int total = 44100 / 4;
        for (int blk = 0; blk * 128 < total; blk++) {
            for (int n = 0; n < 128; n++) I0[n] = I1[n] = (blk == 0 && n == 0) ? 0.5f : 0.0f;
            a->pr(a, in, o, 128);
            if (blk * 128 > 2000) for (int n = 0; n < 128; n++) { if (fabsf(L[n]) > pk[0]) pk[0] = fabsf(L[n]); if (fabsf(R[n]) > pk[1]) pk[1] = fabsf(R[n]); }
        }
        (void)ms;
        printf("info state before echo: pingpong %s offset %s sync %s spread %s feedback %s time %s mix %s\n", disp(a, "pingpong"), disp(a, "offset"), disp(a, "sync"), disp(a, "spread"), disp(a, "feedback"), disp(a, "time"), disp(a, "mix"));
        CHECK(pk[0] > 0.2f && pk[1] > 0.2f, "audio through the wrapper: echo at 100 ms (L %.3f, R %.3f)", pk[0], pk[1]);
        a->setP(a, idx("mix"), 0.3f); a->setP(a, idx("feedback"), 0.35f); a->setP(a, idx("time"), 0.683f); run(a, 400, 0, 100);
    }
    /* preset arrows and browser */
    reset();
    tap(a, "preset_next");
    CHECK(opt(a, "preset", 75) == 1, "preset > = program 1 (%s)", disp(a, "preset"));
    CHECK(automated[idx("mix")] > 0 && automated[idx("time")] > 0 && updates > 0, "preset load reports the knobs and asks for UpdateDisplay");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 75) == 0, "preset < = back to Init");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 75) == 74, "preset < wraps to the last preset (%s)", disp(a, "preset"));
    tap(a, "preset_next");
    /* pick a preset with Offset + Ping-Pong + Sync from the browser: toggles follow */
    reset();
    tap(a, "preset_list");
    CHECK(a->getP(a, idx("browsing")) == 1.0f && opt(a, "bview", 16) >= 1, "browser opens (bview %d)", opt(a, "bview", 16));
    tap(a, "cat4");   /* Ping-Pong */
    int v4 = opt(a, "bview", 16);
    CHECK(v4 > 1, "category 3 (Ping-Pong) view %d", v4);
    tap(a, "pick8");  /* Ping Offset: sync 1/8, pingpong, offset +50 % */
    CHECK(a->getP(a, idx("browsing")) == 0.0f && opt(a, "bview", 16) == 0, "pick closes the browser");
    printf("info picked preset: %d \"%s\"\n", opt(a, "preset", 75), disp(a, "preset"));
    CHECK(a->getP(a, idx("sync")) == 1.0f && a->getP(a, idx("pingpong")) == 1.0f && a->getP(a, idx("offset")) == 1.0f && !strcmp(disp(a, "offset_amt"), "+50 %"),
          "that preset switched Sync, Ping-Pong and Offset on with offset %s", disp(a, "offset_amt"));
    CHECK(automated[idx("offset")] > 0 && automated[idx("pingpong")] > 0 && automated[idx("sync")] > 0, "...and reported all three toggles to MPC");
    /* Init turns them all off again */
    tap(a, "preset_list"); tap(a, "cat1"); tap(a, "pick1");
    CHECK(opt(a, "preset", 75) == 0 && a->getP(a, idx("sync")) == 0.0f && a->getP(a, idx("pingpong")) == 0.0f && a->getP(a, idx("offset")) == 0.0f, "Init from the browser: toggles back off");
    /* state round trip */
    tap(a, "preset_list"); tap(a, "cat4"); tap(a, "pick8");
    a->setP(a, idx("feedback"), 0.61f);
    {
        char buf[1024]; void *chunk = NULL;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        CHECK(n > 0 && chunk, "state chunk %d bytes", (int)n);
        if (n > 0 && chunk) {
            memcpy(buf, chunk, n < 1024 ? n : 1024);
            AEffect *b = VSTPluginMain(host);
            b->d(b, 24, 0, (intptr_t)n, buf, 0);
            CHECK(opt(b, "preset", 75) == opt(a, "preset", 75) && fabsf(b->getP(b, idx("feedback")) - a->getP(a, idx("feedback"))) < 1e-3 &&
                  b->getP(b, idx("offset")) == 1.0f && b->getP(b, idx("pingpong")) == 1.0f && b->getP(b, idx("sync")) == 1.0f,
                  "state reload keeps preset, knobs and the three toggles");
        }
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
