// Poliwow: the mpc_engine() contract (wrapper/engine.h) around WowDSP.
//
// Screen: two pages, switched by the Settings pill: WOW (Mix, Wow) and GRANULES (the room granulator: Size, Density,
// Speed, Variation, Retrace, Split, Time, Length, Amount, Level, the Jump / Reverse and Sync switches). A ? button opens a
// two-page help window; the preset's name sits between two arrows in the top bar; a tap on the name opens the preset browser (wow_nav.h: categories on the left,
// presets on the right; params style_list, browsing, bview, pre_hl, bpage_prev/next, cat1..cat8, pick1..pick12).
// The Q-Links follow the page on screen: 12 slots q1..q12 (4 to a Live III Q-Link page, top knob first).
//   WOW page:    q1 Wow, q2 Mix, q3 Preset
//   GRANULES page: q1 Size, q2 Density, q3 Speed, q4 Variation | q5 Retrace, q6 Split, q7 Time, q8 Length |
//                q9 Amount, q10 Level, q11 Retrace mode (below 50 % = Jump), q12 Sync (below 50 % = off)   (the rows of the screen)
//
// Parameters (vst/params.json, in this order; append only):
//   wow 0..100, mix 0..100, style (the preset, 39 of them; also MPC's PRESET menu), style_prev / style_next (momentary arrows)
//   page (Wow, Granules), tab1, tab2 (momentary: the Settings pill on either page)
//   g_level g_size g_density g_speed g_split g_retrace g_rettime g_retlen g_var   0..100 (the Granules knobs)
//   g_retmode (Jump, Reverse), g_sync (Off, On)
//   q1..q12   the Q-Link slots
//   help_btn, help_next (momentary: the ? button and the help window's arrows), hview (Closed, Help 1, Help 2: engine-owned)
//   cat (the preset's category: engine-owned, shows its name), cat_prev / cat_next (momentary: jump to the previous / next category)
//   g_amount  0..100: extra grains on top of the preset's own cloud
// Picking a preset sets the granulator knobs to what the preset says (they move on screen); after that they are the user's.
// Saved state: "PWW2;wow=..;mix=..;style=..;page=..;gl=..;..". A 1.1 state ("PWW1;wow;mix;style") still loads, its old preset
// number mapped to the preset of the same name, with the granulator neutral.
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <new>

extern "C" {
#include "engine.h"
}
#include "WowDSP.h"
#include "wow_nav.h"

static const int kBlock = 128;
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// A number from MPC's text, or `keep` when it isn't one ("nan", "inf", "1e999"). A NaN let through would stick in the
// filters and silence the plugin. Tested on the bits: -ffast-math allows the compiler to drop isnan()/isfinite().
static bool finiteBits(float v) { uint32_t u; memcpy(&u, &v, 4); return (u & 0x7f800000u) != 0x7f800000u; }
static float num(const char *s, float keep) {
    if (!s) return keep;
    const float v = (float)strtod(s, nullptr);
    return finiteBits(v) ? v : keep;
}
static int styleIndex(float v) { return (int)lrintf(clampf(v, 0, kNumPresets - 1)); }   // clamped before the int conversion

// the 1.1 preset list (state PWW1 stores a number): its names, to find the same preset in the new list
static const char *const kOldNames[16] = {"Heaven Lift", "Soft Smile", "Cloud Nine", "Halo", "Rising Echoes", "Angel Dust", "Sunrise", "Big Smile",
                                          "Bouncy Smile", "Tape Rise", "Hard Grin", "Barber Pole", "Grain Storm", "Face Melter", "Liftoff", "Endless Wow"};

// controls the Q-Link slots and the displays know about
enum Ctl { C_WOW, C_MIX, C_STYLE, C_LEVEL, C_SIZE, C_DENSITY, C_SPEED, C_SPLIT, C_RETRACE, C_RETTIME, C_RETLEN, C_VAR, C_AMOUNT,
           C_RETMODE, C_SYNC, C_COUNT };
static const char *const kCtlName[C_COUNT] = {"Wow", "Mix", "Preset", "Level", "Size", "Density", "Speed", "Split", "Retrace",
                                              "Time", "Length", "Variation", "Amount", "Retrace mode", "Sync"};
static const int kGrainKnobs = 10;   // C_LEVEL .. C_AMOUNT
static const char *const kGrainKey[kGrainKnobs] = {"g_level", "g_size", "g_density", "g_speed", "g_split", "g_retrace",
                                                   "g_rettime", "g_retlen", "g_var", "g_amount"};
static const float kGrainDefault[kGrainKnobs] = {50, 50, 50, 0, 0, 0, 50, 50, 0, 0};
static const int kQSlots = 12;
static const int kQMap[2][kQSlots] = {
    {C_WOW, C_MIX, C_STYLE, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {C_SIZE, C_DENSITY, C_SPEED, C_VAR, C_RETRACE, C_SPLIT, C_RETTIME, C_RETLEN, C_AMOUNT, C_LEVEL, C_RETMODE, C_SYNC},
};
// keys reported back to MPC when the engine moves them itself (mpc_engine_changed): one bit each
enum Extra { X_WOW, X_MIX, X_STYLE, X_PAGE, X_HELP, X_CAT, X_G0, X_RETMODE = X_G0 + kGrainKnobs, X_SYNC, X_Q1, X_COUNT = X_Q1 + kQSlots };
static_assert(X_COUNT <= 64, "pending is a 64-bit mask");

struct Poliwow {
    WowDSP dsp;
    float wow = 0, mix = 100;
    int style = 0, page = 0, help = 0;   // help: 0 closed, 1 / 2 the help window's pages
    float g[kGrainKnobs];
    bool retReverse = false, sync = false;
    WowNav nav;
    uint64_t pending = 0;
    unsigned displayRev = 1;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];

    void init() {
        dsp.init();
        dsp.setWow(0);
        dsp.setMix(1);
        for (int i = 0; i < kGrainKnobs; ++i) g[i] = kGrainDefault[i];
        setStyle(0, true);
    }
    void changed(int x) { pending |= 1ull << x; }
    void changedQs() { for (int q = 0; q < kQSlots; ++q) changed(X_Q1 + q); }
    void changedSlots(int ctl) { for (int q = 0; q < kQSlots; ++q) if (kQMap[page][q] == ctl) changed(X_Q1 + q); }

    void applyGrain() {
        GrainCtl c;
        c.level = g[0] * 0.01f; c.size = g[1] * 0.01f; c.density = g[2] * 0.01f; c.speed = g[3] * 0.01f;
        c.split = g[4] * 0.01f; c.retrace = g[5] * 0.01f; c.retTime = g[6] * 0.01f; c.retLen = g[7] * 0.01f;
        c.variation = g[8] * 0.01f; c.amount = g[9] * 0.01f; c.retReverse = retReverse; c.sync = sync;
        dsp.setGrainCtl(c);
    }
    // the preset's granulator settings go onto the knobs (they move on screen, and on the Q-Links)
    void recall(const GrainCtl &c) {
        g[0] = c.level * 100; g[1] = c.size * 100; g[2] = c.density * 100; g[3] = c.speed * 100; g[4] = c.split * 100;
        g[5] = c.retrace * 100; g[6] = c.retTime * 100; g[7] = c.retLen * 100; g[8] = c.variation * 100; g[9] = c.amount * 100;
        retReverse = c.retReverse; sync = c.sync;
        applyGrain();
        for (int i = 0; i < kGrainKnobs; ++i) changed(X_G0 + i);
        changed(X_RETMODE); changed(X_SYNC);
        changedQs();
    }
    // s = a preset number; the arrows wrap around. `recall` = the preset also sets the granulator knobs
    void setStyle(int s, bool doRecall, bool closeBrowser = false) {
        s = ((s % kNumPresets) + kNumPresets) % kNumPresets;
        const bool moved = s != style;
        style = s;
        dsp.setStyle(kPresets[s].base);
        if (doRecall) recall(kPresets[s].g);
        if (moved) changed(X_CAT);
        nav.onStyle(style, closeBrowser);
    }
    void setPage(int p) {
        p = p < 0 ? 0 : p > 1 ? 1 : p;
        if (p == page) return;
        page = p;
        changed(X_PAGE);
        changedQs();
        ++displayRev;   // MPC re-reads the Q-Link names and the page's value texts
    }
    void setWow(float v) { wow = clampf(v, 0, 100); dsp.setWow(wow / 100.0f); }
    void setMix(float v) { mix = clampf(v, 0, 100); dsp.setMix(mix / 100.0f); }
    void setGrain(int i, float v) { g[i] = clampf(v, 0, 100); applyGrain(); }
    void setRetMode(bool on) { retReverse = on; applyGrain(); }
    void setSync(bool on) { sync = on; applyGrain(); }

    // the preset arrows and a preset pick: tell the host which keys moved
    void stepStyle(int d) {
        setStyle(style + d, true);
        changed(X_STYLE); changedSlots(C_STYLE);
    }
    void stepCat(int d) {
        int c = ((kPresets[style].cat + d) % kNumCats + kNumCats) % kNumCats;
        setStyle(wowFirstPreset(c), true);
        changed(X_STYLE); changedSlots(C_STYLE);
    }

    // a slot (or the control itself) written with a 0..100 number
    void setCtl(int ctl, float v) {
        switch (ctl) {
            case C_WOW: setWow(v); break;
            case C_MIX: setMix(v); break;
            case C_STYLE: setStyle(styleIndex(v * 0.01f * (kNumPresets - 1)), true); changed(X_STYLE); break;
            case C_RETMODE: setRetMode(v >= 50.0f); changed(X_RETMODE); break;
            case C_SYNC: setSync(v >= 50.0f); changed(X_SYNC); break;
            default: if (ctl >= C_LEVEL && ctl <= C_AMOUNT) setGrain(ctl - C_LEVEL, v); break;
        }
    }
    float ctlPos(int ctl) const {
        switch (ctl) {
            case C_WOW: return wow;
            case C_MIX: return mix;
            case C_STYLE: return 100.0f * style / (kNumPresets - 1);
            case C_RETMODE: return retReverse ? 100.0f : 0.0f;
            case C_SYNC: return sync ? 100.0f : 0.0f;
            default: return ctl >= C_LEVEL && ctl <= C_AMOUNT ? g[ctl - C_LEVEL] : 0.0f;
        }
    }

    // value text of a control
    static int fmtScale(char *buf, int len, float v) {   // Size / Density: x0.25 .. x4, 50 % = x1
        char n[24];
        snprintf(n, sizeof n, "%.2f", (double)exp2f((v * 0.01f - 0.5f) * 4.0f));
        char *e = n + strlen(n);
        while (e > n + 1 && e[-1] == '0') *--e = 0;
        if (e[-1] == '.') *--e = 0;
        return snprintf(buf, len, "%sx", n);
    }
    static int fmtTime(char *buf, int len, float ms) {
        if (ms < 1000.0f) return snprintf(buf, len, "%d ms", (int)lrintf(ms));
        return snprintf(buf, len, "%.1f s", (double)(ms * 0.001f));
    }
    int display(int ctl, char *buf, int len) const {
        const float p = ctlPos(ctl);
        switch (ctl) {
            case C_STYLE: return snprintf(buf, len, "%s", kPresets[style].name);
            case C_SIZE: case C_DENSITY: return fmtScale(buf, len, p);
            case C_LEVEL: return snprintf(buf, len, "%d%%", (int)lrintf(p * 2.0f));   // the cloud's gain: 50 = as the preset = 100 %
            case C_RETTIME: return fmtTime(buf, len, 50.0f * powf(40.0f, p * 0.01f));
            case C_RETLEN: return fmtTime(buf, len, 40.0f * powf(25.0f, p * 0.01f));
            case C_RETMODE: return snprintf(buf, len, "%s", retReverse ? "Reverse" : "Jump");
            case C_SYNC: return snprintf(buf, len, "%s", sync ? "On" : "Off");
            default: return snprintf(buf, len, "%d%%", (int)lrintf(p));
        }
    }

    static int qOfKey(const char *key) {   // "q3" -> 2, else -1
        if (key[0] != 'q') return -1;
        char *end = nullptr;
        const long n = strtol(key + 1, &end, 10);
        if (end == key + 1 || *end || n < 1 || n > kQSlots) return -1;
        return (int)n - 1;
    }
    static int grainOfKey(const char *key) {
        for (int i = 0; i < kGrainKnobs; ++i) if (!strcmp(key, kGrainKey[i])) return i;
        return -1;
    }

    void set(const char *key, const char *val) {
        if (!strcmp(key, "state")) { if (val) load(val); return; }
        if (!strcmp(key, "wow")) { setWow(num(val, wow)); changedSlots(C_WOW); return; }
        if (!strcmp(key, "mix")) { setMix(num(val, mix)); changedSlots(C_MIX); return; }
        {   // the preset browser (wow_nav.h): a pick loads that preset (even the current one: it brings back an edited one) and closes it
            const int pk = nav.set(key, val);
            if (pk >= 0) { setStyle(pk, true, true); changed(X_STYLE); changedSlots(C_STYLE); return; }
            if (pk == WowNav::HANDLED) { if (nav.browsing && help) { help = 0; changed(X_HELP); } return; }
        }
        if (!strcmp(key, "style")) {
            const int s = styleIndex(num(val, (float)style));
            if (s != style) { setStyle(s, true); changedSlots(C_STYLE); }   // the same preset again leaves the knobs alone
            return;
        }
        if (!strcmp(key, "style_prev")) { if (num(val, 0) >= 0.5f) stepStyle(-1); return; }
        if (!strcmp(key, "style_next")) { if (num(val, 0) >= 0.5f) stepStyle(1); return; }
        if (!strcmp(key, "cat_prev")) { if (num(val, 0) >= 0.5f) stepCat(-1); return; }
        if (!strcmp(key, "cat_next")) { if (num(val, 0) >= 0.5f) stepCat(1); return; }
        if (!strcmp(key, "cat")) return;   // engine-owned: it follows the preset
        const int gi = grainOfKey(key);
        if (gi >= 0) { setGrain(gi, num(val, g[gi])); changedSlots(C_LEVEL + gi); return; }
        if (!strcmp(key, "g_retmode")) { setRetMode(num(val, retReverse ? 1.0f : 0.0f) >= 0.5f); changedSlots(C_RETMODE); return; }
        if (!strcmp(key, "g_sync")) { setSync(num(val, sync ? 1.0f : 0.0f) >= 0.5f); changedSlots(C_SYNC); return; }
        if (!strcmp(key, "help_btn")) { if (num(val, 0) >= 0.5f) { help = help ? 0 : 1; changed(X_HELP); nav.close(); } return; }
        if (!strcmp(key, "help_next")) { if (num(val, 0) >= 0.5f && help) { help = help == 1 ? 2 : 1; changed(X_HELP); } return; }
        if (!strcmp(key, "hview")) return;   // engine-owned
        if (!strcmp(key, "page")) { setPage((int)lrintf(clampf(num(val, (float)page), 0, 1))); return; }
        if (!strcmp(key, "tab1") || !strcmp(key, "tab2")) { if (num(val, 0) >= 0.5f) setPage(key[3] - '1'); return; }
        const int q = qOfKey(key);
        if (q >= 0) {
            const int ctl = kQMap[page][q];
            if (ctl >= 0) {
                setCtl(ctl, num(val, ctlPos(ctl)));
                changed(X_Q1 + q);
                // the control's own knob / name follow, and any other slot showing it
                if (ctl == C_WOW) changed(X_WOW); else if (ctl == C_MIX) changed(X_MIX);
                else if (ctl == C_STYLE) changed(X_STYLE);
                else if (ctl >= C_LEVEL && ctl <= C_AMOUNT) { changed(X_G0 + ctl - C_LEVEL); changedSlots(ctl); }
            }
        }
    }

    int save(char *buf, int len) const {
        int n = snprintf(buf, len, "PWW2;wow=%.3f;mix=%.3f;style=%d;page=%d;gl=%.2f;gs=%.2f;gd=%.2f;gv=%.2f;gp=%.2f;gr=%.2f;gt=%.2f;gn=%.2f;gx=%.2f;ga=%.2f;rm=%d;sy=%d",
                         (double)wow, (double)mix, style, page, (double)g[0], (double)g[1], (double)g[2], (double)g[3], (double)g[4],
                         (double)g[5], (double)g[6], (double)g[7], (double)g[8], (double)g[9], retReverse ? 1 : 0, sync ? 1 : 0);
        return n > 0 && n < len ? n : 0;
    }
    void load(const char *text) {
        const bool v2 = !strncmp(text, "PWW2;", 5);
        if (!v2 && strncmp(text, "PWW1;", 5)) return;
        static const char *const gk[kGrainKnobs] = {"gl=", "gs=", "gd=", "gv=", "gp=", "gr=", "gt=", "gn=", "gx=", "ga="};
        for (const char *p = text + 5; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : nullptr) {
            if (!strncmp(p, "wow=", 4)) setWow(num(p + 4, wow));
            else if (!strncmp(p, "mix=", 4)) setMix(num(p + 4, mix));
            else if (!strncmp(p, "style=", 6)) {
                int s = styleIndex(num(p + 6, (float)style));
                if (!v2) {   // a 1.1 number: the preset of the same name, the granulator neutral (as 1.1 sounded)
                    const int o = s < 16 ? s : 15;
                    for (int i = 0; i < kNumPresets; ++i) if (!strcmp(kPresets[i].name, kOldNames[o])) { s = i; break; }
                    setStyle(s, false);
                    for (int i = 0; i < kGrainKnobs; ++i) g[i] = kGrainDefault[i];
                    retReverse = false; sync = false; applyGrain();
                } else setStyle(s, false);
            }
            else if (v2 && !strncmp(p, "page=", 5)) setPage((int)lrintf(clampf(num(p + 5, (float)page), 0, 1)));
            else if (v2 && !strncmp(p, "rm=", 3)) { retReverse = num(p + 3, 0) >= 0.5f; applyGrain(); }
            else if (v2 && !strncmp(p, "sy=", 3)) { sync = num(p + 3, 0) >= 0.5f; applyGrain(); }
            else if (v2) for (int i = 0; i < kGrainKnobs; ++i) if (!strncmp(p, gk[i], 3)) { g[i] = clampf(num(p + 3, g[i]), 0, 100); applyGrain(); }
        }
        nav.close(); changed(X_STYLE); changed(X_CAT); changedQs();   // tell MPC the preset name / category are what they are now
    }

    int get(const char *key, char *buf, int len) {
        if (!strcmp(key, "wow")) return snprintf(buf, len, "%.3f", (double)wow);
        if (!strcmp(key, "mix")) return snprintf(buf, len, "%.3f", (double)mix);
        if (!strcmp(key, "style")) return snprintf(buf, len, "%d", style);
        if (!strcmp(key, "cat")) return snprintf(buf, len, "%d", kPresets[style].cat);
        if (!strcmp(key, "style_prev") || !strcmp(key, "style_next") || !strcmp(key, "tab1") || !strcmp(key, "tab2") ||
            !strcmp(key, "cat_prev") || !strcmp(key, "cat_next")) return snprintf(buf, len, "0");
        if (!strcmp(key, "page")) return snprintf(buf, len, "%d", page);
        if (!strcmp(key, "hview")) return snprintf(buf, len, "%d", help);
        if (!strcmp(key, "help_btn") || !strcmp(key, "help_next")) return snprintf(buf, len, "0");
        const int gi = grainOfKey(key);
        if (gi >= 0) return snprintf(buf, len, "%.3f", (double)g[gi]);
        if (!strcmp(key, "g_retmode")) return snprintf(buf, len, "%d", retReverse ? 1 : 0);
        if (!strcmp(key, "g_sync")) return snprintf(buf, len, "%d", sync ? 1 : 0);
        if (!strcmp(key, "wow_display")) return display(C_WOW, buf, len);
        if (!strcmp(key, "mix_display")) return display(C_MIX, buf, len);
        const size_t kl = strlen(key);
        if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
            char base[32];
            if (kl - 8 >= sizeof base) return 0;
            memcpy(base, key, kl - 8);
            base[kl - 8] = 0;
            const int bi = grainOfKey(base);
            if (bi >= 0) return display(C_LEVEL + bi, buf, len);
            const int q = qOfKey(base);
            if (q >= 0) { const int ctl = kQMap[page][q]; return ctl >= 0 ? display(ctl, buf, len) : snprintf(buf, len, "-"); }
            return 0;
        }
        if (kl > 5 && !strcmp(key + kl - 5, "_name")) {
            char base[32];
            if (kl - 5 >= sizeof base) return 0;
            memcpy(base, key, kl - 5);
            base[kl - 5] = 0;
            const int q = qOfKey(base);
            if (q >= 0) { const int ctl = kQMap[page][q]; return snprintf(buf, len, "%s", ctl >= 0 ? kCtlName[ctl] : "-"); }
            return 0;
        }
        const int q = qOfKey(key);
        if (q >= 0) { const int ctl = kQMap[page][q]; return snprintf(buf, len, "%.4f", (double)(ctl >= 0 ? ctlPos(ctl) : 0.0f)); }
        if (!strcmp(key, "display_rev")) return snprintf(buf, len, "%u", displayRev);
        if (!strcmp(key, "state")) return save(buf, len);
        return nav.get(key, buf, len);
    }

    static const char *extraKey(int x, char *tmp, int n) {
        static const char *const fixed[] = {"wow", "mix", "style", "page", "hview", "cat"};
        if (x < X_G0) return fixed[x];
        if (x < X_RETMODE) return kGrainKey[x - X_G0];
        if (x == X_RETMODE) return "g_retmode";
        if (x == X_SYNC) return "g_sync";
        snprintf(tmp, n, "q%d", x - X_Q1 + 1);
        return tmp;
    }
};

static void *engine_create(const char *) {
    void *mem = malloc(sizeof(Poliwow));   // malloc + placement new: nothing needed from the C++ runtime
    if (!mem) return nullptr;
    Poliwow *p = new (mem) Poliwow();
    p->init();
    return p;
}
static void engine_destroy(void *inst) {
    if (!inst) return;
    static_cast<Poliwow *>(inst)->~Poliwow();
    free(inst);
}
static void engine_midi(void *, const uint8_t *, int) {}
static void engine_set_param(void *inst, const char *key, const char *val) { static_cast<Poliwow *>(inst)->set(key, val); }
static int engine_get_param(void *inst, const char *key, char *buf, int len) { return static_cast<Poliwow *>(inst)->get(key, buf, len); }

static void engine_render(void *, int16_t *out_lr, int frames) { memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

static void engine_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    auto *s = static_cast<Poliwow *>(inst);
    for (int done = 0; done < frames;) {
        const int n = frames - done < kBlock ? frames - done : kBlock;
        const int16_t *in = in_lr + 2 * done;
        int16_t *out = out_lr + 2 * done;
        for (int i = 0; i < n; ++i) {
            s->inL[i] = in[2 * i] * (1.0f / 32768.0f);
            s->inR[i] = in[2 * i + 1] * (1.0f / 32768.0f);
        }
        s->dsp.process(s->inL, s->inR, s->outL, s->outR, n);
        for (int i = 0; i < n; ++i) {   // x 32768: with Wow at 0 the samples come back exactly as they went in
            out[2 * i] = (int16_t)lrintf(clampf(s->outL[i] * 32768.0f, -32768.0f, 32767.0f));
            out[2 * i + 1] = (int16_t)lrintf(clampf(s->outR[i] * 32768.0f, -32768.0f, 32767.0f));
        }
        done += n;
    }
}

static void engine_song_pos(void *inst, double ppq, double bpm, int playing, int) {
    static_cast<Poliwow *>(inst)->dsp.setSongPos(ppq, bpm, playing != 0);
}

static const mpc_engine_t ENGINE = {engine_create, engine_destroy, engine_midi, engine_set_param, engine_get_param,
                                    engine_render, engine_process, engine_song_pos};

extern "C" {
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

// values the engine moved by itself (the preset arrows, a page switch, a Q-Link slot): the wrapper reports them to MPC
int mpc_engine_changed(void *inst, char *key, int len) {
    auto *s = static_cast<Poliwow *>(inst);
    if (const char *k = s->nav.changed()) { snprintf(key, len, "%s", k); return 1; }
    if (!s->pending) return 0;
    const int x = __builtin_ctzll(s->pending);
    s->pending &= ~(1ull << x);
    char tmp[8];
    snprintf(key, len, "%s", Poliwow::extraKey(x, tmp, sizeof tmp));
    return 1;
}
}
