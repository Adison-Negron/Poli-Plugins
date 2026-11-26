/* Poli-Juicebox mock host: links vst2_wrap.c the way MPC loads it and checks what the screen would see: read-outs, the P&F Link switch, knob
 * extremes, Q-Link names, the preset arrows and browser, saved state, audio through the wrapper. Build and run: test/run_tests.sh */
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
    printf("info defaults: pitch \"%s\" formant \"%s\" ftrack \"%s\" thresh \"%s\" lowend \"%s\" attack \"%s\" release \"%s\" color \"%s\" f1c \"%s\" f2c \"%s\" f3c \"%s\" mix \"%s\"\n",
           disp(a, "pitch"), disp(a, "formant"), disp(a, "ftrack"), disp(a, "thresh"), disp(a, "lowend"), disp(a, "attack"), disp(a, "release"),
           disp(a, "color"), disp(a, "f1c"), disp(a, "f2c"), disp(a, "f3c"), disp(a, "mix"));
    CHECK(!strcmp(disp(a, "pitch"), "0 st") && !strcmp(disp(a, "formant"), "0 st") && !strcmp(disp(a, "ftrack"), "50 %") &&
          !strcmp(disp(a, "thresh"), "0 dB") && !strcmp(disp(a, "lowend"), "Off"),
          "read-outs: pitch \"%s\", formant \"%s\", F-Track \"%s\", threshold \"%s\", low end \"%s\"", disp(a, "pitch"), disp(a, "formant"), disp(a, "ftrack"), disp(a, "thresh"), disp(a, "lowend"));
    CHECK(!strcmp(disp(a, "attack"), "3.0 ms") && !strcmp(disp(a, "release"), "40 ms") && !strcmp(disp(a, "color"), "1.0 kHz"),
          "tamer / color: attack \"%s\", release \"%s\", color \"%s\"", disp(a, "attack"), disp(a, "release"), disp(a, "color"));
    CHECK(!strcmp(disp(a, "f1c"), "500 Hz") && !strcmp(disp(a, "f2c"), "1.7 kHz") && !strcmp(disp(a, "f3c"), "3.3 kHz") &&
          !strcmp(disp(a, "f1q"), "Off") && !strcmp(disp(a, "f2q"), "Off") && !strcmp(disp(a, "f3q"), "Off"),
          "formant filter: F1 \"%s\" F2 \"%s\" F3 \"%s\", Q all Off", disp(a, "f1c"), disp(a, "f2c"), disp(a, "f3c"));
    CHECK(!strcmp(disp(a, "fsmooth"), "30 %") && !strcmp(disp(a, "air"), "0 %") && !strcmp(disp(a, "natural"), "40 %") && !strcmp(disp(a, "mix"), "100 %") &&
          !strcmp(disp(a, "odd"), "0 %") && !strcmp(disp(a, "even"), "0 %"),
          "smoothen: F-Smooth \"%s\" Air \"%s\" Natural \"%s\" Mix \"%s\"", disp(a, "fsmooth"), disp(a, "air"), disp(a, "natural"), disp(a, "mix"));
    CHECK(!strcmp(disp(a, "hold"), "60 ms") && !strcmp(disp(a, "rasp"), "50 %") && !strcmp(disp(a, "glide"), "40 %") && !strcmp(disp(a, "guard"), "60 %") &&
          !strcmp(disp(a, "morph"), "30 ms") && !strcmp(disp(a, "floor"), "70 Hz") && !strcmp(disp(a, "ceil"), "700 Hz") && !strcmp(disp(a, "overlap"), "30 %"),
          "tracking page: hold \"%s\" rasp \"%s\" glide \"%s\" guard \"%s\" morph \"%s\" floor \"%s\" ceiling \"%s\" overlap \"%s\"", disp(a, "hold"), disp(a, "rasp"),
          disp(a, "glide"), disp(a, "guard"), disp(a, "morph"), disp(a, "floor"), disp(a, "ceil"), disp(a, "overlap"));
    a->setP(a, idx("hold"), 1.0f); CHECK(!strcmp(disp(a, "hold"), "150 ms"), "Hold at max reads \"%s\"", disp(a, "hold")); a->setP(a, idx("hold"), 0.4f);
    a->setP(a, idx("floor"), 1.0f); CHECK(!strcmp(disp(a, "floor"), "200 Hz"), "Floor at max reads \"%s\"", disp(a, "floor")); a->setP(a, idx("floor"), 0.0f);
    a->setP(a, idx("ceil"), 0.0f); CHECK(!strcmp(disp(a, "ceil"), "250 Hz"), "Ceiling at min reads \"%s\"", disp(a, "ceil")); a->setP(a, idx("ceil"), 0.79f);
    a->setP(a, idx("morph"), 0.0f); CHECK(!strcmp(disp(a, "morph"), "5.0 ms"), "Morph at min reads \"%s\"", disp(a, "morph")); a->setP(a, idx("morph"), 0.486f);
    CHECK(opt(a, "link", 2) == 0 && opt(a, "preset", 36) == 0, "P&F Link Off, preset Init");
    /* right-hand pages: the tabs switch the engine-owned page, which the skin's when=page: widgets follow */
    CHECK(opt(a, "page", 4) == 0, "page Shape on start");
    reset(); tap(a, "tab2");
    CHECK(opt(a, "page", 4) == 1 && automated[idx("page")] > 0 && updates > 0, "tab 2 shows the Tone page and reports it to MPC");
    tap(a, "tab3"); CHECK(opt(a, "page", 4) == 2, "tab 3 = Filter");
    tap(a, "tab4"); CHECK(opt(a, "page", 4) == 3, "tab 4 = Tracking");
    tap(a, "tab1");
    CHECK(opt(a, "page", 4) == 0, "tab 1 is back on the Shape page");
    /* Q-Links: slots 1-4 = the Smoothen panel on every page, 5-16 follow the tab on show */
    reset();
    CHECK(!strcmp(pname(a, "ql01"), "F-Smooth") && !strcmp(pname(a, "ql04"), "Mix") && !strcmp(pname(a, "ql05"), "Pitch") && !strcmp(pname(a, "ql08"), "P&F Link"),
          "Q-Links on the Shape page: 1-4 \"%s\" \"%s\"... 5-8 \"%s\" ... \"%s\"", pname(a, "ql01"), pname(a, "ql02"), pname(a, "ql05"), pname(a, "ql08"));
    a->setP(a, idx("ql05"), 0.75f); run(a, 12, 0, 100);
    CHECK(!strcmp(disp(a, "pitch"), "+6.0 st") && !strcmp(disp(a, "ql05"), "+6.0 st") && automated[idx("pitch")] > 0, "Q-Link 5 moves Pitch (%s), the host is told about Pitch", disp(a, "pitch"));
    a->setP(a, idx("ql05"), 0.5f); run(a, 12, 0, 100);
    a->setP(a, idx("ql08"), 1.0f); run(a, 12, 0, 100);
    CHECK(opt(a, "link", 2) == 1 && !strcmp(disp(a, "ql08"), "Variable"), "Q-Link 8 switches P&F Link (%s)", disp(a, "ql08"));
    a->setP(a, idx("ql08"), 0.0f); run(a, 12, 0, 100);
    reset(); tap(a, "tab2");
    CHECK(!strcmp(pname(a, "ql01"), "F-Smooth") && !strcmp(pname(a, "ql05"), "Threshold") && !strcmp(pname(a, "ql09"), "Odd drive") && !strcmp(pname(a, "ql12"), "-"),
          "Tone tab: 1 \"%s\", 5 \"%s\", 9 \"%s\", 12 \"%s\"", pname(a, "ql01"), pname(a, "ql05"), pname(a, "ql09"), pname(a, "ql12"));
    CHECK(automated[idx("ql05")] > 0 && automated[idx("ql01")] > 0, "switching the tab tells the host every Q-Link changed meaning");
    tap(a, "tab3");
    CHECK(!strcmp(pname(a, "ql05"), "F1 Freq") && !strcmp(pname(a, "ql06"), "F2 Freq") && !strcmp(pname(a, "ql09"), "F1 Q"), "Filter tab: 5 \"%s\" 6 \"%s\" 9 \"%s\"", pname(a, "ql05"), pname(a, "ql06"), pname(a, "ql09"));
    tap(a, "tab4");
    CHECK(!strcmp(pname(a, "ql05"), "Floor") && !strcmp(pname(a, "ql12"), "Overlap") && !strcmp(disp(a, "ql05"), "70 Hz"), "Tracking tab: 5 \"%s\" 12 \"%s\" reads %s", pname(a, "ql05"), pname(a, "ql12"), disp(a, "ql05"));
    reset(); a->setP(a, idx("floor"), 0.5f); run(a, 12, 0, 100);
    CHECK(automated[idx("ql05")] > 0 && !strcmp(disp(a, "ql05"), disp(a, "floor")), "a knob moved on screen is reported on its Q-Link too (%s)", disp(a, "ql05"));
    a->setP(a, idx("ql12"), 0.7f); run(a, 12, 0, 100);
    CHECK(!strcmp(disp(a, "ql12"), "70 %") && !strcmp(disp(a, "overlap"), "70 %"), "Q-Link 12 is Overlap (%s)", disp(a, "overlap"));
    a->setP(a, idx("overlap"), 0.3f); a->setP(a, idx("floor"), 0.0f);
    a->setP(a, idx("ql13"), 0.9f); run(a, 12, 0, 100);
    CHECK(!strcmp(disp(a, "ql13"), "--"), "an empty Q-Link slot does nothing (\"%s\")", disp(a, "ql13"));
    tap(a, "tab1");
    CHECK(!strcmp(pname(a, "pitch"), "Pitch") && !strcmp(pname(a, "formant"), "Formant") && !strcmp(pname(a, "ftrack"), "F-Track") &&
          !strcmp(pname(a, "mix"), "Mix") && !strcmp(pname(a, "link"), "P&F Link") && !strcmp(pname(a, "lowend"), "Low end"),
          "names for the Q-Links: %s / %s / %s / %s / %s / %s", pname(a, "pitch"), pname(a, "formant"), pname(a, "ftrack"), pname(a, "mix"), pname(a, "link"), pname(a, "lowend"));
    /* knob extremes read out sensibly */
    a->setP(a, idx("pitch"), 1.0f); CHECK(!strcmp(disp(a, "pitch"), "+12 st"), "Pitch at max reads \"%s\"", disp(a, "pitch"));
    a->setP(a, idx("pitch"), 0.0f); CHECK(!strcmp(disp(a, "pitch"), "-12 st"), "Pitch at min reads \"%s\"", disp(a, "pitch"));
    a->setP(a, idx("pitch"), 0.9f); CHECK(!strcmp(disp(a, "pitch"), "+9.6 st"), "Pitch at 90 %% reads \"%s\"", disp(a, "pitch"));
    a->setP(a, idx("pitch"), 0.5f);
    a->setP(a, idx("formant"), 0.6f); CHECK(!strcmp(disp(a, "formant"), "+2.4 st"), "Formant at 60 %% reads \"%s\"", disp(a, "formant"));
    a->setP(a, idx("formant"), 0.5f);
    a->setP(a, idx("thresh"), 0.0f); CHECK(!strcmp(disp(a, "thresh"), "-60 dB"), "Threshold at min reads \"%s\"", disp(a, "thresh"));
    a->setP(a, idx("thresh"), 1.0f);
    a->setP(a, idx("lowend"), 1.0f); CHECK(!strcmp(disp(a, "lowend"), "350 Hz"), "Low end at max reads \"%s\"", disp(a, "lowend"));
    a->setP(a, idx("lowend"), 0.0f);
    a->setP(a, idx("attack"), 1.0f); CHECK(!strcmp(disp(a, "attack"), "20 ms"), "Attack at max reads \"%s\"", disp(a, "attack"));
    a->setP(a, idx("attack"), 0.0f); CHECK(!strcmp(disp(a, "attack"), "0.5 ms"), "Attack at min reads \"%s\"", disp(a, "attack"));
    a->setP(a, idx("attack"), 0.486f);
    a->setP(a, idx("release"), 1.0f); CHECK(!strcmp(disp(a, "release"), "200 ms"), "Release at max reads \"%s\"", disp(a, "release"));
    a->setP(a, idx("release"), 0.463f);
    a->setP(a, idx("color"), 1.0f); CHECK(!strcmp(disp(a, "color"), "4.0 kHz"), "Color at max reads \"%s\"", disp(a, "color"));
    a->setP(a, idx("color"), 0.0f); CHECK(!strcmp(disp(a, "color"), "200 Hz"), "Color at min reads \"%s\"", disp(a, "color"));
    a->setP(a, idx("color"), 0.537f);
    a->setP(a, idx("f1q"), 1.0f); CHECK(!strcmp(disp(a, "f1q"), "14.0"), "F1 Q at max reads \"%s\"", disp(a, "f1q"));
    a->setP(a, idx("f1q"), 0.5f); CHECK(strcmp(disp(a, "f1q"), "Off") && strcmp(disp(a, "f1q"), "14.0"), "F1 Q at centre reads \"%s\"", disp(a, "f1q"));
    a->setP(a, idx("f1q"), 0.0f);
    a->setP(a, idx("f3c"), 1.0f); CHECK(!strcmp(disp(a, "f3c"), "4.5 kHz"), "F3 at max reads \"%s\"", disp(a, "f3c"));
    a->setP(a, idx("f3c"), 0.473f);
    a->setP(a, idx("mix"), 0.0f); CHECK(!strcmp(disp(a, "mix"), "0 %"), "Mix at min reads \"%s\"", disp(a, "mix"));
    a->setP(a, idx("mix"), 1.0f);
    run(a, 10, 0, 100);
    /* P&F Link */
    reset();
    a->setP(a, idx("link"), 1.0f); run(a, 4, 0, 100);
    CHECK(opt(a, "link", 2) == 1, "P&F Link set to Variable");
    a->setP(a, idx("link"), 0.0f); run(a, 4, 0, 100);
    CHECK(opt(a, "link", 2) == 0, "P&F Link back to Off");
    /* audio through the wrapper: Init is transparent (just delayed), a +5 st shift stays bounded, finite and about as loud */
    {
        float L[128], R[128], I0[128], I1[128], *o[2] = {L, R}, *in[2] = {I0, I1};
        for (int pass = 0; pass < 2; pass++) {
            a->setP(a, idx("pitch"), pass ? 0.708f : 0.5f); a->setP(a, idx("formant"), pass ? 0.583f : 0.5f); run(a, 100, 0, 100);
            double ei = 0, eo = 0; float pk = 0; int nonfinite = 0;
            for (int blk = 0; blk < 200; blk++) {
                for (int n = 0; n < 128; n++) { I0[n] = I1[n] = 0.25f * sinf(tone); tone += 2 * 3.14159265f * 200 / 44100; }
                a->pr(a, in, o, 128);
                for (int n = 0; n < 128; n++) {
                    if (!(fabsf(L[n]) < 100.0f)) nonfinite++;
                    ei += I0[n] * I0[n]; eo += L[n] * L[n]; if (fabsf(L[n]) > pk) pk = fabsf(L[n]);
                }
            }
            double db = 10 * log10(eo / ei);
            if (!pass) CHECK(nonfinite == 0 && fabs(db) < 0.5, "Init: transparent pass-through (%.2f dB)", db);
            else CHECK(nonfinite == 0 && pk <= 1.0f && db > -4.0 && db < 4.0, "pitch +5 st / formant +2 st: bounded (peak %.2f), level within +-4 dB (%+.2f dB)", pk, db);
        }
        a->setP(a, idx("pitch"), 0.5f); a->setP(a, idx("formant"), 0.5f); run(a, 100, 0, 100);
    }
    /* preset arrows and browser */
    reset();
    tap(a, "preset_next");
    CHECK(opt(a, "preset", 36) == 1, "preset > = program 1 (%s)", disp(a, "preset"));
    CHECK(!strcmp(disp(a, "pitch"), "0 st") && !strcmp(disp(a, "formant"), "+1.0 st") && !strcmp(disp(a, "ftrack"), "40 %") && opt(a, "link", 2) == 1, "MtF: Subtle Lift keeps the pitch (%s), formant %s, linked at F-Track %s", disp(a, "pitch"), disp(a, "formant"), disp(a, "ftrack"));
    CHECK(!strcmp(disp(a, "ceil"), "450 Hz") && !strcmp(disp(a, "hold"), "70 ms"), "...and a tracking range for a lower source voice (ceiling %s, hold %s)", disp(a, "ceil"), disp(a, "hold"));
    CHECK(automated[idx("pitch")] > 0 && automated[idx("mix")] > 0 && automated[idx("link")] > 0 && updates > 0, "preset load reports the knobs, the link and asks for UpdateDisplay");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 36) == 0 && !strcmp(disp(a, "pitch"), "0 st"), "preset < = back to Init");
    tap(a, "preset_prev");
    CHECK(opt(a, "preset", 36) == 35, "preset < wraps to the last preset (%s)", disp(a, "preset"));
    tap(a, "preset_next");
    /* latency pills: lview is 0 on the Shape page, 1 + mode on the Tracking page; the pills pick the mode; it is saved in the state */
    reset();
    CHECK(opt(a, "lview", 4) == 0, "lview hidden on the Shape page");
    tap(a, "tab4");
    CHECK(opt(a, "lview", 4) == 2, "Tracking page: Balanced is the default mode (lview %d)", opt(a, "lview", 4));
    tap(a, "lat1");
    CHECK(opt(a, "lview", 4) == 1, "lat1 -> Fast");
    tap(a, "lat3");
    CHECK(opt(a, "lview", 4) == 3, "lat3 -> Smooth");
    {
        char sbuf[1200]; void *chunk = NULL;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        CHECK(n > 0 && chunk && strstr((char *)chunk, "lt=2") != NULL, "the latency mode is in the saved state");
        if (n > 0 && chunk) {
            memcpy(sbuf, chunk, n < 1200 ? n : 1200);
            tap(a, "lat1");
            AEffect *b2 = VSTPluginMain(host);
            b2->d(b2, 24, 0, (intptr_t)n, sbuf, 0);
            tap(b2, "tab4");
            CHECK(opt(b2, "lview", 4) == 3, "a reloaded state brings the latency mode back (lview %d)", opt(b2, "lview", 4));
        }
    }
    tap(a, "tab1");
    CHECK(opt(a, "lview", 4) == 0, "back on the Shape page the pills hide");
    reset();
    /* Learn: listens to the input, switches itself off when it has heard ~6 s of voice, sets the Tracking page from it; Off too early changes nothing */
    reset();
    CHECK(a->getP(a, idx("learn")) == 0.0f && a->getP(a, idx("learn_prog")) == 0.0f, "Learn starts off, progress empty");
    a->setP(a, idx("learn"), 1.0f); run(a, 60, 0.3f, 150);
    CHECK(a->getP(a, idx("learn")) == 1.0f, "Learn On: listening");
    run(a, 400, 0.3f, 150);
    CHECK(a->getP(a, idx("learn_prog")) > 0.1f, "progress bar moves while it listens (%.2f)", a->getP(a, idx("learn_prog")));
    a->setP(a, idx("learn"), 0.0f); run(a, 4, 0.3f, 150);
    CHECK(strcmp(disp(a, "floor"), "70 Hz") != 0 && a->getP(a, idx("learn")) == 0.0f, "Off after ~1.3 s of voice: it learned (floor now \"%s\")", disp(a, "floor"));
    a->setP(a, idx("floor"), 0.0f); a->setP(a, idx("ceil"), 790.0f / 1000.0f);
    reset();
    a->setP(a, idx("learn"), 1.0f); run(a, 20, 0.3f, 150);
    a->setP(a, idx("learn"), 0.0f); run(a, 4, 0.0f, 100);
    CHECK(!strcmp(disp(a, "floor"), "70 Hz") && !strcmp(disp(a, "ceil"), "700 Hz"), "Off before it heard enough: nothing changes (floor \"%s\" ceiling \"%s\")", disp(a, "floor"), disp(a, "ceil"));
    a->setP(a, idx("learn"), 1.0f); run(a, 2300, 0.3f, 150);
    CHECK(a->getP(a, idx("learn")) == 0.0f && a->getP(a, idx("learn_prog")) == 0.0f, "it switched itself off after ~6 s of voice");
    CHECK(strcmp(disp(a, "floor"), "70 Hz") != 0 && strcmp(disp(a, "ceil"), "700 Hz") != 0 && !strcmp(disp(a, "pitch"), "0 st") && !strcmp(disp(a, "formant"), "0 st"),
          "learned floor \"%s\" ceiling \"%s\" for a 150 Hz voice; Pitch \"%s\" and Formant \"%s\" untouched", disp(a, "floor"), disp(a, "ceil"), disp(a, "pitch"), disp(a, "formant"));
    CHECK(updates > 0, "the new values are reported to the host (%d updates)", updates);
    /* help window: ? opens page 1, the arrows flip 1 <-> 2, ? closes, the browser and help exclude each other */
    reset();
    CHECK(opt(a, "hview", 3) == 0, "help starts closed");
    tap(a, "help_btn");
    CHECK(opt(a, "hview", 3) == 1, "? opens help page 1 (hview %d)", opt(a, "hview", 3));
    tap(a, "help_next");
    CHECK(opt(a, "hview", 3) == 2, "help_next -> page 2");
    tap(a, "help_next");
    CHECK(opt(a, "hview", 3) == 1, "help_next -> page 1 again");
    tap(a, "preset_list");
    CHECK(opt(a, "hview", 3) == 0 && a->getP(a, idx("browsing")) == 1.0f, "opening the browser closes help");
    tap(a, "help_btn");
    CHECK(opt(a, "hview", 3) == 1 && a->getP(a, idx("browsing")) == 0.0f, "opening help closes the browser");
    tap(a, "help_btn");
    CHECK(opt(a, "hview", 3) == 0, "? closes help");
    tap(a, "help_next");
    CHECK(opt(a, "hview", 3) == 0, "help_next does nothing while closed");
    /* browser: MtF > 7th preset (Raspy Smooth: linked, pitch 0, formant +2.5 st, rasp 78 %, hold 110 ms) */
    reset();
    tap(a, "preset_list");
    CHECK(a->getP(a, idx("browsing")) == 1.0f && opt(a, "bview", 11) >= 1, "browser opens (bview %d)", opt(a, "bview", 11));
    tap(a, "cat2");   /* MtF */
    CHECK(opt(a, "bview", 11) > 1, "category MtF view %d", opt(a, "bview", 11));
    tap(a, "pick7");
    CHECK(a->getP(a, idx("browsing")) == 0.0f && opt(a, "bview", 11) == 0, "pick closes the browser");
    printf("info picked preset: %d \"%s\"\n", opt(a, "preset", 36), disp(a, "preset"));
    CHECK(opt(a, "preset", 36) == 7 && opt(a, "link", 2) == 1 && !strcmp(disp(a, "pitch"), "0 st") && !strcmp(disp(a, "formant"), "+2.5 st") && !strcmp(disp(a, "rasp"), "78 %") && !strcmp(disp(a, "hold"), "110 ms"),
          "that preset set Link On, pitch %s, rasp %s, hold %s", disp(a, "pitch"), disp(a, "rasp"), disp(a, "hold"));
    CHECK(automated[idx("link")] > 0 && automated[idx("pitch")] > 0, "...and reported them to MPC");
    run(a, 60, 0.25f, 150);
    /* every category button opens a non-empty view; Init from All */
    for (int c = 1; c <= 6; c++) {
        char k[8]; snprintf(k, sizeof k, "cat%d", c);
        tap(a, "preset_list"); tap(a, k);
        CHECK(opt(a, "bview", 11) >= 1 && a->getP(a, idx("browsing")) == 1.0f, "%s opens view %d", k, opt(a, "bview", 11));
        if (c == 1) { tap(a, "pick1"); CHECK(opt(a, "preset", 36) == 0 && opt(a, "link", 2) == 0 && !strcmp(disp(a, "pitch"), "0 st"), "Init from the browser: defaults"); }
        else { tap(a, "pick1"); }
    }
    /* state round trip */
    tap(a, "preset_list"); tap(a, "cat6"); tap(a, "pick4");
    a->setP(a, idx("air"), 0.61f);
    {
        char buf[1024]; void *chunk = NULL;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        CHECK(n > 0 && chunk, "state chunk %d bytes", (int)n);
        if (n > 0 && chunk) {
            memcpy(buf, chunk, n < 1024 ? n : 1024);
            AEffect *b = VSTPluginMain(host);
            b->d(b, 24, 0, (intptr_t)n, buf, 0);
            CHECK(opt(b, "preset", 36) == opt(a, "preset", 36) && opt(b, "link", 2) == opt(a, "link", 2) &&
                  fabsf(b->getP(b, idx("air")) - a->getP(a, idx("air"))) < 1e-3 && fabsf(b->getP(b, idx("pitch")) - a->getP(a, idx("pitch"))) < 1e-3,
                  "state reload keeps preset, link and knobs (preset %d, air %s, rasp %s)", opt(b, "preset", 36), disp(b, "air"), disp(b, "rasp"));
        }
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
