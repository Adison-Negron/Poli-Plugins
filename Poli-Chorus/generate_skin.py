#!/usr/bin/env python3
"""Poli-Chorus skin: turns the Canva mockup (imgs/Poli-Chorus.png) into the MPC skin's images AND writes vst/layout.conf, vst/params.json,
src/layout_gen.h and src/presets.h, so every coordinate lives in this one file.

    python3 generate_skin.py            (from the Poli-Chorus folder; needs Pillow, numpy)

The mockup is 1460x824 and only has the controls drawn in, so the clean background is rebuilt here: the lighter blobs are taken from the
mockup (colour mask, controls filled in, smoothed, stretched: they are shapeless), the title is lifted off it at its natural aspect, and
everything else (bar, labels, buttons, the display box frame) is drawn at screen size in the mockup's colours and Open Sans.

Live parts: eight knobs with the value BELOW each one (the engine's text), the display box (two waves, one meter per column: black =
left LFO, red = right LFO, drawn from Rate / Amount / Width / Mode by the engine), the three Mode buttons (the active one lit red),
the preset selector and the categorized preset browser.

Geometry (MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628, layout.conf y = screen y + 86. The design is spread over the whole width
(x positions mapped, every shape kept natural: text, buttons, knobs, the box).
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
SRC = os.path.join(HERE, "src")
MOCKUP = "Poli-Chorus.png"

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1460, 824
S = H / MOCK_H                    # 0.762: the mockup's height scaled to the screen's
SS = 4

GREEN = (199, 231, 199)          # the mockup's background
LIME = (222, 252, 203)           # its lighter blobs
BLACK = (0, 0, 5)                # knobs, bar, ink (never pure 0,0,0 in the art)
WHITE = (250, 251, 252)
RED = (255, 49, 49)              # the right LFO in the mockup, also the lit button
PANEL = (240, 252, 236)          # browser panel
GREY = (70, 72, 74)              # a pressed button
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
X0M, X1M = 82, 1352                                  # the mockup's left / right edge of the bar
KX = (W - 2 * MARGIN) / (X1M - X0M)                  # x spread 1.0 .. 1152 px


def sx(x):
    """mockup x -> screen x (positions only: shapes keep their natural size)"""
    return MARGIN + (x - X0M) * KX


BAR = (MARGIN, 118, W - MARGIN, 134)                 # the black rule under the title
TITLE_X = MARGIN
TITLE_BOX = (70, 70, 470, 152)                       # where the mockup's title is lifted from (mockup px)

# preset selector: < name > in the header, right of the title (as in the mockup: no pill, black triangles)
SEL_CY = 93
NAME_BOX = (790, 71, 350, 44)                        # x, y, w, h of the live name (an opaque strip cut from the background itself)
ARROW_CX = (750, 1180)
ARROW = (34, 40)                                     # triangle w, h
ARROW_BOX = (64, 58)                                 # touch box

# the display box: a thick black frame round a white field; the waves are columns of meters inside it
VIZ_OUT = (64, 149, 722, 336)                        # outer edge
VIZ_FRAME = 9
VIZ_IN = (VIZ_OUT[0] + VIZ_FRAME, VIZ_OUT[1] + VIZ_FRAME, VIZ_OUT[2] - VIZ_FRAME, VIZ_OUT[3] - VIZ_FRAME)   # 73,158 .. 713,327
VIZ_W, VIZ_H = VIZ_IN[2] - VIZ_IN[0], VIZ_IN[3] - VIZ_IN[1]                                                 # 640 x 169
VIZ_CW = 5                                           # column width
VIZ_COLS = VIZ_W // VIZ_CW                           # 128
VIZ_FRAMES = (12288 // VIZ_H) | 1                    # 71: odd, the strip stays under MPC's 12288 px; the middle frame = the centre line
if VIZ_FRAMES * VIZ_H > 12288:
    VIZ_FRAMES -= 2
VIZ_LINE = 8                                         # stroke thickness (px)
VIZ_AMP_MAX = 0.78                                   # wave amplitude at Amount 100 % (fraction of the half height)

# the Mode buttons under the box
MODE_Y, MODE_H = 351, 48
MODES = [("Classic", 96, 175), ("Ensemble", 296, 196), ("Vibrato", 514, 177)]   # name, x, w
MODE_PX = 34

R_KNOB = 58
LABEL_PX, LABEL_W = 30, 700
VAL_PX = 26
VAL_GAP = 21                                         # value text centre below the knob's lower edge
ROW_CY = (276, 520)
LABEL_BASE = (186, 440)
COLS = (821, 988, 1155)
KNOBS = {   # key: (cx, cy, label, label baseline)
    "rate": (COLS[0], ROW_CY[0], "Rate", LABEL_BASE[0]),
    "amount": (COLS[1], ROW_CY[0], "Amount", LABEL_BASE[0]),
    "feedback": (COLS[2], ROW_CY[0], "Feedback", LABEL_BASE[0]),
    "warmth": (COLS[0], ROW_CY[1], "Warmth", LABEL_BASE[1]),
    "output": (COLS[1], ROW_CY[1], "Output", LABEL_BASE[1]),
    "mix": (COLS[2], ROW_CY[1], "Mix", LABEL_BASE[1]),
    "width": (182, ROW_CY[1], "Width", LABEL_BASE[1]),
    "lpf": (393, ROW_CY[1], "LP Filter", LABEL_BASE[1]),
}

# knob look: black disc, a white pointer line from near the rim to 0.4 r (the mockup's, kept long and bold so it reads on the small screen)
POINTER_OUT, POINTER_IN = 0.96, 0.40


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
    return [R_KNOB]


def make_knobs():
    for r in radii():
        box = 2 * r + 8
        n = strip_frames(r)
        strip = Image.new("RGBA", (box, box * n), (0, 0, 0, 0))
        for i in range(n):
            strip.paste(knob_image(-135.0 + 270.0 * i / (n - 1), r, box), (0, i * box))
        strip.save(os.path.join(OUT, "knob_strip_%d.png" % box), optimize=True)


# ---- the waves: column strips (transparent) --------------------------------------------------------------------------------------
def make_viz_strips():
    """One strip per colour: frame k = a horizontal stroke at the height of value k (0 = bottom of the box, middle frame = the centre
    line), as wide as a column. Exact coverage per pixel row (anti-aliased)."""
    rows = np.arange(VIZ_H, dtype=np.float64)
    for name, colour in (("viz_black", BLACK), ("viz_red", RED)):
        a = np.zeros((VIZ_FRAMES * VIZ_H, VIZ_CW, 4), np.uint8)
        a[:, :, :3] = colour
        for k in range(VIZ_FRAMES):
            f = k / (VIZ_FRAMES - 1)
            yc = (1.0 - f) * VIZ_H
            cov = np.clip(np.minimum(yc + VIZ_LINE / 2, rows + 1) - np.maximum(yc - VIZ_LINE / 2, rows), 0, 1)
            a[k * VIZ_H:(k + 1) * VIZ_H, :, 3] = (cov * 255).astype(np.uint8)[:, None]
        Image.fromarray(a, "RGBA").save(os.path.join(OUT, name + ".png"), optimize=True)


# ---- background -----------------------------------------------------------------------------------------------------------
def load(name):
    im = Image.open(os.path.join(IMGS, name)).convert("RGB")
    assert im.size == (MOCK_W, MOCK_H), "%s must be %dx%d" % (name, MOCK_W, MOCK_H)
    return im


def blob_mask(mock):
    """the lighter blobs of the mockup as a boolean mask (mockup pixels): everything that is not blob-coloured or background-coloured
    (controls, text, anti-aliased edges) takes the nearest known pixel's class, then the outline is smoothed"""
    a = np.asarray(mock).astype(int)
    lime = np.abs(a - np.array(LIME)).sum(axis=2) < 24
    green = np.abs(a - np.array(GREEN)).sum(axis=2) < 24
    unknown = ~(lime | green)
    unknown = np.asarray(Image.fromarray((unknown * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))) > 127   # edges of controls
    img = np.repeat((lime * 255).astype(np.uint8)[:, :, None], 3, axis=2)
    filled = pn.nearest_fill(img, unknown)[:, :, 0] > 127
    im = Image.fromarray((filled * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(16))
    im = im.point(lambda v: 0 if v < 112 else 255 if v > 144 else int((v - 112) * 255 / 32))
    return im


def make_background():
    mock = load(MOCKUP)
    soft = blob_mask(mock)                                  # L image, 1460x824, soft edge
    hard = np.asarray(soft) > 127
    base_arr = np.full((MOCK_H, MOCK_W, 3), GREEN, np.uint8)
    base_arr[hard] = LIME
    base = Image.fromarray(base_arr)
    m = soft.resize((W, H), Image.Resampling.BICUBIC)
    bg = Image.new("RGB", (W, H), GREEN)
    bg.paste(Image.new("RGB", (W, H), LIME), (0, 0), m)
    # the title: lifted off the mockup at its natural aspect
    items = pn.lift(mock, base, [TITLE_BOX])
    assert items, "no title found in the mockup"
    x_left = min(it["box"][0] for it in items)
    pn.place(bg, items, lambda x: TITLE_X + (x - x_left) * S, S, anchor=lambda it: "l")
    d = ImageDraw.Draw(bg)
    d.rectangle([BAR[0], BAR[1], BAR[2] - 1, BAR[3] - 1], fill=BLACK)
    # the display box: frame + white field (the waves are live meters over it)
    d.rectangle([VIZ_OUT[0], VIZ_OUT[1], VIZ_OUT[2] - 1, VIZ_OUT[3] - 1], fill=BLACK)
    d.rectangle([VIZ_IN[0], VIZ_IN[1], VIZ_IN[2] - 1, VIZ_IN[3] - 1], fill=WHITE)
    # Mode buttons (idle: black with white text; the lit and pressed ones are separate pictures over them)
    for name, x, w in MODES:
        d.rectangle([x, MODE_Y, x + w - 1, MODE_Y + MODE_H - 1], fill=BLACK)
        ctext_box(d, (x, MODE_Y, x + w, MODE_Y + MODE_H), name, font(MODE_PX, 700), WHITE)
    # knob labels
    f = font(LABEL_PX, LABEL_W)
    for key, (cx, cy, text, base_y) in KNOBS.items():
        d.text((cx, base_y), text, font=f, fill=INK, anchor="ms")
    bg.save(os.path.join(OUT, "bg.png"), optimize=True)
    return bg


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
        arrow_image(side, ARROW, ARROW_BOX, BLACK).save(os.path.join(OUT, "arrow_sel_%s.png" % side))
        arrow_image(side, ARROW, ARROW_BOX, RED).save(os.path.join(OUT, "arrow_sel_%s_on.png" % side))
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))


# ---- text helpers --------------------------------------------------------------------------------------------------------------
def fit(text, maxw, px, weight=700, floor=11):
    f = font(px * SS, weight)
    while f.getlength(text) > maxw * SS and px > floor:
        px -= 1
        f = font(px * SS, weight)
    return f


def ctext_box(d, box, text, f, fill):
    x0, y0, x1, y1 = box
    cap = f.getbbox("H", anchor="ls")
    d.text(((x0 + x1) / 2, (y0 + y1) / 2 + (cap[3] - cap[1]) / 2), text, font=f, fill=fill, anchor="ms")


# ---- Mode buttons: the lit one (red) and the pressed one (grey) ------------------------------------------------------------------
def make_mode_images():
    for name, x, w in MODES:
        for tag, fill in (("lit", RED), ("press", GREY)):
            im = Image.new("RGB", (w * SS, MODE_H * SS), fill)
            d = ImageDraw.Draw(im)
            ctext_box(d, (0, 0, w * SS, MODE_H * SS), name, font(MODE_PX * SS, 700), WHITE)
            if tag == "lit":   # a thin black keyline so the lit button keeps the same edge as the idle ones
                d.rectangle([0, 0, w * SS - 1, MODE_H * SS - 1], outline=BLACK, width=3 * SS)
            down(im, w, MODE_H).save(os.path.join(OUT, "mode_%s_%s.png" % (name.lower(), tag)))


# ---- live preset name: an opaque filmstrip cut from the background itself (no seam with the blobs) ------------------------------------
PRESET_NAMES = [d["name"] for d in factory.PRESETS]


def make_preset_strip(bg):
    x, y, w, h = NAME_BOX
    crop = bg.crop((x, y, x + w, y + h))
    strip = Image.new("RGB", (w, h * len(PRESET_NAMES)))
    for i, nm in enumerate(PRESET_NAMES):
        im = crop.resize((w * SS, h * SS), Image.Resampling.BICUBIC)
        d = ImageDraw.Draw(im)
        ctext_box(d, (0, 0, w * SS, h * SS), nm, fit(nm, w - 16, 36, 700, 18), BLACK)
        strip.paste(down(im, w, h), (0, i * h))
    strip.save(os.path.join(OUT, "preset_names.png"), optimize=True)


# ---- preset browser ------------------------------------------------------------------------------------------------------------
PANEL_BOX = (48, 140, W - 96, 484)                   # screen x, y, w, h: wider than the page and down to the bottom, so it hides every control under it
SIDE_W, PILL_H, PILL_GAP = 188, 31, 3
GRID_X, GRID_Y = SIDE_W + 32, 64
CELL_H, CELL_GAP = 80, 10
CELL_W = (PANEL_BOX[2] - GRID_X - 15 - 2 * CELL_GAP) // 3
BDX = PANEL_BOX[2] - 985                              # the page arrows keep their place from the right edge
BPAGE_PREV = (806 + BDX, 8, 44, 38)
BPAGE_NEXT = (925 + BDX, 8, 44, 38)
BPAGE_TEXT = (856 + BDX, 8, 63, 38)


def cell_pos(slot):
    r, c = divmod(slot, 3)
    return GRID_X + c * (CELL_W + CELL_GAP), GRID_Y + r * (CELL_H + 12)


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
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=28 * SS, fill=rgba(PANEL), outline=rgba(BLACK), width=b)
        for i, nm in enumerate(cats):          # category column
            y0 = 14 + i * (PILL_H + PILL_GAP)
            on = i == c
            box = [16 * SS, y0 * SS, (16 + SIDE_W) * SS, (y0 + PILL_H) * SS]
            d.rounded_rectangle(box, radius=PILL_H * SS / 2, fill=rgba(RED if on else WHITE), outline=rgba(BLACK), width=2 * SS)
            ctext_box(d, box, nm, fit(nm, SIDE_W - 20, 20, 800 if on else 650), rgba(WHITE if on else BLACK))
        f = font(27 * SS, 800)                 # header: category, page n / m, arrows
        d.text((GRID_X * SS, 30 * SS), cats[c], font=f, fill=rgba(BLACK), anchor="ls")
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
            nm = factory.PRESETS[members[c][k]]["name"]
            ctext_box(d, box, nm, fit(nm, CELL_W - 22, 23, 700), rgba(BLACK))
        fn = "browser_%d.png" % v
        down(im, pw, ph).save(os.path.join(OUT, fn), optimize=True)
        files.append(fn)
    # highlight of the current preset's cell: a red ring and a light tint, over the cell
    im = Image.new("RGBA", (CELL_W * SS, CELL_H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, CELL_W * SS - 1, CELL_H * SS - 1], radius=16 * SS, fill=rgba(RED, 50), outline=rgba(RED), width=6 * SS)
    down(im, CELL_W, CELL_H).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- value text must fit under its knob ----------------------------------------------------------------------------------------
WORST = {"rate": ["10.0 Hz", "0.05 Hz"], "amount": ["100 %"], "feedback": ["100 %"], "width": ["100 %"], "lpf": ["9.9 kHz", "19 kHz", "800 Hz", "Off"],
         "warmth": ["100 %"], "output": ["-24 dB", "+12 dB", "-9.5 dB"], "mix": ["100 %"]}


def check_layout():
    for key, (cx, cy, label, base_y) in KNOBS.items():
        for s in WORST[key]:   # MPC draws the value in Titillium Web: about 0.52 em per character
            assert len(s) * 0.52 * VAL_PX <= (2 * R_KNOB + 2) - 6, "%s: '%s' is wider than the text box" % (key, s)
        assert cy + R_KNOB + VAL_GAP + VAL_PX * 0.6 <= H - 8, "%s: value text leaves the screen" % key
        assert cy - R_KNOB > base_y + 6, "%s: label overlaps the knob" % key
        assert cx + R_KNOB <= W - MARGIN + 2 and cx - R_KNOB >= MARGIN - 2, "%s: knob leaves the margins" % key
    xs = sorted(v[0] for v in KNOBS.values() if v[1] == ROW_CY[1])
    assert all(b - a - 2 * R_KNOB > 40 for a, b in zip(xs, xs[1:])), "bottom-row knobs too close"
    assert COLS[1] - COLS[0] - 2 * R_KNOB > 40
    assert COLS[0] - R_KNOB > VIZ_OUT[2] + 20, "knobs touch the display box"
    assert VIZ_COLS * VIZ_CW == VIZ_W and VIZ_FRAMES % 2 == 1 and VIZ_FRAMES * VIZ_H <= 12288
    assert VIZ_AMP_MAX * VIZ_H / 2 + VIZ_LINE / 2 < VIZ_H / 2, "waves would leave the box"
    for name, x, w in MODES:
        assert VIZ_OUT[0] <= x and x + w <= VIZ_OUT[2], "%s button leaves the box column" % name
    assert MODE_Y > VIZ_OUT[3] + 8 and MODE_Y + MODE_H < LABEL_BASE[1] - 22 + 2, "Mode buttons collide with the lower labels"
    assert NAME_BOX[0] > ARROW_CX[0] + ARROW_BOX[0] // 2 and NAME_BOX[0] + NAME_BOX[2] < ARROW_CX[1] - ARROW_BOX[0] // 2, "preset name overlaps its arrows"
    assert ARROW_CX[1] + ARROW_BOX[0] // 2 <= W - MARGIN + 2
    assert NAME_BOX[1] + NAME_BOX[3] < BAR[1], "preset name touches the bar"


def write_layout_header():
    with open(os.path.join(SRC, "layout_gen.h"), "w", newline="\n") as f:
        f.write("// Generated by generate_skin.py -- edit the numbers there, not here.\n#pragma once\n"
                "static const int kVizCols = %d;       // waveform columns across the display box (vl001.. black = left, vr001.. red = right)\n"
                "static const int kVizFrames = %d;      // frames per column strip (odd: the middle frame = the centre line)\n"
                "static const float kVizAmpMax = %.2ff; // wave amplitude at Amount 100 %%, as a fraction of the box's half height\n"
                % (VIZ_COLS, VIZ_FRAMES, VIZ_AMP_MAX))


# ---- layout.conf -------------------------------------------------------------------------------------------------------------
def cbox(x0, y0, w, h):
    """a widget's cx, cy (layout.conf, y shifted) for a box whose top-left is x0, y0: MPC puts the left edge at cx - w//2"""
    return x0 + w // 2, y0 + h // 2 + Y_OFF


def write_layout(browser_files):
    L = ["# Generated by generate_skin.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=C7E7C7", "theme_ink=000005", "theme_ink_dim=000005", "theme_accent=FF3131", "",
         "[tab POLI-CHORUS]", "art file=skin_assets/bg.png fit=stretch", ""]

    def knob_line(key, cx, cy):
        hbox = max(26, round(VAL_PX * 1.2))
        vy = VAL_GAP - hbox // 2 - 2                         # value text centre = cy + r + VAL_GAP
        return 'knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=000005' % (cx, cy + Y_OFF, R_KNOB, key, VAL_PX, vy, 2 * R_KNOB + 2)

    L.append("# knobs: the value sits below each knob (the engine's text); the labels are in the background")
    for key, (cx, cy, label, base_y) in KNOBS.items():
        L.append(knob_line(key, cx, cy))
    # the waves: one meter per column and colour; the right (red) one first so the left (black) one is on top where they agree
    L += ["", "# display box: the right LFO (red) and the left LFO (black), one meter per column (the engine sets vr001.. / vl001..)"]
    for side, strip in (("r", "viz_red"), ("l", "viz_black")):
        for j in range(VIZ_COLS):
            cx, cy = cbox(VIZ_IN[0] + j * VIZ_CW, VIZ_IN[1], VIZ_CW, VIZ_H)
            L.append("meter cx=%d cy=%d w=%d h=%d key=v%s%03d strip=skin_assets/%s.png frames=%d" % (cx, cy, VIZ_CW, VIZ_H, side, j + 1, strip, VIZ_FRAMES))
    # Mode buttons: a transparent touch button (pressed = grey), and the lit copy shown while that mode is the current one
    L += ["", "# Mode: three buttons under the box; the current one is lit red (a copy shown with when=mode:<name>)"]
    for name, x, w in MODES:
        cx, cy = cbox(x, MODE_Y, w, MODE_H)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=mode_%s img=skin_assets/hit.png img_on=skin_assets/mode_%s_press.png'
                 % (cx, cy, w, MODE_H, name.lower(), name.lower()))
    for name, x, w in MODES:
        cx, cy = cbox(x, MODE_Y, w, MODE_H)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=mode_%s img=skin_assets/mode_%s_lit.png img_on=skin_assets/mode_%s_lit.png when=mode:%s'
                 % (cx, cy, w, MODE_H, name.lower(), name.lower(), name.lower(), name))
    # preset selector
    nx, ny, nw, nh = NAME_BOX
    ncx, ncy = cbox(nx, ny, nw, nh)
    L += ["", "# preset selector: name between the arrows in the header; a tap on the name opens the browser",
          "meter cx=%d cy=%d w=%d h=%d key=preset strip=skin_assets/preset_names.png frames=%d" % (ncx, ncy, nw, nh, len(PRESET_NAMES)),
          'button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/hit.png img_on=skin_assets/hit.png' % (ncx, ncy, nw, nh + 16)]
    for side, x in zip(("prev", "next"), ARROW_CX):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_%s img=skin_assets/arrow_sel_%s.png img_on=skin_assets/arrow_sel_%s_on.png'
                 % (x, SEL_CY + Y_OFF, ARROW_BOX[0], ARROW_BOX[1], side, side, side))
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
        y0 = 14 + i * (PILL_H + PILL_GAP)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=cat%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx + 16 + SIDE_W // 2, by + y0 + PILL_H // 2 + Y_OFF, SIDE_W, PILL_H + PILL_GAP, i + 1))
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx + cx0 + CELL_W // 2, by + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1))
    for key, (ax, ay, aw, ah) in (("bpage_prev", BPAGE_PREV), ("bpage_next", BPAGE_NEXT)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx + ax + aw // 2, by + ay + ah // 2 + Y_OFF, aw + 20, ah + 14, key))
    L += ["", "# Q-Links, 4 to a Live III page: 1 Rate / Amount / Feedback / Mix, 2 Width / LP Filter / Warmth / Output, 3 Mode",
          'qlinks "POLI-CHORUS" = rate,amount,feedback,mix,width,lpf,warmth,output,mode,-,-,-,-,-,-,-', ""]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):   # stale pictures from an earlier layout would ship too (a read-only / no-delete folder just overwrites)
        try:
            os.remove(os.path.join(OUT, f))
        except PermissionError:
            pass
    check_layout()
    factory.main()
    write_layout_header()
    make_params.main()
    make_knobs()
    make_viz_strips()
    bg = make_background()
    make_arrows()
    make_mode_images()
    make_preset_strip(bg)
    browser_files = make_browser()
    write_layout(browser_files)
    print("knob radius %d; display box %dx%d, %d columns x %d frames; %d browser views; %d presets"
          % (R_KNOB, VIZ_W, VIZ_H, VIZ_COLS, VIZ_FRAMES, len(browser_files), len(PRESET_NAMES)))
    print("wrote vst/skin_assets/*.png, vst/layout.conf, src/layout_gen.h")
