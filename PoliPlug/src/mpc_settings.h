/* MPC.settings and plugin-folder helpers shared by scan.c (the report) and discover.c (what PoliPlug can host). */
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const SETTINGS[] = {"/data/Settings/MPC/MPC.settings", "/media/az01-internal/Settings/MPC/MPC.settings", NULL};
#define FACTORY "/usr/share/Akai/Content/Synths"

static inline char *slurp(const char *path, long max) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(max + 1);
    long n = b ? (long)fread(b, 1, max, f) : 0;
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

/* MPC.settings keeps the plugin list as escaped XML inside a VALUE: undo &lt; &gt; &quot; &amp; &#13; in place */
static inline void unescape(char *s) {
    static const struct { const char *e; char c; } T[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''},
                                                          {"&#13;", '\r'}, {"&#10;", '\n'}, {"&amp;", '&'}};
    char *o = s;
    for (char *i = s; *i;) {
        int hit = 0;
        if (*i == '&')
            for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++)
                if (!strncmp(i, T[k].e, strlen(T[k].e))) { *o++ = T[k].c; i += strlen(T[k].e); hit = 1; break; }
        if (!hit) *o++ = *i++;
    }
    *o = 0;
}

static inline void attr(const char *tag, const char *end, const char *name, char *out, int len) {
    char pat[40];
    snprintf(pat, sizeof pat, " %s=\"", name);
    out[0] = 0;
    const char *p = strstr(tag, pat);
    if (!p || p > end) return;
    p += strlen(pat);
    const char *q = strchr(p, '"');
    if (!q || q > end) return;
    snprintf(out, len, "%.*s", (int)(q - p), p);
}

static inline const char *elf_kind(const char *path) {
    unsigned char h[20];
    FILE *f = fopen(path, "rb");
    if (!f) return "missing";
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    if (n < 20 || memcmp(h, "\177ELF", 4)) return "not a library";
    int machine = h[18] | h[19] << 8;
    return machine == 183 ? "aarch64 .so" : machine == 40 ? "armv7 .so (wrong CPU for a Live III)" : "other CPU .so";
}

/* the Synths folders MPC searches for skins (SynthContentLocations), plus the factory one */
static inline int locations(const char *xml, char loc[][256], int max) {
    int n = 0;
    const char *p = xml;
    while (n < max - 1 && (p = strstr(p, "<Location>"))) {
        p += 10;
        const char *q = strstr(p, "</Location>");
        if (!q) break;
        snprintf(loc[n], 256, "%.*s", (int)(q - p), p);
        size_t l = strlen(loc[n]);
        if (l > 1 && loc[n][l - 1] == '/') loc[n][l - 1] = 0;
        n++;
        p = q;
    }
    snprintf(loc[n++], 256, "%s", FACTORY);
    return n;
}

static inline int skin_in(char loc[][256], int nloc, const char *folder, char *where, int len) {
    char path[768];
    struct stat st;
    for (int i = 0; i < nloc; i++) {
        snprintf(path, sizeof path, "%s/%s/Plugin Skins/TUI.json", loc[i], folder);
        if (!stat(path, &st)) { snprintf(where, len, "%s/%s", loc[i], folder); return 1; }
    }
    return 0;
}

