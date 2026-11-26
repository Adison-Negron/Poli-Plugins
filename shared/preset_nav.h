// preset_nav.h -- the preset selector shared by the Poli plugins (Poli-Ess, Poli-Multiband, Shhhhh).
// Copied into each plugin's src/ by its generate_skin.py (shared/poli_preset_ui.py), so the Docker build sees it;
// edit C:\MPC-PLUGINS\shared\preset_nav.h, not the copies.
//
// On screen: the current preset's name with an arrow on each side; tapping the name opens a browser of every preset,
// and picking one loads it and closes the browser. The parameters (appended to params.json by poli_preset_ui.py):
//   preset        option list = the presets (vst.json "programs": {"param": "preset"} also lists them in MPC's PRESET
//                 menu). A host write (PRESET menu, Q-Link, automation) loads it unless it is already the current one.
//   preset_prev   momentary: the arrow left of the name  (loads the previous preset, wrapping)
//   preset_next   momentary: the arrow right of the name (the next one)
//   preset_list   momentary: the name itself, and the browser's backdrop: opens / closes the browser
//   browsing      Off/On, engine-owned: the browser is up (the skin shows its cells with when=browsing:On)
//   pre_hl        "-", "1".."N", engine-owned: the browser cell to highlight (the current preset while browsing, else "-")
//   pick1..pickN  momentary: the browser's cells. A pick loads that preset even if it is the current one (so it also
//                 restores a preset after tweaks), then closes the browser.
// The engine applies the preset itself (it knows its own values) and reports what moved; this struct only keeps the
// index and the browser state, and says which of its own keys changed (changed()).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct PresetNav {
    int count = 1;         // number of presets
    int cur = 0;           // the preset last loaded (0 = Init on a fresh insert)
    bool browsing = false;
    unsigned dirty = 0;    // bit 0: preset, bit 1: browsing, bit 2: pre_hl

    enum { NOT_OURS = -3, HANDLED = -2 };

    explicit PresetNav(int n = 1) : count(n > 0 ? n : 1) {}

    static bool number(const char *v, double &out) {
        if (!v) return false;
        char *end = nullptr;
        double d = strtod(v, &end);
        if (end == v) return false;
        uint64_t u;   // finite check on the bits: -ffast-math may drop isnan/isfinite
        memcpy(&u, &d, sizeof u);
        if ((u & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) return false;
        out = d;
        return true;
    }
    static bool press(const char *v) { double d; return number(v, d) && d >= 0.5; }
    int clampIdx(long i) const { return i < 0 ? 0 : i >= count ? count - 1 : (int)i; }

    // The index to load now, HANDLED (one of ours, nothing to load) or NOT_OURS (another parameter).
    int set(const char *key, const char *val) {
        if (!strcmp(key, "preset")) {
            double d;
            if (!number(val, d)) return HANDLED;
            int i = clampIdx(lround(d));
            return i == cur ? HANDLED : load(i, false);
        }
        if (!strcmp(key, "preset_prev")) return press(val) ? load((cur + count - 1) % count, false) : HANDLED;
        if (!strcmp(key, "preset_next")) return press(val) ? load((cur + 1) % count, false) : HANDLED;
        if (!strcmp(key, "preset_list")) {
            if (press(val)) { browsing = !browsing; dirty |= 6; }
            return HANDLED;
        }
        if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '9') {
            char *end = nullptr;
            long n = strtol(key + 4, &end, 10);
            if (*end || n < 1 || n > count) return NOT_OURS;
            return press(val) ? load((int)n - 1, true) : HANDLED;
        }
        if (!strcmp(key, "browsing") || !strcmp(key, "pre_hl")) return HANDLED;   // engine-owned: host writes ignored
        return NOT_OURS;
    }

    int load(int i, bool close) {
        cur = clampIdx(i);
        dirty |= 1 | 4;
        if (close && browsing) { browsing = false; dirty |= 2; }
        return cur;
    }

    // A restored project: the name only (the values come from the state itself).
    void restore(int i) { cur = clampIdx(i); browsing = false; dirty |= 7; }

    // 1 + text for our keys, 0 for any other key.
    int get(const char *key, char *buf, int len) const {
        if (!strcmp(key, "preset")) return snprintf(buf, len, "%d", cur);
        if (!strcmp(key, "browsing")) return snprintf(buf, len, "%d", browsing ? 1 : 0);
        if (!strcmp(key, "pre_hl")) return snprintf(buf, len, "%d", browsing ? cur + 1 : 0);
        if (!strcmp(key, "preset_prev") || !strcmp(key, "preset_next") || !strcmp(key, "preset_list"))
            return snprintf(buf, len, "0");
        if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '9') {
            char *end = nullptr;
            long n = strtol(key + 4, &end, 10);
            if (!*end && n >= 1 && n <= count) return snprintf(buf, len, "0");
        }
        return 0;
    }

    // One of our keys that moved, for mpc_engine_changed(); nullptr when none is left.
    const char *changed() {
        static const char *const names[3] = {"preset", "browsing", "pre_hl"};
        for (int b = 0; b < 3; ++b)
            if (dirty & (1u << b)) { dirty &= ~(1u << b); return names[b]; }
        return nullptr;
    }
};
