// Poli-verb: the mpc_engine() contract (wrapper/engine.h) around VerbDSP.
//
// Screen: three pages (General, Space, Tail) chosen by the tab pictures at the top; the knobs of every page are real
// parameters (stable for automation), shown with when=page:N. What follows the page:
//   * the 9 Q-Link slots q1..q9: they turn the knobs of the page on screen (a page's knobs in reading order, 4 to a Q-Link
//     page), and the engine reports their new names/values when the page changes;
//   * three picture views that only exist on one page: type_view (the space's drawing, Space page), room_view (the room
//     size letter, Tail page). The toolchain's `picture` can't carry when=, so the engine says "hidden" itself.
// Tempo: HAS_SONG_POS gives the host tempo (song_pos), used when Sync is on: Pre-Delay becomes a note value and the
// early reflections span exactly one straight note value.
// Presets: 101 factory presets (src/presets.h, from presets/factory.py) in 10 categories + All; the browser lives in
// verb_nav.h. A preset sets every parameter (Freeze off), so Init = the defaults.
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <new>

extern "C" {
#include "engine.h"
}
#include "VerbDSP.h"
#include "presets.h"
#include "verb_nav.h"

using namespace verb;

static const int kBlock = 128;

static const char *const kKnobKey[P_COUNT] = {"mix", "input", "output", "drive", "low", "tilt", "high", "size", "predelay",
                                              "width", "er_level", "er_prob", "diffusion", "damping", "modulation",
                                              "density", "spread", "bassmult", "attack", "release", "decay"};
static const char *const kKnobName[P_COUNT] = {"Mix", "Input", "Output", "Drive", "Low", "Tilt", "High", "Size", "Pre-Delay",
                                               "Width", "Lvl", "Probability", "Diffusion", "Damping", "Modulation",
                                               "Density", "Spread", "Bass Mult", "Attack", "Release", "Decay"};

// the 9 Q-Link slots per page: which knob each one turns (-1 = nothing). 4 slots = one Q-Link page of the Live III.
// K_ROOM is not a knob: the Tail page's "Room size" (S..XXL), a step control on screen and a slot on the Q-Links.
static const int K_ROOM = 99;
static const int kQSlots = 10;
static const int kQMap[3][kQSlots] = {
    {P_MIX, P_INPUT, P_OUTPUT, P_DRIVE, P_LOW, P_TILT, P_HIGH, -1, -1, -1},
    {P_SIZE, P_PREDELAY, P_WIDTH, P_ERLVL, P_ERPROB, -1, -1, -1, -1, -1},
    {P_DIFFUSION, P_DAMPING, P_MODULATION, P_DENSITY, P_SPREAD, P_BASSMULT, P_ATTACK, P_RELEASE, P_DECAY, K_ROOM},
};

// keys the engine reports back when it moves them itself (mpc_engine_changed)
enum Extra { X_FREEZE = P_COUNT, X_SYNC, X_WETL, X_WETR, X_TYPE, X_PAGE, X_TYPEVIEW, X_ROOMVIEW, X_Q1, X_COUNT = X_Q1 + kQSlots };
static const char *extraKey(int k) {
    static const char *const names[X_COUNT - P_COUNT] = {"freeze", "sync", "wet_l", "wet_r", "type", "page", "type_view", "room_view",
                                                         "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10"};
    return k < P_COUNT ? kKnobKey[k] : names[k - P_COUNT];
}

static bool isFiniteBits(float x) {   // -ffast-math drops isnan/isfinite
    uint32_t u;
    memcpy(&u, &x, sizeof u);
    return (u & 0x7f800000u) != 0x7f800000u;
}
static bool parseNum(const char *s, float &out) {
    char *end = nullptr;
    const float v = strtof(s, &end);
    if (end == s || !isFiniteBits(v)) return false;
    out = v;
    return true;
}
static int parseInt(const char *s, int lo, int hi, int fallback) {
    float v;
    if (!parseNum(s, v)) return fallback;
    return (int)lrintf(clampf(v, (float)lo, (float)hi));
}

// ---- value text ---------------------------------------------------------------------------------------------------
static void trim(char *b) {   // "12.0" -> "12", "-0" -> "0"
    char *dot = strchr(b, '.');
    if (dot && dot[1] == '0' && dot[2] == 0) *dot = 0;
    if (!strcmp(b, "-0") || !strcmp(b, "+0")) strcpy(b, "0");
}
static int fmtDb(char *buf, int len, float db, bool sign) {
    char n[24];
    snprintf(n, sizeof n, sign && db > 0.049f ? "+%.1f" : "%.1f", (double)db);
    trim(n);
    return snprintf(buf, len, "%s dB", n);
}
static int fmtPct(char *buf, int len, float pct) { return snprintf(buf, len, "%d %%", (int)lrintf(pct)); }
static int fmtTime(char *buf, int len, float ms) {   // 0.0..: "0 ms", "7.2 ms", "125 ms", "1.5 s", "12 s"
    char n[24];
    if (ms >= 9950.0f) return snprintf(buf, len, "%d s", (int)lrintf(ms / 1000.0f));
    if (ms >= 999.5f) { snprintf(n, sizeof n, "%.1f", (double)(ms / 1000.0f)); trim(n); return snprintf(buf, len, "%s s", n); }
    if (ms < 9.95f) { snprintf(n, sizeof n, "%.1f", (double)ms); trim(n); return snprintf(buf, len, "%s ms", n); }
    return snprintf(buf, len, "%d ms", (int)lrintf(ms));
}

struct PoliVerb {
    VerbDSP dsp;
    float pos[P_COUNT];
    int type = 0;
    bool freeze = false, sync = false, wetL = true, wetR = true;
    int page = 0;
    int lastRoom = -1;
    VerbNav nav;

    uint64_t pending = 0;
    unsigned displayRev = 1;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    PoliVerb() {
        dsp.init(44100.0f);
        for (int i = 0; i < P_COUNT; ++i) { pos[i] = kDefaultPos[i]; dsp.setParam(i, pos[i]); }
        dsp.setType(0);
        lastRoom = dsp.roomClass();
    }

    void changed(int k) { pending |= 1ull << k; }
    void changedViews() { changed(X_TYPEVIEW); changed(X_ROOMVIEW); }
    void changedQs() { for (int q = 0; q < kQSlots; ++q) changed(X_Q1 + q); }
    void changedKnobSlots(int knob) {   // the Q slots of the page on screen that turn this knob
        for (int q = 0; q < kQSlots; ++q) if (kQMap[page][q] == knob) changed(X_Q1 + q);
    }
    void checkRoom() {
        const int rc = dsp.roomClass();
        if (rc != lastRoom) { lastRoom = rc; changed(X_ROOMVIEW); changed(X_TYPEVIEW); changedKnobSlots(K_ROOM); }
    }
    // Room size S..XXL: set the Size knob so the space lands in that class (false: this space can't reach it)
    bool setRoomClass(int c) {
        static const float lo[5] = {0.0f, 0.6f, 1.2f, 2.4f, 4.5f}, hi[5] = {0.6f, 1.2f, 2.4f, 4.5f, 99.0f};
        static const float mid[5] = {0.40f, 0.85f, 1.7f, 3.2f, 5.5f};
        c = std::max(0, std::min(4, c));
        const float m = kSpaces[type].sizeMul;
        const float emin = std::max(lo[c], 0.25f * m), emax = std::min(hi[c] - 0.02f, 3.25f * m);
        if (emin > emax) return false;
        const float e = std::max(emin, std::min(emax, mid[c]));
        const float sk = std::max(0.25f, std::min(3.25f, e / m));
        const float s01 = powf((sk - 0.25f) / 3.0f, 1.0f / 1.6f);
        setKnob(P_SIZE, 1000.0f * s01);
        changed(P_SIZE);
        return true;
    }
    void stepRoom() {
        const int cur = dsp.roomClass();
        for (int k = 1; k <= 5; ++k) if (setRoomClass((cur + k) % 5)) return;
    }

    void setKnob(int k, float v) {
        pos[k] = clampf(v, 0.0f, 1000.0f);
        dsp.setParam(k, pos[k]);
        changedKnobSlots(k);
        if (k == P_SIZE) checkRoom();
    }
    static const char *slotName(int kk) { return kk == K_ROOM ? "Room size" : kk >= 0 ? kKnobName[kk] : "-"; }
    float slotPos(int kk) const { return kk == K_ROOM ? 250.0f * dsp.roomClass() : kk >= 0 ? pos[kk] : 0.0f; }
    void setType(int t) {
        t = std::max(0, std::min(kNumTypes - 1, t));
        if (t == type) return;
        type = t;
        dsp.setType(t);
        changed(X_TYPE); changed(X_TYPEVIEW);
        ++displayRev;
        checkRoom();
    }
    void setPage(int p) {
        p = std::max(0, std::min(2, p));
        nav.close();
        if (p == page) return;
        page = p;
        changed(X_PAGE);
        changedViews();
        changedQs();
        ++displayRev;   // MPC re-reads the Q-Link names and the page's value texts
    }
    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const VerbPreset &p = kPresets[idx];
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = (float)p.pos[k]; dsp.setParam(k, pos[k]); changed(k); }
        type = std::max(0, std::min(kNumTypes - 1, (int)p.type));
        dsp.setType(type);
        freeze = false; sync = p.sync != 0; wetL = wetR = true;
        dsp.setFreeze(false); dsp.setSync(sync); dsp.setWetEnable(true, true);
        changed(X_FREEZE); changed(X_SYNC); changed(X_WETL); changed(X_WETR); changed(X_TYPE);
        changedViews(); changedQs();
        ++displayRev;
        lastRoom = dsp.roomClass();
    }

    // value text of a knob
    int display(int k, char *buf, int len) const {
        if (k == K_ROOM) return snprintf(buf, len, "%s", roomClassName(dsp.roomClass()));
        if (k < 0 || k >= P_COUNT) return 0;
        const float p = pos[k];
        switch (k) {
            case P_MIX: case P_DRIVE: case P_SIZE: case P_ERLVL: case P_ERPROB: case P_DIFFUSION: case P_DAMPING:
            case P_MODULATION: case P_DENSITY: case P_SPREAD:
                return fmtPct(buf, len, VerbMap::pct(p));
            case P_WIDTH: return fmtPct(buf, len, VerbMap::widthPct(p));
            case P_INPUT: case P_OUTPUT: return fmtDb(buf, len, VerbMap::db24(p), true);
            case P_LOW: case P_HIGH: return fmtDb(buf, len, VerbMap::db12(p), true);
            case P_TILT: return fmtDb(buf, len, VerbMap::db6(p), true);
            case P_PREDELAY:
                if (sync) return snprintf(buf, len, "%s", kDivName[VerbMap::divIndex(p)]);
                return fmtTime(buf, len, VerbMap::predelayMs(p));
            case P_BASSMULT: { char n[24]; snprintf(n, sizeof n, "%.2f", (double)VerbMap::bassMult(p));
                               char *e = n + strlen(n); while (e > n + 1 && e[-1] == '0' && strchr(n, '.')) *--e = 0; if (e[-1] == '.') *--e = 0;
                               return snprintf(buf, len, "%sx", n); }
            case P_ATTACK: return fmtTime(buf, len, VerbMap::attackMs(p));
            case P_RELEASE: return VerbMap::releaseInf(p) ? snprintf(buf, len, "inf") : fmtTime(buf, len, VerbMap::releaseS(p) * 1000.0f);
            case P_DECAY: return fmtTime(buf, len, VerbMap::decayS(p) * 1000.0f);
        }
        return 0;
    }

    int knobOfKey(const char *key) const {
        for (int k = 0; k < P_COUNT; ++k) if (!strcmp(key, kKnobKey[k])) return k;
        return -1;
    }
    // "q3" -> 2, else -1
    static int qOfKey(const char *key) {
        if (key[0] != 'q') return -1;
        char *end = nullptr;
        const long n = strtol(key + 1, &end, 10);
        if (end == key + 1 || *end || n < 1 || n > kQSlots) return -1;
        return (int)n - 1;
    }
    static bool press(const char *v) { float f; return parseNum(v, f) && f >= 0.5f; }

    void set(const char *key, const char *val) {
        if (!strcmp(key, "state")) { load(val); return; }
        const int pick = nav.set(key, val);
        if (pick >= 0) { applyPreset(pick); return; }
        if (pick == VerbNav::HANDLED) return;
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        const int k = knobOfKey(key);
        if (k >= 0) { setKnob(k, v); return; }
        const int q = qOfKey(key);
        if (q >= 0) {
            const int kk = kQMap[page][q];
            if (kk == K_ROOM) { setRoomClass((int)lrintf(clampf(v, 0.0f, 1000.0f) / 250.0f)); changed(X_Q1 + q); }
            else if (kk >= 0) { setKnob(kk, v); changed(kk); changed(X_Q1 + q); }
            return;
        }
        if (!strcmp(key, "freeze")) { freeze = v >= 0.5f; dsp.setFreeze(freeze); return; }
        if (!strcmp(key, "sync")) {
            sync = v >= 0.5f;
            dsp.setSync(sync);
            changed(P_PREDELAY); changedKnobSlots(P_PREDELAY);
            ++displayRev;
            return;
        }
        if (!strcmp(key, "wet_l")) { wetL = v >= 0.5f; dsp.setWetEnable(wetL, wetR); return; }
        if (!strcmp(key, "wet_r")) { wetR = v >= 0.5f; dsp.setWetEnable(wetL, wetR); return; }
        if (!strcmp(key, "type")) { setType((int)lrintf(clampf(v, 0, kNumTypes - 1))); return; }
        if (!strcmp(key, "room_step")) { if (v >= 0.5f) stepRoom(); return; }
        if (!strcmp(key, "type_prev")) { if (v >= 0.5f) setType((type + kNumTypes - 1) % kNumTypes); return; }
        if (!strcmp(key, "type_next")) { if (v >= 0.5f) setType((type + 1) % kNumTypes); return; }
        if (!strcmp(key, "page")) { setPage((int)lrintf(clampf(v, 0, 2))); return; }
        if (!strcmp(key, "tab1") || !strcmp(key, "tab2") || !strcmp(key, "tab3")) { if (v >= 0.5f) setPage(key[3] - '1'); return; }
        // type_view, room_view and anything unknown: engine-driven / ignored
    }

    // one picture, three families (it covers the same box on every page: the picture is opaque, so it carries the page's own
    // art there): 0 General, 1..5 Tail with room size S..XXL, 6.. Space with its drawing
    int typeView() const { return page == 1 ? 6 + type : page == 2 ? 1 + dsp.roomClass() : 0; }
    int roomView() const { return page == 2 ? 1 + dsp.roomClass() : 0; }

    int get(const char *key, char *buf, int len) {
        const int k = knobOfKey(key);
        if (k >= 0) return snprintf(buf, len, "%.4f", (double)pos[k]);
        const size_t kl = strlen(key);
        if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
            char base[32];
            if (kl - 8 >= sizeof base) return 0;
            memcpy(base, key, kl - 8);
            base[kl - 8] = 0;
            const int bk = knobOfKey(base);
            if (bk >= 0) return display(bk, buf, len);
            const int q = qOfKey(base);
            if (q >= 0) { const int kk = kQMap[page][q]; return kk >= 0 ? display(kk, buf, len) : snprintf(buf, len, "-"); }
            return 0;
        }
        if (kl > 5 && !strcmp(key + kl - 5, "_name")) {
            char base[32];
            if (kl - 5 >= sizeof base) return 0;
            memcpy(base, key, kl - 5);
            base[kl - 5] = 0;
            const int q = qOfKey(base);
            if (q >= 0) { const int kk = kQMap[page][q]; return snprintf(buf, len, "%s", slotName(kk)); }
            return 0;
        }
        const int q = qOfKey(key);
        if (q >= 0) { const int kk = kQMap[page][q]; return snprintf(buf, len, "%.4f", (double)slotPos(kk)); }
        if (!strcmp(key, "freeze")) return snprintf(buf, len, "%d", freeze ? 1 : 0);
        if (!strcmp(key, "sync")) return snprintf(buf, len, "%d", sync ? 1 : 0);
        if (!strcmp(key, "wet_l")) return snprintf(buf, len, "%d", wetL ? 1 : 0);
        if (!strcmp(key, "wet_r")) return snprintf(buf, len, "%d", wetR ? 1 : 0);
        if (!strcmp(key, "type")) return snprintf(buf, len, "%d", type);
        if (!strcmp(key, "page")) return snprintf(buf, len, "%d", page);
        if (!strcmp(key, "type_view")) return snprintf(buf, len, "%d", typeView());
        if (!strcmp(key, "room_view")) return snprintf(buf, len, "%d", roomView());
        if (!strcmp(key, "type_prev") || !strcmp(key, "type_next") || !strcmp(key, "room_step") || !strcmp(key, "tab1") || !strcmp(key, "tab2") || !strcmp(key, "tab3"))
            return snprintf(buf, len, "0");
        if (!strcmp(key, "display_rev")) return snprintf(buf, len, "%u", displayRev);
        if (!strcmp(key, "state")) return save(buf, len);
        if (!strncmp(key, "preset:", 7)) {   // preset n's name without loading it
            const int n = atoi(key + 7);
            return (n >= 0 && n < kNumPresets) ? snprintf(buf, len, "%s", kPresets[n].name) : 0;
        }
        return nav.get(key, buf, len);
    }

    // ---- saved state (the whole sound, the page and the preset name) ---------------------------------------------------
    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "PVB1;page=%d;preset=%d;type=%d;fz=%d;sy=%d;wl=%d;wr=%d;v=", page, nav.cur, type, freeze ? 1 : 0,
                         sync ? 1 : 0, wetL ? 1 : 0, wetR ? 1 : 0);
        for (int k = 0; k < P_COUNT && n > 0 && n < len; ++k) n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)pos[k]);
        return (n > 0 && n < len) ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PVB1;", 5)) return;
        int npage = page, npre = 0, ntype = type, nfz = 0, nsy = 0, nwl = 1, nwr = 1;
        float np[P_COUNT];
        for (int k = 0; k < P_COUNT; ++k) np[k] = pos[k];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "page=", 5)) npage = parseInt(p + 5, 0, 2, npage);
            else if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);
            else if (!strncmp(p, "type=", 5)) ntype = parseInt(p + 5, 0, kNumTypes - 1, ntype);
            else if (!strncmp(p, "fz=", 3)) nfz = parseInt(p + 3, 0, 1, 0);
            else if (!strncmp(p, "sy=", 3)) nsy = parseInt(p + 3, 0, 1, 0);
            else if (!strncmp(p, "wl=", 3)) nwl = parseInt(p + 3, 0, 1, 1);
            else if (!strncmp(p, "wr=", 3)) nwr = parseInt(p + 3, 0, 1, 1);
            else if (!strncmp(p, "v=", 2)) {
                const char *q = p + 2;
                for (int k = 0; k < P_COUNT && q; ++k) {
                    float f;
                    if (!parseNum(q, f)) break;   // a broken list keeps the rest as it was
                    np[k] = clampf(f, 0.0f, 1000.0f);
                    const char *c = strchr(q, ','), *e = strchr(q, ';');
                    q = (c && (!e || c < e)) ? c + 1 : nullptr;
                }
            }
        }
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = np[k]; dsp.setParam(k, pos[k]); changed(k); }
        type = ntype; dsp.setType(type);
        freeze = nfz != 0; sync = nsy != 0; wetL = nwl != 0; wetR = nwr != 0;
        dsp.setFreeze(freeze); dsp.setSync(sync); dsp.setWetEnable(wetL, wetR);
        nav.restore(npre);
        page = npage;
        changed(X_FREEZE); changed(X_SYNC); changed(X_WETL); changed(X_WETR); changed(X_TYPE); changed(X_PAGE);
        changedViews(); changedQs();
        ++displayRev;
        lastRoom = dsp.roomClass();
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliVerb));
    return mem ? new (mem) PoliVerb() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliVerb *>(inst)->~PoliVerb();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<PoliVerb *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    return static_cast<PoliVerb *>(inst)->get(key, buf, buf_len);
}
static void engine_render(void *, int16_t *out_lr, int frames) { std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliVerb *>(inst);
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, kBlock);
        const int16_t *in = in_lr + 2 * done;
        int16_t *out = out_lr + 2 * done;
        for (int i = 0; i < n; ++i) {
            s->inL[i] = in[2 * i] * (1.0f / 32768.0f);
            s->inR[i] = in[2 * i + 1] * (1.0f / 32768.0f);
        }
        s->dsp.process(s->inL, s->inR, s->outL, s->outR, n);
        for (int i = 0; i < n; ++i) {
            out[2 * i] = (int16_t)lrintf(clampf(s->outL[i] * 32767.0f, -32768.0f, 32767.0f));
            out[2 * i + 1] = (int16_t)lrintf(clampf(s->outR[i] * 32767.0f, -32768.0f, 32767.0f));
        }
        done += n;
    }
}

// the host's tempo, once per block (vst.json "defines": {"HAS_SONG_POS": 1}); bpm 0 = the host gave none
static void engine_song_pos(void *inst, double, double bpm, int, int) { static_cast<PoliVerb *>(inst)->dsp.setTempo(bpm); }

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, engine_song_pos};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (a page switch, a preset, a Q-Link slot): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliVerb *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int k = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << k);
    snprintf(key, len, "%s", extraKey(k));
    return 1;
}
}
