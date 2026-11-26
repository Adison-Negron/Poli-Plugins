// Poli-Chorus: the mpc_engine() contract (wrapper/engine.h) around ChoDSP.
//
// One page. Knobs: Rate, Amount, Feedback, Width, LP Filter, Warmth, Output, Mix (positions 0..1000, the value text comes from here);
// Mode: Classic / Ensemble / Vibrato (an option list; three buttons set it, the lit one follows it). The display box draws the two LFOs
// (vl001.. / vr001..: one meter per column, see ChoViz.h). Presets: factory presets (src/presets.h, from presets/factory.py) in 6
// categories + All; the selector and the browser live in chorus_nav.h. A preset sets every parameter and the Mode, so Init = the defaults.
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
#include "ChoDSP.h"
#include "ChoViz.h"
#include "presets.h"
#include "chorus_nav.h"

using namespace cho;

static const int kBlock = 128;

static const char *const kKnobKey[P_COUNT] = {"rate", "amount", "feedback", "width", "lpf", "warmth", "output", "mix"};

// keys the engine reports back when it moves them itself (mpc_engine_changed)
enum Extra { X_MODE = P_COUNT, X_COUNT };
static const char *extraKey(int k) { return k < P_COUNT ? kKnobKey[k] : "mode"; }

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

// "vl012" / "vr128": column key -> index, else -1
static int vizIndex(const char *key, char side) {
    if (key[0] != 'v' || key[1] != side || key[2] < '0' || key[2] > '9') return -1;
    char *end = nullptr;
    const long n = strtol(key + 2, &end, 10);
    return (*end || n < 1 || n > kVizCols) ? -1 : (int)n - 1;
}

struct PoliCho {
    ChoDSP dsp;
    float pos[P_COUNT];
    int mode = M_CLASSIC;
    ChoNav nav;

    uint64_t pending = 0;
    unsigned displayRev = 1;
    // the display box
    bool vizDirty = true;
    uint8_t vl[kVizCols], vr[kVizCols];
    uint32_t vizSig = 0;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    PoliCho() {
        for (int i = 0; i < P_COUNT; ++i) { pos[i] = kDefaultPos[i]; dsp.setParam(i, pos[i]); }
        dsp.setMode(mode);
        dsp.init(44100.0f);
        refreshViz();
    }

    void changed(int k) { pending |= 1ull << k; }

    // recompute the display columns; a change of any column bumps display_rev (the wrapper polls it every 100 ms and reports the columns that moved)
    void refreshViz() {
        if (!vizDirty) return;
        vizDirty = false;
        uint32_t h = 2166136261u;
        for (int j = 0; j < kVizCols; ++j) {
            vl[j] = (uint8_t)vizFrame(mode, pos[P_RATE], pos[P_AMOUNT], pos[P_WIDTH], j, false);
            vr[j] = (uint8_t)vizFrame(mode, pos[P_RATE], pos[P_AMOUNT], pos[P_WIDTH], j, true);
            h = (h ^ vl[j]) * 16777619u;
            h = (h ^ vr[j]) * 16777619u;
        }
        if (h != vizSig) { vizSig = h; ++displayRev; }
    }

    void setKnob(int k, float v) {
        pos[k] = clampf(v, 0.0f, 1000.0f);
        dsp.setParam(k, pos[k]);
        if (k == P_RATE || k == P_AMOUNT || k == P_WIDTH) vizDirty = true;
    }
    void setMode(int m) {
        m = std::max(0, std::min(M_COUNT - 1, m));
        if (m == mode) return;
        mode = m;
        dsp.setMode(m);
        vizDirty = true;
        changed(X_MODE);
    }
    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const ChoPreset &p = kPresets[idx];
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = (float)p.pos[k]; dsp.setParam(k, pos[k]); changed(k); }
        mode = std::min((int)p.mode, M_COUNT - 1);
        dsp.setMode(mode);
        changed(X_MODE);
        vizDirty = true;
        ++displayRev;
    }

    // value text of a knob
    int display(int k, char *buf, int len) const {
        const float p = pos[k];
        switch (k) {
            case P_RATE: {
                const float hz = ChoMap::rateHz(p);
                return hz < 0.995f ? snprintf(buf, len, "%.2f Hz", (double)hz) : snprintf(buf, len, "%.1f Hz", (double)hz);
            }
            case P_LPF: {
                if (ChoMap::lpfOff(p)) return snprintf(buf, len, "Off");
                const float hz = ChoMap::lpfHz(p);
                if (hz < 999.5f) return snprintf(buf, len, "%d Hz", (int)lrintf(hz));
                return hz < 9950.0f ? snprintf(buf, len, "%.1f kHz", (double)hz * 0.001) : snprintf(buf, len, "%d kHz", (int)lrintf(hz * 0.001f));
            }
            case P_OUTPUT: {
                const float db = ChoMap::outputDb(p);
                if (fabsf(db) < 0.05f) return snprintf(buf, len, "0.0 dB");
                return fabsf(db) >= 9.95f ? snprintf(buf, len, "%+d dB", (int)lrintf(db)) : snprintf(buf, len, "%+.1f dB", (double)db);
            }
            default:   // Amount, Feedback, Width, Warmth, Mix: percent
                return snprintf(buf, len, "%d %%", (int)lrintf(p * 0.1f));
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
        if (pick == ChoNav::HANDLED) return;
        if (!strncmp(key, "mode_", 5)) {
            if (press(val)) {
                if (!strcmp(key + 5, "classic")) setMode(M_CLASSIC);
                else if (!strcmp(key + 5, "ensemble")) setMode(M_ENSEMBLE);
                else if (!strcmp(key + 5, "vibrato")) setMode(M_VIBRATO);
            }
            return;
        }
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        const int k = knobOfKey(key);
        if (k >= 0) { setKnob(k, v); return; }
        if (!strcmp(key, "mode")) { setMode((int)lrintf(clampf(v, 0.0f, (float)(M_COUNT - 1)))); return; }
        // anything unknown (the display columns too: read-only): ignored
    }

    int get(const char *key, char *buf, int len) {
        const int k = knobOfKey(key);
        if (k >= 0) return snprintf(buf, len, "%.4f", (double)pos[k]);
        int j;
        if ((j = vizIndex(key, 'l')) >= 0) { refreshViz(); return snprintf(buf, len, "%.4f", (double)vl[j] / (kVizFrames - 1)); }
        if ((j = vizIndex(key, 'r')) >= 0) { refreshViz(); return snprintf(buf, len, "%.4f", (double)vr[j] / (kVizFrames - 1)); }
        const size_t kl = strlen(key);
        if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
            char base[32];
            if (kl - 8 >= sizeof base) return 0;
            memcpy(base, key, kl - 8);
            base[kl - 8] = 0;
            const int bk = knobOfKey(base);
            return bk >= 0 ? display(bk, buf, len) : 0;
        }
        if (!strcmp(key, "mode")) return snprintf(buf, len, "%d", mode);
        if (!strncmp(key, "mode_", 5)) return snprintf(buf, len, "0");
        if (!strcmp(key, "display_rev")) { refreshViz(); return snprintf(buf, len, "%u", displayRev); }
        if (!strcmp(key, "state")) return save(buf, len);
        if (!strncmp(key, "preset:", 7)) {   // preset n's name without loading it
            const int n = atoi(key + 7);
            return (n >= 0 && n < kNumPresets) ? snprintf(buf, len, "%s", kPresets[n].name) : 0;
        }
        return nav.get(key, buf, len);
    }

    // ---- saved state (the whole sound and the preset name) ---------------------------------------------------------------
    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "PCH1;preset=%d;md=%d;v=", nav.cur, mode);
        for (int k = 0; k < P_COUNT && n > 0 && n < len; ++k) n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)pos[k]);
        return (n > 0 && n < len) ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PCH1;", 5)) return;
        int npre = 0, nmd = mode;
        float np[P_COUNT];
        for (int k = 0; k < P_COUNT; ++k) np[k] = pos[k];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);
            else if (!strncmp(p, "md=", 3)) nmd = parseInt(p + 3, 0, M_COUNT - 1, 0);
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
        mode = nmd;
        dsp.setMode(mode);
        changed(X_MODE);
        nav.restore(npre);
        vizDirty = true;
        ++displayRev;
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliCho));
    return mem ? new (mem) PoliCho() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliCho *>(inst)->~PoliCho();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<PoliCho *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    return static_cast<PoliCho *>(inst)->get(key, buf, buf_len);
}
static void engine_render(void *, int16_t *out_lr, int frames) { std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliCho *>(inst);
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

// values the engine moved by itself (a preset, a Mode switch): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliCho *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int k = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << k);
    snprintf(key, len, "%s", extraKey(k));
    return 1;
}
}
