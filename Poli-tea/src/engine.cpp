// Poli-tea: the mpc_engine() contract (wrapper/engine.h) around TeaDSP.
//
// One page. Knobs: Input, Drive, Low, Mid, High, Amount (Core), Limiter Drive, Limiter Threshold, Output; toggle: Sidechain
// (Internal / External). All are real parameters (stable for automation and Q-Links); the knobs show their own value text
// ("dynamic_display"). Six live meters (display only): the input peak and RMS (with the sweet-spot zone drawn in the strip) and
// the gain reduction of the three bands and of the limiter. They are pictures, moved by display_rev
// (vst.json defines HAS_DISPLAY_REV + DISPLAY_REV_NO_UPDATE), never text, so nothing rebuilds the screen under a dragging finger.
// Presets: 57 factory presets (src/presets.h, from presets/factory.py) in 8 categories + All; the browser lives in tea_nav.h.
// A preset sets every parameter, so Init = the defaults.
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
#include "TeaDSP.h"
#include "presets.h"
#include "tea_nav.h"

using namespace tea;

static const int kBlock = 128;
static const int kInFrames = 73;     // input meters: -36 .. 0 dBFS in 0.5 dB steps
static const int kGrFrames = 49;     // gain-reduction meters: 0 .. -12 dB in 0.25 dB steps

static const char *const kKnobKey[P_COUNT] = {"input", "drive", "low", "mid", "high", "amount", "ldrive", "lthresh", "output"};

// keys the engine reports back when it moves them itself (mpc_engine_changed)
enum Extra { X_SC = P_COUNT, X_COUNT };
static const char *extraKey(int k) { return k < P_COUNT ? kKnobKey[k] : "sc"; }

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

// ---- value text -----------------------------------------------------------------------------------------------------------
static void trim(char *b) {   // "12.0" -> "12", "-0" -> "0"
    char *dot = strchr(b, '.');
    if (dot && dot[1] == '0' && dot[2] == 0) *dot = 0;
    if (!strcmp(b, "-0") || !strcmp(b, "+0")) strcpy(b, "0");
}
static int fmtPct(char *buf, int len, float pct) { return snprintf(buf, len, "%d %%", (int)lrintf(pct)); }
static int fmtDb(char *buf, int len, float db, bool plus) {   // "-0.3 dB", "+2.5 dB", "0 dB", "-18 dB"
    char n[24];
    float v = lrintf(db * 10.0f) / 10.0f;
    if (v == 0.0f) v = 0.0f;
    snprintf(n, sizeof n, "%.1f", (double)v);
    trim(n);
    if (plus && v > 0.0f) return snprintf(buf, len, "+%s dB", n);
    return snprintf(buf, len, "%s dB", n);
}

struct PoliTea {
    TeaDSP dsp;
    float pos[P_COUNT];
    bool sc = false;
    TeaNav nav;

    uint64_t pending = 0;
    unsigned displayRev = 1;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    // meters: what the strips show (frame indices) and the smoothed values behind them
    float mPk = 0, mRms2 = 0, mGr[4] = {0, 0, 0, 0};
    int fr[6] = {0, 0, 0, 0, 0, 0};          // pk, rms, low, mid, high, lim

    PoliTea() {
        for (int i = 0; i < P_COUNT; ++i) { pos[i] = kDefaultPos[i]; dsp.setParam(i, pos[i]); }
        dsp.init(44100.0f);
    }

    void changed(int k) { pending |= 1ull << k; }

    void setKnob(int k, float v) {
        pos[k] = clampf(v, 0.0f, 1000.0f);
        dsp.setParam(k, pos[k]);
    }
    void setSc(bool on) { sc = on; dsp.setExternalSidechain(on); }
    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const TeaPreset &p = kPresets[idx];
        for (int k = 0; k < P_COUNT; ++k) { pos[k] = (float)p.pos[k]; dsp.setParam(k, pos[k]); changed(k); }
        setSc(p.sc != 0);
        changed(X_SC);
        ++displayRev;
    }

    // value text of a knob
    int display(int k, char *buf, int len) const {
        const float p = pos[k];
        switch (k) {
            case P_INPUT: return fmtDb(buf, len, TeaMap::inputDb(p), true);
            case P_DRIVE: return fmtPct(buf, len, TeaMap::drivePct(p));
            case P_LOW: return fmtDb(buf, len, TeaMap::focusPct(p) * 0.06f, true);
            case P_MID: return fmtDb(buf, len, TeaMap::focusPct(p) * 0.04f, true);
            case P_HIGH: return fmtDb(buf, len, TeaMap::focusPct(p) * 0.06f, true);
            case P_AMOUNT: return fmtPct(buf, len, TeaMap::amountPct(p));
            case P_LDRIVE: return fmtPct(buf, len, TeaMap::ldrivePct(p));
            case P_LTHRESH: return fmtDb(buf, len, TeaMap::lthreshDb(p), false);
            case P_OUTPUT: return fmtDb(buf, len, TeaMap::outputDb(p), false);
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
        if (pick == TeaNav::HANDLED) return;
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        const int k = knobOfKey(key);
        if (k >= 0) { setKnob(k, v); return; }
        if (!strcmp(key, "sc")) { setSc(v >= 0.5f); return; }
        // meters, display_rev, anything unknown: ignored
    }

    // ---- meters ---------------------------------------------------------------------------------------------------------------
    static int inFrame(float lin) {
        if (lin < 1e-6f) return 0;
        const float db = 20.0f * log10f(lin);
        if (db <= -36.0f) return 0;
        return std::min(kInFrames - 1, (int)lrintf((db + 36.0f) * ((kInFrames - 1) / 36.0f)));
    }
    static int grFrame(float db) {   // db <= 0
        if (db >= -0.12f) return 0;
        return std::min(kGrFrames - 1, (int)lrintf(-db * ((kGrFrames - 1) / 12.0f)));
    }
    void updateMeters() {
        Meters m;
        dsp.takeMeters(m);
        const float fall = 0.9907f;                 // per 128-frame block: ~300 ms to fall by 1/e
        mPk = std::max(m.inPeak, mPk * fall);
        mRms2 += 0.0095f * (m.inRms * m.inRms - mRms2);   // ~300 ms RMS window
        const float gr[4] = {m.grLow, m.grMid, m.grHigh, m.grLim};
        for (int i = 0; i < 4; ++i) mGr[i] = std::min(gr[i], std::min(0.0f, mGr[i] + 0.035f));   // instant attack, ~12 dB/s fall
        int nf[6] = {inFrame(mPk), inFrame(sqrtf(mRms2)), grFrame(mGr[0]), grFrame(mGr[1]), grFrame(mGr[2]), grFrame(mGr[3])};
        bool moved = false;
        for (int i = 0; i < 6; ++i) if (nf[i] != fr[i]) { fr[i] = nf[i]; moved = true; }
        if (moved) ++displayRev;
    }
    void silentMeters() {
        mPk = 0; mRms2 = 0;
        for (int i = 0; i < 4; ++i) mGr[i] = 0;
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
        if (!strcmp(key, "sc")) return snprintf(buf, len, "%d", sc ? 1 : 0);
        if (!strcmp(key, "m_pk")) return snprintf(buf, len, "%.4f", fr[0] / (double)(kInFrames - 1));
        if (!strcmp(key, "m_rms")) return snprintf(buf, len, "%.4f", fr[1] / (double)(kInFrames - 1));
        if (!strcmp(key, "m_gl")) return snprintf(buf, len, "%.4f", fr[2] / (double)(kGrFrames - 1));
        if (!strcmp(key, "m_gm")) return snprintf(buf, len, "%.4f", fr[3] / (double)(kGrFrames - 1));
        if (!strcmp(key, "m_gh")) return snprintf(buf, len, "%.4f", fr[4] / (double)(kGrFrames - 1));
        if (!strcmp(key, "m_lim")) return snprintf(buf, len, "%.4f", fr[5] / (double)(kGrFrames - 1));
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
        int n = snprintf(buf, len, "PTA1;preset=%d;sc=%d;v=", nav.cur, sc ? 1 : 0);
        for (int k = 0; k < P_COUNT && n > 0 && n < len; ++k) n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)pos[k]);
        return (n > 0 && n < len) ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PTA1;", 5)) return;
        int npre = 0, nsc = 0;
        float np[P_COUNT];
        for (int k = 0; k < P_COUNT; ++k) np[k] = pos[k];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);
            else if (!strncmp(p, "sc=", 3)) nsc = parseInt(p + 3, 0, 1, 0);
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
        setSc(nsc != 0);
        nav.restore(npre);
        changed(X_SC);
        ++displayRev;
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliTea));
    return mem ? new (mem) PoliTea() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliTea *>(inst)->~PoliTea();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<PoliTea *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    return static_cast<PoliTea *>(inst)->get(key, buf, buf_len);
}
static void engine_render(void *, int16_t *out_lr, int frames) { std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliTea *>(inst);
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
        s->updateMeters();
        done += n;
    }
}

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, nullptr};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (a preset, a restored project): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliTea *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int k = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << k);
    snprintf(key, len, "%s", extraKey(k));
    return 1;
}
}
