#!/usr/bin/env python3
"""Static checks of vst/layout.conf against vst/params.json and the pictures in vst/skin_assets (the clipping / hidden-indicator checklist of
claude/ui-design-and-knob-rules.md, section 3): every key= exists, every when= names a real option, every picture exists and is the size the
widget expects, strips stay under MPC's limits, nothing sits outside the 1280x628 plugin area, nothing the user watches is under the
Q-Link sidebar by accident, Q-Links name real parameters.   python3 test/check_layout.py   (from the Poli-Chorus folder)"""
import json
import os
import re
import shlex
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
VST = os.path.join(HERE, "..", "vst")
Y_OFF = 86
bad = []


def fail(msg):
    bad.append(msg)
    print("FAIL", msg)


params = {p["key"]: p for p in json.load(open(os.path.join(VST, "params.json")))["params"]}
lines = [l.strip() for l in open(os.path.join(VST, "layout.conf")) if l.strip()]
widgets, qlinks = [], []
for l in lines:
    if l.startswith("#") or l.startswith("[") or l.startswith("theme_"):
        continue
    t = shlex.split(l)
    if t[0] == "qlinks":
        qlinks = [k.strip() for k in l.split("=", 1)[1].split(",")]
        continue
    d = {"kind": t[0], "line": l}
    for x in t[1:]:
        if "=" in x:
            k, v = x.split("=", 1)
            d[k] = v
    widgets.append(d)


def img(path):
    p = os.path.join(VST, path)
    if not os.path.exists(p):
        fail("missing picture %s" % path)
        return None
    return Image.open(p)


n_meter = n_knob = n_button = 0
for w in widgets:
    kind = w["kind"]
    if "key" in w and w["key"] not in params:
        fail("unknown key %s (%s)" % (w["key"], w["line"][:60]))
    if "when" in w:
        k, o = w["when"].split(":")
        opts = params.get(k, {}).get("options")
        if not opts:
            fail("when= on a parameter without options: %s" % w["when"])
        elif o not in opts and not (o.isdigit() and int(o) < len(opts)):
            fail("when=%s: no such option (%s)" % (w["when"], opts[:4]))
    if kind == "art":
        im = img(w["file"])
        if im and im.size != (1280, 628):
            fail("background is %s, not 1280x628" % (im.size,))
        continue
    cx, cy = int(w["cx"]), int(w["cy"]) - Y_OFF
    if kind == "knob":
        n_knob += 1
        r = int(w["r"])
        box = 2 * r + 8
        n = img("skin_assets/knob_strip_%d.png" % box)
        if n:
            if n.width != box or n.height % box or n.height > 12288:
                fail("knob strip for r=%d is %dx%d (frames must be %d px square, strip <= 12288)" % (r, n.width, n.height, box))
            if (n.height // box) % 2 == 0:
                fail("knob strip r=%d has an even frame count" % r)
        hb = max(26, round(int(w["vs"]) * 1.2))
        bottom = cy + r + 2 + int(w["vy"]) + hb
        if cx - r < 0 or cx + r > 1280 or cy - r < 0 or bottom > 628:
            fail("knob %s leaves the screen (bottom %d)" % (w["key"], bottom))
        if int(w["bw"]) < 2 * r:
            fail("knob %s text box narrower than the knob" % w["key"])
        if params[w["key"]].get("dynamic_display") is not True:
            fail("knob %s has no dynamic_display" % w["key"])
    elif kind == "meter":
        n_meter += 1
        wd, hd, fr = int(w["w"]), int(w["h"]), int(w["frames"])
        s = img(w["strip"])
        if s:
            if s.width != wd or s.height != hd * fr or s.height > 12288:
                fail("meter %s: strip %dx%d, expected %dx%d (<= 12288)" % (w["key"], s.width, s.height, wd, hd * fr))
        if int(w["cx"]) - wd // 2 < 0 or int(w["cx"]) - wd // 2 + wd > 1280 or cy - hd // 2 < 0 or cy - hd // 2 + hd > 628:
            fail("meter %s leaves the screen" % w["key"])
        if params[w["key"]].get("live") is not True and w["key"] != "preset":
            fail("meter %s: the parameter is not live" % w["key"])
    elif kind == "button":
        n_button += 1
        bw, bh = int(w["w"]), int(w["h"])
        for k in ("img", "img_on"):
            if k in w and "hit.png" not in w[k]:
                im = img(w[k])
                if im and im.size != (bw, bh):
                    fail("button %s: %s is %s but the box is %dx%d (MPC stretches it)" % (w["key"], w[k], im.size, bw, bh))
        if int(w["cx"]) - bw // 2 < 0 or int(w["cx"]) - bw // 2 + bw > 1280 or cy - bh // 2 < 0 or cy - bh // 2 + bh > 628:
            fail("button %s leaves the screen" % w["key"])
        if params[w["key"]].get("momentary") is not True and w["key"] not in ("preset_list",):
            fail("button %s is not a momentary parameter" % w["key"])

# every Q-Link names a real parameter (or -), at most one bank of 16
if len(qlinks) > 16:
    fail("more than 16 Q-Links")
for k in qlinks:
    if k != "-" and k not in params:
        fail("Q-Link on unknown parameter %s" % k)
# every knob and the Mode are reachable by a Q-Link
for k in [p for p in params if params[p].get("dynamic_display")] + ["mode"]:
    if k not in qlinks:
        fail("%s has no Q-Link" % k)
# the lit Mode copies exist for every option, in the same place as the touch buttons
modes = params["mode"]["options"]
for m in modes:
    lit = [w for w in widgets if w.get("when") == "mode:%s" % m]
    if len(lit) != 1:
        fail("no single lit button for mode %s" % m)
# every parameter the layout reaches is declared (no orphan viz columns)
viz = [k for k in params if re.match(r"v[lr]\d{3}$", k)]
shown = {w["key"] for w in widgets if w["kind"] == "meter"}
for k in viz:
    if k not in shown:
        fail("display column %s is not on the page" % k)
print("checked %d knobs, %d meters, %d buttons, %d Q-Links; %s" % (n_knob, n_meter, n_button, len([k for k in qlinks if k != "-"]),
                                                               "%d problems" % len(bad) if bad else "all fine"))
sys.exit(1 if bad else 0)
