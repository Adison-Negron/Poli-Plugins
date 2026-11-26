/* PoliPlug offline test: loads the built x86 poli_plug.so the way MPC does (dlopen + VSTPluginMain), which in turn
 * loads the real hosted plugins from <dir of poli_plug.so>/hosted/. Checks the rack, the editor proxies, the Q-Link
 * proxies, the audio chain, live values coming back from a hosted plugin, and the saved state.
 *
 *   test/run_tests.sh          (builds everything for x86 with ASan/UBSan and runs this)
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pp_gen.h"

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
enum { effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effGetParamDisplay = 7, effGetParamName = 8,
       effGetChunk = 23, effSetChunk = 24, effGetProgramNameIndexed = 29 };

static int fails, automates[1024], updates;
/* 1.4.2: the Q-Link at Live III Q-Link page sl/4+1, knob sl%4 (0 = top): page 1 = Q-Links 13, 9, 5, 1 (seen on the device) */
#define QS(sl) PI_Q(4 * (3 - (sl) % 4) + (sl) / 4)
static VstTimeInfo ti = {0, 44100, 0, 0, 120, 0, 0, 0, 4, 4, 0, 0, 0, (1 << 10) | (1 << 9)};

static intptr_t master(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)p; (void)o;
    if (op == 0 && idx >= 0 && idx < 1024) automates[idx]++;
    if (op == 42) updates++;
    if (op == 1) return 2400;
    if (op == 7) return (intptr_t)&ti;
    return 0;
}

#define CHECK(cond, ...) do { if (cond) { printf("ok   "); } else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static float in0[128], in1[128], out0[128], out1[128];
static float *ins[2] = {in0, in1}, *outs[2] = {out0, out1};
static double phase;

/* n blocks of a loud 7 kHz tone (sibilance for a de-esser), returns the output RMS */
static double run(AEffect *fx, int blocks, double hz, double amp) {
    double acc = 0;
    for (int b = 0; b < blocks; b++) {
        for (int j = 0; j < 128; j++) {
            in0[j] = in1[j] = (float)(amp * sin(phase));
            phase += 2 * M_PI * hz / 44100;
        }
        fx->processReplacing(fx, ins, outs, 128);
        for (int j = 0; j < 128; j++) acc += out0[j] * out0[j];
        ti.samplePos += 128;
    }
    return sqrt(acc / (blocks * 128.0));
}

static const char *disp(AEffect *fx, int i) {
    static char buf[8][128];
    static int k;
    char *b = buf[k++ & 7];
    memset(b, 0, 128);
    fx->dispatcher(fx, effGetParamDisplay, i, 0, b, 0);
    return b;
}
static const char *pname(AEffect *fx, int i) {
    static char buf[4][128];
    static int k;
    char *b = buf[k++ & 3];
    memset(b, 0, 128);
    fx->dispatcher(fx, effGetParamName, i, 0, b, 0);
    return b;
}
static int opt(AEffect *fx, int i, int nopts) { return (int)lroundf(fx->getParameter(fx, i) * (nopts - 1)); }
/* a tap on a drawn button; the wrapper ignores a second press of the same button within 250 ms (MPC sends one tap as
 * two presses), so repeated taps are spaced like a person's */
static void press(AEffect *fx, int i) {
    static double last[1024];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec * 1e-9;
    if (i >= 0 && i < 1024 && now - last[i] < 0.3) usleep((useconds_t)((0.3 - (now - last[i])) * 1e6));
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (i >= 0 && i < 1024) last[i] = ts.tv_sec + ts.tv_nsec * 1e-9;
    fx->setParameter(fx, i, 1.0f); run(fx, 1, 100, 0); fx->setParameter(fx, i, 0.0f);
}
static void press(AEffect *fx, int i);
static void add(AEffect *fx, int type) {   /* + Add plugin, then the type's button in the list (first page) */
    press(fx, PI_ADD_OPEN);
    run(fx, 2, 100, 0);
    press(fx, PI_PICK(type));
    run(fx, 10, 100, 0);   /* the wrapper reports engine-side changes at most every 1024 frames */
}
static int row_used(AEffect *fx, int r) { return opt(fx, PI_ROW(r) + 1, 2); }

int main(int argc, char **argv) {
    const char *so = argc > 1 ? argv[1] : "build/poli_plug.so";
    void *h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("FAIL dlopen %s: %s\n", so, dlerror()); return 1; }
    AEffect *(*vmain)(audioMasterCallback) = (AEffect * (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    AEffect *fx = vmain(master);
    CHECK(fx && fx->magic == 0x56737450, "PoliPlug loads (%d params)", fx ? fx->numParams : 0);
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0);
    run(fx, 4, 100, 0);
    CHECK(strstr(disp(fx, PI_STATUS), "Empty") != NULL || strstr(disp(fx, PI_STATUS), "3 found") != NULL, "first status: \"%s\"", disp(fx, PI_STATUS));
    CHECK(!row_used(fx, 0), "row 1 hidden while empty");

    /* bypass level: nothing in the chain -> audio passes unchanged */
    double dry = run(fx, 40, 7000, 0.5);
    CHECK(fabs(dry - 0.5 / sqrt(2)) < 0.01, "empty chain passes audio (rms %.3f)", dry);

    /* the list of installed effects */
    press(fx, PI_ADD_OPEN);
    run(fx, 10, 100, 0);
    CHECK(opt(fx, PI_PICKING, 2) == 1, "+ Add plugin opens the effect list");
    CHECK(!strcmp(disp(fx, PI_PICK(0) + 1), "Poli-Ess") && opt(fx, PI_PICK(0) + 2, 2) == 1, "list button 1 = the installed Poli-Ess: \"%s\"",
          disp(fx, PI_PICK(0) + 1));
    CHECK(!strcmp(disp(fx, PI_PICK(1) + 1), "Wide FX") && opt(fx, PI_PICK(3) + 2, 2) == 0,
          "only installed effects are listed, by name (no instrument, no missing file, not PoliPlug itself)");
    CHECK(strstr(disp(fx, PI_STATUS), "3 found") != NULL, "status: \"%s\"", disp(fx, PI_STATUS));
    press(fx, PI_ADD_OPEN);
    run(fx, 4, 100, 0);
    CHECK(opt(fx, PI_PICKING, 2) == 0 && opt(fx, PI_PICK(0) + 2, 2) == 0, "a second tap closes the list");

    add(fx, 0);
    CHECK(row_used(fx, 0), "after + Add: row 1 shown");
    CHECK(!strcmp(disp(fx, PI_ROW(0)), "1  Poli-Ess"), "row 1 text \"%s\"", disp(fx, PI_ROW(0)));
    CHECK(opt(fx, PI_ROW(0) + 2, 3) == 1 && opt(fx, PI_ROW(0) + 3, 3) == 1, "only plugin: both arrows dimmed");
    CHECK(opt(fx, PI_PICKING, 2) == 0, "the list closes after a pick");
    CHECK(automates[PI_ROW(0) + 1] > 0, "row 1 shown is reported to the host (%d)", automates[PI_ROW(0) + 1]);
    /* 1.4.1: on the rack page the Q-Links are on no plugin (Inserts) */
    CHECK(!strcmp(pname(fx, PI_Q(12)), "-") && !strcmp(pname(fx, PI_Q(0)), "-"), "rack: Q-Link 13 is on nothing: \"%s\"", pname(fx, PI_Q(12)));

    /* open it */
    press(fx, PI_ROW(0) + 4);
    run(fx, 2, 100, 0);
    CHECK(opt(fx, PI_VIEW, PP_NVIEW_OPTS) == 1, "Open: view = the plugin's page (%d)", opt(fx, PI_VIEW, PP_NVIEW_OPTS));
    CHECK(opt(fx, PI_EDITING, 2) == 1, "Open: editing on (editor bar shown)");
    CHECK(!strcmp(pname(fx, PI_Q(12)), "Intensity"), "opened: Q-Link 13 is named after the plugin's own Q-Link 13: \"%s\"", pname(fx, PI_Q(12)));
    CHECK(!row_used(fx, 0), "Open: rack rows hidden under the plugin page");
    CHECK(!strcmp(pname(fx, PI_E(0)), "Intensity") && !strcmp(pname(fx, PI_E(2)), "Freq"), "e0/e2 named \"%s\"/\"%s\"",
          pname(fx, PI_E(0)), pname(fx, PI_E(2)));
    CHECK(!strcmp(disp(fx, PI_E(0)), "50 %"), "e0 shows the hosted value text \"%s\"", disp(fx, PI_E(0)));
    CHECK(fabsf(fx->getParameter(fx, PI_E(1)) - 0.5f) < 1e-3, "e1 (Threshold -20 of -40..0) reads 0.5 (%.3f)", fx->getParameter(fx, PI_E(1)));

    /* a touch on the hosted knob and the Q-Link that shows the same parameter */
    memset(automates, 0, sizeof automates);
    fx->setParameter(fx, PI_E(0), 1.0f);
    run(fx, 10, 100, 0);
    CHECK(!strcmp(disp(fx, PI_E(0)), "100 %"), "e0 = 1 sets the hosted Intensity: \"%s\"", disp(fx, PI_E(0)));
    CHECK(fabsf(fx->getParameter(fx, PI_Q(12)) - 1.0f) < 1e-4, "Q-Link 13 follows (%.3f)", fx->getParameter(fx, PI_Q(12)));
    CHECK(automates[PI_Q(12)] > 0, "Q-Link 13's change reported to the host (%d)", automates[PI_Q(12)]);
    fx->setParameter(fx, PI_Q(8), 0.0f);   /* Q-Link 9 = Threshold */
    run(fx, 10, 100, 0);
    CHECK(fabsf(fx->getParameter(fx, PI_E(1))) < 1e-4 && automates[PI_E(1)] > 0, "Q-Link 9 moves e1 (Threshold) and tells the host");

    /* the Diff switch (an Off/On option in the hosted plugin): a tap on its toggle sends 1 */
    fx->setParameter(fx, PI_E(3), 1.0f);
    run(fx, 2, 100, 0);
    CHECK(!strcmp(disp(fx, PI_E(3)), "On"), "e3 (Diff) toggles the hosted option: \"%s\"", disp(fx, PI_E(3)));
    fx->setParameter(fx, PI_E(3), 0.0f);
    run(fx, 2, 100, 0);

    /* audio through the hosted de-esser: a loud 7 kHz tone is turned down, and its meter moves on its own */
    memset(automates, 0, sizeof automates);
    double wet = run(fx, 400, 7000, 0.5);
    CHECK(wet < dry * 0.8, "7 kHz through Poli-Ess at full intensity, threshold min: rms %.3f vs dry %.3f", wet, dry);
    CHECK(fx->getParameter(fx, PI_E(4)) > 0.05f, "hosted meter (e4) reads %.3f", fx->getParameter(fx, PI_E(4)));
    CHECK(automates[PI_E(4)] > 0, "hosted meter's own movement is reported to MPC as e4 (%d times)", automates[PI_E(4)]);
    double low = run(fx, 200, 200, 0.5);
    CHECK(fabs(low - dry) < 0.05, "200 Hz passes (rms %.3f)", low);

    /* back to the rack */
    press(fx, PI_VIEW_BACK);
    run(fx, 2, 100, 0);
    CHECK(opt(fx, PI_VIEW, PP_NVIEW_OPTS) == 0 && opt(fx, PI_EDITING, 2) == 0 && row_used(fx, 0), "Back: rack shown again");
    /* 1.4.1: back on the rack the Q-Links leave the plugin that was open */
    {
        float th = fx->getParameter(fx, PI_E(1));
        CHECK(!strstr(disp(fx, PI_STATUS), "Q-Links") && !strcmp(pname(fx, PI_Q(8)), "-"),
              "Back: the Q-Links leave the plugin (status \"%s\", Q-Link 9 \"%s\")", disp(fx, PI_STATUS), pname(fx, PI_Q(8)));
        fx->setParameter(fx, PI_Q(8), 0.7f);
        run(fx, 4, 100, 0);
        CHECK(fabsf(fx->getParameter(fx, PI_E(1)) - th) < 1e-6, "Back: turning Q-Link 9 leaves the plugin's Threshold alone (%.3f)",
              fx->getParameter(fx, PI_E(1)));
    }

    /* more than 8: fill the rack */
    for (int i = 1; i < PP_MAX_SLOTS; i++) add(fx, 0);
    CHECK(strstr(disp(fx, PI_STATUS), "16/16") != NULL, "16 plugins: \"%s\"", disp(fx, PI_STATUS));
    add(fx, 0);
    CHECK(strstr(disp(fx, PI_STATUS), "full") != NULL, "a 17th is refused: \"%s\"", disp(fx, PI_STATUS));
    CHECK(!strcmp(disp(fx, PI_PAGE_TEXT), "4 / 4"), "rack follows the last add to page \"%s\"", disp(fx, PI_PAGE_TEXT));
    double chain16 = run(fx, 50, 200, 0.5);
    CHECK(chain16 > 0.2, "16 de-essers in series still pass 200 Hz (rms %.3f)", chain16);
    press(fx, PI_PAGE_PREV);
    run(fx, 2, 100, 0);
    CHECK(!strcmp(disp(fx, PI_PAGE_TEXT), "3 / 4"), "page < -> \"%s\"", disp(fx, PI_PAGE_TEXT));
    CHECK(!strcmp(disp(fx, PI_ROW(0)), "9  Poli-Ess"), "page 3 row 1 = slot 9: \"%s\"", disp(fx, PI_ROW(0)));

    /* remove down to 6 and move one across a page boundary */
    for (int i = 0; i < 10; i++) { fx->setParameter(fx, PI_PAGE, 0); run(fx, 1, 100, 0); press(fx, PI_ROW(0) + 7); run(fx, 1, 100, 0); }
    CHECK(strstr(disp(fx, PI_STATUS), "6/16") != NULL, "Bye x10 leaves 6: \"%s\"", disp(fx, PI_STATUS));
    fx->setParameter(fx, PI_PAGE, 1.0f / (PP_PAGES - 1));
    run(fx, 2, 100, 0);
    CHECK(!strcmp(disp(fx, PI_PAGE_TEXT), "2 / 2") && row_used(fx, 1) && !row_used(fx, 2), "page 2 holds slots 5-6 only");
    CHECK(opt(fx, PI_ROW(1) + 3, 3) == 1, "last slot: down arrow dimmed");
    /* give slot 5 a value to follow */
    press(fx, PI_ROW(0) + 4); run(fx, 1, 100, 0);
    fx->setParameter(fx, PI_E(2), 0.25f); run(fx, 1, 100, 0);
    press(fx, PI_VIEW_BACK); run(fx, 1, 100, 0);
    press(fx, PI_ROW(0) + 5);   /* move slot 5 up -> slot 4, page 1 */
    run(fx, 2, 100, 0);
    CHECK(!strcmp(disp(fx, PI_PAGE_TEXT), "1 / 2"), "moving page 2's first row up follows it to page 1");
    usleep(300000);   /* spaced like a person's taps (the wrapper's 250 ms) */
    press(fx, PI_ROW(3) + 4); run(fx, 2, 100, 0);   /* open it where it moved to */
    CHECK(fabsf(fx->getParameter(fx, PI_Q(4)) - 0.25f) < 1e-3, "the moved plugin kept its value (opened, Q-Link 5 Freq %.3f)",
          fx->getParameter(fx, PI_Q(4)));
    usleep(300000);
    press(fx, PI_VIEW_BACK); run(fx, 2, 100, 0);

    /* saved state -> a new instance */
    char *chunk = NULL;
    intptr_t len = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0);
    CHECK(len > 0 && chunk && !strncmp(chunk, "PP4|", 4), "state saved (%d bytes)", (int)len);
    char *copy = strdup(chunk ? chunk : "");
    /* ---- 1.5.0: macro knobs (Inserts tab, right of the rack), Map (learn) / Clear, the Q-Link page dots ---- */
    {
        AEffect *h = vmain(master);
        h->dispatcher(h, effOpen, 0, 0, NULL, 0);
        usleep(300000);
        add(h, 0);   /* Poli-Ess on Inserts row 1 */
        run(h, 10, 100, 0);
        CHECK(opt(h, PI_MK_SHOWN, 2) == 1 && opt(h, PI_MK_PAGE, 5) == 1 && opt(h, PI_MK_MAPST, 3) == 1 &&
              opt(h, PI_MK(0) + 1, 4) == 1 && !strcmp(disp(h, PI_MK(0) + 2), "Empty") && !strcmp(pname(h, PI_Q(12)), "-"),
              "macros: shown on the Inserts rack, Q-Link page 1, Map off, knob 1 empty (\"%s\"), Q-Link 13 \"%s\"",
              disp(h, PI_MK(0) + 2), pname(h, PI_Q(12)));
        press(h, PI_MK_MAPST - 1); run(h, 4, 100, 0);   /* mk_map is just before mk_mapst */
        CHECK(opt(h, PI_MK_MAPST, 3) == 2 && opt(h, PI_MK_CLRST, 2) == 1 && opt(h, PI_MK(0) + 1, 4) == 3 &&
              strstr(disp(h, PI_STATUS), "macro 1") != NULL,
              "Map: on, Clear shown, knob 1 armed (the first empty one), status \"%s\"", disp(h, PI_STATUS));
        float before = h->getParameter(h, PI_MK(0));
        h->setParameter(h, PI_MK(0), 0.9f); run(h, 4, 100, 0);
        CHECK(fabsf(h->getParameter(h, PI_MK(0)) - before) < 1e-6 && opt(h, PI_MK(0) + 1, 4) == 3,
              "Map: turning knob 1 only picks it (nothing moves)");
        usleep(300000);
        press(h, PI_ROW(0) + 4); run(h, 4, 100, 0);   /* open Poli-Ess, move its Threshold */
        CHECK(opt(h, PI_MK_SHOWN, 2) == 0, "on an effect's page the macros are hidden");
        h->setParameter(h, PI_E(1), 0.25f); run(h, 4, 100, 0);
        usleep(300000);
        press(h, PI_VIEW_BACK); run(h, 10, 100, 0);
        CHECK(!strcmp(disp(h, PI_MK(0) + 2), "Threshold") && !strcmp(disp(h, PI_MK(0) + 3), "1 Poli-Ess") &&
              opt(h, PI_MK(0) + 1, 4) == 2 && opt(h, PI_MK_MAPST, 3) == 1 && opt(h, PI_MK_CLRST, 2) == 0 &&
              !strcmp(pname(h, PI_Q(12)), "Threshold") && fabsf(h->getParameter(h, PI_MK(0)) - 0.25f) < 1e-3,
              "the next control moved is mapped: knob 1 = \"%s\" on \"%s\" (%.3f), Map off, Q-Link 13 \"%s\"",
              disp(h, PI_MK(0) + 2), disp(h, PI_MK(0) + 3), h->getParameter(h, PI_MK(0)), pname(h, PI_Q(12)));
        /* the knob drives it, with no screen rebuild; its Q-Link follows */
        run(h, 40, 100, 0);
        updates = 0; automates[PI_Q(12)] = 0;
        for (int k = 0; k <= 30; k++) { h->setParameter(h, PI_MK(0), 0.5f + k / 100.0f); run(h, 2, 100, 0); }
        run(h, 40, 100, 0);
        char thr[64];
        snprintf(thr, sizeof thr, "%s", disp(h, PI_MK(0)));
        CHECK(fabsf(h->getParameter(h, PI_E(1)) - 0.8f) < 1e-3 && updates == 0 && automates[PI_Q(12)] > 0 &&
              fabsf(h->getParameter(h, PI_Q(12)) - 0.8f) < 1e-3 && !strcmp(thr, "-8 dB"),
              "knob 1 drag: Poli-Ess Threshold %.3f (\"%s\"), Q-Link 13 follows, %d UpdateDisplay",
              h->getParameter(h, PI_E(1)), thr, updates);
        automates[PI_MK(0)] = 0;
        h->setParameter(h, PI_Q(12), 0.3f); run(h, 12, 100, 0);   /* the wrapper reports every 1024 samples */
        CHECK(fabsf(h->getParameter(h, PI_MK(0)) - 0.3f) < 1e-3 && automates[PI_MK(0)] > 0,
              "Q-Link 13 (page 1, top knob) turns macro 1, the knob on screen follows (%.3f, %d)", h->getParameter(h, PI_MK(0)),
              automates[PI_MK(0)]);
        /* Q-Link page 2 (Q-Link 14 = macro 5): the dots follow, then a tap on dot 1 goes back */
        h->setParameter(h, PI_Q(13), 0.6f); run(h, 4, 100, 0);
        CHECK(opt(h, PI_MK_PAGE, 5) == 2 && !strcmp(disp(h, PI_MK(0) + 2), "Empty"),
              "turning Q-Link 14 shows Q-Link page 2 (dot 2, knob 1 \"%s\")", disp(h, PI_MK(0) + 2));
        press(h, PI_MK_PAGE + 1); run(h, 4, 100, 0);   /* mk_pg1 follows mk_page */
        CHECK(opt(h, PI_MK_PAGE, 5) == 1 && !strcmp(disp(h, PI_MK(0) + 2), "Threshold"), "dot 1 tapped: page 1 again");
        /* moved on the effect's own page: the macro follows by itself */
        usleep(300000);
        press(h, PI_ROW(0) + 4); run(h, 4, 100, 0);
        h->setParameter(h, PI_E(1), 0.9f); run(h, 4, 100, 0);
        usleep(300000);
        press(h, PI_VIEW_BACK); run(h, 10, 100, 0);
        automates[PI_MK(0)] = 0;
        h->setParameter(h, PI_E(1), 0.95f);   /* (an e write on the rack: the proxies still reach the focused effect) */
        run(h, 40, 100, 0);
        CHECK(fabsf(h->getParameter(h, PI_MK(0)) - 0.95f) < 1e-3 && automates[PI_MK(0)] > 0,
              "the parameter moved elsewhere: macro 1 follows (%.3f)", h->getParameter(h, PI_MK(0)));
        /* save + reload keeps the mapping */
        char *ck = NULL;
        int sl = (int)h->dispatcher(h, effGetChunk, 0, 0, &ck, 0);
        char *st = strdup(ck ? ck : "");
        CHECK(sl > 0 && strstr(st, "|M=0,0.0.1,-") != NULL, "state carries the macros (\"%s\")", strstr(st, "|M=") ? strstr(st, "|M=") : "none");
        AEffect *h2 = vmain(master);
        h2->dispatcher(h2, effOpen, 0, 0, NULL, 0);
        h2->dispatcher(h2, effSetChunk, 0, (intptr_t)strlen(st) + 1, st, 0);
        free(st);
        run(h2, 10, 100, 0);
        CHECK(!strcmp(disp(h2, PI_MK(0) + 2), "Threshold") && fabsf(h2->getParameter(h2, PI_MK(0)) - 0.95f) < 1e-3,
              "reloaded: knob 1 = \"%s\" (%.3f)", disp(h2, PI_MK(0) + 2), h2->getParameter(h2, PI_MK(0)));
        h2->dispatcher(h2, effClose, 0, 0, NULL, 0);
        /* Map, then Clear: knob 1 empty again */
        usleep(300000);
        press(h, PI_MK_MAPST - 1); run(h, 4, 100, 0);
        h->setParameter(h, PI_MK(0), 0.1f); run(h, 4, 100, 0);   /* pick knob 1 */
        press(h, PI_MK_CLRST - 1); run(h, 4, 100, 0);
        CHECK(!strcmp(disp(h, PI_MK(0) + 2), "Empty") && opt(h, PI_MK_MAPST, 3) == 1 && fabsf(h->getParameter(h, PI_E(1)) - 0.95f) < 1e-3,
              "Clear: knob 1 empty, Map off, the effect untouched");
        /* the Parallel tab: no macros (its Q-Links are the Sends) */
        usleep(300000);
        press(h, PI_SECTION + 2); run(h, 10, 100, 0);
        CHECK(opt(h, PI_MK_SHOWN, 2) == 0 && opt(h, PI_MK_MAPST, 3) == 0 && opt(h, PI_MK(0) + 1, 4) == 0 &&
              !strcmp(disp(h, PI_MK(0) + 2), " "), "Parallel tab: the macro column is hidden");
        usleep(300000);
        press(h, PI_SECTION + 1); run(h, 10, 100, 0);
        /* an effect that leaves the rack takes its macros with it */
        usleep(300000);
        press(h, PI_MK_MAPST - 1); run(h, 4, 100, 0);
        usleep(300000);
        press(h, PI_ROW(0) + 4); run(h, 4, 100, 0);
        h->setParameter(h, PI_E(2), 0.4f); run(h, 4, 100, 0);
        usleep(300000);
        press(h, PI_VIEW_BACK); run(h, 10, 100, 0);
        CHECK(!strcmp(disp(h, PI_MK(0) + 2), "Freq"), "remapped knob 1 to Freq (\"%s\")", disp(h, PI_MK(0) + 2));
        usleep(300000);
        press(h, PI_ROW(0) + 7); run(h, 10, 100, 0);
        CHECK(!row_used(h, 0) && !strcmp(disp(h, PI_MK(0) + 2), "Empty") && !strcmp(pname(h, PI_Q(12)), "-"),
              "Bye: its macro is gone (\"%s\")", disp(h, PI_MK(0) + 2));
        h->dispatcher(h, effClose, 0, 0, NULL, 0);
    }

    AEffect *fx2 = vmain(master);
    fx2->dispatcher(fx2, effOpen, 0, 0, NULL, 0);
    fx2->dispatcher(fx2, effSetChunk, 0, (intptr_t)strlen(copy) + 1, copy, 0);
    run(fx2, 2, 100, 0);
    CHECK(strstr(disp(fx2, PI_STATUS), "6/16") != NULL, "restored: \"%s\"", disp(fx2, PI_STATUS));
    press(fx2, PI_ROW(3) + 4); run(fx2, 2, 100, 0);
    CHECK(fabsf(fx2->getParameter(fx2, PI_Q(4)) - 0.25f) < 1e-3, "restored: the plugin's Freq (%.3f)", fx2->getParameter(fx2, PI_Q(4)));
    press(fx2, PI_VIEW_BACK); run(fx2, 2, 100, 0);
    char *chunk2 = NULL;
    fx2->dispatcher(fx2, effGetChunk, 0, 0, &chunk2, 0);
    {
        /* compare everything after the focus/page header */
        const char *a = strchr(strchr(copy + 4, '|') + 1, '|'), *b = strchr(strchr(chunk2 + 4, '|') + 1, '|');
        CHECK(a && b && !strcmp(a, b), "state round trip: plugin list and values identical");
    }
    free(copy);
    fx2->dispatcher(fx2, effClose, 0, 0, NULL, 0);

    /* a project naming a plugin this build doesn't carry */
    AEffect *fx3 = vmain(master);
    const char *odd = "PP1|0|0|Nobody/Gone:0.5|Poli/Poli-Ess:0.1,0.2,0.3,0,0";
    fx3->dispatcher(fx3, effSetChunk, 0, (intptr_t)strlen(odd) + 1, (void *)odd, 0);
    run(fx3, 2, 100, 0);
    CHECK(strstr(disp(fx3, PI_STATUS), "Nobody/Gone") != NULL && row_used(fx3, 0) && !row_used(fx3, 1),
          "unknown plugin skipped, the rest loads: \"%s\"", disp(fx3, PI_STATUS));
    fx3->dispatcher(fx3, effClose, 0, 0, NULL, 0);

    fx->dispatcher(fx, effClose, 0, 0, NULL, 0);

    /* ---- 1.3.0: the opened effect's presets, and the Parallel section ---- */
    {
        AEffect *f = vmain(master);
        f->dispatcher(f, effOpen, 0, 0, NULL, 0);
        run(f, 40, 100, 0);
        char nm[128] = "";
        f->dispatcher(f, effGetProgramNameIndexed, 0, 0, nm, 0);
        CHECK(f->numPrograms == 1 && !strcmp(nm, "(open an effect)"), "PRESET menu with nothing open: %d program \"%s\"", f->numPrograms, nm);
        add(f, 2);   /* X Presets: 20 programs, no selector of its own */
        press(f, PI_ROW(0) + 4);
        run(f, 40, 100, 0);   /* the wrapper re-lists the programs with its next UpdateDisplay (at most every 50 ms) */
        nm[0] = 0;
        f->dispatcher(f, effGetProgramNameIndexed, 3, 0, nm, 0);
        CHECK(f->numPrograms == 20 && !strcmp(nm, "Delta"), "PRESET menu lists the opened effect's presets: %d, #3 \"%s\"", f->numPrograms, nm);
        CHECK(opt(f, PI_HPRE_SHOWN, 2) == 1 && !strcmp(disp(f, PI_HPRE_NAME), "Alpha"), "the preset bar shows over its page: \"%s\"",
              disp(f, PI_HPRE_NAME));
        f->dispatcher(f, effSetProgram, 0, 3, NULL, 0);
        run(f, 20, 100, 0);
        CHECK(!strcmp(disp(f, PI_HPRE_NAME), "Delta") && fabsf(f->getParameter(f, PI_E(1)) - 3 / 19.0f) < 1e-4 && automates[PI_E(1)] > 0,
              "a pick in MPC's PRESET menu loads it in the effect (Tone %.3f), its values reported", f->getParameter(f, PI_E(1)));
        /* 1.3.1: parameters past 255 (e256.., appended) and a Q-Link on a parameter over 127 */
        f->setParameter(f, PI_E(299), 0.7f);
        f->setParameter(f, PI_Q(0), 0.25f);
        run(f, 4, 100, 0);
        CHECK(!strcmp(disp(f, PI_E(299)), "0.70") && fabsf(f->getParameter(f, PI_E(200)) - 0.25f) < 1e-4,
              "parameter 299 through e299 (\"%s\"), Q-Link 1 on parameter 200 (%.2f)", disp(f, PI_E(299)), f->getParameter(f, PI_E(200)));
        press(f, PI_HPRE_SHOWN + 2); run(f, 20, 100, 0);   /* > */
        CHECK(!strcmp(disp(f, PI_HPRE_NAME), "Echo") && f->dispatcher(f, effGetProgram, 0, 0, NULL, 0) == 4,
              "> : \"%s\", MPC's menu follows (%d)", disp(f, PI_HPRE_NAME), (int)f->dispatcher(f, effGetProgram, 0, 0, NULL, 0));
        press(f, PI_HPRE_SHOWN + 1); press(f, PI_HPRE_SHOWN + 1); run(f, 20, 100, 0);   /* < < */
        CHECK(!strcmp(disp(f, PI_HPRE_NAME), "Charlie"), "< < : \"%s\"", disp(f, PI_HPRE_NAME));
        press(f, PI_HPRE_SHOWN + 3); run(f, 20, 100, 0);   /* the name: the browser */
        CHECK(opt(f, PI_HBROWSING, 2) == 1 && opt(f, PI_HPICK(2) + 2, 3) == 2 && opt(f, PI_HPICK(0) + 2, 3) == 1 &&
              !strcmp(disp(f, PI_HPICK(0) + 1), "Alpha") && !strcmp(disp(f, PI_HPRE_PAGE), "1 / 2"),
              "the name opens the browser: page \"%s\", row 1 \"%s\", Charlie highlighted", disp(f, PI_HPRE_PAGE), disp(f, PI_HPICK(0) + 1));
        press(f, PI_HBROWSING + 2); run(f, 20, 100, 0);   /* page > */
        CHECK(!strcmp(disp(f, PI_HPRE_PAGE), "2 / 2") && !strcmp(disp(f, PI_HPICK(0) + 1), "Quebec") && opt(f, PI_HPICK(4) + 2, 3) == 0,
              "page 2: \"%s\" first, rows past the last preset hidden", disp(f, PI_HPICK(0) + 1));
        press(f, PI_HPICK(1)); run(f, 20, 100, 0);
        CHECK(f->dispatcher(f, effGetProgram, 0, 0, NULL, 0) == 17 && opt(f, PI_HBROWSING, 2) == 0 && !strcmp(disp(f, PI_HPRE_NAME), "Romeo"),
              "a pick loads \"%s\" and closes the browser", disp(f, PI_HPRE_NAME));
        press(f, PI_VIEW_BACK); run(f, 20, 100, 0);
        CHECK(opt(f, PI_HPRE_SHOWN, 2) == 0 && !strcmp(disp(f, PI_HPRE_NAME), " "), "back on the rack: no preset bar");
        /* 1.4.2: the PRESET menu leaves the effect too (on the device it kept "Hard Tune" on the rack) */
        run(f, 40, 100, 0);
        nm[0] = 0;
        f->dispatcher(f, effGetProgramNameIndexed, 0, 0, nm, 0);
        {
            char cur[64] = "";
            f->dispatcher(f, 5 /* effGetProgramName */, 0, 0, cur, 0);
            CHECK(f->numPrograms == 1 && !strcmp(nm, "(open an effect)") && !strcmp(cur, "(open an effect)") &&
                  f->dispatcher(f, effGetProgram, 0, 0, NULL, 0) == 0,
                  "back on the rack: PRESET menu %d program \"%s\", current \"%s\"", f->numPrograms, nm, cur);
            f->dispatcher(f, effSetProgram, 0, 3, NULL, 0);   /* MPC re-applying a saved index on the rack: ignored */
            run(f, 4, 100, 0);
            usleep(300000);
            press(f, PI_ROW(0) + 4); run(f, 40, 100, 0);
            CHECK(f->dispatcher(f, effGetProgram, 0, 0, NULL, 0) == 17, "reopened: still preset 17 (%d)",
                  (int)f->dispatcher(f, effGetProgram, 0, 0, NULL, 0));
            usleep(300000);
            press(f, PI_VIEW_BACK); run(f, 20, 100, 0);
        }
        add(f, 0);   /* Poli-Ess: draws its own selector, so PoliPlug's bar stays hidden; the PRESET menu still lists them */
        press(f, PI_ROW(1) + 4);
        run(f, 40, 100, 0);
        nm[0] = 0;
        f->dispatcher(f, effGetProgramNameIndexed, 7, 0, nm, 0);
        CHECK(opt(f, PI_HPRE_SHOWN, 2) == 0 && f->numPrograms == 12 && !strcmp(nm, "Heavy Sibilance"),
              "Poli-Ess opened: its own selector, no bar; PRESET menu %d presets, #7 \"%s\"", f->numPrograms, nm);
        f->dispatcher(f, effSetProgram, 0, 7, NULL, 0);
        run(f, 40, 100, 0);
        CHECK(fabsf(f->getParameter(f, PI_E(0)) - 0.85f) < 1e-3, "PRESET menu pick reaches the hosted Poli-Ess (Intensity %.2f)",
              f->getParameter(f, PI_E(0)));
        press(f, PI_VIEW_BACK); run(f, 4, 100, 0);

        /* the Parallel section (1.4.0): every effect is its own track, fed its own Send of the Inserts' output, added back */
#define RSEND(r) (PI_RX(r) + 3)
#define RTOG(r) (PI_RX(r) + 1)
#define RSC(r) (PI_RX(r) + 5)
#define SC_CLOSE (PI_SCING + 1)
#define SC_TOG (PI_SCING + 3)
        press(f, PI_SECTION + 2); run(f, 20, 100, 0);   /* the Parallel tab */
        CHECK(opt(f, PI_SECTION, 2) == 1 && !row_used(f, 0) && strstr(disp(f, PI_STATUS), "Parallel is empty") != NULL,
              "Parallel tab: its own (empty) list: \"%s\"", disp(f, PI_STATUS));
        double base = run(f, 100, 200, 0.2);
        add(f, 2);
        CHECK(row_used(f, 0) && !row_used(f, 1) && strstr(disp(f, PI_STATUS), "Parallel 1/16") != NULL &&
              strstr(disp(f, PI_ROW(0)), "X Presets") != NULL, "an effect added on the Parallel tab goes there: \"%s\"", disp(f, PI_STATUS));
        CHECK(opt(f, PI_RX(0), 3) == 2 && opt(f, PI_RX(0) + 2, 2) == 1 && opt(f, PI_RX(0) + 4, 3) == 1 && !strcmp(disp(f, RSEND(0)), "50%"),
              "its row: On (green), its own Send \"%s\" and SC (off)", disp(f, RSEND(0)));
        f->setParameter(f, RSEND(0), 0.0f);
        double s0 = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
        f->setParameter(f, RSEND(0), 0.5f);
        double s50 = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
        f->setParameter(f, RSEND(0), 1.0f);
        double s100 = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
        CHECK(fabs(s0 / base - 1) < 0.01 && fabs(s50 / base - 1.5) < 0.02 && fabs(s100 / base - 2.0) < 0.03,
              "its Send 0 / 50 / 100 %%: the track adds 0 / 0.5 / 1 x (%.3f %.3f %.3f of %.3f)", s0, s50, s100, base);
        CHECK(!strcmp(disp(f, RSEND(0)), "100%"), "its Send shows \"%s\"", disp(f, RSEND(0)));
        add(f, 2);   /* a second track: its own Send (50 % to start) */
        double two = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
        f->setParameter(f, RSEND(1), 0.0f);
        double two0 = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
        CHECK(fabs(two / base - 2.5) < 0.04 && fabs(two0 / base - 2.0) < 0.03,
              "two tracks, each with its own Send: 100 %% + 50 %% adds 1.5 x (%.3f), the second at 0 %% adds nothing more (%.3f)",
              two / base, two0 / base);
        /* dragging a row's Send: no screen rebuild (1.3.2's lesson: a changing polled text rebuilt MPC's screen mid-drag) */
        {
            run(f, 200, 200, 0.2);
            updates = 0;
            for (int k = 0; k < 40; k++) { f->setParameter(f, RSEND(1), k / 50.0f); run(f, 4, 200, 0.2); }
            run(f, 200, 200, 0.2);
            CHECK(updates == 0, "a Send drag on a Parallel row: no screen rebuild (%d UpdateDisplay)", updates);
        }
        /* 1.4.1: on the rack, Parallel's Q-Links are the tracks' Sends (Q-Link n = track n) */
        {
            CHECK(!strcmp(pname(f, QS(0)), "P1 Send") && !strcmp(pname(f, QS(1)), "P2 Send") && !strcmp(pname(f, QS(2)), "-"),
                  "Parallel: Q-Links 1-2 are the Sends (\"%s\" \"%s\"), 3 is on nothing (\"%s\")", pname(f, QS(0)),
                  pname(f, QS(1)), pname(f, QS(2)));
            CHECK(strstr(disp(f, PI_STATUS), "Q-Link pages") != NULL && !strcmp(pname(f, QS(4)), "-"),
                  "status: \"%s\", Q-Link 5 on nothing", disp(f, PI_STATUS));
            run(f, 200, 200, 0.2);
            updates = 0; automates[RSEND(1)] = 0;
            for (int k = 0; k <= 30; k++) { f->setParameter(f, QS(1), k / 100.0f); run(f, 2, 200, 0.2); }
            run(f, 200, 200, 0.2);
            CHECK(!strcmp(disp(f, RSEND(1)), "30%") && automates[RSEND(1)] > 0 && !strcmp(disp(f, QS(1)), "30%") && updates == 0,
                  "Q-Link 2 turns track 2's Send: its row knob \"%s\" follows, no screen rebuild (%d)", disp(f, RSEND(1)), updates);
            automates[QS(0)] = 0;
            f->setParameter(f, RSEND(0), 0.8f); run(f, 4, 200, 0.2);
            CHECK(fabsf(f->getParameter(f, QS(0)) - 0.8f) < 1e-3 && automates[QS(0)] > 0,
                  "the row's Send knob moves Q-Link 1 (%.3f)", f->getParameter(f, QS(0)));
            f->setParameter(f, QS(2), 0.9f); run(f, 4, 200, 0.2);   /* no track 3: nothing happens */
            usleep(300000);
            press(f, PI_SECTION + 1); run(f, 20, 100, 0);
            CHECK(!strcmp(pname(f, QS(0)), "-"), "Inserts: the Q-Links are on nothing (\"%s\")", pname(f, QS(0)));
            f->setParameter(f, QS(0), 0.1f); run(f, 4, 100, 0);
            usleep(300000);
            press(f, PI_SECTION + 2); run(f, 20, 100, 0);
            CHECK(!strcmp(disp(f, RSEND(0)), "80%"), "Q-Link 1 on Inserts left track 1's Send alone (\"%s\")", disp(f, RSEND(0)));
            usleep(300000);
            f->setParameter(f, RSEND(0), 1.0f); run(f, 60, 200, 0.2);   /* back to where the next checks expect it */
        }
        /* Send moving: no zipper (the gain glides); a jump 0 -> 100 % between blocks */
        {
            f->setParameter(f, RSEND(1), 0.0f); run(f, 60, 200, 0.2);
            double mx = 0, prev = 0, prev2 = 0;
            f->setParameter(f, RSEND(1), 1.0f);
            for (int b = 0; b < 20; b++) {
                run(f, 1, 200, 0.2);
                for (int j = 0; j < 128; j++) {
                    double d2 = fabs(out0[j] - 2 * prev + prev2);
                    if (b || j > 1) mx = fmax(mx, d2);
                    prev2 = prev; prev = out0[j];
                }
            }
            CHECK(mx < 0.002, "Send jumping 0 -> 100 %% glides (largest 2nd difference %.5f)", mx);
        }
        /* On/Off: track 2 off fades out (no click), then it adds nothing; on again */
        {
            double full = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
            usleep(300000);   /* a person's tap, spaced from the last one on this button (the wrapper's 250 ms) */
            f->setParameter(f, RTOG(1), 1.0f);
            double mx = 0, prev = 0, prev2 = 0;
            for (int b = 0; b < 30; b++) {
                run(f, 1, 200, 0.2);
                if (!b) f->setParameter(f, RTOG(1), 0.0f);
                for (int j = 0; j < 128; j++) {
                    if (b || j > 1) mx = fmax(mx, fabs(out0[j] - 2 * prev + prev2));
                    prev2 = prev; prev = out0[j];
                }
            }
            double off = run(f, 100, 200, 0.2);
            run(f, 10, 100, 0);
            CHECK(fabs(full / base - 3.0) < 0.05 && fabs(off / base - 2.0) < 0.03 && opt(f, PI_RX(1), 3) == 1 && mx < 0.002,
                  "track 2 Off: from 3.0 x to %.3f x, fading (2nd difference %.5f), its switch red", off / base, mx);
            usleep(300000);
            press(f, RTOG(1));
            double back = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
            CHECK(fabs(back / base - 3.0) < 0.05 && opt(f, PI_RX(1), 3) == 2, "On again: %.3f x", back / base);
        }
        /* Inserts On/Off: both Inserts off = the input untouched (Parallel sends at 0 here) */
        {
            f->setParameter(f, RSEND(0), 0.0f); f->setParameter(f, RSEND(1), 0.0f);
            press(f, PI_SECTION + 1); run(f, 20, 100, 0);
            CHECK(opt(f, PI_RX(0), 3) == 2 && opt(f, PI_RX(0) + 2, 2) == 0 && opt(f, PI_RX(0) + 4, 3) == 0,
                  "an Inserts row: On/Off, no Send, no SC");
            press(f, RTOG(0)); press(f, RTOG(1));
            run(f, 100, 7000, 0.2);
            double e = 0;
            for (int b = 0; b < 20; b++) { run(f, 1, 7000, 0.2); for (int j = 0; j < 128; j++) e = fmax(e, fabs(out0[j] - in0[j])); }
            CHECK(e < 1e-4, "both Inserts Off: the input comes out untouched (largest difference %.2g)", e);
            press(f, RTOG(0)); press(f, RTOG(1));
            run(f, 40, 100, 0);
            press(f, PI_SECTION + 2); run(f, 20, 100, 0);
            f->setParameter(f, RSEND(0), 1.0f);
            press(f, RTOG(1));   /* track 2 off for the sidechain checks */
            run(f, 60, 200, 0.2);
        }
        /* the sidechain panel of track 1: Q-Links follow its knobs; ducking keyed by the vocal (the Inserts' output) */
        {
            double ref = run(f, 100, 200, 0.2);   /* 2.0 x: dry + track 1 at 100 % */
            updates = 0;
            press(f, RSC(0)); run(f, 20, 100, 0);
            CHECK(opt(f, PI_SCING, 2) == 1 && !strcmp(disp(f, PI_SC_NAME), "P1  X Presets") && opt(f, PI_SC_ONST, 3) == 1 && updates > 0,
                  "SC opens its panel: \"%s\", Off, MPC asked to re-read (%d)", disp(f, PI_SC_NAME), updates);
            CHECK(!strcmp(pname(f, QS(0)), "Threshold") && !strcmp(pname(f, QS(3)), "Knee") && !strcmp(pname(f, QS(4)), "Ratio") &&
                  !strcmp(pname(f, QS(5)), "-") && !strcmp(pname(f, PI_Q(12)), "Threshold") && !strcmp(pname(f, PI_Q(13)), "Ratio") &&
                  !strcmp(disp(f, QS(0)), "-24 dB") && !strcmp(disp(f, QS(2)), "250 ms"),
                  "Q-Links follow the panel: \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", pname(f, QS(0)), pname(f, QS(1)),
                  pname(f, QS(2)), pname(f, QS(3)), pname(f, QS(4)), pname(f, QS(5)));
            press(f, RTOG(0)); run(f, 10, 100, 0);
            CHECK(opt(f, PI_RX(0), 3) == 2, "rows don't take taps under the panel");
            automates[PI_SC_THR] = 0;
            f->setParameter(f, QS(0), 0.5f); run(f, 10, 100, 0);   /* Q-Link 1: Threshold -30 dB */
            automates[QS(3)] = 0;
            f->setParameter(f, PI_SC_KNEE, 0.0f); run(f, 10, 100, 0);   /* the panel's Knee knob: 0 dB */
            CHECK(!strcmp(disp(f, PI_SC_THR), "-30 dB") && automates[PI_SC_THR] > 0 && automates[QS(3)] > 0 && !strcmp(disp(f, QS(3)), "0 dB"),
                  "Q-Link 1 moves Threshold (\"%s\", its knob reported), the Knee knob moves Q-Link 4 (\"%s\")", disp(f, PI_SC_THR), disp(f, QS(3)));
            double offd = run(f, 100, 200, 0.5);
            updates = 0; automates[PI_SC_GR] = 0;
            press(f, SC_TOG); run(f, 10, 100, 0);
            run(f, 100, 200, 0.5);
            updates = 0;
            double duck = run(f, 200, 200, 0.5);
            float meter = f->getParameter(f, PI_SC_GR);
            /* the vocal peaks at -6 dBFS: 24 dB over -30, ratio 4 -> the track down 18 dB (x 0.126): 2.0 x -> 1.126 x */
            CHECK(opt(f, PI_SC_ONST, 3) == 2 && opt(f, PI_RX(0) + 4, 3) == 2 && fabs(duck / offd - 1.126 / 2.0) < 0.02,
                  "SC On, a loud vocal (-6 dBFS): the track ducked by %.1f dB (want 18), its SC button green",
                  20 * log10((duck / offd * 2.0 - 1.0)));
            CHECK(automates[PI_SC_GR] > 0 && fabs(meter - 18.0 / 24) < 0.05 && updates == 0,
                  "the Ducking meter reaches MPC (%.2f = %.0f dB), no screen rebuild (%d)", meter, meter * 24, updates);
            double quiet = (run(f, 500, 200, 0.01), run(f, 100, 200, 0.01));   /* after the release (250 ms) has let go */
            double quiet_ref = (press(f, SC_TOG), run(f, 60, 200, 0.01), run(f, 100, 200, 0.01));
            press(f, SC_TOG);
            CHECK(fabs(quiet / quiet_ref - 1) < 0.01, "a quiet vocal (-40 dBFS, under Threshold): not ducked (%.3f)", quiet / quiet_ref);
            f->setParameter(f, QS(5), 0.5f); run(f, 10, 100, 0);   /* page 2, knob 2: nothing (the Send stays at 100 %) */
            CHECK(!strcmp(disp(f, RSEND(0)), "100%") && !strcmp(disp(f, PI_SC_RATIO), "4.0:1"),
                  "Q-Link page 2 knob 2 is on nothing under the panel (Send \"%s\", Ratio \"%s\")", disp(f, RSEND(0)), disp(f, PI_SC_RATIO));
            f->setParameter(f, QS(5), 1.0f); run(f, 10, 100, 0);
            press(f, SC_CLOSE); run(f, 20, 100, 0);
            double after = (run(f, 100, 200, 0.5), run(f, 100, 200, 0.5));
            CHECK(opt(f, PI_SCING, 2) == 0 && !strcmp(pname(f, QS(0)), "P1 Send") && fabs(after / offd - 1.126 / 2.0) < 0.02,
                  "closed: Q-Link 1 is \"%s\" again, the ducking stays on (%.3f)", pname(f, QS(0)), after / offd);
            press(f, RSC(0)); run(f, 20, 100, 0);
            CHECK(!strcmp(disp(f, PI_SC_THR), "-30 dB") && !strcmp(disp(f, PI_SC_KNEE), "0 dB") && opt(f, PI_SC_ONST, 3) == 2,
                  "reopened: its settings kept");
            press(f, SC_CLOSE); run(f, 20, 100, 0);
            (void)ref;
        }
        /* 1.4.1: Q-Links 1-4 follow the Parallel page shown (a second instance with 5 tracks) */
        {
            AEffect *h = vmain(master);
            h->dispatcher(h, effOpen, 0, 0, NULL, 0);
            press(h, PI_SECTION + 2); run(h, 10, 100, 0);
            for (int k = 0; k < 5; k++) { usleep(300000); add(h, 0); }
            run(h, 10, 100, 0);
            CHECK(!strcmp(disp(h, PI_PAGE_TEXT), "2 / 2") && !strcmp(pname(h, QS(4)), "P5 Send") && !strcmp(pname(h, QS(5)), "-") &&
                  !strcmp(pname(h, QS(0)), "P1 Send") && !strcmp(pname(h, QS(3)), "P4 Send") && !strcmp(pname(h, PI_Q(13)), "P5 Send"),
                  "Q-Link page 1 = tracks 1-4 (\"%s\" .. \"%s\"), page 2 top = \"%s\", page 2 knob 2 \"%s\"", pname(h, QS(0)),
                  pname(h, QS(3)), pname(h, QS(4)), pname(h, QS(5)));
            h->setParameter(h, QS(4), 0.2f); run(h, 4, 100, 0);
            CHECK(!strcmp(disp(h, RSEND(0)), "20%"), "Q-Link page 2, top knob turns track 5 (rack page 2, row 1): \"%s\"", disp(h, RSEND(0)));
            press(h, PI_PAGE_PREV); run(h, 10, 100, 0);
            CHECK(!strcmp(pname(h, QS(0)), "P1 Send") && !strcmp(disp(h, QS(0)), "50%") && !strcmp(disp(h, QS(4)), "20%"),
                  "rack page 1: the Q-Link pages stay put (%s, track 5 %s)", disp(h, QS(0)), disp(h, QS(4)));
            h->dispatcher(h, effClose, 0, 0, NULL, 0);
        }
        /* saved (PP4) with both sections, each track's On/Off, Send and sidechain; a new instance plays the same */
        double before = (run(f, 60, 200, 0.5), run(f, 100, 200, 0.5));
        char *ch = NULL;
        intptr_t ln = f->dispatcher(f, effGetChunk, 0, 0, &ch, 0);
        char *cp = strdup(ch ? ch : "");
        AEffect *g = vmain(master);
        g->dispatcher(g, effOpen, 0, 0, NULL, 0);
        g->dispatcher(g, effSetChunk, 0, (intptr_t)strlen(cp) + 1, cp, 0);
        run(g, 20, 100, 0);
        double restored = (run(g, 60, 200, 0.5), run(g, 100, 200, 0.5));
        CHECK(ln > 0 && !strncmp(cp, "PP4|", 4) && strstr(cp, "|~|") && opt(g, PI_SECTION, 2) == 1 && row_used(g, 1) &&
              opt(g, PI_RX(0) + 4, 3) == 2 && opt(g, PI_RX(1), 3) == 1 && !strcmp(disp(g, RSEND(0)), "100%") &&
              fabs(restored / before - 1) < 0.01,
              "state (%d bytes): both sections, track 1 SC on, track 2 Off, Sends; the same audio (%.3f)", (int)ln, restored / before);
        free(cp);
        g->dispatcher(g, effClose, 0, 0, NULL, 0);
        /* a 1.3 project (PP3): its single Send goes to every Parallel track (which are now side by side, not in a chain) */
        {
            const char *pp3 = "PP3|-1|0|0|1|0|30.00|Test/X Presets:0.5,0,0|~|Test/X Presets:0.5,0,0|Test/X Presets:0.5,0,0";
            AEffect *h3 = vmain(master);
            h3->dispatcher(h3, effOpen, 0, 0, NULL, 0);
            double dry = run(h3, 100, 200, 0.2);
            h3->dispatcher(h3, effSetChunk, 0, (intptr_t)strlen(pp3) + 1, (void *)pp3, 0);
            run(h3, 20, 100, 0);
            double o3 = (run(h3, 60, 200, 0.2), run(h3, 100, 200, 0.2));
            CHECK(!strcmp(disp(h3, RSEND(0)), "30%") && !strcmp(disp(h3, RSEND(1)), "30%") && fabs(o3 / dry - 1.6) < 0.03,
                  "a 1.3 project (PP3, Send 30 %%): each Parallel track gets 30 %% (%.3f x)", o3 / dry);
            h3->dispatcher(h3, effClose, 0, 0, NULL, 0);
        }
        /* removing a track whose panel is up closes the panel */
        press(f, RSC(0)); run(f, 20, 100, 0);
        CHECK(opt(f, PI_SCING, 2) == 1, "panel up again");
        f->setParameter(f, PI_ROW(0) + 7, 1.0f); run(f, 1, 100, 0); f->setParameter(f, PI_ROW(0) + 7, 0.0f);   /* (under the panel: ignored) */
        run(f, 20, 100, 0);
        CHECK(row_used(f, 1), "Bye under the panel is ignored");
        press(f, SC_CLOSE); run(f, 20, 100, 0);
        /* removing the Parallel effects: nothing is added any more */
        usleep(300000); press(f, PI_ROW(0) + 7); run(f, 20, 100, 0);   /* taps spaced like a person's (250 ms filter) */
        usleep(300000); press(f, PI_ROW(0) + 7); run(f, 20, 100, 0);
        double gone = (run(f, 60, 200, 0.2), run(f, 100, 200, 0.2));
        CHECK(!row_used(f, 0) && fabs(gone / base - 1) < 0.01, "an empty Parallel adds nothing (%.3f)", gone / base);
        /* a 1.2 project (PP2) loads into the Inserts */
        const char *old = "PP2|0|0|Test/X Presets:0.5,0.2,0";
        f->dispatcher(f, effSetChunk, 0, (intptr_t)strlen(old) + 1, (void *)old, 0);
        run(f, 20, 100, 0);
        CHECK(opt(f, PI_SECTION, 2) == 0 && row_used(f, 0) && !row_used(f, 1), "a 1.2 project (PP2) loads into the Inserts");
        f->dispatcher(f, effClose, 0, 0, NULL, 0);
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
