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
sys.path.insert(0, os.path.join(HERE, "..", "presets"))
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
import factory  # noqa: E402  (presets/)
import make_params  # noqa: E402
POS = dict(zip(factory.KEYS, factory.DEFAULT_POS))
TXT = {"hold": "60 ms", "rasp": "50 %", "glide": "40 %", "guard": "60 %", "morph": "30 ms", "floor": "70 Hz", "ceil": "700 Hz", "overlap": "30 %",
       "pitch": "0 st", "formant": "0 st", "ftrack": "50 %", "thresh": "0 dB", "lowend": "Off", "attack": "3.0 ms", "release": "40 ms",
       "odd": "0 %", "even": "0 %", "color": "1.0 kHz", "f1c": "500 Hz", "f1q": "Off", "f2c": "1.7 kHz", "f2q": "Off", "f3c": "3.3 kHz",
       "f3q": "Off", "fsmooth": "30 %", "air": "0 %", "natural": "40 %", "mix": "100 %"}


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
            if "x" in w:
                img.paste(im, (int(w["x"]), int(w["y"])), im)
            else:
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
        elif w["kind"] == "toggle":
            on = state.get(w["key"]) == "On"
            im = Image.open(os.path.join(V, w["img_on" if on else "img"])).convert("RGBA")
            im = im.resize((int(w["w"]), int(w["h"])))
            img.paste(im, (int(w["cx"]) - im.width // 2, int(w["cy"]) - im.height // 2), im)
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


render({"preset_idx": 0, "page": "Shape"}, os.path.join(HERE, "preview_default.png"))
render({"preset_idx": 7, "page": "Tone"}, os.path.join(HERE, "preview_tone.png"))
render({"preset_idx": 7, "page": "Filter"}, os.path.join(HERE, "preview_filter.png"))
render({"preset_idx": 7, "page": "Tracking", "lview": "2"}, os.path.join(HERE, "preview_tracking.png"), pos={"hold": 733, "rasp": 780, "glide": 550}, txt={"hold": "110 ms", "rasp": "78 %", "glide": "55 %"})
render({"preset_idx": 5, "link": "On", "page": "Shape"}, os.path.join(HERE, "preview_loud.png"),
       pos={"pitch": 833, "formant": 646, "ftrack": 1000, "thresh": 200, "lowend": 400, "attack": 100, "release": 900, "odd": 1000, "even": 1000,
            "color": 1000, "f1c": 1000, "f1q": 1000, "f2c": 1000, "f2q": 1000, "f3c": 1000, "f3q": 1000, "fsmooth": 1000, "air": 1000,
            "natural": 1000, "mix": 1000},
       txt={"pitch": "+8.0 st", "formant": "+3.5 st", "ftrack": "100 %", "thresh": "-48 dB", "lowend": "150 Hz", "attack": "0.6 ms",
            "release": "150 ms", "odd": "100 %", "even": "100 %", "color": "4.0 kHz", "f1c": "1.0 kHz", "f1q": "14.0", "f2c": "3.0 kHz",
            "f2q": "14.0", "f3c": "4.5 kHz", "f3q": "14.0", "fsmooth": "100 %", "air": "100 %", "natural": "100 %", "mix": "100 %"})
render({"preset_idx": 2, "bview": "1", "pre_hl": "1", "browsing": "On", "page": "Shape"}, os.path.join(HERE, "preview_browser.png"))
render({"preset_idx": 7, "page": "Tracking", "lview": "2", "learn": "On", "learn_prog": "10"}, os.path.join(HERE, "preview_learn.png"))
render({"hview": "1"}, os.path.join(HERE, "preview_help1.png"))
render({"hview": "2"}, os.path.join(HERE, "preview_help2.png"))
print("wrote test/preview_*.png")
