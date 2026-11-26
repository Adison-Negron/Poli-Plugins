/* PoliPlug against REAL installed effects (x86 sanitizer builds from their own sources, test/build_hosted.py), offline.
 * For each one: + Add plugin -> pick it -> Open, then compare what MPC would read through PoliPlug's proxies with the same
 * plugin loaded on its own (a reference instance, as MPC would host it):
 *   - every parameter's name, and its value text and value after the same writes;
 *   - audio through the chain (finite, not stuck), every page (< >), the Q-Link names;
 *   - the saved state: PoliPlug's chunk reloaded into a second PoliPlug restores the values.
 *
 *   PP_SETTINGS=<fake MPC.settings> test/build/host_real_test <poli_plug.so> "<name>=<so>"...    (test/render_installed.py)
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
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
enum { effOpen = 0, effClose = 1, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetParamLabel = 6,
       effGetParamDisplay = 7, effGetParamName = 8, effGetChunk = 23, effSetChunk = 24 };

static int fails, warns;
static VstTimeInfo ti = {0, 44100, 0, 0, 120, 0, 0, 0, 4, 4, 0, 0, 0, (1 << 10) | (1 << 9)};
static intptr_t master(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)idx; (void)v; (void)o;
    if (op == 1) return 2400;
    if (op == 7) return (intptr_t)&ti;
    if (op == 16) return 44100;
    if (op == 17) return 128;
    if (op == 32 && p) { strcpy(p, "Akai"); return 1; }
    return 0;
}
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)
#define WARN(...) do { printf("note "); warns++; printf(__VA_ARGS__); printf("\n"); } while (0)

static float b0[128], b1[128];
static float *io[2] = {b0, b1};
static unsigned rng = 1;
static float noise(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 9) & 0xffff) / 32768.0f - 1.0f; }
static double run(AEffect *fx, int blocks, double amp) {   /* in place, as MPC runs an insert */
    double acc = 0;
    for (int b = 0; b < blocks; b++) {
        for (int j = 0; j < 128; j++) b0[j] = b1[j] = (float)(amp * noise());
        fx->processReplacing(fx, io, io, 128);
        for (int j = 0; j < 128; j++) acc += isfinite(b0[j]) && isfinite(b1[j]) ? b0[j] * b0[j] : 1e30;
        ti.samplePos += 128;
    }
    return sqrt(acc / (blocks * 128.0));
}
static const char *text(AEffect *fx, int op, int i) {   /* 512 bytes: whatever a plugin writes */
    static char buf[8][512];
    static int k;
    char *b = buf[k++ & 7];
    memset(b, 0, 512);
    fx->dispatcher(fx, op, i, 0, b, 0);
    b[511] = 0;
    return b;
}
static int opt(AEffect *fx, int i, int n) { return (int)lroundf(fx->getParameter(fx, i) * (n - 1)); }
static void press(AEffect *fx, int i) {   /* spaced like a person's taps (the wrapper's 250 ms debounce) */
    static double last[512];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec * 1e-9;
    if (now - last[i] < 0.3) usleep((useconds_t)((0.3 - (now - last[i])) * 1e6));
    clock_gettime(CLOCK_MONOTONIC, &ts);
    last[i] = ts.tv_sec + ts.tv_nsec * 1e-9;
    fx->setParameter(fx, i, 1.0f); run(fx, 1, 0); fx->setParameter(fx, i, 0.0f); run(fx, 9, 0);
}
/* the text MPC shows for a hosted value: PoliPlug's rule (display, plus the unit unless the text has one) */
static void expect_text(AEffect *ref, int i, char *out, int len) {
    char a[512], b[512];
    snprintf(a, sizeof a, "%s", text(ref, effGetParamDisplay, i));
    snprintf(b, sizeof b, "%s", text(ref, effGetParamLabel, i));
    char *x = a; while (*x == ' ') x++;
    int letters = 0;
    for (const char *q = x; *q; q++) letters |= (*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == '%';
    snprintf(out, len, b[0] && !letters ? "%s %s" : "%s", x, b);
    if (!out[0]) snprintf(out, len, " ");   /* PoliPlug answers " " for no text (an empty answer would show the proxy's number) */
}

int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("FAIL dlopen %s\n", dlerror()); return 1; }
    AEffect *(*vm)(cb_t) = (AEffect * (*)(cb_t)) dlsym(h, "VSTPluginMain");
    AEffect *fx = vm(master);
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0);
    run(fx, 4, 0);
    printf("status: %s\n", text(fx, effGetParamDisplay, PI_STATUS));

    /* the list: every page of it */
    char names[64][512];
    int nn = 0;
    press(fx, PI_ADD_OPEN);
    for (int pg = 0; pg < 8 && nn < 64; pg++) {
        int any = 0;
        for (int k = 0; k < PP_NPICK; k++)
            if (opt(fx, PI_PICK(k) + 2, 2)) {
                snprintf(names[nn], sizeof names[nn], "%s", text(fx, effGetParamDisplay, PI_PICK(k) + 1));
                if (pg > 0 && !strcmp(names[nn], names[0])) { any = 0; break; }
                nn++; any = 1;
            }
        if (!any) break;
        press(fx, PI_PAGE_NEXT);
    }
    press(fx, PI_ADD_OPEN);
    printf("list (%d):", nn);
    for (int i = 0; i < nn; i++) printf(" [%s]", names[i]);
    printf("\n");

    for (int a = 2; a < argc; a++) {
        char name[128], *eq = strchr(argv[a], '=');
        snprintf(name, sizeof name, "%.*s", (int)(eq - argv[a]), argv[a]);
        const char *so = eq + 1;
        int idx = -1;
        for (int i = 0; i < nn; i++) if (!strcmp(names[i], name)) idx = i;
        printf("\n=== %s ===\n", name);
        CHECK(idx >= 0, "%s is in the list", name);
        if (idx < 0) continue;

        /* add + open */
        press(fx, PI_ADD_OPEN);
        for (int pg = 0; pg < idx / PP_NPICK; pg++) press(fx, PI_PAGE_NEXT);
        press(fx, PI_PICK(idx % PP_NPICK));
        CHECK(opt(fx, PI_ROW(0) + 1, 2) == 1, "added: row 1 \"%s\", status \"%s\"", text(fx, effGetParamDisplay, PI_ROW(0)),
              text(fx, effGetParamDisplay, PI_STATUS));
        if (opt(fx, PI_ROW(0) + 1, 2) != 1) continue;
        press(fx, PI_ROW(0) + 4);
        int view = opt(fx, PI_VIEW, PP_NVIEW_OPTS);
        CHECK(view > 0 && opt(fx, PI_EDITING, 2) == 1, "Open: view %d", view);

        /* the same plugin on its own */
        void *rh = dlopen(so, RTLD_NOW | RTLD_LOCAL);
        AEffect *(*rm)(cb_t) = rh ? (AEffect * (*)(cb_t)) dlsym(rh, "VSTPluginMain") : NULL;
        AEffect *ref = rm ? rm(master) : NULL;
        if (!ref) { CHECK(0, "reference %s loads", so); continue; }
        ref->dispatcher(ref, effOpen, 0, 0, NULL, 0);
        ref->dispatcher(ref, effSetSampleRate, 0, 0, NULL, 44100.0f);
        ref->dispatcher(ref, effSetBlockSize, 0, 128, NULL, 0);
        ref->dispatcher(ref, effMainsChanged, 0, 1, NULL, 0);
        run(ref, 50, 0); run(fx, 50, 0);
        usleep(500000);   /* plugins with a worker thread (Liminal Hz's IR scan) settle */
        run(ref, 50, 0); run(fx, 50, 0);
        int np = ref->numParams;
        CHECK(np <= PP_MAX_EDIT, "%d parameters (PoliPlug proxies %d)", np, PP_MAX_EDIT);
        if (np > PP_MAX_EDIT) np = PP_MAX_EDIT;
        /* live values (meters, lights): change while audio runs with no writes; not compared */
        char live[PP_MAX_EDIT] = {0};
        {
            float before[PP_MAX_EDIT];
            for (int i = 0; i < np; i++) before[i] = ref->getParameter(ref, i);
            run(ref, 100, 0.5);
            for (int i = 0; i < np; i++) live[i] = fabsf(ref->getParameter(ref, i) - before[i]) > 1e-5f;
            run(ref, 100, 0); run(ref, 100, 0);
            for (int i = 0; i < np; i++) live[i] |= fabsf(ref->getParameter(ref, i) - before[i]) > 1e-5f;
            int nl = 0;
            for (int i = 0; i < np; i++) if (live[i]) printf("%s%s", nl++ ? ", " : "      live (not compared): ", text(ref, effGetParamName, i));
            if (nl) printf("\n");
        }

        int badname = 0, badtext0 = 0, longest = 0;
        for (int i = 0; i < np; i++) {
            const char *rn = text(ref, effGetParamName, i), *pn = text(fx, effGetParamName, PI_E(i));
            if (strncmp(rn, pn, 31) && badname++ < 3) printf("      name e%d: \"%s\" through PoliPlug, \"%s\" on its own\n", i, pn, rn);
            char ex[512];
            expect_text(ref, i, ex, sizeof ex);
            if ((int)strlen(ex) > longest) longest = (int)strlen(ex);
            const char *pt = text(fx, effGetParamDisplay, PI_E(i));
            if (!live[i] && strncmp(ex, pt, 127) && badtext0++ < 4) printf("      text e%d (%s): \"%s\" through PoliPlug, \"%s\" on its own\n", i, rn, pt, ex);
        }
        CHECK(!badname, "parameter names match (%d differ)", badname);
        CHECK(!badtext0, "value texts match at the defaults (%d differ; longest %d chars)", badtext0, longest);
        if (longest > 127) WARN("a value text is %d chars: MPC is handed at most 127 (PARAM_TEXT_MAX)", longest);

        /* the same writes to both: values and texts follow (skipping writes that change pages/presets of the other) */
        const float vals[] = {0.0f, 0.37f, 1.0f, 0.5f};
        int badval = 0, badtext = 0;
        for (int i = 0; i < np; i++)
            for (int v = 0; v < 4; v++) {
                ref->setParameter(ref, i, vals[v]);
                fx->setParameter(fx, PI_E(i), vals[v]);
                run(ref, 2, 0.1); run(fx, 2, 0.1);
                float rv = ref->getParameter(ref, i), pv = fx->getParameter(fx, PI_E(i));
                if (live[i]) continue;
                if (fabsf(rv - pv) > 1e-4f && badval++ < 4)
                    printf("      value e%d (%s) after %.2f: %.4f through PoliPlug, %.4f on its own\n", i, text(ref, effGetParamName, i), vals[v], pv, rv);
                char ex[512];
                expect_text(ref, i, ex, sizeof ex);
                const char *pt = text(fx, effGetParamDisplay, PI_E(i));
                /* a text that names this instance (Liminal Hz's TONE3000 page: its own port) differs between any two instances */
                if (getenv("PER_INSTANCE") && strstr(getenv("PER_INSTANCE"), text(ref, effGetParamName, i))) continue;
                if (strncmp(ex, pt, 127) && badtext++ < 4)
                    printf("      text e%d (%s) after %.2f: \"%s\" through PoliPlug, \"%s\" on its own\n", i, text(ref, effGetParamName, i), vals[v], pt, ex);
            }
        CHECK(!badval, "values follow the same writes (%d differ)", badval);
        CHECK(!badtext, "value texts follow the same writes (%d differ)", badtext);

        double r = run(fx, 200, 0.3);
        CHECK(isfinite(r) && r < 1e3, "audio through it: rms %.3f", r);

        /* its pages and Q-Links: > until both come back to where they started */
        char ql0[400] = "", ql[400] = "", seen[200] = "";
        int pages = 0;
        for (int k = 0; k < 9; k++) {
            ql[0] = 0;
            for (int n = 0; n < PP_NQ; n++) {
                const char *q = text(fx, effGetParamName, PI_Q(n));
                if (strlen(ql) + strlen(q) + 3 < sizeof ql) { strcat(ql, q); strcat(ql, n < 15 ? "|" : ""); }
            }
            int v2 = opt(fx, PI_VIEW, PP_NVIEW_OPTS);
            if (k && v2 == view && !strcmp(ql, ql0)) break;
            if (!k) snprintf(ql0, sizeof ql0, "%s", ql);
            printf("      page %d: view %d, Q-Links %s\n", k + 1, v2, ql);
            snprintf(seen + strlen(seen), sizeof seen - strlen(seen), "%d ", v2);
            pages++;
            press(fx, PI_VIEW_NEXT);
        }
        CHECK(pages >= 1 && pages < 9, "%d page(s) by > (views shown: %s)", pages, seen);

        /* saved state into a fresh PoliPlug */
        for (int i = 0; i < np; i++) { fx->setParameter(fx, PI_E(i), 0.25f); }
        run(fx, 4, 0);
        float want[PP_MAX_EDIT];
        for (int i = 0; i < np; i++) want[i] = fx->getParameter(fx, PI_E(i));
        /* what MPC restores for the plugin on its own: its chunk into a new instance (parameters, if it has none) */
        for (int i = 0; i < np; i++) ref->setParameter(ref, i, 0.25f);
        run(ref, 4, 0);
        void *rch = NULL;
        intptr_t rlen = ref->dispatcher(ref, effGetChunk, 0, 0, &rch, 0);
        if (rlen > 0 && rch) {
            AEffect *ref2 = rm(master);
            ref2->dispatcher(ref2, effOpen, 0, 0, NULL, 0);
            char *rc = malloc(rlen);
            memcpy(rc, rch, rlen);
            ref2->dispatcher(ref2, effSetChunk, 0, rlen, rc, 0);
            run(ref2, 10, 0);
            for (int i = 0; i < np; i++) want[i] = ref2->getParameter(ref2, i);
            ref2->dispatcher(ref2, effClose, 0, 0, NULL, 0);
            free(rc);
        }
        void *chunk = NULL;
        intptr_t len = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0);
        char *copy = malloc(len + 1);
        memcpy(copy, chunk, len); copy[len] = 0;
        AEffect *fx2 = vm(master);
        fx2->dispatcher(fx2, effOpen, 0, 0, NULL, 0);
        fx2->dispatcher(fx2, effSetChunk, 0, len, copy, 0);
        run(fx2, 10, 0);
        press(fx2, PI_ROW(0) + 4);
        int badstate = 0;
        for (int i = 0; i < np; i++)
            if (!live[i] && fabsf(fx2->getParameter(fx2, PI_E(i)) - want[i]) > 1e-3f && badstate++ < 4)
                printf("      state e%d (%s): %.4f restored, %.4f by the plugin on its own\n", i, text(ref, effGetParamName, i), fx2->getParameter(fx2, PI_E(i)), want[i]);
        CHECK(!badstate, "saved state restores every value (%d differ; PoliPlug's chunk %ld bytes: %.60s)", badstate, (long)len, copy);
        void *chunk2 = NULL;
        intptr_t len2 = fx2->dispatcher(fx2, effGetChunk, 0, 0, &chunk2, 0);
        {
            const char *x = strchr(strchr(copy + 4, '|') + 1, '|'), *y = len2 > 0 ? strchr(strchr((char *)chunk2 + 4, '|') + 1, '|') : NULL;
            CHECK(x && y && (!strchr(x, '#') || !strcmp(x, y)), "saved again after the reload: the same state%s", strchr(x ? x : "", '#') ? " (its own chunk)" : " (parameters: not compared, meters move)");
        }
        fx2->dispatcher(fx2, effClose, 0, 0, NULL, 0);
        free(copy);

        press(fx, PI_VIEW_BACK);
        press(fx, PI_ROW(0) + 7);
        CHECK(opt(fx, PI_ROW(0) + 1, 2) == 0, "Bye: removed");
        ref->dispatcher(ref, effClose, 0, 0, NULL, 0);
    }
    fx->dispatcher(fx, effClose, 0, 0, NULL, 0);
    printf("\n%s (%d failed, %d notes)\n", fails ? "FAILED" : "PASSED", fails, warns);
    return fails != 0;
}
