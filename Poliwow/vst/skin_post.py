#!/usr/bin/env python3
"""Poliwow skin touch-up, run by gen_vst.py after the skin is built (vst.json "skin_post"):

    python3 skin_post.py "<build>/skin/Poli - VST - Poliwow"

1. Knob filmstrips. The builder always writes 128-frame knob strips; at these sizes (290 px and 370 px frames) that is
   37120 / 47360 px, and MPC draws images taller than 16384 px wrongly. Each sh_knob_* strip is replaced by our own
   strip of the same frame size from skin_assets/ (knob_mix.png: 55 frames (226 px), knob_wow.png: 43 frames (274 px), knob_grain.png: 61 frames, shared by the eight small Granules knobs; knob_level.png: 61 frames; generate_skin.py),
   pasted at a 1 px offset (the builder centres a 2r+8 box in a 2r+10 frame), and its "numFrames" in TUI.json is set to
   the EXACT frame count. Device fact (Live III, MPC OS 3.10, 2026-11-04, Poli-Ess): MPC takes one frame's height as
   image height / numFrames, so the builder's "one fewer" convention made knobs drift into the next frame.
2. Knob value text keeps its case ("57%"; harmless, but the same as Poli-Multiband).
3. Knob boxes as tall as the knob. With the value text moved up into the face (vy) and no name (ns=0), the builder
   sizes a knob's component to the text's bottom, shorter than the knob picture, and MPC clips the picture to its
   component (Poli-Multiband, Live III, 2026-11-04). Each knob definition's Focus box and every placed knob are made at
   least as tall as the lowest child.
Fails loudly when a strip has no matching source, so a layout change can't silently ship the drifting 128-frame strip.
"""
import json
import os
import sys

from PIL import Image

LIMIT = 16384
ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "skin_assets")
SOURCES = ["knob_mix.png", "knob_wow.png", "knob_grain.png", "knob_level.png"]


def source_for(frame_w):
    for name in SOURCES:
        p = os.path.join(ASSETS, name)
        w, h = Image.open(p).size
        if frame_w - w == 2 and h % w == 0:
            return p
    return None


def walk(o, fn):
    if isinstance(o, dict):
        fn(o)
        for v in o.values():
            walk(v, fn)
    elif isinstance(o, list):
        for v in o:
            walk(v, fn)


def main(skin_dir):
    skins = os.path.join(skin_dir, "Plugin Skins")
    tui_path = os.path.join(skins, "TUI.json")
    tui = json.load(open(tui_path))
    fixed = {}

    def strips(o):
        if o.get("knobType") != "FilmStrip" or not str(o.get("filmStrip", "")).startswith("sh_knob_"):
            return
        name = o["filmStrip"]
        if name not in fixed:
            png = os.path.join(skins, name)
            im = Image.open(png)
            n_old = int(o["numFrames"]) + 1
            fw, fh = im.width, im.height // n_old
            src_path = source_for(fw)
            if not src_path:
                raise SystemExit("skin_post: no skin_assets strip for %s (frame %d px)" % (name, fw))
            src = Image.open(src_path).convert("RGBA")
            sw = src.width
            n_new = src.height // sw
            if fh * n_new > LIMIT:
                raise SystemExit("skin_post: %s would be %d px tall" % (src_path, fh * n_new))
            pad = (fw - sw) // 2
            out = Image.new("RGBA", (fw, fh * n_new), (0, 0, 0, 0))
            for j in range(n_new):
                out.paste(src.crop((0, j * sw, sw, (j + 1) * sw)), (pad, j * fh + pad))
            out.save(png, optimize=True)
            fixed[name] = n_new
            print("skin_post: %s <- %s (%d frames, %d px)" % (name, os.path.basename(src_path), n_new, fh * n_new))
        o["numFrames"] = fixed[name]

    walk(tui, strips)
    if len(fixed) != len(SOURCES):
        raise SystemExit("skin_post: expected %d knob strips, found %d" % (len(SOURCES), len(fixed)))

    def keep_case(o):
        d = o.get("data")
        if (o.get("type") == "Label" and o.get("name") == "Value" and isinstance(d, dict)
                and d.get("type") == "Value" and d.get("textStyle", {}).get("case") == "Upper Case"):
            d["textStyle"]["case"] = "Original"

    walk(tui, keep_case)

    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    need = {}
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]
    for d in defs:
        if not d["key"].startswith("shKnob"):
            continue
        kids = d["value"]["componentsData"]
        bottom = max(sum(xywh(c["bounds"])[1::2]) for c in kids)
        for c in kids:
            if c["componentData"]["type"] == "Focus":
                x, y, w, h = xywh(c["bounds"])
                c["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, max(h, bottom))
        need[d["key"]] = bottom
    grown = [0]

    def grow(o):
        t = o.get("componentData", {}).get("type") if isinstance(o.get("componentData"), dict) else None
        if t in need and isinstance(o.get("bounds"), dict):
            x, y, w, h = xywh(o["bounds"])
            if h < need[t]:
                o["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, need[t])
                grown[0] += 1

    walk(tui, grow)
    print("skin_post: %d knob boxes made as tall as their knob" % grown[0])
    with open(tui_path, "w") as f:
        json.dump(tui, f, indent=2)


if __name__ == "__main__":
    main(sys.argv[1])
