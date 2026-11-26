#!/usr/bin/env python3
"""Offline pictures of the built PoliPlug skin in given states: what MPC should draw, with live text filled in.

    python3 test/preview.py "<skin>/Plugin Skins" test/preview

Like tools/studio.py preview, but with several parameters set at once (rows used, arrows, the open view) and sample
text for the readouts, so the rack and an opened hosted page can be checked against the mockup before a device build.
"""
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
MV = os.environ.get("MPC_VST", os.path.normpath(os.path.join(HERE, "..", "..", "mpc-vst-plugins")))
FONTS = {s: os.path.join(MV, "tools", "html_art", "fonts", "TitilliumWeb-%s.ttf" % s) for s in ("Bold", "SemiBold", "Regular")}
W, H = 1280, 628


def conds(c):
    return [tuple(int(g) for g in m.groups()) for m in
            (re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)$", h) for h in c["bounds"].get("additionalInvalidatingHandles", []))
            if m]


def xywh(b):
    return [int(float(v)) for v in b["bounds"].split()]


def draw(skin, defs, comps, state, texts, im, dr, ox=0, oy=0, binding=None):
    for c in comps:
        if not all(state.get(p, 0) == i for i, n, p in conds(c)):
            continue
        cd = c["componentData"]
        x, y, w, h = xywh(c["bounds"])
        x, y = x + ox, y + oy
        hmap = {m["key"]: int(m["value"].split()[-1]) for m in c["handle remapping"]["map"] if m["value"].startswith("Parameter")}
        hmap = hmap or binding or {}
        t = cd["type"]
        if t == "Image":
            if cd["data"].get("image"):
                img = Image.open(os.path.join(skin, cd["data"]["image"])).convert("RGBA")
                im.paste(img, (x, y), img)
        elif t == "Knob":
            st = Image.open(os.path.join(skin, cd["data"]["filmStrip"])).convert("RGBA")
            n = int(cd["data"]["numFrames"])
            fh = st.size[1] // n
            p = hmap.get(cd["data"].get("handleName", "Data"))
            k = min(n - 1, round(state.get(("v", p), 0.5) * (n - 1)))
            fr = st.crop((0, k * fh, st.size[0], (k + 1) * fh))
            im.paste(fr, (x, y), fr)
        elif t == "Button":
            p = hmap.get("Data")
            img = cd["data"]["onImage" if state.get(("v", p), 0) > 0.5 else "offImage"]
            if img:
                img = Image.open(os.path.join(skin, img)).convert("RGBA")
                im.paste(img, (x, y), img)
        elif t == "Label":
            p = hmap.get(cd["data"].get("handleName", "Data"))
            text = texts.get(p, "")
            if text:
                ts = cd["data"]["textStyle"]
                col = ts["colour"]
                rgba = tuple(int(col[i:i + 2], 16) for i in (2, 4, 6))
                f = ImageFont.truetype(FONTS.get(ts["font"]["style"], FONTS["SemiBold"]), int(ts["font"]["height"] * 0.8))
                bb = dr.textbbox((0, 0), text, font=f)
                tw, th = bb[2] - bb[0], bb[3] - bb[1]
                just = ts.get("justification", "")
                tx = x if just.startswith("left") else x + w - tw if just.startswith("right") else x + (w - tw) // 2
                dr.text((tx - bb[0], y + (h - th) // 2 - bb[1]), text, font=f, fill=rgba)
        elif t in defs:
            draw(skin, defs, defs[t]["componentsData"], state, texts, im, dr, x, y, hmap)
        elif t != "Focus":
            pass


def render(skin, state, texts, out):
    tui = json.load(open(os.path.join(skin, "TUI.json")))["pageData"]
    defs = {d["key"]: d["value"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}
    page = defs[tui["tabs"][0]["componentName"]]
    im = Image.new("RGB", (W, H), (0, 0, 0))
    draw(skin, defs, page["componentsData"], state, texts, im, ImageDraw.Draw(im))
    im.save(out)
    return out


def main(skin, outdir):
    sys.path.insert(0, os.path.join(HERE, "..", "src"))
    hdr = open(os.path.join(HERE, "..", "src", "pp_gen.h")).read()
    num = lambda name: int(re.search(r"#define %s (\d+)" % name, hdr).group(1))
    row0 = int(re.search(r"PI_ROW\(r\) \((\d+)", hdr).group(1))
    e0 = int(re.search(r"PI_E\(n\) \(\(n\) < \d+ \? (\d+)", hdr).group(1))
    rx0 = int(re.search(r"PI_RX\(r\) \((\d+)", hdr).group(1))   # 1.4.0: +0 onst, +2 par, +3 send, +4 scst
    os.makedirs(outdir, exist_ok=True)
    # rack: three plugins (first: up dim, middle: both, last: down dim), page 1 of 1
    st = {}
    tx = {num("PI_PAGE_TEXT"): "1 / 1", num("PI_STATUS"): "3/16"}
    # 1.5.0: the macro column (Q-Link page 1 shown; knobs 1-3 mapped, 4 empty)
    mk0 = int(re.search(r"PI_MK\(k\) \((\d+)", hdr).group(1))
    st[num("PI_MK_SHOWN")] = 1
    st[num("PI_MK_PAGE")] = 1
    st[num("PI_MK_MAPST")] = 1
    for k, (nm, fx, v, t) in enumerate([("Intensity", "1 Poli-Ess", 0.6, "60 %"), ("Threshold", "1 Poli-Ess", 0.4, "-24.0 dB"),
                                        ("Mix", "3 Poli-Ess", 1.0, "100 %"), ("Empty", "Map to assign", 0, "--")]):
        st[mk0 + 4 * k + 1] = 2 if k < 3 else 1
        st[("v", mk0 + 4 * k)] = v
        tx[mk0 + 4 * k] = t
        tx[mk0 + 4 * k + 2] = nm
        tx[mk0 + 4 * k + 3] = fx
    names = ["1  Poli-Ess", "2  Poli-Ess", "3  Poli-Ess"]
    for r in range(3):
        st[row0 + r * 8 + 1] = 1
        st[row0 + r * 8 + 2] = 1 if r == 0 else 2
        st[row0 + r * 8 + 3] = 1 if r == 2 else 2
        st[rx0 + r * 6] = 1 if r == 1 else 2   # On, Off, On
        tx[row0 + r * 8] = names[r]
    print(render(skin, st, tx, os.path.join(outdir, "rack.png")))
    # 1.5.0: Map on, knob 4 armed (Clear shown)
    stm, txm = dict(st), dict(tx)
    stm[num("PI_MK_MAPST")] = 2
    stm[num("PI_MK_CLRST")] = 1
    stm[mk0 + 4 * 3 + 1] = 3
    txm[num("PI_STATUS")] = "Map: macro 4 picked. Open an effect and move a control (or turn another knob)"
    print(render(skin, stm, txm, os.path.join(outdir, "rack_map.png")))
    # the + Add plugin list (rows hidden while it is up)
    pick0 = int(re.search(r"PI_PICK\(n\) \((\d+)", hdr).group(1))
    st2, tx2 = {}, {num("PI_PAGE_TEXT"): "1 / 2", num("PI_STATUS"): "Pick an effect (10 installed)  |  tap + Add plugin to cancel"}
    for k, nm in enumerate(["AW Ambience", "AW Consoles", "AW Dynamics", "Boris Granular", "Cwo", "Delay", "Liminal Hz", "Poli-Ess"]):
        st2[pick0 + 3 * k + 2] = 1
        tx2[pick0 + 3 * k + 1] = nm
    print(render(skin, st2, tx2, os.path.join(outdir, "rack_add_list.png")))
    # an opened Poli-Ess (view 1): knobs at their defaults, meter partly down
    st3 = {num("PI_VIEW"): 1, num("PI_EDITING"): 1, ("v", e0 + 0): 0.5, ("v", e0 + 1): 0.5, ("v", e0 + 2): 0.43,
           ("v", e0 + 4): 0.35}
    st3[e0 + 9] = 7   # Poli-Ess 1.0.6's own preset selector (its "preset" = parameter 9): Heavy Sibilance
    print(render(skin, st3, {}, os.path.join(outdir, "editor_poli-ess.png")))
    # 1.4.0: the Parallel tab: three tracks, each its own Send, On/Off, SC (track 1 ducking)
    st4 = {num("PI_SECTION"): 1}
    tx4 = {num("PI_PAGE_TEXT"): "1 / 1", num("PI_STATUS"): "Parallel 3/16  |  Q-Link pages: the Sends, 4 per page"}
    for r, (nm, send, on, sc) in enumerate([("1  Delay", 40, 2, 2), ("2  AW Ambience", 25, 2, 1), ("3  Boris Granular", 70, 1, 1)]):
        st4[row0 + r * 8 + 1] = 1
        st4[row0 + r * 8 + 2] = 1 if r == 0 else 2
        st4[row0 + r * 8 + 3] = 1 if r == 2 else 2
        st4[rx0 + r * 6] = on
        st4[rx0 + r * 6 + 2] = 1
        st4[("v", rx0 + r * 6 + 3)] = send / 100
        tx4[rx0 + r * 6 + 3] = "%d%%" % send
        st4[rx0 + r * 6 + 4] = sc
        tx4[row0 + r * 8] = nm
    print(render(skin, st4, tx4, os.path.join(outdir, "rack_parallel.png")))
    # 1.4.0: track 1's sidechain panel over the rack
    st6, tx6 = dict(st4), dict(tx4)
    st6[num("PI_SCING")] = 1
    st6[num("PI_SC_ONST")] = 2
    tx6[num("PI_SC_NAME")] = "P1  Delay"
    for k, (v, t) in zip(("PI_SC_THR", "PI_SC_ATT", "PI_SC_REL", "PI_SC_KNEE", "PI_SC_RATIO"),
                         ((0.5, "-30 dB"), (0.566, "5.0 ms"), (0.607, "250 ms"), (0.25, "6 dB"), (0.463, "4.0:1"))):
        st6[("v", num(k))] = v
        tx6[num(k)] = t
    st6[("v", num("PI_SC_GR"))] = 9 / 24
    print(render(skin, st6, tx6, os.path.join(outdir, "rack_sidechain.png")))
    # an effect with no selector of its own (here the test effect "X Presets", drawn with a copy of Poli-Ess's skin):
    # PoliPlug's preset bar over its page, then the browser
    reg = {l.split("\t")[3]: int(l.split("\t")[0]) for l in open(os.path.join(skin, "pp_registry.txt")).read().splitlines() if l}
    if "X Presets" in reg:
        v = reg["X Presets"]
        st5 = {num("PI_VIEW"): v, num("PI_EDITING"): 1, num("PI_HPRE_SHOWN"): 1, ("v", e0 + 0): 0.5, ("v", e0 + 1): 0.3}
        tx5 = {num("PI_HPRE_NAME"): "Charlie"}
        print(render(skin, st5, tx5, os.path.join(outdir, "editor_preset_bar.png")))
        hp = int(re.search(r"PI_HPICK\(n\) \((\d+)", hdr).group(1))
        st5[num("PI_HBROWSING")] = 1
        tx5[num("PI_HPRE_PAGE")] = "1 / 2"
        names = ["Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India", "Juliett", "Kilo", "Lima",
                 "Mike", "November", "Oscar", "Papa"]
        for k, nm in enumerate(names):
            st5[hp + 3 * k + 2] = 2 if k == 2 else 1
            tx5[hp + 3 * k + 1] = nm
        print(render(skin, st5, tx5, os.path.join(outdir, "editor_preset_browser.png")))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
