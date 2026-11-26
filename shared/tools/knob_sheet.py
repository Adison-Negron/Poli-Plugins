#!/usr/bin/env python3
"""Contact sheet of every distinct knob: 7 frames (min .. max) with the value text drawn where MPC puts it.
usage: knob_sheet.py <root> <plugin> <out.png> [text]"""
import glob
import os
import shlex
import sys

from PIL import Image, ImageDraw, ImageFont

root, plugin, out = sys.argv[1:4]
txt = sys.argv[4] if len(sys.argv) > 4 else "-12.5 dB"
vst = os.path.join(root, plugin, "vst")
FONT = os.path.join(root, "Poli-Multiband", "fonts", "TitilliumWeb-SemiBold.ttf")
rows, seen = [], set()
for line in open(os.path.join(vst, "layout.conf")):
    line = line.strip()
    if not line.startswith("knob "):
        continue
    kv = dict(x.split("=", 1) for x in shlex.split(line)[1:] if "=" in x)
    r, vs, vy = int(kv["r"]), int(kv.get("vs", 0)), int(kv.get("vy", 0))
    sig = (kv.get("strip"), r, vs, vy)
    if sig in seen:
        continue
    seen.add(sig)
    if "strip" in kv:
        sp = os.path.join(vst, kv["strip"])
    else:
        sp = next((f for f in sorted(glob.glob(os.path.join(vst, "skin_assets", "knob_strip*.png"))) if Image.open(f).size[0] == 2 * r + 8), None)
    if not sp:
        continue
    im = Image.open(sp).convert("RGBA")
    W, Hh = im.size
    n = int(kv["frames"]) if "frames" in kv else Hh // W
    fh = Hh // n
    rows.append((kv["key"], r, vs, vy, im, W, fh, n, kv.get("ink_dim", "FFFFFF")))
cw = max(rr[5] for rr in rows) + 16
ch = max(rr[6] for rr in rows) + 30
sheet = Image.new("RGB", (cw * 7, ch * len(rows)), (150, 175, 150))
d = ImageDraw.Draw(sheet)
for ri, (key, r, vs, vy, im, W, fh, n, ink) in enumerate(rows):
    for j in range(7):
        k = round(j * (n - 1) / 6)
        fr = im.crop((0, k * fh, W, (k + 1) * fh))
        x0, y0 = j * cw + (cw - W) // 2, ri * ch + 24
        sheet.paste(fr, (x0, y0), fr)
        if vs:
            f = ImageFont.truetype(FONT, vs)
            hbox = max(26, round(1.2 * vs))
            cy = y0 + fh / 2 + r + 2 + vy + hbox / 2
            col = tuple(int(ink[i:i + 2], 16) for i in (0, 2, 4))
            d.text((x0 + W / 2, cy), txt, font=f, fill=col, anchor="mm")
    d.text((4, ri * ch + 4), "%s r=%d vs=%d vy=%d" % (key, r, vs, vy), fill=(0, 0, 0))
sheet.save(out)
print(out, sheet.size)
