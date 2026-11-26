/* PoliPlug stress test (x86, ASan/UBSan): MPC-like use from two threads at once, at random, for a while.
 *   - an audio thread: blocks of 128 mostly, also 1, 37, 64, 256 and 500 frames; in place (in == out, as MPC does), with
 *     separate buffers, and with no input (a load probe); every output sample must be finite;
 *   - a screen thread: taps on every PoliPlug button (row Open/move/Bye, + Add plugin and its list, pages, < RACK, page
 *     arrows), editor and Q-Link moves, reads of every parameter's name/text/value, MPC's parameter-0 jiggle, saving and
 *     reloading the state, and reloading damaged states;
 *   - after each action: the rack is consistent (rows shown = plugins on the page, status count, page text, editing).
 *
 *   PP_SETTINGS=<fake MPC.settings> test/build/stress_test <poli_plug.so> [seconds] [seed]
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pp_gen.h"

typedef struct AEffect AEffect;
typedef intptr_t (*cb_t)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
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
enum { effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8, effGetChunk = 23,
       effSetChunk = 24 };
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
static VstTimeInfo ti = {0, 44100, 0, 0, 120, 0, 0, 0, 4, 4, 0, 0, 0, (1 << 10) | (1 << 9)};
static intptr_t master(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)idx; (void)v; (void)p; (void)o;
    if (op == 1) return 2400;
    if (op == 7) return (intptr_t)&ti;
    return 0;
}

static AEffect *fx;
static volatile int stop;
static long blocks, nonfinite, loud;
static int fails;
#define FAIL(...) do { if (fails++ < 20) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

static unsigned rnd(unsigned *s) { *s = *s * 1103515245u + 12345u; return (*s >> 8) & 0xffffff; }

static void *audio(void *arg) {
    (void)arg;
    unsigned seed = 99;
    static float a0[512], a1[512], c0[512], c1[512];
    float *io[2] = {a0, a1}, *out[2] = {c0, c1};
    const int sizes[] = {128, 128, 128, 128, 128, 128, 64, 1, 37, 256, 500};
    while (!stop) {
        int n = sizes[rnd(&seed) % (sizeof sizes / sizeof sizes[0])], mode = rnd(&seed) % 10;
        for (int j = 0; j < n; j++) a0[j] = a1[j] = (float)((int)(rnd(&seed) % 2001) - 1000) / 1500.0f;
        if (mode == 0) fx->processReplacing(fx, NULL, out, n);          /* a probe with no input */
        else if (mode == 1) fx->processReplacing(fx, io, out, n);       /* separate buffers */
        else fx->processReplacing(fx, io, io, n);                       /* in place */
        float **o = mode <= 1 ? out : io;
        for (int j = 0; j < n; j++) {
            if (!isfinite(o[0][j]) || !isfinite(o[1][j])) nonfinite++;
            else if (fabsf(o[0][j]) > 1.001f || fabsf(o[1][j]) > 1.001f) loud++;   /* int16 between stages: never past 0 dBFS */
        }
        ti.samplePos += n;
        blocks++;
        if (rnd(&seed) % 4 == 0) usleep(200);
    }
    return NULL;
}

static const char *text(int op, int i) {
    static char b[512];
    memset(b, 0x55, sizeof b);   /* not zeroed: a reply must end itself */
    b[511] = 0;
    fx->dispatcher(fx, op, i, 0, b, 0);
    if (!memchr(b, 0, 511)) FAIL("param %d op %d: text not terminated", i, op);
    return b;
}
static int opt(int i, int n) { return (int)lroundf(fx->getParameter(fx, i) * (n - 1)); }
static void tap(int i) { fx->setParameter(fx, i, 1.0f); usleep(3000); fx->setParameter(fx, i, 0.0f); }

static int fill, max_n, opens, adds, byes, moves, restores, fuzzed;

static void check(const char *after) {
    char st[512];
    snprintf(st, sizeof st, "%s", text(effGetParamDisplay, PI_STATUS));
    int view = opt(PI_VIEW, PP_NVIEW_OPTS), editing = opt(PI_EDITING, 2), picking = opt(PI_PICKING, 2);
    if ((view > 0) != editing) FAIL("after %s: view %d but editing %d", after, view, editing);
    if (picking && view) FAIL("after %s: list and a page at once", after);
    if (opt(PI_HBROWSING, 2) && (!view || !opt(PI_HPRE_SHOWN, 2))) FAIL("after %s: preset browser without its bar", after);
    if (opt(PI_HPRE_SHOWN, 2) && !view) FAIL("after %s: preset bar on the rack", after);
    int a = 0, b = 0;
    if (sscanf(text(effGetParamDisplay, PI_PAGE_TEXT), "%d / %d", &a, &b) != 2 || a < 1 || a > b)
        FAIL("after %s: page text \"%s\"", after, text(effGetParamDisplay, PI_PAGE_TEXT));
    int n = -1;
    if (strstr(st, "/16") && sscanf(st, "%d/16", &n) == 1) { if (n > max_n) max_n = n; }
    else n = -1;
    int used = 0;
    for (int r = 0; r < PP_ROWS; r++) {
        int u = opt(PI_ROW(r) + 1, 2);
        used += u;
        if (u && (view || picking)) FAIL("after %s: row %d shown under a page or the list", after, r + 1);
        const char *t = text(effGetParamDisplay, PI_ROW(r));
        if (u && !(t[0] >= '1' && t[0] <= '9')) FAIL("after %s: row %d shown with text \"%s\"", after, r + 1, t);
        if (u && r > 0 && !opt(PI_ROW(r - 1) + 1, 2)) FAIL("after %s: row %d shown but row %d not", after, r + 1, r);
        if (u) {
            int up = opt(PI_ROW(r) + 2, 3), dn = opt(PI_ROW(r) + 3, 3), num = atoi(t);
            if (up != (num == 1 ? 1 : 2)) FAIL("after %s: row %d (#%d) up arrow %d", after, r + 1, num, up);
            if (n > 0 && dn != (num == n ? 1 : 2)) FAIL("after %s: row %d (#%d of %d) down arrow %d", after, r + 1, num, n, dn);
        }
    }
    if (n >= 0 && !view && !picking && a >= 1) {
        int want = n - (a - 1) * PP_ROWS;
        want = want < 0 ? 0 : want > PP_ROWS ? PP_ROWS : want;
        if (used != want) FAIL("after %s: %d rows shown, %d plugins on page %d (status \"%s\")", after, used, want, a, st);
    }
}

int main(int argc, char **argv) {
    int secs = argc > 2 ? atoi(argv[2]) : 60;
    unsigned seed = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    fill = getenv("FILL") != NULL;
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("FAIL dlopen %s\n", dlerror()); return 1; }
    AEffect *(*vm)(cb_t) = (AEffect * (*)(cb_t)) dlsym(h, "VSTPluginMain");
    fx = vm(master);
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0);
    printf("status at start: %s\n", text(effGetParamDisplay, PI_STATUS));
    pthread_t at;
    pthread_create(&at, NULL, audio, NULL);

    char *saved = NULL;
    time_t end = time(NULL) + secs;
    long steps = 0;
    const char *names[] = {"tap", "pick", "row", "edit", "qlink", "read", "jiggle", "save", "restore", "fuzz", "view"};
    while (time(NULL) < end && fails < 20) {
        int act = rnd(&seed) % 100;
        const char *what;
        if (act < 12) { what = names[0];
                        /* 1.3.0: the section tabs, the opened effect's preset bar and browser, a browser row, Send, MPC's PRESET menu */
                        int b[] = {PI_ADD_OPEN, PI_PAGE_PREV, PI_PAGE_NEXT, PI_VIEW_BACK, PI_VIEW_PREV, PI_VIEW_NEXT,
                                   PI_SECTION + 1, PI_SECTION + 2, PI_HPRE_SHOWN + 1, PI_HPRE_SHOWN + 2, PI_HPRE_SHOWN + 3,
                                   PI_HBROWSING + 1, PI_HBROWSING + 2, PI_HPICK(rnd(&seed) % PP_NHPRE)};
                        /* 1.4.0: rows' On/Off, Send, SC; the sidechain panel's Off/On, close, knobs */
                        int c[] = {PI_RX(rnd(&seed) % PP_ROWS) + 1, PI_RX(rnd(&seed) % PP_ROWS) + 5, PI_SCING + 1, PI_SCING + 2,
                                   PI_SCING + 3};
                        /* 1.5.0: macro page dots, Map, Clear, the macro knobs */
                        int m[] = {PI_MK_PAGE + 1 + rnd(&seed) % 4, PI_MK_MAPST - 1, PI_MK_CLRST - 1};
                        int k = rnd(&seed) % 26;
                        if (k >= 22) {
                            if (k < 25) tap(m[k - 22]);
                            else fx->setParameter(fx, PI_MK(rnd(&seed) % 4), (rnd(&seed) % 1001) / 1000.0f);
                        }
                        else if (k < 14) tap(b[k]);
                        else if (k < 19) tap(c[k - 14]);
                        else if (k == 19) fx->setParameter(fx, PI_RX(rnd(&seed) % PP_ROWS) + 3, (rnd(&seed) % 1001) / 1000.0f);
                        else if (k == 20) fx->setParameter(fx, PI_SC_THR + rnd(&seed) % 5, (rnd(&seed) % 1001) / 1000.0f);
                        else fx->dispatcher(fx, 2 /* effSetProgram */, 0, rnd(&seed) % 40, NULL, 0); }
        else if (act < 30) { what = names[1];   /* open the list and pick, so the rack fills up */
            if (!opt(PI_PICKING, 2)) tap(PI_ADD_OPEN);
            usleep(5000);
            int k = rnd(&seed) % PP_NPICK;
            if (opt(PI_PICK(k) + 2, 2)) { tap(PI_PICK(k)); adds++; } }
        else if (act < 48) { what = names[2];
            int r = rnd(&seed) % PP_ROWS, k = rnd(&seed) % 10, which = k < 4 ? 4 : k < 6 ? 5 : k < 8 ? 6 : 7;
            if (fill && which == 7 && max_n < 16) which = 5;   /* FILL=1: no Bye until the rack has been full */
            if (opt(PI_ROW(r) + 1, 2)) { opens += which == 4; moves += which == 5 || which == 6; byes += which == 7; }
            tap(PI_ROW(r) + which); }
        else if (act < 62) { what = names[3]; fx->setParameter(fx, PI_E(rnd(&seed) % PP_MAX_EDIT), (rnd(&seed) % 1001) / 1000.0f); }
        else if (act < 68) { what = names[4]; fx->setParameter(fx, PI_Q(rnd(&seed) % PP_NQ), (rnd(&seed) % 1001) / 1000.0f); }
        else if (act < 82) { what = names[5];
            for (int k = 0; k < 8; k++) {
                int i = rnd(&seed) % fx->numParams;
                text(effGetParamName, i); text(effGetParamDisplay, i); text(effGetParamLabel, i);
                float v = fx->getParameter(fx, i);
                if (!(v >= 0 && v <= 1)) FAIL("param %d reads %f", i, v);
            } }
        else if (act < 85) { what = names[6];   /* MPC: parameter 0 to the far end and straight back to where it was, on insert and on STOP */
            float was = fx->getParameter(fx, 0); fx->setParameter(fx, 0, 1.0f); fx->setParameter(fx, 0, was); usleep(20000); }
        else if (act < 90) { what = names[7];
            void *c = NULL;
            intptr_t n = fx->dispatcher(fx, effGetChunk, 0, 0, &c, 0);
            if (n <= 0 || !c || strncmp(c, "PP4|", 4)) FAIL("state not saved (%ld)", (long)n);
            else { free(saved); saved = strndup(c, n); } }
        else if (act < 94) { what = names[8];
            if (saved && !fill) {
                char *copy = strdup(saved);
                fx->dispatcher(fx, effSetChunk, 0, (intptr_t)strlen(copy) + 1, copy, 0);
                void *c = NULL;
                intptr_t n = fx->dispatcher(fx, effGetChunk, 0, 0, &c, 0);
                /* the chain and every plugin's own state come back (focus and page aside) */
                const char *x = strchr(strchr(copy + 4, '|') + 1, '|'), *y = n > 0 ? strchr(strchr((char *)c + 4, '|') + 1, '|') : NULL;
                int xn = 0, yn = 0;
                for (const char *q = x; q && *q; q++) xn += *q == '|';
                for (const char *q = y; q && *q; q++) yn += *q == '|';
                if (xn != yn) FAIL("reload: %d plugins saved, %d after reloading", xn, yn);
                free(copy);
                restores++;
            } }
        else if (act < 97 && fill) { what = names[5]; }
        else if (act < 97) { what = names[9];   /* a damaged project: bytes changed, cut short, or nonsense */
            char buf[9000];
            int len = 0, how = rnd(&seed) % 4;
            if (saved && how < 2) {
                len = (int)strlen(saved);
                if (len > 8990) len = 8990;
                memcpy(buf, saved, len);
                if (how == 0) for (int k = 0; k < 6; k++) buf[rnd(&seed) % (len ? len : 1)] = (char)(rnd(&seed) % 256);
                else len = (int)(rnd(&seed) % (len + 1));
            } else if (how == 2) {
                const char *pieces[] = {"PP2|", "PP1|", "PP3|", "PP4|", "|=", "|=1,50,1,-30,5,250,6,4", "|=nan,1e30,-5", "|~", "|M=", "|M=2,0.0.1,-,1.0.3", "|M=9,9.99.999,0.-1.5,",  "|", ":", "#", ",", "%", "%4", "%ZZ", "0.5", "-1", "1e9", "nan",
                                        "Poli/Poli-Ess", "3DTHC/Shapey EQ", "Airwindows/AW Ambience", "99999", "Gm0rb/Liminal Hz"};
                for (int k = 0; k < 60 && len < 8000; k++) {
                    const char *pc = pieces[rnd(&seed) % (sizeof pieces / sizeof pieces[0])];
                    memcpy(buf + len, pc, strlen(pc)); len += (int)strlen(pc);
                }
            } else {
                len = 4 + rnd(&seed) % 4000;
                memcpy(buf, "PP2|", 4);
                for (int k = 4; k < len; k++) buf[k] = (char)(1 + rnd(&seed) % 255);
            }
            buf[len] = 0;
            fx->dispatcher(fx, effSetChunk, 0, len + 1, buf, 0);
            fuzzed++; }
        else { what = names[10];   /* straight to a hosted page: Open on a shown row */
            for (int r = 0; r < PP_ROWS; r++) if (opt(PI_ROW(r) + 1, 2)) { tap(PI_ROW(r) + 4); opens++; break; } }
        usleep(1000 + rnd(&seed) % 4000);   /* MPC's screen thread runs every few ms */
        check(what);
        steps++;
    }
    stop = 1;
    pthread_join(at, NULL);
    printf("%ld actions, %ld audio blocks; most plugins at once %d; adds %d, opens %d, moves %d, byes %d, reloads %d, damaged %d\n",
           steps, blocks, max_n, adds, opens, moves, byes, restores, fuzzed);
    if (nonfinite) FAIL("%ld non-finite output samples", nonfinite);
    if (loud) FAIL("%ld output samples past full scale", loud);
    fx->dispatcher(fx, effClose, 0, 0, NULL, 0);
    free(saved);
    printf("%s (%d failed)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
