/* Poli-q mock MPC host: links the real vst2_wrap.c the way MPC loads it and checks what the screen would see.
 * MPC keeps the value it last sent or was told (audioMasterAutomate) per parameter, and while the plugin's screen is up
 * it keeps asking for the value text of what it shows (effGetParamDisplay). Covered: the knobs' text, the analyser
 * reaching the screen (and only while it is up), the Analyzer switch (hide, show again, keeps showing), the curve and
 * dots following a knob, the Q-Links and the 1-4 / 5-8 bank (names, values), Pass on a Q-Link, the type browser,
 * presets from MPC's menu, and save + reload through the chunk. Build: test/run_tests.sh */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void *p;
    void (*setP)(AEffect *, int32_t, float);
    float (*getP)(AEffect *, int32_t);
    int32_t np, npar, ni, no, flags;
    intptr_t r1, r2;
    int32_t a, b, c;
    float io;
    void *obj, *user;
    int32_t uid, ver;
    void (*pr)(AEffect *, float **, float **, int32_t);
    void *pdr;
    char f[56];
};
extern AEffect *VSTPluginMain(cb);
static int automated[NPARAMS], updates, fails, checks;
static float mpc[NPARAMS];   /* what MPC believes each value is */
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)p;
    if (op == 0 && i >= 0 && i < NPARAMS) { automated[i]++; mpc[i] = o; }
    if (op == 42) updates++;
    if (op == 1) return 2400;
    return 0;
}
#define CHECK(c, ...) do { ++checks; printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; fprintf(stderr, "no key %s\n", k); exit(2); }
static void setp(AEffect *a, const char *k, float v) { a->setP(a, idx(k), v); mpc[idx(k)] = v; }
static uint32_t rng = 7;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) * (1.0f / 16777216.0f)) * 2 - 1; }
static const char *disp(AEffect *a, const char *k) { static char b[16][64]; static int n; char *r = b[n++ % 16]; r[0] = 0; a->d(a, 7, idx(k), 0, r, 0); return r; }
static const char *pname(AEffect *a, const char *k) { static char b[4][64]; static int n; char *r = b[n++ % 4]; r[0] = 0; a->d(a, 8, idx(k), 0, r, 0); return r; }
static int screen_up = 1;
static long blocks_run;
/* MPC with the screen up: the knobs' value text, every 100 ms */
static void paint(AEffect *a) {
    static const char *const w[3] = {"f", "g", "q"};
    char k[16];
    for (int b = 1; b <= 8; b++)
        for (int j = 0; j < 3; j++) { char t[64]; snprintf(k, sizeof k, "%s%d", w[j], b); a->d(a, 7, idx(k), 0, t, 0); }
}
static void run(AEffect *a, int blocks, float amp) {
    float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
    for (int k = 0; k < blocks; k++) {
        for (int n = 0; n < 128; n++) I0[n] = I1[n] = amp * noise();
        a->pr(a, in, o, 128);
        if (screen_up && ++blocks_run % 34 == 0) paint(a);
    }
}
static void reset(void) { memset(automated, 0, sizeof automated); updates = 0; }
static int spec_reports(void) { int s = 0; char k[16]; for (int j = 1; j <= 40; j++) { snprintf(k, sizeof k, "sp%02d", j); s += automated[idx(k)]; } return s; }
static float spec_mpc(void) { float s = 0; char k[16]; for (int j = 1; j <= 40; j++) { snprintf(k, sizeof k, "sp%02d", j); s += mpc[idx(k)]; } return s; }
static int opt(const char *k) { int i = idx(k); return (int)lroundf(mpc[i] * (PARAMS[i].nopts - 1)); }

int main(void) {
    AEffect *a = VSTPluginMain(host);
    for (int i = 0; i < NPARAMS; i++) mpc[i] = a->getP(a, i);   /* MPC reads everything when it inserts the plugin */
    run(a, 40, 0);
    CHECK(!strcmp(disp(a, "f3"), "200Hz") && !strcmp(disp(a, "g3"), "0.0dB") && !strcmp(disp(a, "q3"), "1.0") && !strcmp(disp(a, "g1"), "--"),
          "insert: band 3 knobs read \"%s\" \"%s\" \"%s\", the Low Cut's Gain \"%s\"", disp(a, "f3"), disp(a, "g3"), disp(a, "q3"), disp(a, "g1"));
    CHECK(a->getP(a, idx("g3")) == 0.5f, "Gain 0 dB = the knob's middle (pointer straight up)");
    CHECK(!strcmp(pname(a, "qf1"), "1 Freq") && !strcmp(pname(a, "qt4"), "4 Type") && !strcmp(disp(a, "qf3"), "200 Hz"),
          "Q-Links: \"%s\" .. \"%s\", value text \"%s\"", pname(a, "qf1"), pname(a, "qt4"), disp(a, "qf3"));

    /* the analyser reaches the screen, without screen rebuilds */
    reset();
    run(a, 1500, 0.3f);
    CHECK(spec_reports() > 200 && spec_mpc() > 8 && updates == 0, "screen up: the spectrum reaches MPC (%d column reports in 4.4 s), no screen rebuilds (%d)",
          spec_reports(), updates);

    /* Analyzer switch: off hides it (columns sent to 0, then nothing), on shows it again and it keeps showing */
    reset();
    setp(a, "analyzer", 0.0f);
    run(a, 60, 0.3f);
    CHECK(spec_mpc() == 0 && opt("analyzer") == 0, "Analyzer off: MPC's columns are all 0 (and when=analyzer:On hides them)");
    reset();
    run(a, 600, 0.3f);
    CHECK(spec_reports() == 0, "... nothing more is sent while it is off (%d)", spec_reports());
    setp(a, "analyzer", 1.0f);
    run(a, 60, 0.3f);
    const float shown = spec_mpc();
    int stays = 1;
    for (int k = 0; k < 20; k++) { run(a, 170, 0.3f); if (spec_mpc() < shown * 0.5f) stays = 0; }
    CHECK(shown > 8 && stays, "Analyzer on again: it is back within 0.17 s (%.1f) and still there 10 s later (%.1f)", shown, spec_mpc());
    /* the screen closed (MPC stops asking for value text): the analyser stops; opened again: it runs */
    screen_up = 0;
    run(a, 900, 0.3f);
    reset();
    run(a, 600, 0.01f);
    CHECK(spec_reports() == 0, "screen closed for 2.6 s: nothing sent any more (%d)", spec_reports());
    screen_up = 1;
    paint(a);
    reset();
    run(a, 60, 0.01f);
    CHECK(spec_reports() > 0, "screen opened again: the spectrum moves again (%d reports in 0.17 s)", spec_reports());

    /* curve and dots follow a knob */
    reset();
    setp(a, "g4", 0.5f + 12.0f / 36);
    run(a, 50, 0);
    int curve = 0;
    char k[16];
    for (int j = 1; j <= 80; j++) { snprintf(k, sizeof k, "cv%02d", j); curve += automated[idx(k)]; }
    const float dotDb = -18 + 36.0f * lroundf(mpc[idx("dy4")] * 43) / 42;   /* frame -> where the strip draws it */
    CHECK(curve > 5 && automated[idx("dy4")] > 0 && fabsf(dotDb - 12) < 0.5f && updates == 0,
          "Gain 4 to +12: %d curve columns and the dot (drawn at %.1f dB) reach MPC, no rebuild", curve, dotDb);
    reset();
    float f = a->getP(a, idx("f4"));
    setp(a, "f4", f + 0.2f);
    run(a, 50, 0);
    CHECK(automated[idx("dx4")] > 0 && opt("dx4") == 1 + (int)lroundf((f + 0.2f) * 39), "Freq 4 moved: the dot moves to place %d (when=dx4)", opt("dx4"));
    reset();
    setp(a, "on4", 0.0f);
    run(a, 50, 0);
    CHECK(opt("dx4") > 0 && lroundf(mpc[idx("dy4")] * 43) == 43, "band 4 off: its dot turns into a hollow ring (the strip's last frame)");
    setp(a, "on4", 1.0f);
    run(a, 10, 0);

    /* Q-Links: 10 events of 1/128 on "1 Freq"; then the bank switch */
    reset();
    float q = mpc[idx("qf1")];
    for (int n = 0; n < 10; n++) { a->setP(a, idx("qf1"), q + 1.0f / 128); q = a->getP(a, idx("qf1")); mpc[idx("qf1")] = q; run(a, 3, 0); }
    run(a, 20, 0);
    CHECK(fabsf(a->getP(a, idx("f1")) - a->getP(a, idx("qf1"))) < 1e-5f && fabsf(mpc[idx("f1")] - a->getP(a, idx("f1"))) < 1e-5f &&
          automated[idx("f1")] > 0, "Q-Link 1 Freq: 10 events move band 1 and its knob on screen follows (MPC has %.4f)", mpc[idx("f1")]);
    CHECK(fabsf(q - (0.5f * 0 + mpc[idx("qf1")])) < 1e-6f, "the Q-Link lands where MPC put it");
    reset();
    setp(a, "qbank", 1.0f);
    run(a, 30, 0);
    CHECK(!strcmp(pname(a, "qf1"), "5 Freq") && !strcmp(pname(a, "qq2"), "6 Q") && updates > 0,
          "bank 5-8: MPC is asked to re-read (%d) and the Q-Links are \"%s\", \"%s\"", updates, pname(a, "qf1"), pname(a, "qq2"));
    CHECK(fabsf(mpc[idx("qf1")] - a->getP(a, idx("f5"))) < 1e-5f && fabsf(mpc[idx("qg4")] - a->getP(a, idx("g8"))) < 1e-5f,
          "MPC's Q-Link values are now bands 5-8's");
    reset();
    float g = mpc[idx("qg2")];
    for (int n = 0; n < 8; n++) { a->setP(a, idx("qg2"), g - 1.0f / 128); g = a->getP(a, idx("qg2")); mpc[idx("qg2")] = g; }
    run(a, 20, 0);
    CHECK(fabsf((a->getP(a, idx("g6")) - 0.5f) * 36 + 8 * 36.0f / 128) < 0.01f && automated[idx("g6")] > 0 && a->getP(a, idx("g2")) == 0.5f,
          "bank 5-8: 8 events down on \"6 Gain\" = %.2f dB on band 6 (band 2 untouched)", (a->getP(a, idx("g6")) - 0.5f) * 36);
    setp(a, "qbank", 0.0f);
    run(a, 10, 0);

    /* Pass on a Q-Link-style nudge needs 4 events (qlink_ticks); a tap flips it */
    float p = mpc[idx("bypass")];
    for (int n = 0; n < 3; n++) { a->setP(a, idx("bypass"), p + 1.0f / 128); p = a->getP(a, idx("bypass")); }
    CHECK(a->getP(a, idx("bypass")) == 0.0f, "Pass: 3 small nudges don't flip it");
    setp(a, "bypass", 1.0f);
    run(a, 10, 0);
    {
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        int same = 1;
        setp(a, "g3", 1.0f);
        run(a, 40, 0);
        for (int b = 0; b < 20; b++) {
            for (int n = 0; n < 128; n++) I0[n] = I1[n] = (float)((int)(noise() * 3000)) / 32768.0f;
            a->pr(a, in, o, 128);
            for (int n = 0; n < 128; n++) if (L[n] != I0[n]) same = 0;
        }
        CHECK(same, "Pass red (bypassed): the input comes out untouched (bit-exact, no delay)");
        setp(a, "g3", 0.5f);
    }
    setp(a, "bypass", 0.0f);
    run(a, 10, 0);

    /* the type browser */
    struct timespec gap = {0, 260 * 1000000L};
#define TAP(key) do { nanosleep(&gap, NULL); a->setP(a, idx(key), 1.0f); run(a, 20, 0); } while (0)
    reset();
    TAP("topen2");
    CHECK(opt("tb_open") == 1 && opt("tsel") == 2 && opt("tb_hl") == 2, "tap band 2's type name: the browser opens (for band 2, Low Shelf lit)");
    TAP("tpick12");
    CHECK(opt("t2") == 11 && opt("tb_open") == 0 && !strcmp(disp(a, "g2"), "--"), "pick Notch: band 2 is a Notch (Gain \"%s\"), the browser is gone",
          disp(a, "g2"));

    /* presets from MPC's PRESET menu */
    char nm[64] = "";
    a->d(a, 29, 36, 0, nm, 0);
    CHECK(a->np == 43 && !strcmp(nm, "FX: Telephone"), "PRESET menu: %d programs, #36 \"%s\"", a->np, nm);
    reset();
    a->d(a, 2, 0, 36, 0, 0);
    run(a, 20, 0);
    CHECK(opt("t1") == 6 && opt("t8") == 10 && automated[idx("t1")] > 0 && automated[idx("f8")] > 0 && !strcmp(disp(a, "f8"), "3.4kHz"),
          "Telephone from the menu: Low Cut 48 / High Cut 48 at %s, and MPC is told", disp(a, "f8"));

    /* 1.1: the dynamics panel and Learn, through the wrapper */
    reset();
    setp(a, "dsel", 5.0f / 7);   /* band 6 */
    run(a, 20, 0);
    CHECK(automated[idx("dthr")] > 0 && automated[idx("drng")] > 0 && !strcmp(disp(a, "dthr"), "-30dB") && !strcmp(disp(a, "drng"), "Off"),
          "Dynamics band 6: its knobs reported, \"%s\" \"%s\" \"%s\" \"%s\"", disp(a, "dthr"), disp(a, "drng"), disp(a, "datt"), disp(a, "drel"));
    setp(a, "drng", (-6.0f + 18) / 36);
    setp(a, "dthr", (-40.0f + 60) / 60);
    run(a, 10, 0);
    reset();
    run(a, 120, 0.5f);   /* loud noise: band 6 dips, the gain-now bar moves without a screen rebuild */
    CHECK(!strcmp(disp(a, "drng"), "-6.0dB") && automated[idx("dact")] > 0 && mpc[idx("dact")] < 0.4f && updates == 0,
          "Range -6 over -40 dB on loud noise: gain-now bar reported (%.2f of its travel), no screen rebuild", mpc[idx("dact")]);
    setp(a, "dsel", 2.0f / 7);   /* band 3: its own (static) dynamics show */
    run(a, 10, 0);
    CHECK(!strcmp(disp(a, "drng"), "Off"), "band 3 selected: its own Range \"%s\"", disp(a, "drng"));
    reset();
    setp(a, "learn", 1.0f);
    int lb = 0;
    float progress = 0;
    while (lb < 4000 && opt("learn") != 0) { run(a, 1, 0.2f); lb++; if (mpc[idx("learn_prog")] > progress) progress = mpc[idx("learn_prog")]; }
    CHECK(opt("learn") == 0 && lb > 1800 && lb < 2500 && progress > 0.5f && automated[idx("voice")] > 0 && updates > 0,
          "Learn on noise: progress reached MPC (%.2f), it switched itself off after %.1f s, the result text was reported", progress, lb * 128 / 44100.0);

    /* save + reload */
    setp(a, "g5", 0.25f);
    setp(a, "qbank", 1.0f);
    run(a, 700, 0);   /* silence: band 6's dynamic dip releases (the live gain is not state, the settings are) */
    void *chunk = 0;
    intptr_t len = a->d(a, 23, 0, 0, &chunk, 0);
    AEffect *b = VSTPluginMain(host);
    b->d(b, 24, 0, len, chunk, 0);
    b->d(b, 2, 0, b->d(b, 3, 0, 0, 0, 0), 0, 0);   /* JUCE re-selects the current program after a load */
    int same = 1;
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].momentary || !strncmp(PARAMS[i].key, "sp", 2)) continue;
        if (fabsf(a->getP(a, i) - b->getP(b, i)) > 2e-4f) { same = 0; printf("     %s %.4f vs %.4f\n", PARAMS[i].key, a->getP(a, i), b->getP(b, i)); }
    }
    b->d(b, 5, 0, 0, nm, 0);
    CHECK(len > 100 && same && !strcmp(nm, "FX: Telephone"), "save + reload (%d bytes): every value, the bank, and the name \"%s\" (tweak kept)", (int)len, nm);
    b->d(b, 1, 0, 0, 0, 0);
    a->d(a, 1, 0, 0, 0, 0);
#undef TAP
    printf(fails ? "FAILED: %d of %d checks\n" : "PASSED: %d checks\n", fails ? fails : checks, checks);
    return fails != 0;
}
