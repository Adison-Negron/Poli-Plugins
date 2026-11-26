#!/usr/bin/env python3
"""Draws the skin as MPC would show it for a set of parameter values (JSON from test/snapshot.cpp, or defaults):
    python3 test/compose_preview.py [values.json] out.png [learn=1] [bypass=1] [diff=1]"""
import json, re, sys
from PIL import Image, ImageDraw, ImageFont
FONT = "fonts/TitilliumWeb-SemiBold.ttf"   # MPC draws live value text in Titillium Web SemiBold
vals = json.load(open(sys.argv[1])) if len(sys.argv) > 2 else {}
out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
for a in sys.argv[3:]:
    k, v = a.split("="); vals[k] = float(v)
RANGES = {"threshold": (-30, 30), "reduction": (0, 100), "output": (-24, 24)}
DEF = {"threshold": 0, "reduction": 12, "output": 0}
def text(k):
    v = vals.get(k, DEF.get(k, 0))
    return "%d dB" % round(v) if k == "reduction" else ("%+.1f dB" % v).replace("+0.0", "+0.0").replace("-0.0", "+0.0")
texts = []
def norm(k):
    if k.startswith("sp") or k == "learn_prog": return vals.get(k, 0)
    if re.fullmatch(r"n\d", k): return (vals.get(k, -60) + 120) / 120
    lo, hi = RANGES[k]; return (vals.get(k, DEF[k]) - lo) / (hi - lo)
bg = Image.open("vst/skin_assets/bg.png").convert("RGBA")
for line in open("vst/layout.conf").read().splitlines():
    kind = line.split(" ")[0]
    kv = dict((m[0], m[1].strip('"')) for m in re.findall(r'(\w+)=("[^"]*"|\S+)', line))
    if "when" in kv:
        p, o = kv["when"].split(":")
        if int(vals.get(p, 2 if p.endswith("x") else 0)) != int(o): continue
    if kind == "knob":
        r, n = int(kv["r"]), int(kv["frames"]); box = 2 * r + 8
        cx, cy = int(kv["cx"]), int(kv["cy"]) - 86
        f = round(max(0, min(1, norm(kv["key"]))) * (n - 1))
        strip = Image.open("vst/" + kv["strip"]).convert("RGBA")
        bg.alpha_composite(strip.crop((0, f * box, box, f * box + box)), (cx - box // 2, cy - box // 2))
        texts.append((cx, cy - r - 5 + 28 + 15, text(kv["key"]), int(kv["vs"]), (255, 255, 255)))
        continue
    if kind == "slider_v" and kv["key"] in RANGES:
        h = int(kv["h"]); texts.append((int(kv["cx"]), int(kv["cy"]) - 86 - h // 2 + h + 2 + 13, text(kv["key"]), int(kv["vs"]), (0, 0, 0)))
    if kind in ("meter", "slider_v"):
        w, h, n = int(kv["w"]), int(kv["h"]), int(kv["frames"])
        cx, cy = int(kv["cx"]), int(kv["cy"]) - 86
        f = round(max(0, min(1, norm(kv["key"]))) * (n - 1))
        strip = Image.open("vst/" + kv["strip"]).convert("RGBA")
        bg.alpha_composite(strip.crop((0, f * h, w, f * h + h)), (cx - w // 2, cy - h // 2))
    elif kind in ("toggle", "art") and "img" in kv or kind == "art" and "x" in kv:
        if kind == "toggle":
            w, h = int(kv["w"]), int(kv["h"]); x, y = int(kv["cx"]) - w // 2, int(kv["cy"]) - 86 - h // 2
            img = kv["img_on"] if vals.get(kv["key"], 0) >= 0.5 else kv["img"]
        else:
            w, h = int(kv["w"]), int(kv["h"]); x, y = int(kv["x"]), int(kv["y"]) - 86; img = kv["file"]
        bg.alpha_composite(Image.open("vst/" + img).convert("RGBA").resize((w, h)), (x, y))
d = ImageDraw.Draw(bg)
for x, y, t, px, col in texts:
    d.text((x, y), t, font=ImageFont.truetype(FONT, px), fill=col, anchor="mm")
bg.convert("RGB").save(out)
print("wrote", out)
