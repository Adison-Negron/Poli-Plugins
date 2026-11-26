/* PoliPlug: find the installed effects and put their skins into PoliPlug's own skin, on the device, at run time.
 *
 * Why the skin is rebuilt: MPC shows the skin of the plugin it inserted (found by that plugin's name, read when it is
 * inserted). A plugin PoliPlug loads is invisible to MPC, so its own Plugin Skins folder is never read. PoliPlug therefore
 * writes its OWN Plugin Skins/TUI.json = the rack (pp_base.json, made at build time) + a copy of every installed
 * effect's pages, each shown only while PoliPlug's "view" parameter is at that page (IndexedEnabling: the handle when=
 * uses, device-verified), with the effect's "Parameter N" pointed at PoliPlug's editor proxy eN, and every image it uses
 * pointed at the effect's own file through a relative path from PoliPlug's Plugin Skins ("../../<its folder>/Plugin
 * Skins/<file>": the Airwindows ports reach their shared "../../AW Skin/" folder the same way).
 *
 * Skins come in two shapes, and both are read: a tab naming a local definition ("componentName", what tools/shadow_skin.py
 * writes) or a tab holding its page inline ("componentDefinition", what the Airwindows skin generator writes). Reading
 * only the first left those plugins as an empty white page (device, Live III, 2026-11-04: AW Ambience).
 *
 * Stable views: each effect keeps the same views across MPC starts (Plugin Skins/pp_registry.txt), so a skin MPC still
 * has cached from before an install still shows the right page for every effect it knows. TUI.json is only rewritten
 * when the set of effects (or one of their skins) changed: write .tmp, then rename(). Nothing here is on the audio path.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "discover.h"
#include "mpc_settings.h"
#include "pp_gen.h"

#if defined(__aarch64__)
#define MY_ELF_MACHINE 183
#elif defined(__x86_64__)
#define MY_ELF_MACHINE 62
#elif defined(__arm__)
#define MY_ELF_MACHINE 40
#else
#define MY_ELF_MACHINE -1
#endif

static int elf_machine(const char *path) {
    unsigned char h[20];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    if (n < 20 || memcmp(h, "\177ELF", 4)) return -1;
    return h[18] | h[19] << 8;
}

static cJSON *load_json(const char *path) {
    char *s = slurp(path, 32 << 20);
    if (!s) return NULL;
    cJSON *j = cJSON_Parse(s);
    free(s);
    return j;
}

static int is_image(const char *n) {
    const char *d = strrchr(n, '.');
    return d && (!strcasecmp(d, ".png") || !strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg"));
}

/* ---- the installed effects ----------------------------------------------------------------------------------- */
static int by_name(const void *a, const void *b) {
    const pp_rtype_t *x = a, *y = b;
    int c = strcasecmp(x->name, y->name);
    return c ? c : strcasecmp(x->vendor, y->vendor);
}

/* ---- MPC OS 3.10.1+: "<drive>/VST2 Plugins/<Manufacturer> - VST - <Name>/" with a plugin.json (Akai's folder format).
 * 3.10.1's browser lists only these; plugins registered in MPC.settings are no longer offered (but still load in saved
 * projects), so a user who moved their effects there would otherwise see an empty PoliPlug list. ------------------- */

/* /proc/mounts writes a space in a mount point as \040 (and tab, newline, backslash the same way) */
static void unoctal(char *s) {
    char *o = s;
    for (char *i = s; *i;) {
        if (i[0] == '\\' && i[1] >= '0' && i[1] <= '3' && i[2] >= '0' && i[2] <= '7' && i[3] >= '0' && i[3] <= '7') {
            *o++ = (char)((i[1] - '0') * 64 + (i[2] - '0') * 8 + (i[3] - '0'));
            i += 4;
        } else *o++ = *i++;
    }
    *o = 0;
}

static void add_root(char roots[][PATH_MAX], int *n, int max, const char *drive) {
    char dir[PATH_MAX], real[PATH_MAX];
    struct stat st;
    snprintf(dir, sizeof dir, "%s/VST2 Plugins", strcmp(drive, "/") ? drive : "");
    if (*n >= max || stat(dir, &st) || !S_ISDIR(st.st_mode) || !realpath(dir, real)) return;
    for (int k = 0; k < *n; k++) if (!strcmp(roots[k], real)) return;   /* a bind mount of the same drive */
    snprintf(roots[(*n)++], PATH_MAX, "%s", real);
}

/* every "VST2 Plugins" folder: the root of each mounted drive (internal, SD card, USB), plus the internal drive's
 * known places in case /proc/mounts can't be read. Tests set PP_VST2_ROOTS (':'-separated drive roots; "" = none). */
static int vst2_folders(char roots[][PATH_MAX], int max) {
    int n = 0;
    const char *env = getenv("PP_VST2_ROOTS");
    if (env) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s", env);
        for (char *save = NULL, *d = strtok_r(buf, ":", &save); d; d = strtok_r(NULL, ":", &save)) add_root(roots, &n, max, d);
        return n;
    }
    static const char *const FIXED[] = {"/nvme", "/media/az01-internal", "/storage", "/sdcard", NULL};
    for (int i = 0; FIXED[i]; i++) add_root(roots, &n, max, FIXED[i]);
    FILE *f = fopen("/proc/mounts", "r");
    char line[2048], dev[512], mnt[1024];
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, "%511s %1023s", dev, mnt) == 2) { unoctal(mnt); add_root(roots, &n, max, mnt); }
    if (f) fclose(f);
    return n;
}

static int json_uid_is(cJSON *u, uint32_t want) {   /* "PlPg", "0x506c5067", "1349275751" or 1349275751 */
    if (cJSON_IsNumber(u)) return (uint32_t)u->valuedouble == want;
    if (!cJSON_IsString(u)) return 0;
    const char *s = u->valuestring;
    if (strlen(s) == 4 && strncasecmp(s, "0x", 2))
        return ((uint32_t)(unsigned char)s[0] << 24 | (uint32_t)(unsigned char)s[1] << 16 |
                (uint32_t)(unsigned char)s[2] << 8 | (unsigned char)s[3]) == want;
    char *e;
    unsigned long v = strtoul(s, &e, 0);
    return *s && !*e && v == want;
}

/* the binary for this CPU, as plugin.json names it: file64 (aarch64) / file32 (arm), else "file"; relative and inside
 * the folder, as MPC requires */
static int json_binary(cJSON *pj, const char *dir, char *out, int len) {
#if defined(__aarch64__) || defined(__x86_64__)
    const char *key = "file64";
#else
    const char *key = "file32";
#endif
    cJSON *f = cJSON_GetObjectItemCaseSensitive(pj, key);
    if (!f && !cJSON_GetObjectItemCaseSensitive(pj, "file32") && !cJSON_GetObjectItemCaseSensitive(pj, "file64"))
        f = cJSON_GetObjectItemCaseSensitive(pj, "file");
    if (!cJSON_IsString(f) || !f->valuestring[0] || f->valuestring[0] == '/' || strstr(f->valuestring, "..")) return 0;
    return snprintf(out, len, "%s/%s", dir, f->valuestring) < len;
}

static int is_effect(cJSON *pj) {
    cJSON *ii = cJSON_GetObjectItemCaseSensitive(pj, "isInstrument"), *k = cJSON_GetObjectItemCaseSensitive(pj, "kind");
    if (cJSON_IsBool(ii)) return cJSON_IsFalse(ii);
    return cJSON_IsString(k) && !strcasecmp(k->valuestring, "effect");   /* MPC's default kind is instrument */
}

static int find_vst2(pp_rtype_t *t, int n, int max, int *over) {
    char (*roots)[PATH_MAX] = malloc(16 * sizeof *roots);   /* MPC may create several instances at once: no statics */
    int nr = roots ? vst2_folders(roots, 16) : 0;
    for (int r = 0; r < nr; r++) {
        DIR *d = opendir(roots[r]);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char dir[PATH_MAX], path[PATH_MAX + 64], file[PATH_MAX + 600];
            struct stat st;
            if (snprintf(dir, sizeof dir, "%s/%s", roots[r], e->d_name) >= (int)sizeof dir) continue;
            snprintf(path, sizeof path, "%s/Plugin Skins/TUI.json", dir);
            if (stat(path, &st)) continue;   /* no skin: nothing PoliPlug can show */
            snprintf(path, sizeof path, "%s/plugin.json", dir);
            cJSON *pj = load_json(path);
            cJSON *nm = cJSON_GetObjectItemCaseSensitive(pj, "name"), *mf = cJSON_GetObjectItemCaseSensitive(pj, "manufacturer");
            int ok = cJSON_IsString(nm) && nm->valuestring[0] && cJSON_IsString(mf) && is_effect(pj)
                     && strcmp(nm->valuestring, "PoliPlug") && !json_uid_is(cJSON_GetObjectItemCaseSensitive(pj, "uid"), 0x506c5067)
                     && json_binary(pj, dir, file, sizeof file) && elf_machine(file) == MY_ELF_MACHINE;
            for (int k = 0; ok && k < n; k++)   /* the same plugin on two drives: the first one found */
                ok = strcmp(t[k].name, nm->valuestring) || strcmp(t[k].vendor, mf->valuestring);
            if (ok && n >= max) { (*over)++; ok = 0; }
            if (ok) {
                pp_rtype_t *x = &t[n++];
                memset(x, 0, sizeof *x);
                snprintf(x->name, sizeof x->name, "%s", nm->valuestring);
                snprintf(x->vendor, sizeof x->vendor, "%s", mf->valuestring);
                snprintf(x->file, sizeof x->file, "%s", file);
                snprintf(x->skin, sizeof x->skin, "%s", dir);
            }
            cJSON_Delete(pj);
        }
        if (d) closedir(d);
    }
    free(roots);
    return n;
}

static int find_effects(pp_rtype_t *t, int max, char *note, int notelen) {
    int over = 0;
    /* the VST2 Plugins copies first: on 3.10.1 they are what MPC lists and runs (saved projects fall back to them once
     * the old copy is gone), so an effect also registered the old way below is the same one and is skipped */
    int n = find_vst2(t, 0, max, &over), nvst2 = n;
    char *xml = NULL;
    const char *env = getenv("PP_SETTINGS");   /* tests */
    if (env) xml = slurp(env, 8 << 20);
    for (int i = 0; SETTINGS[i] && !xml && !env; i++) xml = slurp(SETTINGS[i], 8 << 20);
    if (!xml) {
        if (!nvst2) snprintf(note, notelen, "Can't read MPC.settings");
        else if (over) snprintf(note, notelen, "%d more effects than PoliPlug lists (%d)", over, max);
        qsort(t, n, sizeof *t, by_name);
        return n;
    }
    unescape(xml);
    char loc[32][256];
    int nloc = locations(xml, loc, 32);
    for (const char *p = xml; (p = strstr(p, "<PLUGIN ")); p++) {
        const char *end = strstr(p, "/>");
        if (!end) break;
        char name[128], fmt[32], cat[64], man[128], file[512], inst[8], uid[32];
        attr(p, end, "name", name, sizeof name);
        attr(p, end, "format", fmt, sizeof fmt);
        attr(p, end, "category", cat, sizeof cat);
        attr(p, end, "manufacturer", man, sizeof man);
        attr(p, end, "file", file, sizeof file);
        attr(p, end, "isInstrument", inst, sizeof inst);
        attr(p, end, "uid", uid, sizeof uid);
        /* the list is XML escaped inside an XML attribute: a name with & < > " was escaped twice, once undone above */
        unescape(name); unescape(man); unescape(file);
        if (strcmp(fmt, "VST") || !strcmp(inst, "1") || strcasestr(cat, "synth") || strcasestr(cat, "instrument")) continue;
        if (!strcmp(name, "PoliPlug") || !strcasecmp(uid, "506c5067")) continue;   /* not ourselves ('PlPg') */
        if (!file[0] || elf_machine(file) != MY_ELF_MACHINE) continue;
        char folder[300], where[600];
        snprintf(folder, sizeof folder, "%s - VST - %s", man, name);
        if (!skin_in(loc, nloc, folder, where, sizeof where)) continue;
        int dup = 0;
        for (int k = 0; k < n; k++) dup |= !strcmp(t[k].file, file);
        for (int k = 0; k < nvst2; k++) dup |= !strcmp(t[k].name, name) && !strcmp(t[k].vendor, man);
        if (dup) continue;   /* the same .so in two plugin lists, or an old registration of a VST2 Plugins copy */
        if (n >= max) { over++; continue; }
        pp_rtype_t *r = &t[n++];
        memset(r, 0, sizeof *r);
        snprintf(r->name, sizeof r->name, "%s", name);
        snprintf(r->vendor, sizeof r->vendor, "%s", man);
        snprintf(r->file, sizeof r->file, "%s", file);
        snprintf(r->skin, sizeof r->skin, "%s", where);
    }
    free(xml);
    if (over) snprintf(note, notelen, "%d more effects than PoliPlug lists (%d)", over, max);
    qsort(t, n, sizeof *t, by_name);
    return n;
}

/* ---- stable view ranges ---------------------------------------------------------------------------------------- */
typedef struct { int view0, nviews; char key[140]; } reg_t;

static int read_registry(const char *path, reg_t *r, int max) {
    FILE *f = fopen(path, "r");
    int n = 0;
    if (!f) return 0;
    char line[300];
    while (n < max && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *a = strchr(line, '\t'), *b = a ? strchr(a + 1, '\t') : NULL;
        if (!b) continue;
        r[n].view0 = atoi(line);
        r[n].nviews = atoi(a + 1);
        snprintf(r[n].key, sizeof r[n].key, "%s", b + 1);
        int ok = r[n].view0 >= 1 && r[n].nviews >= 1 && r[n].view0 + r[n].nviews <= PP_MAX_VIEWS;
        for (int k = 0; ok && k < n; k++)   /* a damaged file: no two effects on one page, one range per effect */
            ok = !strcmp(r[k].key, r[n].key) ? 0 : r[n].view0 + r[n].nviews <= r[k].view0 || r[k].view0 + r[k].nviews <= r[n].view0;
        if (ok) n++;
    }
    fclose(f);
    return n;
}

/* ---- skin merge ------------------------------------------------------------------------------------------------ */
static int has_ext(const char *n, const char *ext) {
    size_t a = strlen(n), b = strlen(ext);
    return a > b && !strcasecmp(n + a - b, ext);
}

/* the path from directory `from` to file `to` (both absolute, no symlinks): "../../X - VST - Y/Plugin Skins/a.png" */
static char *relpath(const char *from, const char *to) {
    size_t i = 0, common = 0;
    while (from[i] && to[i] && from[i] == to[i]) { if (from[i] == '/') common = i; i++; }
    if (!from[i] && to[i] == '/') common = i;   /* `to` is inside `from` */
    int ups = 0;
    for (const char *p = from + common; *p; p++) if (*p == '/') ups++;
    if (from[common] == '/' && !from[common + 1]) ups = 0;
    char *o = malloc(3 * ups + strlen(to) + 2);
    o[0] = 0;
    for (int k = 0; k < ups; k++) strcat(o, "../");
    strcat(o, to + common + 1);
    return o;
}

typedef struct {
    int view0, edit_base;
    char prefix[24];
    cJSON *names;       /* the hosted plugin's local definition keys (an object used as a set) */
    const char *src;    /* its Plugin Skins folder */
    const char *ours;   /* PoliPlug's Plugin Skins folder (real path) */
    int bad;            /* a Parameter >= PP_MAX_EDIT */
} rw_t;

static char *rewrite_str(const char *s, rw_t *w) {
    char path[1100];
    struct stat st;
    if (cJSON_GetObjectItemCaseSensitive(w->names, s)) {   /* one of its own definitions: renamed */
        char *o = malloc(strlen(s) + strlen(w->prefix) + 1);
        sprintf(o, "%s%s", w->prefix, s);
        return o;
    }
    if (s[0] != '/' && (is_image(s) || has_ext(s, ".json"))) {   /* a file of its own skin (or a relative import) */
        char real[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", w->src, s);
        if (!stat(path, &st) && realpath(path, real)) {
            if (has_ext(s, ".json")) return strdup(real);   /* imports: absolute paths work (Generic Knob Overlay) */
            return relpath(w->ours, real);
        }
        /* missing from its skin: still pointed at its own folder, never left bare (it would then pick up a file of OURS
         * with the same name, e.g. sh_bg_0.png) */
        char dir[PATH_MAX];
        if (is_image(s) && realpath(w->src, dir)) {
            snprintf(path, sizeof path, "%s/%s", dir, s);
            return relpath(w->ours, path);
        }
    }
    if (!strstr(s, "Parameter ")) return NULL;
    size_t cap = strlen(s) * 2 + 32, k = 0;
    char *o = malloc(cap);
    for (const char *p = s; *p;) {
        if (!strncmp(p, "Parameter ", 10) && p[10] >= '0' && p[10] <= '9' && (p == s || !((p[-1] >= 'A' && p[-1] <= 'Z') ||
                                                                                     (p[-1] >= 'a' && p[-1] <= 'z')))) {
            char *e;
            long v = strtol(p + 10, &e, 10);
            if (v >= PP_MAX_EDIT) w->bad = 1;
            k += snprintf(o + k, cap - k, "Parameter %ld", v >= 0 && v < PP_MAX_EDIT ? (long)PI_E(v) : w->edit_base + v);
            p = e;
        } else o[k++] = *p++;
        if (k + 24 >= cap) { cap *= 2; o = realloc(o, cap); }
    }
    o[k] = 0;
    return o;
}

static void rewrite(cJSON *j, rw_t *w) {
    for (cJSON *c = j; c; c = c->next) {
        if (cJSON_IsString(c)) {
            char *n = rewrite_str(c->valuestring, w);
            if (n) { free(c->valuestring); c->valuestring = n; }
        }
        if (c->child) rewrite(c->child, w);
    }
}

static void gate(cJSON *comp, const char *handle) {
    cJSON *b = cJSON_GetObjectItemCaseSensitive(comp, "bounds");
    if (!b) return;
    cJSON_ReplaceItemInObjectCaseSensitive(b, "showWhenDataModelInvalid", cJSON_CreateString("Show"));
    cJSON *h = cJSON_GetObjectItemCaseSensitive(b, "additionalInvalidatingHandles");
    if (!cJSON_IsArray(h)) { h = cJSON_CreateArray(); cJSON_ReplaceItemInObjectCaseSensitive(b, "additionalInvalidatingHandles", h); }
    cJSON_AddItemToArray(h, cJSON_CreateString(handle));
}

static cJSON *backdrop(void) {   /* a white picture under a hosted page, so no rack art shows through (pp_backdrop.png) */
    return cJSON_Parse("{\"version\":2,\"componentData\":{\"version\":1,\"name\":\"Backdrop\",\"type\":\"Image\",\"data\":"
                       "{\"version\":2,\"imageType\":\"Regular\",\"colour\":\"0\",\"image\":\"pp_backdrop.png\"}},"
                       "\"handle remapping\":{\"version\":1,\"map\":[]},\"bounds\":{\"version\":2,\"acceptsHWFocus\":\"No\","
                       "\"showWhenDataModelInvalid\":\"Show\",\"whenVisible\":\"Always\",\"boundsType\":\"Absolute\","
                       "\"bounds\":\"0 0 1280 628\",\"additionalInvalidatingHandles\":[]}}");
}

static cJSON *find_def(cJSON *defs, const char *key) {
    cJSON *d;
    cJSON_ArrayForEach(d, defs) {
        cJSON *k = cJSON_GetObjectItemCaseSensitive(d, "key");
        if (cJSON_IsString(k) && !strcmp(k->valuestring, key)) return d;
    }
    return NULL;
}

/* a tab's components: its page inline ("componentDefinition") or the local definition it names ("componentName") */
static cJSON *tab_components(cJSON *tab, cJSON *defs) {
    cJSON *inl = cJSON_GetObjectItemCaseSensitive(tab, "componentDefinition");
    if (cJSON_IsObject(inl)) return cJSON_GetObjectItemCaseSensitive(inl, "componentsData");
    cJSON *cn = cJSON_GetObjectItemCaseSensitive(tab, "componentName");
    cJSON *page = cJSON_IsString(cn) ? find_def(defs, cn->valuestring) : NULL;
    return page ? cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(page, "value"), "componentsData") : NULL;
}

/* the hosted plugin's pages (tabs) and their Q-Link maps; returns the page count (0: no usable skin) */
static int read_pages(pp_rtype_t *t, cJSON **tui_out) {
    char path[700];
    snprintf(path, sizeof path, "%s/Plugin Skins/TUI.json", t->skin);
    cJSON *tui = load_json(path);
    cJSON *tabs = tui ? cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(tui, "pageData"), "tabs") : NULL;
    int n = cJSON_GetArraySize(tabs);
    if (n > PP_MAX_TABS) n = PP_MAX_TABS;
    memset(t->qmap, -1, sizeof t->qmap);
    snprintf(path, sizeof path, "%s/Plugin Skins/Q-Links.json", t->skin);
    cJSON *ql = load_json(path);
    cJSON *map = ql ? cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(ql, "Screen Mode Q-Links"), "map") : NULL;
    for (int i = 0; i < n; i++) {
        cJSON *tab = cJSON_GetArrayItem(tabs, i), *e;
        int fk = cJSON_GetObjectItemCaseSensitive(tab, "fnKeyIndex") ? cJSON_GetObjectItemCaseSensitive(tab, "fnKeyIndex")->valueint : 0;
        int fs = cJSON_GetObjectItemCaseSensitive(tab, "fnKeySubIndex") ? cJSON_GetObjectItemCaseSensitive(tab, "fnKeySubIndex")->valueint : 0;
        cJSON_ArrayForEach(e, map) {
            cJSON *T = cJSON_GetObjectItemCaseSensitive(e, "Tab"), *S = cJSON_GetObjectItemCaseSensitive(e, "SubTab");
            if (!T || !S || T->valueint != fk + 1 || S->valueint != fs + 1) continue;
            cJSON *q = cJSON_GetObjectItemCaseSensitive(e, "Q-Links");
            for (int k = 0; k < 16; k++) {
                char key[16];
                snprintf(key, sizeof key, "Q-Link %d", k + 1);
                cJSON *v = cJSON_GetObjectItemCaseSensitive(q, key);
                t->qmap[i][k] = cJSON_IsNumber(v) && v->valueint >= 0 && v->valueint < PP_MAX_EDIT ? (short)v->valueint : -1;
            }
            break;
        }
    }
    cJSON_Delete(ql);
    /* tabs whose page doesn't resolve (a missing definition): with none left, the skin has nothing to show */
    int usable = 0;
    for (int i = 0; i < n; i++) usable += tab_components(cJSON_GetArrayItem(tabs, i), cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(tui, "pageData"), "componentDefinitions"),
        "localComponentDefinitions")) != NULL;
    if (!usable) n = 0;
    /* pages drawn the same: merged once (a 1 MB page three times over made Shapey EQ's share of our skin 3x) */
    cJSON *hdefs = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(tui ? tui : NULL,
                       "pageData"), "componentDefinitions"), "localComponentDefinitions");
    char *sig[PP_MAX_TABS] = {0};
    for (int i = 0; i < n; i++) {
        cJSON *comps = tab_components(cJSON_GetArrayItem(tabs, i), hdefs);
        sig[i] = comps ? cJSON_PrintUnformatted(comps) : NULL;
        t->alias[i] = (signed char)i;
        for (int j = 0; j < i; j++)
            if (sig[i] && sig[j] && !strcmp(sig[i], sig[j])) { t->alias[i] = (signed char)j; break; }
    }
    for (int i = 0; i < n; i++) free(sig[i]);
    /* a skin binding a parameter past our last editor proxy can't be shown: left out of the list (its pages too) */
    char *all = tui ? cJSON_PrintUnformatted(tui) : NULL;
    for (const char *q = all; q && (q = strstr(q, "Parameter ")); q += 10)
        if (q[10] >= '0' && q[10] <= '9' && atoi(q + 10) >= PP_MAX_EDIT) { n = -1; break; }
    free(all);
    if (tui_out) *tui_out = tui;
    else cJSON_Delete(tui);
    return n;
}

static int merge(const char *skins, pp_rtype_t *t, int n, cJSON **tuis, char *note, int notelen) {
    char path[700];
    snprintf(path, sizeof path, "%s/pp_base.json", skins);
    cJSON *base = load_json(path);
    if (!base) { snprintf(note, notelen, "PoliPlug's skin is missing pp_base.json"); return -1; }
    cJSON *pd = cJSON_GetObjectItemCaseSensitive(base, "pageData");
    cJSON *cd = cJSON_GetObjectItemCaseSensitive(pd, "componentDefinitions");
    cJSON *defs = cJSON_GetObjectItemCaseSensitive(cd, "localComponentDefinitions");
    cJSON *imports = cJSON_GetObjectItemCaseSensitive(cd, "importFiles");
    cJSON *tab0 = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pd, "tabs"), 0);
    cJSON *rack = find_def(defs, cJSON_GetObjectItemCaseSensitive(tab0, "componentName")->valuestring);
    cJSON *kids = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(rack, "value"), "componentsData");
    char bar[32];
    snprintf(bar, sizeof bar, "Parameter %d", PI_VIEW_BACK);
    int at = -1, i = 0;
    cJSON *c;
    cJSON_ArrayForEach(c, kids) {   /* the editor bar (Back): hosted pages go just below it */
        cJSON *m, *map = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(c, "handle remapping"), "map");
        cJSON_ArrayForEach(m, map) {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(m, "value");
            if (cJSON_IsString(v) && !strcmp(v->valuestring, bar) && at < 0) at = i;
        }
        i++;
    }
    if (at < 0) { cJSON_Delete(base); snprintf(note, notelen, "PoliPlug's skin has no Back button"); return -1; }
    for (int k = 0; k < n; k++) {
        cJSON *htui = tuis[k];
        if (!htui) continue;
        rw_t w = {.view0 = t[k].view0, .edit_base = PI_E(0)};
        snprintf(w.prefix, sizeof w.prefix, "pp_h%d__", t[k].view0);
        char src[700], ours[PATH_MAX];
        snprintf(src, sizeof src, "%s/Plugin Skins", t[k].skin);
        w.src = src;
        if (!realpath(skins, ours)) snprintf(ours, sizeof ours, "%s", skins);
        w.ours = ours;
        cJSON *hpd = cJSON_GetObjectItemCaseSensitive(htui, "pageData");
        cJSON *hcd = cJSON_GetObjectItemCaseSensitive(hpd, "componentDefinitions");
        cJSON *hdefs = cJSON_GetObjectItemCaseSensitive(hcd, "localComponentDefinitions");
        cJSON *htabs = cJSON_GetObjectItemCaseSensitive(hpd, "tabs");
        w.names = cJSON_CreateObject();
        cJSON *pages = cJSON_CreateObject(), *hd, *tab;
        cJSON_ArrayForEach(hd, hdefs) {
            cJSON *key = cJSON_GetObjectItemCaseSensitive(hd, "key");
            if (cJSON_IsString(key)) cJSON_AddTrueToObject(w.names, key->valuestring);
        }
        cJSON_ArrayForEach(tab, htabs) {
            cJSON *cn = cJSON_GetObjectItemCaseSensitive(tab, "componentName");
            if (cJSON_IsString(cn)) cJSON_AddTrueToObject(pages, cn->valuestring);
        }
        cJSON *add = cJSON_CreateArray();   /* the hosted pages' components, gated, in order */
        int ti = 0;
        cJSON_ArrayForEach(tab, htabs) {
            if (ti >= t[k].nviews) break;
            if (t[k].alias[ti] != ti) { ti++; continue; }   /* drawn like an earlier page: that one is shown */
            cJSON *comps = tab_components(tab, hdefs);
            char handle[64];
            snprintf(handle, sizeof handle, "IndexedEnabling/%d/%d/Parameter %d", t[k].view0 + ti, PP_MAX_VIEWS, PI_VIEW);
            cJSON *bd = backdrop();
            gate(bd, handle);
            cJSON_AddItemToArray(add, bd);
            cJSON *comp;
            cJSON_ArrayForEach(comp, comps) {
                cJSON *cp = cJSON_Duplicate(comp, 1);
                rewrite(cp, &w);
                gate(cp, handle);
                cJSON_AddItemToArray(add, cp);
            }
            ti++;
        }
        if (w.bad) {   /* binds a parameter past the last editor proxy: leave this plugin's pages out */
            snprintf(note, notelen, "%s: too many parameters for PoliPlug", t[k].name);
            cJSON_Delete(add); cJSON_Delete(pages); cJSON_Delete(w.names);
            continue;
        }
        cJSON_ArrayForEach(hd, hdefs) {   /* its control definitions (knobs, toggles...), renamed */
            cJSON *key = cJSON_GetObjectItemCaseSensitive(hd, "key");
            if (!cJSON_IsString(key) || cJSON_GetObjectItemCaseSensitive(pages, key->valuestring)) continue;
            cJSON *cp = cJSON_Duplicate(hd, 1);
            rewrite(cp, &w);
            cJSON_AddItemToArray(defs, cp);
        }
        cJSON *imp;
        cJSON_ArrayForEach(imp, cJSON_GetObjectItemCaseSensitive(hcd, "importFiles")) {
            int have = 0;
            cJSON *x;
            cJSON_ArrayForEach(x, imports) have |= cJSON_IsString(x) && cJSON_IsString(imp) && !strcmp(x->valuestring, imp->valuestring);
            if (!have && cJSON_IsString(imp)) cJSON_AddItemToArray(imports, cJSON_CreateString(imp->valuestring));
        }
        while (cJSON_GetArraySize(add)) {   /* move them in before the editor bar */
            cJSON *it = cJSON_DetachItemFromArray(add, 0);
            cJSON_InsertItemInArray(kids, at++, it);
        }
        cJSON_Delete(add); cJSON_Delete(pages); cJSON_Delete(w.names);
    }
    char *out = cJSON_PrintUnformatted(base);
    cJSON_Delete(base);
    if (!out) return -1;
    char tmp[720], fin[720];
    snprintf(tmp, sizeof tmp, "%s/TUI.json.tmp", skins);
    snprintf(fin, sizeof fin, "%s/TUI.json", skins);
    FILE *f = fopen(tmp, "w");
    int ok = f && fputs(out, f) >= 0;
    if (f && fclose(f)) ok = 0;
    free(out);
    if (!ok || rename(tmp, fin)) { remove(tmp); snprintf(note, notelen, "Couldn't write PoliPlug's skin (%s)", strerror(errno)); return -1; }
    return 0;
}

int pp_discover(const char *plugin_dir, pp_rtype_t *types, int max, int *skin_changed, char *note, int notelen) {
    char skins[600], regp[700], stampp[700];
    *skin_changed = 0;
    note[0] = 0;
    snprintf(skins, sizeof skins, "%s/Plugin Skins", plugin_dir);
    snprintf(regp, sizeof regp, "%s/pp_registry.txt", skins);
    snprintf(stampp, sizeof stampp, "%s/pp_stamp.txt", skins);
    if (max > PP_MAX_TYPES) max = PP_MAX_TYPES;
    int n = find_effects(types, max, note, notelen);
    cJSON *tuis[PP_MAX_TYPES] = {0};
    for (int i = 0; i < n; i++) types[i].nviews = read_pages(&types[i], &tuis[i]);

    /* stable view ranges */
    reg_t reg[PP_MAX_VIEWS];
    int nr = read_registry(regp, reg, PP_MAX_VIEWS), next = 1, regdirty = 0;
    for (int r = 0; r < nr; r++) if (reg[r].view0 + reg[r].nviews > next) next = reg[r].view0 + reg[r].nviews;
    int kept = 0;
    for (int i = 0; i < n; i++) {
        char key[140];
        snprintf(key, sizeof key, "%s\t%s", types[i].vendor, types[i].name);
        if (types[i].nviews < 0 && !note[0]) snprintf(note, notelen, "%.30s: over %d parameters", types[i].name, PP_MAX_EDIT);
        if (types[i].nviews <= 0) { types[i].view0 = 0; continue; }
        int r;
        for (r = 0; r < nr && strcmp(reg[r].key, key); r++) {}
        if (r < nr && reg[r].nviews >= types[i].nviews) { types[i].view0 = reg[r].view0; }
        else if (next + types[i].nviews <= PP_MAX_VIEWS && nr < PP_MAX_VIEWS) {
            if (r == nr) nr++;
            reg[r].view0 = next; reg[r].nviews = types[i].nviews;
            snprintf(reg[r].key, sizeof reg[r].key, "%s", key);
            types[i].view0 = next;
            next += types[i].nviews;
            regdirty = 1;
        } else { types[i].view0 = 0; if (!note[0]) snprintf(note, notelen, "No room left for %s's pages", types[i].name); }
    }
    /* drop the types without pages */
    for (int i = 0; i < n; i++) {
        if (types[i].view0 > 0) {
            if (kept != i) { types[kept] = types[i]; tuis[kept] = tuis[i]; tuis[i] = NULL; }
            kept++;
        } else { cJSON_Delete(tuis[i]); tuis[i] = NULL; }
    }
    n = kept;
    if (regdirty) {
        FILE *f = fopen(regp, "w");
        for (int r = 0; f && r < nr; r++) fprintf(f, "%d\t%d\t%s\n", reg[r].view0, reg[r].nviews, reg[r].key);
        if (f) fclose(f);
    }

    /* rewrite the skin only when something changed: the effect list, their skins, or our own base */
    char stamp[8192];
    int o = 0;
    struct stat st;
    char p[1100];
    snprintf(p, sizeof p, "%s/pp_base.json", skins);
    if (!stat(p, &st)) o += snprintf(stamp + o, sizeof stamp - o, "base %lld %lld\n", (long long)st.st_size, (long long)st.st_mtime);
    for (int i = 0; i < n && o < (int)sizeof stamp - 200; i++) {
        snprintf(p, sizeof p, "%s/Plugin Skins/TUI.json", types[i].skin);
        long long sz = 0, mt = 0;
        if (!stat(p, &st)) { sz = st.st_size; mt = st.st_mtime; }
        o += snprintf(stamp + o, sizeof stamp - o, "%d %d %s %lld %lld\n", types[i].view0, types[i].nviews, types[i].skin, sz, mt);
    }
    char *old = slurp(stampp, sizeof stamp);
    char tui_path[700];
    snprintf(tui_path, sizeof tui_path, "%s/TUI.json", skins);
    /* ...and our TUI.json as we last wrote it: a reinstall puts the bare rack skin back (its zip may keep the old
     * pp_base.json date, and pp_stamp.txt may survive it), which must be merged again */
    int o0 = o;
    if (!stat(tui_path, &st)) snprintf(stamp + o, sizeof stamp - o, "tui %lld %lld\n", (long long)st.st_size, (long long)st.st_mtime);
    if (!old || strcmp(old, stamp) || stat(tui_path, &st)) {
        if (!merge(skins, types, n, tuis, note, notelen)) {
            *skin_changed = 1;
            if (!stat(tui_path, &st)) snprintf(stamp + o0, sizeof stamp - o0, "tui %lld %lld\n", (long long)st.st_size, (long long)st.st_mtime);
            FILE *f = fopen(stampp, "w");
            if (f) { fputs(stamp, f); fclose(f); }
        }
    }
    free(old);
    for (int i = 0; i < n; i++) cJSON_Delete(tuis[i]);
    if (!note[0]) snprintf(note, notelen, "%d effects found", n);
    return n;
}
