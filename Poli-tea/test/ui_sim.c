/* Poli-tea mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see: read-outs, Sync / Offset /
 * Sidechain toggle, Q-Link names, meters, the preset browser, saved state. Build and run: test/run_tests.sh */
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
    printf("info defaults: input \"%s\" drive \"%s\" low \"%s\" mid \"%s\" high \"%s\" amount \"%s\" ldrive \"%s\" lthresh \"%s\" output \"%s\"\n",
           disp(a, "input"), disp(a, "drive"), disp(a, "low"), disp(a, "mid"), disp(a, "high"), disp(a, "amount"), disp(a, "ldrive"), disp(a, "lthresh"), disp(a, "output"));
    CHECK(!strcmp(disp(a, "input"), "0 dB") && !strcmp(disp(a, "drive"), "30 %") && !strcmp(disp(a, "amount"), "100 %"), "read-outs: input \"%s\", drive \"%s\", amount \"%s\"", disp(a, "input"), disp(a, "drive"), disp(a, "amount"));
    CHECK(!strcmp(disp(a, "lthresh"), "-2 dB") && !strcmp(disp(a, "output"), "-0.3 dB") && !strcmp(disp(a, "low"), "0 dB"), "lthresh \"%s\", output \"%s\", low \"%s\"", disp(a, "lthresh"), disp(a, "output"), disp(a, "low"));
    CHECK(!strcmp(pname(a, "amount"), "Amount") && !strcmp(pname(a, "ldrive"), "Limiter Drive") && !strcmp(pname(a, "lthresh"), "Limiter Threshold") && !strcmp(pname(a, "sc"), "Sidechain"),
          "names for the Q-Links: %s / %s / %s / %s", pname(a, "amount"), pname(a, "ldrive"), pname(a, "lthresh"), pname(a, "sc"));
    /* knob extremes */
    a->setP(a, idx("input"), 0.0f); CHECK(!strcmp(disp(a, "input"), "-18 dB"), "Input at min \"%s\"", disp(a, "input"));
    a->setP(a, idx("input"), 1.0f); CHECK(!strcmp(disp(a, "input"), "+18 dB"), "Input at max \"%s\"", disp(a, "input"));
    a->setP(a, idx("amount"), 1.0f); CHECK(!strcmp(disp(a, "amount"), "200 %"), "Amount at max \"%s\"", disp(a, "amount"));
    a->setP(a, idx("amount"), 0.0f); CHECK(!strcmp(disp(a, "amount"), "0 %"), "Amount at min \"%s\"", disp(a, "amount"));
    a->setP(a, idx("low"), 1.0f); CHECK(!strcmp(disp(a, "low"), "+6 dB"), "Low at max \"%s\"", disp(a, "low"));
    a->setP(a, idx("mid"), 0.0f); CHECK(!strcmp(disp(a, "mid"), "-4 dB"), "Mid at min \"%s\"", disp(a, "mid"));
    a->setP(a, idx("high"), 1.0f); CHECK(!strcmp(disp(a, "high"), "+6 dB"), "High at max \"%s\"", disp(a, "high"));
    a->setP(a, idx("lthresh"), 0.0f); CHECK(!strcmp(disp(a, "lthresh"), "-18 dB"), "Limiter Threshold at min \"%s\"", disp(a, "lthresh"));
    a->setP(a, idx("output"), 1.0f); CHECK(!strcmp(disp(a, "output"), "0 dB"), "Output at max \"%s\"", disp(a, "output"));
    a->setP(a, idx("output"), 0.0f); CHECK(!strcmp(disp(a, "output"), "-24 dB"), "Output at min \"%s\"", disp(a, "output"));
    a->setP(a, idx("input"), 0.5f); a->setP(a, idx("amount"), 0.5f); a->setP(a, idx("low"), 0.5f); a->setP(a, idx("mid"), 0.5f);
    a->setP(a, idx("high"), 0.5f); a->setP(a, idx("lthresh"), 0.889f); a->setP(a, idx("output"), 0.988f); run(a, 10, 0, 100);
    /* Sidechain toggle */
    reset();
    a->setP(a, idx("sc"), 1.0f); run(a, 10, 0, 100);
    CHECK(a->getP(a, idx("sc")) == 1.0f && !strcmp(disp(a, "sc"), "External"), "Sidechain External: \"%s\"", disp(a, "sc"));
    a->setP(a, idx("sc"), 0.0f); run(a, 10, 0, 100);
    CHECK(a->getP(a, idx("sc")) == 0.0f && !strcmp(disp(a, "sc"), "Internal"), "Sidechain Internal: \"%s\"", disp(a, "sc"));
    /* audio + meters through the real wrapper: a hot tone is limited under the ceiling, meters move, silence settles them */
    {
        a->setP(a, idx("input"), 0.9f); a->setP(a, idx("amount"), 0.7f); a->setP(a, idx("ldrive"), 0.7f);
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        float pk = 0;
        for (int blk = 0; blk < 700; blk++) {
            for (int n = 0; n < 128; n++) { I0[n] = I1[n] = 0.8f * sinf(tone) + 0.15f * sinf(tone * 7.3f); tone += 2 * 3.14159265f * 220 / 44100; }
            a->pr(a, in, o, 128);
            if (blk > 300) for (int n = 0; n < 128; n++) { if (fabsf(L[n]) > pk) pk = fabsf(L[n]); if (fabsf(R[n]) > pk) pk = fabsf(R[n]); }
        }
        char b1[32], b2[32];
        float mp = a->getP(a, idx("m_pk")), mr = a->getP(a, idx("m_rms")), ml = a->getP(a, idx("m_lim"));
        printf("info hot tone: peak %.3f, m_pk %.2f m_rms %.2f m_gl %.2f m_gm %.2f m_gh %.2f m_lim %.2f\n", pk, mp, mr, a->getP(a, idx("m_gl")), a->getP(a, idx("m_gm")), a->getP(a, idx("m_gh")), ml);
        (void)b1; (void)b2;
        CHECK(pk > 0.3f && pk <= 1.0f, "output peak %.3f is audible and within full scale", pk);
        CHECK(mp > 0.5f && mr > 0.3f && mp >= mr, "input meters move (peak %.2f >= rms %.2f)", mp, mr);
        CHECK(a->getP(a, idx("m_gl")) + a->getP(a, idx("m_gm")) + a->getP(a, idx("m_gh")) + ml > 0.05f, "gain-reduction meters move");
        CHECK(updates > 0, "meter changes ask MPC to redraw");
        run(a, 3000, 0, 100);
        CHECK(a->getP(a, idx("m_pk")) < 0.05f && a->getP(a, idx("m_gl")) < 0.05f, "meters fall back after silence (pk %.2f, gl %.2f)", a->getP(a, idx("m_pk")), a->getP(a, idx("m_gl")));
        a->setP(a, idx("input"), 0.5f); a->setP(a, idx("amount"), 0.5f); a->setP(a, idx("ldrive"), 0.2f); run(a, 10, 0, 100);
    }
    /* preset arrows and browser */
    reset();
    tap(a, "preset_next");
    CHECK(opt(a, "preset", 57) == 1, "preset > = program 1 (%s)", disp(a, "preset"));
    CHECK(automated[idx("amount")] > 0 && automated[idx("drive")] > 0 && updates > 0, "preset load reports the knobs and asks for UpdateDisplay");
    CHECK(!strcmp(disp(a, "amount"), "80 %") && !strcmp(disp(a, "drive"), "20 %") && !strcmp(disp(a, "lthresh"), "-2.5 dB"), "Green Tea values: amount %s, drive %s, threshold %s", disp(a, "amount"), disp(a, "drive"), disp(a, "lthresh"));
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 57) == 0, "preset < = back to Init");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 57) == 56, "preset < wraps to the last preset (%s)", disp(a, "preset"));
    tap(a, "preset_next");
    reset();
    tap(a, "preset_list");
    CHECK(a->getP(a, idx("browsing")) == 1.0f && opt(a, "bview", 14) >= 1, "browser opens (bview %d)", opt(a, "bview", 14));
    tap(a, "cat2");
    int v2 = opt(a, "bview", 14);
    CHECK(v2 >= 1, "Master category view %d", v2);
    tap(a, "cat9");   /* Lo-Fi */
    CHECK(opt(a, "bview", 14) > v2, "Lo-Fi category view %d", opt(a, "bview", 14));
    tap(a, "pick7");
    CHECK(a->getP(a, idx("browsing")) == 0.0f && opt(a, "bview", 14) == 0, "pick closes the browser");
    printf("info picked preset: %d \"%s\"\n", opt(a, "preset", 57), disp(a, "preset"));
    CHECK(opt(a, "preset", 57) >= 50, "a Lo-Fi preset was loaded");
    /* a preset with External sidechain, if any, is honoured; Init resets everything */
    tap(a, "preset_list"); tap(a, "cat1"); tap(a, "pick1");
    CHECK(opt(a, "preset", 57) == 0 && a->getP(a, idx("sc")) == 0.0f && !strcmp(disp(a, "amount"), "100 %"), "Init from the browser: defaults back");
    /* state round trip */
    tap(a, "preset_list"); tap(a, "cat9"); tap(a, "pick3");
    a->setP(a, idx("amount"), 0.61f); a->setP(a, idx("sc"), 1.0f);
    {
        char buf[1024]; void *chunk = NULL;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        CHECK(n > 0 && chunk, "state chunk %d bytes", (int)n);
        if (n > 0 && chunk) {
            memcpy(buf, chunk, n < 1024 ? n : 1024);
            AEffect *b = VSTPluginMain(host);
            b->d(b, 24, 0, (intptr_t)n, buf, 0);
            CHECK(opt(b, "preset", 57) == opt(a, "preset", 57) && fabsf(b->getP(b, idx("amount")) - a->getP(a, idx("amount"))) < 1e-3 && b->getP(b, idx("sc")) == 1.0f,
                  "state reload keeps preset, knobs and the Sidechain switch");
        }
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
