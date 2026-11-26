/* Poli-Multiband mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see when a band
 * tab is tapped, a band is picked by Q-Link, or a value snaps back. Build: test/run_tests.sh */
#include <stdio.h>
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
int main(void) {
    AEffect *a = VSTPluginMain(host);
    run(a, 40, 0, 100);
    printf("info knob text at insert: input \"%s\" output \"%s\" split \"%s\" attack \"%s\" release \"%s\" knee \"%s\" ratio \"%s\" thresh \"%s\"\n",
           disp(a, "c_input"), disp(a, "c_output"), disp(a, "x_freq"), disp(a, "c_attack"), disp(a, "c_release"),
           disp(a, "c_knee"), disp(a, "c_ratio"), disp(a, "c_thresh"));
    printf("info upward: ratio \"%s\" thresh \"%s\"\n", disp(a, "u_ratio"), disp(a, "u_thresh"));
    CHECK(!strcmp(disp(a, "x_freq"), "500 hz") && !strcmp(disp(a, "c_input"), "0 db"), "insert: Freq \"500 hz\", Input \"0 db\"");
    /* band 1: set a threshold, then tap BAND 2 (MPC sends the button's press and release) */
    a->setP(a, idx("c_thresh"), 0.5f);   /* -30 db */
    reset();
    a->setP(a, idx("sel2"), 1.0f);
    run(a, 40, 0, 100);   /* ~116 ms: the 100 ms poll and the 1024-frame report both ran */
    a->setP(a, idx("sel2"), 0.0f);
    CHECK(a->getP(a, idx("band")) == 0.5f, "tap BAND 2 -> band = Band 2 (%.2f)", a->getP(a, idx("band")));
    CHECK(automated[idx("band")] > 0 && fabsf(autoval[idx("band")] - 0.5f) < 1e-4, "band reported to MPC (tabs picture switches)");
    CHECK(automated[idx("c_thresh")] > 0 && fabsf(autoval[idx("c_thresh")] - 1.0f) < 1e-4, "c_thresh knob reported at band 2's value (0 db)");
    CHECK(updates > 0, "an UpdateDisplay followed, so the value text is re-read (%d)", updates);
    CHECK(!strcmp(disp(a, "range_text"), "500 hz - 4k") && !strcmp(disp(a, "c_thresh"), "0 db"),
          "band 2 reads: range \"%s\", Thresh \"%s\"", disp(a, "range_text"), disp(a, "c_thresh"));
    /* back to band 1 with a Q-Link on "band" (4 events per step, qlink_ticks) */
    reset();
    float b = a->getP(a, idx("band"));
    for (int k = 0; k < 4; k++) { a->setP(a, idx("band"), b - 1.0f / 128); b = a->getP(a, idx("band")); }
    run(a, 40, 0, 100);
    CHECK(a->getP(a, idx("band")) == 0.0f && fabsf(autoval[idx("c_thresh")] - 0.5f) < 1e-4 && updates > 0,
          "Q-Link back to Band 1: Thresh knob reported at -30 db again (%.3f), UpdateDisplay %d", autoval[idx("c_thresh")], updates);
    /* Upward Thresh above Comp Thresh snaps back */
    reset();
    a->setP(a, idx("u_thresh"), 0.9f);   /* -6 db > -30 db */
    run(a, 10, 0, 100);
    CHECK(fabsf(a->getP(a, idx("u_thresh")) - 0.5f) < 1e-4 && automated[idx("u_thresh")] > 0,
          "Upward Thresh -6 db above Comp Thresh -30 db: snaps to %s and is reported", disp(a, "u_thresh"));
    /* meter: a loud 100 hz tone on band 1 -> level moves, the green ends below the grey line (compression) */
    a->setP(a, idx("c_ratio"), 0.4f);   /* 1 + 19 * 0.16 = 4:1 */
    reset();
    run(a, 200, 0.5f, 100);
    float out = a->getP(a, idx("m_out")), in = a->getP(a, idx("m_in")), ct = a->getP(a, idx("m_ct"));
    CHECK(automated[idx("m_out")] > 0 && in > out, "meter on band 1: in %.2f, out %.2f (compressed), comp line %.2f", in, out, ct);
    CHECK(automated[idx("c_input")] == 0, "a moving meter doesn't re-report the knobs");
    CHECK(updates == 0, "a moving meter asks for no UpdateDisplay (DISPLAY_REV_NO_UPDATE)");
    /* Pass */
    a->setP(a, idx("bypass"), 1.0f);
    CHECK(a->getP(a, idx("bypass")) == 1.0f && !strcmp(disp(a, "bypass"), "On"), "Pass tapped: bypassed (red image)");
    a->setP(a, idx("bypass"), 0.0f);
    /* factory presets in MPC's PRESET menu (VST programs) */
    char nm[64];
    int np = a->np;
    a->d(a, 29, 1, 0, nm, 0);
    CHECK(np == 27 && !strcmp(nm, "Vox: Pop Lead"), "PRESET menu: %d programs, #1 \"%s\"", np, nm);
    reset();
    a->d(a, 2, 0, 13, 0, 0);   /* effSetProgram: Drm: NY Smash */
    run(a, 20, 0, 100);
    a->d(a, 5, 0, 0, nm, 0);
    CHECK(a->d(a, 3, 0, 0, 0, 0) == 13 && !strcmp(nm, "Drm: NY Smash"), "pick #13: current program %d \"%s\"", (int)a->d(a, 3, 0, 0, 0, 0), nm);
    CHECK(automated[idx("c_thresh")] > 0 && automated[idx("x_freq")] > 0 && updates > 0,
          "the knobs are reported to MPC and the screen redrawn (c_thresh %d, x_freq %d, UpdateDisplay %d)", automated[idx("c_thresh")],
          automated[idx("x_freq")], updates);
    CHECK(!strcmp(disp(a, "c_thresh"), "-21.5 db") && !strcmp(disp(a, "x_freq"), "100 hz") && !strcmp(disp(a, "u_thresh"), "-27.5 db"),
          "NY Smash on Band 1: Thresh \"%s\", Split \"%s\", Up Thresh \"%s\"", disp(a, "c_thresh"), disp(a, "x_freq"), disp(a, "u_thresh"));
    /* a tweak, then save the project (chunk) and load it into a fresh instance */
    a->setP(a, idx("c_ratio"), 0.5f);
    char ratio[64]; strcpy(ratio, disp(a, "c_ratio"));
    void *chunk = 0;
    intptr_t len = a->d(a, 23, 0, 0, &chunk, 0);
    char saved[4096];
    memcpy(saved, chunk, len < 4096 ? len : 4096);
    AEffect *b2 = VSTPluginMain(host);
    a->d(a, 2, 0, 13, 0, 0);   /* the host re-picking the current program keeps the tweak */
    CHECK(!strcmp(disp(a, "c_ratio"), ratio), "re-picking the current preset keeps a tweak (ratio %s)", disp(a, "c_ratio"));
    b2->d(b2, 24, 0, len, saved, 0);
    run(b2, 20, 0, 100);
    b2->d(b2, 5, 0, 0, nm, 0);
    CHECK(b2->d(b2, 3, 0, 0, 0, 0) == 13 && !strcmp(nm, "Drm: NY Smash") && !strcmp(disp(b2, "c_ratio"), ratio),
          "project reload: menu shows \"%s\", the tweak (ratio %s) is back", nm, disp(b2, "c_ratio"));
    /* the preset selector on the plugin's own screen (shared/preset_nav.h): arrows, the name -> browser, a pick.
     * Taps on the same button are 260 ms apart: the wrapper ignores a second press within 250 ms (one tap reaches a
     * plugin as two presses on the Live III). */
    {
        struct timespec gap = {0, 260 * 1000000L};
#define TAP(k) do { nanosleep(&gap, NULL); a->setP(a, idx(k), 1.0f); run(a, 20, 0, 100); } while (0)
        int pre = (int)a->d(a, 3, 0, 0, 0, 0);
        reset();
        TAP("preset_next");
        a->d(a, 5, 0, 0, nm, 0);
        CHECK(a->d(a, 3, 0, 0, 0, 0) == pre + 1 && !strcmp(nm, "Drm: Bus Glue") && automated[idx("preset")] > 0 &&
              automated[idx("c_thresh")] > 0 && updates > 0, "> on screen: \"%s\", name + knobs reported, screen redrawn", nm);
        TAP("preset_prev"); TAP("preset_prev");
        CHECK(a->d(a, 3, 0, 0, 0, 0) == pre - 1, "< < : back past the one before (%d)", (int)a->d(a, 3, 0, 0, 0, 0));
        a->d(a, 2, 0, 0, 0, 0); run(a, 20, 0, 100);   /* Init from the PRESET menu */
        TAP("preset_prev");
        CHECK(a->d(a, 3, 0, 0, 0, 0) == 26, "< on Init wraps to the last preset (%d)", (int)a->d(a, 3, 0, 0, 0, 0));
        reset();
        TAP("preset_list");
        float hl = a->getP(a, idx("pre_hl"));
        CHECK(a->getP(a, idx("browsing")) == 1.0f && automated[idx("browsing")] > 0 && lroundf(hl * 27) == 27 && updates > 0,
              "the name opens the browser, highlighting the current preset (%ld)", lroundf(hl * 27));
        reset();
        TAP("pick19");   /* Bass: 808 Control */
        a->d(a, 5, 0, 0, nm, 0);
        CHECK(a->d(a, 3, 0, 0, 0, 0) == 18 && !strcmp(nm, "Bass: 808 Control") && a->getP(a, idx("browsing")) == 0.0f &&
              a->getP(a, idx("pre_hl")) == 0.0f && automated[idx("c_thresh")] > 0,
              "a pick loads \"%s\" and closes the browser", nm);
        TAP("preset_list"); TAP("preset_list");
        CHECK(a->getP(a, idx("browsing")) == 0.0f && a->d(a, 3, 0, 0, 0, 0) == 18, "tapping the name (or the backdrop) again closes it");
        a->setP(a, idx("pick99"), 1.0f); a->setP(a, idx("preset"), NAN); a->setP(a, idx("preset"), 9.0f);
        run(a, 4, 0, 100);
        CHECK(a->d(a, 3, 0, 0, 0, 0) >= 0 && a->d(a, 3, 0, 0, 0, 0) < 27, "garbage on the selector's keys is ignored or clamped");
#undef TAP
    }
    b2->d(b2, 1, 0, 0, 0, 0);
    a->d(a, 1, 0, 0, 0, 0);
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
