/* PoliPlug: the effects installed on this MPC that it can host, and its skin rebuilt to show them (discover.c). */
#pragma once

#define PP_MAX_TYPES 48     /* installed effects PoliPlug lists */
#define PP_MAX_VIEWS 96     /* the "view" parameter's option count: 0 = the rack, 1.. = hosted pages (generate.py VIEWS) */
#define PP_MAX_TABS 8       /* pages of one hosted plugin */

typedef struct {
    char name[64], vendor[64];
    char file[512];          /* its .so: from its plugin.json in VST2 Plugins (MPC OS 3.10.1+), else as MPC.settings registers it */
    char skin[512];          /* its folder: <skin>/Plugin Skins/TUI.json */
    int view0, nviews;       /* its pages are views view0 .. view0+nviews-1 */
    short qmap[PP_MAX_TABS][16];   /* per page: the hosted parameter on Q-Link 1..16 (-1 none). 1.3.0 had signed char:
                                      a Q-Link on a hosted parameter over 127 (Shapey EQ's) was lost */
    signed char alias[PP_MAX_TABS];      /* per page: the first page drawn the same (sub-tabs that only switch the Q-Link
                                          * bank, like Shapey EQ's three): its components are merged once, shown on that view */
} pp_rtype_t;

/* Once per process: read MPC.settings, keep the effects that are VST .so files for this CPU with a skin, give each a
 * stable range of views (Plugin Skins/pp_registry.txt), and, when that set changed, rewrite Plugin Skins/TUI.json from
 * Plugin Skins/pp_base.json (the rack) plus every effect's pages. Returns the number of types (sorted by name);
 * *skin_changed is set when TUI.json was rewritten (MPC shows it from the next insert, or the next MPC start).
 * plugin_dir = the folder holding poli_plug.so. note gets a one-line summary or the first problem. */
int pp_discover(const char *plugin_dir, pp_rtype_t *types, int max, int *skin_changed, char *note, int notelen);
