"""Offline picture of a built skin (Plugin Skins/TUI.json) in a given state: which when=/IndexedEnabling parts show,
knob frames, button states and live text. Copied from PoliPlug/test/preview.py so every Poli plugin can use it.

    from skin_render import render
    render(skin_dir, {param_index: option_index, ("v", param_index): 0..1}, {param_index: "text"}, "out.png")
"""
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MV = os.environ.get("MPC_VST", os.path.normpath(os.path.join(HERE, "..", "mpc-vst-plugins")))
FONTS = {s: os.path.join(MV, "tools", "html_art", "fonts", "TitilliumWeb-%s.ttf" % s) for s in ("Bold", "SemiBold", "Regular")}
W, H = 1280, 628


def conds(c):
    return [tuple(int(g) for g in m.groups()) for m in
            (re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)$", h) for h in c["bounds"].get("additionalInvalidatingHandles", []))
            if m]


def xywh(b):
    return [int(float(v)) for v in b["bounds"].split()]


def draw(skin, defs, comps, state, texts, im, dr, ox=0, oy=0, binding=None):
    for c in comps:
        if not all(state.get(p, 0) == i for i, n, p in conds(c)):
            continue
        cd = c["componentData"]
        x, y, w, h = xywh(c["bounds"])
        x, y = x + ox, y + oy
        hmap = {m["key"]: int(m["value"].split()[-1]) for m in c["handle remapping"]["map"] if m["value"].startswith("Parameter")}
        hmap = hmap or binding or {}
        t = cd["type"]
        if t == "Image":
            if cd["data"].get("image"):
                img = Image.open(os.path.join(skin, cd["data"]["image"])).convert("RGBA")
                im.paste(img, (x, y), img)
        elif t == "Knob":
            st = Image.open(os.path.join(skin, cd["data"]["filmStrip"])).convert("RGBA")
            n = int(cd["data"]["numFrames"])
            fh = st.size[1] // n
            p = hmap.get(cd["data"].get("handleName", "Data"))
            k = min(n - 1, round(state.get(("v", p), 0.5) * (n - 1)))
            fr = st.crop((0, k * fh, st.size[0], (k + 1) * fh))
            im.paste(fr, (x, y), fr)
        elif t == "Button":
            p = hmap.get("Data")
            img = cd["data"]["onImage" if state.get(("v", p), 0) > 0.5 else "offImage"]
            if img:
                img = Image.open(os.path.join(skin, img)).convert("RGBA")
                im.paste(img, (x, y), img)
        elif t == "Label":
            p = hmap.get(cd["data"].get("handleName", "Data"))
            text = texts.get(p, "")
            if text:
                ts = cd["data"]["textStyle"]
                col = ts["colour"]
                rgba = tuple(int(col[i:i + 2], 16) for i in (2, 4, 6))
                f = ImageFont.truetype(FONTS.get(ts["font"]["style"], FONTS["SemiBold"]), int(ts["font"]["height"] * 0.8))
                bb = dr.textbbox((0, 0), text, font=f)
                tw, th = bb[2] - bb[0], bb[3] - bb[1]
                just = ts.get("justification", "")
                tx = x if just.startswith("left") else x + w - tw if just.startswith("right") else x + (w - tw) // 2
                dr.text((tx - bb[0], y + (h - th) // 2 - bb[1]), text, font=f, fill=rgba)
        elif t in defs:
            draw(skin, defs, defs[t]["componentsData"], state, texts, im, dr, x, y, hmap)
        elif t != "Focus":
            pass


def render(skin, state, texts, out):
    tui = json.load(open(os.path.join(skin, "TUI.json")))["pageData"]
    defs = {d["key"]: d["value"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}
    page = defs[tui["tabs"][0]["componentName"]]
    im = Image.new("RGB", (W, H), (0, 0, 0))
    draw(skin, defs, page["componentsData"], state, texts, im, ImageDraw.Draw(im))
    im.save(out)
    return out


