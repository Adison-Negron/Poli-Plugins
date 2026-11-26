#!/usr/bin/env python3
"""Poli-Multiband skin assets: turns the Canva mockup (imgs/) into the MPC skin's images AND writes vst/layout.conf,
so every coordinate lives in this one file.

    python3 generate_skin.py            (run from the Poli-Multiband folder; needs Pillow)

Writes (all inside vst/, which is what the skin builder reads):
    skin_assets/bg.png               1280x628 page: the mockup with its controls taken out, the Thresh labels added,
                                     Upwards Comp as a 2x2 grid, and the meter frame with an empty well
    skin_assets/knob_strip.png       black knob filmstrip (KNOB_FRAMES, min at the top; odd so one frame points up)
    skin_assets/freq_locked.png      Band 2's Split slot: a knob without a pointer (its range comes from Bands 1 and 3)
    skin_assets/label_*.png          the Split slot's label per band: "Up To" (Band 1), "Range" (Band 2), "From" (Band 3)
    skin_assets/tabs_<n>.png         the BAND 1/2/3 tabs with band n grown (one picture per band)
    skin_assets/hit.png              transparent: the touch areas over the tabs
    skin_assets/pass_off.png         Pass button, plugin running (green)
    skin_assets/pass_on.png          Pass button, plugin bypassed (red)
    skin_assets/meter_level.png      METER_FRAMES: green band level, filling the well from the bottom
    skin_assets/meter_in.png         METER_FRAMES: thin grey line = the band's level before compression
    skin_assets/meter_comp.png       METER_FRAMES: white bar = Comp Thresh (the white dot's section)
    skin_assets/meter_up.png         METER_FRAMES: brown bar = Upwards Comp Thresh (the brown dot's section)
    layout.conf

Geometry rules (mpc-vst-plugins tools/shadow_skin.py, MPC_PLUGIN_TOOLCHAIN.md):
  * plugin area 1280x628; layout.conf y = screen y + 86 (Y_OFF); the 1920x1080 mockup is scaled by 628/1080, left-aligned
  * a knob is drawn into a (2r+8) box centred in a (2r+10) frame; MPC limits: knob strips <= 16384 px (skin_post.py swaps
    in knob_strip.png), meter strips <= 12288 px (frames are the meter's own w x h)
  * images, strips and art need "art": "html" in vst.json
"""
import math
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared"))
import poli_preset_ui as ppu  # noqa: E402  (the preset selector shared by the Poli plugins)
import poli_native as pn  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
IMGS = os.path.join(HERE, "imgs")
FONTS = os.path.join(HERE, "fonts")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")

# ---- screen ---------------------------------------------------------------------------------------------
W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H
# Horizontal-only stretch (2026-11-09): the mockup's design ends at x=1750 of 1920, i.e. ~1018 px of the 1280 px plugin
# area, which left the right side empty. KX widens x positions AND the baked art so the design ends MARGIN px from the right
# edge (the left margin is ~63 px). Knob radii, value text and every size drawn in code stay as they were.
RIGHT_EDGE, MARGIN = 1750, 90
KX = 1.17      # stretched less than the others: its Pass square and meter would look oval
SX = S * KX   # was uniform: the mockup ended at x=1116
SS = 4                            # supersampling for smooth edges

PAGE = (221, 230, 229)            # Canva colours, measured on the mockup
BLOB = (126, 45, 44)
KNOB = (4, 0, 0)
WHITE = (255, 255, 255)
GREEN = (0, 191, 98)
RED = (214, 40, 40)
WELL = (203, 210, 218)
LINE_IN = (101, 108, 116)
BAR_COMP = (250, 255, 246)
BAR_UP = BLOB

OPEN_SANS = os.path.join(FONTS, "open-sans-latin-400-normal.woff")       # the mockup's labels: Open Sans 28 px
TITILLIUM = os.path.join(FONTS, "TitilliumWeb-SemiBold.ttf")             # what MPC draws live value text in

# ---- mockup geometry (measured on imgs/Poli-Multiband.jpg, 1920x1080) ------------------------------------
# where the drawn knobs are (taken out of the background), (cx, cy, r)
MOCK_KNOBS = [(288, 583, 85), (466, 583, 85), (644, 583, 85), (821.5, 583, 85),
              (292, 836.5, 85), (475.5, 837, 85), (654, 834, 85),
              (1467, 572.5, 89), (1292.5, 706.5, 85), (1467, 883.5, 89)]
# the live knobs: key -> mockup centre. The mockup's grid, with the knobs under the two dots made Thresh (each dot is
# that Thresh's line in the meter: white = Comp, brown = Upwards Comp), and the crossover ("Split") in the 8th Comp slot:
#   Comp:          Input | Output | Thresh (white dot) | Attack
#                  Release | Knee | Ratio | Split (Band 1: "Up To" = X1, Band 3: "From" = X2, Band 2: its range, fixed)
#   Upwards Comp:  Thresh (brown dot), Ratio  (the mockup's Upward Freq and Output knobs were redundant: removed)
KNOBS = {
    "c_input": (288, 583), "c_output": (466, 583), "c_thresh": (644, 583), "c_attack": (821.5, 583),
    "c_release": (292, 836.5), "c_knee": (475.5, 837), "c_ratio": (654, 834), "x_freq": (834, 836),
    "u_thresh": (1467, 572.5), "u_ratio": (1292.5, 706.5),
}
RELABEL = [((599, 428, 682, 476), "Thresh"), ((1420, 411, 1506, 461), "Thresh")]   # the two "Freq" pills under the dots
ERASE = [(1398, 716, 1536, 778)]           # the Upward "Output" pill (its knob goes with MOCK_KNOBS)
SPLIT_PILL = ((834, 714), 50)              # the 8th Comp slot's label: centre, height (Release style)
SPLIT_LABELS = ["Up To", "Range", "From"]  # per band
HINTS = [((206, 398), "turns loud parts down"), ((1008, 398), "brings quiet parts up")]   # under the section titles
PASS_BOX = (1590, 47, 1731, 189)           # the Pass button with a little of the blob around it
METER_BOX = (1578, 262, 1758, 1010)        # the meter, frame included
TABS = [283, 555, 835]                     # BAND 1/2/3 centres
TAB_Y_OFF, TAB_Y_ON = 1015, 985            # tab tops: normal / selected (it grows up and sideways)
TAB_W_OFF, TAB_W_ON = 158, 178
TAB_TXT_OFF, TAB_TXT_ON = 28, 31           # font px
TAB_BOX = (130, 955, 1010, 1080)           # the region the tab pictures cover
METER_TIGHT = (1578, 262, 1751, 1004)      # the meter frame itself (no margin): placed at its natural size
PASS_TIGHT = (1595, 53, 1726, 183)         # the Pass square itself
BLOCK_BOX = (100, 960, 1014, 1080)         # the dashed Band panel (+ its tabs): natural size, left edge under the title
BLOCK_X0 = 108
TITLE_BOX = (100, 120, 1010, 243)
RULE_BOX = (100, 243, 1760, 268)
BODY_BOX = (60, 270, 1570, 955)

# ---- live widgets -----------------------------------------------------------------------------------------
R = 52                       # knob radius on screen (85-89 in the mockup x 0.58)
BOX = 2 * R + 8              # knob image box (shadow_skin: 2r+8)
KNOB_FRAMES = 109            # 109 x 112 px = 12208 px: under MPC's limits; odd = one frame points straight up
POINTER_W = 15.0 / 85        # pointer width / radius
POINTER_IN = 0.26            # pointer runs from the rim this far in: its tip stays outside the value text's corners
VALUE_PX = 18                # value text size (MPC draws it in Titillium SemiBold)
VALUE_DY = 8                 # value text centre below the knob centre (the mockup's "0 db" sits in the lower half)
METER_FRAMES = 30            # engine.cpp kMeterSteps must match; 30 x 409 px <= 12288


def sx(x):
    return x * SX


def sy(y):
    return y * S


def bx(x):
    """the Band panel block keeps its natural size: left edge at the title's, everything in it scaled by S only"""
    return sx(BLOCK_X0) + (x - BLOCK_X0) * S


def font(path, px):
    return ImageFont.truetype(path, px)


# ---- knobs ------------------------------------------------------------------------------------------------
def knob_image(angle_deg, size=BOX, r=R, pointer=True):
    """One knob position: black disc, white pointer from the rim inwards. angle 0 = pointing up."""
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS / 2
    rr = r * SS
    d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=KNOB + (255,))
    if not pointer:
        return big.resize((size, size), Image.Resampling.LANCZOS)
    a = math.radians(angle_deg)
    ux, uy = math.sin(a), -math.cos(a)
    px, py = -uy, ux
    hw = POINTER_W * rr / 2
    r_out, r_in = rr - 0.5 * SS, rr * (1 - POINTER_IN)
    d.polygon([(c + ux * r_out + px * hw, c + uy * r_out + py * hw),
               (c + ux * r_out - px * hw, c + uy * r_out - py * hw),
               (c + ux * r_in - px * hw, c + uy * r_in - py * hw),
               (c + ux * r_in + px * hw, c + uy * r_in + py * hw)], fill=WHITE + (255,))
    return big.resize((size, size), Image.Resampling.LANCZOS)


def make_knobs():
    strip = Image.new("RGBA", (BOX, BOX * KNOB_FRAMES), (0, 0, 0, 0))
    for i in range(KNOB_FRAMES):
        strip.paste(knob_image(-135.0 + 270.0 * i / (KNOB_FRAMES - 1)), (0, i * BOX))
    strip.save(os.path.join(OUT, "knob_strip.png"), optimize=True)
    # Band 2's Split: a knob without a pointer (not adjustable: its edges are Band 1's and Band 3's Split); the range
    # itself is live text on top (readout range_text)
    knob_image(0, pointer=False).save(os.path.join(OUT, "freq_locked.png"), optimize=True)
    # its label per band, the mockup's pill style
    for i, text in enumerate(SPLIT_LABELS):
        f = font(OPEN_SANS, 28 * 2)
        tw = ImageDraw.Draw(Image.new("L", (1, 1))).textlength(text, font=f)
        w, h = round(tw + 56), SPLIT_PILL[1] * 2
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([0, 0, w - 1, h - 1], radius=20, fill=(0, 0, 0, 255))
        d.text((w / 2, h / 2), text, font=f, fill=WHITE + (255,), anchor="mm")
        im.resize((round(S * w / 2), round(S * h / 2)), Image.Resampling.LANCZOS).save(
            os.path.join(OUT, "label_%d.png" % i), optimize=True)


# ---- background -------------------------------------------------------------------------------------------
def pill(d, cx, cy, h, text, px=28):
    f = font(OPEN_SANS, px)
    tw = d.textlength(text, font=f)
    w = tw + 28
    d.rounded_rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2], radius=10, fill=(0, 0, 0))
    d.text((cx, cy), text, font=f, fill=WHITE, anchor="mm")


def well_mask(mock):
    """The meter's inside (everything in the frame that isn't the black frame), at mockup resolution."""
    x0, y0, x1, y1 = METER_BOX
    reg = mock.crop(METER_BOX).convert("RGB")
    m = Image.new("L", reg.size, 0)
    px, mp = reg.load(), m.load()
    w, h = reg.size
    # flood from the middle of the well: not-black pixels connected to it
    from collections import deque
    seen = set()
    q = deque([(w // 2, h // 2)])
    while q:
        x, y = q.popleft()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        if max(px[x, y]) < 60:   # the black frame (dark green / grey edge pixels belong to the well)
            continue
        mp[x, y] = 255
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return m


def alpha_outside_black(crop_rgb):
    """alpha for a rounded black frame cut from the mockup: everything outside the frame (not dark, connected to the crop's
    border) is transparent, the frame and its inside are opaque"""
    arr = np.asarray(crop_rgb).astype(int)
    dark = arr.sum(axis=2) < 120
    lab, n = pn.label(~dark)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    outside = np.isin(lab, list(border))
    a = Image.fromarray(((~outside) * 255).astype(np.uint8))
    return a


def make_background():
    """Native re-layout (2026-11-09): colour blobs are stretched (shapeless); the title, the rule, every label / pill / hint is a sprite
    lifted off the mockup at its natural aspect and centred on its stretched x; the Pass square, the meter and the dashed Band
    panel (with its tabs) keep their natural size."""
    mock = Image.open(os.path.join(IMGS, "Poli-Multiband.jpg")).convert("RGB")
    clean = Image.open(os.path.join(IMGS, "Poli-Multiband-bg.jpg")).convert("RGB")
    assert mock.size == (MOCK_W, MOCK_H) and clean.size == (MOCK_W, MOCK_H), "mockups must be 1920x1080"
    well = well_mask(mock)
    wx0, wy0, wx1, wy1 = well.getbbox()
    meter_art = mock.crop(METER_BOX)
    meter_art.paste(WELL, (0, 0), well)          # empty well: the live meter draws level and lines on it

    # take every drawn control out (they become live widgets), at full resolution
    mask = Image.new("L", (MOCK_W * 2, MOCK_H * 2), 0)
    dm = ImageDraw.Draw(mask)
    for cx, cy, r in MOCK_KNOBS:
        rr = (r + 7) * 2
        dm.ellipse([cx * 2 - rr, cy * 2 - rr, cx * 2 + rr, cy * 2 + rr], fill=255)
    for box in [PASS_BOX, METER_BOX] + ERASE + [b for b, _ in RELABEL]:
        dm.rectangle([(box[0] - 4) * 2, (box[1] - 4) * 2, (box[2] + 4) * 2, (box[3] + 4) * 2], fill=255)
    for cx in TABS:   # the tabs: the clean background has the dashed panel without them
        dm.rectangle([(cx - TAB_W_ON / 2 - 6) * 2, (TAB_Y_ON - 6) * 2, (cx + TAB_W_ON / 2 + 6) * 2, MOCK_H * 2], fill=255)
    mask = mask.resize((MOCK_W, MOCK_H), Image.Resampling.LANCZOS)
    mock.paste(clean, (0, 0), mask)

    d = ImageDraw.Draw(mock)
    for (x0, y0, x1, y1), text in RELABEL:
        pill(d, (x0 + x1) / 2, (y0 + y1) / 2, y1 - y0, text)
    for (x, y), text in HINTS:   # what each section does, in plain words
        d.text((x, y), text, font=font(OPEN_SANS, 26), fill=(0, 0, 0), anchor="lm")

    # the shapeless layer: the clean frame without its title, rule, Band panel, Pass square and meter
    arr = np.asarray(clean).copy()
    tb = TITLE_BOX
    arr[tb[1]:tb[3], tb[0]:tb[2]] = PAGE
    erase = [RULE_BOX, BLOCK_BOX, PASS_BOX, METER_BOX]
    base = Image.fromarray(pn.nearest_fill(arr, pn.box_mask(arr.shape, erase)))
    page = to_screen_blobs(base)
    pn.place(page, pn.lift(mock, base, [TITLE_BOX], open_px=0), sx, S, anchor=lambda it: "l")
    pn.place(page, pn.lift(mock, base, [RULE_BOX], line_ratio=8), sx, S)
    pn.place(page, pn.lift(mock, base, [BODY_BOX]), sx, S)

    # the dashed Band panel: its pixels (grey fill + black dashes) as a sprite, natural size, left edge at the title's
    bb = BLOCK_BOX
    carr = np.asarray(clean.crop(bb)).astype(int)
    isbg = (np.abs(carr - np.array(PAGE)).sum(axis=2) < 40) | (np.abs(carr - np.array(BLOB)).sum(axis=2) < 40)
    alpha = Image.fromarray(((~isbg) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(0.6))
    spr = clean.crop(bb).convert("RGBA")
    spr.putalpha(alpha)
    spr = spr.resize((round((bb[2] - bb[0]) * S), round((bb[3] - bb[1]) * S)), Image.Resampling.LANCZOS)
    page.paste(spr, (round(bx(bb[0])), round(bb[1] * S)), spr)

    # the meter frame, natural size, centred on its stretched x
    mt = METER_TIGHT
    mcrop = meter_art.crop((mt[0] - METER_BOX[0], mt[1] - METER_BOX[1], mt[2] - METER_BOX[0], mt[3] - METER_BOX[1])).convert("RGBA")
    mcrop.putalpha(alpha_outside_black(mcrop.convert("RGB")))
    msize = (round((mt[2] - mt[0]) * S), round((mt[3] - mt[1]) * S))
    mx0 = round(sx((mt[0] + mt[2]) / 2) - msize[0] / 2)
    my0 = round(mt[1] * S)
    mcrop = mcrop.resize(msize, Image.Resampling.LANCZOS)
    page.paste(mcrop, (mx0, my0), mcrop)
    page.save(os.path.join(OUT, "bg.png"), optimize=True)
    # the well in screen px (whole pixels, inside the frame) + its mask for the meter strips
    ox, oy = METER_BOX[0] - mt[0], METER_BOX[1] - mt[1]
    gx0, gy0 = math.floor(mx0 + (wx0 + ox) * S), math.ceil(my0 + (wy0 + oy) * S)
    gx1, gy1 = math.ceil(mx0 + (wx1 + ox) * S), math.floor(my0 + (wy1 + oy) * S)
    wm = well.crop((wx0, wy0, wx1, wy1)).resize((gx1 - gx0, gy1 - gy0), Image.Resampling.LANCZOS)
    wm = wm.point(lambda v: 255 if v > 100 else 0)
    return (gx0, gy0, gx1, gy1), wm


def to_screen_blobs(base):
    scaled = base.resize((round(MOCK_W * SX), H), Image.Resampling.LANCZOS)
    page = Image.new("RGB", (W, H), PAGE)
    page.paste(scaled, (0, 0))
    # right of the mapped mockup: carry its last column on, so the top-right blob runs off the edge instead of stopping in a
    # straight line (MPC's Q-Link sidebar covers this strip while a Q-Link is held anyway)
    if scaled.width < W:
        edge = scaled.crop((scaled.width - 1, 0, scaled.width, H)).resize((W - scaled.width, H))
        page.paste(edge, (scaled.width, 0))
    return page


# ---- meter strips (frames of the well's own size; frame 0 = minimum) ------------------------------------------------
def make_meters(rect, wm):
    x0, y0, x1, y1 = rect
    mw, mh = x1 - x0, y1 - y0
    assert mh * METER_FRAMES <= 12288, "meter strip too tall for MPC"
    n = METER_FRAMES

    def strip(draw_frame, name):
        im = Image.new("RGBA", (mw, mh * n), (0, 0, 0, 0))
        for i in range(n):
            fr = Image.new("RGBA", (mw, mh), (0, 0, 0, 0))
            draw_frame(ImageDraw.Draw(fr), i)
            alpha = Image.composite(fr.getchannel("A"), Image.new("L", (mw, mh), 0), wm)   # keep inside the well
            fr.putalpha(alpha)
            im.paste(fr, (0, i * mh))
        im.save(os.path.join(OUT, name), optimize=True)

    def top_of(i):   # y of value i (0 = bottom of the well, n-1 = top)
        return mh - mh * i / (n - 1)

    def level(d, i):
        if i:
            d.rectangle([0, round(top_of(i)), mw, mh], fill=GREEN + (255,))

    def bar(colour, th, x_from, skip_zero=False):
        def f(d, i):
            if skip_zero and i == 0:
                return
            c = min(mh - th / 2, max(th / 2, top_of(i)))
            d.rectangle([round(mw * x_from), round(c - th / 2), mw, round(c + th / 2) - 1], fill=colour + (255,))
        return f

    strip(level, "meter_level.png")
    strip(bar(LINE_IN, 3, 0.36, skip_zero=True), "meter_in.png")
    strip(bar(BAR_COMP, 13, 0.44), "meter_comp.png")
    strip(bar(BAR_UP, 13, 0.44), "meter_up.png")
    return mw, mh


# ---- pass button, tabs ------------------------------------------------------------------------------------------
def make_pass():
    mock = Image.open(os.path.join(IMGS, "Poli-Multiband.jpg")).convert("RGB")
    im = mock.crop(PASS_TIGHT)
    size = (round((PASS_TIGHT[2] - PASS_TIGHT[0]) * S), round((PASS_TIGHT[3] - PASS_TIGHT[1]) * S))      # natural size: a square stays square
    im.resize(size, Image.Resampling.LANCZOS).save(os.path.join(OUT, "pass_off.png"))
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = px[x, y]
            if g > r + 40 and g > b:          # the green fill (and its anti-aliased edges) -> red, same shading
                t = min(1.0, (g - r) / 191.0)
                px[x, y] = tuple(round(c0 * (1 - t) + c1 * t) for c0, c1 in zip((r, r, r), RED))
    im.resize(size, Image.Resampling.LANCZOS).save(os.path.join(OUT, "pass_on.png"))
    return size


def make_tabs():
    bx0, by0, bx1, by1 = TAB_BOX
    k = 4                                    # supersampling
    size = (round((bx1 - bx0) * S), round((by1 - by0) * S))
    for sel in range(3):
        big = Image.new("RGBA", (size[0] * k, size[1] * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        for i, cx in enumerate(TABS):
            on = i == sel
            tw, top, px = (TAB_W_ON, TAB_Y_ON, TAB_TXT_ON) if on else (TAB_W_OFF, TAB_Y_OFF, TAB_TXT_OFF)
            x0, x1 = (cx - tw / 2 - bx0) * S * k, (cx + tw / 2 - bx0) * S * k
            d.rectangle([x0, (top - by0) * S * k, x1, (by1 - by0) * S * k], fill=(0, 0, 0, 255))
            ty = (top + 45 if not on else top + 62) - by0
            d.text(((cx - bx0) * S * k, ty * S * k), "BAND %d" % (i + 1), font=font(OPEN_SANS, round(px * S * k)), fill=WHITE + (255,), anchor="mm")
        big.resize(size, Image.Resampling.LANCZOS).save(os.path.join(OUT, "tabs_%d.png" % sel), optimize=True)
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))


# ---- layout.conf ------------------------------------------------------------------------------------------------
def write_layout(meter_rect, mw, mh, pass_size):
    def at(key):
        cx, cy = KNOBS[key]
        return round(sx(cx)), round(sy(cy)) + Y_OFF

    vy = (BOX + 2) // 2 + VALUE_DY - max(26, round(VALUE_PX * 1.2)) // 2 - ((BOX + 2) // 2 + R + 2)
    knob = ('knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF '
            'strip=skin_assets/knob_strip.png frames=%d')

    def k(key, when=None):
        cx, cy = at(key)
        return knob % (cx, cy, R, key, VALUE_PX, vy, BOX + 2, KNOB_FRAMES) + (" when=band:%d" % when if when is not None else "")

    lines = [
        "# Generated by generate_skin.py -- edit the numbers there, not here.",
        "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
        "theme_bg=DDE6E5",
        "theme_ink=000000",
        "theme_ink_dim=FFFFFF",
        "theme_accent=7E2D2C",
        "",
        "[tab MULTIBAND]",
        "art file=skin_assets/bg.png fit=stretch",
        "",
        "# band tabs: the selected one grows (one picture per band; MPC swaps them as \"band\" changes)",
        'picture x=%d y=%d w=%d h=%d key=band files="skin_assets/tabs_0.png,skin_assets/tabs_1.png,skin_assets/tabs_2.png" fit=stretch'
        % (round(bx(TAB_BOX[0])), round(sy(TAB_BOX[1])) + Y_OFF, round((TAB_BOX[2] - TAB_BOX[0]) * S),
           round(sy(TAB_BOX[3]) - sy(TAB_BOX[1]))),
        "",
    ]
    # the Split slot's label, per band (baked art, shown by band)
    lcx, lcy = SPLIT_PILL[0]
    for i in range(3):
        im = Image.open(os.path.join(OUT, "label_%d.png" % i))
        lines.append("art file=skin_assets/label_%d.png x=%d y=%d w=%d h=%d fit=stretch when=band:%d"
                     % (i, round(sx(lcx)) - im.width // 2, round(sy(lcy)) + Y_OFF - im.height // 2, im.width, im.height, i))
    cx, cy = at("x_freq")
    lines += [
        "# Band 2's Split: a knob that doesn't turn (its edges are Band 1's and Band 3's Split) with its range on it",
        "art file=skin_assets/freq_locked.png x=%d y=%d w=%d h=%d fit=stretch when=band:1" % (cx - BOX // 2, cy - BOX // 2, BOX, BOX),
        'readout cx=%d cy=%d w=%d h=%d label="" key=range_text box=0 tsize=15 tweight=semibold tcolor=FFFFFF tpad=0 when=band:1'
        % (cx, cy + VALUE_DY, BOX - 6, 26),
        "",
        "# Comp: labels are baked into bg.png; the value text sits inside the knob (ns=0, vs/vy)",
    ]
    for key in ("c_input", "c_output", "c_thresh", "c_attack", "c_release", "c_knee", "c_ratio"):
        lines.append(k(key))
    lines += ["# Split = the crossover: Band 1's top edge, Band 3's bottom edge", k("x_freq", 0), k("x_freq", 2),
              "", "# Upwards Comp", k("u_thresh"), k("u_ratio"), ""]
    lines.append("# band tabs: invisible touch areas (momentary sel1..3 -> the engine switches \"band\")")
    for i, cx in enumerate(TABS):
        lines.append('button cx=%d cy=%d w=%d h=%d label="" key=sel%d img=skin_assets/hit.png img_on=skin_assets/hit.png'
                     % (round(bx(cx)), round(sy(1030)) + Y_OFF, round((TAB_W_ON + 10) * S), round(sy(100)), i + 1))
    pcx, pcy = round(sx((PASS_TIGHT[0] + PASS_TIGHT[2]) / 2)), round(sy((PASS_TIGHT[1] + PASS_TIGHT[3]) / 2)) + Y_OFF
    lines += [
        "",
        "# Pass: green = running, red = bypassed",
        'toggle cx=%d cy=%d label="" key=bypass ns=0 w=%d h=%d img=skin_assets/pass_off.png img_on=skin_assets/pass_on.png'
        % (pcx, pcy, pass_size[0], pass_size[1]),
        "",
        "# meter of the band on screen, layered in this order: green level (after compression), grey line (level",
        "# before it), white bar (Comp Thresh), brown bar (Upwards Comp Thresh). Display only: the engine drives them.",
    ]
    x0, y0, x1, y1 = meter_rect
    mcx, mcy = x0 + mw // 2, y0 + mh // 2 + Y_OFF
    for key, img in (("m_out", "meter_level"), ("m_in", "meter_in"), ("m_ct", "meter_comp"), ("m_ut", "meter_up")):
        lines.append("meter cx=%d cy=%d w=%d h=%d key=%s strip=skin_assets/%s.png frames=%d"
                     % (mcx, mcy, mw, mh, key, img, METER_FRAMES))
    lines += preset_selector()
    lines += [
        "",
        "# Q-Links (4 per column): Comp row 1, Comp row 2, then Upwards Comp + band + pass",
        'qlinks "MULTIBAND" = c_input,c_output,c_thresh,c_attack,c_release,c_knee,c_ratio,x_freq,'
        'u_thresh,u_ratio,band,bypass',
        "",
    ]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(lines))


# ---- presets: a black pill right of the title (the design's label pills), the browser over the knobs ------------------
# The names come from vst/params.json's "preset" options (presets/make_presets.py, from factory.json); the browser
# groups them by their prefix, without it.
PRESET_SEL = (712, 82, 340, 48)          # screen x, y, w, h: right of "Poli-Multiband" (ends ~679), left of Pass (~1085)
PRESET_PANEL = (40, 162, 1018, 446)      # the browser: over both sections and the band tabs, left of the meter (~1074)
PRESET_STYLE = dict(ink=(0, 0, 0), weight=700, name_px=23, cell_px=19, title=None, panel_fill=(221, 230, 229),
                    border=6, radius=30, cell_fill=(255, 255, 255), cell_border=3, hl_fill=(0, 0, 0),
                    hl_ink=(255, 255, 255), sel_fill=(0, 0, 0), sel_radius=24, name_ink=(255, 255, 255),
                    arrow=(255, 255, 255), arrow_on=(150, 150, 150), head_fill=(126, 45, 44), head_ink=(255, 255, 255),
                    head_px=19)
# browser columns: (heading) or preset index; prefixes in factory.json: Vox, Drm, Bass, Mix, Mst, Fx
PRESET_COLUMNS = [[("h", "Start"), ("p", 0), ("h", "Vocals")] + [("p", i) for i in range(1, 6)],
                  [("h", "Vocals")] + [("p", i) for i in range(6, 13)],
                  [("h", "Drums")] + [("p", i) for i in range(13, 18)],
                  [("h", "Bass")] + [("p", i) for i in range(18, 21)] + [("h", "Mix"), ("p", 21)],
                  [("h", "Master"), ("p", 22), ("p", 23), ("h", "FX")] + [("p", i) for i in range(24, 27)]]


def preset_selector():
    names = next(p for p in json.load(open(os.path.join(VST, "params.json")))["params"] if p["key"] == "preset")["options"]
    shown = [n.split(": ", 1)[-1] for n in names]
    ppu.ensure_params(os.path.join(VST, "params.json"), names)
    return ppu.build(VST, names, ppu.Style(**PRESET_STYLE), sel=PRESET_SEL, panel=PRESET_PANEL,
                     columns=PRESET_COLUMNS, shown=shown, arrow_w=42, src_dir=os.path.join(HERE, "src"), cell_gap=8,
                     pad=20, title_h=0) + [""] + [""]


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    rect, wm = make_background()
    mw, mh = make_meters(rect, wm)
    make_knobs()
    ps = make_pass()
    make_tabs()
    write_layout(rect, mw, mh, ps)
    print("well %s (%dx%d), knobs r=%d, pass %s" % (rect, mw, mh, R, ps))
    print("wrote vst/skin_assets/*.png and vst/layout.conf")
