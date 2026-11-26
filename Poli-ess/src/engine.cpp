#include <cstdlib>
#include <strings.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>

extern "C" {
#include "engine.h"
}
#include "DeEsserEngine.h"
#include "presets_gen.h"   // the factory presets (generate_skin.py, from vst/presets.json)
#include "preset_nav.h"    // the preset selector: name, arrows, browser (shared by the Poli plugins)

// Steps the meter is quantised to before it is reported. Matches the frame count of
// skin_assets/meter_strip.png (generate_skin.py METER_FRAMES), so MPC is only told
// about a change when a different frame would actually be shown.
static const int kMeterSteps = 32;
static const int kBlock = 128;

// Attack and Release knobs store their POSITION (0..100) so they turn on a log scale: equal turns = equal ratios of time.
// MPC shows the time itself through "<key>_display" (params.json "dynamic_display").
static const float kAttackMin = 0.1f, kAttackMax = 10.0f;    // ms
static const float kReleaseMin = 5.0f, kReleaseMax = 500.0f; // ms
static float posToMs(float pos, float lo, float hi) { return lo * powf(hi / lo, std::max(0.0f, std::min(100.0f, pos)) / 100.0f); }
static int msText(char *buf, int len, float ms) {
    if (ms < 1.0f) return snprintf(buf, len, "%.2f ms", ms);
    if (ms < 10.0f) return snprintf(buf, len, "%.1f ms", ms);
    return snprintf(buf, len, "%.0f ms", ms);
}

// the parameters a preset (or a restored project) moves; reported back so the knobs follow
static const char *const kKnobKeys[] = {"intensity", "thresh", "freq", "diff", "attack", "release", "knee", "mix"};
static const int kNumKnobKeys = 8;

struct PoliEssState;
static void engine_set_param(void *inst, const char *key, const char *val);

struct PoliEssState {
    DeEsserEngine dsp;
    float intensity = 50.0f;     // %
    float thresh = -20.0f;       // dB
    float freq = 5000.0f;        // Hz
    int   diff = 0;              // 0 = normal, 1 = listen to what is removed
    float attackPos = 45.1545f;  // 0.8 ms (the 1.0.x fixed attack)
    float releasePos = 42.2549f; // 35 ms (the 1.0.x fixed release)
    float knee = 0.0f;           // dB, 0 = hard (as 1.0.x)
    float mix = 100.0f;          // %

    PresetNav nav{kNumPresets};
    unsigned  pending = 0;   // knobs a preset moved, reported back to MPC (mpc_engine_changed): bit = kKnobKeys index

    int      meterStep = 0;  // last quantised meter value (0..kMeterSteps-1)
    unsigned displayRev = 0; // bumped whenever meterStep changes (HAS_DISPLAY_REV)

    float inL[kBlock];
    float inR[kBlock];
    float outL[kBlock];
    float outR[kBlock];

    PoliEssState() {
        dsp.init(44100.0f);
        updateParams();
    }

    void updateParams() {
        DeEsserSettings st;
        st.threshDb = thresh;
        st.freqHz = freq;
        st.intensity = intensity / 100.0f;
        st.listen = diff != 0;
        st.attackMs = posToMs(attackPos, kAttackMin, kAttackMax);
        st.releaseMs = posToMs(releasePos, kReleaseMin, kReleaseMax);
        st.kneeDb = knee;
        st.mix = mix / 100.0f;
        dsp.setParameters(st);
    }

    void updateMeter() {
        int step = (int)lrintf(dsp.getReduction() * (kMeterSteps - 1));
        step = std::max(0, std::min(kMeterSteps - 1, step));
        if (step != meterStep) {
            meterStep = step;
            ++displayRev;
        }
    }
};

static void *engine_create(const char *data_dir) {
    (void)data_dir;
    return new PoliEssState();
}

static void engine_destroy(void *inst) {
    delete static_cast<PoliEssState*>(inst);
}

static void engine_midi(void *inst, const uint8_t *msg, int len) {
    (void)inst; (void)msg; (void)len;
}

// Parse a host value: non-numbers and non-finite values are rejected (keep the old value), the rest clamped to range.
static bool parse_clamped(const char *val, float lo, float hi, float *out) {
    if (!val) return false;
    char *end = nullptr;
    double v = strtod(val, &end);
    if (end == val || !std::isfinite(v)) return false;
    *out = (float)std::max<double>(lo, std::min<double>(hi, v));
    return true;
}

static void apply_preset(PoliEssState *s, int idx) {
    const PresetDef &p = kPresets[idx];
    for (int i = 0; i < p.n; ++i) engine_set_param(s, p.values[i].key, p.values[i].value);
    s->pending = (1u << kNumKnobKeys) - 1;
}

// Saved state (1.0.6): every value plus the preset's index (its name in the PRESET menu and on screen). A project
// restore sets the values as saved and only names the preset, so tweaks made after picking it are kept.
static int save_state(const PoliEssState *s, char *buf, int len) {
    int n = snprintf(buf, len, "PES1;intensity=%.4f;thresh=%.4f;freq=%.3f;diff=%d;attack=%.4f;release=%.4f;knee=%.4f;"
                     "mix=%.4f;preset=%d", s->intensity, s->thresh, s->freq, s->diff, s->attackPos, s->releasePos, s->knee,
                     s->mix, s->nav.cur);
    return n > 0 && n < len ? n : 0;
}
static void load_state(PoliEssState *s, const char *text) {
    if (!text || strncmp(text, "PES1;", 5)) return;
    char key[24], val[40];
    int preset = 0;
    for (const char *p = text + 5; p && *p;) {
        const char *eq = strchr(p, '='), *end = strchr(p, ';');
        if (!end) end = p + strlen(p);
        if (eq && eq < end && eq - p < (long)sizeof key && end - eq - 1 < (long)sizeof val) {
            memcpy(key, p, eq - p); key[eq - p] = 0;
            memcpy(val, eq + 1, end - eq - 1); val[end - eq - 1] = 0;
            if (!strcmp(key, "preset")) { double d; if (PresetNav::number(val, d)) preset = (int)lround(d); }
            else if (strcmp(key, "state")) engine_set_param(s, key, val);   // the knobs, validated as any host write
        }
        p = *end ? end + 1 : nullptr;
    }
    s->nav.restore(preset);
    s->pending = (1u << kNumKnobKeys) - 1;
}

static void engine_set_param(void *inst, const char *key, const char *val) {
    auto *s = static_cast<PoliEssState*>(inst);
    float v;
    if (!strcmp(key, "state")) { load_state(s, val); return; }
    int pick = s->nav.set(key, val);
    if (pick >= 0) { apply_preset(s, pick); return; }
    if (pick == PresetNav::HANDLED) return;
    if (!strcmp(key, "intensity")) { if (!parse_clamped(val, 0.0f, 100.0f, &v)) return; s->intensity = v; }
    else if (!strcmp(key, "thresh")) { if (!parse_clamped(val, -40.0f, 0.0f, &v)) return; s->thresh = v; }
    else if (!strcmp(key, "freq")) { if (!parse_clamped(val, 2000.0f, 10000.0f, &v)) return; s->freq = v; }
    else if (!strcmp(key, "attack")) { if (!parse_clamped(val, 0.0f, 100.0f, &v)) return; s->attackPos = v; }
    else if (!strcmp(key, "release")) { if (!parse_clamped(val, 0.0f, 100.0f, &v)) return; s->releasePos = v; }
    else if (!strcmp(key, "knee")) { if (!parse_clamped(val, 0.0f, 12.0f, &v)) return; s->knee = v; }
    else if (!strcmp(key, "mix")) { if (!parse_clamped(val, 0.0f, 100.0f, &v)) return; s->mix = v; }
    else if (!strcmp(key, "diff")) {            // option index "0"/"1" from the wrapper (or a label)
        if (!val) return;
        if (!strcasecmp(val, "on")) s->diff = 1;
        else if (!strcasecmp(val, "off")) s->diff = 0;
        else if (parse_clamped(val, 0.0f, 1.0f, &v)) s->diff = v >= 0.5f ? 1 : 0;
        else return;
    }
    else return;   // "meter" is engine-driven: host writes are ignored
    s->updateParams();
}

// Full precision: the wrapper reads the knob position back from this text (MPC rounds the value it shows itself),
// so rounding here would snap small Q-Link / data-wheel moves back to where they started.
static int engine_get_param(void *inst, const char *key, char *buf, int buf_len) {
    auto *s = static_cast<PoliEssState*>(inst);
    if (!strcmp(key, "intensity")) return snprintf(buf, buf_len, "%.4f", s->intensity);
    if (!strcmp(key, "thresh")) return snprintf(buf, buf_len, "%.4f", s->thresh);
    if (!strcmp(key, "freq")) return snprintf(buf, buf_len, "%.3f", s->freq);
    if (!strcmp(key, "diff")) return snprintf(buf, buf_len, "%d", s->diff);
    if (!strcmp(key, "attack")) return snprintf(buf, buf_len, "%.4f", s->attackPos);
    if (!strcmp(key, "release")) return snprintf(buf, buf_len, "%.4f", s->releasePos);
    if (!strcmp(key, "knee")) return snprintf(buf, buf_len, "%.4f", s->knee);
    if (!strcmp(key, "mix")) return snprintf(buf, buf_len, "%.4f", s->mix);
    if (!strcmp(key, "attack_display")) return msText(buf, buf_len, posToMs(s->attackPos, kAttackMin, kAttackMax));
    if (!strcmp(key, "release_display")) return msText(buf, buf_len, posToMs(s->releasePos, kReleaseMin, kReleaseMax));
    if (!strcmp(key, "meter"))
        return snprintf(buf, buf_len, "%.6f", (double)s->meterStep / (kMeterSteps - 1));
    if (!strcmp(key, "display_rev")) return snprintf(buf, buf_len, "%u", s->displayRev);
    if (!strcmp(key, "state")) return save_state(s, buf, buf_len);
    return s->nav.get(key, buf, buf_len);
}

static void engine_render(void *inst, int16_t *out_lr, int frames) {
    (void)inst;
    std::memset(out_lr, 0, sizeof(int16_t) * 2 * frames);
}

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<PoliEssState*>(inst);

    // The contract is 128-frame blocks; chunk anyway so a larger block is never left unwritten.
    for (int done = 0; done < frames; ) {
        int n = std::min(frames - done, kBlock);
        const int16_t *in = in_lr + 2 * done;
        int16_t *out = out_lr + 2 * done;

        for (int i = 0; i < n; ++i) {
            s->inL[i] = in[2 * i] / 32768.0f;
            s->inR[i] = in[2 * i + 1] / 32768.0f;
        }

        float* inBuffers[2] = { s->inL, s->inR };
        float* outBuffers[2] = { s->outL, s->outR };
        s->dsp.process(inBuffers, outBuffers, n);

        for (int i = 0; i < n; ++i) {
            int32_t sL = static_cast<int32_t>(s->outL[i] * 32767.0f);
            int32_t sR = static_cast<int32_t>(s->outR[i] * 32767.0f);
            out[2 * i]     = static_cast<int16_t>(sL > 32767 ? 32767 : (sL < -32768 ? -32768 : sL));
            out[2 * i + 1] = static_cast<int16_t>(sR > 32767 ? 32767 : (sR < -32768 ? -32768 : sR));
        }
        done += n;
    }
    s->updateMeter();
}

static const mpc_engine_t ENGINE = {
    engine_create,
    engine_destroy,
    engine_midi,
    engine_set_param,
    engine_get_param,
    engine_render,
    engine_process,
    nullptr
};

extern "C" {
    const mpc_engine_t *mpc_engine(void) {
        return &ENGINE;
    }

    // values the engine moved itself (a preset from the arrows or the browser, a restored project, the browser opening):
    // the wrapper reports them to MPC, so the knobs, the name and the browser follow
    int mpc_engine_changed(void *inst, char *key, int len) {
        auto *s = static_cast<PoliEssState*>(inst);
        if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
        if (!s->pending) return 0;
        int i = __builtin_ctz(s->pending);
        s->pending &= ~(1u << i);
        snprintf(key, len, "%s", kKnobKeys[i]);
        return 1;
    }
}
