/* PoliPlug: a plugin that hosts other plugins, in series, on one MPC track insert (up to PP_MAX_SLOTS).
 *
 * The hosted plugins are the EFFECTS ALREADY INSTALLED on this MPC: discover.c reads MPC.settings once per MPC start,
 * keeps the VST effects whose .so is for this CPU and that have a skin, and rebuilds PoliPlug's own skin with their pages.
 * We dlopen each .so from where MPC registered it, call its VSTPluginMain with our own audioMaster callback, and run the
 * chain on every block. MPC never sees the hosted plugins: it sees PoliPlug's parameters only, so
 *   - the rack (rows, Open, arrows, Bye, + Add plugin and its picker, pages) is PoliPlug's own parameters;
 *   - a hosted plugin's controls are reached through 64 editor proxies e0..e63 (e<N> = parameter N of the plugin
 *     that was opened) and 16 Q-Link proxies q1..q16 (q<n> = what that plugin puts on its own Q-Link n);
 *   - its skin is a copy merged into PoliPlug's skin (discover.c), shown while "view" is at its page.
 *     That is the part that fails when a host just loads another plugin: MPC binds a skin to the plugin it inserted
 *     (by that plugin's own name) and has no way to show a skin for a plugin loaded inside it.
 * Stock MPC effects can't be hosted: they are built into the MPC program, with no plugin file to load (PoliPlug's scan
 * report on a Live III, MPC OS 3.10, 2026-11-04).
 *
 * 1.3.0:
 *   - Two sections: Inserts (in series, as before) and Parallel (its own chain, fed "send" % of the Inserts' output and
 *     added back to it, like an aux send/return). The rack, + Add plugin and the page arrows work on the tab shown.
 *   - The opened (focused) effect's VST programs: MPC's PRESET menu lists them through our "hpreset" parameter (the wrapper's
 *     PROG_DYNAMIC: numPrograms follows "hpreset_count" and is refreshed before each UpdateDisplay), and a bar over its page
 *     (< name >, the name opening a browser) serves effects that don't draw a selector of their own.
 *
 * 1.4.0:
 *   - Every row has On/Off (Off: the effect is passed by, after a 10 ms fade, and not run).
 *   - Parallel: each effect is its own send/return track now (was: one chain fed by one Send). Its row has a Send knob
 *     (% of the Inserts' output that goes into it) and SC: a sidechain panel that ducks that track's return while the
 *     track itself (the Inserts' output: the vocal) is over Threshold, like a compressor keyed by the vocal (Threshold,
 *     Attack, Release, Knee, Ratio). While the panel is up, Q-Links 1-6 follow its knobs (+ the track's Send).
 *
 * Threading: vst2_wrap.c calls us under one per-instance lock (screen and audio never overlap), so the chain needs no
 * locks of its own. Hosted plugins call our audioMaster callback from inside their processReplacing (which runs inside
 * our process()), so the callback only records what changed. Discovery and .so loading happen in create() (MPC's insert),
 * not while audio runs; adding a slot only calls VSTPluginMain + effOpen.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "pp_gen.h"
#include "discover.h"

/* ---- VST2 ABI, host side (hand-written, the same layout as wrapper/vst2_wrap.c) ---------------------------------- */
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
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
typedef AEffect *(*vst_main_t)(audioMasterCallback);

enum { effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effGetProgramName = 5, effGetProgramNameIndexed = 29,
       effBeginSetProgram = 67, effEndSetProgram = 68, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
       effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23, effSetChunk = 24,
       effProcessEvents = 25 };
enum { effFlagsProgramChunks = 1 << 5 };
enum { amAutomate = 0, amVersion = 1, amCurrentId = 2, amIdle = 3, amWantMidi = 6, amGetTime = 7, amProcessEvents = 8,
       amIOChanged = 13, amSizeWindow = 15, amGetSampleRate = 16, amGetBlockSize = 17, amGetInputLatency = 18,
       amGetOutputLatency = 19, amGetCurrentProcessLevel = 23, amGetAutomationState = 24, amGetVendorString = 32,
       amGetProductString = 33, amGetVendorVersion = 34, amCanDo = 37, amGetLanguage = 38, amUpdateDisplay = 42,
       amBeginEdit = 43, amEndEdit = 44 };
enum { kVstTransportPlaying = 1 << 1, kVstPpqPosValid = 1 << 9, kVstTempoValid = 1 << 10, kVstTimeSigValid = 1 << 13 };
enum { effFlagsCanReplacing = 1 << 4 };
#define VST_MAGIC 0x56737450 /* 'VstP' */

#define BLOCK 128
#define MAX_CH 8   /* a hosted plugin may have more than 2 pins; the extra ones get silence / a scratch buffer */

/* ---- the installed effects (found once per process) and their libraries: loaded once, never unloaded ---------- */
static pp_rtype_t g_types[PP_MAX_TYPES];
static int g_ntypes = -1, g_skin_changed;
static char g_note[128];
static struct { void *handle; vst_main_t main; int tried; char err[120]; } g_lib[PP_MAX_TYPES];
static pthread_mutex_t g_lib_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *basename_of(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

static void discover_once(const char *dir) {
    pthread_mutex_lock(&g_lib_lock);
    if (g_ntypes < 0) g_ntypes = pp_discover(dir, g_types, PP_MAX_TYPES, &g_skin_changed, g_note, sizeof g_note);
    pthread_mutex_unlock(&g_lib_lock);
}

static int view_type(int v) {   /* the type whose page view v is, or -1 */
    for (int t = 0; t < g_ntypes; t++)
        if (v >= g_types[t].view0 && v < g_types[t].view0 + g_types[t].nviews) return t;
    return -1;
}

static vst_main_t lib_main(int type, char *err, int errlen) {
    pthread_mutex_lock(&g_lib_lock);
    if (!g_lib[type].tried) {
        const char *path = g_types[type].file;
        g_lib[type].tried = 1;
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char *e = dlerror();
            snprintf(g_lib[type].err, sizeof g_lib[type].err, "Can't load %s: %s", basename_of(g_types[type].file),
                     e ? basename_of(e) : "?");
        } else {
            vst_main_t m = (vst_main_t)dlsym(h, "VSTPluginMain");
            if (!m) m = (vst_main_t)dlsym(h, "main");
            if (!m) snprintf(g_lib[type].err, sizeof g_lib[type].err, "%s has no VSTPluginMain", basename_of(g_types[type].file));
            g_lib[type].handle = h;
            g_lib[type].main = m;
        }
    }
    vst_main_t m = g_lib[type].main;
    if (!m && err) snprintf(err, errlen, "%s", g_lib[type].err);
    pthread_mutex_unlock(&g_lib_lock);
    return m;
}

/* Loading a library is the slow part of adding an effect (AW Ambience: 18 ms the first time, x86; the device is slower),
 * and an add runs while the audio waits (the wrapper's lock). So when the effect list opens, every library is loaded on a
 * thread of its own, and the pick only starts the plugin (well under 1 ms). Once per process. */
static void *preload(void *arg) {
    (void)arg;
    for (int t = 0; t < g_ntypes; t++) lib_main(t, NULL, 0);
    return NULL;
}
static void preload_start(void) {
    pthread_t th;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &a, preload, NULL)) { /* no thread: each library loads on its first pick, as before */ }
    pthread_attr_destroy(&a);
}
static void preload_once(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, preload_start);
}

/* ---- instance ---------------------------------------------------------------------------------------------------- */
typedef struct pp pp_t;
typedef struct {
    pp_t *owner;
    int type;
    AEffect *fx;
    int nparams;   /* min(fx->numParams, PP_MAX_EDIT) */
    int own_sel;   /* it draws its own preset selector (a parameter named "Preset <": the Poli plugins, Poliwow) */
    /* 1.4.0 */
    int on;                   /* On/Off */
    float mix;                /* the On/Off fade, glided */
    float send, send_g;       /* Parallel: Send % and its gain, glided */
    int sc_on;                /* the sidechain: ducks this Parallel track while the vocal is over thr */
    float thr, att, rel, knee, ratio;   /* dB, ms, ms, dB, :1 */
    float env, g, gr_db;      /* detector (peak, linear), ducking gain applied, its dB for the meter */
} slot_t;

#define NSEC 2     /* 0 = Inserts (in series), 1 = Parallel (fed by Send, added back to the Inserts' output) */

struct pp {
    char dir[512];
    slot_t *s[NSEC][PP_MAX_SLOTS];
    int n[NSEC];
    int sec;            /* the section the rack shows (and + Add plugin adds to) */
    int page[NSEC];     /* rack page shown per section, 0.. */
    int fsec, focus;    /* the slot the Q-Links and editor proxies talk to: section, index (-1: none) */
    float send;         /* Parallel send, % */
    float send_g;       /* its gain, glided per sample (no zipper when the knob moves) */
    int hbrowse;        /* the opened effect's preset browser is up */
    int hpage;          /* its page, PP_NHPRE names each */
    int view;           /* 0 = rack; else the hosted page shown (view_type(view) == s[focus]->type) */
    int picking;        /* the + Add plugin list is up */
    int pick_page;      /* its page, PP_NPICK names each */
    char status[128];
    unsigned char dirty[PP_NKEYS];   /* parameters we changed ourselves, reported through mpc_engine_changed() */
    float buf[2][MAX_CH][BLOCK];     /* ping-pong buffers for the chain */
    float scratch[MAX_CH][BLOCK];
    float dry[2][BLOCK];             /* the Inserts' output, while the Parallel tracks run */
    VstTimeInfo ti;
    /* 1.4.0 */
    slot_t *sc;                      /* the Parallel track whose sidechain panel is up, or NULL */
    int sc_frame, sc_since;          /* the meter frame MPC was last told, samples since */
    float pin[2][BLOCK], pout[2][BLOCK], acc[2][BLOCK], key[BLOCK], gbuf[BLOCK];
    /* 1.5.0: macro knobs (Inserts tab). 16 macros = 4 Q-Link pages x 4 knobs; each drives one parameter of a hosted effect */
    struct { slot_t *s; int t; float last; } mk[PP_NMACRO];
    int mk_page;        /* the Q-Link page the 4 on-screen knobs show, 0..3 (follows the last Q-Link turned, or a dot) */
    int mapping;        /* Map is on: the armed macro takes the next parameter moved in an effect */
    int armed;          /* the macro Map assigns, -1 none */
    int mk_since;       /* samples since the macros were last compared with their parameters */
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int npages_of(const pp_t *p, int sec) { int k = (p->n[sec] + PP_ROWS - 1) / PP_ROWS; return clampi(k, 1, PP_PAGES); }
static int npages(const pp_t *p) { return npages_of(p, p->sec); }
static void mark(pp_t *p, int i) { if (i >= 0 && i < PP_NKEYS) p->dirty[i] = 1; }

static void mark_hpre(pp_t *p) {   /* the opened effect's presets: bar, browser, MPC's PRESET menu */
    mark(p, PI_HPRESET); mark(p, PI_HPRE_NAME); mark(p, PI_HPRE_SHOWN); mark(p, PI_HBROWSING); mark(p, PI_HPRE_PAGE);
    for (int k = 0; k < PP_NHPRE; k++) { mark(p, PI_HPICK(k) + 1); mark(p, PI_HPICK(k) + 2); }
}
static void mark_sc(pp_t *p) {   /* the sidechain panel's parameters */
    mark(p, PI_SCING); mark(p, PI_SC_ONST); mark(p, PI_SC_NAME); mark(p, PI_SC_THR); mark(p, PI_SC_ATT); mark(p, PI_SC_REL);
    mark(p, PI_SC_KNEE); mark(p, PI_SC_RATIO); mark(p, PI_SC_GR);
}
static void mark_rows(pp_t *p) {   /* 1.4.0: each row's On/Off, Parallel, Send, SC */
    for (int r = 0; r < PP_ROWS; r++) { mark(p, PI_RX(r)); mark(p, PI_RX(r) + 2); mark(p, PI_RX(r) + 3); mark(p, PI_RX(r) + 4); }
}
static void mk_forget(pp_t *p, const slot_t *s);
static void mk_follow(pp_t *p);
static int slot_text(const slot_t *s, int t, int display, char *buf, int len);
static void mark_mk(pp_t *p) {   /* 1.5.0: the macro column and the Q-Links that follow it */
    mark(p, PI_MK_SHOWN); mark(p, PI_MK_PAGE); mark(p, PI_MK_MAPST); mark(p, PI_MK_CLRST);
    for (int k = 0; k < 4; k++) for (int j = 0; j < 4; j++) mark(p, PI_MK(k) + j);
    for (int n = 0; n < PP_NQ; n++) mark(p, PI_Q(n));
}
static void mark_rack(pp_t *p) {
    mark_mk(p);
    mark_rows(p);
    for (int r = 0; r < PP_ROWS; r++)
        for (int k = 1; k <= 3; k++) mark(p, PI_ROW(r) + k);   /* used / up / down: the options the skin hides rows by */
    mark(p, PI_PAGE); mark(p, PI_VIEW); mark(p, PI_EDITING); mark(p, PI_PAGES); mark(p, PI_PICKING); mark(p, PI_SECTION);
    for (int k = 0; k < PP_NPICK; k++) mark(p, PI_PICK(k) + 2);
    for (int n = 0; n < PP_NQ; n++) mark(p, PI_Q(n));   /* 1.4.1: on the rack, Parallel's Q-Links are its Sends */
    mark_hpre(p);
}
static int pick_pages(void) { int k = (g_ntypes + PP_NPICK - 1) / PP_NPICK; return k < 1 ? 1 : k; }
static void mark_proxies(pp_t *p) {
    for (int n = 0; n < PP_NQ; n++) mark(p, PI_Q(n));
    for (int n = 0; n < PP_MAX_EDIT; n++) mark(p, PI_E(n));
    mark(p, PI_VIEW);   /* an option param: the wrapper follows it with an UpdateDisplay, so MPC re-reads the proxies' names */
}

static slot_t *focused(const pp_t *p) {
    return p->focus >= 0 && p->fsec >= 0 && p->fsec < NSEC && p->focus < p->n[p->fsec] ? p->s[p->fsec][p->focus] : NULL;
}

/* the page whose Q-Link map applies: the one shown, or (on the rack) the focused plugin's first page */
static int qview(const pp_t *p) {
    slot_t *s = focused(p);
    if (!s) return 0;
    if (p->view > 0 && view_type(p->view) == s->type) return p->view;
    return g_types[s->type].view0;
}
/* 1.4.2: where a Q-Link sits on the Live III. It has 4 Q-Link knobs and shows the 16 Q-Links as 4 pages of 4 (seen on the
 * device, 2026-11-06): page 1 = Q-Links 13, 9, 5, 1 top to bottom, page 2 = 14, 10, 6, 2, page 3 = 15, 11, 7, 3, page 4 =
 * 16, 12, 8, 4 (generate.py's `qlinks "RACK"` order). "slot" = (page - 1) * 4 + knob (0 = top): what the user counts.
 * Q-Link n+1 <-> slot. */
static int q_slot(int n) { return (n % 4) * 4 + 3 - n / 4; }
static int slot_q(int sl) { return 4 * (3 - sl % 4) + sl / 4; }
static slot_t *q_send(const pp_t *p, int n) {   /* the Parallel track whose Send is on Q-Link n+1, or NULL: on the rack's
                                                    Parallel tab, Q-Link page 1 = tracks 1-4 (rack page 1), page 2 = 5-8.. */
    if (p->sc || p->view > 0 || p->sec != 1 || n < 0 || n >= PP_NQ) return NULL;
    int i = q_slot(n);
    return i < p->n[1] ? p->s[1][i] : NULL;
}
static int mk_shown(const pp_t *p) { return p->view == 0 && p->sec == 0 && !p->sc; }
static int q_mk(const pp_t *p, int n) {   /* 1.5.0: the macro on Q-Link n+1 (rack, Inserts tab: Q-Link page p = macros of
                                             page p, top knob first), or -1 */
    return mk_shown(p) && n >= 0 && n < PP_NQ ? q_slot(n) : -1;
}
static int sc_k(int n) {   /* the sidechain panel's knob on Q-Link n+1 (page 1: Threshold, Attack, Release, Knee; page 2,
                              top knob: Ratio), or -1 */
    int k = q_slot(n);
    return k < 5 ? k : -1;
}
static int q_target(const pp_t *p, int n) {   /* hosted parameter on Q-Link n+1, or -1 */
    if (p->sc) return -1;   /* the sidechain panel is up: the Q-Links are its knobs */
    if (p->view <= 0) return -1;   /* 1.4.1: the rack page: no hosted plugin on the Q-Links (Parallel: the Sends, q_send) */
    slot_t *s = focused(p);
    if (!s || n < 0 || n >= PP_NQ) return -1;
    int v = qview(p), t = v - g_types[s->type].view0 < PP_MAX_TABS ? g_types[s->type].qmap[v - g_types[s->type].view0][n] : -1;
    return t >= 0 && t < s->nparams ? t : -1;
}
/* hosted parameter t of the focused plugin moved: tell MPC about every proxy that shows it */
static void mark_hosted(pp_t *p, int t, int except_e, int except_q) {
    if (t < 0) return;
    if (t < PP_MAX_EDIT && t != except_e) mark(p, PI_E(t));
    for (int n = 0; n < PP_NQ; n++)
        if (n != except_q && q_target(p, n) == t) mark(p, PI_Q(n));
}

/* ---- the audioMaster callback the hosted plugins get ---------------------------------------------------------- */
/* set while this thread is inside a plugin's VSTPluginMain: its AEffect.user isn't ours yet (a plugin needn't zero it) */
static __thread int t_in_main;
static intptr_t host_cb(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt) {
    (void)v; (void)opt;
    slot_t *s = !t_in_main && e && e->magic == VST_MAGIC ? (slot_t *)e->user : NULL;   /* ours once slot_new() set it */
    switch (op) {
    case amVersion: return 2400;
    case amCurrentId: return e ? e->uniqueID : 0;
    case amAutomate:   /* the plugin changed a value itself (a meter, a trigger springing back) */
        if (s && s->owner && focused(s->owner) == s) mark_hosted(s->owner, idx, -1, -1);
        return 0;
    case amUpdateDisplay:   /* -> an UpdateDisplay from our wrapper; its program may have changed (its own selector) */
        if (s && s->owner && focused(s->owner) == s) { mark(s->owner, PI_VIEW); mark_hpre(s->owner); }
        return 1;
    case amGetTime: return s && s->owner ? (intptr_t)&s->owner->ti : 0;
    case amGetSampleRate: return 44100;
    case amGetBlockSize: return BLOCK;
    case amGetCurrentProcessLevel: return 0;
    case amGetAutomationState: return 1;   /* off */
    case amGetVendorString: if (ptr) strcpy(ptr, "Poli"); return 1;
    case amGetProductString: if (ptr) strcpy(ptr, "PoliPlug"); return 1;
    case amGetVendorVersion: return 1000;
    case amGetLanguage: return 1;
    case amCanDo:
        if (ptr && (!strcmp(ptr, "sendVstTimeInfo") || !strcmp(ptr, "sendVstEvents") || !strcmp(ptr, "sendVstMidiEvent")))
            return 1;
        return 0;
    case amWantMidi: case amIdle: case amBeginEdit: case amEndEdit: return 1;
    default: return 0;   /* amProcessEvents (MIDI out), amIOChanged, amSizeWindow, latency queries: not supported */
    }
}

/* ---- slots ------------------------------------------------------------------------------------------------------- */
static slot_t *slot_new(pp_t *p, int type) {
    vst_main_t m = lib_main(type, p->status, sizeof p->status);
    if (!m) return NULL;
    t_in_main = 1;
    AEffect *fx = m(host_cb);
    t_in_main = 0;
    if (!fx || fx->magic != VST_MAGIC) {
        snprintf(p->status, sizeof p->status, "%s didn't start", g_types[type].name);
        return NULL;
    }
    slot_t *s = calloc(1, sizeof *s);
    if (!s) { fx->dispatcher(fx, effClose, 0, 0, NULL, 0); return NULL; }
    s->owner = p;
    s->type = type;
    s->fx = fx;
    s->nparams = clampi(fx->numParams, 0, PP_MAX_EDIT);
    s->on = 1;
    s->mix = 1;
    s->send = 50.0f;
    s->send_g = 0.5f;
    s->thr = -24.0f; s->att = 5.0f; s->rel = 250.0f; s->knee = 6.0f; s->ratio = 4.0f;
    s->g = 1.0f;
    fx->user = s;
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, 44100.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, BLOCK, NULL, 0);
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0);
    for (int t = 0; t < s->nparams && !s->own_sel; t++) {   /* its skin has a preset selector of its own */
        char nm[512] = "";
        fx->dispatcher(fx, effGetParamName, t, 0, nm, 0);
        nm[sizeof nm - 1] = 0;
        s->own_sel = !strcmp(nm, "Preset <");
    }
    return s;
}

static void slot_free(slot_t *s) {
    if (!s) return;
    if (s->fx) {
        s->fx->dispatcher(s->fx, effMainsChanged, 0, 0, NULL, 0);
        s->fx->dispatcher(s->fx, effClose, 0, 0, NULL, 0);   /* the plugin frees itself */
    }
    free(s);
}

/* ---- 1.4.0: the sidechain (ducking) of a Parallel track, and its knobs as 0..1 (the Q-Links) ------------------- */
static const char *const SC_QNAME[5] = {"Threshold", "Attack", "Release", "Knee", "Ratio"};
static const int SC_KEYS[5] = {PI_SC_THR, PI_SC_ATT, PI_SC_REL, PI_SC_KNEE, PI_SC_RATIO};
static float logpos(float v, float lo, float hi) { return logf(v / lo) / logf(hi / lo); }
static float posval(float t, float lo, float hi) { t = t < 0 ? 0 : t > 1 ? 1 : t; return lo * powf(hi / lo, t); }
static float sc_norm(const slot_t *s, int k) {
    switch (k) {
    case 0: return (s->thr + 60.0f) / 60.0f;
    case 1: return logpos(s->att, PP_SC_ATT_LO, PP_SC_ATT_HI);
    case 2: return logpos(s->rel, PP_SC_REL_LO, PP_SC_REL_HI);
    case 3: return s->knee / 24.0f;
    case 4: return logpos(s->ratio, PP_SC_RATIO_LO, PP_SC_RATIO_HI);
    default: return s->send * 0.01f;
    }
}
static void sc_set_norm(slot_t *s, int k, float v) {
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    switch (k) {
    case 0: s->thr = -60.0f + 60.0f * v; break;
    case 1: s->att = posval(v, PP_SC_ATT_LO, PP_SC_ATT_HI); break;
    case 2: s->rel = posval(v, PP_SC_REL_LO, PP_SC_REL_HI); break;
    case 3: s->knee = 24.0f * v; break;
    case 4: s->ratio = posval(v, PP_SC_RATIO_LO, PP_SC_RATIO_HI); break;
    default: s->send = 100.0f * v; break;
    }
}
static int sc_text(const slot_t *s, int k, char *buf, int len) {
    switch (k) {
    case 0: return snprintf(buf, len, "%.0f dB", s->thr);
    case 1: return snprintf(buf, len, s->att < 10 ? "%.1f ms" : "%.0f ms", s->att);
    case 2: return s->rel < 1000 ? snprintf(buf, len, "%.0f ms", s->rel) : snprintf(buf, len, "%.2f s", s->rel * 0.001f);
    case 3: return snprintf(buf, len, "%.0f dB", s->knee);
    case 4: return snprintf(buf, len, s->ratio < 10 ? "%.1f:1" : "%.0f:1", s->ratio);
    default: return snprintf(buf, len, "%.0f%%", s->send);
    }
}
/* soft-knee gain change (dB, <= 0) for a detector level L dB over threshold T, ratio R, knee width W */
static float sc_gain_db(float L, float T, float R, float W) {
    float over = L - T, slope = 1.0f / R - 1.0f;
    if (W > 0.01f && 2 * over > -W && 2 * over < W) { float x = over + W * 0.5f; return slope * x * x / (2 * W); }
    return over > 0 ? slope * over : 0.0f;
}
/* the ducking gain of track s for n samples of the key (p->key: the vocal's peak, both channels) into g[] */
static void sc_run(pp_t *p, slot_t *s, float *g, int n) {
    float env = s->env, gain = s->g;
    float a_att = 1.0f - expf(-1.0f / (s->att * 0.001f * 44100.0f)), a_rel = 1.0f - expf(-1.0f / (s->rel * 0.001f * 44100.0f));
    for (int j0 = 0; j0 < n; j0 += 16) {
        int m = n - j0 < 16 ? n - j0 : 16;
        float tgt = 1.0f;
        if (s->sc_on) {
            for (int j = j0; j < j0 + m; j++) {
                float x = p->key[j];
                env += (x > env ? a_att : a_rel) * (x - env);
            }
            if (!(env > 1e-9f)) env = 0;   /* flushes denormals and a NaN */
            float gr = sc_gain_db(20.0f * log10f(env + 1e-9f), s->thr, s->ratio, s->knee);
            tgt = powf(10.0f, (gr < -60.0f ? -60.0f : gr) * 0.05f);
        } else env = 0;
        float step = (tgt - gain) / m;   /* a ramp over the 16 samples: no steps */
        for (int j = j0; j < j0 + m; j++) { gain += step; g[j] = gain; }
    }
    s->env = env;
    s->g = gain;
    s->gr_db = 20.0f * log10f(gain > 1e-6f ? gain : 1e-6f);
}

static void set_status(pp_t *p) {
    slot_t *f = focused(p);
    char head[64], q[96] = "";
    if (p->sec == 0) {
        snprintf(head, sizeof head, "%d/%d", p->n[0], PP_MAX_SLOTS);
        if (p->n[1]) snprintf(head + strlen(head), sizeof head - strlen(head), "  +%d parallel", p->n[1]);
    } else snprintf(head, sizeof head, "Parallel %d/%d", p->n[1], PP_MAX_SLOTS);
    /* 1.3.2: no "Send 40 %" here any more. The status is a polled text readout: a change makes the wrapper send an
     * UpdateDisplay, so every step of a Send drag rebuilt MPC's screen and the touch drag was dropped (the Send knob
     * "sometimes didn't respond"). The knob shows its own value. */
    /* 1.4.1: on the rack the Q-Links never stay on the last opened plugin: Parallel = the tracks' Sends, Inserts = none */
    (void)f;
    if (p->sec == 1 && p->n[1]) snprintf(q, sizeof q, "  |  Q-Link pages: the Sends, 4 per page");
    if (p->n[0] + p->n[1] == 0) snprintf(p->status, sizeof p->status, "Empty rack: tap + Add plugin");
    else if (p->n[p->sec] == 0) snprintf(p->status, sizeof p->status, "%s is empty: tap + Add plugin%s",
                                         p->sec ? "Parallel" : "Inserts", q);
    else snprintf(p->status, sizeof p->status, "%s%s", head, q);
    if (p->mapping)   /* 1.5.0 */
        snprintf(p->status, sizeof p->status, "Map: macro %d picked. Open an effect and move a control (or turn another knob)",
                 p->armed + 1);
}

static void refocus(pp_t *p, int sec, int f) {
    p->fsec = sec >= 0 && sec < NSEC ? sec : 0;
    p->focus = f >= 0 && f < p->n[p->fsec] ? f : -1;
    if (p->view > 0 && (!focused(p) || view_type(p->view) != focused(p)->type)) p->view = 0;
    p->hbrowse = 0;
    mark_proxies(p);
    mark_hpre(p);
}

static int add_slot(pp_t *p, int sec, int type) {
    if (type < 0 || type >= g_ntypes || sec < 0 || sec >= NSEC) return -1;
    if (p->n[sec] >= PP_MAX_SLOTS) {
        snprintf(p->status, sizeof p->status, "%s full (%d plugins)", sec ? "Parallel" : "Rack", PP_MAX_SLOTS);
        return -1;
    }
    slot_t *s = slot_new(p, type);
    if (!s) return -1;
    p->s[sec][p->n[sec]++] = s;
    return p->n[sec] - 1;
}

static void remove_slot(pp_t *p, int sec, int i) {
    if (i < 0 || i >= p->n[sec]) return;
    slot_t *s = p->s[sec][i];
    if (p->sc == s) { p->sc = NULL; mark_sc(p); }
    mk_forget(p, s);
    mark_mk(p);
    memmove(&p->s[sec][i], &p->s[sec][i + 1], (size_t)(p->n[sec] - i - 1) * sizeof p->s[0][0]);
    p->s[sec][--p->n[sec]] = NULL;
    slot_free(s);
    int f = p->focus, fs = p->fsec;
    if (fs == sec) {
        if (f == i) f = -1;
        else if (f > i) f--;
    }
    p->page[sec] = clampi(p->page[sec], 0, npages_of(p, sec) - 1);
    refocus(p, fs, f);
}

static void clear_all(pp_t *p) {
    for (int sec = 0; sec < NSEC; sec++) {
        while (p->n[sec]) slot_free(p->s[sec][--p->n[sec]]), p->s[sec][p->n[sec]] = NULL;
        p->page[sec] = 0;
    }
    p->focus = -1;
    p->fsec = 0;
    p->view = 0;
    p->hbrowse = 0;
    p->sc = NULL;
    for (int m = 0; m < PP_NMACRO; m++) p->mk[m].s = NULL;
    p->mapping = 0;
    p->armed = -1;
}

static void swap_slots(pp_t *p, int sec, int a, int b) {
    if (a < 0 || b < 0 || a >= p->n[sec] || b >= p->n[sec]) return;
    slot_t *t = p->s[sec][a]; p->s[sec][a] = p->s[sec][b]; p->s[sec][b] = t;
    if (p->fsec != sec) return;
    if (p->focus == a) p->focus = b;
    else if (p->focus == b) p->focus = a;
}

/* ---- engine API -------------------------------------------------------------------------------------------------- */
void pp_scan_once(const char *plugin_dir);   /* scan.c */

static void *pp_create(const char *data_dir) {
    pp_t *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->armed = -1;
    snprintf(p->dir, sizeof p->dir, "%s", data_dir ? data_dir : ".");
    p->focus = -1;
    p->send = 50.0f;
    p->send_g = 0.5f;
    p->ti.sampleRate = 44100;
    p->ti.tempo = 120;
    p->ti.timeSigNumerator = p->ti.timeSigDenominator = 4;
    p->ti.flags = kVstTempoValid | kVstTimeSigValid;
    discover_once(p->dir);   /* the installed effects; rewrites our skin when they changed (shown from the next insert) */
    pp_scan_once(p->dir);   /* PoliPlug-scan.txt: what this MPC has that could be hosted (read-only) */
    set_status(p);
    if (g_ntypes == 0) snprintf(p->status, sizeof p->status, "No installed effects found");
    else if (g_skin_changed) snprintf(p->status, sizeof p->status, "%d found: re-insert PoliPlug to see them", g_ntypes);
    else if (g_note[0] && !strstr(g_note, "effects found")) snprintf(p->status, sizeof p->status, "%.47s", g_note);   /* a problem */
    return p;
}

static void pp_destroy(void *inst) {
    pp_t *p = inst;
    clear_all(p);
    free(p);
}

static void pp_midi(void *inst, const uint8_t *msg, int len) {
    pp_t *p = inst;
    VstMidiEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = 1;   /* kVstMidiType */
    ev.byteSize = sizeof ev;
    memcpy(ev.midiData, msg, len < 3 ? (size_t)len : 3);
    VstEvents evs = {1, 0, {(VstEvent *)&ev, NULL}};
    for (int sec = 0; sec < NSEC; sec++)
        for (int i = 0; i < p->n[sec]; i++) p->s[sec][i]->fx->dispatcher(p->s[sec][i]->fx, effProcessEvents, 0, 0, &evs, 0);
}

static void pp_song_pos(void *inst, double ppq, double bpm, int playing, int frames) {
    pp_t *p = inst;
    (void)frames;
    p->ti.tempo = bpm > 0 ? bpm : 120;
    p->ti.ppqPos = ppq >= 0 ? ppq : 0;
    p->ti.flags = kVstTempoValid | kVstTimeSigValid | (ppq >= 0 ? kVstPpqPosValid : 0) | (playing ? kVstTransportPlaying : 0);
}

/* run one hosted effect from in[0..1] to out[0..1]; 0 when it can't (mono or silent plugin: leave the audio as it was) */
static int slot_proc(pp_t *p, slot_t *s, float *inL, float *inR, float *outL, float *outR, int n) {
    AEffect *fx = s->fx;
    float *in[MAX_CH], *out[MAX_CH];
    if (clampi(fx->numOutputs, 0, MAX_CH) < 2) return 0;
    in[0] = inL; in[1] = inR; out[0] = outL; out[1] = outR;
    for (int c = 2; c < MAX_CH; c++) { in[c] = out[c] = p->scratch[c]; memset(p->scratch[c], 0, sizeof p->scratch[c]); }
    if (fx->processReplacing && (fx->flags & effFlagsCanReplacing)) fx->processReplacing(fx, in, out, n);
    else if (fx->process) {
        memset(outL, 0, sizeof(float) * n);
        memset(outR, 0, sizeof(float) * n);
        fx->process(fx, in, out, n);
    } else return 0;
    return 1;
}

#define GLIDE 0.00227f   /* 1 - exp(-1 / (0.010 s * 44100)): 10 ms */
static float glide_to(float m, float tgt) { return fabsf(tgt - m) < 1e-4f ? tgt : m; }

/* the Inserts in series on p->buf[0] (in and out); an effect switched Off fades out (10 ms) and then isn't run */
static void run_inserts(pp_t *p, int n) {
    int cur = 0;
    for (int i = 0; i < p->n[0]; i++) {
        slot_t *s = p->s[0][i];
        float tgt = s->on ? 1.0f : 0.0f;
        if (!s->on && s->mix <= 0.0f) { s->mix = 0; continue; }
        float *iL = p->buf[cur][0], *iR = p->buf[cur][1], *oL = p->buf[cur ^ 1][0], *oR = p->buf[cur ^ 1][1];
        if (!slot_proc(p, s, iL, iR, oL, oR, n)) continue;
        if (s->mix != tgt || tgt < 1.0f) {
            float m = s->mix;
            for (int j = 0; j < n; j++) {
                m += (tgt - m) * GLIDE;
                oL[j] = iL[j] + m * (oL[j] - iL[j]);
                oR[j] = iR[j] + m * (oR[j] - iR[j]);
            }
            s->mix = glide_to(m, tgt);
        }
        cur ^= 1;
    }
    if (cur) {
        memcpy(p->buf[0][0], p->buf[1][0], sizeof(float) * n);
        memcpy(p->buf[0][1], p->buf[1][1], sizeof(float) * n);
    }
}

/* 1.4.0: every Parallel effect is its own track: Send % of the Inserts' output (p->buf[0]) goes in, its output x On/Off
 * x its sidechain ducking is added back. With nothing in Parallel it adds nothing. */
static void run_parallel(pp_t *p, int n) {
    if (!p->n[1]) return;
    int any_sc = 0;
    for (int i = 0; i < p->n[1]; i++) any_sc |= p->s[1][i]->sc_on || p->s[1][i]->g < 1.0f;
    for (int c = 0; c < 2; c++) {
        memcpy(p->dry[c], p->buf[0][c], sizeof(float) * n);
        memset(p->acc[c], 0, sizeof(float) * n);
    }
    if (any_sc)
        for (int j = 0; j < n; j++) {
            float a = fabsf(p->dry[0][j]), b = fabsf(p->dry[1][j]);
            p->key[j] = a > b ? a : b;
        }
    for (int i = 0; i < p->n[1]; i++) {
        slot_t *s = p->s[1][i];
        float tgt = s->on ? 1.0f : 0.0f, ts = s->send * 0.01f;
        if (!s->on && s->mix <= 0.0f) { s->mix = 0; s->send_g = ts; s->env = 0; s->g = 1; s->gr_db = 0; continue; }
        float gs = s->send_g;
        for (int j = 0; j < n; j++) {
            gs += (ts - gs) * GLIDE;
            p->pin[0][j] = p->dry[0][j] * gs;
            p->pin[1][j] = p->dry[1][j] * gs;
        }
        s->send_g = glide_to(gs, ts);
        if (!slot_proc(p, s, p->pin[0], p->pin[1], p->pout[0], p->pout[1], n)) {
            memcpy(p->pout[0], p->pin[0], sizeof(float) * n);
            memcpy(p->pout[1], p->pin[1], sizeof(float) * n);
        }
        if (s->sc_on || s->g < 1.0f) sc_run(p, s, p->gbuf, n);
        else for (int j = 0; j < n; j++) p->gbuf[j] = 1.0f;
        float m = s->mix;
        for (int j = 0; j < n; j++) {
            m += (tgt - m) * GLIDE;
            float g = m * p->gbuf[j];
            p->acc[0][j] += p->pout[0][j] * g;
            p->acc[1][j] += p->pout[1][j] * g;
        }
        s->mix = glide_to(m, tgt);
    }
    for (int c = 0; c < 2; c++)
        for (int j = 0; j < n; j++) p->buf[0][c][j] = p->dry[c][j] + p->acc[c][j];
}

static int16_t f2s(float f) { f *= 32768.0f; return f >= 32767.0f ? 32767 : f <= -32768.0f ? -32768 : (int16_t)lrintf(f); }

static void pp_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    pp_t *p = inst;
    for (int done = 0; done < frames;) {
        int n = frames - done < BLOCK ? frames - done : BLOCK;
        const int16_t *in = in_lr + 2 * done;
        for (int j = 0; j < n; j++) {
            p->buf[0][0][j] = in[2 * j] * (1.0f / 32768.0f);
            p->buf[0][1][j] = in[2 * j + 1] * (1.0f / 32768.0f);
        }
        run_inserts(p, n);    /* Inserts, in series */
        run_parallel(p, n);   /* Parallel tracks, each fed its own Send, added back */
        /* the sidechain panel's meter: reported when its frame (1 dB) changes, at most every 50 ms */
        p->sc_since += n;
        if (p->sc && p->sc_since >= 2205) {
            int f = (int)lroundf(-p->sc->gr_db);
            f = clampi(f, 0, PP_SC_METER_FRAMES - 1);
            if (f != p->sc_frame) { p->sc_frame = f; mark(p, PI_SC_GR); p->sc_since = 0; }
        }
        p->mk_since += n;   /* 1.5.0: macros follow their parameters (moved on the effect's page, a preset..), ~20 times a second */
        if (p->mk_since >= 2205) { p->mk_since = 0; mk_follow(p); }
        int16_t *out = out_lr + 2 * done;
        for (int j = 0; j < n; j++) {
            out[2 * j] = f2s(p->buf[0][0][j]);
            out[2 * j + 1] = f2s(p->buf[0][1][j]);
        }
        p->ti.samplePos += n;
        done += n;
    }
}

static void pp_render(void *inst, int16_t *out_lr, int frames) {
    (void)inst;
    memset(out_lr, 0, sizeof(int16_t) * 2 * frames);
}

/* key "row<r>_<what>" / "q<n>" / "e<n>" -> its parts */
static int parse_row(const char *key, int *r, const char **what) {
    if (strncmp(key, "row", 3) || key[3] < '1' || key[3] > '0' + PP_ROWS) return 0;
    *r = key[3] - '1';
    *what = key[4] == '_' ? key + 5 : key + 4;
    return 1;
}
static int parse_num(const char *key, char c, int *n, const char **rest) {
    if (key[0] != c || key[1] < '0' || key[1] > '9') return 0;
    char *end;
    long v = strtol(key + 1, &end, 10);
    *n = (int)v;
    *rest = end;
    return 1;
}

static void sc_open(pp_t *p, slot_t *s);
static void open_slot(pp_t *p, int sec, int i) {
    if (i < 0 || i >= p->n[sec]) return;
    if (p->sc) sc_open(p, NULL);
    p->fsec = sec;
    p->focus = i;
    p->view = g_types[p->s[sec][i]->type].view0;
    p->hbrowse = 0;
    mark_rack(p);
    mark_proxies(p);
}

/* ---- the opened effect's presets (its VST programs) ------------------------------------------------------------- */
/* 1.4.2: MPC's PRESET menu follows the screen like the Q-Links: the opened effect's presets on its page; on the rack
 * "(open an effect)". Before, the rack kept the last opened effect's preset name up there ("Hard Tune" after a reload). */
static slot_t *hslot(const pp_t *p) { return p->view > 0 ? focused(p) : NULL; }
static int hcount(const pp_t *p) { slot_t *s = hslot(p); return s ? clampi(s->fx->numPrograms, 0, PP_NPROG) : 0; }
static int hcur(const pp_t *p) {
    slot_t *s = hslot(p);
    return s && hcount(p) ? clampi((int)s->fx->dispatcher(s->fx, effGetProgram, 0, 0, NULL, 0), 0, hcount(p) - 1) : 0;
}
static int hname(const pp_t *p, int idx, char *buf, int len) {
    slot_t *s = hslot(p);
    char nm[512] = "";   /* VST2 says 24 bytes; some plugins write more */
    if (!s || idx < 0 || idx >= hcount(p)) return snprintf(buf, len, " ");
    if (!s->fx->dispatcher(s->fx, effGetProgramNameIndexed, idx, -1, nm, 0) || !nm[0]) {
        nm[0] = 0;
        if (idx == hcur(p)) s->fx->dispatcher(s->fx, effGetProgramName, 0, 0, nm, 0);   /* only the current one's */
    }
    nm[sizeof nm - 1] = 0;
    char *x = nm;
    while (*x == ' ') x++;
    return *x ? snprintf(buf, len, "%s", x) : snprintf(buf, len, "Preset %d", idx + 1);
}
/* the bar over the opened page: effects whose skin has no selector of their own, with more than one preset */
static int hpre_visible(const pp_t *p) { slot_t *s = focused(p); return p->view > 0 && s && !s->own_sel && hcount(p) > 1; }
static int hpages(const pp_t *p) { int k = (hcount(p) + PP_NHPRE - 1) / PP_NHPRE; return k < 1 ? 1 : k; }
static void set_hprog(pp_t *p, int idx) {
    slot_t *s = hslot(p);
    if (!s || idx < 0 || idx >= hcount(p)) return;
    s->fx->dispatcher(s->fx, effBeginSetProgram, 0, 0, NULL, 0);
    s->fx->dispatcher(s->fx, effSetProgram, 0, idx, NULL, 0);
    s->fx->dispatcher(s->fx, effEndSetProgram, 0, 0, NULL, 0);
    mark_proxies(p);   /* every value of its page may have moved */
    mark_hpre(p);
}

static void hosted_set(pp_t *p, int t, float v) {
    slot_t *s = focused(p);
    if (!s || t < 0 || t >= s->nparams) return;
    s->fx->setParameter(s->fx, t, v < 0 ? 0 : v > 1 ? 1 : v);
}
static float hosted_get(const pp_t *p, int t) {
    slot_t *s = focused(p);
    if (!s || t < 0 || t >= s->nparams) return 0;
    return s->fx->getParameter(s->fx, t);
}

static void restore_state(pp_t *p, const char *val);

/* the slot shown on row r of the rack, or NULL */
static slot_t *row_slot(const pp_t *p, int r) {
    int sec = p->sec, i = p->page[sec] * PP_ROWS + r;
    return p->view == 0 && !p->picking && r >= 0 && r < PP_ROWS && i < p->n[sec] ? p->s[sec][i] : NULL;
}
static int slot_index(const pp_t *p, int sec, const slot_t *s) {
    for (int i = 0; i < p->n[sec]; i++) if (p->s[sec][i] == s) return i;
    return -1;
}
static int parse_val(const char *val, float lo, float hi, float *out) {   /* a finite number, clamped */
    char *end;
    double d = strtod(val, &end);
    if (end == val || !(d == d) || d < -1e9 || d > 1e9) return 0;
    *out = d < lo ? lo : d > hi ? hi : (float)d;
    return 1;
}
static void sc_open(pp_t *p, slot_t *s) {   /* the sidechain panel for Parallel track s (NULL: close it) */
    p->sc = s;
    p->sc_frame = -1;
    p->sc_since = 1 << 20;
    mark_sc(p);
    mark_rows(p);
    for (int n = 0; n < PP_NQ; n++) mark(p, PI_Q(n));   /* the Q-Links follow the panel's knobs, and back */
    mark(p, PI_VIEW);   /* an option: the wrapper follows with an UpdateDisplay, MPC re-reads the Q-Link names */
}

/* ---- 1.5.0: macro knobs ------------------------------------------------------------------------------------------ */
static int mk_ok(const pp_t *p, int m) { return m >= 0 && m < PP_NMACRO && p->mk[m].s && p->mk[m].t >= 0 && p->mk[m].t < p->mk[m].s->nparams; }
static void mk_forget(pp_t *p, const slot_t *s) {   /* an effect leaves the rack: its macros go empty */
    for (int m = 0; m < PP_NMACRO; m++) if (p->mk[m].s == s) p->mk[m].s = NULL;
}
static void mk_set_page(pp_t *p, int pg) {
    pg = clampi(pg, 0, 3);
    if (pg != p->mk_page) { p->mk_page = pg; mark_mk(p); }
}
static void mk_turn(pp_t *p, int m, float v, int from_q) {   /* a macro knob or its Q-Link moved */
    mk_set_page(p, m / 4);
    if (p->mapping) {   /* Map: touching a knob picks it (nothing moves) */
        if (p->armed != m) { p->armed = m; mark_mk(p); }
        mark(p, PI_MK(m % 4)); mark(p, PI_Q(slot_q(m)));   /* the knob / Q-Link snap back to the parameter's value */
        return;
    }
    if (!mk_ok(p, m) || !(v == v)) return;
    slot_t *s = p->mk[m].s;
    s->fx->setParameter(s->fx, p->mk[m].t, v < 0 ? 0 : v > 1 ? 1 : v);
    p->mk[m].last = s->fx->getParameter(s->fx, p->mk[m].t);
    if (from_q) mark(p, PI_MK(m % 4));
    else mark(p, PI_Q(slot_q(m)));
}
static void mk_status(pp_t *p);
static void mk_capture(pp_t *p, int t) {   /* Map is on and a parameter of the opened effect moved: it goes on the armed macro */
    slot_t *s = focused(p);
    if (!p->mapping || p->armed < 0 || !s || t < 0 || t >= s->nparams) return;
    int m = p->armed;
    p->mk[m].s = s;
    p->mk[m].t = t;
    p->mk[m].last = s->fx->getParameter(s->fx, t);
    p->mapping = 0;
    p->armed = -1;
    p->mk_page = m / 4;
    char nm[64];
    slot_text(s, t, 0, nm, sizeof nm);
    snprintf(p->status, sizeof p->status, "Macro %d (Q-Link page %d, knob %d) = %.30s", m + 1, m / 4 + 1, m % 4 + 1, nm);
    mark(p, PI_STATUS);
    mark_mk(p);
}
static void mk_status(pp_t *p) { set_status(p); mark(p, PI_STATUS); }
/* the macros whose parameter moved some other way (its page, its own preset, the effect itself): tell MPC */
static void mk_follow(pp_t *p) {
    for (int m = 0; m < PP_NMACRO; m++) {
        if (!mk_ok(p, m)) continue;
        float v = p->mk[m].s->fx->getParameter(p->mk[m].s->fx, p->mk[m].t);
        if (fabsf(v - p->mk[m].last) > 1e-5f) {
            p->mk[m].last = v;
            if (m / 4 == p->mk_page) mark(p, PI_MK(m % 4));
            mark(p, PI_Q(slot_q(m)));   /* a float: no UpdateDisplay */
        }
    }
}

static void pp_set_param(void *inst, const char *key, const char *val) {
    pp_t *p = inst;
    float v = (float)atof(val);
    int on = v >= 0.5f, r, n;
    const char *what;
    if (!strcmp(key, "state")) { restore_state(p, val); return; }
    if (parse_row(key, &r, &what)) {
        int sec = p->sec, i = p->page[sec] * PP_ROWS + r;
        slot_t *rs = row_slot(p, r);
        if (!strcmp(what, "send")) {   /* 1.4.0: a Parallel track's Send (a knob: values, not presses) */
            float d;
            if (rs && sec == 1 && parse_val(val, 0, 100, &d)) {
                rs->send = d;
                { int qi = slot_index(p, 1, rs); if (qi >= 0 && qi < PP_NQ) mark(p, PI_Q(slot_q(qi))); }   /* its Q-Link
                                                                                                    (a float: no UpdateDisplay) */   /* 1.4.1: its Q-Link
                                                                                       (a float: no UpdateDisplay) */
            }
            return;
        }
        if (!on || p->view > 0 || p->picking || p->sc || i >= p->n[sec]) return;   /* only presses; rows are hidden under a page,
                                                                                     the list or the sidechain panel */
        if (!strcmp(what, "tog")) { rs->on = !rs->on; mark(p, PI_RX(r)); return; }
        if (!strcmp(what, "sc")) { if (sec == 1) sc_open(p, rs); return; }
        if (!strcmp(what, "open")) open_slot(p, sec, i);
        else if (!strcmp(what, "mvup") && i > 0) {
            swap_slots(p, sec, i, i - 1);
            if (r == 0) p->page[sec]--;   /* it moved onto the previous page: follow it */
            mark_rack(p);
        } else if (!strcmp(what, "mvdn") && i < p->n[sec] - 1) {
            swap_slots(p, sec, i, i + 1);
            if (r == PP_ROWS - 1) p->page[sec]++;
            mark_rack(p);
        } else if (!strcmp(what, "bye")) {
            remove_slot(p, sec, i);
            mark_rack(p);
        } else return;
        set_status(p);
        return;   /* row text, used/up/down are host writes we ignore: the engine owns them */
    }
    if (parse_num(key, 'q', &n, &what) && !*what) {
        if (p->sc) {   /* the sidechain panel is up: its five knobs (sc_k) */
            int k = n >= 1 && n <= PP_NQ ? sc_k(n - 1) : -1;
            if (k >= 0 && v == v) { sc_set_norm(p->sc, k, v); mark(p, SC_KEYS[k]); }
            return;
        }
        slot_t *qs = q_send(p, n - 1);
        if (qs) {   /* Parallel on the rack: the tracks' Sends (q_send) */
            if (v == v) {
                qs->send = v < 0 ? 0 : v > 1 ? 100 : v * 100.0f;
                for (int k = 0; k < PP_ROWS; k++) mark(p, PI_RX(k) + 3);
            }
            return;
        }
        int m = q_mk(p, n - 1);
        if (m >= 0) { mk_turn(p, m, v, 1); return; }   /* 1.5.0: Inserts on the rack: the macros */
        int t = q_target(p, n - 1);
        hosted_set(p, t, v);
        mark_hosted(p, t, -1, n - 1);
        if (t >= 0) mk_capture(p, t);
        return;
    }
    if (parse_num(key, 'e', &n, &what) && !*what) {
        hosted_set(p, n, v);
        mark_hosted(p, n, n, -1);
        if (p->view > 0) mk_capture(p, n);
        return;
    }
    /* 1.5.0: the macro column */
    if (!strncmp(key, "mk", 2) && key[2] >= '1' && key[2] <= '4' && !key[3]) {
        if (mk_shown(p)) mk_turn(p, p->mk_page * 4 + key[2] - '1', v, 0);
        return;
    }
    if (!strncmp(key, "mk_pg", 5) && key[5] >= '1' && key[5] <= '4' && !key[6]) {
        if (on && mk_shown(p)) {
            mk_set_page(p, key[5] - '1');
            if (p->mapping && p->armed / 4 != p->mk_page) { p->armed = p->mk_page * 4; mk_status(p); mark_mk(p); }
        }
        return;
    }
    if (!strcmp(key, "mk_map")) {
        if (on && (p->mapping || mk_shown(p))) {
            p->mapping = !p->mapping;
            p->armed = -1;
            if (p->mapping) {   /* the first empty knob of the page shown, else its first */
                p->armed = p->mk_page * 4;
                for (int k = 0; k < 4; k++) if (!mk_ok(p, p->mk_page * 4 + k)) { p->armed = p->mk_page * 4 + k; break; }
                mk_status(p);
            } else { set_status(p); mark(p, PI_STATUS); }
            mark_mk(p);
        }
        return;
    }
    if (!strcmp(key, "mk_clear")) {
        if (on && p->mapping && p->armed >= 0) {
            p->mk[p->armed].s = NULL;
            snprintf(p->status, sizeof p->status, "Macro %d cleared", p->armed + 1);
            mark(p, PI_STATUS);
            p->mapping = 0;
            p->armed = -1;
            mark_mk(p);
        }
        return;
    }
    if (!strcmp(key, "page")) {
        p->page[p->sec] = clampi((int)lroundf(v), 0, npages(p) - 1);   /* only pages that hold plugins */
        mark_rack(p);
    } else if (!strcmp(key, "page_prev") || !strcmp(key, "page_next")) {
        if (on && p->picking) {   /* the same arrows page through the effect list while it is up */
            int k = pick_pages();
            p->pick_page = (p->pick_page + (key[5] == 'n' ? 1 : k - 1)) % k;
            mark_rack(p);
        } else if (on) {
            int k = npages(p);
            p->page[p->sec] = (p->page[p->sec] + (key[5] == 'n' ? 1 : k - 1)) % k;
            mark_rack(p);
        }
    } else if (!strcmp(key, "add_open")) {   /* + Add plugin: show the installed effects; a second tap closes the list */
        if (on && p->view == 0 && !p->sc) {
            p->picking = !p->picking;
            p->pick_page = 0;
            mark_rack(p);
            if (p->picking) preload_once();
            if (p->picking)
                snprintf(p->status, sizeof p->status, g_ntypes ? "Pick an effect (%d found)"
                         : "No installed effects found", g_ntypes);
            else set_status(p);
        }
    } else if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '0' + PP_NPICK && !key[5]) {
        int t = p->pick_page * PP_NPICK + (key[4] - '1');
        if (on && p->picking && t < g_ntypes) {
            p->picking = 0;
            int i = add_slot(p, p->sec, t);
            if (i >= 0) {
                p->page[p->sec] = i / PP_ROWS;
                refocus(p, p->sec, i);   /* the Q-Links follow the plugin just added */
                set_status(p);
            }
            mark_rack(p);
        }
    } else if (!strcmp(key, "sec_ins") || !strcmp(key, "sec_par") || !strcmp(key, "section")) {
        /* the tabs (and a host write of "section"): which chain the rack shows; not while a page is open */
        int tab = !strcmp(key, "sec_ins") ? 0 : !strcmp(key, "sec_par") ? 1 : -1;
        if ((tab >= 0 && !on) || p->view > 0) return;
        int want = tab >= 0 ? tab : (int)lroundf(v) != 0;
        if (p->picking) p->picking = 0;
        if (p->sc) sc_open(p, NULL);
        p->sec = want ? 1 : 0;
        mark_rack(p);
        set_status(p);
    } else if (!strcmp(key, "send")) {
        /* 1.3's single Send (legacy since 1.4.0: each Parallel track has its own): ignored */
    } else if (!strcmp(key, "sc_close")) {
        if (on && p->sc) { sc_open(p, NULL); set_status(p); }
    } else if (!strcmp(key, "sc_tog")) {
        if (on && p->sc) { p->sc->sc_on = !p->sc->sc_on; mark(p, PI_SC_ONST); mark_rows(p); }
    } else if (p->sc && (!strcmp(key, "sc_thr") || !strcmp(key, "sc_att") || !strcmp(key, "sc_rel") || !strcmp(key, "sc_knee") ||
                         !strcmp(key, "sc_ratio"))) {
        float d;
        slot_t *t = p->sc;
        if (!strcmp(key, "sc_thr")) { if (parse_val(val, -60, 0, &d)) { t->thr = d; mark(p, PI_Q(slot_q(0))); } }
        else if (!strcmp(key, "sc_att")) { if (parse_val(val, 0, 100, &d)) { t->att = posval(d * 0.01f, PP_SC_ATT_LO, PP_SC_ATT_HI); mark(p, PI_Q(slot_q(1))); } }
        else if (!strcmp(key, "sc_rel")) { if (parse_val(val, 0, 100, &d)) { t->rel = posval(d * 0.01f, PP_SC_REL_LO, PP_SC_REL_HI); mark(p, PI_Q(slot_q(2))); } }
        else if (!strcmp(key, "sc_knee")) { if (parse_val(val, 0, 24, &d)) { t->knee = d; mark(p, PI_Q(slot_q(3))); } }
        else if (parse_val(val, 0, 100, &d)) { t->ratio = posval(d * 0.01f, PP_SC_RATIO_LO, PP_SC_RATIO_HI); mark(p, PI_Q(slot_q(4))); }
    } else if (!strcmp(key, "hpreset")) {   /* MPC's PRESET menu (the wrapper's programs) or automation */
        char *end;
        double d = strtod(val, &end);
        if (end != val && d == d && d >= 0 && d < PP_NPROG && (int)lround(d) != hcur(p)) set_hprog(p, (int)lround(d));
    } else if (!strcmp(key, "hpre_prev") || !strcmp(key, "hpre_next")) {
        int k = hcount(p);
        if (on && p->view > 0 && k > 1) set_hprog(p, (hcur(p) + (key[5] == 'n' ? 1 : k - 1)) % k);
    } else if (!strcmp(key, "hpre_list")) {   /* the name: opens / closes the browser on the current preset's page */
        if (on && hpre_visible(p)) {
            p->hbrowse = !p->hbrowse;
            p->hpage = hcur(p) / PP_NHPRE;
            mark_hpre(p);
        }
    } else if (!strcmp(key, "hpre_pprev") || !strcmp(key, "hpre_pnext")) {
        if (on && p->hbrowse) {
            int k = hpages(p);
            p->hpage = (p->hpage + (key[6] == 'n' ? 1 : k - 1)) % k;
            mark_hpre(p);
        }
    } else if (!strncmp(key, "hpick", 5) && key[5] >= '1' && key[5] <= '9') {
        int n = atoi(key + 5), idx = p->hpage * PP_NHPRE + n - 1;
        if (on && p->hbrowse && n >= 1 && n <= PP_NHPRE && idx < hcount(p)) {
            p->hbrowse = 0;
            set_hprog(p, idx);   /* even the current one: it brings a tweaked preset back */
        }
    } else if (!strcmp(key, "view_back")) {
        if (on && p->view) { p->view = 0; p->hbrowse = 0; mark_rack(p); mark_proxies(p); set_status(p); }
    } else if (!strcmp(key, "view_prev") || !strcmp(key, "view_next")) {
        slot_t *s = focused(p);
        if (on && s && p->view) {
            const pp_rtype_t *t = &g_types[s->type];
            int k = p->view - t->view0 + (key[5] == 'n' ? 1 : -1);
            p->view = t->view0 + (k + t->nviews) % t->nviews;
            p->hbrowse = 0;
            mark_rack(p);
            mark_proxies(p);
        }
    }
    /* page_text, status, view, editing, pages: shown by the engine; host writes are ignored */
}

static int hosted_text(const pp_t *p, int t, int display, char *buf, int len) { return slot_text(focused(p), t, display, buf, len); }
/* a parameter's name (display 0) or value text (display 1) of any hosted effect */
static int slot_text(const slot_t *s, int t, int display, char *buf, int len) {
    if (!s || t < 0 || t >= s->nparams) return snprintf(buf, len, "%s", display ? " " : "-");
    /* hosted plugins write as much text as their own build allows (the Airwindows ports: 128 bytes, PARAM_TEXT_MAX), more
     * than the VST2 spec's 8/24/32: room for any of them */
    char a[512] = "", b[512] = "";
    if (display) {
        s->fx->dispatcher(s->fx, effGetParamDisplay, t, 0, a, 0);
        s->fx->dispatcher(s->fx, effGetParamLabel, t, 0, b, 0);
        a[sizeof a - 1] = b[sizeof b - 1] = 0;
        char *x = a; while (*x == ' ') x++;
        /* the unit only when the text doesn't carry one already ("0 db" + "dB" read "0 db dB") */
        int has_letters = 0;
        for (const char *q = x; *q; q++) has_letters |= (*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == '%';
        /* an empty text (a trigger, a hidden value) answers " ": a 0-length answer means "no such key" to our wrapper,
         * which then shows the proxy's number ("0.0") instead */
        if (!*x && !b[0]) return snprintf(buf, len, " ");
        return snprintf(buf, len, b[0] && !has_letters ? "%s %s" : "%s", x, b);
    }
    s->fx->dispatcher(s->fx, effGetParamName, t, 0, a, 0);
    a[sizeof a - 1] = 0;
    return snprintf(buf, len, "%s", a[0] ? a : "-");
}

/* a hosted plugin's id in the saved state: "<vendor>/<name>", with | : , / % # escaped as %XX */
static int type_id(int t, char *out, int len) {
    char raw[160];
    snprintf(raw, sizeof raw, "%s/%s", g_types[t].vendor, g_types[t].name);
    int o = 0, slash = (int)strlen(g_types[t].vendor);
    for (int i = 0; raw[i] && o < len - 4; i++) {
        unsigned char ch = (unsigned char)raw[i];
        if (i != slash && strchr("|:,/%#", ch)) o += snprintf(out + o, len - o, "%%%02X", ch);
        else out[o++] = (char)ch;
    }
    out[o] = 0;
    return o;
}

/* a hosted chunk in our state, as itself except | (our separator), % (the escape) and bytes outside 0x20..0x7e, as %XX.
 * (1.2.1 also escaped : # , and spaces, which made a JSON state like Boris Granular's half as big again; both read back.) */
static int chunk_enc(const unsigned char *d, intptr_t n, char *out, int len) {
    int o = 0;
    for (intptr_t i = 0; i < n; i++) {
        if (o > len - 4) return -1;
        unsigned char ch = d[i];
        if (ch >= 0x20 && ch < 0x7f && ch != '|' && ch != '%') out[o++] = (char)ch;
        else o += snprintf(out + o, len - o, "%%%02X", ch);
    }
    out[o] = 0;
    return o;
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

static int save_state(pp_t *p, char *buf, int len) {
    /* PP3|<focus>|<page>|<focus section>|<section shown>|<parallel page>|<send>|<item>...[|~|<item>...]  (1.3.0)
     * one item per slot, the Inserts in chain order, then "~" and the Parallel section's (PP2: the first two numbers and
     * the Inserts only):
     *   <id>#<chunk>   the hosted plugin's own saved state (effGetChunk, %XX-escaped), what MPC itself would save for it:
     *                  it keeps what isn't a parameter (Liminal Hz's IR) and needs no parameter order (Shapey EQ's band pick)
     *   <id>:<v0>,...  its normalized parameter values, for a plugin with no chunk (and PP1 states)
     * Our wrapper hands MPC at most its chunk buffer (8 KB): a slot that no longer fits is left out whole.
     * PP4 (1.4.0): the same, and after each item "|=<on>,<send>,<sc on>,<thr>,<att>,<rel>,<knee>,<ratio>" (its On/Off, its
     * Send and its sidechain); the header's Send is unused (1.3's single Send: a PP3 state gives it to every Parallel track). */
    int o = snprintf(buf, len, "PP4|%d|%d|%d|%d|%d|%.2f", p->focus, p->page[0], p->fsec, p->sec, p->page[1], 50.0);
    char *item = malloc(8192);
    if (!item) return o;
    int total = p->n[0] + p->n[1];
    for (int g = 0; g < total; g++) {
        int sec = g >= p->n[0], i = sec ? g - p->n[0] : g;
        slot_t *s = p->s[sec][i];
        if (sec && !i) {   /* the Parallel section starts */
            if (o + 2 >= len) break;
            memcpy(buf + o, "|~", 3);
            o += 2;
        }
        char id[200];
        type_id(s->type, id, sizeof id);
        int k = snprintf(item, 8192, "|%s", id), done = 0;
        void *ch = NULL;
        intptr_t n = (s->fx->flags & effFlagsProgramChunks) ? s->fx->dispatcher(s->fx, effGetChunk, 0, 0, &ch, 0) : 0;
        if (n > 0 && ch && k + 2 < 8192) {
            item[k] = '#';
            done = chunk_enc(ch, n, item + k + 1, 8192 - k - 1) >= 0;
        }
        if (!done) {
            k += snprintf(item + k, 8192 - k, ":");
            for (int t = 0; t < s->nparams && k < 8192 - 16; t++)
                k += snprintf(item + k, 8192 - k, t ? ",%.5g" : "%.5g", s->fx->getParameter(s->fx, t));
        }
        k = (int)strlen(item);
        if (k < 8192 - 96)
            snprintf(item + k, 8192 - k, "|=%d,%.2f,%d,%.2f,%.3g,%.4g,%.2f,%.3g", s->on, s->send, s->sc_on, s->thr, s->att, s->rel,
                     s->knee, s->ratio);
        int il = (int)strlen(item);
        if (o + il >= len) {   /* MPC keeps at most this much of a plugin's state: say so where it shows */
            snprintf(p->status, sizeof p->status, "Too big to save whole: effects %d-%d left out", g + 1, total);
            mark(p, PI_STATUS);
            break;
        }
        memcpy(buf + o, item, il + 1);
        o += il;
    }
    /* 1.5.0: the macros, as one more item that older PoliPlugs skip (no ':' or '#' in it, so their item loop stops there):
     * "|M=<page>,<sec>.<index>.<param> or - ..." (16 macros) */
    {
        int k = snprintf(item, 8192, "|M=%d", p->mk_page);
        for (int m = 0; m < PP_NMACRO && k < 8192 - 32; m++) {
            int sec = -1, i = -1;
            if (mk_ok(p, m)) { sec = 0; i = slot_index(p, 0, p->mk[m].s); if (i < 0) { sec = 1; i = slot_index(p, 1, p->mk[m].s); } }
            k += i >= 0 ? snprintf(item + k, 8192 - k, ",%d.%d.%d", sec, i, p->mk[m].t) : snprintf(item + k, 8192 - k, ",-");
        }
        if (o + k < len) { memcpy(buf + o, item, k + 1); o += k; }
    }
    free(item);
    return o;
}

static void restore_state(pp_t *p, const char *val) {
    if (strncmp(val, "PP1|", 4) && strncmp(val, "PP2|", 4) && strncmp(val, "PP3|", 4) && strncmp(val, "PP4|", 4)) return;
    int v3 = val[2] == '3' || val[2] == '4', v4 = val[2] == '4';
    clear_all(p);
    const char *c = val + 4;
    double hd[6] = {-1, 0, 0, 0, 0, 50};   /* focus, page, focus section, section shown, parallel page, send */
    int nh = v3 ? 6 : 2;
    for (int k = 0; k < nh && c; k++) {
        char *end;
        double d = strtod(c, &end);
        if (end != c && d == d && d > -1e6 && d < 1e6) hd[k] = d;
        c = strchr(c, '|');
        if (c && k < nh - 1) c++;
    }
    int focus = (int)hd[0], page = (int)hd[1], fsec = clampi((int)hd[2], 0, NSEC - 1), shown = clampi((int)hd[3], 0, NSEC - 1);
    int page1 = (int)hd[4];
    float old_send = hd[5] < 0 ? 0.0f : hd[5] > 100 ? 100.0f : (float)hd[5];   /* PP3: 1.3's single Send */
    int sec = 0;
    char missing[48] = "";
    slot_t *last = NULL;
    while (c && *c == '|') {
        c++;
        if (c[0] == '~' && (c[1] == '|' || !c[1])) { sec = 1; c++; last = NULL; continue; }   /* the Parallel section's items follow */
        if (c[0] == 'M' && c[1] == '=') {   /* 1.5.0: the macros (after every effect) */
            const char *q = c + 2;
            char *end;
            long pg = strtol(q, &end, 10);
            if (end != q) p->mk_page = clampi((int)pg, 0, 3);
            q = end;
            for (int m = 0; m < PP_NMACRO && *q == ','; m++) {
                q++;
                if (*q == '-') { q++; continue; }
                long a = strtol(q, &end, 10), b = -1, t = -1;
                if (end != q && *end == '.') { q = end + 1; b = strtol(q, &end, 10); }
                if (end != q && *end == '.') { q = end + 1; t = strtol(q, &end, 10); }
                q = end;
                if (a >= 0 && a < NSEC && b >= 0 && b < p->n[a] && t >= 0 && t < p->s[a][b]->nparams) {
                    p->mk[m].s = p->s[a][b];
                    p->mk[m].t = (int)t;
                    p->mk[m].last = p->s[a][b]->fx->getParameter(p->s[a][b]->fx, (int)t);
                }
                while (*q && *q != ',' && *q != '|') q++;
            }
            c = strchr(c, '|');
            last = NULL;
            continue;
        }
        if (c[0] == '=') {   /* PP4: the previous item's On/Off, Send, sidechain */
            float f[8] = {1, 50, 0, -24, 5, 250, 6, 4};
            const char *q = c + 1;
            for (int k = 0; k < 8 && *q && *q != '|'; k++) {
                char *end;
                double d = strtod(q, &end);
                if (end == q) break;
                if (d == d && d > -1e6 && d < 1e6) f[k] = (float)d;
                q = *end == ',' ? end + 1 : end;
            }
            if (last) {
                last->on = f[0] >= 0.5f;
                last->mix = last->on ? 1.0f : 0.0f;
                last->send = f[1] < 0 ? 0 : f[1] > 100 ? 100 : f[1];
                last->send_g = last->send * 0.01f;
                last->sc_on = f[2] >= 0.5f;
                last->thr = f[3] < -60 ? -60 : f[3] > 0 ? 0 : f[3];
                last->att = f[4] < PP_SC_ATT_LO ? PP_SC_ATT_LO : f[4] > PP_SC_ATT_HI ? PP_SC_ATT_HI : f[4];
                last->rel = f[5] < PP_SC_REL_LO ? PP_SC_REL_LO : f[5] > PP_SC_REL_HI ? PP_SC_REL_HI : f[5];
                last->knee = f[6] < 0 ? 0 : f[6] > 24 ? 24 : f[6];
                last->ratio = f[7] < PP_SC_RATIO_LO ? PP_SC_RATIO_LO : f[7] > PP_SC_RATIO_HI ? PP_SC_RATIO_HI : f[7];
            }
            last = NULL;
            c = strchr(c, '|');
            continue;
        }
        const char *next = strchr(c, '|'), *sep = c + strcspn(c, ":#|");
        if (*sep != ':' && *sep != '#') break;
        int type = -1;
        for (int t = 0; t < g_ntypes; t++) {
            char id[200];
            if ((size_t)(sep - c) == (size_t)type_id(t, id, sizeof id) && !strncmp(c, id, sep - c)) type = t;
        }
        int i = type >= 0 ? add_slot(p, sec, type) : -1;
        last = i >= 0 ? p->s[sec][i] : NULL;
        if (last && sec == 1 && !v4) { last->send = old_send; last->send_g = old_send * 0.01f; }
        if (type < 0 && !missing[0]) snprintf(missing, sizeof missing, "%.*s", (int)(sep - c) > 40 ? 40 : (int)(sep - c), c);
        if (*sep == '#') {
            size_t el = next ? (size_t)(next - sep - 1) : strlen(sep + 1), n = 0;
            unsigned char *d = malloc(el + 1);
            for (size_t k = 0; d && k < el; k++) {
                const char *q = sep + 1 + k;
                if (*q == '%' && k + 2 < el && hexv(q[1]) >= 0 && hexv(q[2]) >= 0) { d[n++] = (unsigned char)(hexv(q[1]) * 16 + hexv(q[2])); k += 2; }
                else d[n++] = (unsigned char)*q;
            }
            if (d && i >= 0 && n > 0) p->s[sec][i]->fx->dispatcher(p->s[sec][i]->fx, effSetChunk, 0, (intptr_t)n, d, 0);
            free(d);
        } else {
            const char *q = sep + 1;
            for (int t = 0; q && *q && *q != '|'; t++) {
                char *end;
                float v = strtof(q, &end);
                if (end == q) break;
                if (i >= 0 && t < p->s[sec][i]->nparams) p->s[sec][i]->fx->setParameter(p->s[sec][i]->fx, t, v);
                q = *end == ',' ? end + 1 : end;
            }
        }
        c = next;
    }
    p->page[0] = clampi(page, 0, npages_of(p, 0) - 1);
    p->page[1] = clampi(page1, 0, npages_of(p, 1) - 1);
    p->sec = shown;
    p->view = 0;
    refocus(p, fsec, focus);
    mark_rack(p);
    set_status(p);
    if (missing[0]) snprintf(p->status, sizeof p->status, "Skipped (not installed): %.20s", missing);
}

static int pp_get_param(void *inst, const char *key, char *buf, int len) {
    pp_t *p = inst;
    int r, n;
    const char *what;
    if (parse_row(key, &r, &what)) {
        int sec = p->sec, i = p->page[sec] * PP_ROWS + r, used = i < p->n[sec] && p->view == 0 && !p->picking;
        /* an empty row answers " ": a 0-length answer means "no such key" to the wrapper, and the old text would stay */
        if (!*what) return used ? snprintf(buf, len, "%d  %s", i + 1, g_types[p->s[sec][i]->type].name) : snprintf(buf, len, " ");
        if (!strcmp(what, "used")) return snprintf(buf, len, "%d", used);
        if (!strcmp(what, "up")) return snprintf(buf, len, "%d", !used ? 0 : i == 0 ? 1 : 2);
        if (!strcmp(what, "down")) return snprintf(buf, len, "%d", !used ? 0 : i == p->n[sec] - 1 ? 1 : 2);
        if (!strcmp(what, "open") || !strcmp(what, "mvup") || !strcmp(what, "mvdn") || !strcmp(what, "bye") ||
            !strcmp(what, "tog") || !strcmp(what, "sc"))
            return snprintf(buf, len, "0");
        /* 1.4.0 */
        slot_t *rs = used ? p->s[sec][i] : NULL;
        int par = rs && sec == 1;
        if (!strcmp(what, "onst")) return snprintf(buf, len, "%d", !rs ? 0 : rs->on ? 2 : 1);
        if (!strcmp(what, "par")) return snprintf(buf, len, "%d", par);
        if (!strcmp(what, "scst")) return snprintf(buf, len, "%d", !par ? 0 : rs->sc_on ? 2 : 1);
        if (!strcmp(what, "send")) return snprintf(buf, len, "%.4f", par ? rs->send : 50.0f);
        if (!strcmp(what, "send_display")) return par ? snprintf(buf, len, "%.0f%%", rs->send) : snprintf(buf, len, " ");
        return 0;   /* "<key>_on" and the like: not answered */
    }
    if (parse_num(key, 'q', &n, &what) && n >= 1 && n <= PP_NQ) {
        if (p->sc) {   /* 1.4.0: the sidechain panel's knobs */
            int k = sc_k(n - 1);
            if (!*what) return snprintf(buf, len, "%.6f", k >= 0 ? sc_norm(p->sc, k) : 0.0f);
            if (!strcmp(what, "_name")) return snprintf(buf, len, "%s", k >= 0 ? SC_QNAME[k] : "-");
            if (!strcmp(what, "_display")) return k >= 0 ? sc_text(p->sc, k, buf, len) : snprintf(buf, len, " ");
            return 0;
        }
        slot_t *qs = q_send(p, n - 1);
        if (qs) {   /* 1.4.1: Parallel on the rack: the tracks' Sends */
            if (!*what) return snprintf(buf, len, "%.6f", qs->send * 0.01f);
            if (!strcmp(what, "_name")) return snprintf(buf, len, "P%d Send", q_slot(n - 1) + 1);
            if (!strcmp(what, "_display")) return snprintf(buf, len, "%.0f%%", qs->send);
            return 0;
        }
        int m = q_mk(p, n - 1);
        if (m >= 0) {   /* 1.5.0: Inserts on the rack: the macros */
            int ok = mk_ok(p, m);
            if (!*what) return snprintf(buf, len, "%.6f", ok ? p->mk[m].s->fx->getParameter(p->mk[m].s->fx, p->mk[m].t) : 0.0f);
            if (!strcmp(what, "_name")) return ok ? slot_text(p->mk[m].s, p->mk[m].t, 0, buf, len) : snprintf(buf, len, "-");
            if (!strcmp(what, "_display")) return ok ? slot_text(p->mk[m].s, p->mk[m].t, 1, buf, len) : snprintf(buf, len, " ");
            return 0;
        }
        int t = q_target(p, n - 1);
        if (!*what) return snprintf(buf, len, "%.6f", hosted_get(p, t));
        if (!strcmp(what, "_name")) return t < 0 ? snprintf(buf, len, "-") : hosted_text(p, t, 0, buf, len);
        if (!strcmp(what, "_display")) return hosted_text(p, t, 1, buf, len);
        return 0;
    }
    if (parse_num(key, 'e', &n, &what) && n >= 0 && n < PP_MAX_EDIT) {
        if (!*what) return snprintf(buf, len, "%.6f", hosted_get(p, n));
        if (!strcmp(what, "_name")) return hosted_text(p, n, 0, buf, len);
        if (!strcmp(what, "_display")) return hosted_text(p, n, 1, buf, len);
        return 0;
    }
    if (!strcmp(key, "state")) return save_state(p, buf, len);
    if (!strcmp(key, "page")) return snprintf(buf, len, "%d", p->page[p->sec]);
    if (!strcmp(key, "page_text"))
        return p->picking ? snprintf(buf, len, "%d / %d", p->pick_page + 1, pick_pages())
                          : snprintf(buf, len, "%d / %d", p->page[p->sec] + 1, npages(p));
    /* 1.3.0: sections, Send, the opened effect's presets */
    if (!strcmp(key, "section")) return snprintf(buf, len, "%d", p->sec);
    if (!strcmp(key, "send")) return snprintf(buf, len, "50.0000");   /* legacy (1.3's single Send) */
    if (!strcmp(key, "send_display")) return snprintf(buf, len, "50 %%");
    /* 1.4.0: the sidechain panel */
    if (!strcmp(key, "scing")) return snprintf(buf, len, "%d", p->sc != NULL);
    if (!strcmp(key, "sc_onst")) return snprintf(buf, len, "%d", !p->sc ? 0 : p->sc->sc_on ? 2 : 1);
    if (!strcmp(key, "sc_name")) {
        int i = p->sc ? slot_index(p, 1, p->sc) : -1;
        return i >= 0 ? snprintf(buf, len, "P%d  %s", i + 1, g_types[p->sc->type].name) : snprintf(buf, len, " ");
    }
    if (!strcmp(key, "sc_gr")) return snprintf(buf, len, "%.4f", p->sc ? clampi(p->sc_frame, 0, PP_SC_METER_FRAMES - 1) /
                                                         (float)(PP_SC_METER_FRAMES - 1) : 0.0f);
    if (!strcmp(key, "sc_close") || !strcmp(key, "sc_panel") || !strcmp(key, "sc_tog")) return snprintf(buf, len, "0");
    if (!strncmp(key, "sc_", 3)) {
        static const char *const ks[5] = {"sc_thr", "sc_att", "sc_rel", "sc_knee", "sc_ratio"};
        slot_t dflt;   /* nothing open: the defaults */
        memset(&dflt, 0, sizeof dflt);
        dflt.thr = -24; dflt.att = 5; dflt.rel = 250; dflt.knee = 6; dflt.ratio = 4;
        const slot_t *t = p->sc ? p->sc : &dflt;
        for (int k = 0; k < 5; k++) {
            size_t kl = strlen(ks[k]);
            if (strncmp(key, ks[k], kl)) continue;
            if (!key[kl]) {
                float v = k == 0 ? t->thr : k == 3 ? t->knee : 100.0f * sc_norm(t, k);
                return snprintf(buf, len, "%.4f", v);
            }
            if (!strcmp(key + kl, "_display")) return sc_text(t, k, buf, len);
        }
        return 0;
    }
    /* 1.5.0: the macro column */
    if (!strcmp(key, "mk_shown")) return snprintf(buf, len, "%d", mk_shown(p));
    if (!strcmp(key, "mk_page")) return snprintf(buf, len, "%d", mk_shown(p) ? p->mk_page + 1 : 0);
    if (!strcmp(key, "mk_mapst")) return snprintf(buf, len, "%d", !mk_shown(p) ? 0 : p->mapping ? 2 : 1);
    if (!strcmp(key, "mk_clrst")) return snprintf(buf, len, "%d", mk_shown(p) && p->mapping && p->armed >= 0);
    if (!strcmp(key, "mk_map") || !strcmp(key, "mk_clear") || (!strncmp(key, "mk_pg", 5) && key[5] >= '1' && key[5] <= '4'))
        return snprintf(buf, len, "0");
    if (!strncmp(key, "mk", 2) && key[2] >= '1' && key[2] <= '4') {
        int m = p->mk_page * 4 + key[2] - '1', ok = mk_ok(p, m), sh = mk_shown(p);
        const char *w = key + 3;
        slot_t *ms = ok ? p->mk[m].s : NULL;
        if (!*w) return snprintf(buf, len, "%.6f", ok ? ms->fx->getParameter(ms->fx, p->mk[m].t) : 0.0f);
        if (!strcmp(w, "_display")) return ok ? slot_text(ms, p->mk[m].t, 1, buf, len) : snprintf(buf, len, "--");
        if (!strcmp(w, "_st")) return snprintf(buf, len, "%d", !sh ? 0 : p->mapping && p->armed == m ? 3 : ok ? 2 : 1);
        /* the readouts answer " " while hidden (a readout can't be hidden by when=) */
        if (!strcmp(w, "_name")) return !sh ? snprintf(buf, len, " ") : ok ? slot_text(ms, p->mk[m].t, 0, buf, len)
                                                                           : snprintf(buf, len, "Empty");
        if (!strcmp(w, "_fx")) {
            if (!sh || !ok) return snprintf(buf, len, sh ? "Map to assign" : " ");
            int sec = slot_index(p, 0, ms) >= 0 ? 0 : 1, i = slot_index(p, sec, ms);
            return snprintf(buf, len, "%s%d %s", sec ? "P" : "", i + 1, g_types[ms->type].name);
        }
        return 0;
    }
    if (!strcmp(key, "hpreset")) return snprintf(buf, len, "%d", hcur(p));
    if (!strcmp(key, "hpreset_count")) return snprintf(buf, len, "%d", hcount(p) > 0 ? hcount(p) : 1);
    if (!strncmp(key, "hpreset:", 8)) {   /* program n's name for MPC's PRESET menu */
        int n = atoi(key + 8);
        if (!hcount(p)) return snprintf(buf, len, "%s", hslot(p) ? "(no presets)" : "(open an effect)");
        return hname(p, n, buf, len);
    }
    if (!strcmp(key, "hpre_name")) { if (!hpre_visible(p)) return snprintf(buf, len, " "); return hname(p, hcur(p), buf, len); }
    if (!strcmp(key, "hpre_shown")) return snprintf(buf, len, "%d", hpre_visible(p));
    if (!strcmp(key, "hbrowsing")) return snprintf(buf, len, "%d", p->hbrowse && hpre_visible(p));
    if (!strcmp(key, "hpre_page")) return snprintf(buf, len, "%d / %d", p->hpage + 1, hpages(p));
    if (!strncmp(key, "hpick", 5)) return snprintf(buf, len, "0");
    if (!strncmp(key, "hpre", 4) && key[4] >= '1' && key[4] <= '9') {
        char *end;
        long n = strtol(key + 4, &end, 10);
        int idx = p->hpage * PP_NHPRE + (int)n - 1, shown = p->hbrowse && hpre_visible(p) && n >= 1 && n <= PP_NHPRE && idx < hcount(p);
        if (!strcmp(end, "_name")) return shown ? hname(p, idx, buf, len) : snprintf(buf, len, " ");
        if (!strcmp(end, "_state")) return snprintf(buf, len, "%d", !shown ? 0 : idx == hcur(p) ? 2 : 1);
        return 0;
    }
    if (!strcmp(key, "hpre_prev") || !strcmp(key, "hpre_next") || !strcmp(key, "hpre_list") || !strcmp(key, "hpre_pprev") ||
        !strcmp(key, "hpre_pnext") || !strcmp(key, "sec_ins") || !strcmp(key, "sec_par")) return snprintf(buf, len, "0");
    if (!strcmp(key, "picking")) return snprintf(buf, len, "%d", p->picking && p->view == 0);
    if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '0' + PP_NPICK) {
        int t = p->pick_page * PP_NPICK + (key[4] - '1'), shown = p->picking && p->view == 0 && t < g_ntypes;
        if (!key[5]) return snprintf(buf, len, "0");
        if (!strcmp(key + 5, "_name")) return shown ? snprintf(buf, len, "%s", g_types[t].name) : snprintf(buf, len, " ");
        if (!strcmp(key + 5, "_shown")) return snprintf(buf, len, "%d", shown);
        return 0;
    }
    if (!strcmp(key, "add_open")) return snprintf(buf, len, "0");
    if (!strcmp(key, "status")) return snprintf(buf, len, "%s", p->status);
    if (!strcmp(key, "view")) {   /* the page shown: a sub-tab drawn like an earlier one shows that one (discover.c alias) */
        int t = p->view > 0 ? view_type(p->view) : -1, k = t >= 0 ? p->view - g_types[t].view0 : 0;
        return snprintf(buf, len, "%d", t >= 0 && k < PP_MAX_TABS ? g_types[t].view0 + g_types[t].alias[k] : p->view);
    }
    if (!strcmp(key, "editing")) return snprintf(buf, len, "%d", p->view > 0);
    if (!strcmp(key, "pages")) return snprintf(buf, len, "%d", p->view > 0 && focused(p) && g_types[focused(p)->type].nviews > 1);
    if (!strcmp(key, "view_back") || !strcmp(key, "view_prev") || !strcmp(key, "view_next") ||
        !strcmp(key, "page_prev") || !strcmp(key, "page_next")) return snprintf(buf, len, "0");
    return 0;
}

/* engine.h: the parameters we changed ourselves, one key per call (the wrapper asks once per host block) */
int mpc_engine_changed(void *inst, char *key, int len) {
    pp_t *p = inst;
    for (int i = 0; i < PP_NKEYS; i++)
        if (p->dirty[i]) {
            p->dirty[i] = 0;
            snprintf(key, len, "%s", PP_KEYS[i]);
            return 1;
        }
    return 0;
}

static const mpc_engine_t ENGINE = {pp_create, pp_destroy, pp_midi, pp_set_param, pp_get_param, pp_render, pp_process,
                                    pp_song_pos};

const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
