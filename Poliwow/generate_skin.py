#!/usr/bin/env python3
"""Poliwow skin assets: turns the Canva mockups (imgs/) into the MPC skin's images AND writes vst/layout.conf, so every
coordinate lives in this one file.

    python3 generate_skin.py            (run from the Poliwow folder; needs Pillow and numpy)

Reads imgs/1.jpg (WOW page mockup) and imgs/2.jpg (GRANULES page mockup), plus the old mockups in imgs/color variations/
(Poli-Wow-0.jpg, -50.jpg, -100.jpg: the Wow face at 0 / 50 / 100 %, used for the face art) and imgs/Poli-Wow-bg.jpg (the blobs).
Writes (inside vst/, which is what the skin builder reads):
    skin_assets/bg.png         1280x628 page: the background blobs widened to the page, the mockup's title, eyes and
                               labels at its own scale, both knobs taken out, plus the preset arrows
    skin_assets/knob_mix.png   the yellow Mix face, MIX_FRAMES frames, pointer from 9 o'clock (0 %) over the top to
                               3 o'clock (100 %); the eyes and mouth stay put, only the pointer turns
    skin_assets/knob_wow.png   the Wow face, WOW_FRAMES frames: the pointer turns the same way and the face's colour
                               runs through the mockups' three gradients (0 % purple-green, 50 % yellow-orange,
                               100 % red-orange); the tongue stays put
    skin_assets/hit.png        transparent: the touch areas over the preset arrows and the page pills
    skin_assets/knob_grain.png the GRAINS knobs' shared strip (G_FRAMES frames); bg_grains.png (GRAINS page); tabs_0/1.png
                               (the WOW | GRAINS pills); mode_*.png, sync_*.png (the two switches)
    layout.conf
The percentage on each knob is MPC's own live value text ("57%", from engine.cpp *_display), drawn in the middle of
the face, so it is always the exact value, not a picture of it.

Geometry rules (MPC_PLUGIN_TOOLCHAIN.md): plugin area 1280x628; layout.conf y = screen y + 86; the 1920x1080 mockup is
scaled by 628/1080 and left-aligned; a knob is drawn into a (2r+8) box centred in a (2r+10) frame; filmstrips stay
<= 16384 px (vst/skin_post.py puts these strips in place of the builder's 128-frame ones and sets numFrames exactly).
"""
import colorsys
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")

# ---- screen ---------------------------------------------------------------------------------------------
W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H                      # uniform scale; the mockup ends at x=1116
SS = 2                              # knob frames are drawn at 2x the mockup's resolution, then scaled down

# ---- mockup geometry (measured on imgs/Poli-Wow-*.jpg, 1920x1080) ---------------------------------------
MIX_C, MIX_R = (479.5, 731.0), 238.5                    # Mix: centre, outline outer radius
MIX_PTR = (95.0, 239.0, 17.5)                            # pointer: inner radius, outer radius, half width
MIX_PTR_BOX = (459, 490, 500, 670)                       # where the mockup's pointer is (taken out of the face)
MIX_EYES = [(296.5, 660.0), (662.5, 660.0)]
MIX_EYE_R = 55.5
WOW_C, WOW_R = (1445.0, 539.5), 307.5
WOW_PTR = (132.5, 307.5, 26.0)
GRAD_ROW = 450                                           # a row through the Wow face with nothing drawn on it
PRESET_C, PRESET_ARROW_DX = (400, 335), 270             # preset name centre; arrows this far left / right of it
INK = (0, 0, 0)

# ---- live widgets -----------------------------------------------------------------------------------------
LIMIT = 16384                       # MPC's image height limit
FACE_MIX, FACE_WOW = 122.0, 156.0   # face radius on screen (the mockup's were 105.5 / 130)
R_MIX, R_WOW = 125, 158             # knob radius on screen (face + 3 px of margin)
MIX_FRAMES, WOW_FRAMES = 55, 43     # odd (one frame points straight up); 55 x 260 = 14300 px, 43 x 326 = 14018 px (limit 16384)
MIX_VS, WOW_VS = 26, 38             # value text px (MPC draws it in Titillium SemiBold)
MIX_DY, WOW_DY = 0, 0               # value text centre below the knob centre (Mix: between the eyes)
PRESET_TS = 26

# ---- the redesigned screens (imgs/1.jpg = WOW page, imgs/2.jpg = GRANULES page, 1920x1080) -------------------------------
# The faces keep their art (old mockups, imgs/color variations/) but are drawn at the new mockup's size: SC_* = screen px per
# old-mockup px.
NEW_MIX_C, NEW_MIX_R = (315.5, 779.5), 181.5
NEW_WOW_C, NEW_WOW_R = (1155.0, 682.5), 223.5
K_MIX, K_WOW = FACE_MIX / (NEW_MIX_R * S), FACE_WOW / (NEW_WOW_R * S)   # how much bigger than the mockup
NEW_MIX_R, NEW_WOW_R = FACE_MIX / S, FACE_WOW / S
SC_MIX = NEW_MIX_R * S / MIX_R
SC_WOW = NEW_WOW_R * S / WOW_R


def sx(x):
    return x * S


def screen_c(c):
    """A centre in whole screen px, and the same point back in mockup px (so the art sits where the widget is)."""
    x, y = round(sx(c[0])), round(sx(c[1]))
    return (x, y), (x / S, y / S)


def load(name):
    for d in (IMGS, os.path.join(IMGS, "color variations")):   # the old mockups (faces, clean paper) were moved into a sub folder
        p = os.path.join(d, name)
        if os.path.exists(p):
            return Image.open(p).convert("RGB")
    raise FileNotFoundError(name)


def disk_mask(size, cx, cy, r):
    m = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(m).ellipse([(cx - r) * 4, (cy - r) * 4, (cx + r) * 4, (cy + r) * 4], fill=255)
    return m.resize((size, size), Image.Resampling.LANCZOS)


def pointer_poly(c, angle_deg, r_in, r_out, hw):
    """A bar from r_in to r_out pointing at angle (0 = up, -90 = left, +90 = right)."""
    a = math.radians(angle_deg)
    ux, uy = math.sin(a), -math.cos(a)
    px, py = -uy, ux
    cx, cy = c
    return [(cx + ux * r_out + px * hw, cy + uy * r_out + py * hw), (cx + ux * r_out - px * hw, cy + uy * r_out - py * hw),
            (cx + ux * r_in - px * hw, cy + uy * r_in - py * hw), (cx + ux * r_in + px * hw, cy + uy * r_in + py * hw)]


def angle_of(i, n):
    return -90.0 + 180.0 * i / (n - 1)


# ---- Mix knob -------------------------------------------------------------------------------------------------
def make_mix(mock):
    box = 2 * R_MIX + 8
    c = MIX_C                                  # the face's centre in the OLD mockup (the art is cut from there)
    half = box / SC_MIX / 2                      # the frame's half size in mockup px
    big = round(box / SC_MIX * SS)
    k = big / (2 * half)                    # mockup px -> frame px
    x0, y0 = c[0] - half, c[1] - half
    to = lambda x, y: ((x - x0) * k, (y - y0) * k)
    # take the drawn pointer out: every row of its box is filled from the pixels just left and right of it (the
    # outline is nearly level up there, so this rebuilds the black ring and the yellow face without a seam)
    src = np.asarray(mock).astype(float).copy()
    bx0, by0, bx1, by1 = MIX_PTR_BOX
    L, R = src[by0:by1 + 1, bx0 - 2:bx0].mean(1), src[by0:by1 + 1, bx1 + 1:bx1 + 3].mean(1)
    u = (np.arange(bx0, bx1 + 1) - (bx0 - 1)) / (bx1 + 1 - (bx0 - 1))
    src[by0:by1 + 1, bx0:bx1 + 1] = L[:, None, :] * (1 - u[None, :, None]) + R[:, None, :] * u[None, :, None]
    clean_mock = Image.fromarray(src.clip(0, 255).astype(np.uint8), "RGB")
    face = clean_mock.crop((round(x0), round(y0), round(x0 + 2 * half), round(y0 + 2 * half))).resize((big, big), Image.Resampling.BICUBIC)
    face = face.convert("RGBA")
    fc = to(*MIX_C)
    # alpha: the face disc and the eyes (they sit on the outline)
    alpha = disk_mask(big, fc[0], fc[1], (MIX_R + 0.5) * k)
    for e in MIX_EYES:
        ex, ey = to(*e)
        alpha = Image.fromarray(np.maximum(np.asarray(alpha), np.asarray(disk_mask(big, ex, ey, (MIX_EYE_R + 0.5) * k))))
    face.putalpha(alpha)

    strip = Image.new("RGBA", (box, box * MIX_FRAMES), (0, 0, 0, 0))
    r_in, r_out, hw = MIX_PTR
    for i in range(MIX_FRAMES):
        fr = face.copy()
        d = ImageDraw.Draw(fr)
        d.polygon(pointer_poly(fc, angle_of(i, MIX_FRAMES), r_in * k, r_out * k, hw * k), fill=(255, 255, 255, 255))
        # the pointer is drawn over the eyes, so it stays readable when it passes them (about 20 % and 80 %)
        # keep the pointer inside the face
        a = np.minimum(np.asarray(fr.getchannel("A")), np.asarray(alpha))
        fr.putalpha(Image.fromarray(a))
        strip.paste(fr.resize((box, box), Image.Resampling.LANCZOS), (0, i * box))
    assert MIX_FRAMES * (box + 2) <= LIMIT
    strip.save(os.path.join(OUT, "knob_mix.png"), optimize=True)
    return strip


# ---- Wow knob -------------------------------------------------------------------------------------------------
def gradient_ends(img):
    """The face's left and right colours: a straight-line fit along a clean row (Canva's linear gradient)."""
    a = np.asarray(img).astype(float)
    cx, cy = WOW_C
    half = math.sqrt(WOW_R ** 2 - (GRAD_ROW - cy) ** 2) - 12
    xs = np.arange(int(cx - half), int(cx + half))
    row = a[GRAD_ROW, xs]
    t = (xs - (cx - WOW_R)) / (2 * WOW_R)
    ends = []
    for ch in range(3):
        m, b = np.polyfit(t, row[:, ch], 1)
        ends.append((b, b + m))
    left = tuple(e[0] for e in ends)
    right = tuple(e[1] for e in ends)
    return np.array(left), np.array(right)


def tongue_layer(m0, m50):
    """The mouth and tongue (the same on every mockup) as RGBA: where the 0 % and 50 % faces differ only by their
    gradient, the pixel is face; where they agree, it is mouth. alpha = 1 - |p0 - p50| / |b0 - b50|."""
    a0, a5 = np.asarray(m0).astype(float), np.asarray(m50).astype(float)
    l0, r0 = gradient_ends(m0)
    l5, r5 = gradient_ends(m50)
    hgt, wid = a0.shape[:2]
    t = np.clip((np.arange(wid) - (WOW_C[0] - WOW_R)) / (2 * WOW_R), 0, 1)
    b0 = l0[None, :] + (r0 - l0)[None, :] * t[:, None]
    b5 = l5[None, :] + (r5 - l5)[None, :] * t[:, None]
    db = np.linalg.norm(b0 - b5, axis=1)[None, :]
    dp = np.linalg.norm(a0 - a5, axis=2)
    alpha = np.clip(1 - dp / np.maximum(db, 1), 0, 1)
    yy, xx = np.mgrid[0:hgt, 0:wid]
    inside = (xx - WOW_C[0]) ** 2 + (yy - WOW_C[1]) ** 2 < (WOW_R - 6) ** 2
    region = inside & (yy > WOW_C[1] + 40)       # the mouth is in the lower half; the pointer never goes there
    alpha = np.where(region, alpha, 0)
    alpha = np.where(alpha < 0.08, 0, alpha)
    a_ = np.maximum(alpha, 1e-3)[..., None]
    col = (a0 - (1 - alpha[..., None]) * b0[None, :, :]) / a_
    rgba = np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA"), ((l0, r0), (l5, r5))


def hue_mix(c0, c1, u):
    """Blend two RGB colours the short way round the hue wheel (purple -> yellow goes through pink and orange, not brown)."""
    h0, s0, v0 = colorsys.rgb_to_hsv(*(np.clip(c0, 0, 255) / 255.0))
    h1, s1, v1 = colorsys.rgb_to_hsv(*(np.clip(c1, 0, 255) / 255.0))
    dh = (h1 - h0 + 0.5) % 1.0 - 0.5
    rgb = colorsys.hsv_to_rgb((h0 + dh * u) % 1.0, s0 + (s1 - s0) * u, v0 + (v1 - v0) * u)
    return np.array(rgb) * 255.0


def make_wow(m0, m50, m100):
    box = 2 * R_WOW + 8
    c = WOW_C                                  # the face's centre in the OLD mockup (the art is cut from there)
    half = box / SC_WOW / 2
    big = round(box / SC_WOW * SS)
    k = big / (2 * half)
    x0, y0 = c[0] - half, c[1] - half
    to = lambda x, y: ((x - x0) * k, (y - y0) * k)
    tongue, (g0, g50) = tongue_layer(m0, m50)
    g100 = gradient_ends(m100)
    tongue = tongue.crop((round(x0), round(y0), round(x0 + 2 * half), round(y0 + 2 * half))).resize((big, big), Image.Resampling.BICUBIC)
    fc = to(*WOW_C)
    alpha = disk_mask(big, fc[0], fc[1], WOW_R * k)
    xs = np.arange(big)
    t = np.clip((xs - (fc[0] - WOW_R * k)) / (2 * WOW_R * k), 0, 1)

    def ends(v):   # 0..1 -> (left, right) colour: through the three mockups, round the colour wheel (no muddy mix)
        a, b, u = (g0, g50, v / 0.5) if v <= 0.5 else (g50, g100, (v - 0.5) / 0.5)
        return hue_mix(a[0], b[0], u), hue_mix(a[1], b[1], u)

    strip = Image.new("RGBA", (box, box * WOW_FRAMES), (0, 0, 0, 0))
    r_in, r_out, hw = WOW_PTR
    for i in range(WOW_FRAMES):
        v = i / (WOW_FRAMES - 1)
        L, R = ends(v)
        row = L[None, :] + (R - L)[None, :] * t[:, None]
        face = Image.fromarray(np.repeat(row[None, :, :], big, axis=0).clip(0, 255).astype(np.uint8), "RGB").convert("RGBA")
        d = ImageDraw.Draw(face)
        d.polygon(pointer_poly(fc, angle_of(i, WOW_FRAMES), r_in * k, r_out * k, hw * k), fill=(255, 255, 255, 255))
        face.alpha_composite(tongue)
        face.putalpha(alpha)
        strip.paste(face.resize((box, box), Image.Resampling.LANCZOS), (0, i * box))
    assert WOW_FRAMES * (box + 2) <= LIMIT
    strip.save(os.path.join(OUT, "knob_wow.png"), optimize=True)
    return strip


# ---- background -----------------------------------------------------------------------------------------------
# The page is wider than the mockup (1280x628 vs 16:9 = 1116x628). The blobs (the "paper", from imgs/color variations is not
# needed: imgs/Poli-Wow-bg.jpg) are widened to fill the page, but only to the right of x = WARP_X, so everything the mockup puts
# on the left (title, labels, panel) keeps its shape and its place. Static art is taken from the mockups as ink (black on
# white / light blue) at the uniform 628/1080 scale; live things (preset name, Settings pill, knobs, switches) are left out.

# ---- the full-width screens (1280 px: the 1.2 mockups' look, spread over the whole MPC screen) -----------------------------------------------
TITLE_BOX = (140, 130, 632, 234)          # the "Poliwow" letters (inside the white blob), mockup px
TITLE_LINE = (108, 239, 993, 271)         # the old rule baked into Poli-Wow-bg.jpg (rebuilt out of the paper)
WARP_X = 940                              # screen px: the paper is widened to the right of this
UI_MAIN, UI_GRAN = "1.jpg", "2.jpg"
HEADER_BOX = (175, 400, 482, 462)         # "Granules" (mockup px)
EYE_BOXES = [(890, 418, 972, 500), (1338, 418, 1420, 500)]          # the Wow face's two eyes (they sit above the face)
LABEL_BOXES = [(425, 925, 770, 1030), (1270, 885, 1600, 980)]      # "Mix" and "Wow" with their underlines
FONT_PATH = os.path.join(HERE, "fonts", "TitilliumWeb-SemiBold.ttf")
BOLD_PATH = os.path.join(HERE, "fonts", "Poppins-Bold.ttf")        # the mockups' captions (Amount / Level are drawn with it)
PAPER = (198, 228, 250)
YELLOW = (255, 225, 103)

# top bar (screen px)
RULE = (63, 138, 1217, 158)                                       # the heavy rule under the title
BAR_Y = 117                                                       # centre line of the selector
PRE_C, PRE_W = 720, 300                                           # preset name centre / width (a tap on it opens the browser)
ARROW_X = (540, 900)                                              # preset < >
ARROW_HW, ARROW_HH = 15, 16                                       # half width, half height of an arrow
NAME_BOX = (PRE_C - PRE_W // 2 - 4, 95, PRE_W + 8, 44)           # the white pill behind the name: x, y, w, h
PILL = (1135, 112, 160, 43)                                       # Settings pill: cx, cy, w, h (screen)
HELP_BTN = (408, 108, 34)                                         # the ? button after the title: cx, cy, diameter
PANEL_BOX = (64, 70, 1152, 548)                                   # help window: screen x, y, w, h
BPAGE_PREV, BPAGE_TEXT, BPAGE_NEXT = (973, 10, 44, 38), (1023, 10, 63, 38), (1092, 10, 44, 38)   # inside the help window

# WOW page: Mix and Wow keep their mockup look, bigger and moved apart
_old = lambda c: (round(c[0] * S), round(c[1] * S))
MIX_POS, WOW_POS = (320, 464), (800, 392)
MIX_C0, WOW_C0 = _old(NEW_MIX_C), _old(NEW_WOW_C)                 # where the mockup had them

# GRANULES page (screen px)
PANEL = (40, 180, 1240, 604)                                      # the white panel (outer edge)
PANEL_BORDER, PANEL_RADIUS = 8, 24
R_G, G_FRAMES = 52, 61                       # the eight small knobs share one strip: 61 x 114 = 6954 px
R_L, L_FRAMES = 85, 61                       # the big Amount / Level knobs: 61 x 180 = 10980 px
G_VS, L_VS = 22, 30
G_COLS = [140, 300, 460, 620]
G_ROW1, G_ROW2 = 372, 516
AMT_C, LEVEL_C = (860, 385), (1070, 385)
BIG_CAP_PX = 34
G_KNOBS = [  # key, caption, cx, cy
    ("g_size", "Size", G_COLS[0], G_ROW1), ("g_density", "Density", G_COLS[1], G_ROW1),
    ("g_speed", "Speed", G_COLS[2], G_ROW1), ("g_var", "Variation", G_COLS[3], G_ROW1),
    ("g_retrace", "Retrace", G_COLS[0], G_ROW2), ("g_split", "Split", G_COLS[1], G_ROW2),
    ("g_rettime", "Time", G_COLS[2], G_ROW2), ("g_retlen", "Length", G_COLS[3], G_ROW2),
]
# where each caption sits in the GRANULES mockup (its ink box): the crops are re-placed over the knobs
_CAP = {"Size": (216, 508, 278, 532), "Density": (360, 508, 480, 539), "Speed": (543, 508, 640, 539), "Variation": (706, 508, 850, 532),
        "Retrace": (189, 788, 309, 810), "Split": (373, 786, 446, 817), "Time": (529, 786, 605, 810), "Length": (701, 786, 808, 817)}
LABELS = {cap: (_CAP[cap], cx, cy - R_G - 8 - round(24 * S)) for key, cap, cx, cy in G_KNOBS}
G_TOGGLES = [  # key, image stem, off text, on text, cx, cy, w, h
    ("g_retmode", "mode", "Retrace:Jump", "Retrace:Reverse", 880, 548, 240, 42),
    ("g_sync", "sync", "Sync:Off", "Sync:On", 1105, 548, 183, 42),
]


def ink_alpha(lum):
    """Black art on white or light blue: how much of a pixel is ink (35 = the mockup's black, 200 = below both papers)."""
    return np.clip((200.0 - lum) / (200.0 - 35.0), 0, 1)


def font(px):
    return ImageFont.truetype(FONT_PATH, int(round(px)))


def bfont(px):
    return ImageFont.truetype(BOLD_PATH, int(round(px)))


def widened_paper(clean):
    c = np.asarray(clean).astype(float).copy()
    x0, y0, x1, y1 = TITLE_BOX
    c[y0:y1 + 1, x0:x1 + 1] = 255.0
    x0, y0, x1, y1 = TITLE_LINE
    above, below = c[y0 - 4, x0 - 2:x1 + 3], c[y1 + 4, x0 - 2:x1 + 3]
    for y in range(y0 - 3, y1 + 4):
        u = (y - (y0 - 4)) / ((y1 + 4) - (y0 - 4))
        c[y, x0 - 2:x1 + 3] = above * (1 - u) + below * u
    img = np.asarray(Image.fromarray(c.clip(0, 255).astype(np.uint8), "RGB").resize((round(MOCK_W * S), H), Image.Resampling.LANCZOS)).astype(float)
    w = img.shape[1]
    A, D = w - WARP_X, W - WARP_X
    k = (D - A) / D ** 2
    xo = np.arange(W, dtype=float)
    d = np.maximum(xo - WARP_X, 0)
    u = np.clip(np.where(xo <= WARP_X, xo, WARP_X + d - k * d * d), 0, w - 1)
    i0 = np.floor(u).astype(int)
    i1 = np.minimum(i0 + 1, w - 1)
    f = (u - i0)[None, :, None]
    out = img[:, i0] * (1 - f) + img[:, i1] * f
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB").convert("RGBA")


def crop_ink(cv, lum, box, dx=0, dy=0, cx=None, top=None):
    """A box of a mockup (its ink) put into the W x H ink array cv at the uniform scale, moved by (dx, dy) screen px; or centred on
    cx with its top at `top`."""
    x0, y0, x1, y1 = box
    crop = Image.fromarray((ink_alpha(lum[y0 - 2:y1 + 3, x0 - 2:x1 + 3]) * 255).astype(np.uint8), "L")
    w, h = max(1, round(crop.width * S)), max(1, round(crop.height * S))
    a = np.asarray(crop.resize((w, h), Image.Resampling.LANCZOS)).astype(float)
    if cx is None:
        px, py = round((x0 - 2) * S) + dx, round((y0 - 2) * S) + dy
    else:
        px, py = int(round(cx - w / 2)), int(round(top - 2 * S))
    sx0, sy0 = max(0, -px), max(0, -py)
    ex, ey = min(w, W - px), min(h, H - py)
    cv[py + sy0:py + ey, px + sx0:px + ex] = np.maximum(cv[py + sy0:py + ey, px + sx0:px + ex], a[sy0:ey, sx0:ex])


def draw_ink(cv, fn):
    """fn(draw, k) draws white (255) on a k-times larger L image; the result (smoothed down) is added to cv as ink."""
    k = 4
    im = Image.new("L", (W * k, H * k), 0)
    fn(ImageDraw.Draw(im), k)
    np.maximum(cv, np.asarray(im.resize((W, H), Image.Resampling.LANCZOS)).astype(float), out=cv)


def put_ink(page, cv):
    layer = Image.new("RGBA", page.size, INK + (255,))
    layer.putalpha(Image.fromarray(cv.clip(0, 255).astype(np.uint8), "L"))
    page.alpha_composite(layer)


def plate(page, box, radius, border, fill):
    """A rounded plate with a black outline, drawn smooth (4x)."""
    k = 4
    big = Image.new("RGBA", (W * k, H * k), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big)
    x0, y0, x1, y1 = [v * k for v in box]
    bd.rounded_rectangle([x0, y0, x1, y1], radius=radius * k, fill=(0, 0, 0, 255))
    t = border * k
    bd.rounded_rectangle([x0 + t, y0 + t, x1 - t, y1 - t], radius=max(1, radius - border) * k, fill=fill + (255,))
    page.alpha_composite(big.resize((W, H), Image.Resampling.LANCZOS))


def caption(d, k, text, cx, base, px):
    d.text((cx * k, base * k), text, font=bfont(px * k), fill=255, anchor="ms")


def moved_crop(cv, lum, box, c0, c1, k):
    """A mockup box that belonged to a face (eyes, caption): moved with the face, and pushed out by k from its centre."""
    bx, by = (box[0] + box[2]) / 2 * S, (box[1] + box[3]) / 2 * S
    crop_ink(cv, lum, box, round(c1[0] + k * (bx - c0[0]) - bx), round(c1[1] + k * (by - c0[1]) - by))


def chrome(cv, lm):
    """What both pages share: the title, the heavy rule, the four arrows."""
    crop_ink(cv, lm, TITLE_BOX)

    def shapes(d, k):
        x0, y0, x1, y1 = RULE
        d.rectangle([x0 * k, y0 * k, x1 * k, y1 * k], fill=255)
        for i, ax in enumerate(ARROW_X):
            s = 1 if i % 2 == 0 else -1        # even: pointing left
            d.polygon([((ax - s * ARROW_HW) * k, BAR_Y * k), ((ax + s * ARROW_HW) * k, (BAR_Y - ARROW_HH) * k),
                       ((ax + s * ARROW_HW) * k, (BAR_Y + ARROW_HH) * k)], fill=255)
    draw_ink(cv, shapes)


def make_backgrounds(clean, m_main, m_gran):
    paper = widened_paper(clean)
    lm, lg = np.asarray(m_main.convert("L")).astype(float), np.asarray(m_gran.convert("L")).astype(float)
    # the Wow caption sits on the blob's outline: only its own ink is taken (the outline is already in the paper)
    lclean = np.asarray(clean.convert("L")).astype(float)
    lm_lbl = np.where(lclean > 140, lm, 255.0)
    # --- WOW page
    page = paper.copy()
    plate(page, (NAME_BOX[0], NAME_BOX[1], NAME_BOX[0] + NAME_BOX[2], NAME_BOX[1] + NAME_BOX[3]), 22, 3, (255, 255, 255))
    cv = np.zeros((H, W))
    chrome(cv, lm)
    for b in EYE_BOXES:
        moved_crop(cv, lm, b, WOW_C0, WOW_POS, K_WOW)
    moved_crop(cv, lm_lbl, LABEL_BOXES[0], MIX_C0, MIX_POS, K_MIX)
    moved_crop(cv, lm_lbl, LABEL_BOXES[1], WOW_C0, WOW_POS, K_WOW)
    put_ink(page, cv)
    page.convert("RGB").save(os.path.join(OUT, "bg.png"), optimize=True)
    # --- GRANULES page: the same paper, the panel, its header, the captions over the knobs
    page = paper.copy()
    plate(page, (NAME_BOX[0], NAME_BOX[1], NAME_BOX[0] + NAME_BOX[2], NAME_BOX[1] + NAME_BOX[3]), 22, 3, (255, 255, 255))
    plate(page, PANEL, PANEL_RADIUS, PANEL_BORDER, (255, 255, 255))
    cv = np.zeros((H, W))
    chrome(cv, lm)
    crop_ink(cv, lg, HEADER_BOX, -20, -14)
    draw_ink(cv, lambda d, k: d.rectangle([82 * k, 258 * k, 700 * k, 264 * k], fill=255))
    for name, (bx, col_x, top) in LABELS.items():
        crop_ink(cv, lg, bx, cx=col_x, top=top)

    def bigcaps(d, k):
        caption(d, k, "Amount", AMT_C[0], AMT_C[1] - R_L - 16, BIG_CAP_PX)
        caption(d, k, "Level", LEVEL_C[0], LEVEL_C[1] - R_L - 16, BIG_CAP_PX)
    draw_ink(cv, bigcaps)
    put_ink(page, cv)
    page.convert("RGB").save(os.path.join(OUT, "bg_grains.png"), optimize=True)
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))


def make_pointer_knob(name, r, frames):
    """Black disc, white bar from near the middle to the rim; sweeps like the faces (9 o'clock = 0 %, up = 50 %, 3 o'clock = 100 %).
    The value text (white) sits in the middle of the disc, so the bar starts at 0.64 r."""
    box = 2 * r + 8
    k = 4
    big = box * k
    c = big / 2
    rad = (box / 2 - 3) * k
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).ellipse([c - rad, c - rad, c + rad, c + rad], fill=255)
    strip = Image.new("RGBA", (box, box * frames), (0, 0, 0, 0))
    for i in range(frames):
        fr = Image.new("RGBA", (big, big), (0, 0, 0, 255))
        ImageDraw.Draw(fr).polygon(pointer_poly((c, c), angle_of(i, frames), rad * 0.64, rad * 1.02, rad * 0.10), fill=(255, 255, 255, 255))
        fr.putalpha(mask)
        strip.paste(fr.resize((box, box), Image.Resampling.LANCZOS), (0, i * box))
    assert frames * (box + 2) <= LIMIT
    strip.save(os.path.join(OUT, name), optimize=True)


def pill_image(w, h, text, on, px=22):
    k = 4
    big = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle([2 * k, 2 * k, (w - 2) * k, (h - 2) * k], radius=(h - 4) * k / 2,
                        fill=(YELLOW if on else (255, 255, 255)) + (255,), outline=(0, 0, 0, 255), width=3 * k)
    f = font(px * k)
    cap = f.getbbox("H", anchor="ls")
    d.text((w * k / 2, h * k / 2 + (cap[3] - cap[1]) / 2), text, font=f, fill=(0, 0, 0, 255), anchor="ms")
    return big.resize((w, h), Image.Resampling.LANCZOS)


def make_widgets():
    _, _, w, h = PILL
    pill_image(w, h, "Settings", False).save(os.path.join(OUT, "pill_0.png"), optimize=True)
    pill_image(w, h, "Settings", True).save(os.path.join(OUT, "pill_1.png"), optimize=True)
    for key, stem, off, on, cx, cy, tw, th in G_TOGGLES:
        pill_image(tw, th, off, False).save(os.path.join(OUT, "%s_off.png" % stem), optimize=True)
        pill_image(tw, th, on, True).save(os.path.join(OUT, "%s_on.png" % stem), optimize=True)
    cx, cy, dia = HELP_BTN
    for on in (False, True):
        k = 4
        im = Image.new("RGBA", (dia * k, dia * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.ellipse([1.5 * k, 1.5 * k, (dia - 1.5) * k, (dia - 1.5) * k], fill=((0, 0, 0) if on else (255, 255, 255)) + (255,), outline=(0, 0, 0, 255), width=3 * k)
        f = font(24 * k)
        cap = f.getbbox("H", anchor="ls")
        d.text((dia * k / 2, dia * k / 2 + (cap[3] - cap[1]) / 2), "?", font=f, fill=((255, 255, 255) if on else (0, 0, 0)) + (255,), anchor="ms")
        im.resize((dia, dia), Image.Resampling.LANCZOS).save(os.path.join(OUT, "help_btn%s.png" % ("_on" if on else "")), optimize=True)



# ---- preset browser (the Poli-tea scheme: categories left, 12 presets a page right; engine side: src/wow_nav.h) ----------------------------------
import re as _re
_src = open(os.path.join(HERE, "src", "WowDSP.cpp")).read()
CAT_NAMES = ["All"] + _re.findall(r'"([^"]+)"', _re.search(r'kCats\[kNumCats\]\s*=\s*\{([^}]*)\}', _src).group(1))
_PRE = _re.findall(r'^\s*P\("([^"]+)"\s*,\s*(\d+)', _src, _re.M)
PRESETS = [(n, int(c)) for n, c in _PRE]
MEMBERS = {c: [i for i, (n, k) in enumerate(PRESETS) if c == 0 or k == c - 1] for c in range(len(CAT_NAMES))}
PAGES = {c: max(1, (len(MEMBERS[c]) + 11) // 12) for c in MEMBERS}
VIEWS = [(c, p) for c in range(len(CAT_NAMES)) for p in range(PAGES[c])]          # numbered like wow_nav.h's view()
BROWSER_BOX = (64, 172, 1152, 440)                                                 # screen x, y, w, h
SIDE_W, PILL_H, PILL_GAP = 190, 38, 6
GRID_X, GRID_Y = SIDE_W + 44, 62
CELL_W = (BROWSER_BOX[2] - GRID_X - 24 - 2 * 12) // 3
CELL_H, CELL_PITCH = 76, 84
BB_PREV, BB_TEXT, BB_NEXT = (BROWSER_BOX[2] - 250, 10, 44, 38), (BROWSER_BOX[2] - 200, 10, 63, 38), (BROWSER_BOX[2] - 131, 10, 44, 38)


def bfit(text, maxw, px):
    f = bfont(px * SS)
    while f.getlength(text) > maxw * SS and px > 11:
        px -= 1
        f = bfont(px * SS)
    return f


def ctext(d, box, text, f, fill=(0, 0, 0, 255)):
    x0, y0, x1, y1 = box
    cap = f.getbbox("H", anchor="ls")
    d.text(((x0 + x1) / 2, (y0 + y1) / 2 + (cap[3] - cap[1]) / 2), text, font=f, fill=fill, anchor="ms")


def cell_pos(slot):
    r, c = divmod(slot, 3)
    return GRID_X + c * (CELL_W + 12), GRID_Y + r * CELL_PITCH


def make_browser():
    SSb = SS
    pw, ph = BROWSER_BOX[2], BROWSER_BOX[3]
    files = []
    for v, (c, pg) in enumerate(VIEWS):
        im = Image.new("RGBA", (pw * SSb, ph * SSb), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        b = 7 * SSb
        d.rounded_rectangle([b / 2, b / 2, pw * SSb - 1 - b / 2, ph * SSb - 1 - b / 2], radius=26 * SSb, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=b)
        for i, nm in enumerate(CAT_NAMES):
            y0 = 16 + i * (PILL_H + PILL_GAP)
            box = [20 * SSb, y0 * SSb, (20 + SIDE_W) * SSb, (y0 + PILL_H) * SSb]
            d.rounded_rectangle(box, radius=PILL_H * SSb / 2, fill=(YELLOW if i == c else PAPER) + (255,), outline=(0, 0, 0, 255), width=3 * SSb)
            ctext(d, box, nm, bfit(nm, SIDE_W - 24, 20))
        d.text((GRID_X * SSb, 40 * SSb), CAT_NAMES[c], font=bfont(30 * SSb), fill=(0, 0, 0, 255), anchor="ls")
        if PAGES[c] > 1:
            for (ax, ay, aw, ah), left in ((BB_PREV, True), (BB_NEXT, False)):
                pts = [((ax + aw) * SSb, ay * SSb), ((ax + aw) * SSb, (ay + ah) * SSb), (ax * SSb, (ay + ah / 2) * SSb)] if left else \
                      [(ax * SSb, ay * SSb), (ax * SSb, (ay + ah) * SSb), ((ax + aw) * SSb, (ay + ah / 2) * SSb)]
                d.polygon(pts, fill=(0, 0, 0, 255))
            tx0, ty0, tw, th = BB_TEXT
            ctext(d, (tx0 * SSb, ty0 * SSb, (tx0 + tw) * SSb, (ty0 + th) * SSb), "%d / %d" % (pg + 1, PAGES[c]), bfont(20 * SSb))
        for slot in range(12):
            k = pg * 12 + slot
            if k >= len(MEMBERS[c]):
                break
            cx0, cy0 = cell_pos(slot)
            box = [cx0 * SSb, cy0 * SSb, (cx0 + CELL_W) * SSb, (cy0 + CELL_H) * SSb]
            d.rounded_rectangle(box, radius=18 * SSb, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=3 * SSb)
            nm = PRESETS[MEMBERS[c][k]][0]
            ctext(d, box, nm, bfit(nm, CELL_W - 24, 26))
        fn = "browser_%d.png" % v
        im.resize((pw, ph), Image.Resampling.LANCZOS).save(os.path.join(OUT, fn), optimize=True)
        files.append(fn)
    # the current preset's cell: a yellow tint and a heavier ring, over the cell
    im = Image.new("RGBA", (CELL_W * SSb, CELL_H * SSb), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, CELL_W * SSb - 1, CELL_H * SSb - 1], radius=18 * SSb, fill=YELLOW + (90,), outline=(0, 0, 0, 255), width=7 * SSb)
    im.resize((CELL_W, CELL_H), Image.Resampling.LANCZOS).save(os.path.join(OUT, "browser_hl.png"))
    return files


# ---- help window ------------------------------------------------------------------------------------------------------------------
HELP_PAGES = [
    ("Wow page", [
        ("Wow", "The build-up. 0 % = the audio passes untouched. Up: thinner body, brighter, growing reverb, echoes and grains. Snap back to 0 for the drop: the tails ring out."),
        ("Mix", "Dry / wet blend. 100 % is the full effect; lower it to keep more of the original track."),
        ("Preset  < >", "Steps through all 39 presets with the arrows. A preset also sets the Granules knobs; change them afterwards as you like."),
        ("Browser", "Tap the preset name: categories on the left, presets on the right. Tap a category, then a preset to load it. Tap the name again to close."),
        ("Settings  ?", "Settings opens the Granules page; tap again to come back. The ? button opens this help; tap it to close."),
        ("Bright", "Glass Rise, Sparkle Sweep, Bloom, Crystal Rush, Aurora ...: a bright shimmer reverb sweeps up as Wow rises. Made for build-up sweeps."),
        ("Soft", "Heaven Lift, Soft Smile, Cloud Nine, Halo, Angel Dust ...: airy and ethereal, long tails, no noise riser."),
        ("Granular, Retrace", "Grain Storm, Room Bounce, Shatter ... and Time Slip, Rewind, Echo Back ...: grains bouncing in the room, splitting, or going back in time."),
        ("Synced", "Pulse Cloud, Ticker, Stutter Rise, Half Time Haze ...: grains on the beat grid. They lock while the sequencer plays and the pump follows MPC's tempo."),
        ("Medium, Hard", "Sunrise, Big Smile, Tape Rise ... and Hard Grin, Barber Pole, Face Melter, Liftoff, Endless Wow: resonance, drive, a pump, climbing echoes."),
        ("Amount, Level", "On the Settings page. Amount: how many grains (0 = only the preset's own). Level: the cloud's volume. Both sit with the big knobs."),
        ("Q-Links", "They follow the page. Wow page: Wow, Mix, Preset. Settings page: Size to Variation, Retrace to Length, then Amount, Level, Retrace mode, Sync."),
    ]),
    ("Granules page", [
        ("Amount", "How many grains. 0 = only the grains the preset itself makes. Up: a thicker swarm flying about in the room. They only sound while Wow is up."),
        ("Level", "The cloud's volume. 50 % = as the preset has it, up to double, down to silent. Amount adds grains; Level sets how loud they all are."),
        ("Size", "Grain length, x0.25 to x4. At 50 % it is the preset's own. Short grains sparkle, long ones melt into a pad."),
        ("Density", "Grains per second, x0.25 to x4 (50 % = the preset's own). With Sync on it picks the nearest note value."),
        ("Speed", "How fast the grains move through the room (left wall to right wall is also their stereo place). 0 = they stay put."),
        ("Variation", "Sync off: how uneven the spacing of the grains is. Sync on: how far each grain lands off the beat grid."),
        ("Retrace", "The chance that a grain goes back in time while it plays."),
        ("Split", "The chance that a bounce off a wall or another grain splits it: a child at another harmonic and size, a little quieter."),
        ("Time, Length", "Time: how far back a retrace goes, 50 ms to 2 s. Length: how long the retraced piece plays, 40 ms to 1 s."),
        ("Retrace: Jump / Reverse", "Jump: the grain skips back in time and keeps playing forwards. Reverse: it plays backwards from there."),
        ("Sync: Off / On", "On: grains are born on the song's beat grid while the sequencer plays. Off: free-running."),
        ("Presets", "Picking a preset sets all these knobs to its own values. They are yours from then on: change them, and they are saved with the project."),
    ]),
]



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
    SS = 2
    pw, ph = PANEL_BOX[2], PANEL_BOX[3]
    gx, gy, gap_x, gap_y = 24, 76, 12, 8
    cw = (pw - 2 * gx - 2 * gap_x) // 3
    chh = (ph - gy - 18 - 3 * gap_y) // 4
    for n, (title, items) in enumerate(HELP_PAGES):
        im = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        b = 6 * SS
        d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=24 * SS, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=b)
        ft = font(30 * SS)
        d.text((gx * SS, 46 * SS), "Help: " + title, font=ft, fill=(0, 0, 0, 255), anchor="ls")
        d.text((gx * SS + int(ft.getlength("Help: " + title + "    ")), 46 * SS), "tap anywhere else to close", font=font(16 * SS), fill=(90, 90, 90, 255), anchor="ls")
        for (ax, ay, aw, ah), left in ((BPAGE_PREV, True), (BPAGE_NEXT, False)):
            pts = [((ax + aw) * SS, ay * SS), ((ax + aw) * SS, (ay + ah) * SS), (ax * SS, (ay + ah / 2) * SS)] if left else \
                  [(ax * SS, ay * SS), (ax * SS, (ay + ah) * SS), ((ax + aw) * SS, (ay + ah / 2) * SS)]
            d.polygon(pts, fill=(0, 0, 0, 255))
        tx0, ty0, tw, th = BPAGE_TEXT
        fc = font(21 * SS)
        cap = fc.getbbox("H", anchor="ls")
        d.text(((tx0 + tw / 2) * SS, (ty0 + th / 2) * SS + (cap[3] - cap[1]) / 2), "%d / %d" % (n + 1, len(HELP_PAGES)), font=fc, fill=(0, 0, 0, 255), anchor="ms")
        for k, (name, text) in enumerate(items):
            r, c = divmod(k, 3)
            x0, y0 = gx + c * (cw + gap_x), gy + r * (chh + gap_y)
            d.rounded_rectangle([x0 * SS, y0 * SS, (x0 + cw) * SS, (y0 + chh) * SS], radius=14 * SS, fill=PAPER + (255,), outline=(0, 0, 0, 255), width=3 * SS)
            d.text(((x0 + 14) * SS, (y0 + 25) * SS), name, font=font(20 * SS), fill=(0, 0, 0, 255), anchor="ls")
            for px in (15, 14, 13, 12, 11):
                f = font(px * SS)
                lines = wrap_lines(text, f, (cw - 28) * SS)
                if len(lines) * (px + 3) + 36 <= chh:
                    break
            assert len(lines) * (px + 3) + 36 <= chh, "help text too long: " + name
            for j, ln in enumerate(lines):
                d.text(((x0 + 14) * SS, (y0 + 28 + px + j * (px + 3)) * SS), ln, font=f, fill=(0, 0, 0, 255), anchor="ls")
        im.resize((pw, ph), Image.Resampling.LANCZOS).save(os.path.join(OUT, "help_%d.png" % (n + 1)), optimize=True)



# ---- layout.conf ------------------------------------------------------------------------------------------------------------
def write_layout(browser_files):
    def knob(key, cx, cy, r, frames, strip, vs, when, ink="000000"):
        box = 2 * r + 8
        value_h = max(26, round(vs * 1.2))
        vy = -value_h // 2 - r - 2        # ns=0: the value label then sits value_h/2 below the knob's centre
        return ('knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=%s strip=skin_assets/%s frames=%d when=page:%d'
                % (cx, cy + Y_OFF, r, key, vs, vy, box + 2, ink, strip, frames, when))

    ay = BAR_Y + Y_OFF
    px_, py_, pw_, ph_ = PILL
    hx, hy, hd = HELP_BTN
    bx, by, bw, bh = PANEL_BOX
    L = [
        "# Generated by generate_skin.py -- edit the numbers there, not here.",
        "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
        "theme_bg=C6E4FA",
        "theme_ink=000000",
        "theme_ink_dim=000000",
        "theme_accent=000000",
        "",
        "[tab POLIWOW]",
        "art file=skin_assets/bg.png fit=stretch when=page:0",
        "art file=skin_assets/bg_grains.png fit=stretch when=page:1",
        "",
        "# the preset (also MPC's PRESET menu): its name, live, in the white pill between the arrows baked into the backgrounds; a tap on the name opens the browser",
        'readout cx=%d cy=%d w=%d h=40 label="" key=style box=0 tsize=%d tweight=bold tcolor=000000 tpad=0' % (PRE_C, ay, PRE_W - 12, PRESET_TS),
        'button cx=%d cy=%d w=%d h=50 label="" key=style_list img=skin_assets/hit.png img_on=skin_assets/hit.png' % (PRE_C, ay, PRE_W),
    ]
    for ax, key in zip(ARROW_X, ("style_prev", "style_next")):
        L.append('button cx=%d cy=%d w=44 h=52 label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png' % (ax, ay, key))   # 44 wide: neighbours must not overlap
    L += [
        "",
        "# the Settings pill switches the page (yellow on the GRANULES page); two touch areas, one per page",
        'picture x=%d y=%d w=%d h=%d key=page files="skin_assets/pill_0.png,skin_assets/pill_1.png" fit=stretch' % (px_ - pw_ // 2, py_ - ph_ // 2 + Y_OFF, pw_, ph_),
        'button cx=%d cy=%d w=%d h=%d label="" key=tab2 img=skin_assets/hit.png img_on=skin_assets/hit.png when=page:0' % (px_, py_ + Y_OFF, pw_, ph_),
        'button cx=%d cy=%d w=%d h=%d label="" key=tab1 img=skin_assets/hit.png img_on=skin_assets/hit.png when=page:1' % (px_, py_ + Y_OFF, pw_, ph_),
        "",
        "# WOW page: the two faces: 9 o'clock = 0 %, straight up = 50 %, 3 o'clock = 100 %; the number in the middle is the value",
        knob("mix", MIX_POS[0], MIX_POS[1], R_MIX, MIX_FRAMES, "knob_mix.png", MIX_VS, 0),
        knob("wow", WOW_POS[0], WOW_POS[1], R_WOW, WOW_FRAMES, "knob_wow.png", WOW_VS, 0),
        "",
        "# GRANULES page: eight small knobs (one shared strip), the big Amount and Level knobs, two switches; captions are in bg_grains.png",
    ]
    for key, cap, cx, cy in G_KNOBS:
        L.append(knob(key, cx, cy, R_G, G_FRAMES, "knob_grain.png", G_VS, 1, "FFFFFF"))
    L.append(knob("g_amount", AMT_C[0], AMT_C[1], R_L, L_FRAMES, "knob_level.png", L_VS, 1, "FFFFFF"))
    L.append(knob("g_level", LEVEL_C[0], LEVEL_C[1], R_L, L_FRAMES, "knob_level.png", L_VS, 1, "FFFFFF"))
    for key, stem, off, on, cx, cy, w, h in G_TOGGLES:
        L.append('toggle cx=%d cy=%d label="" key=%s ns=0 w=%d h=%d img=skin_assets/%s_off.png img_on=skin_assets/%s_on.png when=page:1'
                 % (cx, cy + Y_OFF, key, w, h, stem, stem))
    L += ["", "# help: the ? button after the title opens a two-page window (every parameter explained); a tap on it closes it",
          'button cx=%d cy=%d w=%d h=%d label="" key=help_btn img=skin_assets/help_btn.png img_on=skin_assets/help_btn_on.png' % (hx, hy + Y_OFF, hd, hd)]
    for n in range(len(HELP_PAGES)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=help_btn img=skin_assets/help_%d.png img_on=skin_assets/help_%d.png when=hview:%d'
                 % (bx + bw // 2, by + bh // 2 + Y_OFF, bw, bh, n + 1, n + 1, n + 1))
    for ax, ay_, aw, ah in (BPAGE_PREV, BPAGE_NEXT):
        for n in range(len(HELP_PAGES)):
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=help_next img=skin_assets/hit.png img_on=skin_assets/hit.png when=hview:%d'
                     % (bx + ax + aw // 2, by + ay_ + ah // 2 + Y_OFF, aw + 20, ah + 14, n + 1))
    bx0, by0, bw0, bh0 = BROWSER_BOX
    L += ["", "# preset browser (on top of everything): one panel picture per (category, page); a tap on its background or on the name closes it"]
    for v, fn in enumerate(browser_files):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=style_list img=skin_assets/%s img_on=skin_assets/%s when=bview:%d'
                 % (bx0 + bw0 // 2, by0 + bh0 // 2 + Y_OFF, bw0, bh0, fn, fn, v + 1))
    L.append("# the current preset's cell")
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/browser_hl.png img_on=skin_assets/browser_hl.png when=pre_hl:%d'
                 % (bx0 + cx0 + CELL_W // 2, by0 + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1, slot + 1))
    L.append("# categories, cells, page arrows (transparent; the panel picture draws them)")
    for i in range(len(CAT_NAMES)):
        y0 = 16 + i * (PILL_H + PILL_GAP)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=cat%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx0 + 20 + SIDE_W // 2, by0 + y0 + PILL_H // 2 + Y_OFF, SIDE_W, PILL_H + PILL_GAP, i + 1))
    for slot in range(12):
        cx0, cy0 = cell_pos(slot)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx0 + cx0 + CELL_W // 2, by0 + cy0 + CELL_H // 2 + Y_OFF, CELL_W, CELL_H, slot + 1))
    for key, (ax, ay_, aw, ah) in (("bpage_prev", BB_PREV), ("bpage_next", BB_NEXT)):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=skin_assets/hit.png img_on=skin_assets/hit.png when=browsing:On'
                 % (bx0 + ax + aw // 2, by0 + ay_ + ah // 2 + Y_OFF, aw + 20, ah + 14, key))
    L += ["",
          "# Q-Links follow the page on screen (the engine's q1..q12: names and values by page). Four to a Q-Link page, top knob first:",
          "# WOW: Wow, Mix, Preset.  GRANULES: Size Density Speed Variation | Retrace Split Time Length | Amount, Level, Retrace mode, Sync",
          'qlinks "POLIWOW" = q1,q2,q3,q4,q5,q6,q7,q8,q9,q10,q11,q12,-,-,-,-', ""]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    m0, m50, m100, clean = (load("Poli-Wow-%s.jpg" % n) for n in ("0", "50", "100", "bg"))
    m_main, m_gran = load(UI_MAIN), load(UI_GRAN)
    for im in (m0, m50, m100, clean, m_main, m_gran):
        assert im.size == (MOCK_W, MOCK_H), "mockups must be 1920x1080"
    make_backgrounds(clean, m_main, m_gran)
    make_mix(m50)
    make_wow(m0, m50, m100)
    make_pointer_knob("knob_grain.png", R_G, G_FRAMES)
    make_pointer_knob("knob_level.png", R_L, L_FRAMES)
    make_widgets()
    make_help()
    write_layout(make_browser())
    print("wrote vst/skin_assets/*.png and vst/layout.conf")

