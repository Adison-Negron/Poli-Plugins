/* Loads poli_plug.so once, as MPC does on insert, so it runs its discovery and rewrites its skin, and prints its status.
 * With "add": then adds every effect in its list, one at a time (+ Add plugin, pick, Open, < RACK, Bye), printing what
 * each did, and runs audio through each.
 *   PP_SETTINGS=<MPC.settings> test/build/discover_only <poli_plug.so> [add]      (test/render_installed.py, test/bad_inputs.py) */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
static const char *txt(int i) { static char b[512]; b[0] = 0; fx->dispatcher(fx, 7, i, 0, b, 0); return b; }
static int opt(int i, int n) { return (int)lroundf(fx->getParameter(fx, i) * (n - 1)); }
static float L[128], R[128];
static void blocks(int n) { float *io[2] = {L, R}; for (int b = 0; b < n; b++) { for (int j = 0; j < 128; j++) L[j] = R[j] = 0.3f * sinf(j * 0.3f); fx->processReplacing(fx, io, io, 128); } }
static void tap(int i) { fx->setParameter(fx, i, 1); blocks(1); fx->setParameter(fx, i, 0); blocks(9); usleep(300000); }
int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    AEffect *(*m)(cb) = (AEffect * (*)(cb)) dlsym(h, "VSTPluginMain");
    fx = m(host);
    fx->dispatcher(fx, 0, 0, 0, 0, 0);
    printf("status: %s\n", txt(PI_STATUS));
    if (argc > 2 && !strcmp(argv[2], "add")) {
        for (int k = 0; k < 64; k++) {
            tap(PI_ADD_OPEN);
            for (int pg = 0; pg < k / PP_NPICK; pg++) tap(PI_PAGE_NEXT);
            if (!opt(PI_PICK(k % PP_NPICK) + 2, 2)) { tap(PI_ADD_OPEN); break; }
            char name[512];
            snprintf(name, sizeof name, "%s", txt(PI_PICK(k % PP_NPICK) + 1));
            tap(PI_PICK(k % PP_NPICK));
            int used = opt(PI_ROW(0) + 1, 2);
            printf("add %-20s -> %s | %s", name, used ? "added" : "not added", txt(PI_STATUS));
            if (used) {
                tap(PI_ROW(0) + 4);
                printf(" | open: view %d", opt(PI_VIEW, PP_NVIEW_OPTS));
                blocks(50);
                float pk = 0;
                for (int j = 0; j < 128; j++) if (!isfinite(L[j]) || fabsf(L[j]) > pk) pk = isfinite(L[j]) ? fabsf(L[j]) : 1e9f;
                printf(" | audio peak %.2f", pk);
                tap(PI_VIEW_BACK);
                tap(PI_ROW(0) + 7);
            }
            printf("\n");
        }
    }
    fx->dispatcher(fx, 1, 0, 0, 0, 0);
    return 0;
}
