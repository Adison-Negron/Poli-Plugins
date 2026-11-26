// Poli-delay: the mpc_engine() contract (wrapper/engine.h) around DelayDSP.
//
// One page. Knobs: Time, Offset (shown only while the Offset toggle is on), Feedback, High Pass, Low Pass, Mix, Rate, Depth,
// Drive, Tone, Spread; toggles: Sync, Offset, Ping-Pong. All are real parameters (stable for automation and Q-Links);
// the skin shows the Offset knob with when=offset:On.
// Tempo: HAS_SONG_POS gives the host tempo (song_pos), used when Sync is on: Time becomes a note value (1/32 .. 1/1).
// Presets: 75 factory presets (src/presets.h, from presets/factory.py) in 8 categories + All; the browser lives in
// delay_nav.h. A preset sets every parameter, so Init = the defaults.
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
#include "DelayDSP.h"
#include "presets.h"
#include "delay_nav.h"

using namespace delay;

static const int kBlock = 128;

static const char *const kKnobKey[P_COUNT] = {"time", "offset_amt", "feedback", "hp", "lp", "mix", "rate", "depth", "drive", "tone", "spread"};

// keys the engine reports back when it moves them itself (mpc_engine_changed)
enum Extra { X_SYNC = P_COUNT, X_OFFSET, X_PP, X_COUNT };
static const char *extraKey(int k) {
    static const char *const names[X_COUNT - P_COUNT] = {"sync", "offset", "pingpong"};
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
static int fmtPct(char *buf, int len, float pct) { return snprintf(buf, len, "%d %%", (int)lrintf(pct)); }
static int fmtSigned(char *buf, int len, float pct) {
    const int v = (int)lrintf(pct);
    return v > 0 ? snprintf(buf, len, "+%d %%", v) : snprintf(buf, len, "%d %%", v);
}
static int fmtTime(char *buf, int len, float ms) {   // "5 ms", "7.5 ms", "125 ms", "1.5 s", "2 s"
    char n[24];
    if (ms >= 1999.5f) return snprintf(buf, len, "2 s");
    if (ms >= 999.5f) { snprintf(n, sizeof n, "%.2f", (double)(ms / 1000.0f)); trim(n); return snprintf(buf, len, "%s s", n); }
    if (ms < 9.95f) { snprintf(n, sizeof n, "%.1f", (double)ms); trim(n); return snprintf(buf, len, "%s ms", n); }
    return snprintf(buf, len, "%d ms", (int)lrintf(ms));
}
static int fmtHz(char *buf, int len, float hz) {
    char n[24];
    if (hz >= 9950.0f) return snprintf(buf, len, "%d kHz", (int)lrintf(hz / 1000.0f));   // "12 kHz": short enough for the knob
    if (hz >= 995.0f) { snprintf(n, sizeof n, "%.1f", (double)(hz / 1000.0f)); trim(n); return snprintf(buf, len, "%s kHz", n); }
    return snprintf(buf, len, "%d Hz", (int)lrintf(hz));
}

struct PoliDelay {
    DelayDSP dsp;
    float pos[P_COUNT];
    bool sync = false, offsetOn = false, pingPong = false;
    DelayNav nav;

    uint64_t pending = 0;
    unsigned displayRev = 1;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    PoliDelay() {
        for (int i = 0; i < P_COUNT; ++i) { pos[i] = kDefaultPos[i]; dsp.setParam(i, pos[i]); }
        dsp.init(44100.0f);
    }

    void changed(int k) { pending |= 1ull << k; }

    void setKnob(int k, float v) {
        pos[k] = clampf(v, 0.0f, 1000.0f);
        dsp.setParam(k, pos[k]);
    }
    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const DelayPreset &p = kPresets[idx];
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = (float)p.pos[k]; dsp.setParam(k, pos[k]); changed(k); }
        sync = p.sync != 0; offsetOn = p.offset != 0; pingPong = p.pingpong != 0;
        dsp.setSync(sync); dsp.setOffsetOn(offsetOn); dsp.setPingPong(pingPong);
        changed(X_SYNC); changed(X_OFFSET); changed(X_PP);
        ++displayRev;
    }

    // value text of a knob
    int display(int k, char *buf, int len) const {
        const float p = pos[k];
        switch (k) {
            case P_TIME:
                if (sync) return snprintf(buf, len, "%s", kDivName[DelayMap::divIndex(p)]);
                return fmtTime(buf, len, DelayMap::timeMs(p));
            case P_OFFSET: return fmtSigned(buf, len, DelayMap::offsetPct(p));
            case P_FEEDBACK: return fmtPct(buf, len, DelayMap::feedbackPct(p));
            case P_HP: return p < 1.5f ? snprintf(buf, len, "Off") : fmtHz(buf, len, DelayMap::hpHz(p));
            case P_LP: return p > 998.5f ? snprintf(buf, len, "Off") : fmtHz(buf, len, DelayMap::lpHz(p));
            case P_MIX: return fmtPct(buf, len, DelayMap::mixPct(p));
            case P_RATE: { const float hz = DelayMap::rateHz(p);
                           return hz < 1.0f ? snprintf(buf, len, "%.2f Hz", (double)hz) : snprintf(buf, len, "%.1f Hz", (double)hz); }
            case P_DEPTH: return fmtPct(buf, len, DelayMap::depthPct(p));
            case P_DRIVE: return fmtPct(buf, len, DelayMap::drivePct(p));
            case P_TONE: return fmtSigned(buf, len, DelayMap::tonePct(p));
            case P_SPREAD: return fmtPct(buf, len, DelayMap::spreadPct(p));
        }
        return 0;
    }

    static int knobOfKey(const char *key) {
        for (int k = 0; k < P_COUNT; ++k) if (!strcmp(key, kKnobKey[k])) return k;
        return -1;
    }

    void set(const char *key, const char *val) {
        if (!strcmp(key, "state")) { load(val); return; }
        const int pick = nav.set(key, val);
        if (pick >= 0) { applyPreset(pick); return; }
        if (pick == DelayNav::HANDLED) return;
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        const int k = knobOfKey(key);
        if (k >= 0) { setKnob(k, v); return; }
        if (!strcmp(key, "sync")) {
            sync = v >= 0.5f;
            dsp.setSync(sync);
            changed(P_TIME);
            ++displayRev;   // Time's readout changes between ms and a note value
            return;
        }
        if (!strcmp(key, "offset")) { offsetOn = v >= 0.5f; dsp.setOffsetOn(offsetOn); return; }
        if (!strcmp(key, "pingpong")) { pingPong = v >= 0.5f; dsp.setPingPong(pingPong); return; }
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
        if (!strcmp(key, "sync")) return snprintf(buf, len, "%d", sync ? 1 : 0);
        if (!strcmp(key, "offset")) return snprintf(buf, len, "%d", offsetOn ? 1 : 0);
        if (!strcmp(key, "pingpong")) return snprintf(buf, len, "%d", pingPong ? 1 : 0);
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
        int n = snprintf(buf, len, "PDL1;preset=%d;sy=%d;of=%d;pp=%d;v=", nav.cur, sync ? 1 : 0, offsetOn ? 1 : 0, pingPong ? 1 : 0);
        for (int k = 0; k < P_COUNT && n > 0 && n < len; ++k) n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)pos[k]);
        return (n > 0 && n < len) ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PDL1;", 5)) return;
        int npre = 0, nsy = 0, nof = 0, npp = 0;
        float np[P_COUNT];
        for (int k = 0; k < P_COUNT; ++k) np[k] = pos[k];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);
            else if (!strncmp(p, "sy=", 3)) nsy = parseInt(p + 3, 0, 1, 0);
            else if (!strncmp(p, "of=", 3)) nof = parseInt(p + 3, 0, 1, 0);
            else if (!strncmp(p, "pp=", 3)) npp = parseInt(p + 3, 0, 1, 0);
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
        sync = nsy != 0; offsetOn = nof != 0; pingPong = npp != 0;
        dsp.setSync(sync); dsp.setOffsetOn(offsetOn); dsp.setPingPong(pingPong);
        nav.restore(npre);
        changed(X_SYNC); changed(X_OFFSET); changed(X_PP);
        ++displayRev;
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliDelay));
    return mem ? new (mem) PoliDelay() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliDelay *>(inst)->~PoliDelay();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<PoliDelay *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    return static_cast<PoliDelay *>(inst)->get(key, buf, buf_len);
}
static void engine_render(void *, int16_t *out_lr, int frames) { std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliDelay *>(inst);
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
static void engine_song_pos(void *inst, double, double bpm, int, int) { static_cast<PoliDelay *>(inst)->dsp.setTempo(bpm); }

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, engine_song_pos};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (a preset, a Sync switch): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliDelay *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int k = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << k);
    snprintf(key, len, "%s", extraKey(k));
    return 1;
}
}
