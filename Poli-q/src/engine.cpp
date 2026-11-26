// Poli-q: the mpc_engine() contract (wrapper/engine.h) around the EQ in PoliQ.cpp.
//
// Parameters (vst/params.json, written by generate_skin.py; append only once released):
//   f1..f8      Freq position 0..100 (20 Hz .. 20 kHz, log)       knob text: "35Hz", "150Hz", "1.5kHz", "12kHz"
//   g1..g8      Gain -18..+18 dB                                   knob text: "+3.0dB", "-12dB", "--" (types without gain)
//   q1..q8      Q position 0..100 (0.1 .. 18, log)                 knob text: "0.71", "2.5", "12"
//   t1..t8      Type (option list, kTypeNames)
//   on1..on8    Off/On (the colour circle)
//   bypass      Off/On  "Pass": green = the EQ is working, red = bypassed (dry)
//   qbank       "1-4" / "5-8": which four bands the 16 Q-Links drive
//   analyzer    Off/On  the spectrum behind the grid
//   qf1..4, qg1..4, qq1..4, qt1..4   the Q-Links: Freq/Gain/Q/Type of band c (qbank 1-4) or c+4 (qbank 5-8)
//   dx1..dx8    display: which of kDotPos places a band's dot is shown at
//   dy1..dy8    display: the dot's height (dB), the band's own response at its frequency; +18 = the last frame, a
//               hollow ring on the 0 dB line (the band is off; its height levels are the frames before it)
//   cv01..      display: the EQ curve, one value (dB) per column of the grid
//   sp01..      display: the spectrum of the output, one value per column (0..1)
//   tb_open, tsel, tb_hl, topen1..8, tpick1..14, tb_close   the type browser (tap a band's type name)
//   preset, preset_prev, preset_next, preset_list, browsing, pre_hl, pick1..N   preset selector (shared/preset_nav.h)
// Values the engine changes itself (presets, proxies, the browser) go out through mpc_engine_changed; the display-only
// values through display_rev (the wrapper polls it every 100 ms and reports what moved, without a screen rebuild).
//
// The analyser only runs while the plugin's screen is shown: MPC keeps asking for the knobs' value text
// ("<key>_display") while the screen is up and stops when it is closed. A pause is only trusted once that polling has
// been seen running (3 seconds in a row), so a host that never polls keeps the analyser running rather than freezing it.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" {
#include "engine.h"
}
#include "PoliQ.h"
#include "Learn.h"         // Learn (1.1): moves the preset onto the voice
#include "layout_gen.h"    // display geometry shared with generate_skin.py (column counts, frame counts)
#include "presets_gen.h"   // the factory presets (generate_skin.py PRESETS)
#include "preset_nav.h"    // the preset selector: name, arrows, browser (shared by the Poli plugins)

using namespace pq;

static const int kBlock = 128;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool finiteBits(double v) { uint64_t u; memcpy(&u, &v, 8); return (u & 0x7ff0000000000000ull) != 0x7ff0000000000000ull; }
static float num(const char *s, float keep) {
    if (!s) return keep;
    char *end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || !finiteBits(v)) return keep;
    if (v > 1e9) return 1e9f;
    if (v < -1e9) return -1e9f;
    return (float)v;
}
static bool press(const char *v) { return num(v, 0) >= 0.5f; }
// "f3" -> 2 with prefix "f"; "qf4" -> 3 with prefix "qf" (max = highest number accepted)
static int idx(const char *key, const char *prefix, int max) {
    const size_t n = strlen(prefix);
    if (strncmp(key, prefix, n)) return -1;
    const char *p = key + n;
    if (*p < '1' || *p > '9') return -1;
    char *end = nullptr;
    const long v = strtol(p, &end, 10);
    if (*end || v < 1 || v > max) return -1;
    return (int)v - 1;
}
static int idx2(const char *key, const char *prefix, int max) {   // two digits: "cv07"
    const size_t n = strlen(prefix);
    if (strncmp(key, prefix, n) || strlen(key) != n + 2) return -1;
    const char *p = key + n;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    const int v = (p[0] - '0') * 10 + (p[1] - '0');
    return v >= 1 && v <= max ? v - 1 : -1;
}

// ---- the keys the engine reports itself ---------------------------------------------------------------------------
// band keys: 0..39 (f g q t on per band), proxies 40..55, globals 56..58, browser 59..61
enum { kKF = 0, kKG = 8, kKQ = 16, kKT = 24, kKOn = 32, kKProxy = 40, kKBypass = 56, kKQbank, kKAnalyzer, kKTbOpen,
       kKTsel, kKTbHl, kKLearn, kKVoice, kKDsel, kKDthr, kKDrng, kKDatt, kKDrel, kNumKeys };
static const char *const kGlobalKeys[] = {"bypass", "qbank", "analyzer", "tb_open", "tsel", "tb_hl", "learn", "voice", "dsel",
                                          "dthr", "drng", "datt", "drel"};

static float logPosTo(float pos, float lo, float hi) { return lo * powf(hi / lo, clampf(pos, 0, 100) * 0.01f); }
static float toLogPos(float v, float lo, float hi) { return clampf(100.0f * logf(v / lo) / logf(hi / lo), 0, 100); }

struct Poliq {
    Eq eq;
    Analyzer an;
    float fpos[kBands], gain[kBands], qpos[kBands];
    int type[kBands];
    bool on[kBands];
    bool bypass = false;
    int qbank = 0;             // 0: Q-Links on bands 1-4, 1: bands 5-8
    bool analyzer = true;
    int tb = -1;               // the band whose type browser is open, -1: closed
    PresetNav nav{kNumPresets};
    bool dirty[kNumKeys] = {};
    // 1.1: dynamics (per band, in eq) shown for one band at a time; Learn
    int dsel = 0;
    Learner learner;
    bool learning = false;
    char voice[64] = "";
    float lastDyn[kBands] = {};
    int64_t lastDynCheck = 0;
    // display
    float cv[kCurveCols] = {}, dy[kBands] = {};
    int dx[kBands] = {};
    bool curveDirty = true;
    unsigned rev = 0;
    uint32_t sig = 0;
    // screen-shown detection (the analyser's pause)
    int64_t clock = 0;                // samples processed
    int64_t lastDisp = -(1 << 30), lastHost = -(1 << 30);
    int64_t dispSec = -10;
    int dispRun = 0;
    bool pollSeen = false;            // MPC's value-text polling has been seen running
    // block FIFO for hosts that don't send whole 128-frame blocks (MPC always does: then nothing is added)
    int16_t qin[2 * kBlock], qout[2 * kBlock];
    int qpos_ = 0, opos = kBlock;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    void init() {
        eq.init();
        an.init(kSpecCols);
        memset(qout, 0, sizeof qout);
        applyPreset(0);
        for (bool &d : dirty) d = false;
        nav.dirty = 0;
    }

    int bandOf(int col) const { return col + 4 * qbank; }
    // which: 0 f, 1 g, 2 q, 3 t, 4 on. src: 0 the engine (report both), 1 the band's own key (report the Q-Link proxy
    // only: the host already knows what it wrote), 2 a proxy (report the band's key only)
    void markBand(int b, int which, int src = 0) {
        if (src != 1) dirty[b + 8 * which] = true;
        const int c = b - 4 * qbank;
        if (src != 2 && c >= 0 && c < 4 && which < 4) dirty[kKProxy + 4 * which + c] = true;
        curveDirty = true;
    }
    void markAllProxies() { for (int i = 0; i < 16; ++i) dirty[kKProxy + i] = true; }
    void push(int b) {
        BandSettings s;
        s.type = type[b];
        s.on = on[b];
        s.hz = posToHz(fpos[b]);
        s.gain = gain[b];
        s.q = posToQ(qpos[b]);
        eq.setBand(b, s);
        curveDirty = true;
    }

    void applyPreset(int i) {
        const PresetDef &p = kPresets[i];
        for (int b = 0; b < kBands; ++b) {
            const PresetBand &pb = p.band[b];
            type[b] = pb.type;
            on[b] = pb.on != 0;
            fpos[b] = clampf(hzToPos(pb.hz), 0, 100);
            gain[b] = clampf(pb.gain, -kGainMax, kGainMax);
            qpos[b] = clampf(qToPos(pb.q), 0, 100);
            push(b);
            DynSettings ds;
            ds.thrDb = pb.dyn.thr; ds.rangeDb = pb.dyn.range; ds.attMs = pb.dyn.att; ds.relMs = pb.dyn.rel;
            eq.setDynamics(b, ds);
            for (int w = 0; w < 5; ++w) markBand(b, w);
        }
        markAllProxies();
        markDyn();
    }
    void markDyn() { dirty[kKDthr] = dirty[kKDrng] = dirty[kKDatt] = dirty[kKDrel] = true; curveDirty = true; }
    void setDyn(int which, const char *v) {   // 0 thr, 1 range, 2 attack pos, 3 release pos (the selected band)
        DynSettings d = eq.dynamics(dsel);
        switch (which) {
        case 0: d.thrDb = clampf(num(v, d.thrDb), -60, 0); break;
        case 1: d.rangeDb = clampf(num(v, d.rangeDb), -kGainMax, kGainMax); break;
        case 2: d.attMs = logPosTo(num(v, toLogPos(d.attMs, kAttLo, kAttHi)), kAttLo, kAttHi); break;
        default: d.relMs = logPosTo(num(v, toLogPos(d.relMs, kRelLo, kRelHi)), kRelLo, kRelHi); break;
        }
        eq.setDynamics(dsel, d);
        curveDirty = true;
    }

    // ---- Learn ----
    void startLearn() {
        if (learning) return;
        learner.start();
        learning = true;
        snprintf(voice, sizeof voice, "Listening... play, sing or talk");
        dirty[kKLearn] = dirty[kKVoice] = true;
    }
    // stop listening; with enough voice heard, move the current preset onto it (the preset's own values are the start,
    // so Learning again doesn't drift)
    void finishLearn() {
        if (!learning) return;
        learner.stop();
        learning = false;
        dirty[kKLearn] = dirty[kKVoice] = true;
        LearnResult r;
        const char *pn = kPresets[nav.cur].name;   // the preset's family breaks ties between a voice and an instrument
        const int hint = !strncmp(pn, "Vox:", 4) ? kHintVoice : !strncmp(pn, "Drm:", 4) ? kHintDrums :
                         !strncmp(pn, "Bass:", 5) ? kHintBass : !strncmp(pn, "Inst:", 5) ? kHintInstrument : kHintNone;
        learner.analyse(r, hint);
        AdaptBand ab[kBands];
        const PresetDef &p = kPresets[nav.cur];
        for (int b = 0; b < kBands; ++b) {
            const PresetBand &pb = p.band[b];
            ab[b].type = pb.type; ab[b].on = pb.on != 0; ab[b].hz = pb.hz; ab[b].gain = pb.gain; ab[b].q = pb.q;
            ab[b].dyn.thrDb = pb.dyn.thr; ab[b].dyn.rangeDb = pb.dyn.range; ab[b].dyn.attMs = pb.dyn.att;
            ab[b].dyn.relMs = pb.dyn.rel;
        }
        Learner::adapt(ab, r, learner, voice, sizeof voice);
        if (!r.ok) return;
        bool changed = false;
        for (int b = 0; b < kBands; ++b) {
            const PresetBand &pb = p.band[b];
            changed |= ab[b].hz != pb.hz || ab[b].gain != pb.gain || ab[b].dyn.thrDb != pb.dyn.thr ||
                       ab[b].dyn.rangeDb != pb.dyn.range;
            type[b] = ab[b].type;
            on[b] = ab[b].on;
            fpos[b] = clampf(hzToPos(ab[b].hz), 0, 100);
            gain[b] = clampf(ab[b].gain, -kGainMax, kGainMax);
            qpos[b] = clampf(qToPos(ab[b].q), 0, 100);
            push(b);
            eq.setDynamics(b, ab[b].dyn);
            for (int w = 0; w < 5; ++w) markBand(b, w);
        }
        markAllProxies();
        markDyn();
        if (!changed) {   // Init (or a preset Learn has nothing to move): say so
            const size_t n = strlen(voice);
            snprintf(voice + n, sizeof voice - n, "  - pick a preset");
        }
    }

    void setTb(int b) {   // open the type browser for band b (-1: close)
        if (b == tb) return;
        tb = b;
        dirty[kKTbOpen] = dirty[kKTsel] = dirty[kKTbHl] = true;
        if (b >= 0 && nav.browsing) { nav.browsing = false; nav.dirty |= 6; }
    }

    // band value setters (from a knob or a Q-Link proxy)
    // a value the host wrote that was out of range is reported back from both sides (src 0)
    void setF(int b, const char *v, int src) {
        const float raw = num(v, fpos[b]), x = clampf(raw, 0, 100);
        if (x != fpos[b]) { fpos[b] = x; push(b); }
        markBand(b, 0, raw == x ? src : 0);
    }
    void setG(int b, const char *v, int src) {
        const float raw = num(v, gain[b]), x = clampf(raw, -kGainMax, kGainMax);
        if (x != gain[b]) { gain[b] = x; push(b); }
        markBand(b, 1, raw == x ? src : 0);
    }
    void setQ(int b, const char *v, int src) {
        const float raw = num(v, qpos[b]), x = clampf(raw, 0, 100);
        if (x != qpos[b]) { qpos[b] = x; push(b); }
        markBand(b, 2, raw == x ? src : 0);
    }
    void setT(int b, int t, int src) {
        const int c = t < 0 ? 0 : t >= kTypes ? kTypes - 1 : t;
        if (c != type[b]) { type[b] = c; push(b); }
        markBand(b, 3, c == t ? src : 0);
        if (tb == b) dirty[kKTbHl] = true;
    }
    void setOn(int b, bool o) { if (o != on[b]) { on[b] = o; push(b); } markBand(b, 4, 1); }

    void set(const char *key, const char *val) {
        lastHost = clock;
        const bool wasBrowsing = nav.browsing;
        const int pick = nav.set(key, val);   // preset (menu / Q-Link), the arrows, the browser
        if (pick >= 0) { applyPreset(pick); setTb(-1); return; }
        if (pick == PresetNav::HANDLED) {
            if (nav.browsing && !wasBrowsing) setTb(-1);
            return;
        }
        int i;
        if ((i = idx(key, "f", kBands)) >= 0) setF(i, val, 1);
        else if ((i = idx(key, "g", kBands)) >= 0) setG(i, val, 1);
        else if ((i = idx(key, "q", kBands)) >= 0) setQ(i, val, 1);
        else if ((i = idx(key, "t", kBands)) >= 0) setT(i, (int)lrintf(clampf(num(val, (float)type[i]), -1, 99)), 1);
        else if ((i = idx(key, "on", kBands)) >= 0) setOn(i, num(val, on[i] ? 1.0f : 0.0f) >= 0.5f);
        else if ((i = idx(key, "qf", 4)) >= 0) setF(bandOf(i), val, 2);
        else if ((i = idx(key, "qg", 4)) >= 0) setG(bandOf(i), val, 2);
        else if ((i = idx(key, "qq", 4)) >= 0) setQ(bandOf(i), val, 2);
        else if ((i = idx(key, "qt", 4)) >= 0) setT(bandOf(i), (int)lrintf(clampf(num(val, (float)type[bandOf(i)]), -1, 99)), 2);
        else if (!strcmp(key, "bypass")) { bypass = num(val, bypass ? 1.0f : 0.0f) >= 0.5f; eq.setBypass(bypass); dirty[kKBypass] = true; }
        else if (!strcmp(key, "qbank")) {
            const int q = num(val, (float)qbank) >= 0.5f ? 1 : 0;
            if (q != qbank) { qbank = q; markAllProxies(); }
            dirty[kKQbank] = true;
        } else if (!strcmp(key, "analyzer")) {
            const bool a = num(val, analyzer ? 1.0f : 0.0f) >= 0.5f;
            if (a != analyzer) {
                analyzer = a;
                an.clear();   // off: the columns drop to empty, so the next show starts clean
            }
            dirty[kKAnalyzer] = true;
        } else if ((i = idx(key, "topen", kBands)) >= 0) { if (press(val)) setTb(tb == i ? -1 : i); }
        else if ((i = idx(key, "tpick", kTypes)) >= 0) {
            if (press(val) && tb >= 0) { setT(tb, i, 0); setTb(-1); }
        } else if (!strcmp(key, "tb_close")) { if (press(val)) setTb(-1); }
        else if (!strcmp(key, "state") && val) load(val);
        else if (!strcmp(key, "learn")) {
            const bool want = num(val, learning ? 1.0f : 0.0f) >= 0.5f;
            if (want && !learning) startLearn();
            else if (!want && learning) finishLearn();
            dirty[kKLearn] = true;
        } else if (!strcmp(key, "dsel")) {
            dsel = (int)clampf(lrintf(num(val, (float)dsel)), 0, kBands - 1);
            dirty[kKDsel] = true;
            markDyn();
        } else if (!strcmp(key, "dsel_prev") || !strcmp(key, "dsel_next")) {
            if (press(val)) { dsel = (dsel + (key[5] == 'n' ? 1 : kBands - 1)) % kBands; dirty[kKDsel] = true; markDyn(); }
        } else if (!strcmp(key, "dthr")) setDyn(0, val);
        else if (!strcmp(key, "drng")) setDyn(1, val);
        else if (!strcmp(key, "datt")) setDyn(2, val);
        else if (!strcmp(key, "drel")) setDyn(3, val);
        // display-only keys (dx, dy, cv, sp, tb_open, tsel, tb_hl) are the engine's: host writes are ignored
    }

    // ---- state ----
    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "PQ1;byp=%d;qb=%d;an=%d;pre=%d;b=", bypass ? 1 : 0, qbank, analyzer ? 1 : 0, nav.cur);
        for (int b = 0; b < kBands && n > 0 && n < len; ++b)
            n += snprintf(buf + n, len - n, "%s%d,%d,%.4f,%.4f,%.4f", b ? "/" : "", type[b], on[b] ? 1 : 0, (double)fpos[b],
                          (double)gain[b], (double)qpos[b]);
        // 1.1: dynamics per band (thr,range,attack,release), the band shown, Learn's result
        if (n > 0 && n < len) n += snprintf(buf + n, len - n, ";ds=%d;dyn=", dsel);
        for (int b = 0; b < kBands && n > 0 && n < len; ++b) {
            const DynSettings &d = eq.dynamics(b);
            n += snprintf(buf + n, len - n, "%s%.2f,%.2f,%.2f,%.1f", b ? "/" : "", (double)d.thrDb, (double)d.rangeDb,
                          (double)d.attMs, (double)d.relMs);
        }
        if (n > 0 && n < len && voice[0] && !learning) {
            n += snprintf(buf + n, len - n, ";vo=");
            for (const char *c = voice; *c && n > 0 && n < len - 1; ++c) buf[n++] = *c == ';' ? ',' : *c;
            if (n < len) buf[n] = 0;
        }
        return n > 0 && n < len ? n : 0;
    }
    void load(const char *text) {
        if (strncmp(text, "PQ1;", 4)) return;
        int preset = 0;
        if (learning) { learner.stop(); learning = false; }
        voice[0] = 0;
        for (int b = 0; b < kBands; ++b) eq.setDynamics(b, DynSettings());   // a 1.0 state: static bands
        for (const char *p = text + 4; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "byp=", 4)) { bypass = num(p + 4, 0) >= 0.5f; eq.setBypass(bypass); }
            else if (!strncmp(p, "qb=", 3)) qbank = num(p + 3, 0) >= 0.5f ? 1 : 0;
            else if (!strncmp(p, "an=", 3)) analyzer = num(p + 3, 1) >= 0.5f;
            else if (!strncmp(p, "pre=", 4)) preset = (int)clampf(num(p + 4, 0), 0, kNumPresets - 1);
            else if (!strncmp(p, "ds=", 3)) dsel = (int)clampf(num(p + 3, 0), 0, kBands - 1);
            else if (!strncmp(p, "vo=", 3)) {
                size_t k = 0;
                for (const char *c = p + 3; *c && *c != ';' && k + 1 < sizeof voice; ++c)
                    voice[k++] = (*c >= 0x20 && *c < 0x7f) ? *c : ' ';
                voice[k] = 0;
            } else if (!strncmp(p, "dyn=", 4)) {
                const char *q = p + 4;
                for (int b = 0; b < kBands && q && *q && *q != ';'; ++b) {
                    const DynSettings &o = eq.dynamics(b);
                    float v[4] = {o.thrDb, o.rangeDb, o.attMs, o.relMs};
                    for (int k = 0; k < 4 && q && *q && *q != ';'; ++k) {
                        v[k] = num(q, v[k]);
                        q = strpbrk(q, ",/;");
                        if (q && *q == ',' && k < 3) ++q;
                        else break;
                    }
                    DynSettings d;
                    d.thrDb = v[0]; d.rangeDb = v[1]; d.attMs = v[2]; d.relMs = v[3];
                    eq.setDynamics(b, d);   // clamps
                    if (q && *q == '/') ++q;
                    else break;
                }
            }
            else if (!strncmp(p, "b=", 2)) {
                const char *q = p + 2;
                for (int b = 0; b < kBands && q && *q && *q != ';'; ++b) {
                    float v[5] = {(float)type[b], on[b] ? 1.0f : 0.0f, fpos[b], gain[b], qpos[b]};
                    for (int k = 0; k < 5 && q && *q && *q != ';'; ++k) {
                        v[k] = num(q, v[k]);
                        q = strpbrk(q, ",/;");
                        if (q && *q == ',' && k < 4) ++q;
                        else break;
                    }
                    type[b] = (int)clampf(lrintf(v[0]), 0, kTypes - 1);
                    on[b] = v[1] >= 0.5f;
                    fpos[b] = clampf(v[2], 0, 100);
                    gain[b] = clampf(v[3], -kGainMax, kGainMax);
                    qpos[b] = clampf(v[4], 0, 100);
                    push(b);
                    if (q && *q == '/') ++q;
                    else break;
                }
            }
        }
        nav.restore(preset);
        setTb(-1);
        for (int k = 0; k < kNumKeys; ++k) dirty[k] = true;
        curveDirty = true;
    }

    // ---- display ----
    static int quant(float v, float lo, float hi, int frames) {
        return (int)lrintf(clampf((v - lo) / (hi - lo), 0, 1) * (frames - 1));
    }
    float cvQ(int j) const { return -kGainMax + quant(cv[j], -kGainMax, kGainMax, kCurveFrames) * (2 * kGainMax) / (kCurveFrames - 1); }
    // the dot's frame: 0..kDotFrames-2 = its height (-18..+18 dB), the last frame = a hollow ring (the band is off)
    int dotFrame(int b) const { return on[b] ? quant(dy[b], -kGainMax, kGainMax, kDotFrames - 1) : kDotFrames - 1; }
    float dyQ(int b) const { return -kGainMax + dotFrame(b) * (2 * kGainMax) / (kDotFrames - 1); }
    int specQ(int j) const { return quant(an.level(j), 0, 1, kSpecFrames); }

    void computeCurve() {
        Design d[kBands];
        for (int b = 0; b < kBands; ++b) {
            if (on[b]) design(type[b], posToHz(fpos[b]), gain[b] + eq.dynGain(b), posToQ(qpos[b]), d[b]);
            else d[b].n = 0, d[b].post = 1;
        }
        for (int j = 0; j < kCurveCols; ++j) {
            const float hz = kFLo * powf(1000.0f, (j + 0.5f) / kCurveCols);
            float s = 0;
            for (int b = 0; b < kBands; ++b) if (on[b]) s += responseDb(d[b], hz);
            cv[j] = clampf(s, -kGainMax, kGainMax);
        }
        for (int b = 0; b < kBands; ++b) {
            dx[b] = 1 + (int)lrintf(fpos[b] / 100.0f * (kDotPos - 1));   // 0 ("-", hidden) is not used now
            const float own = type[b] == kNotch || type[b] == kTilt ? 0.0f : responseDb(d[b], posToHz(fpos[b]));
            dy[b] = clampf(own, -kGainMax, kGainMax);
        }
        curveDirty = false;
    }

    bool screenShown() const {
        if (!pollSeen) return true;
        return clock - lastDisp < (int64_t)(2.5 * kFs) || clock - lastHost < (int64_t)(5 * kFs);
    }
    void sawDisplayCall() {
        lastDisp = clock;
        const int64_t sec = clock / (int64_t)kFs;
        if (sec == dispSec) return;
        dispRun = sec == dispSec + 1 ? dispRun + 1 : 1;
        dispSec = sec;
        if (dispRun >= 3) pollSeen = true;
    }

    int dactFrame() const { return quant(eq.dynGain(dsel), -kGainMax, kGainMax, kDactFrames); }
    int learnFrame() const { return learning ? quant(learner.progress(), 0, 1, kLearnFrames) : 0; }

    void afterBlock() {
        // dynamic bands move the curve and the dots: redrawn at most every 50 ms, only while the screen is up
        if (clock - lastDynCheck >= 2205 && screenShown()) {
            lastDynCheck = clock;
            for (int b = 0; b < kBands; ++b) {
                const float g = eq.dynGain(b);
                if (fabsf(g - lastDyn[b]) > 0.25f || (g == 0 && lastDyn[b] != 0)) { lastDyn[b] = g; curveDirty = true; }
            }
        }
        if (curveDirty) computeCurve();
        uint32_t h = 2166136261u;
        auto mix = [&h](int v) { h = (h ^ (uint32_t)v) * 16777619u; };
        for (int j = 0; j < kCurveCols; ++j) mix(quant(cv[j], -kGainMax, kGainMax, kCurveFrames));
        for (int b = 0; b < kBands; ++b) { mix(dx[b]); mix(dotFrame(b)); }
        for (int j = 0; j < kSpecCols; ++j) mix(specQ(j));
        mix(dactFrame());
        mix(learnFrame());
        if (h != sig) { sig = h; ++rev; }
    }

    void block(const int16_t *in, int16_t *out) {
        for (int i = 0; i < kBlock; ++i) { inL[i] = in[2 * i] * (1.0f / 32768.0f); inR[i] = in[2 * i + 1] * (1.0f / 32768.0f); }
        eq.process(inL, inR, outL, outR, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            out[2 * i] = (int16_t)lrintf(clampf(outL[i] * 32768.0f, -32768.0f, 32767.0f));
            out[2 * i + 1] = (int16_t)lrintf(clampf(outR[i] * 32768.0f, -32768.0f, 32767.0f));
        }
        clock += kBlock;
        if (learning) {   // Learn listens to the input (before the EQ); nothing runs here otherwise
            learner.push(inL, inR, kBlock);
            if (learner.done()) finishLearn();
        }
        an.setRunning(analyzer && screenShown());
        an.push(outL, outR, kBlock);
        afterBlock();
    }
};

// ---- text ------------------------------------------------------------------------------------------------------------
static int hzShort(char *buf, int len, float hz) {   // inside a knob: "35Hz", "150Hz", "1.5kHz", "12kHz"
    if (hz < 999.5f) return snprintf(buf, len, "%.0fHz", (double)hz);
    if (hz < 9950) return snprintf(buf, len, "%.1fkHz", (double)(hz / 1000));
    return snprintf(buf, len, "%.0fkHz", (double)(hz / 1000));
}
static int hzLong(char *buf, int len, float hz) {
    if (hz < 1000) return snprintf(buf, len, "%.0f Hz", (double)hz);
    return snprintf(buf, len, "%.2f kHz", (double)(hz / 1000));
}
static int gainShort(char *buf, int len, float g) {
    const float r = roundf(g * 10) / 10;
    if (r == 0) return snprintf(buf, len, "0.0");
    return snprintf(buf, len, fabsf(r) < 9.95f ? "%+.1f" : "%+.0f", (double)r);
}
static int qShort(char *buf, int len, float q) {
    if (q < 0.995f) return snprintf(buf, len, "%.2f", (double)q);
    if (q < 9.95f) return snprintf(buf, len, "%.1f", (double)q);
    return snprintf(buf, len, "%.0f", (double)q);
}

static int msText(char *buf, int len, float ms) {
    if (ms < 9.95f) return snprintf(buf, len, "%.1fms", (double)ms);
    if (ms < 999.5f) return snprintf(buf, len, "%.0fms", (double)ms);
    return snprintf(buf, len, "%.1fs", (double)(ms / 1000));
}

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(Poliq));
    if (!mem) return nullptr;
    Poliq *p = new (mem) Poliq();
    p->init();
    return p;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<Poliq *>(inst)->~Poliq();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) {
    if (inst && key) static_cast<Poliq *>(inst)->set(key, val);
}

static int bandText(Poliq *s, int b, char which, bool full, char *buf, int len) {
    switch (which) {
    case 'f': return full ? hzLong(buf, len, posToHz(s->fpos[b])) : hzShort(buf, len, posToHz(s->fpos[b]));
    case 'g':
        if (!typeHasGain(s->type[b])) return snprintf(buf, len, "--");
        {
            const int n = gainShort(buf, len, s->gain[b]);
            return n + snprintf(buf + n, len - n, full ? " dB" : "dB");
        }
    case 'q':
        if (!typeHasQ(s->type[b])) return snprintf(buf, len, "--");
        return qShort(buf, len, posToQ(s->qpos[b]));
    }
    return 0;
}

static int engine_get_param(void *inst, const char *key, char *buf, int len) {
    auto *s = static_cast<Poliq *>(inst);
    if (!s || !key || !buf || len <= 0) return 0;
    int i;
    const size_t kl = strlen(key);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        s->sawDisplayCall();
        char k[16];
        if (kl - 8 >= sizeof k) return 0;
        memcpy(k, key, kl - 8);
        k[kl - 8] = 0;
        if ((i = idx(k, "f", kBands)) >= 0) return bandText(s, i, 'f', false, buf, len);
        if ((i = idx(k, "g", kBands)) >= 0) return bandText(s, i, 'g', false, buf, len);
        if ((i = idx(k, "q", kBands)) >= 0) return bandText(s, i, 'q', false, buf, len);
        if ((i = idx(k, "qf", 4)) >= 0) return bandText(s, s->bandOf(i), 'f', true, buf, len);
        if ((i = idx(k, "qg", 4)) >= 0) return bandText(s, s->bandOf(i), 'g', true, buf, len);
        if ((i = idx(k, "qq", 4)) >= 0) return bandText(s, s->bandOf(i), 'q', true, buf, len);
        const DynSettings &d = s->eq.dynamics(s->dsel);
        if (!strcmp(k, "dthr")) return snprintf(buf, len, "%.0fdB", (double)d.thrDb);
        if (!strcmp(k, "drng")) {
            if (d.rangeDb == 0) return snprintf(buf, len, "Off");
            const int n = gainShort(buf, len, d.rangeDb);
            return n + snprintf(buf + n, len - n, "dB");
        }
        if (!strcmp(k, "datt")) return msText(buf, len, d.attMs);
        if (!strcmp(k, "drel")) return msText(buf, len, d.relMs);
        return 0;
    }
    if (kl > 5 && !strcmp(key + kl - 5, "_name")) {   // the Q-Links' names follow the bank: "5 Freq"
        static const char *const what[4] = {"Freq", "Gain", "Q", "Type"};
        static const char *const pre[4] = {"qf", "qg", "qq", "qt"};
        char k[16];
        if (kl - 5 >= sizeof k) return 0;
        memcpy(k, key, kl - 5);
        k[kl - 5] = 0;
        for (int w = 0; w < 4; ++w)
            if ((i = idx(k, pre[w], 4)) >= 0) return snprintf(buf, len, "%d %s", s->bandOf(i) + 1, what[w]);
        return 0;
    }
    if ((i = idx(key, "f", kBands)) >= 0) return snprintf(buf, len, "%.4f", (double)s->fpos[i]);
    if ((i = idx(key, "g", kBands)) >= 0) return snprintf(buf, len, "%.4f", (double)s->gain[i]);
    if ((i = idx(key, "q", kBands)) >= 0) return snprintf(buf, len, "%.4f", (double)s->qpos[i]);
    if ((i = idx(key, "t", kBands)) >= 0) return snprintf(buf, len, "%d", s->type[i]);
    if ((i = idx(key, "on", kBands)) >= 0) return snprintf(buf, len, "%d", s->on[i] ? 1 : 0);
    if ((i = idx(key, "qf", 4)) >= 0) return snprintf(buf, len, "%.4f", (double)s->fpos[s->bandOf(i)]);
    if ((i = idx(key, "qg", 4)) >= 0) return snprintf(buf, len, "%.4f", (double)s->gain[s->bandOf(i)]);
    if ((i = idx(key, "qq", 4)) >= 0) return snprintf(buf, len, "%.4f", (double)s->qpos[s->bandOf(i)]);
    if ((i = idx(key, "qt", 4)) >= 0) return snprintf(buf, len, "%d", s->type[s->bandOf(i)]);
    if (!strcmp(key, "bypass")) return snprintf(buf, len, "%d", s->bypass ? 1 : 0);
    if (!strcmp(key, "qbank")) return snprintf(buf, len, "%d", s->qbank);
    if (!strcmp(key, "analyzer")) return snprintf(buf, len, "%d", s->analyzer ? 1 : 0);
    if (s->curveDirty && (key[0] == 'd' || key[0] == 'c')) s->computeCurve();   // read before any audio ran (a reload)
    if ((i = idx(key, "dx", kBands)) >= 0) return snprintf(buf, len, "%d", s->dx[i]);
    if ((i = idx(key, "dy", kBands)) >= 0) return snprintf(buf, len, "%.4f", (double)s->dyQ(i));
    if ((i = idx2(key, "cv", kCurveCols)) >= 0) return snprintf(buf, len, "%.4f", (double)s->cvQ(i));
    if ((i = idx2(key, "sp", kSpecCols)) >= 0) return snprintf(buf, len, "%.4f", (double)s->specQ(i) / (kSpecFrames - 1));
    if (!strcmp(key, "tb_open")) return snprintf(buf, len, "%d", s->tb >= 0 ? 1 : 0);
    if (!strcmp(key, "tsel")) return snprintf(buf, len, "%d", s->tb + 1);
    if (!strcmp(key, "tb_hl")) return snprintf(buf, len, "%d", s->tb >= 0 ? s->type[s->tb] + 1 : 0);
    if (idx(key, "topen", kBands) >= 0 || idx(key, "tpick", kTypes) >= 0 || !strcmp(key, "tb_close"))
        return snprintf(buf, len, "0");
    if (!strcmp(key, "learn")) return snprintf(buf, len, "%d", s->learning ? 1 : 0);
    if (!strcmp(key, "learn_prog")) return snprintf(buf, len, "%.4f", (double)s->learnFrame() / (kLearnFrames - 1));
    if (!strcmp(key, "voice")) return snprintf(buf, len, "%s", s->voice[0] ? s->voice : " ");
    if (!strcmp(key, "dsel")) return snprintf(buf, len, "%d", s->dsel);
    if (!strcmp(key, "dsel_prev") || !strcmp(key, "dsel_next")) return snprintf(buf, len, "0");
    if (!strcmp(key, "dthr")) return snprintf(buf, len, "%.4f", (double)s->eq.dynamics(s->dsel).thrDb);
    if (!strcmp(key, "drng")) return snprintf(buf, len, "%.4f", (double)s->eq.dynamics(s->dsel).rangeDb);
    if (!strcmp(key, "datt")) return snprintf(buf, len, "%.4f", (double)toLogPos(s->eq.dynamics(s->dsel).attMs, kAttLo, kAttHi));
    if (!strcmp(key, "drel")) return snprintf(buf, len, "%.4f", (double)toLogPos(s->eq.dynamics(s->dsel).relMs, kRelLo, kRelHi));
    if (!strcmp(key, "dact")) return snprintf(buf, len, "%.4f", (double)(-kGainMax + s->dactFrame()));
    if (!strcmp(key, "display_rev")) return snprintf(buf, len, "%u", s->rev);
    if (!strcmp(key, "state")) return s->save(buf, len);
    return s->nav.get(key, buf, len);   // preset, browsing, pre_hl, the selector's buttons
}

static void engine_render(void *, int16_t *out_lr, int frames) { memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<Poliq *>(inst);
    if (!s || frames <= 0) return;
    int i = 0;
    while (i < frames) {
        if (s->qpos_ == 0 && s->opos >= kBlock && frames - i >= kBlock) {   // MPC: whole blocks, no added delay
            s->block(in_lr + 2 * i, out_lr + 2 * i);
            i += kBlock;
            continue;
        }
        s->qin[2 * s->qpos_] = in_lr[2 * i];
        s->qin[2 * s->qpos_ + 1] = in_lr[2 * i + 1];
        if (s->opos < kBlock) { out_lr[2 * i] = s->qout[2 * s->opos]; out_lr[2 * i + 1] = s->qout[2 * s->opos + 1]; ++s->opos; }
        else { out_lr[2 * i] = 0; out_lr[2 * i + 1] = 0; }
        if (++s->qpos_ == kBlock) { s->block(s->qin, s->qout); s->qpos_ = 0; s->opos = 0; }
        ++i;
    }
}

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, nullptr};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<Poliq *>(inst);
    if (!s) return 0;
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    static const char *const band[5] = {"f", "g", "q", "t", "on"};
    static const char *const prox[4] = {"qf", "qg", "qq", "qt"};
    for (int k = 0; k < kNumKeys; ++k) {
        if (!s->dirty[k]) continue;
        s->dirty[k] = false;
        if (k < kKProxy) snprintf(key, len, "%s%d", band[k / 8], k % 8 + 1);
        else if (k < kKBypass) snprintf(key, len, "%s%d", prox[(k - kKProxy) / 4], (k - kKProxy) % 4 + 1);
        else snprintf(key, len, "%s", kGlobalKeys[k - kKBypass]);
        return 1;
    }
    return 0;
}
}
