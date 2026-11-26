#!/usr/bin/env python3
"""Poli-Saturator skin: turns the Canva mockup (imgs/Poli-Saturator.jpg) into the MPC skin's images AND writes vst/layout.conf,
so every coordinate lives in this one file.

    python3 generate_skin.py            (from the Poli-Saturator folder; needs Pillow, numpy)

The mockup is 1920x1080 and only has the controls drawn in, so the clean background is rebuilt here: the two maroon corner blobs
are taken from the mockup (colour mask, stretched: they are shapeless), the title is lifted off it at its natural aspect, and
everything else (bar, labels, Type name, preset pill, panels) is drawn at screen size in the mockup's colours and Open Sans.
Controls are live widgets: knobs with the value BELOW (as the mockup's Saturation knob has it), the Type selector, the preset
selector and browser.

Geometry (MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628, layout.conf y = screen y + 86. The mockup's design is re-laid out for the
whole screen instead of stretched: knob radii 54 (six small knobs) and 134 (Saturation) are what the mockup had at that scale.
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
MOCKUP = "Poli-Saturator.jpg"

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H
SS = 4

BLUE = (199, 217, 231)           # the mockup's background
MAROON = (112, 42, 42)           # its corner blobs
BLACK = (0, 0, 5)                # knobs, bar, ink (never pure 0,0,0 in the art)
WHITE = (250, 251, 252)
PANEL = (233, 241, 247)          # browser panel: a little lighter than the page
GREY = (150, 160, 170)           # pressed arrow
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
BAR = (MARGIN, 119, W - MARGIN, 138)                 # the black rule under the title
TITLE_X = MARGIN
PILL = (430, 66, 400, 46)                            # preset selector: x, y, w, h (a black pill right of the title)
PILL_ARROW = (24, 30)                                # triangle w, h inside the pill
PILL_ARROW_BOX = (60, 56)                            # touch box
NAME_PAD = 66                                        # the name strip starts this far inside the pill

TYPE_CY = 186
TYPE_LABEL_BASE, TYPE_LABEL_PX = 200, 40
TYPE_ARROW_X = (228, 472)
TYPE_ARROW = (26, 34)                                # triangle w, h
TYPE_ARROW_BOX = (64, 60)
TYPE_NAME = (350, TYPE_CY, 170, 56)                  # cx, cy, w, h of the live name
TYPE_NAME_PX = 46
TYPE_LINE = (262, 213, 438, 217)

R_SMALL, R_SAT = 54, 134
LABEL_PX, LABEL_W = 30, 700
VAL_PX_SMALL, VAL_PX_SAT = 26, 40
COLS = (150, 350, 550)
ROW_CY = (316, 506)
ROW_LABEL_BASE = (250, 440)
VAL_GAP = 21                                         # value text centre below the knob's lower edge
KNOBS = {   # key: (cx, cy, r, label, label baseline, value px)
    "smash": (COLS[0], ROW_CY[0], R_SMALL, "Smash", ROW_LABEL_BASE[0], VAL_PX_SMALL),
    "crunch": (COLS[1], ROW_CY[0], R_SMALL, "Crunch", ROW_LABEL_BASE[0], VAL_PX_SMALL),
    "soften": (COLS[2], ROW_CY[0], R_SMALL, "Soften", ROW_LABEL_BASE[0], VAL_PX_SMALL),
    "low": (COLS[0], ROW_CY[1], R_SMALL, "Low", ROW_LABEL_BASE[1], VAL_PX_SMALL),
    "mid": (COLS[1], ROW_CY[1], R_SMALL, "Mid", ROW_LABEL_BASE[1], VAL_PX_SMALL),
    "high": (COLS[2], ROW_CY[1], R_SMALL, "High", ROW_LABEL_BASE[1], VAL_PX_SMALL),
    "sat": (900, 396, R_SAT, "Saturation", 232, VAL_PX_SAT),
}
SAT_LABEL_PX = 50

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
    return sorted({v[2] for v in KNOBS.values()})


def make_knobs():
    for r in radii():
        box = 2 * r + 8
        n = strip_frames(r)
        strip = Image.new("RGBA", (box, box * n), (0, 0, 0, 0))
        for i in range(n):
            strip.paste(knob_image(-135.0 + 270.0 * i / (n - 1), r, box), (0, i * box))
        strip.save(os.path.join(OUT, "knob_strip_%d.png" % box), optimize=True)


# ---- background -----------------------------------------------------------------------------------------------------------
def load(name):
    im = Image.open(os.path.join(IMGS, name)).convert("RGB")
    assert im.size == (MOCK_W, MOCK_H), "%s must be 1920x1080" % name
    return im


def blob_mask(mock):
    """the two maroon corner blobs of the mockup as a boolean mask (mockup pixels), with the controls that overlap them filled in"""
    a = np.asarray(mock).astype(int)
    m = np.abs(a - np.array(MAROON)).sum(axis=2) < 130
    keep = np.zeros(m.shape, bool)
    keep[0:300, 1100:1920] = True
    keep[600:1080, 0:420] = True
    m &= keep
    unknown = pn.box_mask(m.shape, [(1100, 196, 1760, 250), (180, 735, 392, 1010)])
    unknown &= keep
    img = np.repeat((m * 255).astype(np.uint8)[:, :, None], 3, axis=2)
    filled = pn.nearest_fill(img, unknown)[:, :, 0] > 127
    mask = filled & keep | (m & keep)
    # clean-up: drop specks and thin arcs of the mockup's controls (open), then fill pinholes (close)
    im = Image.fromarray((mask * 255).astype(np.uint8))
    im = im.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MaxFilter(9))
    im = im.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.MinFilter(9))
    return (np.asarray(im) > 127) & keep


def make_background():
    mock = load(MOCKUP)
    mask = blob_mask(mock)
    base_arr = np.full((MOCK_H, MOCK_W, 3), BLUE, np.uint8)
    base_arr[mask] = MAROON
    base = Image.fromarray(base_arr)
    # the blobs at screen size: stretched (shapeless), anti-aliased
    m = Image.fromarray((mask * 255).astype(np.uint8)).resize((W, H), Image.Resampling.BICUBIC)
    bg = Image.new("RGB", (W, H), BLUE)
    bg.paste(Image.new("RGB", (W, H), MAROON), (0, 0), m)
    # the title: lifted off the mockup at its natural aspect
    items = pn.lift(mock, base, [(96, 100, 700, 200)])
    pn.place(bg, items, lambda x: TITLE_X + (x - 117) * S, S, anchor=lambda it: "l")
    d = ImageDraw.Draw(bg)
    d.rectangle([BAR[0], BAR[1], BAR[2] - 1, BAR[3] - 1], fill=BLACK)
    # preset pill (the name and arrows are live widgets over it)
    x, y, w, h = PILL
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=h / 2, fill=BLACK)
    # Type row
    d.text((MARGIN, TYPE_LABEL_BASE), "Type:", font=font(TYPE_LABEL_PX, 700), fill=INK, anchor="ls")
    d.rectangle([TYPE_LINE[0], TYPE_LINE[1], TYPE_LINE[2] - 1, TYPE_LINE[3] - 1], fill=BLACK)
    # knob labels
    f = font(LABEL_PX, LABEL_W)
    for key, (cx, cy, r, text, base_y, vp) in KNOBS.items():
        ff = font(SAT_LABEL_PX, 720) if key == "sat" else f
        d.text((cx, base_y), text, font=ff, fill=INK, anchor="ms")
    bg.save(os.path.join(OUT, "bg.png"), optimize=True)


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
        arrow_image(side, TYPE_ARROW, TYPE_ARROW_BOX, BLACK).save(os.path.join(OUT, "arrow_type_%s.png" % side))
        arrow_image(side, TYPE_ARROW, TYPE_ARROW_BOX, MAROON).save(os.path.join(OUT, "arrow_type_%s_on.png" % side))
        arrow_image(side, PILL_ARROW, PILL_ARROW_BOX, WHITE).save(os.path.join(OUT, "arrow_sel_%s.png" % side))
        arrow_image(side, PILL_ARROW, PILL_ARROW_BOX, GREY).save(os.path.join(OUT, "arrow_sel_%s_on.png" % side))
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


# ---- live names: filmstrips (opaque, drawn by a meter widget: a picture swap would leave bars, see Poli-q's Dynamics pill) -------------
PRESET_NAMES = [d["name"] for d in factory.PRESETS]
NAME_BOX = (PILL[0] + NAME_PAD, PILL[1] + 4, PILL[2] - 2 * NAME_PAD, PILL[3] - 8)      # x, y, w, h of the preset name


def make_preset_strip():
    x, y, w, h = NAME_BOX
    strip = Image.new("RGB", (w, h * len(PRESET_NAMES)), BLACK)
    for i, nm in enumerate(PRESET_NAMES):
        im = Image.new("RGB", (w * SS, h * SS), BLACK)
        d = ImageDraw.Draw(im)
        ctext_box(d, (0, 0, w * SS, h * SS), nm, fit(nm, w - 8, 28, 700, 16), WHITE)
        strip.paste(down(im, w, h), (0, i * h))
    strip.save(os.path.join(OUT, "preset_names.png"), optimize=True)


def make_type_strip():
    _, _, w, h = TYPE_NAME
    strip = Image.new("RGB", (w, h * len(factory.TYPES)), BLUE)
    for i, nm in enumerate(factory.TYPES):
        im = Image.new("RGB", (w * SS, h * SS), BLUE)
        d = ImageDraw.Draw(im)
        ctext_box(d, (0, 0, w * SS, h * SS), nm, font(TYPE_NAME_PX * SS, 720), INK)
        strip.paste(down(im, w, h), (0, i * h))
    strip.save(os.path.join(OUT, "type_names.png"), optimize=True)


# ---- preset browser ------------------------------------------------------------------------------------------------------------
PANEL_BOX = (MARGIN, 146, W - 2 * MARGIN, 402)       # screen x, y, w, h: as wide as the page
SIDE_W, PILL_H, PILL_GAP = 188, 31, 3
GRID_X, GRID_Y = SIDE_W + 32, 64
CELL_H, CELL_GAP = 72, 10
CELL_W = (PANEL_BOX[2] - GRID_X - 15 - 2 * CELL_GAP) // 3
BDX = PANEL_BOX[2] - 985                              # the page arrows keep their place from the right edge
BPAGE_PREV = (806 + BDX, 8, 44, 38)
BPAGE_NEXT = (925 + BDX, 8, 44, 38)
BPAGE_TEXT = (856 + BDX, 8, 63, 38)


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
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=28 * SS, fill=rgba(PANEL), outline=rgba(BLACK), width=b)
        for i, nm in enumerate(cats):          # category column
            y0 = 14 + i * (PILL_H + PILL_GAP)
            on = i == c
            box = [16 * SS, y0 * SS, (16 + SIDE_W) * SS, (y0 + PILL_H) * SS]
            d.rounded_rectangle(box, radius=PILL_H * SS / 2, fill=rgba(MAROON if on else WHITE), outline=rgba(BLACK), width=2 * SS)
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
    # highlight of the current preset's cell: a maroon ring and a light tint, over the cell
    im = Image.new("RGBA", (CELL_W * SS, CELL_H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, CELL_W * SS - 1, CELL_H * SS - 1], radius=16 * SS, fill=rgba(MAROON, 60), outline=rgba(MAROON), width=6 * SS)
    down(im, CELL_W, CELL_H).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- value text must fit under its knob ----------------------------------------------------------------------------------------
WORST = {"sat": ["100 %"], "smash": ["100 %"], "crunch": ["100 %"], "soften": ["100 %"],
         "low": ["+12 dB", "-24 dB", "Off"], "mid": ["+12 dB", "-24 dB", "Off"], "high": ["+12 dB", "-24 dB", "Off"]}


def check_layout():
    for key, (cx, cy, r, label, base_y, vp) in KNOBS.items():
        f = font(vp)
        for s in WORST[key]:
            assert f.getlength(s) <= (2 * r + 2) - 6, "%s: '%s' is wider than the text box" % (key, s)
        assert cy + r + VAL_GAP + vp * 0.6 <= H - 8, "%s: value text leaves the screen" % key
        assert cy - r > base_y + 6, "%s: label overlaps the knob" % key
    # columns do not touch each other
    assert COLS[1] - COLS[0] - 2 * R_SMALL > 40
    assert KNOBS["sat"][0] + R_SAT < 1040, "Saturation knob should stay left of the Q-Link sidebar (x ~ 1025-1040)"


# ---- layout.conf -------------------------------------------------------------------------------------------------------------
def write_layout(browser_files):
    L = ["# Generated by generate_skin.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=C7D9E7", "theme_ink=000005", "theme_ink_dim=000005", "theme_accent=702A2A", "",
         "[tab POLI-SATURATOR]", "art file=skin_assets/bg.png fit=stretch", ""]

    def knob_line(key, cx, cy, r, vp):
        hbox = max(26, round(vp * 1.2))
        vy = VAL_GAP - hbox // 2 - 2                         # value text centre = cy + r + VAL_GAP
        return 'knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=000005' % (cx, cy + Y_OFF, r, key, vp, vy, 2 * r + 2)

    L.append("# knobs: the value sits below each knob (the engine's text); the labels are in the background")
    for key, (cx, cy, r, label, base_y, vp) in KNOBS.items():
        L.append(knob_line(key, cx, cy, r, vp))
    # Type selector
    tcx, tcy, tw, th = TYPE_NAME
    L += ["", "# Type: < name > ; the name is a 4-frame filmstrip driven by the type parameter",
          "meter cx=%d cy=%d w=%d h=%d key=type strip=skin_assets/type_names.png frames=%d" % (tcx, tcy + Y_OFF, tw, th, len(factory.TYPES))]
    for side, x in zip(("prev", "next"), TYPE_ARROW_X):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=type_%s img=skin_assets/arrow_type_%s.png img_on=skin_assets/arrow_type_%s_on.png'
                 % (x, TYPE_CY + Y_OFF, TYPE_ARROW_BOX[0], TYPE_ARROW_BOX[1], side, side, side))
    # preset selector
    px_, py_, pw_, ph_ = PILL
    nx, ny, nw, nh = NAME_BOX
    L += ["", "# preset selector: name between the arrows in the black pill; a tap on the name opens the browser",
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
    L += ["", "# Q-Links, 4 to a Live III page: 1 Saturation / Smash / Crunch / Soften, 2 Low / Mid / High / Type",
          'qlinks "POLI-SATURATOR" = sat,smash,crunch,soften,low,mid,high,type,-,-,-,-,-,-,-,-', ""]
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
    make_arrows()
    make_preset_strip()
    make_type_strip()
    browser_files = make_browser()
    write_layout(browser_files)
    print("knob radii %s; %d browser views; %d presets" % (radii(), len(browser_files), len(PRESET_NAMES)))
    print("wrote vst/skin_assets/*.png and vst/layout.conf")
