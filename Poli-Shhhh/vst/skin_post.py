#!/usr/bin/env python3
"""Shhhhh skin touch-up and guard, run by gen_vst.py after the skin is built (vst.json "skin_post"):

    python3 skin_post.py "<build>/skin/Poli - VST - Shhhhh"

1. Value text keeps its case: the builder sets slider value labels to upper case ("+0.0 DB"); the engine writes
   "+0.0 dB", so every "Value" label gets "case": "Original".
2. The Output knob's filmstrip: the builder always writes 128 frames (98 x 128 = 12544 px, over the limit); it is
   replaced by skin_assets/knob_out.png (generate_skin.py: 121 frames, odd so 0 dB points straight up), pasted at the
   builder's 1 px offset, and numFrames is set to the exact count.
3. Knob boxes as tall as the knob: with the value text moved into the face (vy) and no name, the builder sizes the
   knob's component to the text's bottom and MPC clips the picture to it (Poli-Multiband, Live III, 2026-11-04).
4. Guard (fails the build): every FilmStrip's numFrames is the EXACT frame count of its image (MPC takes one frame as
   height / numFrames: device fact, Live III 3.10) and no image is taller than 12288 px (the builder's slider/meter
   limit; MPC draws images over 16384 px wrongly).
"""
import json
import os
import sys

from PIL import Image

LIMIT = 12288
KNOB_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "skin_assets", "knob_out.png")


def swap_knobs(o, skins, done):
    if isinstance(o, dict):
        if o.get("knobType") == "FilmStrip" and str(o.get("filmStrip", "")).startswith("sh_knob_"):
            name = o["filmStrip"]
            if name not in done:
                png = os.path.join(skins, name)
                im = Image.open(png)
                fw = im.width
                fh = im.height // (int(o["numFrames"]) + 1)   # the builder's rotary convention: one fewer
                src = Image.open(KNOB_SRC).convert("RGBA")
                sw = src.width
                n = src.height // sw
                if fw - sw != 2 or fh != fw:
                    raise SystemExit("skin_post: %s frame %dx%d doesn't fit knob_out.png (%d)" % (name, fw, fh, sw))
                out = Image.new("RGBA", (fw, fh * n), (0, 0, 0, 0))
                for j in range(n):
                    out.paste(src.crop((0, j * sw, sw, (j + 1) * sw)), (1, j * fh + 1))
                out.save(png, optimize=True)
                done[name] = n
                print("skin_post: %s <- knob_out.png (%d frames, %d px)" % (name, n, fh * n))
            o["numFrames"] = done[name]
        for v in o.values():
            swap_knobs(v, skins, done)
    elif isinstance(o, list):
        for v in o:
            swap_knobs(v, skins, done)


def grow_knob_boxes(tui):
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]
    need = {}
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
        if isinstance(o, dict):
            cd = o.get("componentData")
            t = cd.get("type") if isinstance(cd, dict) else None
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
    return grown[0]


def main(skin_dir):
    skins = os.path.join(skin_dir, "Plugin Skins")
    tui_path = os.path.join(skins, "TUI.json")
    tui = json.load(open(tui_path))
    problems, cased = [], 0
    done = {}
    swap_knobs(tui, skins, done)
    if len(done) != 1:
        raise SystemExit("skin_post: expected 1 knob strip (Output), found %d" % len(done))
    grown = grow_knob_boxes(tui)

    def walk(o):
        nonlocal cased
        if isinstance(o, dict):
            if o.get("knobType") == "FilmStrip":
                im = Image.open(os.path.join(skins, o["filmStrip"]))
                n = int(o["numFrames"])
                if im.height % n:
                    problems.append("%s: %d px is not %d whole frames" % (o["filmStrip"], im.height, n))
            d = o.get("data")
            if (o.get("type") == "Label" and o.get("name") == "Value" and isinstance(d, dict)
                    and d.get("textStyle", {}).get("case") == "Upper Case"):
                d["textStyle"]["case"] = "Original"
                cased += 1
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(tui)
    for f in os.listdir(skins):
        if f.endswith(".png"):
            h = Image.open(os.path.join(skins, f)).height
            if h > LIMIT:
                problems.append("%s: %d px tall (limit %d)" % (f, h, LIMIT))
    if problems:
        raise SystemExit("skin_post: " + "; ".join(problems))
    with open(tui_path, "w") as f:
        json.dump(tui, f, indent=2)
    print("skin_post: %d value labels keep their case; %d knob boxes grown; all filmstrips whole and under %d px"
          % (cased, grown, LIMIT))


if __name__ == "__main__":
    main(sys.argv[1])
