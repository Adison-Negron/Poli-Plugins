#!/usr/bin/env python3
"""Offline preview from vst/layout.conf + skin_assets (approximates MPC's drawing). Writes test/preview_main.png, preview_ext.png,
preview_browser.png (1280x714: plugin area plus the 86 px above it)."""
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
W = [parse(l) for l in lines if re.match(r"(art|picture|knob|toggle|button|meter) ", l)]
vals = {"input": 500, "drive": 300, "low": 500, "mid": 500, "high": 500, "amount": 500, "ldrive": 200, "lthresh": 889, "output": 988}
txt = {"input": "0 dB", "drive": "30 %", "low": "0 %", "mid": "0 %", "high": "0 %", "amount": "100 %", "ldrive": "20 %", "lthresh": "-2 dB", "output": "-0.3 dB"}
meters = {"m_pk": 0.62, "m_rms": 0.45, "m_gl": 0.3, "m_gm": 0.35, "m_gh": 0.25, "m_lim": 0.2}
def frame(path, n, h, v):
    im = Image.open(os.path.join(V, path)).convert("RGBA"); j = round(v * (n - 1)); return im.crop((0, j * h, im.width, (j + 1) * h))
def render(state, out):
    img = Image.new("RGB", (1280, 628 + 86), (0, 0, 0)); d = ImageDraw.Draw(img)
    for w in sorted(W, key=lambda w: 0 if w["kind"] == "art" else 1):
        if "when" in w:
            k, o = w["when"].split(":")
            if str(state.get(k)) != o: continue
        k = w.get("kind")
        if k == "art":
            img.paste(Image.open(os.path.join(V, w["file"])).convert("RGB"), (0, 86))
        elif k == "picture":
            files = w["files"].split(","); im = Image.open(os.path.join(V, files[state.get("pic", 0)])).convert("RGBA")
            im = im.resize((int(w["w"]), int(w["h"]))); img.paste(im, (int(w["x"]), int(w["y"])), im)
        elif k == "knob":
            r = int(w["r"]); key = w["key"]; box = 2 * r + 8; n = g.strip_frames(r)
            im = Image.open(os.path.join(V, "skin_assets", "knob_strip_%d.png" % box)).convert("RGBA")
            j = round(vals[key] / 1000 * (n - 1)); im = im.crop((0, j * box, box, (j + 1) * box)); cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(im, (cx - box // 2, cy - box // 2), im)
            f = g.font(int(w["vs"]), 700); hbox = max(26, round(int(w["vs"]) * 1.2))
            d.text((cx, cy + r + 2 + int(w["vy"]) + hbox // 2), txt[key], font=f, fill=(0, 0, 5), anchor="mm")
        elif k == "meter":
            n = int(w["frames"]); im = frame(w["strip"], n, int(w["h"]), meters[w["key"]]); cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
        elif k in ("toggle", "button"):
            nm = w.get("img_on") if (k == "toggle" and state.get("sc")) else w.get("img")
            if not nm or "hit" in nm: continue
            im = Image.open(os.path.join(V, nm)).convert("RGBA"); cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
    img.save(os.path.join(HERE, out))
render({"pic": 1}, "preview_main.png"); render({"sc": 1, "pic": 1}, "preview_ext.png"); render({"browsing": "On", "bview": 1, "pre_hl": 2, "pic": 1}, "preview_browser.png")
print("ok")
