/* Poli-verb mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see: tabs, Q-Link
 * names, toggles, the space picture, the preset browser. Build and run: test/run_tests.sh */
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
    CHECK(opt(a, "page", 3) == 0, "starts on General");
    CHECK(!strcmp(pname(a, "q1"), "Mix") && !strcmp(disp(a, "q1"), "30 %"), "General Q1 = Mix \"%s\" (%s)", pname(a, "q1"), disp(a, "q1"));
    CHECK(!strcmp(disp(a, "mix"), "30 %") && !strcmp(disp(a, "input"), "0 dB"), "readouts: mix \"%s\", input \"%s\"", disp(a, "mix"), disp(a, "input"));
    reset();
    tap(a, "tab2");
    CHECK(opt(a, "page", 3) == 1 && automated[idx("page")] > 0 && updates > 0, "tab Space -> page Space, reported, UpdateDisplay");
    CHECK(!strcmp(pname(a, "q1"), "Size"), "Space Q1 = %s", pname(a, "q1"));
    CHECK(!strcmp(pname(a, "q5"), "Probability") || !strcmp(pname(a, "q4"), "Probability"), "Space page has Probability on Q-Links (%s,%s)", pname(a, "q4"), pname(a, "q5"));
    printf("info space page: size \"%s\" pre-delay \"%s\" width \"%s\" lvl \"%s\" prob \"%s\" room \"%s\" type \"%s\"\n", disp(a, "size"), disp(a, "predelay"),
           disp(a, "width"), disp(a, "er_level"), disp(a, "er_prob"), disp(a, "room_view"), disp(a, "type_view"));
    CHECK(opt(a, "type_view", 20) == 6, "space picture shows Hall (type_view=%d)", opt(a, "type_view", 20));
    reset();
    tap(a, "type_next");
    CHECK(opt(a, "type", 14) == 1 && opt(a, "type_view", 20) == 7 && automated[idx("type_view")] > 0, "next space -> Room (%s), picture follows", disp(a, "type"));
    for (int i = 0; i < 12; i++) { tap(a, "type_next"); }
    CHECK(opt(a, "type", 14) == 13, "13 more steps reach Stairwell (%s)", disp(a, "type"));
    tap(a, "type_next");
    CHECK(opt(a, "type", 14) == 0, "wraps to Hall");
    tap(a, "type_prev");
    CHECK(opt(a, "type", 14) == 13, "prev wraps back");
    CHECK(opt(a, "room_view", 6) == 0, "room_view stays hidden on the Space page");
    /* Sync */
    a->setP(a, idx("sync"), 1.0f); run(a, 40, 0, 100);
    printf("info synced pre-delay \"%s\"\n", disp(a, "predelay"));
    CHECK(strchr(disp(a, "predelay"), '/') != NULL, "Sync on: pre-delay reads a note value (\"%s\")", disp(a, "predelay"));
    a->setP(a, idx("sync"), 0.0f); run(a, 10, 0, 100);
    CHECK(strchr(disp(a, "predelay"), '/') == NULL, "Sync off: milliseconds again (\"%s\")", disp(a, "predelay"));
    /* Tail */
    tap(a, "tab3");
    CHECK(opt(a, "page", 3) == 2 && !strcmp(pname(a, "q1"), "Diffusion"), "Tail page, Q1 = %s", pname(a, "q1"));
    printf("info tail: decay \"%s\" release \"%s\" bass \"%s\" attack \"%s\"\n", disp(a, "decay"), disp(a, "release"), disp(a, "bassmult"), disp(a, "attack"));
    CHECK(!strcmp(disp(a, "release"), "inf"), "Release default reads inf");
    CHECK(opt(a, "type_view", 20) >= 1 && opt(a, "type_view", 20) <= 5, "Tail page shows the room-size picture (type_view %d, room \"%s\")", opt(a, "type_view", 20), disp(a, "room_view"));
    CHECK(!strcmp(pname(a, "q10"), "Room size") && strlen(disp(a, "q10")) >= 1, "Tail Q10 = %s (%s)", pname(a, "q10"), disp(a, "q10"));
    {
        int seen[6] = {0}, v0 = opt(a, "type_view", 20);
        char first[16]; strcpy(first, disp(a, "q10"));
        for (int i = 0; i < 6; i++) { tap(a, "room_step"); int v = opt(a, "type_view", 20); if (v >= 1 && v <= 5) seen[v]++; }
        int moved = 0; for (int i = 1; i <= 5; i++) moved += seen[i] > 0;
        CHECK(moved >= 3, "room_step cycles the room size (%d distinct pictures, started %d, now \"%s\", Size knob %s)", moved, v0, disp(a, "q10"), disp(a, "size"));
    }
    /* Freeze toggle */
    tap(a, "tab1");
    a->setP(a, idx("freeze"), 1.0f); run(a, 10, 0.3f, 220);
    CHECK(a->getP(a, idx("freeze")) == 1.0f && !strcmp(disp(a, "freeze"), "On"), "Freeze on");
    a->setP(a, idx("freeze"), 0.0f);
    /* preset arrows and browser */
    reset();
    tap(a, "preset_next");
    CHECK(opt(a, "preset", 101) == 1, "preset > = program 1 (%s)", disp(a, "preset"));
    CHECK(automated[idx("mix")] > 0 && updates > 0, "preset load reports the knobs and asks for UpdateDisplay");
    reset();
    tap(a, "preset_list");
    CHECK(a->getP(a, idx("browsing")) == 1.0f && opt(a, "bview", 22) >= 1, "browser opens (bview %d)", opt(a, "bview", 22));
    CHECK(opt(a, "pre_hl", 13) >= 0, "current cell highlight %d", opt(a, "pre_hl", 13));
    tap(a, "cat3");
    int v3 = opt(a, "bview", 22);
    CHECK(v3 > 1, "category 3 view %d", v3);
    tap(a, "bpage_next");
    tap(a, "pick2");
    CHECK(a->getP(a, idx("browsing")) == 0.0f && opt(a, "bview", 22) == 0, "pick closes the browser");
    printf("info picked preset: %d \"%s\"\n", opt(a, "preset", 101), disp(a, "preset"));
    /* state round trip */
    char buf[1024]; void *chunk = NULL;
    intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
    CHECK(n > 0 && chunk, "state chunk %d bytes", (int)n);
    if (n > 0 && chunk) {
        memcpy(buf, chunk, n < 1024 ? n : 1024);
        AEffect *b = VSTPluginMain(host);
        b->d(b, 24, 0, (intptr_t)n, buf, 0);
        CHECK(opt(b, "preset", 101) == opt(a, "preset", 101) && fabsf(b->getP(b, idx("mix")) - a->getP(a, idx("mix"))) < 1e-3, "state reload keeps preset and knobs");
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
