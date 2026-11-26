#!/usr/bin/env python3
"""Poli-q skin + parameters: turns the Canva mockup (imgs/) into the MPC skin's images AND writes vst/params.json,
vst/layout.conf, src/layout_gen.h and src/presets_gen.h, so every coordinate and every number shared with the engine
lives in this one file.

    python3 generate_skin.py            (run from the Poli-q folder; needs Pillow + numpy)

The page (screen px, 1280x628; the 1920x1080 mockup scaled by 628/1080 and left-aligned, the rest of the width is the
mockup's right edge mirrored):
  * top: "Poli-q", the preset selector beside it, the Analyzer switch, the Q-Link bank switch (1-4 / 5-8), Pass
  * the grid: a true log axis 20 Hz .. 20 kHz (Hz numbers along the bottom), +-18 dB (3 dB per row, dB numbers on the
    left), the output spectrum behind (40 columns, only while Analyzer is on), the EQ curve (80 columns: a line and a
    soft fill to the 0 dB line), and one dot per band in that band's colour (40 places across, hidden while it is off)
  * 8 band sections: Freq / Gain / Q knobs with their value inside, the colour circle (tap: band on/off), the type
    name (tap: the type browser). The bar under four sections shows which four the Q-Links drive.

Engine constants in src/layout_gen.h: kCurveCols, kCurveFrames, kDotPos, kDotFrames, kSpecCols, kSpecFrames.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared"))
import poli_preset_ui as ppu  # noqa: E402  (the preset selector shared by the Poli plugins)

HERE = os.path.dirname(os.path.abspath(__file__))
IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")
SRC = os.path.join(HERE, "src")
FONT = ppu.OPEN_SANS

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H                     # 0.5815
MOCKUP = os.path.join(IMGS, "Poli-q.jpg")
CLEAN = os.path.join(IMGS, "Poli-q-bg.jpg")
SS = 4                             # supersampling

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GREEN = (0, 191, 98)
RED = (255, 49, 49)
# the band colours, measured from the mockup's circles (sections 1..8)
COLORS = [(24, 0, 172), (56, 182, 255), (0, 151, 178), (140, 82, 255), (255, 222, 89), (255, 49, 49),
          (255, 116, 31), (111, 68, 0)]

# ---- mockup geometry (Poli-q.jpg, 1920x1080) ------------------------------------------------------------------------
SEC_X = [161, 364, 567, 770, 973, 1175, 1378, 1581]   # section boxes: left outer edge
SEC_W, SEC_Y0, SEC_Y1 = 175, 775, 1045
# measured centres per section: Freq, Gain, Q knobs and the colour circle (Canva placed them a few px apart)
KNOB_C = [[(206, 837), (409, 837), (611, 839), (814, 839), (1017, 839), (1220, 839), (1426.5, 839), (1628, 839)],
          [(287, 835), (490, 835), (693, 838), (895, 838), (1098.5, 838), (1301, 838), (1508, 838), (1709, 838)],
          [(206, 919.5), (409, 919.5), (611.5, 922), (814, 922), (1017, 922), (1220, 922), (1426.5, 922), (1627.5, 922)]]
CIRCLE_C = [(287, 918.5), (490, 918.5), (693, 921.5), (895.5, 921.5), (1098, 921.5), (1301, 921.5), (1508, 921.5),
            (1709.5, 921.5)]
GRID_OUT = (108, 298, 1811, 747)                       # grid frame, outer edge

# ---- screen geometry ------------------------------------------------------------------------------------------------
GX0, GX1, GY0, GY1 = 70, 1046, 180, 428               # the grid's inner area (20 Hz .. 20 kHz, +18 .. -18 dB)
GW, GH = GX1 - GX0, GY1 - GY0                         # 976 x 248
GYC = (GY0 + GY1) // 2                                # 304: 0 dB
HALF = GH // 2                                        # 124 px = 18 dB
FRAME_W, FRAME_R = 7, 29                              # grid frame width, outer corner radius
INNER_R = FRAME_R - FRAME_W
DB_MAX = 18.0
F_LO, F_HI = 20.0, 20000.0

CURVE_COLS = 80
CURVE_FRAMES = 12288 // HALF                          # 99: each half (above / below 0 dB) is its own meter
DOT_POS = 40
DOT_D = 26                                            # dot diameter on screen (the mockup's 44 px)
DOT_W = DOT_D + 2
DOT_PAD = DOT_D // 2 + 1
DOT_H = GH + 2 * DOT_PAD
DOT_FRAMES = 12288 // DOT_H                           # 44: frames 0..42 = the dot at -18..+18 dB, the last frame = a
                                                      # hollow ring on the 0 dB line (the band is off)
SPEC_COLS = 40
SPEC_FRAMES = 12288 // GH                             # 49
SPEC_W = 20

KNOB_R = 16                                           # the mockup's knobs are 15 px (1.1: back near the mockup's size)
KNOB_FRAMES = 101                                     # odd: the middle frame points straight up (0 dB)
KNOB_BOX = 2 * KNOB_R + 8
KNOB_VS = 9                                           # value text size (MPC's Titillium), units included: "1.5kHz"
KNOB_VY = -33                                         # value text offset: puts it in the knob's middle (checked in the built skin)
CIRCLE_R = 13

# top bar (screen)
PRESET_SEL = (262, 90, 344, 50)
AN_BOX = (616, 90, 150, 50)                           # Visualizer (was "Analyzer"; the key stays "analyzer")
LEARN_BOX = (774, 90, 122, 50)                        # Learn (1.1)
QB_BOX = (906, 92, 116, 46)
PASS_C, PASS_R = (1062, 115), 30
# the dynamics panel right of the grid (1.1): the selected band's Threshold / Range / Attack / Release
DYN_BOX = (1064, 166, 208, 448)                       # x, y, w, h (screen)
DYN_SEL_Y = 236                                       # the band selector row
DYN_KNOB_X = (1114, 1222)                             # 1.1.1: bigger knobs (user: hard to read and to touch), same places
DYN_KNOB_Y = (300, 400)
DYN_KNOB_R = 30                                       # the band knobs stay 16 (the mockup); these have room
DYN_KNOB_BOX = 2 * DYN_KNOB_R + 8
DYN_KNOB_VS = 15                                      # value text in the knob (band knobs: 9)
DYN_KNOB_VY = -(DYN_KNOB_R + 2) - 13 - 2              # centres the value text in the face (as KNOB_VY does for r=16)
DYN_KNOB_BW = 104                                     # touch box width (the two columns are 108 apart)
DYN_LABEL_DY = DYN_KNOB_R + 13                        # the name under each knob
DYN_BAR = (1092, 482, 152, 14)                        # the gain-now bar (x, y, w, h)
VOICE_BOX = (690, 184, 350, 18)                       # Learn's result, top right inside the grid
PRESET_PANEL = (63, 166, 990, 460)
TB_PANEL = (63, 173, 990, 262)                        # the type browser covers the grid

TYPE_NAMES = ["Bell", "Low Shelf", "High Shelf", "Low Cut 6", "Low Cut 12", "Low Cut 24", "Low Cut 48", "High Cut 6",
              "High Cut 12", "High Cut 24", "High Cut 48", "Notch", "Band Pass", "Tilt"]
T = {n: i for i, n in enumerate(TYPE_NAMES)}
# the type browser's columns (indices into TYPE_NAMES)
TB_COLUMNS = [[T["Bell"], T["Notch"], T["Band Pass"], T["Tilt"]], [T["Low Shelf"], T["High Shelf"]],
              [T["Low Cut 6"], T["Low Cut 12"], T["Low Cut 24"], T["Low Cut 48"]],
              [T["High Cut 6"], T["High Cut 12"], T["High Cut 24"], T["High Cut 48"]]]


def sc(v):
    return v * S


def x_of(hz):
    return GX0 + math.log10(hz / F_LO) / 3.0 * GW


def y_of(db):
    return GYC - db / DB_MAX * HALF


def font(px, weight=600):
    f = ImageFont.truetype(FONT, int(round(px)))
    try:
        axes = f.get_variation_axes()
        f.set_variation_by_axes([weight] + [a["default"] for a in axes[1:]])
    except (OSError, AttributeError):
        pass
    return f


def down(im, w, h):
    return im.resize((w, h), Image.Resampling.LANCZOS)


def disc(d, r, fill, ring=None, ring_w=0):
    """an anti-aliased disc of radius r in a (2r+2) square (ring: an outline colour, drawn inside)"""
    n = 2 * r + 2
    im = Image.new("RGBA", (n * SS, n * SS), (0, 0, 0, 0))
    dd = ImageDraw.Draw(im)
    c = n * SS / 2
    dd.ellipse((c - r * SS, c - r * SS, c + r * SS, c + r * SS), fill=fill)
    if ring is not None:
        dd.ellipse((c - r * SS, c - r * SS, c + r * SS, c + r * SS), outline=ring, width=int(ring_w * SS))
    return down(im, n, n)


def text_centered(d, cx, cy, text, f, fill):
    l, t, r, b = d.textbbox((0, 0), text, font=f, anchor="ls")
    cap = f.getbbox("H", anchor="ls")
    d.text((cx - (r - l) / 2 - l, cy + (cap[3] - cap[1]) / 2), text, font=f, fill=fill, anchor="ls")


# ---- presets --------------------------------------------------------------------------------------------------------
# A band: (type, Hz, gain dB, Q) and on (default True). The Init layout follows the mockup: Low Cut, Low Shelf, four
# Bells, High Shelf, High Cut (the two cuts off). A preset names the bands it changes (1..8); the others stay as Init.
# Sources: common practice in mixing guides (iZotope's EQ cheat sheet, Sonicbids, musicguymixing drum EQ chart, Pulsar's
# vocal cheat sheet): high-pass first, cut mud around 200-500 Hz, presence 2-5 kHz, air from 10 kHz with a shelf;
# telephone / radio voices band-limit to ~300 Hz-3.4 kHz with a mid push.
INIT = [("Low Cut 12", 30, 0, 0.71, False), ("Low Shelf", 80, 0, 0.71), ("Bell", 200, 0, 1.0), ("Bell", 500, 0, 1.0),
        ("Bell", 1500, 0, 1.0), ("Bell", 3500, 0, 1.0), ("High Shelf", 8000, 0, 0.71), ("High Cut 12", 16000, 0, 0.71, False)]


NODYN = (-30.0, 0.0, 10.0, 150.0)   # Threshold dB, Range dB, Attack ms, Release ms: Range 0 = a static band


def D(thr, rng, att=10.0, rel=150.0):
    """a band's dynamics (1.1): cut (Range < 0) or boost (> 0) by up to Range while its region is over Threshold"""
    return ("dyn", (float(thr), float(rng), float(att), float(rel)))


def P(name, **bands):
    """a preset: bands it changes as b<n>=(type, Hz, gain, Q[, on][, D(...)]); the rest as Init. Each band comes out
    as (type, Hz, gain, Q, on, dyn)."""
    def norm(v):
        v = list(v)
        dyn = NODYN
        if v and isinstance(v[-1], tuple) and v[-1][0] == "dyn":
            dyn = v.pop()[1]
        if len(v) == 4:
            v.append(True)
        return tuple(v) + (dyn,)
    b = [norm(x) for x in INIT]
    for k, v in bands.items():
        b[int(k[1:]) - 1] = norm(v)
    return name, b


PRESETS = [
    P("Init"),
    # vocals
    # vocals (1.1): every one high-passes at 150 Hz (the usual vocal low cut; Learn moves it to just under the singer's
    # lowest notes), and uses dynamic bands where a fixed cut would dull the voice all the time: a de-esser (5-8 kHz,
    # fast attack, short release: only the s / sh / t are cut), a mud / proximity band that only dips when the low mids
    # build up (closer to the mic, lower notes). Their Thresholds are typical for a vocal around -18 dBFS; Learn sets
    # them from the voice itself.
    P("Vox: Pop Vocal", b1=("Low Cut 24", 150, 0, 0.71), b2=("Bell", 250, -1.5, 1.2),
      b3=("Bell", 400, 0, 1.5, D(-30, -4, 10, 150)), b4=("Bell", 900, 0, 1.0), b5=("Bell", 3000, 2.5, 1.0),
      b6=("Bell", 6500, 0, 3.0, D(-32, -6, 1, 60)), b7=("High Shelf", 12000, 3, 0.71)),
    P("Vox: Rap Vocal", b1=("Low Cut 24", 150, 0, 0.71), b2=("Bell", 200, 1.5, 1.0),
      b3=("Bell", 350, -2, 1.4, D(-30, -3, 10, 120)), b4=("Bell", 800, -1, 1.5), b5=("Bell", 2500, 3, 1.2),
      b6=("Bell", 6000, 0, 3.0, D(-30, -6, 1, 60)), b7=("High Shelf", 10000, 2.5, 0.71), b8=("High Cut 12", 18000, 0, 0.71)),
    P("Vox: R&B Vocal", b1=("Low Cut 12", 150, 0, 0.71), b2=("Low Shelf", 220, 1, 0.71),
      b3=("Bell", 300, -1.5, 1.2, D(-30, -2.5, 15, 200)), b4=("Bell", 1200, -1, 1.0), b5=("Bell", 3500, 1.5, 1.0),
      b6=("Bell", 7000, 0, 3.0, D(-32, -5, 1, 80)), b7=("High Shelf", 12000, 3.5, 0.71)),
    P("Vox: Podcast Voice", b1=("Low Cut 24", 150, 0, 0.71), b2=("Bell", 180, 1.5, 1.0),
      b3=("Bell", 300, -2, 1.2, D(-28, -3, 10, 200)), b5=("Bell", 3000, 2, 1.0),
      b6=("Bell", 5500, 0, 3.0, D(-30, -5, 1, 60)), b7=("High Shelf", 10000, 1, 0.71), b8=("High Cut 12", 15000, 0, 0.71)),
    P("Vox: Vocal Air", b1=("Low Cut 12", 150, 0, 0.71), b5=("Bell", 5000, 1, 1.0),
      b6=("Bell", 7000, 0, 3.0, D(-32, -6, 1, 60)), b7=("High Shelf", 12000, 5, 0.6)),
    P("Vox: Vocal De-Mud", b1=("Low Cut 24", 150, 0, 0.71), b3=("Bell", 250, -2.5, 1.3, D(-30, -3, 10, 150)),
      b4=("Bell", 450, -2.5, 1.6)),
    P("Vox: Backing Vocals", b1=("Low Cut 24", 200, 0, 0.71), b2=("Low Shelf", 250, -2, 0.71), b3=("Bell", 400, -2, 1.0),
      b5=("Bell", 2500, -2, 1.0), b6=("Bell", 6000, 0, 3.0, D(-34, -6, 1, 60)), b7=("High Shelf", 10000, 2, 0.71),
      b8=("High Cut 12", 16000, 0, 0.71)),
    # drums
    P("Drm: Kick Punch", b1=("Low Cut 24", 30, 0, 0.71), b2=("Bell", 60, 4, 1.2), b3=("Bell", 300, -4, 1.5),
      b4=("Bell", 600, -1, 1.0), b5=("Bell", 3500, 4, 1.5), b7=("High Shelf", 10000, -2, 0.71), b8=("High Cut 12", 14000, 0, 0.71)),
    P("Drm: Kick Sub", b1=("Low Cut 24", 25, 0, 0.71), b2=("Low Shelf", 60, 4, 0.71), b3=("Bell", 250, -3, 1.2),
      b4=("Bell", 450, -2, 1.5), b5=("Bell", 2000, -1, 1.0), b6=("Bell", 4500, 1.5, 1.5), b7=("High Shelf", 9000, -3, 0.71),
      b8=("High Cut 24", 9000, 0, 0.71)),
    P("Drm: Snare Crack", b1=("Low Cut 24", 90, 0, 0.71), b2=("Bell", 200, 3, 1.4), b3=("Bell", 450, -3, 1.5),
      b4=("Bell", 900, -1, 2.0), b5=("Bell", 2500, 2, 1.2), b6=("Bell", 5000, 3, 1.2), b7=("High Shelf", 10000, 2, 0.71)),
    P("Drm: Claps", b1=("Low Cut 24", 150, 0, 0.71), b4=("Bell", 1000, -2, 1.2), b5=("Bell", 2000, 2, 1.0),
      b7=("High Shelf", 8000, 3, 0.71)),
    P("Drm: Hi-Hats", b1=("Low Cut 24", 300, 0, 0.71), b4=("Bell", 600, -2, 1.0), b6=("Bell", 4000, -1.5, 2.0),
      b7=("High Shelf", 10000, 3, 0.71)),
    P("Drm: Overheads", b1=("Low Cut 12", 100, 0, 0.71), b3=("Bell", 400, -2, 1.2), b5=("Bell", 2500, -1.5, 1.5),
      b7=("High Shelf", 10000, 2.5, 0.71)),
    P("Drm: Drum Bus", b1=("Low Cut 12", 25, 0, 0.71), b2=("Low Shelf", 70, 2, 0.71), b3=("Bell", 300, -2, 1.0),
      b5=("Bell", 3000, 1.5, 1.0), b7=("High Shelf", 10000, 2, 0.71)),
    # bass
    P("Bass: 808 Clean", b1=("Low Cut 24", 28, 0, 0.71), b2=("Low Shelf", 55, 2, 0.71), b3=("Bell", 250, -3, 1.2),
      b4=("Bell", 500, -2, 1.0), b5=("Bell", 1000, 1.5, 1.2), b7=("High Shelf", 8000, -3, 0.71), b8=("High Cut 24", 6000, 0, 0.71)),
    P("Bass: 808 Knock", b1=("Low Cut 24", 30, 0, 0.71), b2=("Bell", 60, 2, 1.0), b3=("Bell", 200, -2, 1.0),
      b4=("Bell", 800, 3, 1.0), b5=("Bell", 1500, 2, 1.5), b8=("High Cut 24", 8000, 0, 0.71)),
    P("Bass: Bass Guitar", b1=("Low Cut 24", 35, 0, 0.71), b2=("Bell", 80, 2, 1.0), b3=("Bell", 250, -3, 1.2),
      b4=("Bell", 700, 1.5, 1.4), b5=("Bell", 2000, 2, 1.5), b7=("High Shelf", 7000, -2, 0.71), b8=("High Cut 12", 10000, 0, 0.71)),
    P("Bass: Synth Bass", b1=("Low Cut 24", 30, 0, 0.71), b2=("Low Shelf", 90, 1.5, 0.71), b3=("Bell", 300, -2.5, 1.0),
      b5=("Bell", 1500, 2, 1.2), b8=("High Cut 24", 8000, 0, 0.71)),
    # instruments
    P("Inst: Acoustic Guitar", b1=("Low Cut 24", 90, 0, 0.71), b3=("Bell", 200, -3, 1.2), b4=("Bell", 800, -1, 1.5),
      b5=("Bell", 3000, 2, 1.2), b7=("High Shelf", 10000, 3, 0.71)),
    P("Inst: Electric Guitar", b1=("Low Cut 24", 100, 0, 0.71), b3=("Bell", 250, -2, 1.2), b4=("Bell", 800, 1, 1.0),
      b5=("Bell", 2500, 2, 1.4), b6=("Bell", 4500, -2, 2.5), b8=("High Cut 12", 9000, 0, 0.71)),
    P("Inst: Piano", b1=("Low Cut 12", 40, 0, 0.71), b3=("Bell", 250, -2, 1.0), b5=("Bell", 3000, 2, 1.0),
      b7=("High Shelf", 10000, 2, 0.71)),
    P("Inst: Rhodes & Keys", b1=("Low Cut 12", 60, 0, 0.71), b2=("Low Shelf", 150, 1.5, 0.71), b3=("Bell", 400, -2, 1.0),
      b5=("Bell", 2500, 1.5, 1.0), b7=("High Shelf", 8000, -1.5, 0.71)),
    P("Inst: Pads & Strings", b1=("Low Cut 24", 150, 0, 0.71), b3=("Bell", 400, -2, 1.0), b5=("Bell", 2500, -1.5, 1.0),
      b7=("High Shelf", 10000, 2, 0.71)),
    P("Inst: Synth Lead", b1=("Low Cut 24", 150, 0, 0.71), b3=("Bell", 400, -2, 1.0), b5=("Bell", 2000, 2, 1.0),
      b6=("Bell", 5000, 1, 1.0), b7=("High Shelf", 10000, 1.5, 0.71)),
    # tone
    P("Tone: Airy", b1=("Low Cut 12", 30, 0, 0.71), b6=("Bell", 5000, 1, 1.0), b7=("High Shelf", 12000, 5, 0.6)),
    P("Tone: Darker", b6=("Bell", 3500, -2, 1.0), b7=("High Shelf", 6000, -5, 0.6), b8=("High Cut 12", 12000, 0, 0.71)),
    P("Tone: Warm", b2=("Low Shelf", 150, 3, 0.6), b6=("Bell", 3500, -1.5, 1.0), b7=("High Shelf", 9000, -2.5, 0.71)),
    P("Tone: Bright", b5=("Bell", 2500, 2, 1.0), b7=("High Shelf", 7000, 4, 0.6)),
    P("Tone: Smile Curve", b2=("Low Shelf", 100, 4, 0.71), b4=("Bell", 600, -3, 0.7), b7=("High Shelf", 9000, 4, 0.71)),
    P("Tone: Clarity", b1=("Low Cut 12", 40, 0, 0.71), b3=("Bell", 300, -3, 1.0), b5=("Bell", 2500, 2, 1.0),
      b7=("High Shelf", 10000, 1.5, 0.71)),
    P("Tone: Mid Focus", b2=("Low Shelf", 200, -3, 0.71), b4=("Bell", 1000, 3, 0.8), b7=("High Shelf", 6000, -3, 0.71)),
    # mix / master
    P("Mix: Master Polish", b1=("Low Cut 24", 25, 0, 0.71), b2=("Low Shelf", 60, 1, 0.71), b3=("Bell", 250, -1, 1.0),
      b5=("Bell", 3000, 0.5, 0.8), b7=("High Shelf", 12000, 1.5, 0.6)),
    P("Mix: Low End Tidy", b1=("Low Cut 24", 30, 0, 0.71), b3=("Bell", 200, -1.5, 1.2), b4=("Bell", 350, -1.5, 1.5)),
    P("Mix: Sample Clean", b1=("Low Cut 24", 30, 0, 0.71), b3=("Bell", 300, -2, 1.0), b7=("High Shelf", 10000, 1.5, 0.71),
      b8=("High Cut 12", 18000, 0, 0.71)),
    P("Mix: Loop Sit Back", b1=("Low Cut 24", 120, 0, 0.71), b5=("Bell", 2500, -3, 0.7), b7=("High Shelf", 9000, -2, 0.71)),
    # effects
    P("FX: Telephone", b1=("Low Cut 48", 400, 0, 0.71), b5=("Bell", 1500, 4, 1.2), b8=("High Cut 48", 3400, 0, 0.71)),
    P("FX: AM Radio", b1=("Low Cut 24", 250, 0, 0.71), b4=("Bell", 1200, 4, 0.8), b8=("High Cut 24", 4500, 0, 0.71)),
    P("FX: Lo-Fi", b1=("Low Cut 12", 120, 0, 0.71), b3=("Bell", 400, 2, 1.0), b7=("High Shelf", 5000, -6, 0.71),
      b8=("High Cut 12", 7000, 0, 0.71)),
    P("FX: Walkie Talkie", b1=("Low Cut 48", 600, 0, 0.71), b5=("Bell", 2000, 6, 1.5), b8=("High Cut 48", 2800, 0, 0.71)),
    P("FX: Megaphone", b1=("Low Cut 48", 700, 0, 0.71), b5=("Bell", 1800, 8, 2.0), b8=("High Cut 48", 4500, 0, 0.71)),
    P("FX: Underwater", b2=("Low Shelf", 100, 3, 0.71), b8=("High Cut 24", 400, 0, 2.0)),
    P("FX: Cassette", b1=("Low Cut 12", 40, 0, 0.71), b2=("Low Shelf", 100, 2, 0.71), b7=("High Shelf", 8000, -4, 0.71),
      b8=("High Cut 12", 13000, 0, 0.71)),
]
PRESET_GROUPS = [("Start", ""), ("Vocals", "Vox: "), ("Drums", "Drm: "), ("Bass", "Bass: "), ("Instruments", "Inst: "),
                 ("Tone", "Tone: "), ("Mix", "Mix: "), ("FX", "FX: ")]


def preset_columns():
    idx = {g: [] for g, _ in PRESET_GROUPS}
    for i, (name, _) in enumerate(PRESETS):
        g = next((g for g, p in PRESET_GROUPS if p and name.startswith(p)), "Start")
        idx[g].append(i)
    col = lambda *gs: sum(([("h", g)] + [("p", i) for i in idx[g]] for g in gs), [])
    return [col("Start", "Mix"), col("Vocals"), col("Drums"), col("Bass"), col("Instruments"), col("Tone"), col("FX")]


def check_presets():
    for name, bands in PRESETS:
        assert len(name) <= 24, name
        assert len(bands) == 8
        for t, hz, g, q, on, (thr, rng, att, rel) in bands:
            assert t in T and 20 <= hz <= 20000 and -18 <= g <= 18 and 0.1 <= q <= 18, (name, t, hz, g, q)
            assert -60 <= thr <= 0 and -18 <= rng <= 18 and 0.5 <= att <= 200 and 10 <= rel <= 2000, (name, thr, rng)
            assert rng == 0 or t in ("Bell", "Low Shelf", "High Shelf", "Band Pass", "Tilt"), (name, t)
        if name.startswith("Vox: "):   # user, 2026-11-05: vocal presets high-pass at 150 Hz
            assert bands[0][0].startswith("Low Cut") and bands[0][4] and bands[0][1] >= 150, name


def write_presets_header():
    rows = []
    for name, bands in PRESETS:
        fl = lambda v: repr(float(v)) + "f"
        bs = ", ".join("{%d, %d, %s, %s, %s, {%s, %s, %s, %s}}" % (T[t], 1 if on else 0, fl(hz), fl(g), fl(q), fl(d[0]), fl(d[1]),
                                                                   fl(d[2]), fl(d[3])) for t, hz, g, q, on, d in bands)
        rows.append('    {"%s", {%s}},' % (name, bs))
    h = ["// Generated by generate_skin.py (PRESETS) -- edit that, not this.", "#pragma once",
         "struct PresetDyn { float thr, range, att, rel; };",
         "struct PresetBand { int type, on; float hz, gain, q; PresetDyn dyn; };",
         "struct PresetDef { const char *name; PresetBand band[8]; };",
         "static const PresetDef kPresets[] = {", *rows, "};",
         "static const int kNumPresets = %d;" % len(PRESETS), ""]
    with open(os.path.join(SRC, "presets_gen.h"), "w", newline="\n") as f:
        f.write("\n".join(h))


def write_presets_md():
    L = ["# Poli-q factory presets", "", "Generated by generate_skin.py (PRESETS): edit that, not this. Starting points: "
         "high-pass first, cut mud around 200-500 Hz, presence 2-5 kHz, air from 10 kHz with a shelf (common mixing-guide "
         "practice); the FX voices band-limit like a phone or radio. Bands not listed are as Init (Bells at 0 dB do nothing; "
         "the two cuts are off).", ""]
    init = PRESETS[0][1]
    for name, bands in PRESETS:
        L.append("## " + name)
        rows = []
        for i, (t, hz, g, q, on, d) in enumerate(bands):
            if name != "Init" and (t, hz, g, q, on, d) == tuple(init[i]):
                continue
            hzs = "%g Hz" % hz if hz < 1000 else "%g kHz" % (hz / 1000)
            gs = "%+g dB" % g if t in ("Bell", "Low Shelf", "High Shelf", "Band Pass", "Tilt") else "-"
            ds = "%+g dB over %g dB (%g / %g ms)" % (d[1], d[0], d[2], d[3]) if d[1] else ""
            rows.append("| %d | %s | %s | %s | %s | %s | %s |" % (i + 1, t, hzs, gs, "%g" % q, "on" if on else "off", ds))
        L += ["| Band | Type | Freq | Gain | Q | | Dynamics |", "|---|---|---|---|---|---|---|"] + rows + [""]
    with open(os.path.join(HERE, "PRESETS.md"), "w", newline="\n") as f:
        f.write("\n".join(L))


def write_layout_header():
    h = ["// Generated by generate_skin.py -- the display geometry the engine has to match.", "#pragma once",
         "static const int kCurveCols = %d;    // curve columns across the grid (cv01..)" % CURVE_COLS,
         "static const int kCurveFrames = %d;  // frames per curve half (each half is one meter)" % CURVE_FRAMES,
         "static const int kDotPos = %d;       // places a dot can be shown at (dx = 1..kDotPos, 0 = hidden)" % DOT_POS,
         "static const int kDotFrames = %d;    // dot strip frames (dy): the last one = off (hollow ring at 0 dB)" % DOT_FRAMES,
         "static const int kSpecCols = %d;     // spectrum columns (sp01..)" % SPEC_COLS,
         "static const int kSpecFrames = %d;   // spectrum strip frames" % SPEC_FRAMES,
         "static const int kDactFrames = %d;   // the dynamics panel's gain-now bar" % DACT_FRAMES,
         "static const int kLearnFrames = %d;  // Learn's progress bar" % LEARN_FRAMES,
         "static const float kAttLo = %.1ff, kAttHi = %.1ff, kRelLo = %.1ff, kRelHi = %.1ff;   // dynamics knobs (ms, log)"
         % (ATT_LO, ATT_HI, REL_LO, REL_HI), ""]
    with open(os.path.join(SRC, "layout_gen.h"), "w", newline="\n") as f:
        f.write("\n".join(h))


# ---- parameters -----------------------------------------------------------------------------------------------------
def hz_pos(hz):
    return 100 * math.log10(hz / F_LO) / 3


def q_pos(q):
    return 100 * math.log(q / 0.1) / math.log(180)


def write_params():
    P_ = []
    init = PRESETS[0][1]
    for b in range(8):
        t, hz, g, q, on, _ = init[b]
        n = b + 1
        P_ += [{"key": "f%d" % n, "name": "Freq %d" % n, "min": 0, "max": 100, "default": round(hz_pos(hz), 4),
                "dynamic_display": True},
               {"key": "g%d" % n, "name": "Gain %d" % n, "min": -18, "max": 18, "default": g, "dynamic_display": True},
               {"key": "q%d" % n, "name": "Q %d" % n, "min": 0, "max": 100, "default": round(q_pos(q), 4),
                "dynamic_display": True},
               {"key": "t%d" % n, "name": "Type %d" % n, "options": TYPE_NAMES, "default": T[t], "qlink_ticks": 4},
               {"key": "on%d" % n, "name": "Band %d" % n, "options": ["Off", "On"], "default": 1 if on else 0,
                "refresh_display": True, "qlink_ticks": 4}]
    P_ += [{"key": "bypass", "name": "Pass", "options": ["Off", "On"], "default": 0, "refresh_display": True,
            "qlink_ticks": 4},
           {"key": "qbank", "name": "Q-Link Bands", "options": ["1-4", "5-8"], "default": 0, "refresh_display": True,
            "qlink_ticks": 4},
           {"key": "analyzer", "name": "Visualizer", "options": ["Off", "On"], "default": 1, "refresh_display": True,
            "qlink_ticks": 4}]
    # the Q-Links: band c (bank 1-4) or c+4 (bank 5-8); names follow the bank ("5 Freq")
    for c in range(4):
        t, hz, g, q, on, _ = init[c]
        n = c + 1
        P_ += [{"key": "qf%d" % n, "name": "%d Freq" % n, "min": 0, "max": 100, "default": round(hz_pos(hz), 4),
                "dynamic_display": True, "dynamic_name": True},
               {"key": "qg%d" % n, "name": "%d Gain" % n, "min": -18, "max": 18, "default": g, "dynamic_display": True,
                "dynamic_name": True},
               {"key": "qq%d" % n, "name": "%d Q" % n, "min": 0, "max": 100, "default": round(q_pos(q), 4),
                "dynamic_display": True, "dynamic_name": True},
               {"key": "qt%d" % n, "name": "%d Type" % n, "options": TYPE_NAMES, "default": T[t], "qlink_ticks": 4,
                "dynamic_name": True}]
    # display only (the engine sets them; the wrapper reports them through display_rev)
    for b in range(8):
        P_.append({"key": "dx%d" % (b + 1), "name": "Dot %d" % (b + 1), "options": ["-"] + [str(i + 1) for i in range(DOT_POS)],
                   "default": 0, "live": True})
    for b in range(8):
        P_.append({"key": "dy%d" % (b + 1), "name": "Dot %d dB" % (b + 1), "min": -18, "max": 18, "default": 0, "live": True})
    for j in range(CURVE_COLS):
        P_.append({"key": "cv%02d" % (j + 1), "name": "Curve %d" % (j + 1), "min": -18, "max": 18, "default": 0, "live": True})
    for j in range(SPEC_COLS):
        P_.append({"key": "sp%02d" % (j + 1), "name": "Spectrum %d" % (j + 1), "min": 0, "max": 1, "default": 0, "live": True})
    # the type browser
    mom = lambda k, nm: {"key": k, "name": nm, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}
    P_ += [{"key": "tb_open", "name": "Type list", "options": ["Off", "On"], "default": 0, "live": True},
           {"key": "tsel", "name": "Type band", "options": ["-"] + [str(i + 1) for i in range(8)], "default": 0, "live": True},
           {"key": "tb_hl", "name": "Type shown", "options": ["-"] + [str(i + 1) for i in range(len(TYPE_NAMES))],
            "default": 0, "live": True}]
    P_ += [mom("topen%d" % (b + 1), "Type %d list" % (b + 1)) for b in range(8)]
    P_ += [mom("tpick%d" % (i + 1), "Pick %s" % TYPE_NAMES[i]) for i in range(len(TYPE_NAMES))]
    P_ += [mom("tb_close", "Type list close")]
    for p in P_:   # the engine reads positions / dB as plain numbers; no unit text (the _display text has them)
        p.setdefault("unit", "") if "options" not in p else None
    path = os.path.join(VST, "params.json")
    with open(path, "w", newline="\n") as f:
        json.dump({"params": P_}, f, indent=1)
        f.write("\n")
    names = [n for n, _ in PRESETS]
    ppu.ensure_params(path, names)
    # 1.1, appended after the preset selector's (append only): Learn, the dynamics panel
    d = json.load(open(path))
    have = {p["key"] for p in d["params"]}
    for p in learn_dyn_params():
        if p["key"] not in have:
            d["params"].append(p)
    with open(path, "w", newline="\n") as f:
        json.dump(d, f, indent=1)
        f.write("\n")
    return path


ATT_LO, ATT_HI = 0.5, 200.0     # Attack knob: 0.5 .. 200 ms, log (position 0..100)
REL_LO, REL_HI = 10.0, 2000.0   # Release knob: 10 .. 2000 ms, log
DACT_FRAMES = 37                # the gain-now bar: -18 .. +18 dB in 1 dB steps
LEARN_FRAMES = 21               # Learn's progress bar


def log_pos(v, lo, hi):
    return 100 * math.log(v / lo) / math.log(hi / lo)


def learn_dyn_params():
    mom = lambda k, nm: {"key": k, "name": nm, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}
    return [
        {"key": "learn", "name": "Learn", "options": ["Off", "On"], "default": 0, "refresh_display": True,
         "qlink_ticks": 4},
        {"key": "learn_prog", "name": "Learn progress", "min": 0, "max": 1, "default": 0, "live": True},
        {"key": "voice", "name": "Voice", "display": "string", "live": True},
        {"key": "dsel", "name": "Dynamics band", "options": [str(i + 1) for i in range(8)], "default": 0,
         "refresh_display": True, "qlink_ticks": 4},
        mom("dsel_prev", "Dyn band <"), mom("dsel_next", "Dyn band >"),
        {"key": "dthr", "name": "Threshold", "min": -60, "max": 0, "default": -30, "dynamic_display": True, "unit": ""},
        {"key": "drng", "name": "Range", "min": -18, "max": 18, "default": 0, "dynamic_display": True, "unit": ""},
        {"key": "datt", "name": "Attack", "min": 0, "max": 100, "default": round(log_pos(10, ATT_LO, ATT_HI), 4),
         "dynamic_display": True, "unit": ""},
        {"key": "drel", "name": "Release", "min": 0, "max": 100, "default": round(log_pos(150, REL_LO, REL_HI), 4),
         "dynamic_display": True, "unit": ""},
        {"key": "dact", "name": "Dynamic gain", "min": -18, "max": 18, "default": 0, "live": True, "unit": ""},
    ]


# ---- background -----------------------------------------------------------------------------------------------------
def build_bg(mock, clean):
    page = clean.copy()
    # the sections (and their numbers) come from the mockup; then the live parts are taken out of them
    page.paste(mock.crop((0, 760, MOCK_W, MOCK_H)), (0, 760))
    d = ImageDraw.Draw(page)
    for x in SEC_X:   # everything inside the box above the type name's underline goes; the widgets replace it
        d.rounded_rectangle((x + 17, 793, x + SEC_W - 17, 992), radius=30, fill=WHITE)
    small = page.resize((round(MOCK_W * S), H), Image.Resampling.LANCZOS)
    bg = Image.new("RGB", (W, H), WHITE)
    bg.paste(small, (0, 0))
    # the right edge (past the mockup): the clean background's last columns (right of the grid), stretched
    sw = small.width
    edge = clean.resize((round(MOCK_W * S), H), Image.Resampling.LANCZOS).crop((sw - 52, 0, sw, H))
    bg.paste(edge.resize((W - sw, H), Image.Resampling.BICUBIC), (sw, 0))
    bg = bg.convert("RGBA")
    bg.alpha_composite(grid_panel())
    bg.alpha_composite(section_labels())
    bg.alpha_composite(dyn_panel())
    return bg


def dyn_panel():
    """the dynamics panel, static parts: a box like the band sections, its title, the knob labels, the bar's scale"""
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    x, y, w, h = DYN_BOX
    d.rounded_rectangle((x * SS, y * SS, (x + w) * SS, (y + h) * SS), radius=26 * SS, fill=BLACK + (255,))
    d.rounded_rectangle(((x + 7) * SS, (y + 7) * SS, (x + w - 7) * SS, (y + h - 7) * SS), radius=20 * SS, fill=WHITE + (255,))
    cx = x + w / 2
    text_centered(d, cx * SS, (y + 32) * SS, "Dynamics", font(21 * SS, 800), BLACK + (255,))
    f = font(14 * SS, 700)
    for (kx, ky), name in zip([(DYN_KNOB_X[0], DYN_KNOB_Y[0]), (DYN_KNOB_X[1], DYN_KNOB_Y[0]),
                               (DYN_KNOB_X[0], DYN_KNOB_Y[1]), (DYN_KNOB_X[1], DYN_KNOB_Y[1])],
                              ["Threshold", "Range", "Attack", "Release"]):
        text_centered(d, kx * SS, (ky + DYN_LABEL_DY) * SS, name, f, BLACK + (255,))
    bx, by, bw, bh = DYN_BAR
    d.rounded_rectangle(((bx - 2) * SS, (by - 2) * SS, (bx + bw + 2) * SS, (by + bh + 2) * SS), radius=5 * SS,
                        outline=BLACK + (255,), width=2 * SS, fill=(236, 241, 246, 255))
    fs = font(11 * SS, 650)
    for v, lab in ((-18, "-18"), (0, "0"), (18, "+18")):
        tx = bx + (v + 18) / 36 * bw
        text_centered(d, tx * SS, (by + bh + 13) * SS, lab, fs, (90, 90, 90, 255))
    text_centered(d, cx * SS, (by - 13) * SS, "Gain now (dB)", font(12 * SS, 700), BLACK + (255,))
    fh = font(11 * SS, 600)
    for i, line in enumerate(["Range < 0 cuts, > 0 boosts", "over the Threshold.", "Bell, Shelf, Band Pass, Tilt"]):
        text_centered(d, cx * SS, (by + 44 + i * 16) * SS, line, fh, (70, 70, 70, 255))
    return down(im, W, H)


def dyn_images():
    """the band selector (a pill in the band's colour, one per band: a picture), its arrows, the gain-now bar strip"""
    # 1.1.2: ONE filmstrip drawn by a meter (frame = the band) instead of 8 swapped pictures. A `picture` is a crop baked into
    # the background layer and MPC swaps it on a band change; swapping left black bars on the pill (partial, or over the whole
    # pill) just like the type names did before 1.1.0. A meter frame is drawn over the page like the type names are.
    pw, ph = 104, 34
    strip = Image.new("RGBA", (pw, ph * len(COLORS)), (0, 0, 0, 0))
    for b, c in enumerate(COLORS):
        im = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((0, 0, pw * SS - 1, ph * SS - 1), radius=ph * SS / 2, fill=c + (255,))
        light = sum(c) > 500
        text_centered(d, pw * SS / 2, ph * SS / 2, "Band %d" % (b + 1), font(18 * SS, 800),
                      (BLACK if light else WHITE) + (255,))
        strip.paste(down(im, pw, ph), (0, b * ph))
    strip.save(os.path.join(OUT, "dsel_strip.png"), optimize=True)
    files = ["skin_assets/dsel_strip.png"]
    aw = 30
    for side in ("prev", "next"):
        for on in (0, 1):
            im = Image.new("RGBA", (aw * SS, ph * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            cx, cy, s_ = aw * SS / 2, ph * SS / 2, 9 * SS
            pts = [(cx + s_ * 0.7, cy - s_), (cx + s_ * 0.7, cy + s_), (cx - s_ * 0.8, cy)] if side == "prev" else \
                  [(cx - s_ * 0.7, cy - s_), (cx - s_ * 0.7, cy + s_), (cx + s_ * 0.8, cy)]
            d.polygon(pts, fill=((56, 182, 255) if on else BLACK) + (255,))
            down(im, aw, ph).save(os.path.join(OUT, "dsel_%s%s.png" % (side, "_on" if on else "")))
    bx, by, bw, bh = DYN_BAR
    strip = Image.new("RGBA", (bw, bh * DACT_FRAMES), (0, 0, 0, 0))
    for k in range(DACT_FRAMES):
        v = -18 + k   # dB
        im = Image.new("RGBA", (bw * SS, bh * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        x0, x1 = (bw / 2) * SS, (bw / 2 + v / 36 * bw) * SS
        col = (255, 49, 49) if v < 0 else GREEN
        if v != 0:
            d.rectangle((min(x0, x1), 0, max(x0, x1), bh * SS), fill=col + (255,))
        d.rectangle((x0 - 1 * SS, 0, x0 + 1 * SS, bh * SS), fill=BLACK + (255,))
        strip.paste(down(im, bw, bh), (0, k * bh))
    strip.save(os.path.join(OUT, "dact.png"), optimize=True)
    return files, pw, ph, aw


def section_labels():
    """Freq / Gain under the top knobs (the units are in the knobs' text), Q / On under the bottom row (the circle switches the band on/off)"""
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    f, fu = font(9 * SS, 600), font(8 * SS, 600)
    for b in range(8):
        for (cx, cy), (name, unit) in zip([knob_xy(b, 0), knob_xy(b, 1), knob_xy(b, 2), circle_xy(b)],
                                          [("Freq", ""), ("Gain", ""), ("Q", ""), ("On", "")]):
            y = cy + LABEL_DY
            w1 = d.textlength(name, font=f)
            w2 = d.textlength(" " + unit, font=fu) if unit else 0
            x0 = cx * SS - (w1 + w2) / 2
            cap = f.getbbox("H", anchor="ls")
            base = y * SS + (cap[3] - cap[1]) / 2
            d.text((x0, base), name, font=f, fill=BLACK + (255,), anchor="ls")
            if unit:
                d.text((x0 + w1, base), " " + unit, font=fu, fill=(105, 105, 105, 255), anchor="ls")
    return down(im, W, H)


def grid_panel():
    """the grid, drawn at screen scale: black frame, white inside, log frequency lines, 3 dB rows, labels"""
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ox0, oy0, ox1, oy1 = GX0 - FRAME_W, GY0 - FRAME_W, GX1 + FRAME_W, GY1 + FRAME_W
    d.rounded_rectangle((ox0 * SS, oy0 * SS, ox1 * SS, oy1 * SS), radius=FRAME_R * SS, fill=BLACK + (255,))
    d.rounded_rectangle((GX0 * SS, GY0 * SS, GX1 * SS, GY1 * SS), radius=INNER_R * SS, fill=WHITE + (255,))
    minor, major = (222, 222, 222, 255), (178, 178, 178, 255)
    for dec in (10, 100, 1000, 10000):
        for m in range(1, 10):
            hz = dec * m
            if hz <= F_LO or hz >= F_HI:
                continue
            x = x_of(hz) * SS
            col = major if m == 1 else minor
            wdt = (1.6 if m == 1 else 1.0) * SS
            d.rectangle((x - wdt / 2, GY0 * SS, x + wdt / 2, GY1 * SS), fill=col)
    for k in range(1, 12):
        y = (GY0 + k * GH / 12) * SS
        if k == 6:
            continue
        col = major if k in (2, 4, 8, 10) else minor
        d.rectangle((GX0 * SS, y - 0.5 * SS, GX1 * SS, y + 0.5 * SS), fill=col)
    # 0 dB line, the mockup's dark grey
    d.rectangle((GX0 * SS, (GYC - 1.1) * SS, GX1 * SS, (GYC + 1.1) * SS), fill=(110, 110, 110, 255))
    # clip the lines to the rounded inside
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((GX0 * SS, GY0 * SS, GX1 * SS, GY1 * SS), radius=INNER_R * SS, fill=255)
    inner = Image.new("RGBA", im.size, (0, 0, 0, 0))
    inner.paste(im, (0, 0), mask)
    frame = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(frame).rounded_rectangle((ox0 * SS, oy0 * SS, ox1 * SS, oy1 * SS), radius=FRAME_R * SS, fill=BLACK + (255,))
    frame.alpha_composite(inner)
    # numbers: semi-transparent, small
    d = ImageDraw.Draw(frame)
    fl = font(11 * SS, 650)
    ink = (0, 0, 0, 120)
    for hz, t in ((50, "50"), (100, "100"), (200, "200"), (500, "500"), (1000, "1k"), (2000, "2k"), (5000, "5k"),
                  (10000, "10k")):
        x = x_of(hz) * SS
        d.text((x + 3 * SS, (GY1 - 5) * SS), t, font=fl, fill=ink, anchor="ls")
    for db in (12, 6, -6, -12):
        y = y_of(db) * SS
        d.text(((GX0 + 6) * SS, y - 2 * SS), "%+d" % db, font=fl, fill=ink, anchor="ls")
    d.text(((GX0 + 6) * SS, (GYC - 3) * SS), "0 dB", font=fl, fill=ink, anchor="ls")
    return down(frame, W, H)


def inner_mask():
    m = Image.new("L", (W * SS, H * SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((GX0 * SS, GY0 * SS, GX1 * SS, GY1 * SS), radius=INNER_R * SS, fill=255)
    return np.asarray(down(m, W, H))


# ---- grid widgets ---------------------------------------------------------------------------------------------------
CURVE_INK = (20, 20, 20)
CURVE_FILL = (20, 20, 20, 52)
LINE_W = 3


def curve_columns():
    xs = [GX0 + j * GW / CURVE_COLS for j in range(CURVE_COLS + 1)]
    return [(round(xs[j]), round(xs[j + 1])) for j in range(CURVE_COLS)]


def curve_strips():
    """per column: an upper strip (0 dB up to +18) and a lower one (0 dB down to -18). Frame k = the value
    -18 + 36 k / (frames - 1); each column's strip is clipped to the rounded inside at its place (identical strips are
    shared: files are named by content)."""
    mask = inner_mask()
    cols = curve_columns()
    files = {}
    out = []
    for j, (x0, x1) in enumerate(cols):
        w = x1 - x0
        pair = []
        for half in ("u", "l"):
            y0 = GY0 if half == "u" else GYC
            m = mask[y0:y0 + HALF, x0:x1].astype(np.float32) / 255
            key = (half, w, m.tobytes())
            if key not in files:
                big = Image.new("RGBA", (w * SS, HALF * CURVE_FRAMES * SS), (0, 0, 0, 0))
                d = ImageDraw.Draw(big)
                for k in range(CURVE_FRAMES):
                    v = -DB_MAX + 2 * DB_MAX * k / (CURVE_FRAMES - 1)
                    yy = HALF - v / DB_MAX * HALF if half == "u" else -v / DB_MAX * HALF   # within this half
                    top = k * HALF
                    if half == "u" and v > 0:
                        d.rectangle((0, (top + yy) * SS, w * SS, (top + HALF) * SS), fill=CURVE_FILL)
                    if half == "l" and v < 0:
                        d.rectangle((0, top * SS, w * SS, (top + yy) * SS), fill=CURVE_FILL)
                    ly0, ly1 = yy - LINE_W / 2, yy + LINE_W / 2
                    ly0, ly1 = max(ly0, 0), min(ly1, HALF)
                    if ly1 > ly0:
                        d.rectangle((0, (top + ly0) * SS, w * SS, (top + ly1) * SS), fill=CURVE_INK + (255,))
                strip = down(big, w, HALF * CURVE_FRAMES)
                a = np.asarray(strip).copy()
                for k in range(CURVE_FRAMES):
                    a[k * HALF:(k + 1) * HALF, :, 3] = (a[k * HALF:(k + 1) * HALF, :, 3] * m).astype(np.uint8)
                name = "curve_%s%d_%d.png" % (half, w, len(files))
                Image.fromarray(a, "RGBA").save(os.path.join(OUT, name), optimize=True)
                files[key] = name
            pair.append(files[key])
        out.append(pair)
    return cols, out


def dot_strip(color):
    pic = disc(None, DOT_D // 2, color + (255,))
    ring = disc(None, DOT_D // 2, WHITE + (255,), ring=color + (255,), ring_w=3.5)
    strip = Image.new("RGBA", (DOT_W, DOT_H * DOT_FRAMES), (0, 0, 0, 0))
    levels = DOT_FRAMES - 1
    for k in range(DOT_FRAMES):
        if k < levels:
            cy = DOT_PAD + (1 - k / (levels - 1)) * GH
            p = pic
        else:   # the band is off: a hollow ring on the 0 dB line
            cy = DOT_PAD + GH / 2
            p = ring
        strip.alpha_composite(p, ((DOT_W - p.width) // 2, k * DOT_H + round(cy - p.height / 2)))
    return strip


def dot_x(k):
    return GX0 + k / (DOT_POS - 1) * GW


def spec_columns():
    cols = []
    for j in range(SPEC_COLS):
        a, b = x_of(F_LO * 1000 ** (j / SPEC_COLS)), x_of(F_LO * 1000 ** ((j + 1) / SPEC_COLS))
        cols.append(round((a + b) / 2))
    return cols


SPEC_COLOR = (98, 150, 205)


def spec_strips(cols):
    mask = inner_mask()
    files, names = {}, []
    for cx in cols:
        x0 = cx - SPEC_W // 2
        m = mask[GY0:GY1, x0:x0 + SPEC_W].astype(np.float32) / 255
        key = m.tobytes()
        if key not in files:
            strip = Image.new("RGBA", (SPEC_W, GH * SPEC_FRAMES), (0, 0, 0, 0))
            d = ImageDraw.Draw(strip)
            for f in range(1, SPEC_FRAMES):
                hgt = round(f / (SPEC_FRAMES - 1) * GH)
                top = f * GH + GH - hgt
                d.rectangle((2, top, SPEC_W - 3, f * GH + GH - 1), fill=SPEC_COLOR + (44,))
                d.rectangle((2, top, SPEC_W - 3, min(f * GH + GH - 1, top + 1)), fill=SPEC_COLOR + (120,))
            a = np.asarray(strip).copy()
            for f in range(SPEC_FRAMES):
                a[f * GH:(f + 1) * GH, :, 3] = (a[f * GH:(f + 1) * GH, :, 3] * m).astype(np.uint8)
            name = "spec_%d.png" % len(files)
            Image.fromarray(a, "RGBA").save(os.path.join(OUT, name), optimize=True)
            files[key] = name
        names.append(files[key])
    return names


# ---- sections -------------------------------------------------------------------------------------------------------
def knob_strip(R=KNOB_R, BOX=KNOB_BOX):
    """black knob, a white tick near the rim (out of the way of the value text in the middle), 270 degrees"""
    strip = Image.new("RGBA", (BOX, BOX * KNOB_FRAMES), (0, 0, 0, 0))
    for j in range(KNOB_FRAMES):
        big = Image.new("RGBA", (BOX * SS, BOX * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        c, r = BOX * SS / 2, R * SS
        d.ellipse((c - r, c - r, c + r, c + r), fill=BLACK + (255,))
        a = math.radians(-135 + 270 * j / (KNOB_FRAMES - 1))
        ux, uy = math.sin(a), -math.cos(a)
        r0, r1 = 0.86 * r, 1.0 * r
        d.line((c + ux * r0, c + uy * r0, c + ux * r1, c + uy * r1), fill=WHITE + (255,), width=int(2.6 * (R / 16) ** 0.6 * SS))
        strip.paste(down(big, BOX, BOX), (0, j * BOX))
    return strip


def sec_center(b):
    return sc(SEC_X[b] + SEC_W / 2)


ROW_Y = (487, 535)          # knob rows (screen): where the mockup has them
LABEL_DY = 22              # label (cap centre) under each knob / the circle
TYPE_Y = 571               # the type name (its underline, from the mockup, is at 579-581)


def knob_xy(b, i):
    cx, _ = KNOB_C[i][b]
    return round(sc(cx)), ROW_Y[0 if i < 2 else 1]


def circle_xy(b):
    cx, _ = CIRCLE_C[b]
    return round(sc(cx)), ROW_Y[1]


TYPE_BOX = (sc(SEC_W) - 20, 20)      # the type name picture (w, h), screen px


def type_y():
    return TYPE_Y


def type_strip():
    """1.1: the type names as ONE filmstrip (a meter per band, frame = the type), instead of 14 mode images per band
    (112 crops of the background). Fewer images for MPC to decode and cache on the first open, which is where the user
    saw the type name drawn as a black square once (2026-11-05)."""
    w, h = round(TYPE_BOX[0]), TYPE_BOX[1]
    strip = Image.new("RGBA", (w, h * len(TYPE_NAMES)), (0, 0, 0, 0))
    for i, name in enumerate(TYPE_NAMES):
        im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        px = 11.5
        while px > 9:
            f = font(px * SS, 700)
            if d.textlength(name, font=f) <= (w - 4) * SS:
                break
            px -= 0.5
        text_centered(d, w * SS / 2, h * SS / 2, name, f, BLACK + (255,))
        strip.paste(down(im, w, h), (0, i * h))
    strip.save(os.path.join(OUT, "types.png"), optimize=True)
    return "skin_assets/types.png"


def circle_images():
    for b, c in enumerate(COLORS):
        n = 2 * CIRCLE_R + 2
        disc(None, CIRCLE_R, c + (255,)).save(os.path.join(OUT, "band%d_on.png" % (b + 1)))
        disc(None, CIRCLE_R, WHITE + (255,), ring=c + (255,), ring_w=3).save(os.path.join(OUT, "band%d_off.png" % (b + 1)))
    return 2 * CIRCLE_R + 2


# ---- top bar --------------------------------------------------------------------------------------------------------
def pill_image(w, h, fill, border, text=None, ink=BLACK, px=20, weight=700, icon=None):
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bw = 4 * SS
    d.rounded_rectangle((bw / 2, bw / 2, w * SS - bw / 2, h * SS - bw / 2), radius=h * SS / 2, fill=fill + (255,),
                        outline=border + (255,), width=bw)
    if icon:
        icon(d)
    if text:
        text_centered(d, w * SS / 2 + (14 * SS if icon else 0), h * SS / 2, text, font(px * SS, weight), ink + (255,))
    return down(im, w, h)


def analyzer_images():
    w, h = AN_BOX[2], AN_BOX[3]

    def bars(on):
        def f(d):
            x0 = 22 * SS
            for i, hh in enumerate((10, 18, 13, 22, 8)):
                c = (SPEC_COLOR if on else (170, 170, 170)) + (255,)
                x = x0 + i * 6 * SS
                d.rectangle((x, (h / 2 + 11 - hh) * SS, x + 4 * SS, (h / 2 + 11) * SS), fill=c)
            if not on:
                d.line((18 * SS, (h / 2 + 12) * SS, 54 * SS, (h / 2 - 13) * SS), fill=(60, 60, 60, 255), width=3 * SS)
        return f
    pill_image(w, h, WHITE, BLACK, "Visualizer", BLACK, 16, 700, bars(True)).save(os.path.join(OUT, "an_on.png"))
    pill_image(w, h, (232, 232, 232), (120, 120, 120), "Visualizer", (120, 120, 120), 16, 700, bars(False)).save(
        os.path.join(OUT, "an_off.png"))


def learn_images():
    """Learn: white pill like Visualizer (off), green while listening; a progress bar fills under the word"""
    w, h = LEARN_BOX[2], LEARN_BOX[3]

    def wave(on):
        def f(d):
            x0, cy = 20 * SS, h * SS / 2
            for i, hh in enumerate((6, 14, 20, 12, 6)):
                x = x0 + i * 6 * SS
                d.rounded_rectangle((x, cy - hh / 2 * SS, x + 3 * SS, cy + hh / 2 * SS), radius=1.5 * SS,
                                    fill=(WHITE if on else BLACK) + (255,))
        return f
    pill_image(w, h, WHITE, BLACK, "Learn", BLACK, 18, 700, wave(False)).save(os.path.join(OUT, "learn_off.png"))
    pill_image(w, h, GREEN, BLACK, "Learn", WHITE, 18, 700, wave(True)).save(os.path.join(OUT, "learn_on.png"))
    bw, bh = w - 44, 5
    strip = Image.new("RGBA", (bw, bh * LEARN_FRAMES), (0, 0, 0, 0))
    for k in range(LEARN_FRAMES):
        im = Image.new("RGBA", (bw * SS, bh * SS), (255, 255, 255, 90))
        d = ImageDraw.Draw(im)
        if k:
            d.rounded_rectangle((0, 0, bw * SS * k / (LEARN_FRAMES - 1), bh * SS - 1), radius=2 * SS, fill=WHITE + (255,))
        strip.paste(down(im, bw, bh), (0, k * bh))
    strip.save(os.path.join(OUT, "learn_prog.png"), optimize=True)
    return bw, bh


def qbank_images():
    w, h = QB_BOX[2], QB_BOX[3]
    for state in (0, 1):
        im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        bw = 4 * SS
        rad = h * SS / 2
        d.rounded_rectangle((bw / 2, bw / 2, w * SS - bw / 2, h * SS - bw / 2), radius=rad, fill=WHITE + (255,),
                            outline=BLACK + (255,), width=bw)
        half = w * SS / 2
        # the active half filled black
        hl = Image.new("L", im.size, 0)
        hd = ImageDraw.Draw(hl)
        hd.rounded_rectangle((bw / 2, bw / 2, w * SS - bw / 2, h * SS - bw / 2), radius=rad, fill=255)
        hd.rectangle((half, 0, w * SS, h * SS) if state == 0 else (0, 0, half, h * SS), fill=0)
        im.paste(Image.new("RGBA", im.size, BLACK + (255,)), (0, 0), hl)
        f = font(19 * SS, 700)
        text_centered(d, half / 2, h * SS / 2, "1-4", f, (WHITE if state == 0 else BLACK) + (255,))
        text_centered(d, half * 1.5, h * SS / 2, "5-8", f, (WHITE if state == 1 else BLACK) + (255,))
        down(im, w, h).save(os.path.join(OUT, "qbank_%d.png" % state))


def pass_images():
    n = 2 * PASS_R + 2
    for name, col in (("pass_on", GREEN), ("pass_off", RED)):
        im = Image.new("RGBA", (n * SS, n * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        c = n * SS / 2
        r = PASS_R * SS
        d.ellipse((c - r, c - r, c + r, c + r), fill=col + (255,), outline=BLACK + (255,), width=4 * SS)
        text_centered(d, c, c, "Pass", font(17 * SS, 800), WHITE + (255,))
        down(im, n, n).save(os.path.join(OUT, name + ".png"))
    return n


def qbank_bar(state):
    """the bar under the four sections the Q-Links drive"""
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    b0 = 4 * state
    x0, x1 = sc(SEC_X[b0]) + 6, sc(SEC_X[b0 + 3] + SEC_W) - 6
    y = 617
    d.rounded_rectangle((x0 * SS, (y - 2) * SS, x1 * SS, (y + 2) * SS), radius=2 * SS, fill=BLACK + (255,))
    f = font(11 * SS, 700)
    tw = d.textlength("Q-LINKS", font=f) / SS
    cx = (x0 + x1) / 2
    d.rounded_rectangle(((cx - tw / 2 - 8) * SS, (y - 8) * SS, (cx + tw / 2 + 8) * SS, (y + 8) * SS), radius=8 * SS,
                        fill=BLACK + (255,))
    text_centered(d, cx * SS, y * SS, "Q-LINKS", f, WHITE + (255,))
    box = (round(x0 - 2), y - 10, round(x1 + 2), y + 10)
    small = down(im, W, H).crop(box)
    small.save(os.path.join(OUT, "qbar_%d.png" % state))
    return box


# ---- the type browser -----------------------------------------------------------------------------------------------
def type_icon(d, t, x0, y0, w, h, color):
    """a little response curve of type t in the box (supersampled coordinates)"""
    sys.path.insert(0, HERE)
    pts = []
    for i in range(41):
        fr = i / 40
        hz = 20 * 1000 ** fr
        db = type_db(t, hz)
        y = y0 + h / 2 - max(-14, min(14, db)) / 14 * (h / 2)
        pts.append((x0 + fr * w, y))
    d.line(pts, fill=color + (255,), width=int(2.4 * SS), joint="curve")


def type_db(t, hz):
    """a sketch of each type's shape for the icons (not the DSP: just the look)"""
    fc = {"Bell": 1000, "Notch": 1000, "Band Pass": 1000, "Tilt": 1000, "Low Shelf": 600, "High Shelf": 1500}.get(
        TYPE_NAMES[t], 900)
    x = math.log2(hz / fc)
    n = TYPE_NAMES[t]
    if n == "Bell":
        return 10 * math.exp(-x * x * 1.2)
    if n == "Notch":
        return -14 * math.exp(-x * x * 6)
    if n == "Band Pass":
        return -min(14, 6 * abs(x) ** 1.4) + 2
    if n == "Tilt":
        return 6 * math.tanh(x)
    if n == "Low Shelf":
        return 8 / (1 + math.exp(x * 3))
    if n == "High Shelf":
        return 8 / (1 + math.exp(-x * 3))
    slope = int(n.split()[-1]) / 6
    if n.startswith("Low Cut"):
        return -min(14, max(0, -x) * 1.6 * slope) if x < 0 else 0
    return -min(14, max(0, x) * 1.6 * slope) if x > 0 else 0


def type_browser():
    px, py, pw, ph = TB_PANEL
    pad, gap, title_h = 16, 8, 38
    ncol = len(TB_COLUMNS)
    nrow = max(len(c) for c in TB_COLUMNS)
    cw = (pw - 2 * pad - (ncol - 1) * gap) // ncol
    ch = (ph - title_h - 2 * pad - (nrow - 1) * gap) // nrow
    L = ["", "# the type browser: tap a band's type name; on top of the grid while open (a tap on its background closes it)"]
    pan = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(pan)
    d.rounded_rectangle((3 * SS, 3 * SS, (pw - 3) * SS, (ph - 3) * SS), radius=FRAME_R * SS, fill=(236, 241, 246, 255),
                        outline=BLACK + (255,), width=FRAME_W * SS)
    down(pan, pw, ph).save(os.path.join(OUT, "tb_panel.png"))
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=tb_close img=skin_assets/tb_panel.png img_on=skin_assets/tb_panel.png '
             'when=tb_open:On' % (px + pw // 2, py + ph // 2 + Y_OFF, pw, ph))
    # title per band: "Band 3 type" with the band's colour
    tw, th = 300, 30
    for b in range(8):
        im = Image.new("RGBA", (tw * SS, th * SS), (0, 0, 0, 0))
        dd = ImageDraw.Draw(im)
        r = 9 * SS
        dd.ellipse((2 * SS, th * SS / 2 - r, 2 * SS + 2 * r, th * SS / 2 + r), fill=COLORS[b] + (255,))
        dd.text((2 * SS + 2 * r + 10 * SS, th * SS / 2 + 7 * SS), "Band %d  type" % (b + 1), font=font(20 * SS, 700),
                fill=BLACK + (255,), anchor="ls")
        down(im, tw, th).save(os.path.join(OUT, "tb_title_%d.png" % (b + 1)))
        # a button, not art: art with when= is baked under the widgets (the panel would cover it); a tap closes
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=tb_close img=skin_assets/tb_title_%d.png '
                 'img_on=skin_assets/tb_title_%d.png when=tsel:%d' % (px + pad + 4 + tw // 2, py + 10 + th // 2 + Y_OFF, tw, th,
                                                                       b + 1, b + 1, b + 1))
    hl = []
    f = font(17 * SS, 700)
    for c, col in enumerate(TB_COLUMNS):
        for r, t in enumerate(col):
            x0 = px + pad + c * (cw + gap)
            y0 = py + title_h + pad - 6 + r * (ch + gap)
            for variant, fill, ink in (("", WHITE, BLACK), ("_on", (200, 200, 200), BLACK), ("_hl", BLACK, WHITE),
                                       ("_hlon", (70, 70, 70), WHITE)):
                im = Image.new("RGBA", (cw * SS, ch * SS), (0, 0, 0, 0))
                dd = ImageDraw.Draw(im)
                dd.rounded_rectangle((1.5 * SS, 1.5 * SS, (cw - 1.5) * SS, (ch - 1.5) * SS), radius=ch * SS * 0.3,
                                     fill=fill + (255,), outline=BLACK + (255,), width=3 * SS)
                iw = 56
                type_icon(dd, t, 12 * SS, 8 * SS, iw * SS, (ch - 16) * SS, ink)
                dd.text(((iw + 22) * SS, ch * SS / 2 + 6 * SS), TYPE_NAMES[t], font=f, fill=ink + (255,), anchor="ls")
                down(im, cw, ch).save(os.path.join(OUT, "tb_cell_%d%s.png" % (t, variant)))
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=tpick%d img=skin_assets/tb_cell_%d.png '
                     'img_on=skin_assets/tb_cell_%d_on.png when=tb_open:On' % (x0 + cw // 2, y0 + ch // 2 + Y_OFF, cw, ch, t + 1, t, t))
            hl.append('button cx=%d cy=%d w=%d h=%d label="" key=tpick%d img=skin_assets/tb_cell_%d_hl.png '
                      'img_on=skin_assets/tb_cell_%d_hlon.png when=tb_hl:%d' % (x0 + cw // 2, y0 + ch // 2 + Y_OFF, cw, ch, t + 1, t, t, t + 1))
    L += ["# the band's current type, highlighted"] + hl
    return L


# ---- presets UI -----------------------------------------------------------------------------------------------------
PRESET_STYLE = dict(ink=(0, 0, 0), weight=700, name_px=22, cell_px=16, title=None, panel_fill=(236, 241, 246),
                    border=FRAME_W, radius=FRAME_R, cell_fill=(255, 255, 255), cell_border=3, hl_fill=(0, 0, 0),
                    hl_ink=(255, 255, 255), sel_fill=(255, 255, 255), sel_border=(0, 0, 0), sel_border_w=4, sel_radius=25,
                    name_ink=(0, 0, 0), arrow=(0, 0, 0), arrow_on=(56, 182, 255), head_fill=(24, 0, 172),
                    head_ink=(255, 255, 255), head_px=15)


def preset_selector():
    names = [n for n, _ in PRESETS]
    shown = [n.split(": ", 1)[-1] for n in names]
    return ppu.build(VST, names, ppu.Style(**PRESET_STYLE), sel=PRESET_SEL, panel=PRESET_PANEL, columns=preset_columns(),
                     shown=shown, arrow_w=40, src_dir=SRC, cell_gap=7, pad=16, title_h=0, arrow_inset=6)


# ---- main -----------------------------------------------------------------------------------------------------------
def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    check_presets()
    write_presets_header()
    write_presets_md()
    write_layout_header()
    write_params()
    mock = Image.open(MOCKUP).convert("RGB")
    clean = Image.open(CLEAN).convert("RGB")
    assert mock.size == clean.size == (MOCK_W, MOCK_H)
    build_bg(mock, clean).convert("RGB").save(os.path.join(OUT, "bg.png"), optimize=True)
    cols, curve_files = curve_strips()
    for b, c in enumerate(COLORS):
        dot_strip(c).save(os.path.join(OUT, "dot%d.png" % (b + 1)), optimize=True)
    scols = spec_columns()
    spec_files = spec_strips(scols)
    knob_strip().save(os.path.join(OUT, "knob.png"), optimize=True)
    knob_strip(DYN_KNOB_R, DYN_KNOB_BOX).save(os.path.join(OUT, "knob_dyn.png"), optimize=True)
    circ = circle_images()
    type_file = type_strip()
    analyzer_images()
    lbw, lbh = learn_images()
    dsel_files, dpw, dph, daw = dyn_images()
    qbank_images()
    pass_n = pass_images()
    qbars = [qbank_bar(s) for s in (0, 1)]

    L = []
    A = L.append
    A("# Generated by generate_skin.py -- edit the numbers there, not here.")
    A("# Coordinates: x = screen x, y = screen y + %d (shadow_skin Y_OFF). Plugin area 1280x628." % Y_OFF)
    A("theme_bg=FFFFFF")
    A("theme_ink=000000")
    A("theme_ink_dim=000000")
    A("theme_accent=000000")
    A("")
    A("[tab POLI-Q]")
    A("art file=skin_assets/bg.png fit=stretch")
    for s, (x0, y0, x1, y1) in enumerate(qbars):
        A("art file=skin_assets/qbar_%d.png x=%d y=%d w=%d h=%d fit=stretch when=qbank:%d" % (s, x0, y0 + Y_OFF, x1 - x0, y1 - y0, s))
    A("")
    A("# the output spectrum behind the curve (display only), shown while Analyzer is on")
    for j, cx in enumerate(scols):
        A("meter cx=%d cy=%d w=%d h=%d key=sp%02d strip=skin_assets/%s frames=%d when=analyzer:On"
          % (cx, GYC + Y_OFF, SPEC_W, GH, j + 1, spec_files[j], SPEC_FRAMES))
    A("")
    A("# the EQ curve: per column one meter above 0 dB and one below (same value)")
    for j, ((x0, x1), (fu, fl)) in enumerate(zip(cols, curve_files)):
        w = x1 - x0
        cx = x0 + w // 2
        A("meter cx=%d cy=%d w=%d h=%d key=cv%02d strip=skin_assets/%s frames=%d" % (cx, GY0 + HALF // 2 + Y_OFF, w, HALF, j + 1, fu, CURVE_FRAMES))
        A("meter cx=%d cy=%d w=%d h=%d key=cv%02d strip=skin_assets/%s frames=%d" % (cx, GYC + HALF // 2 + Y_OFF, w, HALF, j + 1, fl, CURVE_FRAMES))
    A("")
    A("# the band dots: one copy per place across the grid, the one at dx<b> shows; its frame is the height (dy<b>),")
    A("# or a hollow ring on the 0 dB line while the band is off")
    for b in range(8):
        for k in range(DOT_POS):
            A("meter cx=%d cy=%d w=%d h=%d key=dy%d strip=skin_assets/dot%d.png frames=%d when=dx%d:%d"
              % (round(dot_x(k)), GYC + Y_OFF, DOT_W, DOT_H, b + 1, b + 1, DOT_FRAMES, b + 1, k + 1))
    A("")
    A("# the 8 sections: Freq / Gain / Q (value inside the knob), the colour circle = band on/off, the type name")
    for b in range(8):
        for i, k in enumerate(("f", "g", "q")):
            x, y = knob_xy(b, i)
            A("knob cx=%d cy=%d r=%d label=\"\" key=%s%d ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF strip=skin_assets/knob.png frames=%d"
              % (x, y + Y_OFF, KNOB_R, k, b + 1, KNOB_VS, KNOB_VY, KNOB_BOX, KNOB_FRAMES))
        x, y = circle_xy(b)
        A("toggle cx=%d cy=%d label=\"\" key=on%d ns=0 w=%d h=%d img=skin_assets/band%d_off.png img_on=skin_assets/band%d_on.png"
          % (x, y + Y_OFF, b + 1, circ, circ, b + 1, b + 1))
        tw, th = round(TYPE_BOX[0]), TYPE_BOX[1]
        A("meter cx=%d cy=%d w=%d h=%d key=t%d strip=%s frames=%d"
          % (round(sec_center(b)), round(type_y()) + Y_OFF, tw, th, b + 1, type_file, len(TYPE_NAMES)))
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(OUT, "hit.png"))
    for b in range(8):
        tw, th = round(TYPE_BOX[0]), TYPE_BOX[1] + 8
        A('button cx=%d cy=%d w=%d h=%d label="" key=topen%d img=skin_assets/hit.png img_on=skin_assets/hit.png'
          % (round(sec_center(b)), round(type_y()) + Y_OFF, tw, th, b + 1))
    A("")
    A("# top bar: Visualizer, Learn, the Q-Link bank (1-4 / 5-8), Pass (green = EQ on, red = bypassed)")
    x, y, w, h = AN_BOX
    A("toggle cx=%d cy=%d label=\"\" key=analyzer ns=0 w=%d h=%d img=skin_assets/an_off.png img_on=skin_assets/an_on.png"
      % (x + w // 2, y + h // 2 + Y_OFF, w, h))
    x, y, w, h = LEARN_BOX
    A("toggle cx=%d cy=%d label=\"\" key=learn ns=0 w=%d h=%d img=skin_assets/learn_off.png img_on=skin_assets/learn_on.png"
      % (x + w // 2, y + h // 2 + Y_OFF, w, h))
    A("meter cx=%d cy=%d w=%d h=%d key=learn_prog strip=skin_assets/learn_prog.png frames=%d when=learn:On"
      % (x + w // 2 + 10, y + h - 9 + Y_OFF, lbw, lbh, LEARN_FRAMES))
    x, y, w, h = VOICE_BOX
    A('readout cx=%d cy=%d w=%d h=%d label="" key=voice box=0 tsize=13 tweight=semibold tcolor=3A3A3A talign=right tpad=0'
      % (x + w // 2, y + h // 2 + Y_OFF, w, h))
    A("")
    A("# the dynamics panel: band selector, Threshold / Range / Attack / Release of that band, its gain right now")
    bx = DYN_BOX[0] + DYN_BOX[2] // 2
    A("meter cx=%d cy=%d w=%d h=%d key=dsel strip=%s frames=%d" % (bx, DYN_SEL_Y + Y_OFF, dpw, dph, dsel_files[0], len(COLORS)))
    for side, x in (("prev", bx - dpw // 2 - daw // 2 - 4), ("next", bx + dpw // 2 + daw // 2 + 4)):
        A('button cx=%d cy=%d w=%d h=%d label="" key=dsel_%s img=skin_assets/dsel_%s.png img_on=skin_assets/dsel_%s_on.png'
          % (x, DYN_SEL_Y + Y_OFF, daw, dph, side, side, side))
    for (kx, ky), k in zip([(DYN_KNOB_X[0], DYN_KNOB_Y[0]), (DYN_KNOB_X[1], DYN_KNOB_Y[0]), (DYN_KNOB_X[0], DYN_KNOB_Y[1]),
                            (DYN_KNOB_X[1], DYN_KNOB_Y[1])], ("dthr", "drng", "datt", "drel")):
        A("knob cx=%d cy=%d r=%d label=\"\" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF strip=skin_assets/knob_dyn.png frames=%d"
          % (kx, ky + Y_OFF, DYN_KNOB_R, k, DYN_KNOB_VS, DYN_KNOB_VY, DYN_KNOB_BW, KNOB_FRAMES))
    x, y, w, h = DYN_BAR
    A("meter cx=%d cy=%d w=%d h=%d key=dact strip=skin_assets/dact.png frames=%d" % (x + w // 2, y + h // 2 + Y_OFF, w, h,
                                                                                   DACT_FRAMES))
    x, y, w, h = QB_BOX
    A("toggle cx=%d cy=%d label=\"\" key=qbank ns=0 w=%d h=%d img=skin_assets/qbank_0.png img_on=skin_assets/qbank_1.png"
      % (x + w // 2, y + h // 2 + Y_OFF, w, h))
    A("toggle cx=%d cy=%d label=\"\" key=bypass ns=0 w=%d h=%d img=skin_assets/pass_on.png img_on=skin_assets/pass_off.png"
      % (PASS_C[0], PASS_C[1] + Y_OFF, pass_n, pass_n))
    L += preset_selector()
    L += type_browser()
    A("")
    A("# Q-Links: one column per band (Freq, Gain, Q, Type, top to bottom), bands 1-4 or 5-8 (the bank switch)")
    keys = ",".join("q%s%d" % (w, c + 1) for c in range(4) for w in ("f", "g", "q", "t"))
    A('qlinks "POLI-Q" = %s' % keys)
    A("qlinks_track = %s" % keys)
    open(os.path.join(VST, "layout.conf"), "w", newline="\n").write("\n".join(L) + "\n")
    print("generate_skin: grid %dx%d at (%d,%d); curve %d cols x %d frames; dots %d places x %d frames; spectrum %d x %d;"
          " %d presets" % (GW, GH, GX0, GY0, CURVE_COLS, CURVE_FRAMES, DOT_POS, DOT_FRAMES, SPEC_COLS, SPEC_FRAMES,
                           len(PRESETS)))


if __name__ == "__main__":
    main()
