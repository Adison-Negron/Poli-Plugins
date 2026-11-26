#!/usr/bin/env python3
"""Offline preview from vst/layout.conf + skin_assets (approximates MPC's drawing: knob frame at the value, value text in black below
the knob, meters showing the frame of their option, buttons with their image). Writes test/preview_default.png, preview_fold.png and
preview_browser.png (1280x628)."""
import os
import re
import shlex
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
V = os.path.join(HERE, "..", "vst")
sys.path.insert(0, os.path.join(HERE, ".."))
import generate_skin as g  # noqa: E402

lines = [l.strip() for l in open(os.path.join(V, "layout.conf")) if l.strip() and not l.startswith("#")]


def parse(l):
    t = shlex.split(l)
    d = {"kind": t[0]}
    for x in t[1:]:
        if "=" in x:
            k, v = x.split("=", 1)
            d[k] = v
    return d


W = [parse(l) for l in lines if re.match(r"(art|picture|knob|toggle|button|meter) ", l)]
POS = {"sat": 500, "smash": 0, "crunch": 400, "soften": 250, "low": 500, "mid": 500, "high": 500}
TXT = {"sat": "50 %", "smash": "0 %", "crunch": "40 %", "soften": "25 %", "low": "0 dB", "mid": "0 dB", "high": "0 dB"}


def strip(r, pos):
    box = 2 * r + 8
    n = g.strip_frames(r)
    im = Image.open(os.path.join(V, "skin_assets", "knob_strip_%d.png" % box))
    j = round(pos / 1000 * (n - 1))
    return im.crop((0, j * box, box, (j + 1) * box))


def render(state, out, pos=None, txt=None):
    pos, txt = dict(POS, **(pos or {})), dict(TXT, **(txt or {}))
    img = Image.new("RGB", (1280, 628 + 86), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for w in sorted(W, key=lambda w: 0 if w["kind"] == "art" else 1):
        if "when" in w:
            k, o = w["when"].split(":")
            if state.get(k) != o:
                continue
        if w["kind"] == "art":
            im = Image.open(os.path.join(V, w["file"])).convert("RGBA")
            img.paste(im, (0, 86))
        elif w["kind"] == "knob":
            r = int(w["r"])
            k = w["key"]
            im = strip(r, pos[k])
            cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
            f = g.font(int(w["vs"]), 700)
            vy = int(w["vy"])
            hbox = max(26, round(int(w["vs"]) * 1.2))
            ty = cy + r + 2 + vy + hbox // 2
            d.text((cx, ty), txt[k], font=f, fill=(0, 0, 5), anchor="mm")
        elif w["kind"] == "meter":
            n = int(w["frames"])
            strip_im = Image.open(os.path.join(V, w["strip"])).convert("RGB")
            wd, hd = int(w["w"]), int(w["h"])
            idx = int(state.get(w["key"] + "_idx", 0))
            cx, cy = int(w["cx"]), int(w["cy"])
            img.paste(strip_im.crop((0, idx * hd, wd, (idx + 1) * hd)), (cx - wd // 2, cy - hd // 2))
        elif w["kind"] == "button":
            if "img" not in w or "hit" in w["img"]:
                continue
            im = Image.open(os.path.join(V, w["img"])).convert("RGBA")
            cx, cy = int(w["cx"]), int(w["cy"])
            im = im.resize((int(w["w"]), int(w["h"])))   # MPC stretches a widget's image to its box
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
    img.crop((0, 86, 1280, 714)).save(out)


render({"type_idx": 0, "preset_idx": 0}, os.path.join(HERE, "preview_default.png"))
render({"type_idx": 3, "preset_idx": 59}, os.path.join(HERE, "preview_fold.png"),
       pos={"sat": 1000, "smash": 1000, "crunch": 1000, "soften": 0, "low": 0, "mid": 1000, "high": 625},
       txt={"sat": "100 %", "smash": "100 %", "crunch": "100 %", "soften": "0 %", "low": "Off", "mid": "+12 dB", "high": "+3 dB"})
render({"type_idx": 2, "preset_idx": 2, "bview": "1", "pre_hl": "1", "browsing": "On"}, os.path.join(HERE, "preview_browser.png"))
print("wrote test/preview_*.png")
