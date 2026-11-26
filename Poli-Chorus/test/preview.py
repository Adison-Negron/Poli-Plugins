#!/usr/bin/env python3
"""Offline preview from vst/layout.conf + skin_assets (approximates MPC's drawing: knob frame at the value, value text in black below the
knob, meters showing their frame, buttons with their image, `when=` widgets only in their state). The display box columns come from the
engine's own mapping (test/viz_dump.cpp). Writes test/preview_default.png, preview_ensemble.png, preview_vibrato.png, preview_browser.png
(1280x628).  Needs g++ (to build viz_dump) and Pillow."""
import os
import re
import shlex
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
V = os.path.join(HERE, "..", "vst")
sys.path.insert(0, os.path.join(HERE, ".."))
import generate_skin as g  # noqa: E402

TMP = os.path.join(HERE, "..", "vst", "build", "t")
os.makedirs(TMP, exist_ok=True)
DUMP = os.path.join(TMP, "viz_dump")
subprocess.check_call(["g++", "-std=gnu++11", "-O1", "-I", os.path.join(HERE, "..", "src"), os.path.join(HERE, "viz_dump.cpp"), "-o", DUMP])

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
KEYS = ["rate", "amount", "feedback", "width", "lpf", "warmth", "output", "mix"]
POS = dict(zip(KEYS, [523, 500, 0, 600, 1000, 250, 667, 500]))
TXT = {"rate": "0.80 Hz", "amount": "50 %", "feedback": "0 %", "width": "60 %", "lpf": "Off", "warmth": "25 %", "output": "0.0 dB", "mix": "50 %"}
MODES = ["Classic", "Ensemble", "Vibrato"]


def strip(r, pos):
    box = 2 * r + 8
    n = g.strip_frames(r)
    im = Image.open(os.path.join(V, "skin_assets", "knob_strip_%d.png" % box))
    j = round(pos / 1000 * (n - 1))
    return im.crop((0, j * box, box, (j + 1) * box))


def viz(mode):
    out = subprocess.check_output([DUMP, str(mode), str(POSV["rate"]), str(POSV["amount"]), str(POSV["width"])]).decode().split("\n")
    return {"l": [int(x) for x in out[0].split()], "r": [int(x) for x in out[1].split()]}


def render(state, out, pos=None, txt=None):
    global POSV
    pos, txt = dict(POS, **(pos or {})), dict(TXT, **(txt or {}))
    POSV = pos
    mode = MODES.index(state.get("mode", "Classic"))
    cols = viz(mode)
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
            hbox = max(26, round(int(w["vs"]) * 1.2))
            ty = cy + r + 2 + int(w["vy"]) + hbox // 2
            d.text((cx, ty), txt[k], font=f, fill=(0, 0, 5), anchor="mm")
        elif w["kind"] == "meter":
            n = int(w["frames"])
            sp = Image.open(os.path.join(V, w["strip"])).convert("RGBA")
            wd, hd = int(w["w"]), int(w["h"])
            key = w["key"]
            if key.startswith("vl") or key.startswith("vr"):
                idx = cols[key[1]][int(key[2:]) - 1]
            else:
                idx = int(state.get(key + "_idx", 0))
            cx, cy = int(w["cx"]), int(w["cy"])
            fr = sp.crop((0, idx * hd, wd, (idx + 1) * hd))
            img.paste(fr, (cx - wd // 2, cy - hd // 2), fr)
        elif w["kind"] == "button":
            if "img" not in w or "hit" in w["img"]:
                continue
            im = Image.open(os.path.join(V, w["img"])).convert("RGBA")
            cx, cy = int(w["cx"]), int(w["cy"])
            im = im.resize((int(w["w"]), int(w["h"])))   # MPC stretches a widget's image to its box
            img.paste(im, (cx - im.width // 2, cy - im.height // 2), im)
    img.crop((0, 86, 1280, 714)).save(out)


render({"mode": "Classic", "preset_idx": 0}, os.path.join(HERE, "preview_default.png"))
render({"mode": "Ensemble", "preset_idx": 12}, os.path.join(HERE, "preview_ensemble.png"),
       pos={"rate": 420, "amount": 850, "feedback": 150, "width": 1000, "lpf": 752, "warmth": 300, "output": 667, "mix": 700},
       txt={"rate": "0.45 Hz", "amount": "85 %", "feedback": "15 %", "width": "100 %", "lpf": "9.0 kHz", "warmth": "30 %", "output": "0.0 dB", "mix": "70 %"})
render({"mode": "Vibrato", "preset_idx": 20}, os.path.join(HERE, "preview_vibrato.png"),
       pos={"rate": 850, "amount": 1000, "feedback": 1000, "width": 0, "lpf": 0, "warmth": 1000, "output": 0, "mix": 1000},
       txt={"rate": "5.2 Hz", "amount": "100 %", "feedback": "100 %", "width": "0 %", "lpf": "800 Hz", "warmth": "100 %", "output": "-24 dB", "mix": "100 %"})
render({"mode": "Classic", "preset_idx": 2, "bview": "1", "pre_hl": "1", "browsing": "On"}, os.path.join(HERE, "preview_browser.png"))
print("wrote test/preview_*.png")
