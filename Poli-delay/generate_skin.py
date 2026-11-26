#!/usr/bin/env python3
"""Poli-delay skin: turns the Canva mockups (imgs/) into the MPC skin's images AND writes vst/layout.conf, so every
coordinate lives in this one file.

    python3 generate_skin.py            (from the Poli-delay folder; needs Pillow, numpy)

imgs/: BG.jpg = the clean frame (title, header, empty panel), GUI.jpg = the design with every control drawn (for reference:
the control positions below were measured on it). The labels and the section line are drawn here in the mockup's font
(Open Sans, fitted to the mockup's lettering), the controls are live widgets: knobs (value inside), toggles (outlined = off,
filled maroon with a lit dot = on), the preset selector and browser.

Offset: the Offset toggle reveals a stereo-offset knob (when=offset:On). With Offset off the Sync / Offset pills sit where the
mockup draws them; with it on they close up and the Offset knob appears under them, beside Time.

Geometry (MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628, layout.conf y = screen y + 86; the 1920x1080 mockup is scaled
uniformly by 628/1080 and left-aligned. Knobs are bigger than the mockup's where there was room (user's standing rule: the
MPC Live III screen is small): r 56 upper row, r 92 Mix, r 46 lower row; value text 20-36 px.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

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

MAROON = (90, 50, 50)
PANEL = (240, 240, 242)
WHITE = (243, 240, 244)
AMBER = (255, 196, 92)
BLACK = (0, 0, 5)
GREY = (205, 190, 192)          # pressed arrow
INK = MAROON

FONT = ppu.OPEN_SANS


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


# ---- mockup geometry (measured on imgs/GUI.jpg, 1920x1080) -----------------------------------------------------------------
# Knob: key -> (mockup cx, cy, screen radius, value text px, value text centre below the knob centre as a fraction of r, when)
R_UP, R_MIX, R_LOW, R_OFF = 60, 100, 52, 40
KNOBS = {
    "time": (273.0, 457.0, R_UP, 22, 0.06, None),
    "feedback": (720.5, 440.5, R_UP, 24, 0.06, None),
    "hp": (982.5, 440.5, R_UP, 22, 0.06, None),
    "lp": (1259.0, 440.5, R_UP, 22, 0.06, None),
    "mix": (1539.0, 540.0, R_MIX, 38, 0.06, None),
    "rate": (229.0, 856.0, R_LOW, 21, 0.06, None),
    "depth": (407.5, 856.0, R_LOW, 21, 0.06, None),
    "drive": (614.0, 856.0, R_LOW, 21, 0.06, None),
    "tone": (832.0, 856.0, R_LOW, 21, 0.06, None),
    "spread": (1096.0, 856.0, R_LOW, 21, 0.06, None),
}
# the Offset knob: screen px (not mockup), shown only while the Offset toggle is on
OFFSET_KNOB = dict(cx=round(sx(496.5)), cy=306, r=R_OFF, vs=16, dy=0.0)
# toggles: key -> (mockup box x0,y0,x1,y1, label); the Sync / Offset pills have a second, closed-up place (screen px) for Offset on
TOGGLES = {
    "sync": ((396, 362, 597, 431), "Sync"),
    "offset": ((396, 472, 597, 542), "Offset"),
    "pingpong": ((1402, 829, 1677, 899), "Ping-Pong"),
}
CLOSED = {"sync": (round(sx(496.5)), 190), "offset": (round(sx(496.5)), 232)}       # screen cx, cy with Offset on
CLOSED_H = 34
TOGGLE_PX = 25
# static labels: (text, mockup cx, baseline y)
LABEL_PX, LABEL_W = 44, 640
TITLE_PX, TITLE_W = 54, 720
LABELS = [("Time", 273, 339), ("Feedback", 720.5, 337), ("High Pass", 982.5, 337), ("Low Pass", 1259, 337), ("Mix", 1540, 341),
          ("Rate", 229, 745), ("Depth", 408, 745), ("Drive", 614, 745), ("Tone", 833, 745), ("Spread", 1097, 745)]
TITLES = [("Modulation", 310, 675), ("Saturation", 681, 675), ("Width", 1095, 675)]
BAR = (154, 687, 1378, 699)
# header: the placeholder name and the two baked arrows are erased and redrawn live
HEADER_ERASE = (600, 124, 1346, 206)
SEL_ARROWS = ((616, 139, 670, 200), (1284, 133, 1338, 194))      # mockup boxes of the baked arrows
SEL_CY = 166                                                      # mockup centre line of both arrows
SEL_TEXT_CY = 160                                                 # ... and of the name

# ---- knob images ------------------------------------------------------------------------------------------------------
POINTER_W = 0.20
POINTER_LEN = 0.26


def knob_image(angle, r, size):
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS / 2
    rr = r * SS
    d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=rgba(MAROON))
    a = math.radians(angle)
    ux, uy = math.sin(a), -math.cos(a)
    px, py = -uy, ux
    hw = max(POINTER_W * rr, 3.5 * SS) / 2
    r_out, r_in = rr * 0.97, rr * (0.97 - POINTER_LEN)
    d.polygon([(c + ux * r_out + px * hw, c + uy * r_out + py * hw), (c + ux * r_out - px * hw, c + uy * r_out - py * hw),
               (c + ux * r_in - px * hw, c + uy * r_in - py * hw), (c + ux * r_in + px * hw, c + uy * r_in + py * hw)],
              fill=rgba(PANEL))
    return down(big, size, size)


def strip_frames(r):
    """odd frame count whose strip stays under MPC's 12288 px (frame = 2r+10)"""
    n = 12288 // (2 * r + 10)
    return n if n % 2 else n - 1


def radii():
    return sorted({v[2] for v in KNOBS.values()} | {OFFSET_KNOB["r"]})


def make_knobs():
    for r in radii():
        box = 2 * r + 8
        n = strip_frames(r)
        strip = Image.new("RGBA", (box, box * n), (0, 0, 0, 0))
        for i in range(n):
            strip.paste(knob_image(-135.0 + 270.0 * i / (n - 1), r, box), (0, i * box))
        strip.save(os.path.join(OUT, "knob_strip_%d.png" % box), optimize=True)


# ---- toggles ---------------------------------------------------------------------------------------------------------
def pill(w, h, label, state):
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bw = 3 * SS
    box = [0, 0, w * SS - 1, h * SS - 1]
    rad = (h * SS) / 2
    if state:
        d.rounded_rectangle(box, radius=rad, fill=rgba(MAROON))
        ink = WHITE
    else:
        d.rounded_rectangle(box, radius=rad, fill=rgba(PANEL), outline=rgba(MAROON), width=bw)
        ink = MAROON
    f = font(TOGGLE_PX * SS)
    cap = f.getbbox("H", anchor="ls")
    tx = w * SS / 2
    ty = h * SS / 2 + (cap[3] - cap[1]) / 2
    if state:     # a lit dot before the label: unmistakably "on"
        tw = f.getlength(label)
        tx += 9 * SS
        dr = 5 * SS
        dcx = tx - tw / 2 - 15 * SS
        d.ellipse([dcx - dr, h * SS / 2 - dr, dcx + dr, h * SS / 2 + dr], fill=rgba(AMBER))
    d.text((tx, ty), label, font=f, fill=rgba(ink), anchor="ms")
    return down(im, w, h)


def make_toggles():
    sizes = {}
    for key, (box, label) in TOGGLES.items():
        w, h = round(sx(box[2] - box[0])), round(sy(box[3] - box[1]))
        for state in (0, 1):
            pill(w, h, label, state).save(os.path.join(OUT, "%s_%s.png" % (key, "on" if state else "off")), optimize=True)
        sizes[key] = (w, h)
        if key in CLOSED:
            for state in (0, 1):
                pill(w, CLOSED_H, label, state).save(os.path.join(OUT, "%s_c_%s.png" % (key, "on" if state else "off")), optimize=True)
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))
    return sizes


# ---- background ---------------------------------------------------------------------------------------------------------------
def load(name):
    im = Image.open(os.path.join(IMGS, name)).convert("RGB")
    assert im.size == (MOCK_W, MOCK_H), "%s must be 1920x1080" % name
    return im


def to_screen(im):
    scaled = im.resize((round(MOCK_W * SX), H), Image.Resampling.LANCZOS)
    page = Image.new("RGB", (W, H), MAROON)
    page.paste(scaled, (0, 0))
    if scaled.width < W:
        edge = scaled.crop((scaled.width - 1, 0, scaled.width, H)).resize((W - scaled.width, H))   # carry the last column on
        page.paste(edge, (scaled.width, 0))
    return page


def ctext(d, cx, baseline, text, f, fill):
    d.text((cx, baseline), text, font=f, fill=fill, anchor="ms")


RING_OUT = (108, 247, 1812, 971)     # outer edge of the panel outline in the mockup (10 px stroke, radius ~50)


def make_background():
    """Native re-layout (2026-11-09): the maroon / pastel blobs are stretched (shapeless), the title and underline are lifted from the
    mockup at their natural aspect, the panel outline is redrawn with a constant stroke, and every label is drawn at screen size."""
    clean = load("BG.jpg")
    arr = np.asarray(clean)
    arr = arr.copy()
    for bx in ((96, 96, 596, 207), HEADER_ERASE):      # the title and the placeholder name sit on flat maroon
        arr[bx[1]:bx[3], bx[0]:bx[2]] = MAROON
    erase = [(96, 207, 1500, 222), (100, 236, 1822, 982)]
    base = Image.fromarray(pn.nearest_fill(arr, pn.box_mask(arr.shape, erase)))
    bg = to_screen(base)
    x0, y0, x1, y1 = RING_OUT
    pn.panel_fill(bg, (round(sx(x0)), round(y0 * S), round(sx(x1)), round(y1 * S)), 50 * S, PANEL)
    pn.ring(bg, (sx(x0 + 5), (y0 + 4.5) * S, sx(x1 - 5), (y1 - 4.5) * S), 5, 45 * S, BLACK)
    pn.place(bg, pn.lift(clean, base, [(96, 96, 596, 207)]), sx, S, anchor=lambda it: "l")
    pn.place(bg, pn.lift(clean, base, [(96, 207, 1500, 222)], line_ratio=8), sx, S)
    d = ImageDraw.Draw(bg)
    f = font(LABEL_PX * S, LABEL_W)
    for text, cx, base_y in LABELS:
        ctext(d, sx(cx), sy(base_y), text, f, MAROON)
    f = font(TITLE_PX * S, TITLE_W)
    for text, cx, base_y in TITLES:
        ctext(d, sx(cx), sy(base_y), text, f, MAROON)
    d.rectangle([round(sx(BAR[0])), round(sy(BAR[1])), round(sx(BAR[2])) - 1, round(sy(BAR[3])) - 1], fill=MAROON)
    bg.save(os.path.join(OUT, "bg.png"), optimize=True)


# ---- arrows -----------------------------------------------------------------------------------------------------------------
def make_arrows():
    """The picture is the whole touch box (the triangle plus a transparent margin): MPC stretches a button's image to its box."""
    res = {}
    PADX, PADY = 20, 14
    for side, b in zip(("prev", "next"), SEL_ARROWS):
        w, h = round(sx(b[2] - b[0])), round(sy(b[3] - b[1]))
        bw, bh = w + PADX, h + PADY
        for suffix, c in (("", WHITE), ("_on", GREY)):
            im = Image.new("RGBA", (bw * SS, bh * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            x0, y0, x1, y1 = PADX // 2 * SS, PADY // 2 * SS, (PADX // 2 + w) * SS, (PADY // 2 + h) * SS
            if side == "prev":
                d.polygon([(x1, y0), (x1, y1), (x0, (y0 + y1) / 2)], fill=rgba(c))
            else:
                d.polygon([(x0, y0), (x0, y1), (x1, (y0 + y1) / 2)], fill=rgba(c))
            down(im, bw, bh).save(os.path.join(OUT, "arrow_sel_%s%s.png" % (side, suffix)))
        res[side] = (round(sx((b[0] + b[2]) / 2)), round(sy(SEL_CY)), bw, bh)
    return res


# ---- preset selector + browser -------------------------------------------------------------------------------------------------
PRESET_NAMES = [d["name"] for d in factory.PRESETS]
SEL_X0, SEL_X1 = round(sx(SEL_ARROWS[0][2] + 12)), round(sx(SEL_ARROWS[1][0] - 12))
SEL = (SEL_X0, round(sy(SEL_TEXT_CY)) - 20, SEL_X1 - SEL_X0, 40)
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


def ctext_box(d, box, text, f, fill):
    x0, y0, x1, y1 = box
    cap = f.getbbox("H", anchor="ls")
    d.text(((x0 + x1) / 2, (y0 + y1) / 2 + (cap[3] - cap[1]) / 2), text, font=f, fill=fill, anchor="ms")


def make_selector():
    x, y, w, h = SEL
    files = []
    for i, nm in enumerate(PRESET_NAMES):
        im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        ctext_box(d, (0, 0, w * SS, h * SS), nm, fit(nm, w - 6, 30), rgba(WHITE))
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
                            outline=rgba(MAROON), width=b)
        for i, nm in enumerate(cats):          # category column
            y0 = 14 + i * (PILL_H + PILL_GAP)
            on = i == c
            box = [16 * SS, y0 * SS, (16 + SIDE_W) * SS, (y0 + PILL_H) * SS]
            d.rounded_rectangle(box, radius=PILL_H * SS / 2, fill=rgba(AMBER if on else WHITE), outline=rgba(MAROON), width=2 * SS)
            ctext_box(d, box, nm, fit(nm, SIDE_W - 20, 20, 800 if on else 650), rgba(MAROON))
        f = font(27 * SS, 800)                 # header: category, page n / m, arrows
        d.text((GRID_X * SS, 30 * SS), cats[c], font=f, fill=rgba(MAROON), anchor="ls")
        for (ax, ay, aw, ah), left in ((BPAGE_PREV, True), (BPAGE_NEXT, False)):
            if pages[c] > 1:
                pts = [((ax + aw) * SS, ay * SS), ((ax + aw) * SS, (ay + ah) * SS), (ax * SS, (ay + ah / 2) * SS)] if left else \
                      [(ax * SS, ay * SS), (ax * SS, (ay + ah) * SS), ((ax + aw) * SS, (ay + ah / 2) * SS)]
                d.polygon(pts, fill=rgba(MAROON))
        tx0, ty0, tw, th = BPAGE_TEXT
        ctext_box(d, (tx0 * SS, ty0 * SS, (tx0 + tw) * SS, (ty0 + th) * SS), "%d / %d" % (pg + 1, pages[c]), font(21 * SS, 700), rgba(MAROON))
        for slot in range(12):                 # the page's presets
            k = pg * 12 + slot
            if k >= len(members[c]):
                break
            cx0, cy0 = cell_pos(slot)
            box = [cx0 * SS, cy0 * SS, (cx0 + CELL_W) * SS, (cy0 + CELL_H) * SS]
            d.rounded_rectangle(box, radius=16 * SS, fill=rgba(WHITE), outline=rgba(MAROON), width=3 * SS)
            nm = factory.PRESETS[members[c][k]]["name"]
            ctext_box(d, box, nm, fit(nm, CELL_W - 22, 23, 700), rgba(MAROON))
        fn = "browser_%d.png" % v
        down(im, pw, ph).save(os.path.join(OUT, fn), optimize=True)
        files.append(fn)
    # highlight of the current preset's cell: a ring and an amber tint, over the cell
    im = Image.new("RGBA", (CELL_W * SS, CELL_H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, CELL_W * SS - 1, CELL_H * SS - 1], radius=16 * SS, fill=rgba(AMBER, 120), outline=rgba(MAROON), width=6 * SS)
    down(im, CELL_W, CELL_H).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- value text must fit inside its knob -------------------------------------------------------------------------------------
WORST = {   # the longest read-outs each knob can show (engine.cpp display())
    "time": ["1.25 s", "999 ms", "1/16T", "1/16.", "0.5 ms"], "offset_amt": ["+100 %", "-100 %"], "feedback": ["100 %"],
    "hp": ["1.9 kHz", "Off", "999 Hz"], "lp": ["20 kHz", "9.9 kHz", "999 Hz"], "mix": ["100 %"], "rate": ["0.54 Hz", "10.0 Hz"],
    "depth": ["100 %"], "drive": ["100 %"], "tone": ["+100 %", "-100 %"], "spread": ["200 %"],
}


def check_text_fits():
    items = [(k, v[2], v[3], v[4]) for k, v in KNOBS.items()]
    items.append(("offset_amt", OFFSET_KNOB["r"], OFFSET_KNOB["vs"], OFFSET_KNOB["dy"]))
    for key, r, px, dyf in items:
        f = font(px)
        dy = round(dyf * r)
        th = 0.72 * px
        chord = 2 * math.sqrt(max(0.0, r * r - (abs(dy) + th / 2) ** 2))
        for s in WORST[key]:
            w = f.getlength(s)
            assert w <= chord - 6, "%s: '%s' is %.0f px, the knob has %.0f px at that height" % (key, s, w, chord)


# ---- layout.conf -------------------------------------------------------------------------------------------------------------
def write_layout(tog, arrows, sel_files, browser_files):
    L = ["# Generated by generate_skin.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=5A3232", "theme_ink=F0F0F2", "theme_ink_dim=FFFFFF", "theme_accent=FFC45C", "",
         "[tab POLI-DELAY]", "art file=skin_assets/bg.png fit=stretch", ""]

    def knob_line(key, cx, cy, r, vp, dyf, when=None):
        dy = round(dyf * r)
        vy = dy - max(26, round(vp * 1.2)) // 2 - r - 2
        s = 'knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF' % (cx, cy + Y_OFF, r, key, vp, vy, 2 * r + 2)
        return s + (" when=%s" % when if when else "")

    L.append("# knobs: value inside (the engine's text); the labels are in the background")
    for key, (cx, cy, r, vp, dyf, when) in KNOBS.items():
        L.append(knob_line(key, round(sx(cx)), round(sy(cy)), r, vp, dyf, when))
    o = OFFSET_KNOB
    L += ["", "# the Offset knob: only while the Offset toggle is on (closed-up pills above it)",
          knob_line("offset_amt", o["cx"], o["cy"], o["r"], o["vs"], o["dy"], "offset:On")]
    L += ["", "# toggles: outlined = off, filled maroon with a lit dot = on. Sync and Offset have two places (Offset off / on)"]
    for key, (box, label) in TOGGLES.items():
        w, h = tog[key]
        cx, cy = round(sx((box[0] + box[2]) / 2)), round(sy((box[1] + box[3]) / 2))
        if key in CLOSED:
            L.append('toggle cx=%d cy=%d label="" key=%s ns=0 w=%d h=%d img=skin_assets/%s_off.png img_on=skin_assets/%s_on.png when=offset:Off'
                     % (cx, cy + Y_OFF, key, w, h, key, key))
            ccx, ccy = CLOSED[key]
            L.append('toggle cx=%d cy=%d label="" key=%s ns=0 w=%d h=%d img=skin_assets/%s_c_off.png img_on=skin_assets/%s_c_on.png when=offset:On'
                     % (ccx, ccy + Y_OFF, key, w, CLOSED_H, key, key))
        else:
            L.append('toggle cx=%d cy=%d label="" key=%s ns=0 w=%d h=%d img=skin_assets/%s_off.png img_on=skin_assets/%s_on.png'
                     % (cx, cy + Y_OFF, key, w, h, key, key))
    # preset selector
    sx_, sy_, sw, sh = SEL
    L += ["", "# preset selector: name between the arrows; a tap on the name opens the browser",
          'picture x=%d y=%d w=%d h=%d key=preset files="%s" fit=stretch' % (sx_, sy_ + Y_OFF, sw, sh, ",".join(sel_files)),
          'button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/hit.png img_on=skin_assets/hit.png' % (sx_ + sw // 2, sy_ + sh // 2 + Y_OFF, sw, sh + 10)]
    for side in ("prev", "next"):
        cx, cy, w, h = arrows[side]
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_%s img=skin_assets/arrow_sel_%s.png img_on=skin_assets/arrow_sel_%s_on.png'
                 % (cx, cy + Y_OFF, w, h, side, side, side))
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
    L += ["", "# Q-Links, 4 to a Live III page: 1 Time / Offset / Feedback / Mix, 2 High Pass / Low Pass / Drive / Tone,",
          "# 3 Rate / Depth / Spread / Ping-Pong, 4 Sync / Offset on-off",
          'qlinks "POLI-DELAY" = time,offset_amt,feedback,mix,hp,lp,drive,tone,rate,depth,spread,pingpong,sync,offset,-,-', ""]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    check_text_fits()
    factory.main()
    make_params.main()
    make_knobs()
    tog = make_toggles()
    make_background()
    arrows = make_arrows()
    sel_files = make_selector()
    browser_files = make_browser()
    write_layout(tog, arrows, sel_files, browser_files)
    print("knob radii %s; %d browser views; %d presets" % (radii(), len(browser_files), len(sel_files)))
    print("wrote vst/skin_assets/*.png and vst/layout.conf")
