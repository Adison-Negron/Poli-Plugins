// Poli-Saturator: the mpc_engine() contract (wrapper/engine.h) around SatDSP.
//
// One page. Knobs: Saturation, Smash, Crunch, Soften, Low, Mid, High (positions 0..1000, the value text comes from here);
// Type: Tube / Tape / Clip / Fold (an option list with < > buttons). All are real parameters (stable for automation and
// Q-Links). Presets: 65 factory presets (src/presets.h, from presets/factory.py) in 6 categories + All; the selector and
// the browser live in sat_nav.h. A preset sets every parameter and the Type, so Init = the defaults.
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
#include "SatDSP.h"
#include "presets.h"
#include "sat_nav.h"

using namespace sat;

static const int kBlock = 128;

static const char *const kKnobKey[P_COUNT] = {"sat", "smash", "crunch", "soften", "low", "mid", "high"};

// keys the engine reports back when it moves them itself (mpc_engine_changed)
enum Extra { X_TYPE = P_COUNT, X_COUNT };
static const char *extraKey(int k) { return k < P_COUNT ? kKnobKey[k] : "type"; }

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
static bool press(const char *v) { float f; return parseNum(v, f) && f >= 0.5f; }

struct PoliSat {
    SatDSP dsp;
    float pos[P_COUNT];
    int type = T_TUBE;
    SatNav nav;

    uint64_t pending = 0;
    unsigned displayRev = 1;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    PoliSat() {
        for (int i = 0; i < P_COUNT; ++i) { pos[i] = kDefaultPos[i]; dsp.setParam(i, pos[i]); }
        dsp.setType(type);
        dsp.init(44100.0f);
    }

    void changed(int k) { pending |= 1ull << k; }

    void setKnob(int k, float v) {
        pos[k] = clampf(v, 0.0f, 1000.0f);
        dsp.setParam(k, pos[k]);
    }
    void setType(int t) {
        t = std::max(0, std::min(T_COUNT - 1, t));
        if (t == type) return;
        type = t;
        dsp.setType(t);
        changed(X_TYPE);
    }
    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const SatPreset &p = kPresets[idx];
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = (float)p.pos[k]; dsp.setParam(k, pos[k]); changed(k); }
        type = std::min((int)p.type, T_COUNT - 1);
        dsp.setType(type);
        changed(X_TYPE);
        ++displayRev;
    }

    // value text of a knob
    int display(int k, char *buf, int len) const {
        const float p = pos[k];
        switch (k) {
            case P_SAT: case P_SMASH: case P_CRUNCH: case P_SOFTEN:
                return snprintf(buf, len, "%d %%", (int)lrintf(p * 0.1f));
            default: {   // Low / Mid / High: the band's drive relative to Saturation
                if (SatMap::bandOff(p)) return snprintf(buf, len, "Off");
                const int db = (int)lrintf(SatMap::bandDb(p));
                return db > 0 ? snprintf(buf, len, "+%d dB", db) : db == 0 ? snprintf(buf, len, "0 dB") : snprintf(buf, len, "%d dB", db);
            }
        }
    }

    static int knobOfKey(const char *key) {
        for (int k = 0; k < P_COUNT; ++k) if (!strcmp(key, kKnobKey[k])) return k;
        return -1;
    }

    void set(const char *key, const char *val) {
        if (!strcmp(key, "state")) { load(val); return; }
        const int pick = nav.set(key, val);
        if (pick >= 0) { applyPreset(pick); return; }
        if (pick == SatNav::HANDLED) return;
        if (!strcmp(key, "type_prev") || !strcmp(key, "type_next")) {
            if (press(val)) setType((type + (key[5] == 'n' ? 1 : T_COUNT - 1)) % T_COUNT);
            return;
        }
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        const int k = knobOfKey(key);
        if (k >= 0) { setKnob(k, v); return; }
        if (!strcmp(key, "type")) { setType((int)lrintf(clampf(v, 0.0f, (float)(T_COUNT - 1)))); return; }
        // anything unknown: ignored
    }

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
            return bk >= 0 ? display(bk, buf, len) : 0;
        }
        if (!strcmp(key, "type")) return snprintf(buf, len, "%d", type);
        if (!strcmp(key, "type_prev") || !strcmp(key, "type_next")) return snprintf(buf, len, "0");
        if (!strcmp(key, "display_rev")) return snprintf(buf, len, "%u", displayRev);
        if (!strcmp(key, "state")) return save(buf, len);
        if (!strncmp(key, "preset:", 7)) {   // preset n's name without loading it
            const int n = atoi(key + 7);
            return (n >= 0 && n < kNumPresets) ? snprintf(buf, len, "%s", kPresets[n].name) : 0;
        }
        return nav.get(key, buf, len);
    }

    // ---- saved state (the whole sound and the preset name) ---------------------------------------------------------------
    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "PSA1;preset=%d;ty=%d;v=", nav.cur, type);
        for (int k = 0; k < P_COUNT && n > 0 && n < len; ++k) n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)pos[k]);
        return (n > 0 && n < len) ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PSA1;", 5)) return;
        int npre = 0, nty = type;
        float np[P_COUNT];
        for (int k = 0; k < P_COUNT; ++k) np[k] = pos[k];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);
            else if (!strncmp(p, "ty=", 3)) nty = parseInt(p + 3, 0, T_COUNT - 1, 0);
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
        type = nty;
        dsp.setType(type);
        changed(X_TYPE);
        nav.restore(npre);
        ++displayRev;
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliSat));
    return mem ? new (mem) PoliSat() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliSat *>(inst)->~PoliSat();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<PoliSat *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    return static_cast<PoliSat *>(inst)->get(key, buf, buf_len);
}
static void engine_render(void *, int16_t *out_lr, int frames) { std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliSat *>(inst);
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

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, nullptr};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (a preset, a Type switch): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliSat *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int k = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << k);
    snprintf(key, len, "%s", extraKey(k));
    return 1;
}
}
