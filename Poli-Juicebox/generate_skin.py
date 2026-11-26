#!/usr/bin/env python3
"""Poli-Juicebox skin: turns the mockup (imgs/GUI.png) into the MPC skin's images AND writes vst/layout.conf, so every coordinate
lives in this one file.

    python3 generate_skin.py            (from the Poli-Juicebox folder; needs Pillow, numpy)

The mockup is 1374x773 and only has the controls drawn in. The design is re-laid out for the whole 1280x628 plugin area, not
stretched: the two lavender corner blobs come from the mockup (colour mask, stretched: they are shapeless), the title, the wrench and
the three face icons are lifted off it as sprites at their natural aspect, and everything else (bar, panels, headings, labels, the
wavy band, the link switch, preset pill, browser) is drawn at screen size in the mockup's colours and Open Sans.
Controls are live widgets: knobs with the value BELOW each one, the P&F Link toggle, the preset selector and the browser.

Geometry (MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628, layout.conf y = screen y + 86.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "shared"))
sys.path.insert(0, os.path.join(HERE, "presets"))
sys.path.insert(0, HERE)
import poli_preset_ui as ppu  # noqa: E402  (the Open Sans font shared by the Poli plugins)
import factory  # noqa: E402
import make_params  # noqa: E402
import poli_native as pn  # noqa: E402

IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")
MOCKUP = "GUI.png"

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1374, 773
S = 0.8125                                   # mockup px -> screen px for sprites (natural aspect)
SS = 4

PURPLE = (69, 40, 126)
LAV = (244, 234, 255)
BLACK = (0, 0, 5)
WHITE = (255, 255, 255)
GREY = (190, 170, 235)                       # pressed arrow
INK = BLACK
FONT = ppu.OPEN_SANS


def font(px, weight=700):
    f = ImageFont.truetype(FONT, int(round(px)))
    try:
        axes = f.get_variation_axes()
        f.set_variation_by_axes([weight] + [a["default"] for a in axes[1:]])
    except (OSError, AttributeError):
        pass
    return f


def down(im, w, h):
    return im.resize((w, h), Image.Resampling.LANCZOS)


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


# ---- layout (screen px) -----------------------------------------------------------------------------------------------------
MARGIN = 64
BAR = (MARGIN, 84, W - MARGIN, 98)                   # the white rule under the title
TITLE_X, TITLE_CY = MARGIN, 56
STRAW = (352, 0)                                      # top-left of the straw sprite
PILL = (562, 36, 276, 46)                            # preset selector: x, y, w, h (white arrows + name on the purple page)
PILL_ARROW = (26, 32)
PILL_ARROW_BOX = (54, 56)
NAME_PAD = 48
LEARN = (848, 80)                                     # the Learn button: x, w (y = the tabs', height TAB_H); its progress bar sits under it
LEARN_BAR_H, PROG_FRAMES = 4, 21
HELP_BTN = (538, 59, 34)                              # the ? button after the title and straw: cx, cy (screen), diameter
TABS = [("Shape", 936, 62), ("Tone", 1004, 54), ("Filter", 1064, 58), ("Tracking", 1128, 80)]   # right-hand page tabs: label, x, w (height TAB_H)
TAB_Y, TAB_H = 42, 34
NAME_BOX = (PILL[0] + NAME_PAD, PILL[1] + 3, PILL[2] - 2 * NAME_PAD, PILL[3] - 6)

PANEL_Y0, PANEL_Y1 = 108, 610
LEFT = (64, PANEL_Y0, 600, PANEL_Y1)                 # panels: x0, y0, x1, y1
RIGHT = (616, PANEL_Y0, 1216, PANEL_Y1)
BORDER, PANEL_R = 6, 10

# sections: title, x0, x1 (the underline), title baseline
TITLE_PX = 31
HEAD = {   # title, x0, x1 (the underline), baseline, px, page (None = the left panel, always there)
    "smooth": ("Smoothen", 96, 570, 362, 40, None),
    "shift": ("Shift", 632, 1200, 146, TITLE_PX, 0),
    "tamer": ("Spectral Tamer", 632, 1200, 146, TITLE_PX, 1),
    "harm": ("Harmonic Shape", 632, 1200, 372, TITLE_PX, 1),
    "nff": ("Natural Formant Filter", 632, 1200, 146, TITLE_PX, 2),
    "track": ("Pitch Tracker", 632, 1200, 146, TITLE_PX, 3),
    "rasp": ("Rasp & Transitions", 632, 1200, 372, TITLE_PX, 3),
}
HEAD_Q = {"smooth": [1], "shift": [2], "tamer": [2], "harm": [3], "nff": [2, 3], "track": [2], "rasp": [3]}   # the Q-Link page(s) a section's knobs are on
PAGE_NAMES = make_params.PAGES
LABEL_PX = 20
KNOBS = {   # key: (cx, cy, r, label, label baseline, value px)
    "fsmooth": (136, 496, 56, "F-Smooth", 420, 26), "air": (267, 496, 56, "Air", 420, 26),
    "natural": (398, 496, 56, "Natural", 420, 26), "mix": (529, 496, 56, "Mix", 420, 26),
    # Shape: the three big knobs
    "pitch": (716, 306, 72, "Pitch", 214, 28), "formant": (916, 306, 72, "Formant", 214, 28), "ftrack": (1116, 306, 72, "F-Track", 214, 28),
    # Tone: Spectral Tamer, Harmonic Shape
    "thresh": (712, 250, 52, "Threshold", 188, 22), "lowend": (848, 250, 52, "Low end", 188, 22),
    "attack": (984, 250, 52, "Attack", 188, 22), "release": (1120, 250, 52, "Release", 188, 22),
    "odd": (780, 480, 52, "Odd drive", 414, 22), "even": (916, 480, 52, "Even drive", 414, 22), "color": (1052, 480, 52, "Color", 414, 22),
    # Filter: F1 / F2 / F3 columns, Freq / Q rows
    "f1c": (800, 292, 52, "", 0, 22), "f2c": (940, 292, 52, "", 0, 22), "f3c": (1080, 292, 52, "", 0, 22),
    "f1q": (800, 484, 52, "", 0, 22), "f2q": (940, 484, 52, "", 0, 22), "f3q": (1080, 484, 52, "", 0, 22),
    # Tracking
    "floor": (712, 250, 52, "Floor", 188, 22), "ceil": (848, 250, 52, "Ceiling", 188, 22), "glide": (984, 250, 52, "Glide", 188, 22),
    "guard": (1120, 250, 52, "Oct guard", 188, 22),
    "rasp": (712, 480, 52, "Rasp", 414, 22), "hold": (848, 480, 52, "Hold", 414, 22), "morph": (984, 480, 52, "Morph", 414, 22),
    "overlap": (1120, 480, 52, "Overlap", 414, 22),
}
PAGE_KEYS = {0: {"pitch", "formant", "ftrack"}, 1: {"thresh", "lowend", "attack", "release", "odd", "even", "color"},
             2: {"f1c", "f2c", "f3c", "f1q", "f2q", "f3q"}, 3: {"floor", "ceil", "glide", "guard", "rasp", "hold", "morph", "overlap"}}
LEFT_KEYS = {"fsmooth", "air", "natural", "mix"}


def page_of(key):
    """0 Shape, 1 Tone, 2 Filter, 3 Tracking, None = the left panel (every page)"""
    for pg, ks in PAGE_KEYS.items():
        if key in ks:
            return pg
    return None


# ---- Q-Link indicator: a numbered dot on every knob that has a Q-Link (the number = which of the four Q-Links, the colour = which Q-Link page);
# every section's heading carries a pill "Q-Link N". The mapping is make_params.QLEFT / QPAGES (the engine reads the same table).
QCOL = {1: PURPLE, 2: (200, 72, 32), 3: (0, 118, 118), 4: (70, 120, 30)}
CHIP_R = 13


def qchip_of(key):
    """(Q-Link page 1..4, Q-Link number 1..4) of a knob / the Link switch, or None"""
    if key in make_params.QLEFT:
        return 1, make_params.QLEFT.index(key) + 1
    for pg in range(len(PAGE_NAMES)):
        for i, k in enumerate(make_params.QPAGES[pg]):
            if k == key:
                return i // 4 + 2, i % 4 + 1
    return None


def draw_chip(d, cx, cy, n, col, ox=0, oy=0):
    """the numbered dot; (ox, oy) = the origin of the canvas, in screen px (the canvas is SS x)"""
    r = CHIP_R
    x, y = (cx - ox) * SS, (cy - oy) * SS
    d.ellipse([x - r * SS, y - r * SS, x + r * SS, y + r * SS], fill=rgba(col), outline=rgba(WHITE), width=2 * SS)
    ctext_box(d, [x - r * SS, y - r * SS, x + r * SS, y + r * SS], str(n), font(17 * SS, 800), rgba(WHITE))


def chip_pos(key):
    cx, cy, r = KNOBS[key][:3]
    return cx + 0.93 * r, cy - 0.93 * r - 1


def draw_legend(d, x1, base, pages, ox=0, oy=0):
    """pills "Q-Link N", right-aligned to x1, sitting on the heading's baseline"""
    f = font(15 * SS, 800)
    x = x1
    for pg in reversed(pages):
        text = "Q-Link %d" % pg
        w = int(f.getlength(text) / SS) + 18
        box = [(x - w - ox) * SS, (base - 22 - oy) * SS, (x - ox) * SS, (base + 1 - oy) * SS]
        d.rounded_rectangle(box, radius=11 * SS, fill=rgba(QCOL[pg]))
        ctext_box(d, box, text, f, rgba(WHITE))
        x -= w + 6
VAL_GAP = 20                                         # value text centre below the knob's lower edge
NFF_COLS = ((800, "F1"), (940, "F2"), (1080, "F3"))
NFF_HEAD_BASE = 224
NFF_ROWS = ((292, "Freq"), (484, "Q"))
NFF_ROW_LABEL_X = 632

LINK = (916, 484, 158, 40)                           # P&F Link switch: cx, cy, w, h (a pill, left = Off)
LINK_LABEL_BASE = 456
LINK_HINT = "Link on: the formant follows the pitch by F-Track"

# knob look: black disc, a white pointer line from near the rim to 0.4 r
POINTER_OUT, POINTER_IN = 0.96, 0.40

WORST = {"pitch": ["+12 st", "-12 st", "+9.6 st"], "formant": ["+12 st", "-12 st", "+9.6 st"], "ftrack": ["100 %"],
         "thresh": ["-60 dB", "-34 dB"], "lowend": ["350 Hz", "Off"], "attack": ["20 ms", "0.5 ms"], "release": ["200 ms", "100 ms"],
         "odd": ["100 %"], "even": ["100 %"], "color": ["4.0 kHz", "2.5 kHz"], "f1c": ["1.0 kHz", "850 Hz"], "f2c": ["3.0 kHz", "1.7 kHz"],
         "f3c": ["4.5 kHz", "2.5 kHz"], "f1q": ["14.0", "Off"], "f2q": ["14.0", "Off"], "f3q": ["14.0", "Off"],
         "fsmooth": ["100 %"], "air": ["100 %"], "natural": ["100 %"], "mix": ["100 %"],
         "floor": ["200 Hz", "70 Hz"], "ceil": ["920 Hz", "250 Hz"], "glide": ["100 %"], "guard": ["100 %"], "rasp": ["100 %"],
         "hold": ["150 ms", "60 ms"], "morph": ["200 ms", "5.0 ms"], "overlap": ["100 %"]}


# ---- knob images -----------------------------------------------------------------------------------------------------------
def knob_image(angle, r, size):
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS / 2
    rr = r * SS
    d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=rgba(BLACK))
    a = math.radians(angle)
    ux, uy = math.sin(a), -math.cos(a)
    px, py = -uy, ux
    hw = max(0.085 * rr, 5.0 * SS) / 2
    r_out, r_in = rr * POINTER_OUT, rr * POINTER_IN
    d.polygon([(c + ux * r_out + px * hw, c + uy * r_out + py * hw), (c + ux * r_out - px * hw, c + uy * r_out - py * hw),
               (c + ux * r_in - px * hw, c + uy * r_in - py * hw), (c + ux * r_in + px * hw, c + uy * r_in + py * hw)],
              fill=rgba(WHITE))
    for e in (r_out, r_in):    # rounded ends
        d.ellipse([c + ux * e - hw, c + uy * e - hw, c + ux * e + hw, c + uy * e + hw], fill=rgba(WHITE))
    return down(big, size, size)


def strip_frames(r):
    """odd frame count whose strip stays under MPC's 12288 px (frame = 2r+10)"""
    n = 12288 // (2 * r + 10)
    return n if n % 2 else n - 1


def radii():
    return sorted({v[2] for v in KNOBS.values()})


def make_knobs():
    for r in radii():
        box = 2 * r + 8
        n = strip_frames(r)
        strip = Image.new("RGBA", (box, box * n), (0, 0, 0, 0))
        for i in range(n):
            strip.paste(knob_image(-135.0 + 270.0 * i / (n - 1), r, box), (0, i * box))
        strip.save(os.path.join(OUT, "knob_strip_%d.png" % box), optimize=True)


# ---- text helpers --------------------------------------------------------------------------------------------------------------
def fit(text, maxw, px, weight=700, floor=11):
    f = font(px * SS, weight)
    while f.getlength(text) > maxw * SS and px > floor:
        px -= 1
        f = font(px * SS, weight)
    return f


def fit1(text, maxw, px, weight=700, floor=11):
    """like fit(), but a font at 1x (for text drawn straight on a screen-size canvas)"""
    f = font(px, weight)
    while f.getlength(text) > maxw and px > floor:
        px -= 1
        f = font(px, weight)
    return f


def ctext_box(d, box, text, f, fill):
    x0, y0, x1, y1 = box
    cap = f.getbbox("H", anchor="ls")
    d.text(((x0 + x1) / 2, (y0 + y1) / 2 + (cap[3] - cap[1]) / 2), text, font=f, fill=fill, anchor="ms")


# ---- background -----------------------------------------------------------------------------------------------------------
def load(name):
    im = Image.open(os.path.join(IMGS, name)).convert("RGB")
    assert im.size == (MOCK_W, MOCK_H), "%s must be %dx%d" % (name, MOCK_W, MOCK_H)
    return im


def blob_mask(mock):
    """the lavender corner blobs of the mockup as a boolean mask (mockup pixels)"""
    a = np.asarray(mock).astype(int)
    m = np.abs(a - np.array(LAV)).sum(axis=2) < 20
    keep = np.zeros(m.shape, bool)
    keep[0:260, 1000:MOCK_W] = True
    keep[620:MOCK_H, 0:360] = True
    m &= keep
    im = Image.fromarray((m * 255).astype(np.uint8))
    im = im.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(5))
    im = im.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    return (np.asarray(im) > 127) & keep


def wave_y(x):
    """centre line of the black wavy band across the left panel (screen px)"""
    t = (x - LEFT[0]) / float(LEFT[2] - LEFT[0])
    return 292 - 22 * t + 7 * math.sin(2 * math.pi * t)


def draw_wave(canvas):
    pts_top, pts_bot = [], []
    th = 15
    for i in range(0, 201):
        x = LEFT[0] + (LEFT[2] - LEFT[0]) * i / 200.0
        y = wave_y(x)
        pts_top.append((x * SS, (y - th) * SS))
        pts_bot.append((x * SS, (y + th) * SS))
    big = Image.new("L", (W * SS, 330 * SS), 0)
    ImageDraw.Draw(big).polygon(pts_top + pts_bot[::-1], fill=255)
    m = big.resize((W, 330), Image.Resampling.LANCZOS)
    canvas.paste(Image.new("RGB", (W, 330), BLACK), (0, 0), m)


def lift_dark(mock, box):
    """black-on-light line art inside `box` (mockup px) as an RGBA sprite (black, alpha = darkness)"""
    x0, y0, x1, y1 = box
    a = np.asarray(mock.crop(box)).astype(float)
    lum = a.mean(axis=2)
    alpha = np.clip((200.0 - lum) / 170.0, 0, 1)
    out = np.zeros(alpha.shape + (4,), np.uint8)
    out[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def lift_on_purple(mock, box):
    """black art on the purple page (the wrench): alpha from the blue channel (black 5 .. purple 126)"""
    a = np.asarray(mock.crop(box)).astype(float)
    alpha = np.clip(1.0 - (a[..., 2] - 5.0) / 121.0, 0, 1)
    out = np.zeros(alpha.shape + (4,), np.uint8)
    out[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def paste_scaled(canvas, spr, x, y, scale):
    w, h = max(1, round(spr.width * scale)), max(1, round(spr.height * scale))
    s2 = spr.resize((w, h), Image.Resampling.LANCZOS)
    canvas.paste(s2, (int(round(x)), int(round(y))), s2)
    return w, h


def draw_straw(bg):
    """a bendy drinking straw (as if it stuck out of a juice box) beside the title: striped, black outline, a corrugated bend and a slanted tip"""
    W2, H2 = 200, 90
    big = Image.new("RGBA", (W2 * SS, H2 * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    p0, p1, p2, p3 = (14, 98), (60, 34), (76, 20), (160, 20)      # up the slope, the bend, then the horizontal beak
    wout, win = 24, 14

    def line(pts, w, col):
        d.line([(x * SS, y * SS) for x, y in pts], fill=col, width=int(w * SS), joint="curve")
        for x, y in (pts[0], pts[-1]):
            d.ellipse([(x - w / 2) * SS, (y - w / 2) * SS, (x + w / 2) * SS, (y + w / 2) * SS], fill=col)
    pe = (104, 20)                                                # where the straight beak starts: its end is drawn below as a clean slanted cut
    line([p0, p1, p2, pe], wout, rgba(BLACK))
    line([p0, p1, p2, pe], win, rgba(WHITE))
    d.polygon([(pe[0] * SS, 8 * SS), (150 * SS, 8 * SS), (160 * SS, 32 * SS), (pe[0] * SS, 32 * SS)], fill=rgba(BLACK))   # the beak, outline
    d.polygon([(pe[0] * SS, 13 * SS), (146.4 * SS, 13 * SS), (154.6 * SS, 27 * SS), (pe[0] * SS, 27 * SS)], fill=rgba(WHITE))   # ... and inside
    # stripes on the long straight part (purple, slanted) and on the beak
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux
    k = 6.0
    while k < L - 6:
        cx, cy = p0[0] + ux * k, p0[1] + uy * k
        d.line([((cx - nx * 6.4 - ux * 3) * SS, (cy - ny * 6.4 - uy * 3) * SS), ((cx + nx * 6.4 + ux * 3) * SS, (cy + ny * 6.4 + uy * 3) * SS)], fill=rgba(PURPLE), width=int(4 * SS))
        k += 17
    for x in range(94, 140, 17):
        d.line([((x - 3) * SS, 26.6 * SS), ((x + 3) * SS, 13.4 * SS)], fill=rgba(PURPLE), width=int(4 * SS))
    # the accordion bend: short black rings across the tube
    for t in (0.15, 0.4, 0.65):
        bx, by = p1[0] + (p2[0] - p1[0]) * t, p1[1] + (p2[1] - p1[1]) * t
        d.line([((bx - 4) * SS, (by + 7) * SS), ((bx + 4) * SS, (by - 7) * SS)], fill=rgba(BLACK), width=int(2.4 * SS))
    im = down(big, W2, H2)
    bg.paste(im, (STRAW[0], STRAW[1]), im)


def make_background():
    mock = load(MOCKUP)
    mask = blob_mask(mock)
    m = Image.fromarray((mask * 255).astype(np.uint8)).resize((W, H), Image.Resampling.BICUBIC)
    bg = Image.new("RGB", (W, H), PURPLE)
    bg.paste(Image.new("RGB", (W, H), LAV), (0, 0), m)
    # the wrench (lifted, drawn before the bar so the bar covers its lower end)
    draw_straw(bg)
    # the title: lifted off the mockup at its natural aspect
    base = Image.new("RGB", (MOCK_W, MOCK_H), PURPLE)
    items = pn.lift(mock, base, [(78, 66, 432, 130)])
    ys = [it["box"] for it in items]
    cy_mock = (min(b[1] for b in ys) + max(b[3] for b in ys)) / 2.0
    x_mock = min(b[0] for b in ys)
    pn.place(bg, items, lambda x: TITLE_X + (x - x_mock) * S, S, anchor=lambda it: "l", dy=TITLE_CY - cy_mock * S)
    d = ImageDraw.Draw(bg)
    d.rectangle([BAR[0], BAR[1], BAR[2] - 1, BAR[3] - 1], fill=WHITE)
    # panels: white with a thick black border
    for box in (LEFT, RIGHT):
        pn.panel_fill(bg, box, PANEL_R, BLACK)
        pn.panel_fill(bg, (box[0] + BORDER, box[1] + BORDER, box[2] - BORDER, box[3] - BORDER), max(2, PANEL_R - 4), WHITE)
    # left panel: faces on the wave
    wpath = os.path.join(OUT, "_wave.png")
    del wpath
    faces = [((100, 236, 218, 372), 0.15), ((278, 236, 416, 372), 0.51), ((458, 236, 590, 372), 0.81)]
    fscale = 0.95
    for box, frac in faces:
        spr = lift_dark(mock, box)
        cx = LEFT[0] + (LEFT[2] - LEFT[0]) * frac
        w, h = round(spr.width * fscale), round(spr.height * fscale)
        top = wave_y(cx) - 12 - h
        paste_scaled(bg, spr, cx - w / 2, top, fscale)
    draw_wave(bg)
    d = ImageDraw.Draw(bg)
    # the left panel's heading and labels (the right-hand pages are overlays: make_pages)
    for key, (text, x0, x1, base, px, pg) in HEAD.items():
        if pg is None:
            d.text((x0, base), text, font=fit1(text, x1 - x0, px, 700), fill=INK, anchor="ls")
            d.rectangle([x0, base + 6, x1 - 1, base + 11], fill=BLACK)
    for key, (cx, cy, r, text, base_y, vp) in KNOBS.items():
        if text and page_of(key) is None:
            d.text((cx, base_y), text, font=font(24, 700), fill=INK, anchor="ms")
    # Q-Link indicator of the left panel (always Q-Link 1): the pill on the heading, a numbered dot on every knob (drawn at SS and scaled: on the page canvas)
    big = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big)
    draw_legend(bd, HEAD["smooth"][2], HEAD["smooth"][3] - 8, HEAD_Q["smooth"])
    for key in LEFT_KEYS:
        q = qchip_of(key)
        draw_chip(bd, *chip_pos(key), q[1], QCOL[q[0]])
    ov = down(big, W, H)
    bg.paste(ov, (0, 0), ov)
    bg.save(os.path.join(OUT, "bg.png"), optimize=True)


# ---- right-hand pages: transparent overlays with each page's headings, labels and rules --------------------------------------------------
PAGE_BOX = (RIGHT[0], RIGHT[1], RIGHT[2] - RIGHT[0], RIGHT[3] - RIGHT[1])


def make_pages():
    bx, by, bw, bh = PAGE_BOX
    for pg, name in enumerate(PAGE_NAMES):
        im = Image.new("RGBA", (bw * SS, bh * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)

        def T(x, y, text, f, anchor="ls", col=INK):
            d.text(((x - bx) * SS, (y - by) * SS), text, font=f, fill=rgba(col), anchor=anchor)
        for key, (text, x0, x1, base, px, page) in HEAD.items():
            if page == pg:
                T(x0, base, text, fit(text, x1 - x0, px, 700))
                d.rectangle([(x0 - bx) * SS, (base + 6 - by) * SS, (x1 - bx) * SS - 1, (base + 12 - by) * SS - 1], fill=rgba(BLACK))
        for key, (cx, cy, r, text, base_y, vp) in KNOBS.items():
            if text and page_of(key) == pg:
                T(cx, base_y, text, fit(text, min(2 * r + 20, 100 if r >= 50 else 84), LABEL_PX, 700), "ms")
        if pg == 0:
            T(LINK[0], LINK_LABEL_BASE, "P&F Link", font(22 * SS, 800), "ms")
            T(LINK[0], LINK[1] + LINK[3] // 2 + 30, LINK_HINT, font(17 * SS, 600), "ms", (70, 60, 100))
        if pg == 2:
            for cx, text in NFF_COLS:
                T(cx, NFF_HEAD_BASE, text, font(26 * SS, 800), "ms")
            for cy, text in NFF_ROWS:
                T(NFF_ROW_LABEL_X + 4, cy + 7, text, font(22 * SS, 800))
        # Q-Link indicator: the pills on the headings, the numbered dots on the knobs
        for key, (text, x0, x1, base, px, page) in HEAD.items():
            if page == pg:
                draw_legend(d, x1, base, HEAD_Q[key], bx, by)
        for key in KNOBS:
            if page_of(key) == pg and qchip_of(key):
                q = qchip_of(key)
                draw_chip(d, *chip_pos(key), q[1], QCOL[q[0]], bx, by)
        if pg == 0:
            q = qchip_of("link")
            lx = LINK[0] + LINK[2] / 2 - 2
            draw_chip(d, lx, LINK[1] - LINK[3] / 2 - 8, q[1], QCOL[q[0]], bx, by)
        down(im, bw, bh).save(os.path.join(OUT, "page_%d.png" % pg), optimize=True)


def make_tabs():
    for i, (label, x, w) in enumerate(TABS):
        for on in (False, True):
            im = Image.new("RGBA", (w * SS, TAB_H * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            d.rounded_rectangle([0, 0, w * SS - 1, TAB_H * SS - 1], radius=TAB_H * SS / 2, fill=rgba(BLACK if on else WHITE), outline=rgba(BLACK), width=3 * SS)
            ctext_box(d, [0, 0, w * SS, TAB_H * SS], label, font(17 * SS, 800), rgba(WHITE if on else BLACK))
            down(im, w, TAB_H).save(os.path.join(OUT, "tab%d_%s.png" % (i + 1, "on" if on else "off")))


# ---- the link switch --------------------------------------------------------------------------------------------------------------
def make_link():
    _, _, w, h = LINK
    for on in (False, True):
        im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=h * SS / 2, fill=rgba(PURPLE if on else BLACK))
        kr = (h - 6) / 2.0
        kx = (w - 3 - kr) if on else (3 + kr)
        d.ellipse([(kx - kr) * SS, (h / 2 - kr) * SS, (kx + kr) * SS, (h / 2 + kr) * SS], fill=rgba(WHITE))
        txt, tx = ("VAR", 3 + kr * 2 + (w - 6 - kr * 2) / 2 - 3) if False else (None, 0)
        f = font(13 * SS, 800)
        if on:
            d.text(((3 + (w - 6 - 2 * kr)) / 2 * SS + 2 * SS, h / 2 * SS + 4.6 * SS), "Variable", font=font(11 * SS, 800), fill=rgba(WHITE), anchor="ms")
        else:
            d.text(((w + 2 * kr) / 2 * SS + 2 * SS, h / 2 * SS + 4.6 * SS), "Off", font=f, fill=rgba(WHITE), anchor="ms")
        down(im, w, h).save(os.path.join(OUT, "link_%s.png" % ("on" if on else "off")))


# ---- arrows ----------------------------------------------------------------------------------------------------------------
def arrow_image(side, tri, box, colour):
    """The picture is the whole touch box (the triangle plus a transparent margin): MPC stretches a button's image to its box."""
    bw, bh = box
    im = Image.new("RGBA", (bw * SS, bh * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    tw, th = tri
    x0, y0, x1, y1 = (bw - tw) / 2 * SS, (bh - th) / 2 * SS, (bw + tw) / 2 * SS, (bh + th) / 2 * SS
    if side == "prev":
        d.polygon([(x1, y0), (x1, y1), (x0, (y0 + y1) / 2)], fill=rgba(colour))
    else:
        d.polygon([(x0, y0), (x0, y1), (x1, (y0 + y1) / 2)], fill=rgba(colour))
    return down(im, bw, bh)


def make_arrows():
    for side in ("prev", "next"):
        arrow_image(side, PILL_ARROW, PILL_ARROW_BOX, WHITE).save(os.path.join(OUT, "arrow_sel_%s.png" % side))
        arrow_image(side, PILL_ARROW, PILL_ARROW_BOX, GREY).save(os.path.join(OUT, "arrow_sel_%s_on.png" % side))
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))


# ---- live preset name: a filmstrip (opaque, drawn by a meter widget) -------------------------------------------------------------
PRESET_NAMES = [factory.menu_name(d) for d in factory.PRESETS]


def make_preset_strip():
    x, y, w, h = NAME_BOX
    strip = Image.new("RGB", (w, h * len(PRESET_NAMES)), PURPLE)
    for i, nm in enumerate(PRESET_NAMES):
        im = Image.new("RGB", (w * SS, h * SS), PURPLE)
        d = ImageDraw.Draw(im)
        ctext_box(d, (0, 0, w * SS, h * SS), nm, fit(nm, w - 8, 29, 700, 16), WHITE)
        strip.paste(down(im, w, h), (0, i * h))
    strip.save(os.path.join(OUT, "preset_names.png"), optimize=True)


# ---- preset browser ------------------------------------------------------------------------------------------------------------
PANEL_BOX = (MARGIN, PANEL_Y0, W - 2 * MARGIN, 500)  # screen x, y, w, h: as wide as the page
SIDE_W, PILL_H, PILL_GAP = 188, 33, 5
GRID_X, GRID_Y = SIDE_W + 32, 70
CELL_H, CELL_GAP = 84, 10
CELL_W = (PANEL_BOX[2] - GRID_X - 15 - 2 * CELL_GAP) // 3
ROW_PITCH = CELL_H + 18
BDX = PANEL_BOX[2] - 985
BPAGE_PREV = (806 + BDX, 10, 44, 38)
BPAGE_NEXT = (925 + BDX, 10, 44, 38)
BPAGE_TEXT = (856 + BDX, 10, 63, 38)


def cell_pos(slot):
    r, c = divmod(slot, 3)
    return GRID_X + c * (CELL_W + CELL_GAP), GRID_Y + r * ROW_PITCH


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
        b = 6 * SS
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=24 * SS, fill=rgba(WHITE), outline=rgba(BLACK), width=b)
        for i, nm in enumerate(cats):          # category column
            y0 = 18 + i * (PILL_H + PILL_GAP)
            on = i == c
            box = [20 * SS, y0 * SS, (20 + SIDE_W) * SS, (y0 + PILL_H) * SS]
            d.rounded_rectangle(box, radius=PILL_H * SS / 2, fill=rgba(PURPLE if on else WHITE), outline=rgba(BLACK), width=2 * SS)
            ctext_box(d, box, nm, fit(nm, SIDE_W - 20, 20, 800 if on else 650), rgba(WHITE if on else BLACK))
        f = font(28 * SS, 800)                 # header: category, page n / m, arrows
        d.text((GRID_X * SS, 38 * SS), cats[c], font=f, fill=rgba(BLACK), anchor="ls")
        for (ax, ay, aw, ah), left in ((BPAGE_PREV, True), (BPAGE_NEXT, False)):
            if pages[c] > 1:
                pts = [((ax + aw) * SS, ay * SS), ((ax + aw) * SS, (ay + ah) * SS), (ax * SS, (ay + ah / 2) * SS)] if left else \
                      [(ax * SS, ay * SS), (ax * SS, (ay + ah) * SS), ((ax + aw) * SS, (ay + ah / 2) * SS)]
                d.polygon(pts, fill=rgba(BLACK))
        tx0, ty0, tw, th = BPAGE_TEXT
        ctext_box(d, (tx0 * SS, ty0 * SS, (tx0 + tw) * SS, (ty0 + th) * SS), "%d / %d" % (pg + 1, pages[c]), font(21 * SS, 700), rgba(BLACK))
        for slot in range(12):                 # the page's presets
            k = pg * 12 + slot
            if k >= len(members[c]):
                break
            cx0, cy0 = cell_pos(slot)
            box = [cx0 * SS, cy0 * SS, (cx0 + CELL_W) * SS, (cy0 + CELL_H) * SS]
            d.rounded_rectangle(box, radius=16 * SS, fill=rgba(WHITE), outline=rgba(BLACK), width=3 * SS)
            idx = members[c][k]
            nm = PRESET_NAMES[idx] if c == 0 else factory.PRESETS[idx]["name"]   # All shows the category prefix
            ctext_box(d, box, nm, fit(nm, CELL_W - 22, 24, 700), rgba(BLACK))
        fn = "browser_%d.png" % v
        down(im, pw, ph).save(os.path.join(OUT, fn), optimize=True)
        files.append(fn)
    # highlight of the current preset's cell: a purple ring and a light tint, over the cell
    im = Image.new("RGBA", (CELL_W * SS, CELL_H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, CELL_W * SS - 1, CELL_H * SS - 1], radius=16 * SS, fill=rgba(PURPLE, 55), outline=rgba(PURPLE), width=6 * SS)
    down(im, CELL_W, CELL_H).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- latency pills (Tracking page footer) ------------------------------------------------------------------------------------------
LAT_BAR = (RIGHT[0] + 14, 574, 572, 28)               # x, y, w, h (screen)
LAT_LABEL_W = 84
LAT_PILLS = [("Fast  8.7 ms", 128), ("Balanced  11.6 ms", 170), ("Smooth  17.4 ms", 152)]
LAT_GAP = 8


def lat_pill_boxes():
    x, out = LAT_LABEL_W, []
    for _, w in LAT_PILLS:
        out.append((x, w))
        x += w + LAT_GAP
    return out


def make_learn():
    x, w = LEARN
    for on in (False, True):
        im = Image.new("RGBA", (w * SS, TAB_H * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([0, 0, w * SS - 1, TAB_H * SS - 1], radius=TAB_H * SS / 2, fill=rgba(BLACK if on else WHITE), outline=rgba(BLACK), width=3 * SS)
        ctext_box(d, [0, 0, w * SS, TAB_H * SS], "Listening" if on else "Learn", font((16 if on else 19) * SS, 800), rgba(WHITE if on else BLACK))
        down(im, w, TAB_H).save(os.path.join(OUT, "learn_%s.png" % ("on" if on else "off")))
    strip = Image.new("RGBA", (w, LEARN_BAR_H * PROG_FRAMES), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    for f in range(1, PROG_FRAMES):
        d.rectangle((0, f * LEARN_BAR_H, round(f / (PROG_FRAMES - 1) * w) - 1, f * LEARN_BAR_H + LEARN_BAR_H - 1), fill=rgba(BLACK))
    strip.save(os.path.join(OUT, "learn_prog.png"), optimize=True)


def make_latency():
    x0, y0, bw, bh = LAT_BAR
    for sel in range(len(LAT_PILLS)):
        im = Image.new("RGBA", (bw * SS, bh * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.text((4 * SS, (bh // 2) * SS), "Latency", font=font(19 * SS, 800), fill=rgba(BLACK), anchor="lm")
        for i, ((label, w), (px, _)) in enumerate(zip(LAT_PILLS, lat_pill_boxes())):
            on = i == sel
            box = [px * SS, 1.5 * SS, (px + w) * SS, (bh - 1.5) * SS]
            d.rounded_rectangle(box, radius=(bh - 3) * SS / 2, fill=rgba(BLACK if on else WHITE), outline=rgba(BLACK), width=3 * SS)
            ctext_box(d, box, label, font(15 * SS, 800), rgba(WHITE if on else BLACK))
        down(im, bw, bh).save(os.path.join(OUT, "lat_%d.png" % (sel + 1)))


# ---- help window -----------------------------------------------------------------------------------------------------------------
HELP_PAGES = [
    ("Shape & Tracking", [
        ("Pitch", "Shifts the voice up or down, +/-12 semitones. Centre = no change."),
        ("Formant", "Moves the vocal-tract colour: up = smaller, brighter, more feminine; down = bigger, darker."),
        ("F-Track", "With P&F Link on, formant also follows Pitch by this amount. 100% = formant moves as much as the pitch."),
        ("P&F Link", "Off: Pitch and Formant are independent. On: formant = Formant + F-Track x Pitch. Natural for gender shifts."),
        ("Floor / Ceiling", "The pitch range the tracker searches. Set them just around your voice to stop octave errors."),
        ("Glide", "Smooths the tracked pitch. Higher = steadier and less jumpy, lower = quicker to follow."),
        ("Oct guard", "Ignores sudden octave jumps unless they repeat. Raise it if the voice flips up or down an octave."),
        ("Rasp", "For raspy or breathy voices: keeps the tracker locked through noisy passages. Try 60-80%."),
        ("Hold", "Keeps the voice treated as voiced through short drop-outs. Raise it if you hear a robotic growl."),
        ("Morph", "Time to fade between voiced and unvoiced processing. Longer = smoother, shorter = tighter."),
        ("Overlap", "Wider, softer grain overlap. Adds smoothness on rough voices, slightly less crisp."),
        ("Raspy tip", "Rasp 70-80, Hold 100+, Glide 50+, Floor / Ceiling around the voice. Or load a Raspy Smooth preset."),
    ]),
    ("Polish", [
        ("Threshold", "Spectral Tamer: harsh highs above this level are pulled down. Lower = more taming."),
        ("Low end", "Where the Tamer's band starts. Raise it to leave warmth alone; Off = whole range."),
        ("Attack / Release", "How fast the Tamer reacts and lets go. Fast attack catches sibilance, slow release is smoother."),
        ("Odd / Even / Color", "Harmonic Shaper: odd drive adds edge and grit, even drive warmth and body. Color (200 Hz - 4 kHz) is where the added harmonics grow from."),
        ("Q-Links", "Q-Link page 1 is always Smoothen. Pages 2-4 follow the tab on show. The numbered dots on the knobs say which Q-Link each one is."),
        ("F1 / F2 / F3", "Natural Formant Filter: three vowel peaks, each with Freq and Q. Q at Off = that peak is bypassed."),
        ("F-Smooth", "Smooths formant movement but keeps plosives and attacks crisp."),
        ("Air", "Adds an 8 kHz shelf and a touch of breath for openness."),
        ("Natural", "Adds tiny voiced jitter and shimmer so the shifted voice sounds less synthetic."),
        ("Mix", "Dry / wet blend (equal-power). Pitch and Formant at centre pass the dry voice."),
        ("Learn", "Sing or speak for about 6 s: it sets Floor, Ceiling, Rasp, Hold, Glide, Guard, Morph, Overlap and Threshold to fit the voice. Pitch and Formant stay."),
        ("Latency", "Tracking tab footer: Fast 8.7 ms, Balanced 11.6 ms (default), Smooth 17.4 ms. More look-ahead = smoother deep voices."),
    ]),
]
HELP_NEXT = BPAGE_NEXT


def wrap_lines(text, f, maxw):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if f.getlength(t) <= maxw or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = w
    lines.append(cur)
    return lines


def make_help():
    pw, ph = PANEL_BOX[2], PANEL_BOX[3]
    gx, gy, gap_x, gap_y = 24, 76, 12, 8
    cw = (pw - 2 * gx - 2 * gap_x) // 3
    chh = (ph - gy - 18 - 3 * gap_y) // 4
    for n, (title, items) in enumerate(HELP_PAGES):
        im = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        b = 6 * SS
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=24 * SS, fill=rgba(WHITE), outline=rgba(BLACK), width=b)
        d.text((gx * SS, 46 * SS), "Help: " + title, font=font(30 * SS, 800), fill=rgba(BLACK), anchor="ls")
        d.text((gx * SS + int(font(30 * SS, 800).getlength("Help: " + title + "    ")), 46 * SS), "tap anywhere else to close", font=font(16 * SS, 600), fill=rgba((110, 100, 140)), anchor="ls")
        for (ax, ay, aw, ah), left in ((BPAGE_PREV, True), (BPAGE_NEXT, False)):
            pts = [((ax + aw) * SS, ay * SS), ((ax + aw) * SS, (ay + ah) * SS), (ax * SS, (ay + ah / 2) * SS)] if left else \
                  [(ax * SS, ay * SS), (ax * SS, (ay + ah) * SS), ((ax + aw) * SS, (ay + ah / 2) * SS)]
            d.polygon(pts, fill=rgba(BLACK))
        tx0, ty0, tw, th = BPAGE_TEXT
        ctext_box(d, (tx0 * SS, ty0 * SS, (tx0 + tw) * SS, (ty0 + th) * SS), "%d / %d" % (n + 1, len(HELP_PAGES)), font(21 * SS, 700), rgba(BLACK))
        for k, (name, text) in enumerate(items):
            r, c = divmod(k, 3)
            x0, y0 = gx + c * (cw + gap_x), gy + r * (chh + gap_y)
            d.rounded_rectangle([x0 * SS, y0 * SS, (x0 + cw) * SS, (y0 + chh) * SS], radius=14 * SS, fill=rgba(WHITE), outline=rgba(BLACK), width=3 * SS)
            d.text(((x0 + 14) * SS, (y0 + 25) * SS), name, font=font(19 * SS, 800), fill=rgba(PURPLE), anchor="ls")
            for px in (16, 15, 14, 13, 12):
                f = font(px * SS, 500)
                lines = wrap_lines(text, f, (cw - 28) * SS)
                if len(lines) * (px + 3) + 36 <= chh:
                    break
            assert len(lines) * (px + 3) + 36 <= chh, "help text too long: " + name
            for j, ln in enumerate(lines):
                d.text(((x0 + 14) * SS, (y0 + 28 + px + j * (px + 3)) * SS), ln, font=f, fill=rgba(BLACK), anchor="ls")
        down(im, pw, ph).save(os.path.join(OUT, "help_%d.png" % (n + 1)), optimize=True)
    # the ? button
    cx, cy, dia = HELP_BTN
    for on in (False, True):
        im = Image.new("RGBA", (dia * SS, dia * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.ellipse([1.5 * SS, 1.5 * SS, (dia - 1.5) * SS, (dia - 1.5) * SS], fill=rgba(BLACK if on else WHITE), outline=rgba(BLACK), width=3 * SS)
        ctext_box(d, [0, 0, dia * SS, dia * SS], "?", font(24 * SS, 800), rgba(WHITE if on else BLACK))
        down(im, dia, dia).save(os.path.join(OUT, "help_btn%s.png" % ("_on" if on else "")))


# ---- value text must fit under its knob ----------------------------------------------------------------------------------------
def check_layout():
    for key, (cx, cy, r, label, base_y, vp) in KNOBS.items():
        assert r >= 32, key
        f = font(vp)
        for s in WORST[key]:
            assert f.getlength(s) <= (2 * r + 2) - 6, "%s: '%s' is wider than the text box" % (key, s)
        assert cy + r + VAL_GAP + vp * 0.6 <= PANEL_Y1 - 4, "%s: value text leaves its panel" % key
        if label:
            assert cy - r > base_y + 4, "%s: label overlaps the knob" % key
            lw = (fit(label, min(2 * r + 20, 90 if r >= 50 else 84), LABEL_PX, 700).getlength(label) / SS) if key not in ("fsmooth", "air", "natural", "mix") else font(24, 700).getlength(label)
            assert lw <= 2 * r + 24, "%s label too wide" % key
    # knobs in one panel never touch each other
    ks = list(KNOBS.items())
    for i in range(len(ks)):
        for j in range(i + 1, len(ks)):
            (_, (x1, y1, r1, *_)), (_, (x2, y2, r2, *_)) = ks[i], ks[j]
            if page_of(ks[i][0]) != page_of(ks[j][0]) and None not in (page_of(ks[i][0]), page_of(ks[j][0])):
                continue
            assert math.hypot(x1 - x2, y1 - y2) > r1 + r2 + 8, "%s and %s touch" % (ks[i][0], ks[j][0])
    assert KNOBS["fsmooth"][0] - KNOBS["fsmooth"][2] > LEFT[0] + BORDER + 8
    assert KNOBS["color"][0] + KNOBS["color"][2] < RIGHT[2] - BORDER - 8 and KNOBS["guard"][0] + KNOBS["guard"][2] < RIGHT[2] - BORDER - 8
    assert STRAW[0] + 162 < HELP_BTN[0] - HELP_BTN[2] // 2 and HELP_BTN[0] + HELP_BTN[2] // 2 + 6 < PILL[0] and PILL[0] + PILL[2] + 8 < LEARN[0] and LEARN[0] + LEARN[1] + 6 < TABS[0][1]
    for (_, x0_, w0_), (_, x1_, _) in zip(TABS, TABS[1:]):
        assert x0_ + w0_ + 4 <= x1_
    assert TABS[-1][1] + TABS[-1][2] <= W - MARGIN + 4


# ---- layout.conf -------------------------------------------------------------------------------------------------------------
QLINKS = ",".join("ql%02d" % (i + 1) for i in range(16))


def write_layout(browser_files):
    L = ["# Generated by generate_skin.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=45287E", "theme_ink=000005", "theme_ink_dim=000005", "theme_accent=45287E", "",
         "[tab POLI-JUICEBOX]", "art file=skin_assets/bg.png fit=stretch", ""]

    def knob_line(key, cx, cy, r, vp):
        hbox = max(26, round(vp * 1.2))
        vy = VAL_GAP - hbox // 2 - 2                         # value text centre = cy + r + VAL_GAP
        return 'knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=000005' % (cx, cy + Y_OFF, r, key, vp, vy, 2 * r + 2)

    L.append("# knobs: the value sits below each knob (the engine's text); the labels are in the background")
    for key in factory.KEYS:
        cx, cy, r, label, base_y, vp = KNOBS[key]
        pg = page_of(key)
        L.append(knob_line(key, cx, cy, r, vp) + ("" if pg is None else " when=page:%s" % PAGE_NAMES[pg]))
    lcx, lcy, lw, lh = LINK
    L += ["", "# P&F Link: Off / Variable (Shape page)",
          'toggle cx=%d cy=%d w=%d h=%d label="" key=link ns=0 img=skin_assets/link_off.png img_on=skin_assets/link_on.png when=page:Shape' % (lcx, lcy + Y_OFF, lw, lh)]
    # the right-hand pages: headings and labels overlay, and the tabs
    bx0, by0, bw0, bh0 = PAGE_BOX
    L += ["", "# right-hand pages (a tap on a tab changes the engine's page parameter; the knobs above carry when=page:...)"]
    for pg, nm in enumerate(PAGE_NAMES):
        L.append("art file=skin_assets/page_%d.png x=%d y=%d w=%d h=%d when=page:%s" % (pg, bx0, by0 + Y_OFF, bw0, bh0, nm))
    for i, (label, x, w) in enumerate(TABS):   # the selected tab (black) shows on its own page; the white one on every other page (one when= per widget)
        for state, nm in [("on", PAGE_NAMES[i])] + [("off", n) for j, n in enumerate(PAGE_NAMES) if j != i]:
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=tab%d img=skin_assets/tab%d_%s.png img_on=skin_assets/tab%d_%s.png when=page:%s'
                     % (x + w // 2, TAB_Y + TAB_H // 2 + Y_OFF, w, TAB_H, i + 1, i + 1, state, i + 1, state, nm))
    # preset selector
    px_, py_, pw_, ph_ = PILL
    nx, ny, nw, nh = NAME_BOX
    L += ["", "# preset selector: arrows and name on the purple header; a tap on the name opens the browser",
          "meter cx=%d cy=%d w=%d h=%d key=preset strip=skin_assets/preset_names.png frames=%d" % (nx + nw // 2, ny + nh // 2 + Y_OFF, nw, nh, len(PRESET_NAMES)),
          'button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/hit.png img_on=skin_assets/hit.png' % (nx + nw // 2, ny + nh // 2 + Y_OFF, nw, ph_ + 8)]
    for side, x in (("prev", px_ + PILL_ARROW_BOX[0] // 2 + 2), ("next", px_ + pw_ - PILL_ARROW_BOX[0] // 2 - 2)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_%s img=skin_assets/arrow_sel_%s.png img_on=skin_assets/arrow_sel_%s_on.png'
                 % (x, py_ + ph_ // 2 + Y_OFF, PILL_ARROW_BOX[0], PILL_ARROW_BOX[1], side, side, side))
    # browser: on top of everything
    bx, by, bw, bh = PANEL_BOX
    L += ["", "# preset browser (on top of everything): one panel picture per (category, page); a tap on its background closes it"]
    for v, fn in enumerate(browser_files):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/%s img_on=skin_assets/%s when=bview:%d'
                 % (bx + bw // 2, by + bh // 2 + Y_OFF, bw, bh, fn, fn, v + 1))
    L.append("# the current preset's cell")
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/browser_hl.png img_on=skin_assets/browser_hl.png when=pre_hl:%d'
                 % (bx + cx0 + CELL_W // 2, by + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1, slot + 1))
    L.append("# categories, cells, page arrows (transparent; the panel picture draws them)")
    for i in range(len(factory.CAT_NAMES)):
        y0 = 18 + i * (PILL_H + PILL_GAP)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=cat%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx + 20 + SIDE_W // 2, by + y0 + PILL_H // 2 + Y_OFF, SIDE_W, PILL_H + PILL_GAP, i + 1))
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx + cx0 + CELL_W // 2, by + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1))
    for key, (ax, ay, aw, ah) in (("bpage_prev", BPAGE_PREV), ("bpage_next", BPAGE_NEXT)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx + ax + aw // 2, by + ay + ah // 2 + Y_OFF, aw + 20, ah + 14, key))
    # latency pills: one footer picture per mode (shown on the Tracking page only: lview is 0 elsewhere), three hit areas per picture
    lx, ly, lw_, lh_ = LAT_BAR
    L += ["", "# latency (Tracking page footer): Fast 8.7 ms / Balanced 11.6 ms / Smooth 17.4 ms; lview = 0 off the page, else 1 + mode"]
    for n in range(len(LAT_PILLS)):
        L.append("art file=skin_assets/lat_%d.png x=%d y=%d w=%d h=%d when=lview:%d" % (n + 1, lx, ly + Y_OFF, lw_, lh_, n + 1))
    for n in range(len(LAT_PILLS)):
        for i, (px, pw2) in enumerate(lat_pill_boxes()):
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=lat%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=lview:%d'
                     % (lx + px + pw2 // 2, ly + lh_ // 2 + Y_OFF, pw2, lh_ + 4, i + 1, n + 1))
    # Learn: listens to the voice (switches itself off when it has heard ~6 s), the bar under the button shows the progress
    lx0, lw0 = LEARN
    L += ["", "# Learn (header): on = listening; the engine turns it off by itself",
          'toggle cx=%d cy=%d w=%d h=%d label="" key=learn ns=0 img=skin_assets/learn_off.png img_on=skin_assets/learn_on.png' % (lx0 + lw0 // 2, TAB_Y + TAB_H // 2 + Y_OFF, lw0, TAB_H),
          "meter cx=%d cy=%d w=%d h=%d key=learn_prog strip=skin_assets/learn_prog.png frames=%d" % (lx0 + lw0 // 2, TAB_Y + TAB_H + 2 + LEARN_BAR_H // 2 + Y_OFF, lw0, LEARN_BAR_H, PROG_FRAMES)]
    # help window: the ? button after the presets; the panel sits on top of everything
    hx, hy, hd = HELP_BTN
    L += ["", "# help: the ? button after the preset selector opens a two-page window (tips for every parameter); a tap on it closes it",
          'button cx=%d cy=%d w=%d h=%d label="" key=help_btn img=skin_assets/help_btn.png img_on=skin_assets/help_btn_on.png' % (hx, hy + Y_OFF, hd, hd)]
    for n in range(len(HELP_PAGES)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=help_btn img=skin_assets/help_%d.png img_on=skin_assets/help_%d.png when=hview:%d'
                 % (bx + bw // 2, by + bh // 2 + Y_OFF, bw, bh, n + 1, n + 1, n + 1))
    for key, (ax, ay, aw, ah) in (("help_next", BPAGE_PREV), ("help_next", BPAGE_NEXT)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png when=hview:1'
                 % (bx + ax + aw // 2, by + ay + ah // 2 + Y_OFF, aw + 20, ah + 14, key))
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png when=hview:2'
                 % (bx + ax + aw // 2, by + ay + ah // 2 + Y_OFF, aw + 20, ah + 14, key))
    L += ["", "# Q-Links (ql01..ql16, 4 to a Live III page): page 1 = the Smoothen panel (F-Smooth / Air / Natural / Mix) on every tab; pages 2-4 follow the tab",
          "# on show (make_params.QPAGES, the engine remaps the proxies): Shape = Pitch / Formant / F-Track / Link; Tone = Tamer, then Odd / Even / Color;",
          "# Filter = the three Freqs, then the three Qs; Tracking = Floor / Ceiling / Glide / Guard, then Rasp / Hold / Morph / Overlap",
          'qlinks "POLI-JUICEBOX" = %s' % QLINKS, "qlinks_track = %s" % QLINKS, ""]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    check_layout()
    factory.main()
    make_params.main()
    make_knobs()
    make_background()
    make_pages()
    make_tabs()
    make_link()
    make_arrows()
    make_preset_strip()
    make_help()
    make_latency()
    make_learn()
    browser_files = make_browser()
    write_layout(browser_files)
    print("knob radii %s; %d browser views; %d presets" % (radii(), len(browser_files), len(PRESET_NAMES)))
    print("wrote vst/skin_assets/*.png and vst/layout.conf")
