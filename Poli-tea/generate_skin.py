#!/usr/bin/env python3
"""Poli-tea skin: turns the Canva mockup (imgs/Poli-tea.jpg) into the MPC skin's images AND writes vst/layout.conf, so every
coordinate lives in this one file.

    python3 generate_skin.py            (from the Poli-tea folder; needs Pillow, numpy, scipy)

imgs/Poli-tea.jpg is the only artwork: the design with every control drawn. There is no clean background, so make_background()
rebuilds one: the mockup sits on three flat pastel shapes (blue sky, white cloud, mint cloud), so every knob, label, the bar and
the preset text are erased (the cloud edge that runs behind the Core knob is continued from its visible ends) and redrawn by this
script in Open Sans, fitted to the mockup's lettering. The boba cup is kept as drawn. The live controls are widgets:
knobs (black, with a long white pointer; the value sits under the knob), the Sidechain switch, six meters, the preset selector
and browser.

Geometry (MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628, layout.conf y = screen y + 86; the 1920x1080 mockup is scaled uniformly
by 628/1080 and left-aligned. Knobs are as big as the mockup's (r 56 / 120 / 48 on screen); value text 24-34 px. The first row
moved up 8 px and the Limiter section down 10 px so the value texts fit between them.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "shared"))
sys.path.insert(0, os.path.join(HERE, "presets"))
sys.path.insert(0, HERE)
import poli_preset_ui as ppu  # noqa: E402  (the Open Sans font shared by the Poli plugins)
import factory  # noqa: E402
import make_params  # noqa: E402

IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H
SS = 4

INK = (0, 0, 5)
PANEL = (249, 244, 248)
BLUE = (199, 217, 231)
MINT = (241, 244, 237)
AMBER = (255, 200, 105)
GREY = (150, 150, 160)          # pressed arrow
WHITE = (255, 255, 255)

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
    return x * S


def sy(y):
    return y * S


def down(im, w, h):
    return im.resize((w, h), Image.Resampling.LANCZOS)


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


# ---- screen geometry (px, 1280x628 plugin area; mockup positions x 0.5815, see the module docstring) ---------------------------
R_SM, R_CORE, R_OUT = 56, 120, 48
# key -> (cx, cy, r, value text px)
KNOBS = {
    "input": (119, 304, R_SM, 24), "drive": (241, 304, R_SM, 24),
    "low": (419, 304, R_SM, 24), "mid": (538, 304, R_SM, 24), "high": (660, 304, R_SM, 24),
    "amount": (908, 374, R_CORE, 34),
    "ldrive": (119, 537, R_SM, 24), "lthresh": (270, 537, R_SM, 24),
    "output": (780, 537, R_OUT, 24),
}
# static labels: (text, screen cx, baseline y, size class)
LABEL_PX, LABEL_W = 27, 700
TITLE_PX, TITLE_W = 41, 740
BIG_PX, BIG_W = 46, 760
LABELS = [("Input", 119, 238), ("Drive", 241, 238), ("Low", 419, 238), ("Mid", 538, 238), ("High", 660, 238),
          ("Level", 330, 238),
          ("Drive", 119, 468), ("Threshold", 270, 468), ("Output", 780, 480)]
TITLES = [("Front", 125, 192), ("Shape", 540, 192), ("Core", 908, 202), ("Limiter", 138, 428)]
TITLE_BIG = ("Poli-Tea", 64, 111)            # left-aligned at x, baseline y
BAR = (63, 119, 968, 138)                    # screen box of the black rule under the header

# meters
IN_METER = dict(w=16, h=130, cy=305, cx=(319, 341), frames=73)          # peak, RMS: -36..0 dBFS, 0.5 dB per frame
SWEET = (-21.0, -15.0)                       # the input's sweet spot, dBFS RMS (the compressors' thresholds are tuned for -18)
GR_METER = dict(x0=428, w=278, h=20, cy=(506, 538, 570, 602), frames=49)  # 0..-12 dB, 0.25 dB per frame
GR_LABELS = ["Low", "Mid", "High", "Limit"]
GR_SWEET = (1.5, 4.5)                        # per-band gain reduction target (dB) drawn as a zone
GR_TITLE = ("Gain Reduction", 527, 478)
SC_BOX = (527, 428, 250, 38)                 # centre x, centre y, w, h of the Sidechain switch

# header: the selector between two arrows
SEL_ARROWS = ((621, 80, 27, 32), (941, 80, 27, 32))      # screen boxes of the arrows (x, y, w, h)
SEL_CY = 96
SEL_X0, SEL_X1 = 655, 934
SEL = (SEL_X0, SEL_CY - 20, SEL_X1 - SEL_X0, 40)

# ---- widened layout (2026-11-09) -------------------------------------------------------------------------------------------------
# The first layout ended at ~x 1030 of 1280 (the cup at 1105). Every x POSITION below is spread out from the left margin; knob
# radii, text sizes and the cup's proportions are untouched. Body controls by BODY_K (the Core knob ends ~87 px from the right
# edge), the header (rule + preset selector) by HEAD_K so it still stops ~15 px short of the cup, which now sits (smaller) at the right edge.
X_LEFT, BODY_K, HEAD_K, CUP_RIGHT = 63, 1.195, 1.135, 1216
CUP_SCALE, CUP_TOP = 0.8, 12            # the cup is drawn smaller so it clears the Core title (now under it)


def bx(x):
    return int(round(X_LEFT + (x - X_LEFT) * BODY_K))


def hx(x):
    return int(round(X_LEFT + (x - X_LEFT) * HEAD_K))


KNOBS = {k: (bx(v[0]),) + tuple(v[1:]) for k, v in KNOBS.items()}
LABELS = [(t, bx(cx), b) for t, cx, b in LABELS]
TITLES = [(t, bx(cx), b) for t, cx, b in TITLES]
BAR = (BAR[0], BAR[1], hx(BAR[2]), BAR[3])
IN_METER["cx"] = tuple(bx(c) for c in IN_METER["cx"])
_gr_x1 = bx(GR_METER["x0"] + GR_METER["w"])
GR_METER["x0"] = bx(GR_METER["x0"])
GR_METER["w"] = _gr_x1 - GR_METER["x0"]                 # the gain-reduction bars grow with the spread
GR_TITLE = (GR_TITLE[0], bx(GR_TITLE[1]), GR_TITLE[2])
SC_BOX = (bx(SC_BOX[0]),) + tuple(SC_BOX[1:])
SEL_ARROWS = tuple((hx(a[0]),) + tuple(a[1:]) for a in SEL_ARROWS)
SEL_X0, SEL_X1 = hx(SEL_X0), hx(SEL_X1)
SEL = (SEL_X0, SEL_CY - 20, SEL_X1 - SEL_X0, 40)

# ---- knob images ------------------------------------------------------------------------------------------------------------
POINTER_W = 0.17       # of the radius (min 6 px)
POINTER_FROM, POINTER_TO = 0.95, 0.26     # outer / inner end of the pointer, of the radius: a long, high-contrast bar


def knob_image(angle, r, size):
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS / 2
    rr = r * SS
    d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=rgba(INK))
    a = math.radians(angle)
    ux, uy = math.sin(a), -math.cos(a)
    px, py = -uy, ux
    hw = max(POINTER_W * rr, 6.0 * SS) / 2
    r_out, r_in = rr * POINTER_FROM, rr * POINTER_TO
    d.polygon([(c + ux * r_out + px * hw, c + uy * r_out + py * hw), (c + ux * r_out - px * hw, c + uy * r_out - py * hw),
               (c + ux * r_in - px * hw, c + uy * r_in - py * hw), (c + ux * r_in + px * hw, c + uy * r_in + py * hw)],
              fill=rgba(WHITE))
    return down(big, size, size)


def strip_frames(r):
    """odd frame count whose strip stays under MPC's 12288 px (frame = 2r+10 in the builder; ours are 2r+8 boxes)"""
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


# ---- Sidechain switch -------------------------------------------------------------------------------------------------------
def pill(w, h, label, state):
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bw = 3 * SS
    box = [0, 0, w * SS - 1, h * SS - 1]
    rad = (h * SS) / 2
    if state:
        d.rounded_rectangle(box, radius=rad, fill=rgba(INK))
        ink = WHITE
    else:
        d.rounded_rectangle(box, radius=rad, fill=rgba(PANEL), outline=rgba(INK), width=bw)
        ink = INK
    f = font(24 * SS, 700)
    cap = f.getbbox("H", anchor="ls")
    tx = w * SS / 2
    ty = h * SS / 2 + (cap[3] - cap[1]) / 2
    if state:     # a lit dot before the label: unmistakably "on"
        tw = f.getlength(label)
        tx += 9 * SS
        dr = 6 * SS
        dcx = tx - tw / 2 - 16 * SS
        d.ellipse([dcx - dr, h * SS / 2 - dr, dcx + dr, h * SS / 2 + dr], fill=rgba(AMBER))
    d.text((tx, ty), label, font=f, fill=rgba(ink), anchor="ms")
    return down(im, w, h)


def make_toggles():
    _, _, w, h = SC_BOX
    pill(w, h, "Sidechain: Internal", 0).save(os.path.join(OUT, "sc_off.png"), optimize=True)
    pill(w, h, "Sidechain: External", 1).save(os.path.join(OUT, "sc_on.png"), optimize=True)
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))


# ---- meters -----------------------------------------------------------------------------------------------------------------
TRACK = (34, 40, 52)
ZONE = (96, 205, 130)
C_LOW = (112, 144, 186)      # below the sweet spot
C_OK = (70, 196, 112)        # in it
C_HOT = (244, 160, 52)       # above it
C_CLIP = (232, 72, 72)
C_PEAK = (205, 228, 255)


def db_to_y(db, h, lo=-36.0):
    return h - 2 - (h - 4) * (db - lo) / (0 - lo)


def make_input_meters():
    n, w, h = IN_METER["frames"], IN_METER["w"], IN_METER["h"]
    assert h * n <= 12288
    for name, rms in (("meter_in_peak.png", False), ("meter_in_rms.png", True)):
        strip = Image.new("RGBA", (w, h * n), (0, 0, 0, 0))
        for i in range(n):
            db = -36.0 + i * 0.5
            big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(big)
            d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=4 * SS, fill=rgba(TRACK))
            # the sweet-spot zone is part of the track, always visible
            z0, z1 = db_to_y(SWEET[1], h), db_to_y(SWEET[0], h)
            d.rectangle([1 * SS, z0 * SS, (w - 1) * SS, z1 * SS], fill=rgba(ZONE, 105))
            if i > 0:
                if rms:
                    col = C_LOW if db < SWEET[0] else C_OK if db <= SWEET[1] else C_HOT if db < -6 else C_CLIP
                else:
                    col = C_CLIP if db >= -0.5 else C_HOT if db >= -3.0 else C_PEAK
                top = db_to_y(db, h)
                inset = 3 if rms else 5
                d.rectangle([inset * SS, top * SS, (w - inset) * SS, (h - 2) * SS], fill=rgba(col))
            # tick marks every 6 dB on the track
            for t in (-30, -24, -18, -12, -6):
                y = db_to_y(t, h)
                d.line([(0, y * SS), (3 * SS, y * SS)], fill=rgba((235, 235, 240), 150), width=SS)
                d.line([((w - 3) * SS, y * SS), (w * SS, y * SS)], fill=rgba((235, 235, 240), 150), width=SS)
            strip.paste(down(big, w, h), (0, i * h))
        strip.save(os.path.join(OUT, name), optimize=True)


def make_gr_meters():
    n, w, h = GR_METER["frames"], GR_METER["w"], GR_METER["h"]
    for name, band in (("meter_gr_band.png", True), ("meter_gr_lim.png", False)):
        strip = Image.new("RGBA", (w, h * n), (0, 0, 0, 0))
        for i in range(n):
            db = i * 0.25
            big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(big)
            d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=5 * SS, fill=rgba(TRACK))
            if band:
                x0, x1 = 2 + (w - 4) * GR_SWEET[0] / 12.0, 2 + (w - 4) * GR_SWEET[1] / 12.0
                d.rectangle([x0 * SS, 1 * SS, x1 * SS, (h - 1) * SS], fill=rgba(ZONE, 105))
            if i > 0:
                col = (C_OK if GR_SWEET[0] <= db <= GR_SWEET[1] else C_LOW if db < GR_SWEET[0] else C_HOT) if band else \
                      (C_LOW if db < 3 else C_HOT if db < 8 else C_CLIP)
                x = 2 + (w - 4) * db / 12.0
                d.rounded_rectangle([2 * SS, 3 * SS, x * SS, (h - 3) * SS], radius=3 * SS, fill=rgba(col))
            for t in (3, 6, 9):
                xx = 2 + (w - 4) * t / 12.0
                d.line([(xx * SS, 0), (xx * SS, 3 * SS)], fill=rgba((235, 235, 240), 150), width=SS)
                d.line([(xx * SS, (h - 3) * SS), (xx * SS, h * SS)], fill=rgba((235, 235, 240), 150), width=SS)
            strip.paste(down(big, w, h), (0, i * h))
        strip.save(os.path.join(OUT, name), optimize=True)


# ---- background ---------------------------------------------------------------------------------------------------------------
def load(name):
    im = Image.open(os.path.join(IMGS, name)).convert("RGB")
    assert im.size == (MOCK_W, MOCK_H), "%s must be 1920x1080" % name
    return im


PALETTE = np.array([BLUE, PANEL, MINT], float)
CUP = (1665, 40, 1900, 355)                   # the boba cup: kept exactly as drawn
CORE_C, CORE_R = (1562, 643), 232             # the big knob's circle (mockup px), with margin
# visible points (mockup px) of the cloud's lower edge on both sides of the Core knob: y = f(x) there
CLOUD_EDGE = [(1250, 457.5), (1300, 470.0), (1340, 477.5), (1790, 543.0), (1820, 533.0), (1850, 521.0), (1900, 495.0), (1915, 485.0)]


def make_clean_mockup():
    """The mockup without its controls and lettering: only the sky, the two clouds and the cup remain (mockup resolution)."""
    im = np.asarray(load("Poli-tea.jpg"), float)
    dist = np.linalg.norm(im[:, :, None, :] - PALETTE[None, None], axis=3)
    cls = dist.argmin(2)
    dirty = dist.min(2) > 22
    cx0, cy0, cx1, cy1 = CUP
    dirty[cy0:cy1, cx0:cx1] = False
    dirty = ndimage.binary_dilation(dirty, iterations=5)
    dirty[cy0:cy1, cx0:cx1] = False
    # the cloud edge behind the Core knob (white above, sky below), continued from its visible ends
    yy, xx = np.mgrid[0:MOCK_H, 0:MOCK_W]
    core = (xx - CORE_C[0]) ** 2 + (yy - CORE_C[1]) ** 2 <= CORE_R ** 2
    px, py = zip(*CLOUD_EDGE)
    poly = np.polyfit(px, py, 3)
    edge_y = np.polyval(poly, xx)
    cls = np.where(core, np.where(yy < edge_y, 1, 0), cls)
    known = ~dirty | core
    # fill the remaining holes from their surroundings: a smoothed vote of the known pixels' classes, at growing scales
    todo = dirty & ~core
    assigned = np.where(known, cls, -1)
    for sigma in (5, 12, 28, 60, 120):
        if not todo.any():
            break
        votes = np.stack([ndimage.gaussian_filter(((assigned == k)).astype(float), sigma) for k in range(3)], axis=2)
        total = votes.sum(2)
        pick = votes.argmax(2)
        ok = todo & (total > 0.12)
        assigned = np.where(ok, pick, assigned)
        todo = todo & ~ok
    assigned = np.where(assigned < 0, 0, assigned)
    flat = PALETTE[assigned]
    alpha = ndimage.gaussian_filter((dirty | core).astype(float), 0.9)
    alpha = np.clip(alpha * 1.6, 0, 1)[:, :, None]
    alpha[cy0:cy1, cx0:cx1] = 0
    out = im * (1 - alpha) + flat * alpha
    return Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8))


def to_screen(im):
    scaled = im.resize((round(MOCK_W * S), H), Image.Resampling.LANCZOS)
    page = Image.new("RGB", (W, H), BLUE)
    page.paste(scaled, (0, 0))
    edge = scaled.crop((scaled.width - 1, 0, scaled.width, H)).resize((W - scaled.width, H))   # carry the last column on
    page.paste(edge, (scaled.width, 0))
    return page


def widen(page):
    """Stretch everything right of the left margin by BODY_K (soft shapes only: lettering is drawn afterwards), and paste the
    boba cup back at its own proportions against the right edge."""
    box = tuple(round(v * S) for v in CUP)
    cup = page.crop(box)
    flat = page.copy()
    ImageDraw.Draw(flat).rectangle([box[0], box[1], box[2] - 1, box[3] - 1], fill=PANEL)   # the cloud is flat under the cup
    rest = flat.crop((X_LEFT, 0, W, H))
    rest = rest.resize((round(rest.width * BODY_K), H), Image.Resampling.LANCZOS).crop((0, 0, W - X_LEFT, H))
    out = Image.new("RGB", (W, H), BLUE)
    out.paste(flat.crop((0, 0, X_LEFT, H)), (0, 0))
    out.paste(rest, (X_LEFT, 0))
    cup = cup.resize((round(cup.width * CUP_SCALE), round(cup.height * CUP_SCALE)), Image.Resampling.LANCZOS)
    out.paste(cup, (CUP_RIGHT - cup.width, CUP_TOP))
    return out


def ctext(d, cx, baseline, text, f, fill, anchor="ms"):
    d.text((cx, baseline), text, font=f, fill=fill, anchor=anchor)


def make_background():
    clean = make_clean_mockup()
    clean.save(os.path.join(HERE, "imgs", "_clean_preview.png")) if os.environ.get("TEA_SAVE_CLEAN") else None
    page = widen(to_screen(clean))
    # all lettering at screen resolution, 4x supersampled
    big = page.resize((W * 2, H * 2), Image.Resampling.LANCZOS)
    d = ImageDraw.Draw(big)
    k = 2
    f = font(LABEL_PX * k, LABEL_W)
    for text, cx, base in LABELS:
        ctext(d, cx * k, base * k, text, f, INK)
    f = font(TITLE_PX * k, TITLE_W)
    for text, cx, base in TITLES:
        ctext(d, cx * k, base * k, text, f, INK)
    text, x, base = TITLE_BIG
    d.text((x * k, base * k), text, font=font(BIG_PX * k * 1.7, BIG_W), fill=INK, anchor="ls") if False else \
        d.text((x * k, base * k), text, font=font(BIG_PX * k, BIG_W), fill=INK, anchor="ls")
    x0, y0, x1, y1 = BAR
    d.rectangle([x0 * k, y0 * k, x1 * k, y1 * k], fill=INK)
    # the Gain Reduction legend and its row labels
    f = font(24 * k, 700)
    text, cx, base = GR_TITLE
    ctext(d, cx * k, base * k, text, f, INK)
    for t, cy in zip(GR_LABELS, GR_METER["cy"]):
        ctext(d, (GR_METER["x0"] - 38) * k, (cy + 8) * k, t, f, INK)
    page = down(big, W, H)
    page.save(os.path.join(OUT, "bg.png"), optimize=True)
    return page


# ---- arrows -----------------------------------------------------------------------------------------------------------------
def make_arrows():
    """The picture is the whole touch box (the triangle plus a transparent margin): MPC stretches a button's image to its box."""
    res = {}
    PADX, PADY = 20, 14
    for side, b in zip(("prev", "next"), SEL_ARROWS):
        w, h = b[2], b[3]
        bw, bh = w + PADX, h + PADY
        for suffix, c in (("", INK), ("_on", GREY)):
            im = Image.new("RGBA", (bw * SS, bh * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            x0, y0, x1, y1 = PADX // 2 * SS, PADY // 2 * SS, (PADX // 2 + w) * SS, (PADY // 2 + h) * SS
            if side == "prev":
                d.polygon([(x1, y0), (x1, y1), (x0, (y0 + y1) / 2)], fill=rgba(c))
            else:
                d.polygon([(x0, y0), (x0, y1), (x1, (y0 + y1) / 2)], fill=rgba(c))
            down(im, bw, bh).save(os.path.join(OUT, "arrow_sel_%s%s.png" % (side, suffix)))
        res[side] = (b[0] + w // 2, SEL_CY, bw, bh)
    return res


# ---- preset selector + browser ---------------------------------------------------------------------------------------------------
PRESET_NAMES = [d["name"] for d in factory.PRESETS]
PANEL_BOX = (66, 160, CUP_RIGHT - 66, 402)     # screen x, y, w, h of the browser: as wide as the layout
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
        ctext_box(d, (0, 0, w * SS, h * SS), nm, fit(nm, w - 6, 31), rgba(INK))
        fn = "preset_name_%d.png" % i
        down(im, w, h).save(os.path.join(OUT, fn), optimize=True)
        files.append("skin_assets/" + fn)
    return files


def cell_pos(slot):
    r, c = divmod(slot, 3)
    return GRID_X + c * (CELL_W + CELL_GAP), GRID_Y + r * (CELL_H + 8)


def make_browser():
    pw, ph = PANEL_BOX[2], PANEL_BOX[3]
    cats = factory.CAT_NAMES
    members = {0: list(range(len(factory.PRESETS)))}
    for c in range(1, len(cats)):
        members[c] = [i for i, d in enumerate(factory.PRESETS) if d["cat"] == cats[c]]
    pages = {c: max(1, (len(members[c]) + 11) // 12) for c in members}
    files = []
    for v, (c, pg) in enumerate(make_params.views()):
        im = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        b = 5 * SS
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=28 * SS, fill=rgba(PANEL),
                            outline=rgba(INK), width=b)
        for i, nm in enumerate(cats):          # category column
            y0 = 14 + i * (PILL_H + PILL_GAP)
            on = i == c
            box = [16 * SS, y0 * SS, (16 + SIDE_W) * SS, (y0 + PILL_H) * SS]
            d.rounded_rectangle(box, radius=PILL_H * SS / 2, fill=rgba(AMBER if on else WHITE), outline=rgba(INK), width=2 * SS)
            ctext_box(d, box, nm, fit(nm, SIDE_W - 20, 20, 800 if on else 650), rgba(INK))
        f = font(27 * SS, 800)                 # header: category, page n / m, arrows
        d.text((GRID_X * SS, 30 * SS), cats[c], font=f, fill=rgba(INK), anchor="ls")
        for (ax, ay, aw, ah), left in ((BPAGE_PREV, True), (BPAGE_NEXT, False)):
            if pages[c] > 1:
                pts = [((ax + aw) * SS, ay * SS), ((ax + aw) * SS, (ay + ah) * SS), (ax * SS, (ay + ah / 2) * SS)] if left else \
                      [(ax * SS, ay * SS), (ax * SS, (ay + ah) * SS), ((ax + aw) * SS, (ay + ah / 2) * SS)]
                d.polygon(pts, fill=rgba(INK))
        tx0, ty0, tw, th = BPAGE_TEXT
        ctext_box(d, (tx0 * SS, ty0 * SS, (tx0 + tw) * SS, (ty0 + th) * SS), "%d / %d" % (pg + 1, pages[c]), font(21 * SS, 700), rgba(INK))
        for slot in range(12):                 # the page's presets
            k = pg * 12 + slot
            if k >= len(members[c]):
                break
            cx0, cy0 = cell_pos(slot)
            box = [cx0 * SS, cy0 * SS, (cx0 + CELL_W) * SS, (cy0 + CELL_H) * SS]
            d.rounded_rectangle(box, radius=16 * SS, fill=rgba(WHITE), outline=rgba(INK), width=3 * SS)
            nm = factory.PRESETS[members[c][k]]["name"]
            ctext_box(d, box, nm, fit(nm, CELL_W - 22, 23, 700), rgba(INK))
        fn = "browser_%d.png" % v
        down(im, pw, ph).save(os.path.join(OUT, fn), optimize=True)
        files.append(fn)
    # highlight of the current preset's cell: a ring and an amber tint, over the cell
    im = Image.new("RGBA", (CELL_W * SS, CELL_H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, CELL_W * SS - 1, CELL_H * SS - 1], radius=16 * SS, fill=rgba(AMBER, 120), outline=rgba(INK), width=6 * SS)
    down(im, CELL_W, CELL_H).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- value text must fit under its knob ---------------------------------------------------------------------------------------
WORST = {   # the longest read-outs each knob can show (engine.cpp display())
    "input": ["+18 dB", "-18 dB", "+12.5 dB", "-12.5 dB"], "drive": ["100 %"], "low": ["+6 dB", "-6 dB", "+2.4 dB", "-2.4 dB"],
    "mid": ["+4 dB", "-4 dB", "-2.4 dB"], "high": ["+6 dB", "-6 dB", "+2.4 dB"], "amount": ["200 %"],
    "ldrive": ["100 %"], "lthresh": ["-18 dB", "-12.5 dB"], "output": ["-24 dB", "-0.3 dB", "-12.5 dB"],
}
SPACING = {"output": 130, "input": 122, "drive": 122, "low": 119, "mid": 119, "high": 119, "ldrive": 151, "lthresh": 151}


def check_text_fits():
    for key, (cx, cy, r, px) in KNOBS.items():
        f = font(px)
        for s in WORST[key]:
            w = f.getlength(s)
            assert w <= SPACING.get(key, 2 * r) - 6, "%s: '%s' is %.0f px wide, its column has %d px" % (key, s, w, SPACING.get(key, 2 * r))


# ---- layout.conf -------------------------------------------------------------------------------------------------------------
def write_layout(arrows, sel_files, browser_files):
    L = ["# Generated by generate_skin.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=C7D9E7", "theme_ink=000005", "theme_ink_dim=000005", "theme_accent=FFC869", "",
         "[tab POLI-TEA]", "art file=skin_assets/bg.png fit=stretch", ""]

    L.append("# knobs: black with a long white pointer; the value (the engine's text) sits under each knob, the labels are in the background")
    for key, (cx, cy, r, vp) in KNOBS.items():
        hbox = max(26, round(vp * 1.2))
        L.append('knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=000005'
                 % (cx, cy + Y_OFF, r, key, vp, 3, max(2 * r + 2, 132 if r < 100 else 2 * r + 2)))
    cx, cy, w, h = SC_BOX
    L += ["", "# Sidechain source: Internal (outlined) / External (filled, lit dot)",
          'toggle cx=%d cy=%d label="" key=sc ns=0 w=%d h=%d img=skin_assets/sc_off.png img_on=skin_assets/sc_on.png' % (cx, cy + Y_OFF, w, h)]
    m = IN_METER
    L += ["", "# input meters (after Input gain): peak, RMS; the green zone on the track is the sweet spot"]
    L.append("meter cx=%d cy=%d w=%d h=%d key=m_pk strip=skin_assets/meter_in_peak.png frames=%d" % (m["cx"][0], m["cy"] + Y_OFF, m["w"], m["h"], m["frames"]))
    L.append("meter cx=%d cy=%d w=%d h=%d key=m_rms strip=skin_assets/meter_in_rms.png frames=%d" % (m["cx"][1], m["cy"] + Y_OFF, m["w"], m["h"], m["frames"]))
    g = GR_METER
    L += ["", "# gain reduction: Low / Mid / High band (green zone = target) and the limiter"]
    for key, strip, cyy in zip(("m_gl", "m_gm", "m_gh", "m_lim"), ("band", "band", "band", "lim"), g["cy"]):
        L.append("meter cx=%d cy=%d w=%d h=%d key=%s strip=skin_assets/meter_gr_%s.png frames=%d"
                 % (g["x0"] + g["w"] // 2, cyy + Y_OFF, g["w"], g["h"], key, strip, g["frames"]))
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
    L += ["", "# Q-Links, 4 to a Live III page: 1 Amount / Input / Drive / Output, 2 Low / Mid / High / Sidechain, 3 Limiter Drive / Limiter Threshold",
          'qlinks "POLI-TEA" = amount,input,drive,output,low,mid,high,sc,ldrive,lthresh,-,-,-,-,-,-', ""]
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
    make_toggles()
    make_input_meters()
    make_gr_meters()
    make_background()
    arrows = make_arrows()
    sel_files = make_selector()
    browser_files = make_browser()
    write_layout(arrows, sel_files, browser_files)
    print("knob radii %s; %d browser views; %d presets" % (radii(), len(browser_files), len(sel_files)))
    print("wrote vst/skin_assets/*.png and vst/layout.conf")
