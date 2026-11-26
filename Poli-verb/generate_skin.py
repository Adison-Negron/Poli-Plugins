#!/usr/bin/env python3
"""Poli-verb skin: turns the Canva mockups (imgs/) into the MPC skin's images AND writes vst/layout.conf, so every
coordinate lives in this one file.

    python3 generate_skin.py            (from the Poli-verb folder; needs Pillow, numpy)

imgs/: BG.jpg = the clean frame (title, header, panel), Main/Space/Tail.jpg = the three pages with their controls drawn.
The drawn controls are taken out of the page pictures (pasting the clean panel over them) and become live widgets:
knobs (value inside), toggles (outlined = off, filled = on), the Space drawing, the room-size letter.

Geometry (docs/NOTES.md, MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628, layout.conf y = screen y + 86; the 1920x1080
mockup is scaled uniformly by 628/1080 and left-aligned. Knobs are bigger than the mockup's where there was room (user's
standing rule: the MPC Live III screen is small): r 54..60 in the grids, value text 24-30 px.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "shared"))
sys.path.insert(0, os.path.join(HERE, "presets"))
sys.path.insert(0, HERE)
import poli_preset_ui as ppu  # noqa: E402  (fonts and arrow drawing shared by the Poli plugins)
import factory  # noqa: E402
import poli_native as pn  # noqa: E402
import make_params  # noqa: E402

IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H
# Horizontal-only stretch (2026-11-09): the mockup's design ends at x=1812 of 1920, i.e. ~1054 px of the 1280 px plugin
# area, which left the right side empty. KX widens x positions AND the baked art so the design ends MARGIN px from the right
# edge (the left margin is ~63 px). Knob radii, value text and every size drawn in code stay as they were.
RIGHT_EDGE, MARGIN = 1812, 64
KX = (W - MARGIN) / (RIGHT_EDGE * S)
SX = S * KX
SS = 4

SLATE = (49, 75, 90)
YELLOW = (255, 225, 103)
PANEL = (221, 230, 229)
WHITE = (255, 255, 255)
BLACK = (0, 0, 5)
GREY = (190, 202, 208)

FONT = ppu.OPEN_SANS     # the mockup's lettering is Open Sans Bold


def font(px, weight=700):
    f = ImageFont.truetype(FONT, int(round(px)))
    try:
        axes = f.get_variation_axes()
        f.set_variation_by_axes([weight] + [a["default"] for a in axes[1:]])
    except (OSError, AttributeError):
        pass
    return f


def sx(x):
    return x * SX


def sy(y):
    return y * S


def down(im, w, h):
    return im.resize((w, h), Image.Resampling.LANCZOS)


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


# ---- mockup geometry (measured on imgs/*.jpg, 1920x1080) ------------------------------------------------------------
# page 0 General, 1 Space, 2 Tail. Knob: key -> (page, mockup cx, cy, screen radius, value px)
R_GEN, R_SPC, R_ER, R_TAIL, R_MIX = 54, 60, 48, 60, 106
KNOBS = {
    "mix": (0, 395.0, 686.5, R_MIX, 42),
    "input": (0, 1245.5, 534.5, R_GEN, 20), "output": (0, 1438.0, 534.5, R_GEN, 20), "drive": (0, 1634.5, 531.5, R_GEN, 26),
    "low": (0, 1245.5, 825.0, R_GEN, 20), "tilt": (0, 1438.0, 824.5, R_GEN, 20), "high": (0, 1634.5, 822.5, R_GEN, 20),
    "size": (1, 274.0, 503.0, R_SPC, 28), "predelay": (1, 561.0, 503.0, R_SPC, 26), "width": (1, 865.0, 498.0, R_SPC, 28),
    "er_level": (1, 244.5, 845.0, R_ER, 24), "er_prob": (1, 438.0, 845.0, R_ER, 24),
    "diffusion": (2, 297.0, 499.0, R_TAIL, 28), "damping": (2, 578.0, 496.5, R_TAIL, 28),
    "modulation": (2, 854.0, 496.5, R_TAIL, 28), "density": (2, 1147.0, 502.0, R_TAIL, 28),
    "spread": (2, 297.0, 794.0, R_TAIL, 28), "bassmult": (2, 578.0, 794.0, R_TAIL, 26),
    "attack": (2, 858.0, 794.0, R_TAIL, 26), "release": (2, 1141.0, 794.0, R_TAIL, 26),
    "decay": (2, 1542.0, 794.0, R_TAIL, 26),
}
MOCK_R = {"mix": 182.5}   # drawn knob radii in the mockup (default 94 / 85.5 / 64.5), for taking them out of the page
MOCK_R_DEFAULT = {0: 86, 1: 95, 2: 95}
# toggles: key -> (page, mockup box x0,y0,x1,y1, shape, label)
TOGGLES = {
    "freeze": (0, (841, 540, 1089, 616), "pill", "Freeze"),
    "sync": (1, (604, 806, 808, 886), "pill", "Sync"),
    "wet_l": (1, (728, 590, 814, 676), "circle", "L"),
    "wet_r": (1, (908, 590, 994, 676), "circle", "R"),
}
TOGGLE_PX = 25
TABS = [("General", (1262, 160, 1442, 205)), ("Space", (1448, 160, 1626, 205)), ("Tail", (1634, 160, 1810, 205))]
TAB_PX = 21
# the Space page's drawing + name, and its arrows
TYPE_BOX = (1190, 318, 1696, 905)            # mockup box of the picture (icon + name)
TYPE_ICON_MOCK = (1215, 333, 1668, 765)      # the mockup's Hall arch
TYPE_NAME_Y = 850                            # mockup y of the name's centre
TYPE_ARROWS = ((1146, 815, 1218, 897), (1663, 815, 1735, 897))
ROOM_BOX = (1420, 478, 1656, 582)            # the "S" of Tail's "Room size:"
# header: preset selector between the arrows, in the mockup's header
SEL_ARROWS_BAKED = ((548, 166, 578, 198), (1028, 166, 1058, 198))   # where the mockup draws them (erased from the background)
SEL_RAISE = 18                                                          # mockup px: the live arrows and name sit this much higher
SEL_ARROWS = tuple((a[0], a[1] - SEL_RAISE, a[2], a[3] - SEL_RAISE) for a in SEL_ARROWS_BAKED)
HEADER_LINE = (100, 205, 1815, 222)                                     # the rule under the title (black in the mockup): recoloured pastel
SEL_BOX = (548, 150, 1058, 214)

# erase regions in the page pictures (mockup px): the "Preset:" label and the tab pills are redrawn
HEADER_ERASE = [(590, 152, 740, 205), (1246, 148, 1822, 206)]
SPACE_ERASE = [(1130, 318, 1752, 912)]
TAIL_ERASE = [(1492, 492, 1584, 574)]

# ---- knob images ------------------------------------------------------------------------------------------------------
POINTER_W = 0.14
POINTER_LEN = 0.25
TRACK = (76, 104, 120)


def knob_image(angle, r, size, vp):
    """slate disc, a pastel pointer line (cut where the value text sits), and a pastel arc along the rim up to the value"""
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS / 2
    rr = r * SS
    d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=rgba(SLATE))
    a = math.radians(angle)
    ux, uy = math.sin(a), -math.cos(a)
    px, py = -uy, ux
    hw = max(POINTER_W * rr, 4.5 * SS) / 2
    r_out, r_in = rr * 0.98, rr * (0.98 - POINTER_LEN)      # a tick on the rim: the value text lives in the middle of the disc
    d.polygon([(c + ux * r_out + px * hw, c + uy * r_out + py * hw), (c + ux * r_out - px * hw, c + uy * r_out - py * hw),
               (c + ux * r_in - px * hw, c + uy * r_in - py * hw), (c + ux * r_in + px * hw, c + uy * r_in + py * hw)],
              fill=rgba(PANEL))
    # arc: a faint track over the whole travel, the pastel part up to the pointer
    ra, aw = rr * 0.885, max(0.05 * rr, 2.5 * SS)
    box = [c - ra, c - ra, c + ra, c + ra]
    d.arc(box, -225 + 360, 45, fill=rgba(TRACK), width=int(aw))
    if angle > -134.5:
        d.arc(box, 135, angle - 90 + (360 if angle - 90 < 0 else 0), fill=rgba(PANEL), width=int(aw))
    else:
        d.ellipse([c + math.sin(math.radians(-135)) * ra - aw / 2, c - math.cos(math.radians(-135)) * ra - aw / 2,
                   c + math.sin(math.radians(-135)) * ra + aw / 2, c - math.cos(math.radians(-135)) * ra + aw / 2], fill=rgba(PANEL))
    return down(big, size, size)


def strip_frames(r):
    """odd frame count whose strip stays under MPC's 12288 px (frame = 2r+10)"""
    n = 12288 // (2 * r + 10)
    return n if n % 2 else n - 1


def make_knobs():
    vps = {v[3]: v[4] for v in KNOBS.values()}
    for r in sorted(vps):
        box = 2 * r + 8
        n = strip_frames(r)
        strip = Image.new("RGBA", (box, box * n), (0, 0, 0, 0))
        for i in range(n):
            strip.paste(knob_image(-135.0 + 270.0 * i / (n - 1), r, box, vps[r]), (0, i * box))
        strip.save(os.path.join(OUT, "knob_strip_%d.png" % box), optimize=True)


# ---- toggles ---------------------------------------------------------------------------------------------------------
def make_toggles():
    sizes = {}
    for key, (page, (x0, y0, x1, y1), shape, label) in TOGGLES.items():
        w, h = round(sx(x1 - x0)), round(sy(y1 - y0))
        if shape == "circle":
            w = h = max(w, h)
        for state in (0, 1):
            im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            bw = 3 * SS
            box = [0, 0, w * SS - 1, h * SS - 1]
            rad = (h * SS) / 2 if shape == "pill" else (w * SS) / 2
            if state:
                d.rounded_rectangle(box, radius=rad, fill=rgba(SLATE))
                ink = YELLOW
            else:
                d.rounded_rectangle(box, radius=rad, fill=rgba(PANEL), outline=rgba(SLATE), width=bw)
                ink = SLATE
            f = font(TOGGLE_PX * SS)
            cap = f.getbbox("H", anchor="ls")
            tx = w * SS / 2
            ty = h * SS / 2 + (cap[3] - cap[1]) / 2
            if state and shape == "pill":     # a small lit dot: unmistakably "on"
                tw = f.getlength(label)
                tx += 9 * SS
                dr = 5 * SS
                dcx = tx - tw / 2 - 16 * SS
                d.ellipse([dcx - dr, h * SS / 2 - dr, dcx + dr, h * SS / 2 + dr], fill=rgba(YELLOW))
            d.text((tx, ty), label, font=f, fill=rgba(ink), anchor="ms")
            down(im, w, h).save(os.path.join(OUT, "%s_%s.png" % (key, "on" if state else "off")), optimize=True)
        sizes[key] = (w, h)
    return sizes


# ---- tabs ---------------------------------------------------------------------------------------------------------------
TAB_REGION = (1250, 150, 1822, 210)


def make_tabs():
    bx0, by0, bx1, by1 = TAB_REGION
    size = (round(sx(bx1 - bx0)), round(sy(by1 - by0)))
    for sel in range(3):
        big = Image.new("RGBA", (size[0] * SS, size[1] * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        for i, (name, (x0, y0, x1, y1)) in enumerate(TABS):
            on = i == sel
            box = [sx(x0 - bx0) * SS, sy(y0 - by0) * SS, sx(x1 - bx0) * SS, sy(y1 - by0) * SS]
            h = box[3] - box[1]
            d.rounded_rectangle(box, radius=h / 2, fill=rgba(YELLOW if on else WHITE))
            f = font(TAB_PX * SS, 800 if on else 650)
            cap = f.getbbox("H", anchor="ls")
            d.text(((box[0] + box[2]) / 2, (box[1] + box[3]) / 2 + (cap[3] - cap[1]) / 2), name, font=f, fill=rgba(SLATE), anchor="ms")
        down(big, *size).save(os.path.join(OUT, "tabs_%d.png" % sel), optimize=True)
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))
    return (round(sx(bx0)), round(sy(by0)), size[0], size[1])


# ---- backgrounds ------------------------------------------------------------------------------------------------------------
def load(name):
    im = Image.open(os.path.join(IMGS, name)).convert("RGB")
    assert im.size == (MOCK_W, MOCK_H), "%s must be 1920x1080" % name
    return im


def circle_mask(shapes):
    m = Image.new("L", (MOCK_W * 2, MOCK_H * 2), 0)
    d = ImageDraw.Draw(m)
    for kind, v in shapes:
        if kind == "c":
            cx, cy, r = v
            d.ellipse([(cx - r) * 2, (cy - r) * 2, (cx + r) * 2, (cy + r) * 2], fill=255)
        else:
            x0, y0, x1, y1 = v
            d.rectangle([x0 * 2, y0 * 2, x1 * 2, y1 * 2], fill=255)
    return m.resize((MOCK_W, MOCK_H), Image.Resampling.LANCZOS)


def to_screen(im):
    scaled = im.resize((round(MOCK_W * SX), H), Image.Resampling.LANCZOS)
    page = Image.new("RGB", (W, H), SLATE)
    page.paste(scaled, (0, 0))
    if scaled.width < W:
        edge = scaled.crop((scaled.width - 1, 0, scaled.width, H)).resize((W - scaled.width, H))   # carry the last column on
        page.paste(edge, (scaled.width, 0))
    return page


RING_OUT = (108, 271, 1812, 972)     # outer edge of the panel outline in the mockup (10 px stroke, radius ~50)
RING_T = 5                           # the stroke drawn natively, screen px


def make_backgrounds():
    """Native re-layout (2026-11-09): the colour blobs are stretched (they have no shape to distort); the title, the rule and the
    panel outline are redrawn / lifted at their natural aspect, and every label on the pages is a sprite centred on its stretched x."""
    clean = load("BG.jpg")
    arr = np.asarray(clean)
    arr = pn.blob_fill(arr, (96, 96, 540, 204), YELLOW, SLATE)                       # the title sits across a blob's edge
    erase = [(540, 140, 1070, 226), (1240, 140, 1830, 214), (100, 204, 1822, 225), (100, 262, 1822, 982)]
    base = Image.fromarray(pn.nearest_fill(arr, pn.box_mask(arr.shape, erase)))
    bg = to_screen(base)
    x0, y0, x1, y1 = RING_OUT
    pn.panel_fill(bg, (round(sx(x0)), round(y0 * S), round(sx(x1)), round(y1 * S)), 50 * S, PANEL)
    pn.ring(bg, (sx(x0 + 5), (y0 + 4.5) * S, sx(x1 - 5), (y1 - 4.5) * S), RING_T, 45 * S, BLACK)
    title = pn.lift(clean, base, [(96, 96, 540, 203)], open_px=5)
    pn.place(bg, title, sx, S, anchor=lambda it: "l")
    rule = pn.lift(clean, base, [HEADER_LINE[:2] + (HEADER_LINE[2], HEADER_LINE[3] + 4)], line_ratio=8)
    for r_ in rule:
        r_["colour"] = PANEL     # the black rule is the panel's pastel
    pn.place(bg, rule, sx, S)
    bg.save(os.path.join(OUT, "bg.png"), optimize=True)
    # per page: the panel interior with its drawn controls taken out, the labels re-placed at their natural size
    pages = ["Main.jpg", "Space.jpg", "Tail.jpg"]
    rect = PAGE_RECT
    outs = []
    flat = Image.new("RGB", (MOCK_W, MOCK_H), PANEL)
    for p, name in enumerate(pages):
        im = load(name)
        shapes = []
        for key, (pg, cx, cy, r, vp) in KNOBS.items():
            if pg == p:
                shapes.append(("c", (cx, cy, MOCK_R.get(key, MOCK_R_DEFAULT[pg] if key not in ("er_level", "er_prob") else 66) + 10)))
        for key, (pg, box, shape, label) in TOGGLES.items():
            if pg == p:
                shapes.append(("r", (box[0] - 8, box[1] - 8, box[2] + 8, box[3] + 8)))
        if p == 1:
            shapes += [("r", b) for b in SPACE_ERASE]
        if p == 2:
            shapes += [("r", b) for b in TAIL_ERASE]
        m = circle_mask(shapes)
        im.paste(clean, (0, 0), m)    # the clean panel is flat: pasting it removes the control
        items = pn.lift(im, flat, [(134, 296, 1786, 946)], thr=60)
        scr = Image.new("RGB", (rect[2] - rect[0], rect[3] - rect[1]), PANEL)
        pn.place(scr, items, lambda x: sx(x) - rect[0], S, dy=-rect[1])
        fn = "page_%d.png" % p
        scr.save(os.path.join(OUT, fn), optimize=True)
        outs.append((fn, rect))
    return outs


# ---- the 14 spaces: minimalist drawings on the panel ---------------------------------------------------------------------
IW, IH = round(sx(TYPE_ICON_MOCK[2] - TYPE_ICON_MOCK[0])), round(sy(TYPE_ICON_MOCK[3] - TYPE_ICON_MOCK[1]))


class Canvas:
    """a 100x100 drawing space centred in the icon box, supersampled"""
    def __init__(self):
        self.im = Image.new("RGB", (IW * SS, IH * SS), PANEL)
        self.d = ImageDraw.Draw(self.im)
        self.k = min(IW, IH) * SS / 100.0
        self.ox = (IW * SS - 100 * self.k) / 2
        self.oy = (IH * SS - 100 * self.k) / 2

    def p(self, x, y):
        return (self.ox + x * self.k, self.oy + y * self.k)

    def box(self, x0, y0, x1, y1):
        return [*self.p(x0, y0), *self.p(x1, y1)]

    def poly(self, pts, fill):
        self.d.polygon([self.p(*q) for q in pts], fill=fill)

    def line(self, pts, w, fill=SLATE):
        self.d.line([self.p(*q) for q in pts], fill=fill, width=int(w * self.k), joint="curve")
        for q in (pts[0], pts[-1]):
            x, y = self.p(*q)
            r = w * self.k / 2
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=fill)

    def ell(self, x0, y0, x1, y1, fill=None, outline=None, w=0):
        self.d.ellipse(self.box(x0, y0, x1, y1), fill=fill, outline=outline, width=int(w * self.k))

    def rect(self, x0, y0, x1, y1, fill, r=0):
        if r:
            self.d.rounded_rectangle(self.box(x0, y0, x1, y1), radius=r * self.k, fill=fill)
        else:
            self.d.rectangle(self.box(x0, y0, x1, y1), fill=fill)

    def arch(self, x0, x1, top, bottom, fill):    # a round-topped doorway
        w = x1 - x0
        self.ell(x0, top, x1, top + w, fill=fill)
        self.rect(x0, top + w / 2, x1, bottom, fill)

    def done(self):
        return down(self.im, IW, IH)


def bez(p0, p1, p2, n=14):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in [i / n for i in range(n + 1)]]


def icon(name):
    c = Canvas()
    P_, S_ = PANEL, SLATE
    if name == "Hall":
        crop = load("Space.jpg").crop(TYPE_ICON_MOCK)
        return down(crop, IW, IH)
    if name == "Room":
        c.rect(8, 10, 92, 90, S_, 3)
        c.rect(30, 32, 70, 68, P_)
        for a, b in (((8, 10), (30, 32)), ((92, 10), (70, 32)), ((8, 90), (30, 68)), ((92, 90), (70, 68))):
            c.line([a, b], 1.6, P_)
    elif name == "Chamber":
        c.rect(6, 14, 94, 86, S_, 12)
        c.rect(20, 27, 80, 73, P_, 9)
        c.rect(34, 40, 66, 60, S_, 6)
    elif name == "Plate":
        c.rect(22, 8, 78, 92, S_, 3)
        for x, y in ((31, 17), (69, 17), (31, 83), (69, 83)):
            c.ell(x - 4, y - 4, x + 4, y + 4, fill=P_)
        c.line([(34, 70), (66, 30)], 2.2, P_)
        c.line([(42, 78), (70, 44)], 1.6, P_)
    elif name == "Spring":
        c.rect(6, 34, 16, 66, S_, 2)
        c.rect(84, 34, 94, 66, S_, 2)
        pts = [(16 + 68 * t / 160.0, 50 + 22 * math.sin(2 * math.pi * 7 * t / 160.0)) for t in range(161)]
        c.line(pts, 5.5)
    elif name == "Cathedral":
        left = bez((20, 94), (20, 30), (50, 4), 18)
        right = bez((50, 4), (80, 30), (80, 94), 18)
        c.poly(left + right[1:], S_)
        wl = bez((36, 94), (36, 46), (50, 26), 14)
        wr = bez((50, 26), (64, 46), (64, 94), 14)
        c.poly(wl + wr[1:], P_)
        c.line([(50, 36), (50, 94)], 2.2, S_)
        c.ell(44, 14, 56, 26, fill=P_)
    elif name == "Arena":
        c.ell(4, 20, 96, 80, fill=S_)
        c.ell(20, 32, 80, 68, fill=P_)
        c.ell(32, 41, 68, 59, fill=S_)
        for i in range(24):
            a = 2 * math.pi * i / 24
            c.line([(50 + 27 * math.cos(a), 50 + 21 * math.sin(a)), (50 + 38 * math.cos(a), 50 + 28 * math.sin(a))], 1.3, P_)
    elif name == "Booth":
        c.rect(22, 8, 78, 92, S_, 3)
        c.rect(30, 16, 70, 84, P_)
        for i in range(7):    # foam wedges
            y = 16 + i * 9.7
            c.poly([(30, y), (30, y + 9.7), (38, y + 4.8)], S_)
            c.poly([(70, y), (70, y + 9.7), (62, y + 4.8)], S_)
        c.ell(44, 28, 56, 40, fill=S_)
        c.line([(50, 40), (50, 68)], 2.6)
        c.line([(40, 70), (60, 70)], 2.6)
    elif name == "Canyon":
        c.poly([(3, 94), (3, 14), (15, 24), (22, 8), (33, 36), (41, 30), (47, 94)], S_)
        c.poly([(97, 94), (97, 18), (85, 28), (78, 10), (67, 40), (59, 34), (53, 94)], S_)
        for r in (7, 14, 21):
            c.d.arc(c.box(50 - r, 52 - r, 50 + r, 52 + r), 215, 325, fill=S_, width=int(2.4 * c.k))
    elif name == "Ambience":
        c.ell(44, 44, 56, 56, fill=S_)
        for r, w, f in ((17, 4.5, 0.0), (29, 3.6, 0.3), (41, 2.8, 0.55)):
            col = tuple(int(S_[i] * (1 - f) + P_[i] * f) for i in range(3))
            c.ell(50 - r, 50 - r, 50 + r, 50 + r, outline=col, w=w)
    elif name == "Cave":
        c.ell(6, 8, 94, 150, fill=S_)
        c.rect(0, 90, 100, 100, P_)
        c.ell(22, 24, 78, 130, fill=P_)
        c.rect(0, 90, 100, 100, P_)
        for a, b, tip in ((30, 40, 50), (45, 57, 54), (60, 70, 46)):
            c.poly([(a, 30), (b, 30), ((a + b) / 2, tip)], S_)
        for a, b, tip in ((29, 39, 72), (57, 68, 68)):
            c.poly([(a, 90), (b, 90), ((a + b) / 2, tip)], S_)
        c.rect(6, 88, 94, 92, S_)
    elif name == "Tunnel":
        for i, (x0, x1, t) in enumerate(((6, 94, 8), (17, 83, 22), (28, 72, 36), (38, 62, 48), (45, 55, 58))):
            c.arch(x0, x1, t, 92, S_ if i % 2 == 0 else P_)
    elif name == "Garage":
        c.poly([(5, 36), (50, 8), (95, 36)], S_)
        c.rect(12, 34, 88, 92, S_)
        for y in (46, 58, 70, 82):
            c.line([(18, y), (82, y)], 2.4, P_)
    elif name == "Stairwell":
        c.poly([(8, 92), (8, 76), (26, 76), (26, 60), (44, 60), (44, 44), (62, 44), (62, 28), (80, 28), (80, 12), (94, 12),
                (94, 92)], S_)
        c.line([(14, 66), (30, 50), (48, 34), (66, 18), (82, 4)], 1.8, S_)
    return c.done()


PAGE_RECT = (round(sx(134)), round(sy(296)), round(sx(1786)), round(sy(946)))   # where the page pictures sit on the screen
ROOM_LETTERS = ["S", "M", "L", "XL", "XXL"]


def composed(page):
    """what the screen shows on this page behind the widgets: the common background with the page picture on it"""
    im = Image.open(os.path.join(OUT, "bg.png")).convert("RGB")
    im.paste(Image.open(os.path.join(OUT, "page_%d.png" % page)).convert("RGB"), PAGE_RECT[:2])
    return im


def make_type_pictures():
    """ONE picture over one box, opaque (the toolchain bakes it into the background layer, over the page's own picture), so it
    carries what the page shows there: General and Tail show their own art (Tail with the room-size letter in a button
    outline), Space shows the space's drawing and name. The engine's type_view picks: 0 General, 1..5 Tail S..XXL, 6.. Space."""
    bx0, by0, bx1, by1 = TYPE_BOX
    bw, bh = round(sx(bx1 - bx0)), round(sy(by1 - by0))
    box = (round(sx(bx0)), round(sy(by0)), round(sx(bx0)) + bw, round(sy(by0)) + bh)
    files = []
    gen = composed(0).crop(box)
    gen.save(os.path.join(OUT, "view_general.png"), optimize=True)
    files.append("skin_assets/view_general.png")
    tail = composed(2).crop(box)
    rx, ry = round(sx(ROOM_BOX[0] - bx0)), round(sy(ROOM_BOX[1] - by0))
    rw, rh = round(sx(ROOM_BOX[2] - ROOM_BOX[0])), round(sy(ROOM_BOX[3] - ROOM_BOX[1]))
    for i, letter in enumerate(ROOM_LETTERS):
        big = Image.new("RGB", (rw * SS, rh * SS), PANEL)
        d = ImageDraw.Draw(big)
        d.rounded_rectangle([3 * SS, 3 * SS, rw * SS - 3 * SS, rh * SS - 3 * SS], radius=(rh - 6) * SS / 2, fill=WHITE, outline=rgba(SLATE), width=3 * SS)
        ctext(d, (0, 0, rw * SS, rh * SS), letter, font(46 * SS, 800), rgba(SLATE))
        im = tail.copy()
        im.paste(down(big, rw, rh), (rx, ry))
        fn = "view_room_%d.png" % i
        im.save(os.path.join(OUT, fn), optimize=True)
        files.append("skin_assets/" + fn)
    ix = round(sx(TYPE_ICON_MOCK[0] - bx0))
    iy = round(sy(TYPE_ICON_MOCK[1] - by0))
    namey = sy(TYPE_NAME_Y - by0)
    maxw = round(sx(TYPE_ARROWS[1][0] - TYPE_ARROWS[0][2])) - 12
    for t in factory.TYPES:
        im = composed(1).crop(box)
        im.paste(icon(t), (ix, iy))
        d = ImageDraw.Draw(im)
        px = 52
        f = font(px)
        while f.getlength(t) > maxw and px > 24:
            px -= 1
            f = font(px)
        cap = f.getbbox("H", anchor="ls")
        d.text((bw / 2, namey + (cap[3] - cap[1]) / 2), t, font=f, fill=SLATE, anchor="ms")
        fn = "type_%s.png" % t.lower()
        im.save(os.path.join(OUT, fn), optimize=True)
        files.append("skin_assets/" + fn)
    return (box[0], box[1], bw, bh), files


def room_button_box():
    return (round(sx((ROOM_BOX[0] + ROOM_BOX[2]) / 2)), round(sy((ROOM_BOX[1] + ROOM_BOX[3]) / 2)),
            round(sx(ROOM_BOX[2] - ROOM_BOX[0])), round(sy(ROOM_BOX[3] - ROOM_BOX[1])))


# ---- arrows -----------------------------------------------------------------------------------------------------------------
def make_arrows():
    res = {}
    specs = {"sel": (SEL_ARROWS, WHITE, GREY), "type": (TYPE_ARROWS, SLATE, (120, 140, 152))}
    for name, (boxes, col, on) in specs.items():
        for side, b in zip(("prev", "next"), boxes):
            w, h = round(sx(b[2] - b[0])), round(sy(b[3] - b[1]))
            for suffix, c in (("", col), ("_on", on)):
                im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
                d = ImageDraw.Draw(im)
                if side == "prev":
                    d.polygon([(w * SS, 0), (w * SS, h * SS), (0, h * SS / 2)], fill=rgba(c))
                else:
                    d.polygon([(0, 0), (0, h * SS), (w * SS, h * SS / 2)], fill=rgba(c))
                down(im, w, h).save(os.path.join(OUT, "arrow_%s_%s%s.png" % (name, side, suffix)))
            res[(name, side)] = (round(sx((b[0] + b[2]) / 2)), round(sy((b[1] + b[3]) / 2)), w, h)
    return res


# ---- preset selector + browser -------------------------------------------------------------------------------------------------
PRESET_NAMES = [d["name"] if d["cat"] == "Init" else d["name"] for d in factory.PRESETS]
SEL = (round(sx(SEL_ARROWS[0][2] + 6)), 90 - round(sy(SEL_RAISE)), round(sx(SEL_ARROWS[1][0] - 6)) - round(sx(SEL_ARROWS[0][2] + 6)), 30)
PANEL_BOX = (66, 160, round(sx(1808)) - 66, 402)     # screen x, y, w, h of the browser: as wide as the page panel
SIDE_W, PILL_H, PILL_GAP = 168, 31, 3
GRID_X, GRID_Y = SIDE_W + 32, 64
CELL_H, CELL_GAP = 72, 10
CELL_W = (PANEL_BOX[2] - GRID_X - 15 - 2 * CELL_GAP) // 3     # was 250 in a 985 px panel
BDX = PANEL_BOX[2] - 985                                      # the page arrows keep their place from the right edge
BPAGE_PREV = (806 + BDX, 8, 44, 38)       # inside the panel
BPAGE_NEXT = (925 + BDX, 8, 44, 38)
BPAGE_TEXT = (856 + BDX, 8, 63, 38)


def fit(text, maxw, px, weight=700):
    f = font(px * SS, weight)
    while f.getlength(text) > maxw * SS and px > 11:
        px -= 1
        f = font(px * SS, weight)
    return f


def ctext(d, box, text, f, fill):
    x0, y0, x1, y1 = box
    cap = f.getbbox("H", anchor="ls")
    d.text(((x0 + x1) / 2, (y0 + y1) / 2 + (cap[3] - cap[1]) / 2), text, font=f, fill=fill, anchor="ms")


def make_selector():
    x, y, w, h = SEL
    files = []
    for i, nm in enumerate(PRESET_NAMES):
        im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        ctext(d, (0, 0, w * SS, h * SS), nm, fit(nm, w - 8, 25), rgba(WHITE))
        fn = "preset_name_%d.png" % i
        down(im, w, h).save(os.path.join(OUT, fn), optimize=True)
        files.append("skin_assets/" + fn)
    return files


def cell_pos(slot):
    r, c = divmod(slot, 3)
    return GRID_X + c * (CELL_W + CELL_GAP), GRID_Y + r * (CELL_H + 8)


def make_browser():
    pw, ph = PANEL_BOX[2], PANEL_BOX[3]
    views = make_params.views()
    cats = factory.CAT_NAMES
    members = {0: list(range(len(factory.PRESETS)))}
    for c in range(1, len(cats)):
        members[c] = [i for i, d in enumerate(factory.PRESETS) if d["cat"] == cats[c]]
    pages = {c: max(1, (len(members[c]) + 11) // 12) for c in members}
    files = []
    for v, (c, pg) in enumerate(views):
        im = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        b = 5 * SS
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=28 * SS, fill=rgba(PANEL),
                            outline=rgba(SLATE), width=b)
        for i, nm in enumerate(cats):          # category column
            y0 = 14 + i * (PILL_H + PILL_GAP)
            on = i == c
            box = [16 * SS, y0 * SS, (16 + SIDE_W) * SS, (y0 + PILL_H) * SS]
            d.rounded_rectangle(box, radius=PILL_H * SS / 2, fill=rgba(YELLOW if on else WHITE), outline=rgba(SLATE), width=2 * SS)
            ctext(d, box, nm, fit(nm, SIDE_W - 20, 20, 800 if on else 650), rgba(SLATE))
        f = font(27 * SS, 800)                 # header: category, page n / m, arrows
        d.text((GRID_X * SS, 30 * SS), cats[c], font=f, fill=rgba(SLATE), anchor="ls")
        for (ax, ay, aw, ah), left in ((BPAGE_PREV, True), (BPAGE_NEXT, False)):
            if pages[c] > 1:
                pts = [((ax + aw) * SS, ay * SS), ((ax + aw) * SS, (ay + ah) * SS), (ax * SS, (ay + ah / 2) * SS)] if left else \
                      [(ax * SS, ay * SS), (ax * SS, (ay + ah) * SS), ((ax + aw) * SS, (ay + ah / 2) * SS)]
                d.polygon([(px_, py_) for px_, py_ in pts], fill=rgba(SLATE))
        tx0, ty0, tw, th = BPAGE_TEXT
        ctext(d, (tx0 * SS, ty0 * SS, (tx0 + tw) * SS, (ty0 + th) * SS), "%d / %d" % (pg + 1, pages[c]), font(21 * SS, 700), rgba(SLATE))
        for slot in range(12):                 # the page's presets
            k = pg * 12 + slot
            if k >= len(members[c]):
                break
            cx0, cy0 = cell_pos(slot)
            box = [cx0 * SS, cy0 * SS, (cx0 + CELL_W) * SS, (cy0 + CELL_H) * SS]
            d.rounded_rectangle(box, radius=16 * SS, fill=rgba(WHITE), outline=rgba(SLATE), width=3 * SS)
            nm = factory.PRESETS[members[c][k]]["name"]
            ctext(d, box, nm, fit(nm, CELL_W - 22, 23, 700), rgba(SLATE))
        fn = "browser_%d.png" % v
        down(im, pw, ph).save(os.path.join(OUT, fn), optimize=True)
        files.append(fn)
    # highlight of the current preset's cell: a ring and a yellow tint, over the cell
    for slot in range(12):
        im = Image.new("RGBA", (CELL_W * SS, CELL_H * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([0, 0, CELL_W * SS - 1, CELL_H * SS - 1], radius=16 * SS, fill=rgba(YELLOW, 120), outline=rgba(SLATE),
                            width=6 * SS)
        down(im, CELL_W, CELL_H).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- layout.conf -------------------------------------------------------------------------------------------------------------
def write_layout(page_art, tab_box, tog, type_box, type_files, arrows, sel_files, browser_files):
    L = ["# Generated by generate_skin.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=314B5A", "theme_ink=DDE6E5", "theme_ink_dim=FFFFFF", "theme_accent=FFE167", "",
         "[tab POLI-VERB]", "art file=skin_assets/bg.png fit=stretch", ""]
    for p, (fn, rect) in enumerate(page_art):
        L.append("art file=skin_assets/%s x=%d y=%d w=%d h=%d fit=stretch when=page:%d"
                 % (fn, rect[0], rect[1] + Y_OFF, rect[2] - rect[0], rect[3] - rect[1], p))
    rcx, rcy, rw, rh = room_button_box()
    L += ["", "# one opaque picture over the Space drawing / Tail room-size letter / General's same patch (type_view: 0 General, 1..5 Tail, 6.. Space)",
          'picture x=%d y=%d w=%d h=%d key=type_view files="%s" fit=stretch' % (type_box[0], type_box[1] + Y_OFF, type_box[2], type_box[3], ",".join(type_files)),
          "# Tail: tap the room-size letter to step S, M, L, XL, XXL (also Q-Link 10)",
          'button cx=%d cy=%d w=%d h=%d label="" key=room_step img=skin_assets/hit.png img_on=skin_assets/hit.png when=page:2' % (rcx, rcy + Y_OFF, rw + 10, rh + 10),
          "", "# tabs: the page on show is yellow (one picture per page); transparent touch areas over the pills",
          'picture x=%d y=%d w=%d h=%d key=page files="skin_assets/tabs_0.png,skin_assets/tabs_1.png,skin_assets/tabs_2.png" fit=stretch'
          % (tab_box[0], tab_box[1] + Y_OFF, tab_box[2], tab_box[3])]
    for i, (name, (x0, y0, x1, y1)) in enumerate(TABS):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=tab%d img=skin_assets/hit.png img_on=skin_assets/hit.png'
                 % (round(sx((x0 + x1) / 2)), round(sy((y0 + y1) / 2)) + Y_OFF, round(sx(x1 - x0)), round(sy(y1 - y0)) + 6, i + 1))
    L += ["", "# knobs: value inside (the engine's text), labels are in the page pictures"]
    for key, (pg, cx, cy, r, vp) in KNOBS.items():
        dy = round(0.04 * r)
        vy = dy - max(26, round(vp * 1.2)) // 2 - r - 2
        L.append('knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF when=page:%d'
                 % (round(sx(cx)), round(sy(cy)) + Y_OFF, r, key, vp, vy, 2 * r + 2, pg))
    L += ["", "# toggles: outlined = off, filled with a yellow label = on"]
    for key, (pg, box, shape, label) in TOGGLES.items():
        w, h = tog[key]
        L.append('toggle cx=%d cy=%d label="" key=%s ns=0 w=%d h=%d img=skin_assets/%s_off.png img_on=skin_assets/%s_on.png when=page:%d'
                 % (round(sx((box[0] + box[2]) / 2)), round(sy((box[1] + box[3]) / 2)) + Y_OFF, key, w, h, key, key, pg))
    L += ["", "# space arrows (Space page)"]
    for side in ("prev", "next"):
        cx, cy, w, h = arrows[("type", side)]
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=type_%s img=skin_assets/arrow_type_%s.png img_on=skin_assets/arrow_type_%s_on.png when=page:1'
                 % (cx, cy + Y_OFF, w + 14, h + 10, side, side, side))
    # preset selector
    sx_, sy_, sw, sh = SEL
    L += ["", "# preset selector: name between the arrows; a tap on the name opens the browser",
          'picture x=%d y=%d w=%d h=%d key=preset files="%s" fit=stretch' % (sx_, sy_ + Y_OFF, sw, sh, ",".join(sel_files)),
          'button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/hit.png img_on=skin_assets/hit.png' % (sx_ + sw // 2, sy_ + sh // 2 + Y_OFF, sw, sh + 10)]
    for side in ("prev", "next"):
        cx, cy, w, h = arrows[("sel", side)]
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_%s img=skin_assets/arrow_sel_%s.png img_on=skin_assets/arrow_sel_%s_on.png'
                 % (cx, cy + Y_OFF, w + 20, h + 14, side, side, side))
    # browser: on top of everything
    px, py, pw, ph = PANEL_BOX
    L += ["", "# preset browser (on top of everything): one panel picture per (category, page); a tap on its background closes it"]
    for v, fn in enumerate(browser_files):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/%s img_on=skin_assets/%s when=bview:%d'
                 % (px + pw // 2, py + ph // 2 + Y_OFF, pw, ph, fn, fn, v + 1))
    L.append("# the current preset's cell")
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/browser_hl.png img_on=skin_assets/browser_hl.png when=pre_hl:%d'
                 % (px + cx0 + CELL_W // 2, py + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1, slot + 1))
    L.append("# categories, cells, page arrows (transparent; the panel picture draws them)")
    for i in range(len(factory.CAT_NAMES)):
        y0 = 14 + i * (PILL_H + PILL_GAP)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=cat%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (px + 16 + SIDE_W // 2, py + y0 + PILL_H // 2 + Y_OFF, SIDE_W, PILL_H + PILL_GAP, i + 1))
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (px + cx0 + CELL_W // 2, py + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1))
    for key, (ax, ay, aw, ah) in (("bpage_prev", BPAGE_PREV), ("bpage_next", BPAGE_NEXT)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (px + ax + aw // 2, py + ay + ah // 2 + Y_OFF, aw + 20, ah + 14, key))
    L += ["", "# Q-Links follow the page on screen: q1..q9 are the knobs of that page (4 per Q-Link page); names/values from the engine",
          'qlinks "POLI-VERB" = q1,q2,q3,q4,q5,q6,q7,q8,q9,q10,-,-,-,-,-,-', ""]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    factory.main()
    make_params.main()
    make_knobs()
    tog = make_toggles()
    tab_box = make_tabs()
    page_art = make_backgrounds()
    type_box, type_files = make_type_pictures()
    arrows = make_arrows()
    sel_files = make_selector()
    browser_files = make_browser()
    write_layout(page_art, tab_box, tog, type_box, type_files, arrows, sel_files, browser_files)
    print("knob radii %s; %d type pictures; %d browser views; %d presets" % (sorted({v[3] for v in KNOBS.values()}), len(type_files) - 1,
                                                                            len(browser_files), len(sel_files)))
    print("wrote vst/skin_assets/*.png and vst/layout.conf")
