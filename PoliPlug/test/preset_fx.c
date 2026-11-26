/* A stand-in for a third-party effect in PoliPlug's tests: a plain VST2 plugin (not built with the toolchain, so it draws
 * no preset selector of its own) with 300 parameters (past PoliPlug 1.3.0's 256 proxies) and 20 programs. Program k sets Tone to k/19. Audio: out = in x Gain x 2
 * (Gain starts at 0.5 = unity), so a test can tell which program is loaded and hear what the Parallel chain adds.
 * Built by test/run_tests.sh as "Test - VST - Preset FX" (skin: a copy of Poli-Ess's). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define NPROG 20
static const char *const NAMES[NPROG] = {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India",
                                         "Juliett", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo",
                                         "Sierra", "Tango"};
#define NPAR 300
typedef struct { AEffect fx; float p[NPAR]; int prog; } inst_t;

static intptr_t disp(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt) {
    inst_t *s = (inst_t *)e;
    (void)opt;
    switch (op) {
    case 1: free(s); return 0;                                                       /* effClose */
    case 2: if (v >= 0 && v < NPROG) { s->prog = (int)v; s->p[1] = v / (float)(NPROG - 1); } return 0;   /* effSetProgram */
    case 3: return s->prog;                                                          /* effGetProgram */
    case 5: strcpy(ptr, NAMES[s->prog]); return 0;                                   /* effGetProgramName */
    case 29: if (idx < 0 || idx >= NPROG) return 0; strcpy(ptr, NAMES[idx]); return 1;   /* effGetProgramNameIndexed */
    case 8: if (idx < 3) strcpy(ptr, idx == 0 ? "Gain" : idx == 1 ? "Tone" : "Width"); else snprintf(ptr, 24, "P%d", idx); return 0;
    case 7: snprintf(ptr, 24, "%.2f", idx >= 0 && idx < NPAR ? s->p[idx] : 0); return 0;
    default: return 0;
    }
}
static void setp(AEffect *e, int32_t i, float v) { if (i >= 0 && i < NPAR) ((inst_t *)e)->p[i] = v; }
static float getp(AEffect *e, int32_t i) { return i >= 0 && i < NPAR ? ((inst_t *)e)->p[i] : 0; }
static void run(AEffect *e, float **in, float **out, int32_t n) {
    float g = ((inst_t *)e)->p[0] * 2;
    for (int c = 0; c < 2; c++)
        for (int j = 0; j < n; j++) out[c][j] = in[c][j] * g;
}

__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    (void)m;
    inst_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->fx.magic = 0x56737450;
    s->fx.dispatcher = disp;
    s->fx.setParameter = setp;
    s->fx.getParameter = getp;
    s->fx.processReplacing = run;
    s->fx.numPrograms = NPROG;
    s->fx.numParams = NPAR;
    s->fx.numInputs = s->fx.numOutputs = 2;
    s->fx.flags = 1 << 4;   /* effFlagsCanReplacing; no chunks: PoliPlug saves its parameter values */
    s->fx.uniqueID = 0x50724678;
    s->p[0] = 0.5f;
    return &s->fx;
}
