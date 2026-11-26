#!/usr/bin/env python3
"""Poli-tea skin touch-up (vst.json "skin_post"), run on the built skin folder.

1. Knob filmstrips: the builder writes 128-frame strips (too tall for MPC). Each sh_knob_r<R>.png is replaced by our
   skin_assets/knob_strip_<2R+8>.png (odd frame count, <= 12288 px), centred in the builder's 2R+10 frame; numFrames is
   the EXACT strip length (device fact, Live III 2026-11-04).
2. Knob value text keeps its case ("inf", "1.0x", "dB").
3. Knob boxes as tall as the knob picture (else MPC clips the bottom when the value text sits inside the knob).
"""
import json
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
LIMIT = 12288


def main(skin_dir):
    skins = os.path.join(skin_dir, "Plugin Skins")
    tui_path = os.path.join(skins, "TUI.json")
    tui = json.load(open(tui_path))
    fixed = {}

    def walk(o):
        if isinstance(o, dict):
            fs = str(o.get("filmStrip", ""))
            if o.get("knobType") == "FilmStrip" and fs.startswith("sh_knob_"):
                if fs not in fixed:
                    r = int(re.match(r"sh_knob_r(\d+)", fs).group(1))
                    src_path = os.path.join(HERE, "skin_assets", "knob_strip_%d.png" % (2 * r + 8))
                    if not os.path.exists(src_path):
                        sys.exit("skin_post: missing %s" % src_path)
                    png = os.path.join(skins, fs)
                    old = Image.open(png)
                    fw = old.width
                    fh = old.height // (int(o["numFrames"]) + 1)
                    src = Image.open(src_path).convert("RGBA")
                    sw = src.width
                    n = src.height // sw
                    pad = (fw - sw) // 2
                    out = Image.new("RGBA", (fw, fh * n), (0, 0, 0, 0))
                    for j in range(n):
                        out.paste(src.crop((0, j * sw, sw, (j + 1) * sw)), (pad, j * fh + pad))
                    if out.height > LIMIT:
                        sys.exit("skin_post: %s is %d px tall" % (fs, out.height))
                    out.save(png, optimize=True)
                    fixed[fs] = n
                    print("skin_post: %s <- %s (%d frames, %d px)" % (fs, os.path.basename(src_path), n, out.height))
                o["numFrames"] = fixed[fs]
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(tui)

    def keep_case(o):
        if isinstance(o, dict):
            d = o.get("data")
            if (o.get("type") == "Label" and o.get("name") == "Value" and isinstance(d, dict)
                    and d.get("type") == "Value" and d.get("textStyle", {}).get("case") == "Upper Case"):
                d["textStyle"]["case"] = "Original"
            for v in o.values():
                keep_case(v)
        elif isinstance(o, list):
            for v in o:
                keep_case(v)

    keep_case(tui)

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
    grown = 0

    def grow(o):
        nonlocal grown
        if isinstance(o, dict):
            t = o.get("componentData", {}).get("type") if isinstance(o.get("componentData"), dict) else None
            if t in need and isinstance(o.get("bounds"), dict):
                x, y, w, h = xywh(o["bounds"])
                if h < need[t]:
                    o["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, need[t])
                    grown += 1
            for v in o.values():
                grow(v)
        elif isinstance(o, list):
            for v in o:
                grow(v)

    grow(tui)
    print("skin_post: %d knob boxes made as tall as their knob" % grown)
    with open(tui_path, "w") as f:
        json.dump(tui, f, indent=2)


if __name__ == "__main__":
    main(sys.argv[1])
