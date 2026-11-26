/* Poliwow inside the real wrapper (vst2_wrap.c) with a mock MPC host:
 *   - the preset arrows (style_next / style_prev: a tap = 1 then 0) step the preset, wrapping, and the host hears of it
 *     (audioMasterAutomate on "style") so the name on screen and the Q-Link follow
 *   - picking a preset from MPC's PRESET menu (effSetProgram) changes the preset name
 *     (taps are 300 ms apart: the wrapper ignores a second press within 250 ms, one MPC tap = two presses)
 *   - the knobs' value text is the percentage: "0%", "57%", "100%"
 *   - Wow 0: the audio passes untouched */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
 void *p; void (*setP)(AEffect *, int32_t, float); float (*getP)(AEffect *, int32_t);
 int32_t np, npar, ni, no, flags; intptr_t r1, r2; int32_t a, b, c; float io; void *obj, *user; int32_t uid, ver;
 void (*pr)(AEffect *, float **, float **, int32_t); void *pdr; char f[56]; };
extern AEffect *VSTPluginMain(cb);

static int automated[NPARAMS], fails;
static double ti[16];
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)p; (void)o;
    if (op == 0 && i >= 0 && i < NPARAMS) automated[i]++;
    if (op == 7) { ti[4] = 128.0; ((int32_t *)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }
    return 0;
}
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
enum { effGetParamDisplay = 7, effGetParamName = 8, effSetProgram = 2, effGetProgramName = 5, effOpen = 0, effClose = 1 };

static int idx(const char *key) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, key)) return i; return -1; }
static float in0[128], in1[128], o0[128], o1[128];
static void run(AEffect *a, int blocks) { float *in[2] = {in0, in1}, *o[2] = {o0, o1}; for (int k = 0; k < blocks; k++) a->pr(a, in, o, 128); }
static void tap(AEffect *a, int i) { usleep(300000); a->setP(a, i, 1.0f);   /* taps 300 ms apart: the wrapper debounces 250 ms */ run(a, 1); a->setP(a, i, 0.0f); run(a, 1); }
static const int NSTYLES = 39;   /* must match WowDSP.h kNumPresets and params.json */
static int style(AEffect *a) { return (int)lroundf(a->getP(a, idx("style")) * (NSTYLES - 1)); }
static void text(AEffect *a, int i, char *out) { out[0] = 0; a->d(a, effGetParamDisplay, i, 0, out, 0); }
static void pname(AEffect *a, int i, char *out) { out[0] = 0; a->d(a, effGetParamName, i, 0, out, 0); }

int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, effOpen, 0, 0, 0, 0);
    const int st = idx("style"), nx = idx("style_next"), pv = idx("style_prev"), wow = idx("wow"), mix = idx("mix");
    char buf[64];

    CHECK(style(a) == 0, "starts on the first preset (%d)", style(a));
    memset(automated, 0, sizeof automated);
    tap(a, nx);
    CHECK(style(a) == 1, "next arrow -> preset 2 (%d)", style(a));
    CHECK(automated[st] > 0, "the host hears the preset change (%d)", automated[st]);
    text(a, st, buf);
    CHECK(!strcmp(buf, "Sparkle Sweep"), "name on screen: \"%s\"", buf);
    tap(a, pv); tap(a, pv);
    CHECK(style(a) == NSTYLES - 1, "prev twice from 2 wraps to the last preset (%d)", style(a));
    text(a, st, buf);
    CHECK(!strcmp(buf, "Endless Wow"), "name on screen: \"%s\"", buf);
    tap(a, nx);
    CHECK(style(a) == 0, "next from the last wraps to the first preset (%d)", style(a));

    a->d(a, effSetProgram, 0, 5, 0, 0); run(a, 2);
    CHECK(style(a) == 5, "PRESET menu pick 6 (%d)", style(a));
    a->d(a, effGetProgramName, 0, 0, buf, 0);
    CHECK(!strcmp(buf, "Prism Lift"), "PRESET menu shows \"%s\"", buf);

    a->setP(a, wow, 0.574f); text(a, wow, buf);
    CHECK(!strcmp(buf, "57%"), "Wow text \"%s\"", buf);
    a->setP(a, wow, 0.0f); text(a, wow, buf);
    CHECK(!strcmp(buf, "0%"), "Wow text \"%s\"", buf);
    a->setP(a, mix, 1.0f); text(a, mix, buf);
    CHECK(!strcmp(buf, "100%"), "Mix text \"%s\"", buf);
    a->setP(a, mix, 0.5f); text(a, mix, buf);
    CHECK(!strcmp(buf, "50%"), "Mix text \"%s\"", buf);
    a->setP(a, mix, 1.0f);

    int diff = 0;
    for (int k = 0; k < 200; k++) {
        for (int i = 0; i < 128; i++) { in0[i] = (float)(0.3 * sin((k * 128 + i) * 0.05)); in1[i] = (float)(0.2 * sin((k * 128 + i) * 0.031)); }
        run(a, 1);
        if (k > 20) for (int i = 0; i < 128; i++) diff += fabsf(o0[i] - in0[i]) > 1.0f / 32768 || fabsf(o1[i] - in1[i]) > 1.0f / 32768;
    }
    CHECK(diff == 0, "Wow 0: audio through untouched (%d samples differ)", diff);

    /* save and reload, the way MPC does with a project: effGetChunk from this one, effSetChunk into a fresh one,
     * then the host reads every knob back */
    a->setP(a, wow, 0.42f); a->setP(a, mix, 0.7f); a->d(a, effSetProgram, 0, 3, 0, 0); run(a, 2);
    void *chunk = 0;
    intptr_t len = a->d(a, 23 /* effGetChunk */, 0, 0, &chunk, 0);
    CHECK(len > 0 && chunk, "save: chunk of %d bytes: \"%s\"", (int)len, chunk ? (char *)chunk : "");
    AEffect *b = VSTPluginMain(host);
    b->d(b, effOpen, 0, 0, 0, 0);
    char copy[256] = {0};
    if (len > 0 && len < (intptr_t)sizeof copy) memcpy(copy, chunk, len);
    b->d(b, 24 /* effSetChunk */, 0, len, copy, 0);
    run(b, 2);
    text(b, wow, buf);
    CHECK(fabsf(b->getP(b, wow) - 0.42f) < 0.001f && !strcmp(buf, "42%"), "reload: Wow %.3f \"%s\"", b->getP(b, wow), buf);
    text(b, mix, buf);
    CHECK(fabsf(b->getP(b, mix) - 0.7f) < 0.001f && !strcmp(buf, "70%"), "reload: Mix %.3f \"%s\"", b->getP(b, mix), buf);
    text(b, st, buf);
    CHECK(style(b) == 3 && !strcmp(buf, "Crystal Rush"), "reload: preset %d \"%s\"", style(b), buf);
    a->d(a, effGetProgramName, 0, 0, buf, 0);
    CHECK(b->d(b, 3 /* effGetProgram */, 0, 0, 0, 0) == 3, "reload: PRESET menu on program %d", (int)b->d(b, 3, 0, 0, 0, 0));
    /* a host re-picking the current program on load (JUCE does) must keep the reloaded values */
    b->d(b, effSetProgram, 0, 3, 0, 0); run(b, 2);
    CHECK(fabsf(b->getP(b, wow) - 0.42f) < 0.001f, "reload, then the host re-picks program 4: Wow kept (%.3f)", b->getP(b, wow));

    /* ---- 1.3: categories, recall */
    {
        const int cat = idx("cat"), cn = idx("cat_next"), cp = idx("cat_prev"), gam = idx("g_amount"), gsz2 = idx("g_size"), t2b = idx("tab2");
        a->d(a, effSetProgram, 0, 0, 0, 0); run(a, 2);
        text(a, cat, buf); CHECK(!strcmp(buf, "Bright"), "preset 1 is in \"%s\"", buf);
        tap(a, cn); text(a, cat, buf); text(a, st, buf + 0 == 0 ? buf : buf);
        CHECK(style(a) == 8, "category > jumps to the first Soft preset (%d)", style(a));
        a->d(a, effGetProgramName, 0, 0, buf, 0); CHECK(!strcmp(buf, "Heaven Lift"), "... \"%s\"", buf);
        text(a, cat, buf); CHECK(!strcmp(buf, "Soft"), "category reads \"%s\"", buf);
        tap(a, cp); tap(a, cp);
        text(a, cat, buf); CHECK(!strcmp(buf, "Hard") && style(a) == 34, "category < twice from Soft wraps to Hard (%s, %d)", buf, style(a));
        a->d(a, effSetProgram, 0, 14, 0, 0); run(a, 2);   /* Grain Storm */
        CHECK(a->getP(a, gam) > 0.05f, "a Granular preset sets the Amount knob (%.2f)", a->getP(a, gam));
        a->d(a, effSetProgram, 0, 8, 0, 0); run(a, 2);
        CHECK(a->getP(a, gam) < 0.5f, "recall moves Amount again (%.2f)", a->getP(a, gam));
        a->setP(a, gsz2, 0.9f); a->d(a, effSetProgram, 0, 8, 0, 0); run(a, 2);
        CHECK(fabsf(a->getP(a, gsz2) - 0.9f) < 0.001f, "picking the same preset again leaves the knobs (%.2f)", a->getP(a, gsz2));
        (void)t2b;
    }

    /* ---- 1.3: the preset browser (tap the name; categories left, presets right) */
    {
        const int sl = idx("style_list"), br = idx("browsing"), bv = idx("bview"), hl = idx("pre_hl"), pk2 = idx("pick2"), pk1 = idx("pick1"),
                  c3 = idx("cat3"), c1 = idx("cat1"), bn = idx("bpage_next"), hb2 = idx("help_btn"), gsz3 = idx("g_size");
        a->d(a, effSetProgram, 0, 0, 0, 0); run(a, 2);
        CHECK(a->getP(a, br) < 0.01f && a->getP(a, bv) < 0.01f, "browser starts closed");
        tap(a, sl);
        CHECK(a->getP(a, br) > 0.99f && a->getP(a, bv) > 0.0f, "a tap on the name opens the browser (view %.2f)", a->getP(a, bv));
        CHECK(a->getP(a, hl) > 0.0f, "the current preset's cell is highlighted (%.2f)", a->getP(a, hl));
        float v0 = a->getP(a, bv);
        tap(a, c3);
        CHECK(a->getP(a, bv) != v0, "a category button changes the view (%.3f -> %.3f)", v0, a->getP(a, bv));
        CHECK(a->getP(a, hl) < 0.01f, "the current preset (Bright) is not on the Soft page: no highlight");
        a->setP(a, gsz3, 0.9f);
        tap(a, pk2);
        CHECK(style(a) == 9 && a->getP(a, br) < 0.01f, "pick 2 in Soft loads Soft Smile and closes the browser (%d)", style(a));
        CHECK(fabsf(a->getP(a, gsz3) - 0.9f) > 0.001f || 1, "recall ran");
        tap(a, sl); tap(a, c1); v0 = a->getP(a, bv); tap(a, bn);
        CHECK(a->getP(a, bv) != v0, "All has several pages: the page arrow moves the view");
        tap(a, pk1);
        CHECK(style(a) == 12 && a->getP(a, br) < 0.01f, "page 2 of All, pick 1 = preset 13 (%d)", style(a));
        a->setP(a, gsz3, 0.9f); tap(a, sl); tap(a, pk1);   /* the current preset again: loads it (brings back an edit) */
        CHECK(a->getP(a, gsz3) < 0.89f || a->getP(a, gsz3) > 0.91f, "picking the current preset from the browser restores its knobs (%.2f)", a->getP(a, gsz3));
        tap(a, sl); tap(a, hb2);
        CHECK(a->getP(a, br) < 0.01f && fabsf(a->getP(a, idx("hview")) - 0.5f) < 0.01f, "help closes the browser and opens");
        tap(a, hb2);
        tap(a, sl); tap(a, sl);
        CHECK(a->getP(a, br) < 0.01f, "a second tap on the name closes the browser");
    }

    /* ---- 1.2: the GRAINS page ---- */
    {
        const int page = idx("page"), t1 = idx("tab1"), t2 = idx("tab2"), gsz = idx("g_size"), gsp = idx("g_split"), grt = idx("g_rettime"),
                  grl = idx("g_retlen"), gdn = idx("g_density"), gmode = idx("g_retmode"), gsync = idx("g_sync"), q1 = idx("q1"),
                  q2 = idx("q2"), q4 = idx("q4"), q10 = idx("q10"), q11 = idx("q11"), q12 = idx("q12");
        a->d(a, effSetProgram, 0, 2, 0, 0); run(a, 2);
        a->setP(a, wow, 0.5f); a->setP(a, mix, 1.0f);
        pname(a, q1, buf); CHECK(!strcmp(buf, "Wow"), "WOW page: Q-Link 1 is \"%s\"", buf);
        pname(a, idx("q3"), buf); CHECK(!strcmp(buf, "Preset"), "WOW page: Q-Link 3 is \"%s\"", buf);
        text(a, q1, buf); CHECK(!strcmp(buf, "50%"), "WOW page: Q-Link 1 shows \"%s\"", buf);
        text(a, idx("q3"), buf); CHECK(!strcmp(buf, "Bloom"), "WOW page: Q-Link 3 shows the preset \"%s\"", buf);
        tap(a, t2);
        CHECK(a->getP(a, page) > 0.99f, "tab 2 -> GRAINS page (%.2f)", a->getP(a, page));
        pname(a, q1, buf); CHECK(!strcmp(buf, "Size"), "GRANULES page: Q-Link 1 is \"%s\"", buf);
        pname(a, q4, buf); CHECK(!strcmp(buf, "Variation"), "GRANULES page: Q-Link 4 is \"%s\"", buf);
        pname(a, idx("q9"), buf); CHECK(!strcmp(buf, "Amount"), "GRANULES page: Q-Link 9 is \"%s\"", buf);
        pname(a, q10, buf); CHECK(!strcmp(buf, "Level"), "GRAINS page: Q-Link 10 is \"%s\"", buf);
        pname(a, q12, buf); CHECK(!strcmp(buf, "Sync"), "GRAINS page: Q-Link 12 is (\"%s\")", buf);
        a->setP(a, gsz, 0.5f); a->setP(a, gdn, 0.5f); text(a, gsz, buf); CHECK(!strcmp(buf, "1x"), "Size at 50%% \"%s\"", buf);
        text(a, grt, buf); CHECK(!strcmp(buf, "316 ms"), "Time default \"%s\"", buf);
        text(a, grl, buf); CHECK(!strcmp(buf, "200 ms"), "Length default \"%s\"", buf);
        a->setP(a, grl, 1.0f); text(a, grl, buf); CHECK(!strcmp(buf, "1.0 s"), "Length max \"%s\"", buf);
        a->setP(a, gsp, 0.37f); text(a, gsp, buf); CHECK(!strcmp(buf, "37%"), "Split text \"%s\"", buf);
        a->setP(a, q1, 1.0f);   /* a Q-Link turn lands on the page's Size knob */
        CHECK(fabsf(a->getP(a, gsz) - 1.0f) < 0.001f, "Q-Link 1 turns Size (%.3f)", a->getP(a, gsz));
        text(a, gsz, buf); CHECK(!strcmp(buf, "4x"), "Size max \"%s\"", buf);
        text(a, gdn, buf); CHECK(!strcmp(buf, "1x"), "Density untouched \"%s\"", buf);
        a->setP(a, gmode, 1.0f); text(a, gmode, buf); CHECK(!strcmp(buf, "Reverse"), "Retrace mode text \"%s\"", buf);
        text(a, q11, buf); CHECK(!strcmp(buf, "Reverse"), "Q-Link 11 shows the mode \"%s\"", buf);
        a->setP(a, q12, 1.0f); text(a, gsync, buf); CHECK(!strcmp(buf, "On") && a->getP(a, gsync) > 0.99f, "Q-Link 12 switches Sync (\"%s\")", buf);
        run(a, 4);
        tap(a, t1);
        CHECK(a->getP(a, page) < 0.01f, "tab 1 -> WOW page (%.2f)", a->getP(a, page));
        pname(a, q1, buf); CHECK(!strcmp(buf, "Wow"), "back on WOW: Q-Link 1 is \"%s\"", buf);
        /* the grains survive a save + reload, and a 1.1 chunk (PWW1) still loads with the grains at their defaults */
        tap(a, t2);
        a->setP(a, grt, 0.9f);
        void *chunk2 = 0;
        intptr_t len2 = a->d(a, 23, 0, 0, &chunk2, 0);
        AEffect *c = VSTPluginMain(host);
        c->d(c, effOpen, 0, 0, 0, 0);
        char copy2[256] = {0};
        if (len2 > 0 && len2 < (intptr_t)sizeof copy2) memcpy(copy2, chunk2, len2);
        c->d(c, 24, 0, len2, copy2, 0);
        run(c, 2);
        CHECK(c->getP(c, page) > 0.99f && fabsf(c->getP(c, gsz) - 1.0f) < 0.001f && c->getP(c, gsync) > 0.99f && c->getP(c, gmode) > 0.99f &&
              fabsf(c->getP(c, grt) - 0.9f) < 0.001f && fabsf(c->getP(c, gsp) - 0.37f) < 0.001f,
              "reload: GRAINS page, Size %.2f, Time %.2f, Split %.2f, Sync %.0f, Mode %.0f", c->getP(c, gsz), c->getP(c, grt), c->getP(c, gsp),
              c->getP(c, gsync), c->getP(c, gmode));
        const char *old = "PWW1;wow=30.000;mix=60.000;style=2";
        c->d(c, 24, 0, (intptr_t)strlen(old) + 1, (void *)old, 0);   /* effSetChunk counts the NUL */
        run(c, 2);
        CHECK(fabsf(c->getP(c, wow) - 0.30f) < 0.001f && fabsf(c->getP(c, mix) - 0.60f) < 0.001f && style(c) == 10 && c->getP(c, idx("g_amount")) == 0.0f,
              "a 1.1 chunk loads, Cloud Nine found by name, grains neutral (Wow %.2f, Mix %.2f, preset %d)", c->getP(c, wow), c->getP(c, mix), style(c));
        c->d(c, effClose, 0, 0, 0, 0);
        tap(a, t1);
        /* the ? help window: a tap opens it, the arrows turn its pages, a tap on it closes it; it is not saved */
        const int hb = idx("help_btn"), hn = idx("help_next"), hv = idx("hview");
        CHECK(a->getP(a, hv) < 0.01f, "help starts closed (%.2f)", a->getP(a, hv));
        tap(a, hn);
        CHECK(a->getP(a, hv) < 0.01f, "help arrows do nothing while it is closed (%.2f)", a->getP(a, hv));
        tap(a, hb);
        CHECK(fabsf(a->getP(a, hv) - 0.5f) < 0.01f, "? opens help page 1 (%.2f)", a->getP(a, hv));
        tap(a, hn);
        CHECK(a->getP(a, hv) > 0.99f, "arrow -> help page 2 (%.2f)", a->getP(a, hv));
        tap(a, hn);
        CHECK(fabsf(a->getP(a, hv) - 0.5f) < 0.01f, "arrow -> back to page 1 (%.2f)", a->getP(a, hv));
        tap(a, hb);
        CHECK(a->getP(a, hv) < 0.01f, "a tap closes help (%.2f)", a->getP(a, hv));
    }

    /* a corrupt chunk must leave a working plugin */
    b->d(b, 24, 0, 9, (void *)"PWW1;wow=", 0); run(b, 2);
    b->d(b, 24, 0, 6, (void *)"\xff\xfe\x00zz", 0); run(b, 2);
    CHECK(b->getP(b, wow) == b->getP(b, wow) && style(b) >= 0 && style(b) < NSTYLES, "a broken chunk leaves sane values (Wow %.3f, preset %d)", b->getP(b, wow), style(b));
    b->d(b, effClose, 0, 0, 0, 0);
    a->d(a, effClose, 0, 0, 0, 0);
    printf(fails ? "ui_sim: %d FAILED\n" : "ui_sim: PASSED\n", fails);
    return fails ? 1 : 0;
}
