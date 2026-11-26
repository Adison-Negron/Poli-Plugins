#!/usr/bin/env python3
"""A picture of the page as MPC should draw it in a given state (studio.py preview draws every control at one fixed
frame). python3 test/compose_preview.py <band 0-2> <bypass 0/1> <out.png>"""
import math, os, re, sys
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(HERE, "vst", "skin_assets")
band, byp, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
Y = 86
lay = open(os.path.join(HERE, "vst", "layout.conf")).read().splitlines()
page = Image.open(os.path.join(A, "bg.png")).convert("RGBA")
font = ImageFont.truetype(os.path.join(HERE, "fonts", "TitilliumWeb-SemiBold.ttf"), 17)
# a plausible state: band 1 compressing a kick, values like the mockup's
vals = {0: dict(c_input=("0 db", .5), c_output=("2 db", .54), c_thresh=("-18 db", .7), c_attack=("3 ms", .492), c_release=("100 ms", .5),
                c_knee=("6 db", .25), c_ratio=("4:1", .4), x_freq=("500 hz", .466), u_ratio=("2:1", .33),
                u_thresh=("-40 db", .33), m=(22, 25, 18, 9)),
        1: dict(c_input=("0 db", .5), c_output=("0 db", .5), c_thresh=("-24 db", .6), c_attack=("10 ms", .667), c_release=("150 ms", .56),
                c_knee=("0 db", 0), c_ratio=("2:1", .23), x_freq=("", .5), u_ratio=("1.5:1", .24),
                u_thresh=("-36 db", .4), m=(20, 21, 15, 7), range_text="500 hz - 4k"),
        2: dict(c_input=("0 db", .5), c_output=("0 db", .5), c_thresh=("0 db", 1), c_attack=("1 ms", .333), c_release=("60 ms", .41),
                c_knee=("0 db", 0), c_ratio=("1:1", 0), x_freq=("4k", .767), u_ratio=("3:1", .47),
                u_thresh=("-30 db", .5), m=(19, 15, 29, 14))}[band]
def paste(img, x, y):
    page.alpha_composite(img, (int(x), int(y)))
for l in lay:
    m = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', l))
    when = m.get("when")
    if when and int(when.split(":")[1]) != band:
        continue
    if l.startswith("picture"):
        f = m["files"].strip('"').split(",")[band]
        paste(Image.open(os.path.join(HERE, "vst", f)).convert("RGBA").resize((int(m["w"]), int(m["h"]))), int(m["x"]), int(m["y"]) - Y)
    elif l.startswith("art file=skin_assets/freq") or l.startswith("art file=skin_assets/label"):
        paste(Image.open(os.path.join(HERE, "vst", m["file"])).convert("RGBA"), int(m["x"]), int(m["y"]) - Y)
    elif l.startswith("knob"):
        strip = Image.open(os.path.join(HERE, "vst", m["strip"])).convert("RGBA")
        n = int(m["frames"]); bw = strip.width
        text, v = vals[m["key"]]
        fr = strip.crop((0, round(v * (n - 1)) * bw, bw, (round(v * (n - 1)) + 1) * bw))
        cx, cy = int(m["cx"]), int(m["cy"]) - Y
        paste(fr, cx - bw // 2, cy - bw // 2)
        ImageDraw.Draw(page).text((cx, cy + 11), text, font=font, fill="white", anchor="mm")
    elif l.startswith("readout"):
        f2 = ImageFont.truetype(os.path.join(HERE, "fonts", "TitilliumWeb-SemiBold.ttf"), int(float(m["tsize"])))
        ImageDraw.Draw(page).text((int(m["cx"]), int(m["cy"]) - Y), vals.get(m["key"], ""), font=f2, fill="white", anchor="mm")
    elif l.startswith("toggle"):
        paste(Image.open(os.path.join(HERE, "vst", m["img_on" if byp else "img"])).convert("RGBA"),
              int(m["cx"]) - int(m["w"]) // 2, int(m["cy"]) - Y - int(m["h"]) // 2)
    elif l.startswith("meter"):
        k = ["m_out", "m_in", "m_ct", "m_ut"].index(m["key"])
        strip = Image.open(os.path.join(HERE, "vst", m["strip"])).convert("RGBA")
        w, h = int(m["w"]), int(m["h"])
        f = vals["m"][k]
        paste(strip.crop((0, f * h, w, (f + 1) * h)), int(m["cx"]) - w // 2, int(m["cy"]) - Y - h // 2)
page.convert("RGB").save(out)
