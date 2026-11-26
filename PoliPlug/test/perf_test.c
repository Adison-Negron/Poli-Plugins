/* PoliPlug audio cost: time per 128-frame block with one effect, then a full rack, against MPC's budget
 * (128 frames at 44.1 kHz = 2.90 ms per block, for the whole track's inserts and everything else MPC runs).
 *   PP_SETTINGS=<MPC.settings> test/build/perf_test <poli_plug.so>      (non-sanitizer -O2 builds) */
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
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
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
};
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) { (void)e; (void)i; (void)v; (void)p; (void)o; return op == 1 ? 2400 : 0; }
static AEffect *fx;
static float L[128], R[128];
static unsigned rs = 1;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void blocks(int n) { float *io[2] = {L, R}; for (int b = 0; b < n; b++) { for (int j = 0; j < 128; j++) { rs = rs * 1103515245u + 12345u; L[j] = R[j] = ((rs >> 9) & 0xffff) / 65536.0f - 0.5f; } fx->processReplacing(fx, io, io, 128); } }
static double ts[20000];
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double timed(int n, double *worst) {   /* average; *worst = the 99.9th percentile (one block in a thousand) */
    double t0 = now();
    for (int b = 0; b < n; b++) { double a = now(); blocks(1); ts[b] = now() - a; }
    double avg = (now() - t0) / n;
    qsort(ts, n, sizeof ts[0], cmpd);
    *worst = ts[(int)(n * 0.999)];
    return avg;
}
static int opt(int i, int n) { return (int)lroundf(fx->getParameter(fx, i) * (n - 1)); }
static void tap(int i) { fx->setParameter(fx, i, 1); blocks(1); fx->setParameter(fx, i, 0); blocks(9); usleep(300000); }
static char names[64][512];   /* PoliPlug writes up to 128 bytes of text */
static int pick(int k) {
    tap(PI_ADD_OPEN);
    for (int pg = 0; pg < k / PP_NPICK; pg++) tap(PI_PAGE_NEXT);
    if (!opt(PI_PICK(k % PP_NPICK) + 2, 2)) { tap(PI_ADD_OPEN); return 0; }
    fx->dispatcher(fx, 7, PI_PICK(k % PP_NPICK) + 1, 0, names[k], 0);
    for (int j = 0; j < k && names[0][0]; j++) if (!strcmp(names[j], names[k]) && k < 64 && k >= 0 && j != k && !getenv("ANY")) { tap(PI_ADD_OPEN); return 0; }
    tap(PI_PICK(k % PP_NPICK));
    return 1;
}
int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    fx = ((AEffect * (*)(cb)) dlsym(h, "VSTPluginMain"))(host);
    fx->dispatcher(fx, 0, 0, 0, 0, 0);
    double w, budget = 128 / 44100.0;
    double e = timed(20000, &w);
    printf("empty rack: %.3f ms per block, p99.9 %.3f ms, max %.3f ms (this machine's own noise)\n", e * 1e3, w * 1e3, ts[19999] * 1e3);
    int n = 0;
    double cost[64];
    for (int k = 0; k < 64 && pick(k); k++) {
        blocks(200);
        double avg = timed(3000, &w);
        cost[k] = avg;
        printf("%-16s %.3f ms per block (%4.1f%% of 2.90 ms), p99.9 %.3f ms\n", names[k], avg * 1e3, 100 * avg / budget, w * 1e3);
        tap(PI_ROW(0) + 7);
        n = k + 1;
    }
    /* a full rack: 16, the heaviest first, as many as there are */
    int order[64];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (cost[order[j]] > cost[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    printf("full rack:");
    for (int s = 0; s < PP_MAX_SLOTS; s++) { setenv("ANY", "1", 1); pick(order[s % n]); printf(" %s%s", names[order[s % n]], s < 15 ? "," : ""); }
    blocks(500);
    double avg = timed(5000, &w);
    printf("\n16 effects: %.3f ms per block (%.0f%% of 2.90 ms), p99.9 %.3f ms, max %.3f ms\n", avg * 1e3, 100 * avg / budget, w * 1e3, ts[4999] * 1e3);
    fx->dispatcher(fx, 1, 0, 0, 0, 0);
    return 0;
}
