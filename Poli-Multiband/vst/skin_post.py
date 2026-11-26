#!/usr/bin/env python3
"""Poli-Multiband skin touch-up, run by gen_vst.py after the skin is built (vst.json "skin_post"):

    python3 skin_post.py "<build>/skin/Poli - VST - Poli-Multiband"

1. Knob value text keeps its case. The builder draws knob values in upper case ("500 HZ", "1K"); the mockup has
   "500 hz", "1k", "0 db". Every knob "Value" label gets "case": "Original" (the case the builder itself uses for
   live readouts), so MPC shows the engine's text as written.
2. Knob boxes as tall as the knob. With the value text moved up into the knob (vy=-54) and no name (ns=0), the builder
   sizes a knob's component to the text's bottom (85 px), shorter than the knob picture (110 px), and MPC clips the
   picture to its component: the bottom of every knob was cut off flat (seen on the Live III, 2026-11-04). Each knob
   definition's box (and its Focus) and every placed knob are made at least as tall as the knob picture.
3. Knob filmstrips (below; same as Poli-Ess):

The builder always writes knob filmstrips with 128 frames. At the mockup's knob size (r=90, a 190 px frame) that is a
24320 px tall PNG, and MPC draws images taller than 16384 px wrongly (the knob drifts as it turns). So every knob
filmstrip over the limit is replaced by skin_assets/knob_strip.png (generate_skin.py: 63 frames, an odd count so one
frame points straight up; 63 x 190 = 11970 px), or, without it, resampled to as many evenly spaced frames as fit.
Its "numFrames" in TUI.json is set to the EXACT number of frames in the strip. Device fact (Live III, MPC OS 3.10,
2026-11-04): MPC takes one frame's height as image height / numFrames, so the builder's "one fewer" convention
(numFrames 62 for 63 frames) made every frame 3 px too tall and the knobs drifted up/down into the next frame.
"""
import json
import os
import sys

from PIL import Image

LIMIT = 12288   # stay under the stricter slider/meter limit too, not just 16384
SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "skin_assets", "knob_strip.png")


def _fits(path, frame_w):
    """True when the source strip is square-framed, 2 px narrower than the builder's frame and under the limit."""
    w, h = Image.open(path).size
    return h % w == 0 and frame_w - w == 2 and h // w * frame_w <= LIMIT


def main(skin_dir):
    skins = os.path.join(skin_dir, "Plugin Skins")
    tui_path = os.path.join(skins, "TUI.json")
    tui = json.load(open(tui_path))
    fixed = {}

    def walk(o):
        if isinstance(o, dict):
            if o.get("knobType") == "FilmStrip" and str(o.get("filmStrip", "")).startswith("sh_knob_"):
                png = os.path.join(skins, o["filmStrip"])
                if o["filmStrip"] not in fixed:
                    im = Image.open(png)
                    n_old = int(o["numFrames"]) + 1                 # frames actually in the strip
                    fw, fh = im.width, im.height // n_old
                    if im.height <= LIMIT:
                        fixed[o["filmStrip"]] = int(o["numFrames"])
                    elif os.path.exists(SOURCE) and _fits(SOURCE, fw):
                        # our own strip (generate_skin.py): already the right frame count, used as drawn
                        src = Image.open(SOURCE).convert("RGBA")
                        sw = src.width
                        n_new = src.height // sw
                        pad = (fw - sw) // 2   # the builder centres the knob's 2r+8 box in a 2r+10 frame
                        out = Image.new("RGBA", (fw, fh * n_new), (0, 0, 0, 0))
                        for j in range(n_new):
                            out.paste(src.crop((0, j * sw, sw, (j + 1) * sw)), (pad, j * fh + pad))
                        out.save(png, optimize=True)
                        fixed[o["filmStrip"]] = n_new
                        print("skin_post: %s <- %s (%d frames, %d px)" % (o["filmStrip"], SOURCE, n_new, fh * n_new))
                    else:
                        n_new = LIMIT // fh
                        n_new -= 1 - n_new % 2   # odd, so one frame sits exactly at the middle (pointer straight up)
                        out = Image.new(im.mode, (fw, fh * n_new))
                        for j in range(n_new):
                            src = round(j * (n_old - 1) / (n_new - 1))
                            out.paste(im.crop((0, src * fh, fw, (src + 1) * fh)), (0, j * fh))
                        out.save(png, optimize=True)
                        fixed[o["filmStrip"]] = n_new
                        print("skin_post: %s %d -> %d frames (%d px)" % (o["filmStrip"], n_old, n_new, fh * n_new))
                o["numFrames"] = fixed[o["filmStrip"]]
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

    # 2. knob boxes at least as tall as the knob picture
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    need = {}
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]
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
