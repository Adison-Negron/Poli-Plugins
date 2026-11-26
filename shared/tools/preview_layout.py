#!/usr/bin/env python3
"""Rough preview of a plugin's layout.conf: bg + page art + knobs (as discs with r) + buttons/toggles/pictures.
usage: preview_layout.py <plugin dir> <out prefix> [when-key=value ...]   e.g. page=1"""
import os
import re
import shlex
import sys

from PIL import Image, ImageDraw

pdir, out = sys.argv[1], sys.argv[2]
state = dict(a.split("=", 1) for a in sys.argv[3:])
vst = os.path.join(pdir, "vst")
page = Image.new("RGB", (1280, 628), (255, 0, 255))
Y = 86


def load(fn):
    p = os.path.join(vst, fn)
    return Image.open(p).convert("RGBA") if os.path.exists(p) else None


def ok(kv):
    w = kv.get("when")
    if not w:
        return True
    k, v = w.split(":", 1)
    if k in state:
        return state[k] == v
    return True


for line in open(os.path.join(vst, "layout.conf")):
    line = line.strip()
    if not line or line.startswith("#") or line.startswith("[") or line.startswith("theme") or line.startswith("qlinks"):
        continue
    try:
        parts = shlex.split(line)
    except ValueError:
        continue
    kind, kv = parts[0], dict(p.split("=", 1) for p in parts[1:] if "=" in p)
    if not ok(kv):
        continue
    I = lambda k, d=0: int(float(kv.get(k, d)))
    if kind == "art":
        im = load(kv["file"])
        if im is None:
            continue
        if "x" in kv:
            page.paste(im.resize((I("w"), I("h"))) if im.size != (I("w"), I("h")) else im, (I("x"), I("y") - Y), im.resize((I("w"), I("h"))) if im.size != (I("w"), I("h")) else im)
        else:
            page.paste(im.convert("RGB").resize((1280, 628)))
    elif kind == "picture":
        fl = kv["files"].split(",")
        im = load(fl[0])
        if im is not None:
            page.paste(im, (I("x"), I("y") - Y), im)
    elif kind in ("button", "toggle"):
        if kv.get("when", "").startswith(("browsing", "bview", "pre_hl")):
            continue
        im = load(kv.get("img", ""))
        if im is not None and im.size[0] > 8:
            page.paste(im, (I("cx") - im.size[0] // 2, I("cy") - Y - im.size[1] // 2), im)
    elif kind == "knob":
        d = ImageDraw.Draw(page)
        r = I("r")
        cx, cy = I("cx"), I("cy") - Y
        sp = None
        if "strip" in kv:
            sp = os.path.join(vst, kv["strip"])
        else:
            import glob
            for f in sorted(glob.glob(os.path.join(vst, "skin_assets", "knob_strip*.png"))):
                if Image.open(f).size[0] == 2 * r + 8:
                    sp = f
        pos = float(os.environ.get("KNOBPOS", "0.35"))
        if sp:
            st = Image.open(sp).convert("RGBA")
            W_ = st.size[0]
            n = int(kv["frames"]) if "frames" in kv else st.size[1] // W_
            fh = st.size[1] // n
            k = round(pos * (n - 1))
            fr = st.crop((0, k * fh, W_, (k + 1) * fh))
            page.paste(fr, (cx - W_ // 2, cy - fh // 2), fr)
        else:
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(60, 60, 70), outline=(255, 255, 255))
        vs = I("vs")
        if vs:
            from PIL import ImageFont
            fp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "TitilliumWeb-SemiBold.ttf")
            if not os.path.exists(fp):
                fp = os.path.join(pdir, "..", "Poli-Multiband", "fonts", "TitilliumWeb-SemiBold.ttf")
            f = ImageFont.truetype(fp, vs)
            hb = max(26, round(1.2 * vs))
            ty = cy + r + 2 + I("vy") + hb / 2
            ink = kv.get("ink_dim", "FFFFFF")
            col = tuple(int(ink[i:i + 2], 16) for i in (0, 2, 4))
            d.text((cx, ty), os.environ.get("KNOBTXT", "-12.5 dB"), font=f, fill=col, anchor="mm")
    elif kind in ("meter", "readout"):
        d = ImageDraw.Draw(page)
        cx, cy, w, h = I("cx"), I("cy") - Y, I("w"), I("h")
        d.rectangle([cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2], outline=(0, 160, 255))
d = ImageDraw.Draw(page)
d.line([(1216, 0), (1216, 628)], fill=(255, 0, 0))   # 64 px margin
d.line([(1025, 0), (1025, 628)], fill=(0, 200, 255))  # Q-Link sidebar
page.save(out)
print("saved", out)
