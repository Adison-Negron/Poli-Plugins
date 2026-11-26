// Poli-Multiband: the mpc_engine() contract (wrapper/engine.h) around MultibandDSP.
//
// The screen shows ONE band at a time. Its knobs are bound to "proxy" parameters (c_* = Comp, u_* = Upwards Comp)
// that always read and write the band selected with "band". Switching bands loads that band's values into the
// proxies and reports them back to MPC (mpc_engine_changed + display_rev), so the knobs, their value text and the
// Q-Links follow the band. All three bands are saved in the "state" chunk, not as separate parameters.
//
// Split = the crossovers, as in Ableton's Multiband Dynamics (two frequencies cut the signal into three bands):
//   Band 1 (low):  20 hz .. X1    its Split knob sets X1 ("Up To")
//   Band 2 (mid):  X1 .. X2       no Split of its own: its range follows Bands 1 and 3 (shown on a still knob)
//   Band 3 (high): X2 .. 20k      its Split knob sets X2 ("From")
// X1 stays at least a factor 1.25 below X2 (a knob pushed past the other edge snaps back). Upward Thresh can never go
// above Comp Thresh: lowering Comp Thresh pushes it down, raising Upward Thresh past it snaps back.
//
// Factory presets (src/presets.h, generated from presets/factory.json): the "preset" parameter is an option list, and
// vst.json "programs": {"param": "preset"} makes the wrapper list it in MPC's PRESET menu. Picking one loads all three
// bands and both crossovers (the band on screen and Pass stay as they are). The picked index is saved in the state, so
// the menu shows the right name after a project reload; knob changes after a pick don't change it.
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
#include "MultibandDSP.h"
#include "presets.h"
#include "preset_nav.h"   // the preset selector: name, arrows, browser (shared by the Poli plugins)

static const int kBlock = 128;
static const int kMeterSteps = 30;          // = generate_skin.py METER_FRAMES (frames in each meter strip)
static const float kMeterFloorDb = -60.0f;  // bottom of the meter (top = 0 dBFS); also the Thresh knobs' range
static const float kXMin = 20.0f, kXMax = 20000.0f, kXGap = 1.25f;

// ---- knob position <-> value (positions are 0..1000 so the knob travel is musical) -------------------------------
static float posToFreq(float p) { return kXMin * powf(1000.0f, p / 1000.0f); }
static float freqToPos(float f) { return 1000.0f * logf(f / kXMin) / logf(1000.0f); }
static float posToAttack(float p) { return 0.1f * powf(1000.0f, p / 1000.0f); }      // 0.1 .. 100 ms
static float attackToPos(float t) { return 1000.0f * logf(t / 0.1f) / logf(1000.0f); }
static float posToRelease(float p) { return 5.0f * powf(400.0f, p / 1000.0f); }      // 5 .. 2000 ms
static float releaseToPos(float t) { return 1000.0f * logf(t / 5.0f) / logf(400.0f); }
static float posToRatio(float p, float top) { float x = p / 1000.0f; return 1.0f + (top - 1.0f) * x * x; }
static float ratioToPos(float r, float top) { return 1000.0f * sqrtf(std::max(0.0f, (r - 1.0f) / (top - 1.0f))); }
static const float kRatioTop = 20.0f, kUpRatioTop = 10.0f;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// finite check on the bits (-ffast-math lets the compiler drop isnan/isfinite): "nan"/"inf" text must never reach
// the DSP, or the plugin goes silent for good
static bool isFiniteBits(float x) {
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
static int parseInt(const char *s, int lo, int hi, int fallback) {   // clamped, no overflow
    float v;
    if (!parseNum(s, v)) return fallback;
    return (int)lrintf(clampf(v, (float)lo, (float)hi));
}

// "%.1f" without a trailing ".0"
static void num1(char *b, int n, float v) {
    snprintf(b, n, "%.1f", (double)v);
    char *dot = strchr(b, '.');
    if (dot && dot[1] == '0' && dot[2] == 0) *dot = 0;
    if (!strcmp(b, "-0")) strcpy(b, "0");
}

static int fmtFreq(char *buf, int len, float f) {
    char n[24];
    if (f < 999.5f) return snprintf(buf, len, "%d hz", (int)lrintf(f));
    num1(n, sizeof n, f / 1000.0f);                 // 1000 -> "1k", 1500 -> "1.5k", 12500 -> "12.5k"
    return snprintf(buf, len, "%sk", n);
}
static int fmtDb(char *buf, int len, float db) { char n[24]; num1(n, sizeof n, db); return snprintf(buf, len, "%s db", n); }
static int fmtMs(char *buf, int len, float ms) {
    char n[24];
    if (ms >= 999.5f) { num1(n, sizeof n, ms / 1000.0f); return snprintf(buf, len, "%s s", n); }
    if (ms < 9.95f) { num1(n, sizeof n, ms); return snprintf(buf, len, "%s ms", n); }
    return snprintf(buf, len, "%d ms", (int)lrintf(ms));
}
static int fmtRatio(char *buf, int len, float r) { char n[24]; num1(n, sizeof n, r); return snprintf(buf, len, "%s:1", n); }

// ---- the keys the engine reports back when it moves them itself ----------------------------------------------------
enum Key { K_BAND, K_C_INPUT, K_C_OUTPUT, K_C_THRESH, K_C_ATTACK, K_C_RELEASE, K_C_KNEE, K_C_RATIO, K_X_FREQ,
           K_U_THRESH, K_U_RATIO, K_BYPASS, K_COUNT };
static const char *const kKeyName[K_COUNT] = {"band", "c_input", "c_output", "c_thresh", "c_attack", "c_release", "c_knee",
                                              "c_ratio", "x_freq", "u_thresh", "u_ratio", "bypass"};
static const uint32_t kProxyMask = ((1u << K_BYPASS) - 1u) & ~1u;   // c_input .. u_ratio

struct PoliMultiband {
    MultibandDSP dsp;
    BandSettings bands[MultibandDSP::kBands];
    float x1 = 500.0f, x2 = 4000.0f;   // crossovers (Hz)
    int band = 0;                      // band on screen (0..2)
    int bypass = 0;
    int preset = 0;                    // factory preset last picked (kPresets)
    PresetNav nav{kNumPresets};        // the selector on screen (its index follows "preset")

    uint32_t pending = 0;              // keys to report (mpc_engine_changed)
    unsigned displayRev = 0;
    float meterIn = 0.0f, meterOut = 0.0f;   // displayed levels (linear, peak with fall)
    int fIn = 0, fOut = 0, fCt = 0, fUt = 0;  // meter frames last reported

    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    PoliMultiband() {
        dsp.init(44100.0f);
        for (int b = 0; b < MultibandDSP::kBands; ++b) push(b);
        dsp.setCrossovers(x1, x2);
        dsp.init(44100.0f);
        updateLines();
    }

    void push(int b) { dsp.setBand(b, bands[b]); }
    void changed(int k) { pending |= 1u << k; }

    static int stepOf(float db) {
        return (int)clampf(lrintf((db - kMeterFloorDb) / -kMeterFloorDb * (kMeterSteps - 1)), 0, kMeterSteps - 1);
    }
    void updateLines() {
        int ct = stepOf(bands[band].threshDb), ut = stepOf(bands[band].upThreshDb);
        if (ct != fCt || ut != fUt) { fCt = ct; fUt = ut; ++displayRev; }
    }

    void selectBand(int b) {
        b = std::max(0, std::min(MultibandDSP::kBands - 1, b));
        if (b != band) { band = b; meterIn = meterOut = 0.0f; }
        pending |= kProxyMask | 1u << K_BAND;
        ++displayRev;   // the wrapper then also re-reads every value (knob positions) on its next poll
        updateLines();
    }

    void setX1(float f) {
        float v = clampf(f, kXMin, x2 / kXGap);
        if (v != f) changed(K_X_FREQ);   // pushed against the other edge: snap the knob back
        x1 = v;
        dsp.setCrossovers(x1, x2);
    }
    void setX2(float f) {
        float v = clampf(f, x1 * kXGap, kXMax);
        if (v != f) changed(K_X_FREQ);
        x2 = v;
        dsp.setCrossovers(x1, x2);
    }

    void applyPreset(int idx) {
        idx = std::max(0, std::min(kNumPresets - 1, idx));
        const Preset &p = kPresets[idx];
        for (int b = 0; b < MultibandDSP::kBands; ++b) {
            const PresetBand &q = p.band[b];
            BandSettings &s = bands[b];
            s.inputDb = clampf(q.input, -24, 24);
            s.outputDb = clampf(q.output, -24, 24);
            s.threshDb = clampf(q.thresh, kMeterFloorDb, 0);
            s.attackMs = clampf(q.attack, 0.1f, 100.0f);
            s.releaseMs = clampf(q.release, 5.0f, 2000.0f);
            s.kneeDb = clampf(q.knee, 0, 24);
            s.ratio = clampf(q.ratio, 1.0f, kRatioTop);
            s.upThreshDb = std::min(clampf(q.upThresh, kMeterFloorDb, 0), s.threshDb);
            s.upRatio = clampf(q.upRatio, 1.0f, kUpRatioTop);
            s.upOutputDb = 0;
            push(b);
        }
        x1 = clampf(p.x1, kXMin, kXMax / kXGap);
        x2 = clampf(p.x2, x1 * kXGap, kXMax);
        dsp.setCrossovers(x1, x2);
        preset = idx;
        nav.cur = idx;
        pending |= kProxyMask;   // every knob on screen moved
        ++displayRev;
        updateLines();
    }

    float splitPos() const { return freqToPos(band == 2 ? x2 : x1); }
    // the band on screen, low edge - high edge: "20 hz - 500 hz", "500 hz - 4k", "4k - 20k"
    int rangeText(char *buf, int len) const {
        char a[16], b[16];
        fmtFreq(a, sizeof a, band == 0 ? kXMin : band == 1 ? x1 : x2);
        fmtFreq(b, sizeof b, band == 0 ? x1 : band == 1 ? x2 : kXMax);
        return snprintf(buf, len, "%s - %s", a, b);
    }

    void set(const char *key, const char *val) {
        if (!strcmp(key, "state")) { load(val); return; }
        const int pick = nav.set(key, val);   // preset (menu / Q-Link), the arrows, the browser
        if (pick >= 0) { applyPreset(pick); return; }
        if (pick == PresetNav::HANDLED) return;
        float v;
        if (!parseNum(val, v)) return;   // empty, garbage, nan, inf: keep the old value
        BandSettings &s = bands[band];
        if (!strcmp(key, "band")) { selectBand((int)lrintf(v)); return; }
        if (!strcmp(key, "sel1") || !strcmp(key, "sel2") || !strcmp(key, "sel3")) {
            if (v >= 0.5f) selectBand(key[3] - '1');
            return;
        }
        if (!strcmp(key, "bypass")) { bypass = v >= 0.5f; dsp.setBypass(bypass); return; }
        if (!strcmp(key, "c_input")) s.inputDb = clampf(v, -24, 24);
        else if (!strcmp(key, "c_output")) s.outputDb = clampf(v, -24, 24);
        else if (!strcmp(key, "c_attack")) s.attackMs = posToAttack(clampf(v, 0, 1000));
        else if (!strcmp(key, "c_release")) s.releaseMs = posToRelease(clampf(v, 0, 1000));
        else if (!strcmp(key, "c_knee")) s.kneeDb = clampf(v, 0, 24);
        else if (!strcmp(key, "c_ratio")) s.ratio = posToRatio(clampf(v, 0, 1000), kRatioTop);
        else if (!strcmp(key, "c_thresh")) {
            s.threshDb = clampf(v, kMeterFloorDb, 0);
            if (s.upThreshDb > s.threshDb) { s.upThreshDb = s.threshDb; changed(K_U_THRESH); }
        } else if (!strcmp(key, "u_ratio")) s.upRatio = posToRatio(clampf(v, 0, 1000), kUpRatioTop);
        else if (!strcmp(key, "u_thresh")) {
            float t = clampf(v, kMeterFloorDb, 0);
            if (t > s.threshDb) { t = s.threshDb; changed(K_U_THRESH); }   // can't go above Comp Thresh
            s.upThreshDb = t;
        } else if (!strcmp(key, "x_freq")) {
            if (band == 0) setX1(posToFreq(clampf(v, 0, 1000)));
            else if (band == 2) setX2(posToFreq(clampf(v, 0, 1000)));
            else changed(K_X_FREQ);   // Band 2 has no Split of its own: it sits between Bands 1 and 3
            return;
        } else return;   // meters and anything unknown: engine-driven / ignored
        push(band);
        updateLines();
    }

    // value in the parameter's own units (what the wrapper turns into a knob position)
    bool value(const char *key, float &out) const {
        const BandSettings &s = bands[band];
        if (!strcmp(key, "c_input")) out = s.inputDb;
        else if (!strcmp(key, "c_output")) out = s.outputDb;
        else if (!strcmp(key, "x_freq")) out = splitPos();
        else if (!strcmp(key, "c_attack")) out = attackToPos(s.attackMs);
        else if (!strcmp(key, "c_release")) out = releaseToPos(s.releaseMs);
        else if (!strcmp(key, "c_knee")) out = s.kneeDb;
        else if (!strcmp(key, "c_ratio")) out = ratioToPos(s.ratio, kRatioTop);
        else if (!strcmp(key, "c_thresh")) out = s.threshDb;
        else if (!strcmp(key, "u_ratio")) out = ratioToPos(s.upRatio, kUpRatioTop);
        else if (!strcmp(key, "u_thresh")) out = s.upThreshDb;
        else return false;
        return true;
    }

    // the knob's value text: "500 hz", "1.2k", "0 db", "3 ms", "4:1"
    int display(const char *key, char *buf, int len) const {
        const BandSettings &s = bands[band];
        if (!strcmp(key, "c_input")) return fmtDb(buf, len, s.inputDb);
        if (!strcmp(key, "c_output")) return fmtDb(buf, len, s.outputDb);
        if (!strcmp(key, "x_freq")) return band == 1 ? rangeText(buf, len) : fmtFreq(buf, len, band == 0 ? x1 : x2);
        if (!strcmp(key, "c_attack")) return fmtMs(buf, len, s.attackMs);
        if (!strcmp(key, "c_release")) return fmtMs(buf, len, s.releaseMs);
        if (!strcmp(key, "c_knee")) return fmtDb(buf, len, s.kneeDb);
        if (!strcmp(key, "c_ratio")) return fmtRatio(buf, len, s.ratio);
        if (!strcmp(key, "c_thresh")) return fmtDb(buf, len, s.threshDb);
        if (!strcmp(key, "u_ratio")) return fmtRatio(buf, len, s.upRatio);
        if (!strcmp(key, "u_thresh")) return fmtDb(buf, len, s.upThreshDb);
        return 0;
    }

    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "PMB1;band=%d;bypass=%d;preset=%d;x1=%.2f;x2=%.2f", band, bypass, preset, (double)x1,
                         (double)x2);
        for (int b = 0; b < MultibandDSP::kBands && n > 0 && n < len; ++b) {
            const BandSettings &s = bands[b];
            n += snprintf(buf + n, len - n, ";b%d=%.3f,%.3f,%.4f,%.3f,%.3f,%.4f,%.3f,%.4f,%.3f,%.3f", b,
                          (double)s.inputDb, (double)s.outputDb, (double)s.attackMs, (double)s.releaseMs,
                          (double)s.kneeDb, (double)s.ratio, (double)s.threshDb, (double)s.upRatio,
                          (double)s.upThreshDb, (double)s.upOutputDb);
        }
        return n < len ? n : 0;
    }

    void load(const char *text) {
        if (strncmp(text, "PMB1;", 5)) return;
        float nx1 = x1, nx2 = x2;
        int nband = band, nbyp = bypass, npre = 0;   // a state from before presets: "Init"
        BandSettings nb[MultibandDSP::kBands];
        for (int b = 0; b < MultibandDSP::kBands; ++b) nb[b] = bands[b];
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "band=", 5)) nband = parseInt(p + 5, 0, 2, nband);
            else if (!strncmp(p, "bypass=", 7)) nbyp = parseInt(p + 7, 0, 1, nbyp);
            else if (!strncmp(p, "preset=", 7)) npre = parseInt(p + 7, 0, kNumPresets - 1, 0);   // the name only: the values follow
            else if (!strncmp(p, "x1=", 3)) parseNum(p + 3, nx1);
            else if (!strncmp(p, "x2=", 3)) parseNum(p + 3, nx2);
            else if (p[0] == 'b' && p[1] >= '0' && p[1] <= '2' && p[2] == '=') {
                float f[10];
                int k = 0;
                const char *q = p + 3;
                for (; k < 10 && q; ++k) {
                    if (!parseNum(q, f[k])) break;   // a broken band keeps its old values
                    const char *c = strchr(q, ','), *e = strchr(q, ';');
                    q = (c && (!e || c < e)) ? c + 1 : nullptr;
                }
                if (k == 10) {
                    BandSettings &s = nb[p[1] - '0'];
                    s.inputDb = clampf(f[0], -24, 24);
                    s.outputDb = clampf(f[1], -24, 24);
                    s.attackMs = clampf(f[2], 0.1f, 100.0f);
                    s.releaseMs = clampf(f[3], 5.0f, 2000.0f);
                    s.kneeDb = clampf(f[4], 0, 24);
                    s.ratio = clampf(f[5], 1.0f, kRatioTop);
                    s.threshDb = clampf(f[6], kMeterFloorDb, 0);
                    s.upRatio = clampf(f[7], 1.0f, kUpRatioTop);
                    s.upThreshDb = std::min(clampf(f[8], kMeterFloorDb, 0), s.threshDb);
                    s.upOutputDb = 0;   // no longer a control (the band's Output does it); kept in the format
                }
            }
        }
        for (int b = 0; b < MultibandDSP::kBands; ++b) { bands[b] = nb[b]; push(b); }
        x1 = clampf(nx1, kXMin, kXMax / kXGap);
        x2 = clampf(nx2, x1 * kXGap, kXMax);
        dsp.setCrossovers(x1, x2);
        bypass = nbyp ? 1 : 0;
        dsp.setBypass(bypass);
        preset = (npre >= 0 && npre < kNumPresets) ? npre : 0;
        nav.restore(preset);
        band = -1;
        selectBand(nband);
        changed(K_BYPASS);
    }

    void updateMeters() {
        float pin = 0, pout = 0;
        for (int b = 0; b < MultibandDSP::kBands; ++b) {
            float i, o;
            dsp.takeMeter(b, i, o);
            if (b == band) { pin = i; pout = o; }
        }
        const float fall = 0.9907f;   // per 128-frame block: about 300 ms to fall by 1/e
        meterIn = std::max(pin, meterIn * fall);
        meterOut = std::max(pout, meterOut * fall);
        auto frame = [](float lin) {
            if (lin < 1e-6f) return 0;
            return stepOf(20.0f * log10f(lin));
        };
        int fi = frame(meterIn), fo = frame(meterOut);
        if (fi != fIn || fo != fOut) { fIn = fi; fOut = fo; ++displayRev; }
    }
};

// malloc + placement new: the engine needs nothing from the C++ runtime library
static void *engine_create(const char *) {
    void *mem = malloc(sizeof(PoliMultiband));
    return mem ? new (mem) PoliMultiband() : nullptr;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<PoliMultiband *>(inst)->~PoliMultiband();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}

static void engine_set_param(void *inst, const char *key, const char *val) {
    static_cast<PoliMultiband *>(inst)->set(key, val);
}

static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    auto *s = static_cast<PoliMultiband *>(inst);
    float v;
    if (s->value(key, v)) return snprintf(buf, buf_len, "%.3f", (double)v);
    const size_t kl = strlen(key);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        char base[32];
        if (kl - 8 >= sizeof base) return 0;
        memcpy(base, key, kl - 8);
        base[kl - 8] = 0;
        return s->display(base, buf, buf_len);
    }
    if (!strcmp(key, "band")) return snprintf(buf, buf_len, "%d", s->band);
    if (!strcmp(key, "bypass")) return snprintf(buf, buf_len, "%d", s->bypass);
    if (!strcmp(key, "sel1") || !strcmp(key, "sel2") || !strcmp(key, "sel3")) return snprintf(buf, buf_len, "0");
    const double d = kMeterSteps - 1;
    if (!strcmp(key, "m_out")) return snprintf(buf, buf_len, "%.4f", s->fOut / d);
    if (!strcmp(key, "m_in")) return snprintf(buf, buf_len, "%.4f", s->fIn / d);
    if (!strcmp(key, "m_ct")) return snprintf(buf, buf_len, "%.4f", s->fCt / d);
    if (!strcmp(key, "m_ut")) return snprintf(buf, buf_len, "%.4f", s->fUt / d);
    if (!strcmp(key, "display_rev")) return snprintf(buf, buf_len, "%u", s->displayRev);
    if (!strcmp(key, "state")) return s->save(buf, buf_len);
    if (!strcmp(key, "range_text")) return s->rangeText(buf, buf_len);
    return s->nav.get(key, buf, buf_len);   // preset, browsing, pre_hl, the selector's buttons
}

static void engine_render(void *, int16_t *out_lr, int frames) {
    std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames);
}

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliMultiband *>(inst);
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
            const float l = s->outL[i] * 32767.0f, r = s->outR[i] * 32767.0f;
            out[2 * i] = (int16_t)lrintf(clampf(l, -32768.0f, 32767.0f));
            out[2 * i + 1] = (int16_t)lrintf(clampf(r, -32768.0f, 32767.0f));
        }
        done += n;
    }
    s->updateMeters();
}

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, nullptr};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (a band switch, a snapped knob): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<PoliMultiband *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    int k = __builtin_ctz(s->pending);
    s->pending &= ~(1u << k);
    snprintf(key, len, "%s", kKeyName[k]);
    return 1;
}
}
