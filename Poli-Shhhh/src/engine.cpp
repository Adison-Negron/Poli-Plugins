// Shhhhh: the mpc_engine() contract (wrapper/engine.h) around ShhDSP.
//
// Parameters (vst/params.json, in this order; append only):
//   threshold  -30..+30 dB   moves the whole threshold curve (all six nodes) up/down
//   reduction    0..100 dB   how far noise is turned down
//   learn      Off/On        On = listening (up to 3 s, then the engine turns it Off); Off early = use what it heard
//   bypass     Off/On        "Pass": green = passing the input untouched (delay-matched)
//   diff       Off/On        "Diff": green = hear only what is removed. Ignored (and greyed on screen) while Pass is on
//   n1..n6     -120..0 dB    the nodes: where each one sits on the grid. The engine moves them (Learn, Threshold);
//                            dragging one sets that node's own offset
//   n1x..n6x   P1..P5        display only: each node's horizontal position inside its segment (chosen by Learn)
//   learn_prog 0..1          display only: the Learn button's progress bar
//   sp01..sp32 0..1          display only: the input spectrum behind the grid (32 log columns, 20 Hz - 20 kHz)
//   output     -24..+24 dB   output volume of the processed sound (Pass stays untouched)
// Display-only values change through display_rev (the wrapper polls it every 100 ms and reports what moved, with no
// screen rebuild). Nodes and Learn, which the user also touches, are reported through mpc_engine_changed too, so Q-Link
// nudges start from the right place. Saved state: "SHH1;thr=..;red=..;byp=..;dif=..;lrn=..;out=..;off=..;pos=..;prof=..".
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <new>

extern "C" {
#include "engine.h"
}
#include "ShhDSP.h"
#include "presets_gen.h"   // the factory presets (generate_skin.py PRESETS)
#include "preset_nav.h"    // the preset selector: name, arrows, browser (shared by the Poli plugins)

using namespace shh;

static const int kBlock = 128;
static const int kSpecFrames = 37;   // spectrum column filmstrip frames (generate_skin.py SPEC_FRAMES)
static const int kProgFrames = 16;   // Learn progress bar frames (generate_skin.py PROG_FRAMES)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool finiteBits(float v) { uint32_t u; memcpy(&u, &v, 4); return (u & 0x7f800000u) != 0x7f800000u; }
// a number from MPC's text, or `keep` when it isn't one ("nan", "inf", "1e999", garbage)
static float num(const char *s, float keep) {
    if (!s) return keep;
    char *end = nullptr;
    const float v = (float)strtod(s, &end);
    if (end == s) return keep;
    return finiteBits(v) ? v : keep;
}
static bool on(const char *s, bool keep) { return num(s, keep ? 1.0f : 0.0f) >= 0.5f; }
static int nodeIndex(const char *key, const char *suffix) {   // "n3" -> 2, "n3x" -> 2 (suffix "x")
    if (key[0] != 'n' || key[1] < '1' || key[1] > '0' + kNodes) return -1;
    return strcmp(key + 2, suffix) ? -1 : key[1] - '1';
}

struct Shh {
    ShhDSP dsp;
    float thr = 0, red = 12, out = 0;
    bool learnWas = false;
    float lastProg = 0;
    unsigned changed = 0;          // bits 0..5: nodes, bit 6: learn, bits 7..11: kPresetKeys (a preset moved them)
    PresetNav nav{kNumPresets};
    unsigned rev = 0;
    uint32_t sig = 0;              // hash of what the screen shows from display_rev
    // block FIFO for hosts that don't send whole 128-frame blocks (MPC always does: then nothing is added)
    int16_t qin[2 * kBlock], qout[2 * kBlock];
    int qpos = 0, opos = kBlock;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    void init() {
        dsp.init();
        memset(qout, 0, sizeof qout);
    }
    void nodesChanged() { changed |= (1u << kNodes) - 1; }

    void applyPreset(int idx) {
        const PresetDef &p = kPresets[idx];
        for (int k = 0; k < p.n; ++k) set(p.values[k].key, p.values[k].value);
        changed |= 0x1fu << (kNodes + 1);   // threshold, reduction, output, bypass, diff
    }

    void set(const char *key, const char *val) {
        int i;
        const int pick = nav.set(key, val);   // preset (menu / Q-Link), the arrows, the browser
        if (pick >= 0) { applyPreset(pick); return; }
        if (pick == PresetNav::HANDLED) return;
        if (!strcmp(key, "threshold")) {
            const float t = clampf(num(val, thr), -30, 30);
            if (t != thr) { thr = t; dsp.setThreshold(t); nodesChanged(); }
        } else if (!strcmp(key, "reduction")) { red = clampf(num(val, red), 0, 100); dsp.setReduction(red); }
        else if (!strcmp(key, "output")) { out = clampf(num(val, out), -24, 24); dsp.setOutput(out); }
        else if (!strcmp(key, "learn")) {
            const bool want = on(val, dsp.learning());
            if (want && !dsp.learning()) dsp.startLearn();
            else if (!want && dsp.learning()) { dsp.stopLearn(); nodesChanged(); }
            learnWas = dsp.learning();
        } else if (!strcmp(key, "bypass")) dsp.setBypass(on(val, dsp.bypass()));
        else if (!strcmp(key, "diff")) dsp.setDiff(on(val, dsp.diff()));
        else if ((i = nodeIndex(key, "")) >= 0) {
            const float cur = dsp.nodeLevel(i), v = num(val, cur);
            if (fabsf(v - cur) > 1e-4f) dsp.setNodeLevel(i, v);
            if (fabsf(dsp.nodeLevel(i) - v) > 1e-3f) changed |= 1u << i;   // clamped or refused (learning): say so
        } else if ((i = nodeIndex(key, "x")) >= 0) {
            dsp.setNodePos(i, (int)lrintf(clampf(num(val, (float)dsp.nodePos(i)), 0, kNodePos - 1)));
        } else if (!strcmp(key, "state") && val) load(val);
    }

    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "SHH1;thr=%.3f;red=%.3f;byp=%d;dif=%d;lrn=%d;out=%.3f;off=", (double)thr, (double)red,
                         dsp.bypass() ? 1 : 0, dsp.diff() ? 1 : 0, dsp.profileLearned() ? 1 : 0, (double)out);
        for (int i = 0; i < kNodes && n > 0 && n < len; ++i)
            n += snprintf(buf + n, len - n, i ? ",%.2f" : "%.2f", (double)dsp.nodeOffset(i));
        if (n > 0 && n < len) n += snprintf(buf + n, len - n, ";pre=%d", nav.cur);   // the preset's name (1.0.3)
        if (n > 0 && n < len) n += snprintf(buf + n, len - n, ";pos=");
        for (int i = 0; i < kNodes && n > 0 && n < len; ++i) n += snprintf(buf + n, len - n, "%d", dsp.nodePos(i));
        if (dsp.profileLearned()) {
            if (n > 0 && n < len) n += snprintf(buf + n, len - n, ";prof=");
            for (int k = 0; k < kBins && n > 0 && n < len; ++k)
                n += snprintf(buf + n, len - n, k ? ",%.2f" : "%.2f", (double)dsp.profileDb(k));
        }
        return n > 0 && n < len ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "SHH1;", 5)) return;
        bool learned = false;
        int preset = 0;   // a 1.0.2 state has none: Init
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "thr=", 4)) { thr = clampf(num(p + 4, thr), -30, 30); dsp.setThreshold(thr); }
            else if (!strncmp(p, "red=", 4)) { red = clampf(num(p + 4, red), 0, 100); dsp.setReduction(red); }
            else if (!strncmp(p, "out=", 4)) { out = clampf(num(p + 4, out), -24, 24); dsp.setOutput(out); }
            else if (!strncmp(p, "byp=", 4)) dsp.setBypass(num(p + 4, 0) >= 0.5f);
            else if (!strncmp(p, "dif=", 4)) dsp.setDiff(num(p + 4, 0) >= 0.5f);
            else if (!strncmp(p, "lrn=", 4)) learned = num(p + 4, 0) >= 0.5f;
            else if (!strncmp(p, "pre=", 4)) preset = (int)clampf(num(p + 4, 0), 0, kNumPresets - 1);
            else if (!strncmp(p, "off=", 4)) {
                const char *q = p + 4;
                for (int i = 0; i < kNodes && q && *q && *q != ';'; ++i) {
                    dsp.setNodeOffset(i, num(q, 0));
                    q = strpbrk(q, ",;");
                    if (q && *q == ',') ++q; else break;
                }
            } else if (!strncmp(p, "pos=", 4)) {
                for (int i = 0; i < kNodes && p[4 + i] >= '0' && p[4 + i] < '0' + kNodePos; ++i) dsp.setNodePos(i, p[4 + i] - '0');
            } else if (!strncmp(p, "prof=", 5)) {
                const char *q = p + 5;
                int k = 0;
                for (; k < kBins && q && *q && *q != ';'; ++k) {
                    dsp.setProfileDb(k, num(q, -60));
                    q = strpbrk(q, ",;");
                    if (q && *q == ',') ++q; else { ++k; break; }
                }
                if (k == kBins) dsp.setLearned(learned);
            }
        }
        nav.restore(preset);
        nodesChanged();
        changed |= 0x1fu << (kNodes + 1);
    }

    // after each block: did Learn finish by itself, and did anything on the display-only side move?
    void afterBlock() {
        if (learnWas && !dsp.learning()) { changed |= 1u << kNodes; nodesChanged(); }
        const float prog = dsp.learnProgress();
        if (dsp.learning() && prog != lastProg) nodesChanged();   // the nodes follow the live estimate while listening
        lastProg = prog;
        learnWas = dsp.learning();
        uint32_t h = 2166136261u;
        auto mix = [&h](int v) { h = (h ^ (uint32_t)v) * 16777619u; };
        for (int j = 0; j < kBands; ++j) mix(specQ(j));
        mix(progQ());
        for (int i = 0; i < kNodes; ++i) { mix(dsp.nodePos(i)); mix((int)lrintf(dsp.nodeLevel(i) * 10)); }
        mix(dsp.learning());
        if (h != sig) { sig = h; ++rev; }
    }
    int specQ(int j) const { return (int)lrintf(clampf(dsp.band(j), 0, 1) * (kSpecFrames - 1)); }
    int progQ() const { return (int)lrintf(clampf(dsp.learnProgress(), 0, 1) * (kProgFrames - 1)); }

    void block(const int16_t *in, int16_t *out) {
        for (int i = 0; i < kBlock; ++i) { inL[i] = in[2 * i] * (1.0f / 32768.0f); inR[i] = in[2 * i + 1] * (1.0f / 32768.0f); }
        dsp.process(inL, inR, outL, outR);
        for (int i = 0; i < kBlock; ++i) {
            out[2 * i] = (int16_t)lrintf(clampf(outL[i] * 32768.0f, -32768.0f, 32767.0f));
            out[2 * i + 1] = (int16_t)lrintf(clampf(outR[i] * 32768.0f, -32768.0f, 32767.0f));
        }
        afterBlock();
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(Shh));   // malloc + placement new: nothing needed from the C++ runtime
    if (!mem) return nullptr;
    Shh *p = new (mem) Shh();
    p->init();
    return p;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<Shh *>(inst)->~Shh();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) {
    if (inst && key) static_cast<Shh *>(inst)->set(key, val);
}

static int dbText(char *buf, int len, float v, bool sign) {
    const float r = roundf(v * 10) / 10;
    return snprintf(buf, len, sign ? "%+.1f dB" : "%.1f dB", (double)(r == 0 ? 0.0f : r));
}

static int engine_get_param(void *inst, const char *key, char *buf, int len) {
    auto *s = static_cast<Shh *>(inst);
    if (!s || !key || !buf || len <= 0) return 0;
    int i;
    if (!strcmp(key, "threshold")) return snprintf(buf, len, "%.4f", (double)s->thr);
    if (!strcmp(key, "reduction")) return snprintf(buf, len, "%.4f", (double)s->red);
    if (!strcmp(key, "threshold_display")) return dbText(buf, len, s->thr, true);
    if (!strcmp(key, "reduction_display")) return snprintf(buf, len, "%d dB", (int)lrintf(s->red));
    if (!strcmp(key, "output")) return snprintf(buf, len, "%.4f", (double)s->out);
    if (!strcmp(key, "output_display")) return dbText(buf, len, s->out, true);
    if (!strcmp(key, "learn")) return snprintf(buf, len, "%d", s->dsp.learning() ? 1 : 0);
    if (!strcmp(key, "bypass")) return snprintf(buf, len, "%d", s->dsp.bypass() ? 1 : 0);
    if (!strcmp(key, "diff")) return snprintf(buf, len, "%d", s->dsp.diff() ? 1 : 0);
    if ((i = nodeIndex(key, "")) >= 0) return snprintf(buf, len, "%.4f", (double)s->dsp.nodeLevel(i));
    if (!strncmp(key, "n", 1) && strlen(key) == 10 && !strcmp(key + 2, "_display") && (i = key[1] - '1') >= 0 && i < kNodes) {
        buf[0] = 0;   // nothing under the nodes on screen (the grid's frequency labels are there)
        return 1;
    }
    if ((i = nodeIndex(key, "x")) >= 0) return snprintf(buf, len, "%d", s->dsp.nodePos(i));
    if (!strcmp(key, "learn_prog")) return snprintf(buf, len, "%.4f", (double)s->progQ() / (kProgFrames - 1));
    if (!strncmp(key, "sp", 2) && key[2] >= '0' && key[2] <= '9' && key[3] >= '0' && key[3] <= '9' && !key[4]) {
        const int j = (key[2] - '0') * 10 + (key[3] - '0') - 1;
        if (j < 0 || j >= kBands) return 0;
        return snprintf(buf, len, "%.4f", (double)s->specQ(j) / (kSpecFrames - 1));
    }
    if (!strcmp(key, "display_rev")) return snprintf(buf, len, "%u", s->rev);
    if (!strcmp(key, "state")) return s->save(buf, len);
    return s->nav.get(key, buf, len);   // preset, browsing, pre_hl, the selector's buttons
}

static void engine_render(void *, int16_t *out_lr, int frames) { memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<Shh *>(inst);
    if (!s || frames <= 0) return;
    int i = 0;
    while (i < frames) {
        if (s->qpos == 0 && s->opos >= kBlock && frames - i >= kBlock) {   // MPC: whole blocks, no added delay
            s->block(in_lr + 2 * i, out_lr + 2 * i);
            i += kBlock;
            continue;
        }
        // any other block size: collected and processed one block late (as the wrapper itself does)
        s->qin[2 * s->qpos] = in_lr[2 * i];
        s->qin[2 * s->qpos + 1] = in_lr[2 * i + 1];
        if (s->opos < kBlock) { out_lr[2 * i] = s->qout[2 * s->opos]; out_lr[2 * i + 1] = s->qout[2 * s->opos + 1]; ++s->opos; }
        else { out_lr[2 * i] = 0; out_lr[2 * i + 1] = 0; }
        if (++s->qpos == kBlock) { s->block(s->qin, s->qout); s->qpos = 0; s->opos = 0; }
        ++i;
    }
}

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, nullptr};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// nodes the engine moved (Learn, Threshold) and Learn turning itself off: the wrapper tells MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<Shh *>(inst);
    if (!s) return 0;
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->changed) return 0;
    static const char *const kPresetKeys[5] = {"threshold", "reduction", "output", "bypass", "diff"};
    for (int i = 0; i <= kNodes + 5; ++i) {
        if (s->changed & (1u << i)) {
            s->changed &= ~(1u << i);
            if (i == kNodes) snprintf(key, len, "learn");
            else if (i > kNodes) snprintf(key, len, "%s", kPresetKeys[i - kNodes - 1]);
            else snprintf(key, len, "n%d", i + 1);
            return 1;
        }
    }
    s->changed = 0;
    return 0;
}
}
