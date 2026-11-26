#!/usr/bin/env python3
"""Knob audit: does the value text sit on the pointer? For every `knob` in each plugin's layout.conf, loads the knob filmstrip
actually used, finds the pointer pixels of every frame and intersects them with the value text's box
(text box top = cy + r + 2 + vy, height max(26, 1.2 vs): where MPC puts it, per the generators' own formula).
usage: audit_knobs.py <MPC-PLUGINS root> [plugin ...]"""
import glob
import os
import shlex
import sys

from PIL import Image, ImageFont

root = sys.argv[1]
plugins = sys.argv[2:] or ["Poli-ess", "PoliPlug", "Poliwow", "Poli-Shhhh", "Poli-q", "Poli-verb", "Poli-delay", "Poli-tea", "Poli-Multiband"]
FONT = os.path.join(root, "Poli-Multiband", "fonts", "TitilliumWeb-SemiBold.ttf")
DB_SAMPLES = {"short": "0 dB", "mid": "-12.5 dB", "long": "+18.0 dB", "hz": "20.0 kHz"}
PLAIN_SAMPLES = {"pct": "100 %", "ms": "500 ms", "x": "1.25x", "s": "10 s", "k": "12.5k"}
DB_KEYS = set(filter(None, os.environ.get("DB_KEYS", "").split(",")))   # knobs that show dB / Hz; the rest show %, ms, s, x (env ALLWIDE=1: all wide)
ALLWIDE = os.environ.get("ALLWIDE") == "1"


def text_w(s, px):
    try:
        return ImageFont.truetype(FONT, int(px)).getlength(s)
    except OSError:
        return 0.55 * px * len(s)


def find_strip(vst, kv, r):
    if "strip" in kv:
        return os.path.join(vst, kv["strip"])
    want = 2 * r + 8
    for f in sorted(glob.glob(os.path.join(vst, "skin_assets", "knob_strip*.png"))):
        if Image.open(f).size[0] == want:
            return f
    return None


for p in plugins:
    vst = os.path.join(root, p, "vst")
    print("=" * 8, p)
    seen = set()
    for line in open(os.path.join(vst, "layout.conf")):
        line = line.strip()
        if not line.startswith("knob "):
            continue
        parts = shlex.split(line)
        kv = dict(x.split("=", 1) for x in parts[1:] if "=" in x)
        r, vs, vy = int(kv["r"]), int(kv.get("vs", 0)), int(kv.get("vy", 0))
        sig = (kv.get("strip"), r, vs, vy)
        if sig in seen:
            continue
        seen.add(sig)
        sp = find_strip(vst, kv, r)
        if sp is None or not os.path.exists(sp):
            print("  %-12s r=%d: strip not found" % (kv["key"], r))
            continue
        im = Image.open(sp).convert("RGBA")
        W, Hh = im.size
        n = int(kv["frames"]) if "frames" in kv else None
        if n is None:
            n = Hh // W           # knob_strip_<2r+8>.png: square frames
        fh = Hh // n
        # body colour = most common opaque colour of the middle frame
        mid = im.crop((0, (n // 2) * fh, W, (n // 2 + 1) * fh))
        px = mid.load()
        from collections import Counter
        cnt = Counter(px[x, y][:3] for y in range(fh) for x in range(W) if px[x, y][3] > 250)
        body = cnt.most_common(1)[0][0] if cnt else (0, 0, 0)
        bl = sum(body) / 3
        if vs == 0:
            print("  %-12s r=%-3d no value text (strip %s)" % (kv["key"], r, os.path.basename(sp)))
            continue
        hbox = max(26, round(1.2 * vs))
        top = r + 2 + vy                    # text box top relative to the knob centre
        cyt = top + hbox / 2                # centre of the text
        th = 0.72 * vs                      # digit height
        res = {}
        SAMPLES = DB_SAMPLES if (ALLWIDE or kv['key'] in DB_KEYS) else PLAIN_SAMPLES
        for name, s in SAMPLES.items():
            tw = text_w(s, vs)
            x0, x1, y0, y1 = -tw / 2 - 1, tw / 2 + 1, cyt - th / 2 - 1, cyt + th / 2 + 1
            hit = 0
            worst = 0
            for k in range(n):
                fr = im.crop((0, k * fh, W, (k + 1) * fh)).load()
                c = 0
                for y in range(fh):
                    for x in range(W):
                        a = fr[x, y]
                        if a[3] < 128 or abs(sum(a[:3]) / 3 - bl) < 70:
                            continue
                        X, Y = x - W / 2, y - fh / 2
                        if x0 <= X <= x1 and y0 <= Y <= y1:
                            c += 1
                if c:
                    hit += 1
                    worst = max(worst, c)
            res[name] = (hit, worst, round(tw))
        bw = int(kv.get("bw", 0))
        flag = ""
        if any(v[0] for v in res.values()):
            flag = "  <-- POINTER UNDER TEXT"
        if bw and max(v[2] for v in res.values()) > bw - 4:
            flag += "  <-- text wider than box (bw=%d)" % bw
        print("  %-12s r=%-3d vs=%-2d text centre %+.0f px, frames %d/%d: %s%s" % (
            kv["key"], r, vs, cyt, n, n,
            " ".join("%s %d fr (%dpx,w%d)" % (k, v[0], v[1], v[2]) for k, v in res.items()), flag))
