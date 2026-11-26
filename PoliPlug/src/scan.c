/* PoliPlug scout: once per MPC start, on its own thread, write what this MPC has that PoliPlug could host.
 *
 *   /nvme/MPC Documents/PoliPlug-scan.txt   (else /storage/MPC Documents, /sdcard/MPC Documents, else next to poli_plug.so)
 *
 * Read-only: it reads MPC.settings and looks at files; it never loads a plugin, so it can't take MPC down.
 * For every <PLUGIN .../> in MPC.settings (all plugin lists, factory ones included) it reports the name, format
 * (VST = a .so file PoliPlug can load; anything else, e.g. "MPC", is built into the MPC program), effect or instrument,
 * whether the file exists and is a 64-bit ARM library, and whether its skin folder exists in a Synths location.
 * It also lists the factory skin folders and any .so files under the factory content folder and /usr/lib/vst, so
 * stock effects that ship as files would show up even if the plugin list doesn't name them. */
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "mpc_settings.h"

static const char *DOCS[] = {"/nvme/MPC Documents", "/storage/MPC Documents", "/sdcard/MPC Documents", NULL};

static void find_so(FILE *o, const char *dir, int depth, int *count) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && *count < 400) {
        if (e->d_name[0] == '.') continue;
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(p, &st)) continue;
        if (S_ISDIR(st.st_mode) && depth < 4) find_so(o, p, depth + 1, count);
        else if (S_ISREG(st.st_mode) && strlen(e->d_name) > 3 && !strcmp(e->d_name + strlen(e->d_name) - 3, ".so")) {
            fprintf(o, "  %s  (%s)\n", p, elf_kind(p));
            (*count)++;
        }
    }
    closedir(d);
}

static void write_report(FILE *o, const char *plugdir) {
    time_t t = time(NULL);
    fprintf(o, "PoliPlug scan, %s", ctime(&t));
    fprintf(o, "PoliPlug folder: %s\n\n", plugdir);
    char *xml = NULL;
    const char *sp = NULL;
    if (getenv("PP_SCAN_SETTINGS") && (xml = slurp(getenv("PP_SCAN_SETTINGS"), 8 << 20))) sp = getenv("PP_SCAN_SETTINGS");   /* tests */
    for (int i = 0; SETTINGS[i] && !xml; i++) if ((xml = slurp(SETTINGS[i], 8 << 20))) sp = SETTINGS[i];
    if (!xml) { fprintf(o, "MPC.settings not found or not readable\n"); return; }
    fprintf(o, "Settings: %s\n", sp);
    unescape(xml);
    char loc[32][256];
    int nloc = locations(xml, loc, 32);
    fprintf(o, "Skin locations searched:\n");
    for (int i = 0; i < nloc; i++) fprintf(o, "  %s\n", loc[i]);

    fprintf(o, "\n== Registered plugins (every <PLUGIN> in MPC.settings) ==\n");
    fprintf(o, "HOSTABLE = an effect that is a 64-bit .so file with a skin folder: PoliPlug can load it and show its skin.\n\n");
    int n = 0, host = 0;
    for (const char *p = xml; (p = strstr(p, "<PLUGIN ")); p++) {
        const char *end = strstr(p, "/>");
        if (!end) break;
        const char *list = NULL;   /* which plugin list this entry sits in: the nearest VALUE name before it */
        for (const char *q = p; q > xml && !list; q--) if (!strncmp(q, "<VALUE name=\"", 13)) list = q + 13;
        char lname[64] = "?";
        if (list) snprintf(lname, sizeof lname, "%.*s", (int)(strchr(list, '"') ? strchr(list, '"') - list : 1), list);
        char name[128], fmt[32], cat[64], man[128], file[512], inst[8], nin[8], nout[8];
        attr(p, end, "name", name, sizeof name);
        attr(p, end, "format", fmt, sizeof fmt);
        attr(p, end, "category", cat, sizeof cat);
        attr(p, end, "manufacturer", man, sizeof man);
        attr(p, end, "file", file, sizeof file);
        attr(p, end, "isInstrument", inst, sizeof inst);
        attr(p, end, "numInputs", nin, sizeof nin);
        attr(p, end, "numOutputs", nout, sizeof nout);
        int effect = strcmp(inst, "1") && strcasestr(cat, "synth") == NULL;
        const char *kind = file[0] ? elf_kind(file) : "no file";
        char folder[300], where[600] = "";
        snprintf(folder, sizeof folder, "%s - VST - %s", man, name);
        int skin = skin_in(loc, nloc, folder, where, sizeof where);
        int ok = effect && !strcmp(fmt, "VST") && !strcmp(kind, "aarch64 .so") && skin;
        host += ok;
        fprintf(o, "%s %-28s [%s] format=%s %s in=%s out=%s list=%s\n    file: %s (%s)\n    skin: %s\n",
                ok ? "HOSTABLE" : "--      ", name, man, fmt[0] ? fmt : "?", effect ? "effect" : "instrument", nin, nout, lname,
                file[0] ? file : "-", kind, skin ? where : "(no skin folder found)");
        n++;
    }
    fprintf(o, "\n%d plugin entries, %d hostable effects.\n", n, host);

    fprintf(o, "\n== Factory skin folders (%s) ==\n", FACTORY);
    DIR *d = opendir(FACTORY);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) if (e->d_name[0] != '.') fprintf(o, "  %s\n", e->d_name);
        closedir(d);
    } else fprintf(o, "  (not readable)\n");

    fprintf(o, "\n== .so files in the factory content and VST folders ==\n");
    int c = 0;
    find_so(o, FACTORY, 0, &c);
    find_so(o, "/usr/lib/vst", 0, &c);
    find_so(o, "/usr/local/lib/vst", 0, &c);
    if (!c) fprintf(o, "  none: the stock effects are not separate files\n");
    free(xml);
}

static char g_scan_dir[512];

static void *scan_thread(void *arg) {
    (void)arg;
    char path[700];
    FILE *o = NULL;
    for (int i = 0; DOCS[i] && !o; i++) {
        struct stat st;
        if (stat(DOCS[i], &st) || !S_ISDIR(st.st_mode)) continue;
        snprintf(path, sizeof path, "%s/PoliPlug-scan.txt", DOCS[i]);
        o = fopen(path, "w");
    }
    if (!o) { snprintf(path, sizeof path, "%s/PoliPlug-scan.txt", g_scan_dir); o = fopen(path, "w"); }
    if (!o) return NULL;
    write_report(o, g_scan_dir);
    fclose(o);
    return NULL;
}

/* called from create(); the scan runs once per MPC start, on its own detached thread (off the audio and screen threads) */
void pp_scan_once(const char *plugin_dir) {
    static int started;
    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    snprintf(g_scan_dir, sizeof g_scan_dir, "%s", plugin_dir ? plugin_dir : ".");
    char *slash = strrchr(g_scan_dir, '/');   /* create() gets <plugin folder>/hosted: report the plugin folder */
    if (slash && !strcmp(slash, "/hosted")) *slash = 0;
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (getenv("PP_NO_SCAN") == NULL) pthread_create(&t, &a, scan_thread, NULL);
    pthread_attr_destroy(&a);
}
