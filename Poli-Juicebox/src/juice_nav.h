// juice_nav.h -- Poli-Juicebox's preset selector and categorized, multi-page browser (engine side).
//
// On screen (generate_skin.py draws it): the current preset's name between two arrows, in the header. Tapping the name
// opens a browser: a column of categories (All, Drums, Vocals, ...) on the left, a page of 12 presets on the right,
// page arrows below. All = every preset in order; the other categories list their own, 12 to a page. The arrows
// step through All, whatever the browser shows.
//
// Parameters (all in vst/params.json; the momentary ones take a press >= 0.5):
//   preset         option list = every preset (vst.json "programs": also lists them in MPC's PRESET menu)
//   preset_prev/_next/_list   the arrows and the name (opens/closes the browser)
//   browsing       Off/On, engine-owned: the browser is open (the skin shows its buttons with when=browsing:On)
//   bview          option list, engine-owned: 0 = closed, 1 + view = which (category, page) the panel picture shows
//   pre_hl         option list, engine-owned: 0 = none, 1..12 = the cell to highlight (the current preset, if on this page)
//   bpage_prev/_next          page arrows inside the browser (wrap)
//   cat1..cat<C+1>            category buttons (cat1 = All)
//   pick1..pick12             the 12 cells of the page on show: loads that preset and closes the browser
// The engine applies the preset itself; this struct keeps the index, the category/page on show and the open state.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "presets.h"

struct JuiceNav {
    static const int kPages = 4;                 // right-hand pages: Shape, Tone, Filter, Tracking (the latency pills live on the last)
    static const int kPer = 12;                  // cells per page
    static const int kCats = kNumCats + 1;       // "All" + the real categories
    enum { NOT_OURS = -3, HANDLED = -2 };

    int cur = 0;                                 // the preset last loaded (0 = Init)
    bool browsing = false;
    int cat = 0, bpage = 0;                      // what the browser shows
    int page = 0;                                // the right-hand page on show (0 Shape, 1 Tone, 2 Filter, 3 Tracking)
    int lat = 1;                                 // latency mode: 0 Fast, 1 Balanced, 2 Smooth (the engine hands it to the DSP)
    int help = 0;                                // 0 closed, 1 / 2 = the help window's page on show
    unsigned dirty = 0;                          // bit 0 preset, 1 browsing, 2 bview, 3 pre_hl, 4 page, 5 hview, 6 lview

    int members[kCats][kNumPresets];
    int count[kCats], pages[kCats], base[kCats + 1];

    JuiceNav() {
        for (int c = 0; c < kCats; ++c) {
            count[c] = 0;
            for (int i = 0; i < kNumPresets; ++i)
                if (c == 0 || kPresets[i].cat == c) members[c][count[c]++] = i;
            pages[c] = count[c] ? (count[c] + kPer - 1) / kPer : 1;
        }
        base[0] = 0;
        for (int c = 0; c < kCats; ++c) base[c + 1] = base[c] + pages[c];
    }
    int viewCount() const { return base[kCats]; }
    int view() const { return base[cat] + bpage; }

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
    static int clampIdx(long i, int n) { return i < 0 ? 0 : i >= n ? n - 1 : (int)i; }

    int indexIn(int c, int preset) const {
        for (int i = 0; i < count[c]; ++i) if (members[c][i] == preset) return i;
        return -1;
    }
    // the category and page that hold the current preset (its own category; Init lives in All)
    void showCurrent() {
        int c = (cur > 0 && kPresets[cur].cat >= 1 && kPresets[cur].cat < kCats) ? kPresets[cur].cat : 0;
        int i = indexIn(c, cur);
        cat = c;
        bpage = i < 0 ? 0 : i / kPer;
    }
    int slot() const {   // 1..12 when the current preset is on the page on show, else 0
        int i = indexIn(cat, cur);
        return (i >= 0 && i / kPer == bpage) ? i % kPer + 1 : 0;
    }

    // The preset index to load now, HANDLED (one of ours, nothing to load) or NOT_OURS (another parameter).
    int set(const char *key, const char *val) {
        if (!strcmp(key, "preset")) {
            double d;
            if (!number(val, d)) return HANDLED;
            int i = clampIdx(lround(d), kNumPresets);
            return i == cur ? HANDLED : load(i, false);
        }
        if (!strcmp(key, "preset_prev")) return press(val) ? load((cur + kNumPresets - 1) % kNumPresets, false) : HANDLED;
        if (!strcmp(key, "preset_next")) return press(val) ? load((cur + 1) % kNumPresets, false) : HANDLED;
        if (!strcmp(key, "preset_list")) {
            if (press(val)) {
                browsing = !browsing;
                if (browsing) { showCurrent(); if (help) { help = 0; dirty |= 32; } }
                dirty |= 2 | 4 | 8;
            }
            return HANDLED;
        }
        if (!strcmp(key, "help_btn")) {   // the ? button opens the help window, a tap on the window closes it
            if (press(val)) {
                help = help ? 0 : 1;
                if (help && browsing) { browsing = false; dirty |= 2 | 4 | 8; }
                dirty |= 32;
            }
            return HANDLED;
        }
        if (!strcmp(key, "help_next")) {
            if (press(val) && help) { help = help == 1 ? 2 : 1; dirty |= 32; }
            return HANDLED;
        }
        if (!strcmp(key, "bpage_prev") || !strcmp(key, "bpage_next")) {
            if (press(val)) {
                bpage = (bpage + (key[6] == 'p' ? pages[cat] - 1 : 1)) % pages[cat];
                dirty |= 4 | 8;
            }
            return HANDLED;
        }
        if (!strncmp(key, "cat", 3) && key[3] >= '1' && key[3] <= '9') {
            char *end = nullptr;
            long n = strtol(key + 3, &end, 10);
            if (*end || n < 1 || n > kCats) return NOT_OURS;
            if (press(val)) { cat = (int)n - 1; bpage = 0; dirty |= 4 | 8; }
            return HANDLED;
        }
        if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '9') {
            char *end = nullptr;
            long n = strtol(key + 4, &end, 10);
            if (*end || n < 1 || n > kPer) return NOT_OURS;
            if (!press(val)) return HANDLED;
            const int i = bpage * kPer + (int)n - 1;
            return i < count[cat] ? load(members[cat][i], true) : HANDLED;
        }
        if (!strncmp(key, "tab", 3) && key[3] >= '1' && key[3] <= '4' && !key[4]) {
            if (press(val)) { const int p = key[3] - '1'; if (p != page) { page = p; dirty |= 16 | 64; } }
            return HANDLED;
        }
        if (!strcmp(key, "page")) {   // a Q-Link / automation may also set it
            double d;
            if (number(val, d)) { const int p = clampIdx(lround(d), kPages); if (p != page) { page = p; dirty |= 16 | 64; } }
            return HANDLED;
        }
        if (!strncmp(key, "lat", 3) && key[3] >= '1' && key[3] <= '3' && !key[4]) {   // the latency pills on the Tracking page (1 Fast, 2 Balanced, 3 Smooth)
            if (press(val)) { const int m = key[3] - '1'; if (m != lat) { lat = m; dirty |= 64; } }
            return HANDLED;
        }
        if (!strcmp(key, "lview")) return HANDLED;                                    // derived: 0 off the Tracking page, else 1 + mode
        if (!strcmp(key, "browsing") || !strcmp(key, "bview") || !strcmp(key, "pre_hl") || !strcmp(key, "hview")) return HANDLED;   // engine-owned
        return NOT_OURS;
    }

    int load(int i, bool close) {
        cur = clampIdx(i, kNumPresets);
        dirty |= 1 | 8;
        if (browsing && !close && cat == 0) { bpage = cur / kPer; dirty |= 4; }   // the arrows follow along in All
        if (close && browsing) { browsing = false; dirty |= 2 | 4 | 8; }
        return cur;
    }
    void close() { if (browsing) { browsing = false; dirty |= 2 | 4 | 8; } }
    // a restored project: the name only (the values come from the state itself)
    void restore(int i) { cur = clampIdx(i, kNumPresets); browsing = false; cat = 0; bpage = 0; help = 0; dirty |= 111; }

    // 1 + text for our keys, 0 for any other key
    int get(const char *key, char *buf, int len) const {
        if (!strcmp(key, "preset")) return snprintf(buf, len, "%d", cur);
        if (!strcmp(key, "browsing")) return snprintf(buf, len, "%d", browsing ? 1 : 0);
        if (!strcmp(key, "bview")) return snprintf(buf, len, "%d", browsing ? 1 + view() : 0);
        if (!strcmp(key, "pre_hl")) return snprintf(buf, len, "%d", browsing ? slot() : 0);
        if (!strcmp(key, "page")) return snprintf(buf, len, "%d", page);
        if (!strcmp(key, "hview")) return snprintf(buf, len, "%d", help);
        if (!strcmp(key, "help_btn") || !strcmp(key, "help_next")) return snprintf(buf, len, "0");
        if ((!strncmp(key, "tab", 3) && key[3] >= '1' && key[3] <= '4' && !key[4])) return snprintf(buf, len, "0");
        if (!strcmp(key, "lview")) return snprintf(buf, len, "%d", page == kPages - 1 ? 1 + lat : 0);
        if (!strncmp(key, "lat", 3) && key[3] >= '1' && key[3] <= '3' && !key[4]) return snprintf(buf, len, "0");
        if (!strcmp(key, "preset_prev") || !strcmp(key, "preset_next") || !strcmp(key, "preset_list") ||
            !strcmp(key, "bpage_prev") || !strcmp(key, "bpage_next"))
            return snprintf(buf, len, "0");
        if (!strncmp(key, "cat", 3) && key[3] >= '1' && key[3] <= '9') {
            char *end = nullptr;
            long n = strtol(key + 3, &end, 10);
            if (!*end && n >= 1 && n <= kCats) return snprintf(buf, len, "0");
        }
        if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '9') {
            char *end = nullptr;
            long n = strtol(key + 4, &end, 10);
            if (!*end && n >= 1 && n <= kPer) return snprintf(buf, len, "0");
        }
        return 0;
    }

    // One of our keys that moved, for mpc_engine_changed(); nullptr when none is left.
    const char *changed() {
        static const char *const names[7] = {"preset", "browsing", "bview", "pre_hl", "page", "hview", "lview"};
        for (int b = 0; b < 7; ++b)
            if (dirty & (1u << b)) { dirty &= ~(1u << b); return names[b]; }
        return nullptr;
    }
};
