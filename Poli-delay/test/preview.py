#!/usr/bin/env python3
"""Offline preview from vst/layout.conf + skin_assets (approximates MPC's drawing: knob frame at the default position, value text
in white, toggles off). Writes test/preview_offset_off.png, preview_offset_on.png, preview_browser.png (1280x628)."""
import os, re, shlex, sys
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__)); V = os.path.join(HERE, "..", "vst")
sys.path.insert(0, os.path.join(HERE, ".."))
import generate_skin as g
lines = [l.strip() for l in open(os.path.join(V, "layout.conf")) if l.strip() and not l.startswith("#")]
def parse(l):
    t = shlex.split(l); d = {"kind": t[0]}
    for x in t[1:]:
        if "=" in x: k, v = x.split("=", 1); d[k] = v
    return d
W = [parse(l) for l in lines if re.match(r"(art|picture|knob|toggle|button) ", l)]
vals = {"time": 683, "offset_amt": 500, "feedback": 350, "hp": 200, "lp": 850, "mix": 300, "rate": 450, "depth": 0, "drive": 0, "tone": 500, "spread": 500}
txt = {"time": "299 ms", "offset_amt": "0 %", "feedback": "35 %", "hp": "50 Hz", "lp": "11 kHz", "mix": "30 %", "rate": "0.54 Hz", "depth": "0 %",
       "drive": "0 %", "tone": "0 %", "spread": "100 %"}
def strip(r, pos):
    box = 2 * r + 8; n = g.strip_frames(r); im = Image.open(os.path.join(V, "skin_assets", "knob_strip_%d.png" % box))
    j = round(pos / 1000 * (n - 1)); return im.crop((0, j * box, box, (j + 1) * box))
def render(state, out, text_override=None, on_toggles=()):
    img = Image.new("RGB", (1280, 628 + 86), (0, 0, 0)); d = ImageDraw.Draw(img)
    for w in sorted(W, key=lambda w: 0 if w["kind"] == "art" else 1):
        if "when" in w:
            k, o = w["when"].split(":")
            if state.get(k) != o: continue
        if w["kind"] == "art":
            im = Image.open(os.path.join(V, w["file"])).convert("RGBA"); img.paste(im, (0, 86))
        elif w["kind"] == "picture":
            files = w["files"].split(","); im = Image.open(os.path.join(V, files[state.get("pic", 0)])).convert("RGBA")
            im = im.resize((int(w["w"]), int(w["h"]))); img.paste(im, (int(w["x"]), int(w["y"])), im)
        elif w["kind"] == "knob":
            r = int(w["r"]); k = w["key"]; im = strip(r, vals[k]); cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
            f = g.font(int(w["vs"]), 700); vy = int(w["vy"]); s = 2 * r + 10
            hbox = max(26, round(int(w["vs"]) * 1.2))
            ty = cy + r + 2 + vy + hbox // 2
            d.text((cx, ty), (text_override or txt)[k], font=f, fill=(255, 255, 255), anchor="mm")
        elif w["kind"] in ("toggle", "button"):
            if w["kind"] == "button" and ("img" not in w or "hit" in w["img"]): continue
            fn = w["img_on"] if (w["kind"] == "toggle" and w["key"] in on_toggles) else w["img"]
            im = Image.open(os.path.join(V, fn)).convert("RGBA"); cx, cy = int(w["cx"]), int(w["cy"])
            im = im.resize((int(w["w"]), int(w["h"])))   # MPC stretches a widget's image to its box
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
    img.crop((0, 86, 1280, 714)).save(out)
render({"offset": "Off", "pic": 0}, os.path.join(HERE, "preview_offset_off.png"))
render({"offset": "On", "pic": 0}, os.path.join(HERE, "preview_offset_on.png"), on_toggles=("offset",),
       text_override=dict(txt, offset_amt="+25 %", time="1/8", rate="0.54 Hz"))
render({"offset": "Off", "pic": 12, "bview": "1", "pre_hl": "1", "browsing": "On"}, os.path.join(HERE, "preview_browser.png"))
print("wrote test/preview_*.png")
