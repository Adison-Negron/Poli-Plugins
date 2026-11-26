#!/usr/bin/env python3
"""A picture of the page as MPC should draw it at given values (studio.py preview draws every knob at one fixed frame):
    python3 test/compose_preview.py <wow 0-100> <mix 0-100> "<preset name>" <out.png>
The number in each face is drawn where MPC puts the knob's value text (layout.conf vs/vy), in Titillium SemiBold."""
import os, re, sys
from PIL import Image, ImageDraw, ImageFont
H = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(H, "vst")
wv, mv, preset, out = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3], sys.argv[4]
font = lambda px: ImageFont.truetype(os.path.join(H, "fonts", "TitilliumWeb-SemiBold.ttf"), px)
pg = Image.open(os.path.join(A, "skin_assets", "bg.png")).convert("RGBA")
d = ImageDraw.Draw(pg)
for l in open(os.path.join(A, "layout.conf")).read().splitlines():
    m = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', l))
    if l.startswith("knob"):
        st = Image.open(os.path.join(A, m["strip"])).convert("RGBA")
        n, b = int(m["frames"]), st.width
        v = wv if m["key"] == "wow" else mv
        i = round(v / 100 * (n - 1))
        cx, cy = int(m["cx"]), int(m["cy"]) - 86
        pg.alpha_composite(st.crop((0, i * b, b, (i + 1) * b)), (cx - b // 2, cy - b // 2))
        vs, r, vy = int(m["vs"]), int(m["r"]), int(m["vy"])
        d.text((cx, cy + r + 2 + vy + max(26, round(vs * 1.2)) / 2), "%d%%" % round(v), font=font(vs), fill="black", anchor="mm")
    elif l.startswith("readout"):
        d.text((int(m["cx"]), int(m["cy"]) - 86), preset, font=font(int(m["tsize"])), fill="black", anchor="mm")
pg.convert("RGB").save(out)
