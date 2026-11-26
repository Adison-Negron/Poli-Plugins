#!/usr/bin/env python3
"""PoliPlug: run by gen_vst.py after the rack skin is built (vst.json "skin_post"):

    python3 skin_post.py "<build>/skin/Poli - VST - PoliPlug"

Keeps a copy of the built TUI.json as Plugin Skins/pp_base.json. On the device, PoliPlug (src/discover.c) rewrites
TUI.json from pp_base.json plus the pages of every installed effect it can host, so the effects' own skins show inside
PoliPlug (MPC itself only ever shows the skin of the plugin it inserted). The shipped TUI.json is the rack alone, which
is what MPC shows until that first rewrite. Also checks the parts discover.c relies on are there.
"""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


KNOB_STRIPS = ("knob_row.png", "knob_sc.png", "knob_mk.png")   # 1.5.0: + the macro knobs (r=40)   # 1.4.0: a row's Send (r=26), the sidechain panel (r=34); 1.3's r=56 is gone


def own_knobs(skins, tui):
    """PoliPlug's knobs: the builder always makes a 128-frame strip and tells MPC one fewer (numFrames 127), which makes
    every frame a little too tall on the Live III (the knob drifts as it turns; MPC_PLUGIN_TOOLCHAIN.md section 5). Swap
    in our own strips (generate.py: 81 frames of the knob drawn at 2r+8 px, picked by width) padded into the builder's
    2r+10 px frames, and give MPC the EXACT frame count."""
    from PIL import Image
    srcs = {}
    for name in KNOB_STRIPS:
        im = Image.open(os.path.join(HERE, "skin_assets", name)).convert("RGBA")
        srcs[im.width + 2] = (name, im)
    done = {}

    def walk(o):
        if isinstance(o, dict):
            fs = str(o.get("filmStrip", ""))
            if o.get("knobType") == "FilmStrip" and fs.startswith("sh_knob_"):
                png = os.path.join(skins, fs)
                if fs not in done:
                    fw = Image.open(png).width
                    if fw in srcs:
                        name, src = srcs[fw]
                        box = src.width
                        n = src.height // box
                        out = Image.new("RGBA", (fw, fw * n), (0, 0, 0, 0))
                        for j in range(n):
                            out.paste(src.crop((0, j * box, box, (j + 1) * box)), (1, j * fw + 1))
                        out.save(png, optimize=True)
                        done[fs] = (n, name)
                        print("skin_post: %s <- %s (%d frames, %d px)" % (fs, name, n, fw * n))
                    else:
                        done[fs] = None
                if done[fs]:
                    o["numFrames"] = done[fs][0]
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(tui)
    got = sorted(v[1] for v in done.values() if v)
    if got != sorted(KNOB_STRIPS):
        raise SystemExit("skin_post: knob strips swapped %s, want %s" % (got, sorted(KNOB_STRIPS)))


def grow_knob_boxes(tui):
    """The Send knob shows its value inside it (vy < 0, ns=0): shadow_skin then sizes the knob's component to the value
    text's bottom, which is above the bottom of the knob picture, and MPC clips children to their parent: the knob was
    cut off at the bottom on the Live III (user, 2026-11-05; Poli-Multiband had the same, fixed the same way). Make the
    knob definition's box (and its Focus) and every placed knob at least as tall as its lowest child."""
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]
    need = {}
    for d in defs:
        if not d["key"].startswith("shKnob"):
            continue
        kids = d["value"]["componentsData"]
        bottom = max(sum(xywh(c["bounds"])[1::2]) for c in kids)   # y + h of the lowest child
        for c in kids:
            if c["componentData"]["type"] == "Focus":
                x, y, w, h = xywh(c["bounds"])
                c["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, max(h, bottom))
        need[d["key"]] = bottom
    grown = [0]

    def grow(o):
        if isinstance(o, dict):
            t = o.get("componentData", {}).get("type") if isinstance(o.get("componentData"), dict) else None
            if t in need and isinstance(o.get("bounds"), dict):
                x, y, w, h = xywh(o["bounds"])
                if h < need[t]:
                    o["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, need[t])
                    grown[0] += 1
            for v in o.values():
                grow(v)
        elif isinstance(o, list):
            for v in o:
                grow(v)
    grow(tui)
    if not need:
        raise SystemExit("skin_post: no knob definition found (the Send knobs)")
    print("skin_post: %d knob box(es) made as tall as the knob" % grown[0])


def main(skin_dir):
    skins = os.path.join(skin_dir, "Plugin Skins")
    tui = json.load(open(os.path.join(skins, "TUI.json")))
    params = json.load(open(os.path.join(HERE, "params.json")))["params"]
    back = "Parameter %d" % [p["key"] for p in params].index("view_back")
    pd = tui["pageData"]
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
    rack = defs[pd["tabs"][0]["componentName"]]["componentsData"]
    if not any(m.get("value") == back for c in rack for m in c["handle remapping"]["map"]):
        raise SystemExit("skin_post: the Back button (%s) is not on the RACK page" % back)
    own_knobs(skins, tui)
    grow_knob_boxes(tui)
    with open(os.path.join(skins, "TUI.json"), "w") as f:
        json.dump(tui, f, indent=2)
    shutil.copy2(os.path.join(HERE, "skin_assets", "pp_backdrop.png"), os.path.join(skins, "pp_backdrop.png"))
    shutil.copy2(os.path.join(skins, "TUI.json"), os.path.join(skins, "pp_base.json"))
    print("skin_post: pp_base.json + pp_backdrop.png written; installed effects are merged in on the device")


if __name__ == "__main__":
    main(sys.argv[1])
