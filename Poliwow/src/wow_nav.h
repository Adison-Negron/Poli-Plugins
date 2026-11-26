// wow_nav.h -- Poliwow's preset browser (engine side), the same scheme as Poli-tea's tea_nav.h.
//
// On screen: the current preset's name between two arrows in the header. Tapping the name opens a browser: a column of
// categories (All, Bright, Soft, ...) on the left, a page of 12 presets on the right, page arrows in its header. All =
// every preset in order; a category lists its own. The arrows step through all presets, whatever the browser shows.
//
// Parameters (all in vst/params.json; the momentary ones take a press >= 0.5):
//   style_list     the name: opens / closes the browser
//   browsing       Off/On, engine-owned: the browser is open (the skin shows its buttons with when=browsing:On)
//   bview          option list, engine-owned: 0 = closed, 1 + view = which (category, page) the panel picture shows
//   pre_hl         option list, engine-owned: 0 = none, 1..12 = the cell to highlight (the current preset, if on this page)
//   bpage_prev/_next   page arrows inside the browser (wrap)
//   cat1..cat8     category buttons (cat1 = All, cat2 = Bright ...)
//   pick1..pick12  the 12 cells of the page on show: loads that preset (the engine does) and closes the browser
// The engine owns the preset itself (`style`); it tells this struct about every change with onStyle().
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "WowDSP.h"

struct WowNav {
    static const int kPer = 12;                  // cells per page
    static const int kCatsN = kNumCats + 1;      // "All" + the real categories
    enum { NOT_OURS = -3, HANDLED = -2 };

    int cur = 0;                                 // the preset now loaded
    bool browsing = false;
    int cat = 0, bpage = 0;                      // what the browser shows
    unsigned dirty = 0;                          // bit 0 browsing, 1 bview, 2 pre_hl

    int members[kCatsN][kNumPresets];
    int count[kCatsN], pages[kCatsN], base[kCatsN + 1];

    WowNav() {
        for (int c = 0; c < kCatsN; ++c) {
            count[c] = 0;
            for (int i = 0; i < kNumPresets; ++i)
                if (c == 0 || kPresets[i].cat == c - 1) members[c][count[c]++] = i;
            pages[c] = count[c] ? (count[c] + kPer - 1) / kPer : 1;
        }
        base[0] = 0;
        for (int c = 0; c < kCatsN; ++c) base[c + 1] = base[c] + pages[c];
    }
    int viewCount() const { return base[kCatsN]; }
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

    int indexIn(int c, int preset) const {
        for (int i = 0; i < count[c]; ++i) if (members[c][i] == preset) return i;
        return -1;
    }
    // the category and page that hold the current preset
    void showCurrent() {
        const int c = kPresets[cur].cat + 1;
        const int i = indexIn(c, cur);
        cat = c;
        bpage = i < 0 ? 0 : i / kPer;
    }
    int slot() const {   // 1..12 when the current preset is on the page on show, else 0
        const int i = indexIn(cat, cur);
        return (i >= 0 && i / kPer == bpage) ? i % kPer + 1 : 0;
    }

    // The preset to load now (a pick), HANDLED (one of ours, nothing to load) or NOT_OURS (another parameter).
    int set(const char *key, const char *val) {
        if (!strcmp(key, "style_list")) {
            if (press(val)) {
                browsing = !browsing;
                if (browsing) showCurrent();
                dirty |= 1 | 2 | 4;
            }
            return HANDLED;
        }
        if (!strcmp(key, "bpage_prev") || !strcmp(key, "bpage_next")) {
            if (press(val)) {
                bpage = (bpage + (key[6] == 'p' ? pages[cat] - 1 : 1)) % pages[cat];
                dirty |= 2 | 4;
            }
            return HANDLED;
        }
        if (!strncmp(key, "cat", 3) && key[3] >= '1' && key[3] <= '9') {
            char *end = nullptr;
            const long n = strtol(key + 3, &end, 10);
            if (*end || n < 1 || n > kCatsN) return NOT_OURS;
            if (press(val)) { cat = (int)n - 1; bpage = 0; dirty |= 2 | 4; }
            return HANDLED;
        }
        if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '9') {
            char *end = nullptr;
            const long n = strtol(key + 4, &end, 10);
            if (*end || n < 1 || n > kPer) return NOT_OURS;
            if (!press(val)) return HANDLED;
            const int i = bpage * kPer + (int)n - 1;
            return i < count[cat] ? members[cat][i] : HANDLED;
        }
        if (!strcmp(key, "browsing") || !strcmp(key, "bview") || !strcmp(key, "pre_hl")) return HANDLED;   // engine-owned
        return NOT_OURS;
    }

    // the engine changed the preset (closeIt: a pick from the browser)
    void onStyle(int s, bool closeIt) {
        cur = s;
        dirty |= 4;
        if (browsing && !closeIt && cat == 0) { bpage = cur / kPer; dirty |= 2; }   // the arrows follow along in All
        if (closeIt && browsing) { browsing = false; dirty |= 1 | 2 | 4; }
    }
    void close() { if (browsing) { browsing = false; dirty |= 1 | 2 | 4; } }

    // 1 + text for our keys, 0 for any other key
    int get(const char *key, char *buf, int len) const {
        if (!strcmp(key, "browsing")) return snprintf(buf, len, "%d", browsing ? 1 : 0);
        if (!strcmp(key, "bview")) return snprintf(buf, len, "%d", browsing ? 1 + view() : 0);
        if (!strcmp(key, "pre_hl")) return snprintf(buf, len, "%d", browsing ? slot() : 0);
        if (!strcmp(key, "style_list") || !strcmp(key, "bpage_prev") || !strcmp(key, "bpage_next")) return snprintf(buf, len, "0");
        if (!strncmp(key, "cat", 3) && key[3] >= '1' && key[3] <= '9') {
            char *end = nullptr;
            const long n = strtol(key + 3, &end, 10);
            if (!*end && n >= 1 && n <= kCatsN) return snprintf(buf, len, "0");
        }
        if (!strncmp(key, "pick", 4) && key[4] >= '1' && key[4] <= '9') {
            char *end = nullptr;
            const long n = strtol(key + 4, &end, 10);
            if (!*end && n >= 1 && n <= kPer) return snprintf(buf, len, "0");
        }
        return 0;
    }

    // One of our keys that moved, for mpc_engine_changed(); nullptr when none is left.
    const char *changed() {
        static const char *const names[3] = {"browsing", "bview", "pre_hl"};
        for (int b = 0; b < 3; ++b)
            if (dirty & (1u << b)) { dirty &= ~(1u << b); return names[b]; }
        return nullptr;
    }
};
