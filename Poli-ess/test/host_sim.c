/* Poli-Ess through the real wrapper (vst2_wrap.c), driven like MPC's plugin host:
 *  - MPC keeps the value it last sent / read and draws controls from it; it re-reads a param when the plugin reports
 *    it (audioMasterAutomate) or asks for a screen rebuild (audioMasterUpdateDisplay).
 *  - A Q-Link / data-wheel move = MPC's value +/- a delta (1/128 per Q-Link event; smaller in fine mode).
 * Build + run: test/run_tests.sh */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
  void (*process)(AEffect *, float **, float **, int32_t); void (*setParameter)(AEffect *, int32_t, float);
  float (*getParameter)(AEffect *, int32_t); int32_t numPrograms, numParams, numInputs, numOutputs, flags;
  intptr_t resvd1, resvd2; int32_t initialDelay, realQualities, offQualities; float ioRatio; void *object, *user;
  int32_t uniqueID, version; void (*processReplacing)(AEffect *, float **, float **, int32_t);
  void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t); char future[56]; };
AEffect *VSTPluginMain(audioMasterCallback);

enum { P_INT = 0, P_THR = 1, P_FREQ = 2, P_DIFF = 3, P_METER = 4, P_ATT = 5, P_REL = 6, P_KNEE = 7, P_MIX = 8, NP = 9 };
/* the preset selector (1.0.6, shared/preset_nav.h): appended after the knobs */
enum { P_PRESET = 9, P_PREV = 10, P_NEXT = 11, P_LIST = 12, P_BROWSING = 13, P_HL = 14, P_PICK1 = 15, NALL = 27 };
enum { effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effGetProgramName = 5, effGetParamLabel = 6, effGetParamDisplay = 7 };

static int fails = 0, passes = 0;
static void check(int ok, const char *name, const char *detail) {
    printf("%s  %-58s %s\n", ok ? "ok  " : "FAIL", name, detail ? detail : "");
    if (ok) passes++; else fails++;
}

/* ---- the mock host ---- */
static AEffect *cur;            /* instance the callback belongs to (one at a time here) */
static float host[NALL];        /* what MPC holds and draws */
static int automates[NALL], updates;
static intptr_t master(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float opt) {
    (void)e; (void)v; (void)p;
    if (op == 0 && idx >= 0 && idx < NALL) { host[idx] = opt; automates[idx]++; }
    if (op == 42) { updates++; if (cur) for (int i = 0; i < NALL; i++) host[i] = cur->getParameter(cur, i); }
    if (op == 1) return 2400;
    return 0;
}
static AEffect *open_fx(void) {
    AEffect *fx = VSTPluginMain(master); cur = fx;
    fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
    for (int i = 0; i < NALL; i++) host[i] = fx->getParameter(fx, i);
    memset(automates, 0, sizeof automates); updates = 0;
    return fx;
}
static void close_fx(AEffect *fx) { fx->dispatcher(fx, effClose, 0, 0, 0, 0); cur = NULL; }

static float bufL[128], bufR[128], outL[128], outR[128];
static double ph = 0;
/* run `blocks` 128-frame blocks of a sine (amp 0 = silence) */
static void run(AEffect *fx, int blocks, double f, double amp) {
    float *in[2] = {bufL, bufR}, *out[2] = {outL, outR};
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < 128; i++) { bufL[i] = bufR[i] = (float)(amp * sin(ph)); ph += 2 * M_PI * f / 44100.0; }
        fx->processReplacing(fx, in, out, 128);
    }
}
/* a tap on a drawn button: a press; the wrapper springs it back */
/* the wrapper ignores a second press of the same button within 250 ms (debounce_ms), so taps here are that far apart */
static void tap_gap(void) { struct timespec t = {0, 260 * 1000000L}; nanosleep(&t, NULL); }
static void tap(AEffect *fx, int p) { tap_gap(); fx->setParameter(fx, p, 1.0f); run(fx, 2, 1000, 0); fx->setParameter(fx, p, 0.0f); }
static void text(AEffect *fx, int i, char *buf) { char t[256] = ""; fx->dispatcher(fx, effGetParamDisplay, i, 0, t, 0); strcpy(buf, t); }
/* one Q-Link / wheel event as MPC sends it: from the value it holds, by delta */
static void nudge(AEffect *fx, int i, float delta) {
    float v = host[i] + delta; if (v < 0) v = 0; if (v > 1) v = 1;
    host[i] = v; fx->setParameter(fx, i, v);
    run(fx, 4, 1000, 0);                 /* the wrapper's deferred work runs in process */
}

int main(void) {
    char d[200], t[256];

    /* 1. Q-Link and fine moves on continuous knobs must actually move them (MPC nudges from its read-back) */
    struct { int idx; const char *name; } knobs[] = {{P_INT, "Intensity"}, {P_THR, "Threshold"}, {P_FREQ, "Freq"},
        {P_ATT, "Attack"}, {P_REL, "Release"}, {P_KNEE, "Knee"}, {P_MIX, "Mix"}};
    for (int k = 0; k < 7; k++) {
        for (int fine = 0; fine < 2; fine++) {
            AEffect *fx = open_fx();
            if (knobs[k].idx == P_MIX || knobs[k].idx == P_KNEE) { fx->setParameter(fx, knobs[k].idx, 0.5f); run(fx, 2, 1000, 0); host[knobs[k].idx] = 0.5f; }
            float delta = fine ? 1.0f / 1000 : 1.0f / 128, start = fx->getParameter(fx, knobs[k].idx);
            for (int n = 0; n < 10; n++) { host[knobs[k].idx] = fx->getParameter(fx, knobs[k].idx); nudge(fx, knobs[k].idx, delta); }
            float end = fx->getParameter(fx, knobs[k].idx), want = start + 10 * delta;
            snprintf(d, sizeof d, "%.4f -> %.4f (want %.4f)", start, end, want);
            char name[96]; snprintf(name, sizeof name, "%s: 10 %s moves up land where MPC sent them", knobs[k].name, fine ? "fine (1/1000)" : "Q-Link (1/128)");
            check(fabsf(end - want) < 0.0015f, name, d);
            close_fx(fx);
        }
    }

    /* 2. Diff on a Q-Link: what MPC draws must catch up with the engine. The wrapper sends at most one screen refresh
     *    per 50 ms (requests wait, never dropped), so after each event, time how long until the screen matches. */
    {
        AEffect *fx = open_fx(); int flips = 0, last = 0; double worst_ms = 0; int never = 0;
        int dirs[] = {1,1,1,1,1,1,-1,-1,-1,-1,-1,-1,1,1,1,1,-1,-1,-1,-1,1,-1,1,-1,1,1,1,1,1,1,1,1};
        for (unsigned k = 0; k < sizeof dirs / sizeof *dirs; k++) {
            float v = host[P_DIFF] + dirs[k] / 128.0f; if (v < 0) v = 0; if (v > 1) v = 1;
            host[P_DIFF] = v; fx->setParameter(fx, P_DIFF, v);
            int eng = fx->getParameter(fx, P_DIFF) >= 0.5f, b;
            for (b = 0; b < 40 && (host[P_DIFF] >= 0.5f) != eng; b++) run(fx, 1, 1000, 0);
            if ((host[P_DIFF] >= 0.5f) != eng) never++;
            double ms = b * 128 / 44.1; if (ms > worst_ms) worst_ms = ms;
            flips += eng != last; last = eng;
            run(fx, 3, 1000, 0);   /* ~9 ms to the next Q-Link event */
        }
        snprintf(d, sizeof d, "slowest catch-up %.0f ms, %d stuck, %d flips in 32 events", worst_ms, never, flips);
        check(never == 0 && worst_ms <= 60, "Diff on a Q-Link: screen catches up with the engine (<= 50 ms)", d);
        check(flips >= 2 && flips <= 8, "Diff on a Q-Link: a brush doesn't flip it, a turn does", d);
        /* tap: MPC's toggle sends exactly 1 / 0 */
        fx->setParameter(fx, P_DIFF, 1.0f); run(fx, 2, 1000, 0); text(fx, P_DIFF, t);
        snprintf(d, sizeof d, "text \"%s\"", t); check(fx->getParameter(fx, P_DIFF) == 1.0f && !strcmp(t, "On"), "Diff tap on -> On", d);
        fx->setParameter(fx, P_DIFF, 0.0f); run(fx, 2, 1000, 0); text(fx, P_DIFF, t);
        snprintf(d, sizeof d, "text \"%s\"", t); check(fx->getParameter(fx, P_DIFF) == 0.0f && !strcmp(t, "Off"), "Diff tap off -> Off", d);
        close_fx(fx);
    }

    /* 3. value texts MPC shows */
    {
        AEffect *fx = open_fx(); char a[64], b[64], c[64], u[16] = "";
        fx->setParameter(fx, P_INT, 0.3742f); fx->setParameter(fx, P_THR, 0.504f); fx->setParameter(fx, P_FREQ, 0.5679f);
        run(fx, 2, 1000, 0);   /* param 0 is applied at the next block (wrapper's STOP-hack guard) */
        text(fx, P_INT, a); text(fx, P_THR, b); text(fx, P_FREQ, c); fx->dispatcher(fx, effGetParamLabel, P_FREQ, 0, u, 0);
        snprintf(d, sizeof d, "\"%s\" \"%s\" \"%s %s\"", a, b, c, u);
        check(!strcmp(a, "37") && !strcmp(b, "-20") && !strcmp(c, "6543") && !strcmp(u, "Hz"), "value texts are whole numbers with units", d);
        close_fx(fx);
    }

    /* 4. the meter: reported to MPC while sibilance plays, back to 0 after, without screen rebuilds */
    {
        AEffect *fx = open_fx();
        run(fx, 345, 7000, 0.5);                       /* 1 s of loud 7 kHz */
        float m_on = host[P_METER]; int a_on = automates[P_METER], u_on = updates;
        snprintf(d, sizeof d, "MPC shows %.3f, engine %.3f, %d reports in 1 s", m_on, fx->getParameter(fx, P_METER), a_on);
        check(m_on > 0.2f && fabsf(m_on - fx->getParameter(fx, P_METER)) < 1e-4f, "meter reaches MPC while sibilance plays", d);
        check(a_on <= 12, "meter reports stay at the 100 ms poll rate (<= ~10/s)", d);
        snprintf(d, sizeof d, "%d UpdateDisplay", u_on);
        check(u_on == 0, "meter moves without rebuilding MPC's screen", d);
        run(fx, 690, 7000, 0.0);                       /* 2 s of silence */
        snprintf(d, sizeof d, "MPC shows %.3f", host[P_METER]);
        check(host[P_METER] == 0.0f, "meter returns to 0 on MPC after the sibilance stops", d);
        close_fx(fx);
    }

    /* 5. save and reload: MPC saves every param's value and sets them back in a new instance */
    {
        AEffect *a = open_fx(); float vals[NP];
        a->setParameter(a, P_INT, 0.8125f); a->setParameter(a, P_THR, 0.3125f); a->setParameter(a, P_FREQ, 0.0625f);
        a->setParameter(a, P_DIFF, 1.0f); run(a, 345, 7000, 0.5);
        for (int i = 0; i < NP; i++) vals[i] = a->getParameter(a, i);
        close_fx(a);
        AEffect *b = open_fx();
        for (int i = 0; i < NP; i++) b->setParameter(b, i, vals[i]);
        run(b, 2, 1000, 0);
        int ok = 1; for (int i = 0; i < P_METER; i++) ok &= fabsf(b->getParameter(b, i) - vals[i]) < 1e-4f;
        snprintf(d, sizeof d, "%.4f %.4f %.4f %.0f -> %.4f %.4f %.4f %.0f", vals[0], vals[1], vals[2], vals[3],
                 b->getParameter(b, 0), b->getParameter(b, 1), b->getParameter(b, 2), b->getParameter(b, 3));
        check(ok, "save + reload restores every knob and Diff exactly", d);
        snprintf(d, sizeof d, "meter after reload %.3f", b->getParameter(b, P_METER));
        check(b->getParameter(b, P_METER) == 0.0f, "a saved meter value is not restored as fake reduction", d);
        close_fx(b);
    }


    /* 6. value texts of the new knobs */
    {
        AEffect *fx = open_fx(); char a[64], r[64], k[64], m[64], ku[16] = "", mu[16] = "";
        text(fx, P_ATT, a); text(fx, P_REL, r);
        snprintf(d, sizeof d, "\"%s\" \"%s\"", a, r);
        check(!strcmp(a, "0.80 ms") && !strcmp(r, "35 ms"), "Attack / Release show milliseconds (defaults)", d);
        fx->setParameter(fx, P_ATT, 1.0f); fx->setParameter(fx, P_REL, 0.0f); fx->setParameter(fx, P_KNEE, 0.5f); fx->setParameter(fx, P_MIX, 0.7f);
        run(fx, 2, 1000, 0);
        text(fx, P_ATT, a); text(fx, P_REL, r); text(fx, P_KNEE, k); text(fx, P_MIX, m);
        fx->dispatcher(fx, effGetParamLabel, P_KNEE, 0, ku, 0); fx->dispatcher(fx, effGetParamLabel, P_MIX, 0, mu, 0);
        snprintf(d, sizeof d, "\"%s\" \"%s\" \"%s %s\" \"%s %s\"", a, r, k, ku, m, mu);
        check(!strcmp(a, "10 ms") && !strcmp(r, "5.0 ms") && !strcmp(k, "6.0") && !strcmp(ku, "dB") && !strcmp(m, "70") && !strcmp(mu, "%"),
              "Attack / Release / Knee / Mix texts at other values", d);
        close_fx(fx);
    }

    /* 7. presets through MPC's PRESET menu */
    {
        AEffect *fx = open_fx(); char nm[64]; int named = 0;
        for (int i = 0; i < fx->numPrograms; i++) { nm[0] = 0; fx->dispatcher(fx, effGetProgramName, i, 0, nm, 0); named += nm[0] != 0; }
        snprintf(d, sizeof d, "%d programs, %d named", fx->numPrograms, named);
        check(fx->numPrograms == 12 && named == 12, "12 presets listed with names", d);
        /* Init (program 0) = the defaults: a fresh insert is already on it */
        float def[NP]; for (int i = 0; i < NP; i++) def[i] = fx->getParameter(fx, i);
        fx->dispatcher(fx, effSetProgram, 1, 1, 0, 0); run(fx, 2, 1000, 0);   /* away, then back to Init */
        fx->dispatcher(fx, effSetProgram, 0, 0, 0, 0); run(fx, 2, 1000, 0);
        int same = 1; for (int i = 0; i < NP; i++) if (i != P_METER) same &= fabsf(fx->getParameter(fx, i) - def[i]) < 1e-4f;
        check(same, "Init restores every default", "");
        /* a preset turns Diff off and MPC is told about every value it changed */
        fx->setParameter(fx, P_DIFF, 1.0f); run(fx, 20, 1000, 0);
        memset(automates, 0, sizeof automates);
        fx->dispatcher(fx, effSetProgram, 0, 7, 0, 0); run(fx, 20, 1000, 0);   /* Heavy Sibilance */
        nm[0] = 0; fx->dispatcher(fx, effGetProgramName, 7, 0, nm, 0);
        int told = 1; for (int i = 0; i < NP; i++) if (i != P_METER && i != P_KNEE) told &= automates[i] > 0 && fabsf(host[i] - fx->getParameter(fx, i)) < 1e-4f;
        snprintf(d, sizeof d, "\"%s\": diff %.0f, intensity %.2f, MPC in sync %d", nm, fx->getParameter(fx, P_DIFF), fx->getParameter(fx, P_INT), told);
        check(fx->getParameter(fx, P_DIFF) == 0.0f && fabsf(fx->getParameter(fx, P_INT) - 0.85f) < 1e-4f && told,
              "a preset sets its values, turns Diff off, and MPC shows them", d);
        close_fx(fx);
    }

    /* 8. switching presets while audio plays: every value glides, no click */
    {
        AEffect *fx = open_fx();
        static float o[44100 * 2]; int n = 0;
        float *in[2] = {bufL, bufR}, *out[2] = {outL, outR};
        double ph2 = 0;
        for (int b = 0; b < 300; b++) {
            if (b == 150) fx->dispatcher(fx, effSetProgram, 0, 7, 0, 0);   /* Heavy Sibilance, mid-signal */
            if (b == 230) fx->dispatcher(fx, effSetProgram, 0, 11, 0, 0);  /* Cymbals in a Mix: Freq, Mix, Knee all jump */
            for (int i = 0; i < 128; i++) { float v = (float)(0.3 * sin(ph) + 0.25 * sin(ph2)); bufL[i] = bufR[i] = v; ph += 2 * M_PI * 200 / 44100.0; ph2 += 2 * M_PI * 7350 / 44100.0; }
            fx->processReplacing(fx, in, out, 128);
            for (int i = 0; i < 128; i++) o[n++] = outL[i];
        }
        /* 3rd difference: spikes on a step or kink; compare around the switches with a steady stretch */
        double steady = 0, sw = 0;
        for (int i = 3; i < n; i++) {
            double d3 = fabs(o[i] - 3.0 * o[i - 1] + 3.0 * o[i - 2] - o[i - 3]);
            if (i > 100 * 128 && i < 145 * 128) steady = fmax(steady, d3);
            if ((i > 150 * 128 && i < 160 * 128) || (i > 230 * 128 && i < 240 * 128)) sw = fmax(sw, d3);
        }
        snprintf(d, sizeof d, "around the switches %.4f vs steady %.4f", sw, steady);
        check(sw < steady * 1.5, "switching presets during audio: no click", d);
        close_fx(fx);
    }

    /* 9. the preset selector on the plugin's own screen: arrows, the name opening the browser, a pick */
    {
        AEffect *fx = open_fx(); char nm[64];
        int np = 12;
        #define OPT(i, n) ((float)(i) / ((n) - 1))
        #define HOSTIDX(p, n) ((int)lroundf(host[p] * ((n) - 1)))
        tap(fx, P_NEXT); run(fx, 20, 1000, 0);
        nm[0] = 0; fx->dispatcher(fx, effGetProgramName, 0, 0, nm, 0);
        snprintf(d, sizeof d, "preset %d (\"%s\") freq %.3f, MPC shows preset %d", HOSTIDX(P_PRESET, np), nm, host[P_FREQ],
                 HOSTIDX(P_PRESET, np));
        check(fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0) == 1 && !strcmp(nm, "Male Ess") && HOSTIDX(P_PRESET, np) == 1 &&
              fabsf(host[P_FREQ] - fx->getParameter(fx, P_FREQ)) < 1e-4f, "> loads the next preset; name, knobs and PRESET menu follow", d);
        tap(fx, P_PREV); run(fx, 20, 1000, 0); tap(fx, P_PREV); run(fx, 20, 1000, 0);
        snprintf(d, sizeof d, "program %d", (int)fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0));
        check(fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0) == np - 1, "< twice from Male Ess wraps round to the last preset", d);
        /* one tap reaches the plugin as two presses 30-140 ms apart (Live III): the wrapper's debounce keeps it one step */
        tap_gap(); fx->setParameter(fx, P_NEXT, 1.0f); fx->setParameter(fx, P_NEXT, 1.0f); run(fx, 20, 1000, 0);
        check(fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0) == 0, "a double press of > steps once (debounce)", "");
        /* the name: the browser opens, its highlight is the current preset; a pick loads + closes */
        updates = 0;
        tap(fx, P_LIST); run(fx, 40, 1000, 0);
        snprintf(d, sizeof d, "browsing %.0f, highlight %d, updates %d", host[P_BROWSING], HOSTIDX(P_HL, np + 1), updates);
        check(host[P_BROWSING] == 1.0f && HOSTIDX(P_HL, np + 1) == 1 && updates > 0, "tapping the name opens the browser on the current preset", d);
        fx->setParameter(fx, P_DIFF, 1.0f); run(fx, 20, 1000, 0);
        tap(fx, P_PICK1 + 7); run(fx, 40, 1000, 0);   /* Heavy Sibilance */
        snprintf(d, sizeof d, "program %d, browsing %.0f, highlight %d, intensity %.2f, diff %.0f", (int)fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0),
                 host[P_BROWSING], HOSTIDX(P_HL, np + 1), host[P_INT], host[P_DIFF]);
        check(fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0) == 7 && host[P_BROWSING] == 0.0f && HOSTIDX(P_HL, np + 1) == 0 &&
              fabsf(host[P_INT] - 0.85f) < 1e-4f && host[P_DIFF] == 0.0f, "a pick loads it, closes the browser, MPC in sync", d);
        /* re-picking the current one in the browser restores it after a tweak (the PRESET menu ignores a re-pick) */
        fx->setParameter(fx, P_INT, 0.2f); run(fx, 2, 1000, 0);
        tap(fx, P_LIST); run(fx, 20, 1000, 0); tap(fx, P_PICK1 + 7); run(fx, 40, 1000, 0);
        snprintf(d, sizeof d, "intensity %.2f", fx->getParameter(fx, P_INT));
        check(fabsf(fx->getParameter(fx, P_INT) - 0.85f) < 1e-4f, "picking the current preset again restores it", d);
        /* the name on the backdrop closes it without loading anything */
        tap(fx, P_LIST); run(fx, 20, 1000, 0); fx->setParameter(fx, P_INT, 0.3f); tap(fx, P_LIST); run(fx, 20, 1000, 0);
        check(host[P_BROWSING] == 0.0f && fabsf(fx->getParameter(fx, P_INT) - 0.3f) < 1e-4f, "a second tap closes the browser, values kept", "");
        /* save + reload: tweaked values stay, the preset keeps its name */
        void *chunk = NULL; intptr_t len = fx->dispatcher(fx, 23, 0, 0, &chunk, 0);
        char saved[2048] = ""; if (len > 0 && len < (intptr_t)sizeof saved) memcpy(saved, chunk, len);
        AEffect *b = open_fx();
        b->dispatcher(b, 24, 0, len, saved, 0);
        /* JUCE re-selects the current program after a load: must not undo the restore */
        b->dispatcher(b, effSetProgram, 0, b->dispatcher(b, effGetProgram, 0, 0, 0, 0), 0, 0);
        run(b, 20, 1000, 0);
        nm[0] = 0; b->dispatcher(b, effGetProgramName, 0, 0, nm, 0);
        snprintf(d, sizeof d, "%ld bytes, \"%s\", intensity %.2f", (long)len, nm, b->getParameter(b, P_INT));
        check(len > 0 && !strcmp(nm, "Heavy Sibilance") && fabsf(b->getParameter(b, P_INT) - 0.3f) < 1e-4f,
              "save + reload: the tweak survives and the name stays", d);
        /* garbage */
        b->setParameter(b, P_PRESET, NAN); b->setParameter(b, P_PRESET, 7.0f); b->setParameter(b, P_PRESET, -3.0f); run(b, 4, 1000, 0);
        b->dispatcher(b, 24, 0, 22, "PES1;preset=1e40;freq=", 0); b->dispatcher(b, 24, 0, 12, "PES1;;;;;;;", 0); run(b, 4, 1000, 0);
        int prog = (int)b->dispatcher(b, effGetProgram, 0, 0, 0, 0);
        check(prog >= 0 && prog < np, "garbage preset values and states are clamped", "");
        close_fx(b);
        close_fx(fx);
    }

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
