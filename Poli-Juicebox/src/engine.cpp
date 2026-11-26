// Poli-Juicebox: the mpc_engine() contract (wrapper/engine.h) around JuiceDSP.
//
// Two pages (Shape / Tracking) of 28 knobs (positions 0..1000; the value text comes from here) and the P&F Link switch (Off / Variable).
// All are real parameters (stable for automation and Q-Links). Presets: src/presets.h (from presets/factory.py) in 5 categories
// + All; the selector and the browser live in juice_nav.h. A preset sets every knob and the link, so Init = the defaults.
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
#include "JuiceDSP.h"
#include "presets.h"
#include "juice_nav.h"
#include "qlinks_gen.h"

using namespace jb;

static const int kBlock = 128;

static const char *const kKnobKey[P_COUNT] = {"pitch", "formant", "ftrack", "thresh", "lowend", "attack", "release", "odd", "even", "color",
                                              "f1c", "f1q", "f2c", "f2q", "f3c", "f3q", "fsmooth", "air", "natural", "mix",
                                              "hold", "rasp", "glide", "guard", "morph", "floor", "ceil", "overlap"};

// keys the engine reports back when it moves them itself (mpc_engine_changed)
enum Extra { X_LINK = P_COUNT, X_LEARN, X_PROG, X_Q0, X_COUNT = X_Q0 + 16 };   // X_Q0.. = the Q-Link proxies ql01..ql16
static const int kProgFrames = 21;   // the Learn progress meter: frames of the strip
static const char *extraKey(int k) {
    static char ql[16][5];
    if (k >= X_Q0) { snprintf(ql[k - X_Q0], sizeof ql[0], "ql%02d", k - X_Q0 + 1); return ql[k - X_Q0]; }
    return k < P_COUNT ? kKnobKey[k] : k == X_LINK ? "link" : k == X_LEARN ? "learn" : "learn_prog";
}
// "ql07" -> 6, anything else -> -1
static int qSlotOf(const char *key) {
    if (key[0] != 'q' || key[1] != 'l' || key[2] < '0' || key[2] > '9' || key[3] < '0' || key[3] > '9' || key[4]) return -1;
    const int n = (key[2] - '0') * 10 + (key[3] - '0');
    return n >= 1 && n <= 16 ? n - 1 : -1;
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

struct PoliJuice {
    JuiceDSP dsp;
    float pos[P_COUNT];
    int link = 0;
    int latApplied = JuiceDSP::kLatencyDefault;   // the latency mode the DSP runs (the pills on the Tracking page set nav.lat)
    JuiceNav nav;
    int lastProg = 0;
    int qPage = 0;                                 // the page the Q-Link proxies are mapped for

    uint64_t pending = 0;
    unsigned displayRev = 1;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    PoliJuice() {
        for (int i = 0; i < P_COUNT; ++i) { pos[i] = kDefaultPos[i]; dsp.setParam(i, pos[i]); }
        dsp.setLink(false);
        dsp.init(44100.0f);
    }

    // ---- Q-Links: slot (0..15) -> knob on the page on show (qlinks_gen.h): a knob index, P_COUNT = the Link switch, -1 = nothing
    int qTarget(int slot) const {
        const char *k = kQKey[nav.page][slot];
        if (!*k) return -1;
        return !strcmp(k, "link") ? (int)P_COUNT : knobOfKey(k);
    }
    void markProxies(int t) { if (t >= 0 && t <= P_COUNT) for (int s = 0; s < 16; ++s) if (qTarget(s) == t) pending |= 1ull << (X_Q0 + s); }
    void markAllProxies() { for (int s = 0; s < 16; ++s) pending |= 1ull << (X_Q0 + s); }
    // the engine moved a value itself (a preset, Learn, the Link): the host hears about it, and about the Q-Links that show it
    void changed(int k) { pending |= 1ull << k; markProxies(k); }

    void setKnob(int k, float v) {
        pos[k] = clampf(v, 0.0f, 1000.0f);
        dsp.setParam(k, pos[k]);
    }
    void setLink(int on) {
        on = on ? 1 : 0;
        if (on == link) return;
        link = on;
        dsp.setLink(on != 0);
        changed(X_LINK);
    }
    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const JuicePreset &p = kPresets[idx];
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = (float)p.pos[k]; dsp.setParam(k, pos[k]); changed(k); }
        link = p.link ? 1 : 0;
        dsp.setLink(link != 0);
        changed(X_LINK);
        ++displayRev;
    }

    int progQ() const { return (int)lrintf(clampf(dsp.learnProgress(), 0.0f, 1.0f) * (kProgFrames - 1)); }
    // Learn is over (heard enough, or switched off): apply what it learned, from the knob values as they are (absolute: no drift)
    void finishLearn(bool apply) {
        if (!dsp.learning()) return;
        float p[P_COUNT];
        bool st[P_COUNT];
        const bool ok = dsp.learnResult(p, st);
        dsp.stopLearn();
        if (apply && ok) {
            for (int k = 0; k < P_COUNT; ++k) if (st[k]) { setKnob(k, p[k]); changed(k); }
            ++displayRev;
        }
        changed(X_LEARN);
        lastProg = 0;
        changed(X_PROG);
    }
    // after each audio call: Learn finished by itself, and the progress meter
    void afterProcess() {
        if (!dsp.learning()) return;
        if (dsp.learnFinished()) { finishLearn(true); return; }
        const int q = progQ();
        if (q != lastProg) { lastProg = q; changed(X_PROG); }
    }

    static int semis(char *buf, int len, float p) {
        const float v = JuiceMap::semis(p);
        const float a = fabsf(v);
        if (a < 0.05f) return snprintf(buf, len, "0 st");
        return a >= 9.95f ? snprintf(buf, len, "%+d st", (int)lrintf(v)) : snprintf(buf, len, "%+.1f st", (double)v);
    }
    static int hz(char *buf, int len, float f) {
        return f >= 995.0f ? snprintf(buf, len, "%.1f kHz", (double)(f * 0.001f)) : snprintf(buf, len, "%d Hz", (int)lrintf(f));
    }
    static int ms(char *buf, int len, float v) {
        return v < 9.95f ? snprintf(buf, len, "%.1f ms", (double)v) : snprintf(buf, len, "%d ms", (int)lrintf(v));
    }

    // value text of a knob
    int display(int k, char *buf, int len) const {
        const float p = pos[k];
        switch (k) {
            case P_PITCH: case P_FORMANT: return semis(buf, len, p);
            case P_THRESH: return snprintf(buf, len, "%d dB", (int)lrintf(JuiceMap::thresholdDb(p)));
            case P_LOWEND: return JuiceMap::lowEndOff(p) ? snprintf(buf, len, "Off") : hz(buf, len, JuiceMap::lowEndHz(p));
            case P_ATTACK: return ms(buf, len, JuiceMap::attackMs(p));
            case P_HOLD: return ms(buf, len, JuiceMap::holdMs(p));
            case P_MORPH: return ms(buf, len, JuiceMap::morphMs(p));
            case P_FLOOR: return hz(buf, len, JuiceMap::floorHz(p));
            case P_CEIL: return hz(buf, len, JuiceMap::ceilHz(p));
            case P_RELEASE: return ms(buf, len, JuiceMap::releaseMs(p));
            case P_COLOR: return hz(buf, len, JuiceMap::colorHz(p));
            case P_F1C: return hz(buf, len, JuiceMap::f1Hz(p));
            case P_F2C: return hz(buf, len, JuiceMap::f2Hz(p));
            case P_F3C: return hz(buf, len, JuiceMap::f3Hz(p));
            case P_F1Q: case P_F2Q: case P_F3Q:
                return JuiceMap::qOff(p) ? snprintf(buf, len, "Off") : snprintf(buf, len, "%.1f", (double)JuiceMap::q(p));
            default: return snprintf(buf, len, "%d %%", (int)lrintf(JuiceMap::pct(p)));   // F-Track, drives, smooth, air, natural, mix, rasp, glide, guard, overlap
        }
    }

    static int knobOfKey(const char *key) {
        for (int k = 0; k < P_COUNT; ++k) if (!strcmp(key, kKnobKey[k])) return k;
        return -1;
    }

    void set(const char *key, const char *val) {
        if (!strcmp(key, "state")) { load(val); return; }
        const int pick = nav.set(key, val);
        if (nav.page != qPage) { qPage = nav.page; markAllProxies(); }   // the Q-Links follow the page on show
        if (nav.lat != latApplied) { latApplied = nav.lat; dsp.setLatencyMode(latApplied); }
        if (pick >= 0) { applyPreset(pick); return; }
        if (pick == JuiceNav::HANDLED) return;
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        const int qs = qSlotOf(key);
        if (qs >= 0) {   // a Q-Link: it moves the knob it stands for (the host knows what it wrote: only the knob is reported)
            const int t = qTarget(qs);
            if (t >= 0 && t < P_COUNT) { setKnob(t, v); pending |= 1ull << t; }
            else if (t == P_COUNT) setLink(v >= 500.0f ? 1 : 0);
            return;
        }
        const int k = knobOfKey(key);
        if (k >= 0) { setKnob(k, v); markProxies(k); return; }
        if (!strcmp(key, "link")) { setLink(v >= 0.5f ? 1 : 0); return; }
        if (!strcmp(key, "learn")) {
            if (v >= 0.5f && !dsp.learning()) { dsp.startLearn(); lastProg = 0; changed(X_LEARN); }
            else if (v < 0.5f && dsp.learning()) finishLearn(true);   // Off early: use what it heard
            return;
        }
        // anything unknown: ignored
    }

    int get(const char *key, char *buf, int len) {
        {   // the Q-Link proxies: value, name and value text of whatever they stand for on this page
            char base[8];
            const size_t kl0 = strlen(key);
            const char *suffix = kl0 > 4 ? key + 4 : "";
            if (kl0 >= 4 && kl0 < sizeof base + 8 && (kl0 == 4 || !strcmp(suffix, "_name") || !strcmp(suffix, "_display"))) {
                memcpy(base, key, 4);
                base[4] = 0;
                const int qs = qSlotOf(base);
                if (qs >= 0) {
                    const int t = qTarget(qs);
                    if (!strcmp(suffix, "_name")) return snprintf(buf, len, "%s", kQName[nav.page][qs]);
                    if (!strcmp(suffix, "_display")) {
                        if (t < 0) return snprintf(buf, len, "--");
                        if (t == P_COUNT) return snprintf(buf, len, link ? "Variable" : "Off");
                        return display(t, buf, len);
                    }
                    return snprintf(buf, len, "%.4f", t < 0 ? 0.0 : t == P_COUNT ? (link ? 1000.0 : 0.0) : (double)pos[t]);
                }
            }
        }
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
        if (!strcmp(key, "link")) return snprintf(buf, len, "%d", link);
        if (!strcmp(key, "learn")) return snprintf(buf, len, "%d", dsp.learning() ? 1 : 0);
        if (!strcmp(key, "learn_prog")) return snprintf(buf, len, "%.4f", (double)progQ() / (kProgFrames - 1));
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
        int n = snprintf(buf, len, "PJB2;preset=%d;lk=%d;lt=%d;v=", nav.cur, link, nav.lat);
        for (int k = 0; k < P_COUNT && n > 0 && n < len; ++k) n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)pos[k]);
        return (n > 0 && n < len) ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PJB2;", 5)) return;
        int npre = 0, nlk = link, nlat = nav.lat;
        float np[P_COUNT];
        for (int k = 0; k < P_COUNT; ++k) np[k] = pos[k];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);
            else if (!strncmp(p, "lk=", 3)) nlk = parseInt(p + 3, 0, 1, 0);
            else if (!strncmp(p, "lt=", 3)) nlat = parseInt(p + 3, 0, 2, 1);
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
        link = nlk;
        dsp.setLink(link != 0);
        changed(X_LINK);
        nav.restore(npre);
        if (nlat != nav.lat) { nav.lat = nlat; nav.dirty |= 64; }
        if (nav.lat != latApplied) { latApplied = nav.lat; dsp.setLatencyMode(latApplied); }
        ++displayRev;
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliJuice));
    return mem ? new (mem) PoliJuice() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliJuice *>(inst)->~PoliJuice();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<PoliJuice *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    return static_cast<PoliJuice *>(inst)->get(key, buf, buf_len);
}
static void engine_render(void *, int16_t *out_lr, int frames) { std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliJuice *>(inst);
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
    s->afterProcess();
}

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, nullptr};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (a preset): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliJuice *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int k = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << k);
    snprintf(key, len, "%s", extraKey(k));
    return 1;
}
}
