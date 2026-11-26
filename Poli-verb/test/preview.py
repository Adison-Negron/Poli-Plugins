#!/usr/bin/env python3
"""Offline preview of each page from vst/layout.conf + skin_assets (approximates MPC's drawing: knob frame at the
default position, value text in white, toggles off). Writes test/preview_<page>.png (1280x628)."""
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
vals = {"mix":300,"input":500,"output":500,"drive":0,"low":500,"tilt":500,"high":500,"size":500,"predelay":120,"width":500,"er_level":600,"er_prob":1000,"diffusion":800,"damping":500,"modulation":300,"density":600,"spread":700,"bassmult":500,"attack":0,"release":1000,"decay":500}
txt = {"mix":"30 %","input":"0 dB","output":"0 dB","drive":"0 dB","low":"0 dB","tilt":"0 dB","high":"0 dB","size":"50 %","predelay":"7.2 ms","width":"100 %","er_level":"60 %","er_prob":"100 %","diffusion":"80 %","damping":"50 %","modulation":"30 %","density":"60 %","spread":"70 %","bassmult":"1x","attack":"0 ms","release":"inf","decay":"2.4 s"}
def strip(r, pos):
    box = 2*r+8; n = g.strip_frames(r); im = Image.open(os.path.join(V, "skin_assets", "knob_strip_%d.png" % box))
    j = round(pos/1000*(n-1)); return im.crop((0, j*box, box, (j+1)*box))
for page in range(3):
    img = Image.new("RGB", (1280, 628+86), (0,0,0)); d = ImageDraw.Draw(img)
    pic = {"page": page, "type_view": {0: 0, 1: 6, 2: 3}[page], "preset": 0}
    for w in sorted(W, key=lambda w: 0 if w["kind"] == "art" else 1):
        if "when" in w:
            k, o = w["when"].split(":")
            if k == "page" and int(o) != page: continue
            if k in ("bview", "pre_hl", "browsing"): continue
        if w["kind"] == "art":
            im = Image.open(os.path.join(V, w["file"])).convert("RGBA")
            if "x" in w: im = im.resize((int(w["w"]), int(w["h"]))); img.paste(im, (int(w["x"]), int(w["y"])), im)
            else: img.paste(im, (0, 86))
        elif w["kind"] == "picture":
            files = w["files"].split(","); im = Image.open(os.path.join(V, files[pic.get(w["key"], 0)])).convert("RGBA")
            img.paste(im.resize((int(w["w"]), int(w["h"]))), (int(w["x"]), int(w["y"])), im.resize((int(w["w"]), int(w["h"]))))
        elif w["kind"] == "knob":
            r = int(w["r"]); k = w["key"]; im = strip(r, vals[k]); cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(im, (cx-im.width//2, cy-im.height//2), im)
            f = g.font(int(w["vs"]), 700); vy = int(w["vy"]); s = 2*r+10
            ty = cy - s//2 + s//2 + r + 2 + vy + max(26, round(int(w["vs"])*1.2))//2
            d.text((cx, ty), txt[k], font=f, fill=(255,255,255), anchor="mm")
        elif w["kind"] == "toggle":
            im = Image.open(os.path.join(V, w["img"])).convert("RGBA"); img.paste(im, (int(w["cx"])-im.width//2, int(w["cy"])-im.height//2), im)
        elif w["kind"] == "button" and "arrow" in w.get("img", "") and not w["img"].endswith("_on.png") and "bview" not in w.get("when", ""):
            im = Image.open(os.path.join(V, w["img"])).convert("RGBA"); img.paste(im, (int(w["cx"])-im.width//2, int(w["cy"])-im.height//2), im)
    img.crop((0, 86, 1280, 714)).save(os.path.join(HERE, "preview_%d.png" % page))
print("ok")
