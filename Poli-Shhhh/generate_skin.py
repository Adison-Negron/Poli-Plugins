#!/usr/bin/env python3
"""Shhhhh skin assets: turns the Canva mockup (imgs/) into the MPC skin's images AND writes vst/layout.conf, so every
coordinate lives in this one file.

    python3 generate_skin.py            (run from the Poli-Shhhh folder; needs Pillow + numpy)

The grid fix: the mockup's vertical lines were evenly spaced, so its labels (100, 200, 500, 1k, 5k, 10k) could not all
be on a real frequency scale (100->200 was 107 px but 500->1k was 320 px). The grid is redrawn as a true log axis,
20 Hz (left edge) to 20 kHz (right edge), with lines AT 50, 100, 200, 500, 1k, 2k, 5k, 10k, and labels under them.
The level axis is 0 dB (top) to -120 dB (bottom), 20 dB per row (6 rows). The nodes sit exactly on their frequencies
(100, 200, 500, 800, 3.5k, 10k: the mockup's 1..6) at -60 dB (the mockup's middle line).

Writes (inside vst/, which is what the skin builder reads):
    skin_assets/bg.png            1280x628: title, the redrawn grid + labels, the fader tracks and their names
    skin_assets/node<i>.png       slider strip per node (NODE_FRAMES frames, frame 0 = the node at the bottom)
    skin_assets/fader.png         fader cap strip (FADER_FRAMES frames, frame 0 = cap at the bottom)
    skin_assets/spec_{l,m,r}.png  spectrum column strips (SPEC_FRAMES, frame 0 = empty); l/r are clipped to the
                                  grid's rounded corners
    skin_assets/learn_{off,on}.png, learn_prog.png (PROG_FRAMES)
    skin_assets/{pass,diff}_{off,on}.png, diff_grey.png
    layout.conf
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared"))
import poli_preset_ui as ppu  # noqa: E402  (the preset selector shared by the Poli plugins)

HERE = os.path.dirname(os.path.abspath(__file__))
IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")

W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H                     # 0.5815: uniform scale, left-aligned (the mockup ends at x=1116)

MOCKUP = os.path.join(IMGS, "Poli-denoise-ui.jpg")
CLEAN = os.path.join(IMGS, "Poli-Shhh-bg.jpg")

# ---- mockup geometry (measured on Poli-denoise-ui.jpg, 1920x1080) -------------------------------------------------
PANEL = (306, 386, 1557, 971)      # outer edge of the grid's black frame
PANEL_R, BORDER = 51, 10           # corner radius, frame width
GX0, GY0, GX1, GY1 = 316, 396, 1547, 961   # inner (grey) area
LINE = 5                           # grid line width
GRID_FILL = (200, 209, 218)
BLACK = (0, 0, 0)
NODE_BLUE = (0, 77, 183)
GREEN = (0, 191, 98)
GREY = (168, 172, 178)
GRID_ALPHA = 0.5                   # the grid blends into the page: fill and lines at half strength (frame stays black)
LINE_ALPHA = 0.45
LABEL_ROWS = (982, 1006)           # frequency labels
GLYPHS = {"1": (389, 400), "0": (403, 417), "2": (501, 512), "5": (708, 720), "k": (1053, 1063)}
GLYPH_GAP = 3
TRACKS = [(102, 113, 386, 970), (225, 236, 388, 971)]   # Threshold, Reduction fader lines (x0, x1, y0, y1)
CAPS = [(60, 884, 156, 914), (183, 884, 280, 914)]      # the drawn caps (taken out of the background)
NAMES = (50, 354, 292, 380)        # "Threshold" "Reduction" text
LEARN = (1257, 289, 1517, 378)
PASS_C, DIFF_C, TOG_R = (1641.5, 530.0), (1641.5, 837.0), 52.5
NODE_R = 23.5
NODE_MOCK = [(412.5, 690), (524.5, 689.5), (732, 690), (948.5, 689.5), (1207, 690), (1464.5, 690)]

# ---- the scales (shared with src/ShhDSP.cpp) ------------------------------------------------------------------------
F_LO, F_HI = 20.0, 20000.0
DB_TOP, DB_BOT = 0.0, -120.0
LINES_HZ = [50, 100, 200, 500, 1000, 2000, 5000, 10000]
LABELS = {50: "50", 100: "100", 200: "200", 500: "500", 1000: "1k", 2000: "2k", 5000: "5k", 10000: "10k"}
NODE_HZ = [100.0, 200.0, 500.0, 800.0, 3500.0, 10000.0]   # kNodeDefaultHz
NODE_POS = 5
N_BANDS = 32
DEFAULT_DB = -60.0

SPEC_FRAMES = 37                   # engine kSpecFrames  (12288 // grid height)
PROG_FRAMES = 16                   # engine kProgFrames


def mock_x(hz):                    # frequency -> mockup x
    return GX0 + math.log10(hz / F_LO) / math.log10(F_HI / F_LO) * (GX1 - GX0)


def node_candidates(i):            # same as ShhDSP::init()
    d = NODE_HZ[i]
    lo = 20.0 if i == 0 else math.sqrt(NODE_HZ[i - 1] * d)
    hi = 20000.0 if i == len(NODE_HZ) - 1 else math.sqrt(NODE_HZ[i + 1] * d)
    lo *= 2 ** 0.22
    hi /= 2 ** 0.22
    lo, hi = min(lo, d), max(hi, d)
    return [lo, math.sqrt(lo * d), d, math.sqrt(d * hi), hi]


def sc(v):
    return v * S


# screen geometry (integers)
SX0, SX1 = round(sc(GX0)), round(sc(GX1))
SY0, SY1 = round(sc(GY0)), round(sc(GY1))
GRID_H = SY1 - SY0                 # 329
NODE_D = 27                        # node picture on screen (odd: centred)
NODE_W = 29
NODE_PAD = 14                      # the slider reaches 14 px past the grid so the node's CENTRE spans 0..-120 dB
NODE_H = GRID_H + 2 * NODE_PAD
NODE_FRAMES = 12288 // NODE_H      # 34 (builder's slider strip limit)
SPEC_W = 22
FADER_W = 56
FADER_TOP, FADER_BOT = round(sc(386)), round(sc(971))
FADER_H = FADER_BOT - FADER_TOP
FADER_FRAMES = 12288 // FADER_H
CAP_H = round(sc(30))
TOG = 62
assert SPEC_FRAMES == 12288 // GRID_H, (SPEC_FRAMES, GRID_H)


def screen_x(hz):
    return sc(mock_x(hz))


def level_y(db):                   # screen y of a level
    return SY0 + (DB_TOP - db) / (DB_TOP - DB_BOT) * GRID_H


# ---- helpers --------------------------------------------------------------------------------------------------------
def circle_mask(size, cx, cy, r, ss=4):
    m = Image.new("L", (size[0] * ss, size[1] * ss), 0)
    ImageDraw.Draw(m).ellipse(((cx - r) * ss, (cy - r) * ss, (cx + r) * ss, (cy + r) * ss), fill=255)
    return m.resize(size, Image.LANCZOS)


def panel_canvas(ss=2):
    """the grid panel drawn from scratch at ss x the mockup resolution, cropped to its outer box: frame, fill, lines"""
    x0, y0, x1, y1 = PANEL
    w, h = (x1 - x0 + 1) * ss, (y1 - y0 + 1) * ss
    im = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=PANEL_R * ss, fill=BLACK + (255,))
    ix0, iy0, ix1, iy1 = (GX0 - x0) * ss, (GY0 - y0) * ss, (GX1 - x0 + 1) * ss - 1, (GY1 - y0 + 1) * ss - 1
    fill = tuple(round(255 - (255 - c) * GRID_ALPHA) for c in GRID_FILL)
    line = tuple(round(255 - (255 - c) * LINE_ALPHA) for c in BLACK)
    d.rounded_rectangle((ix0, iy0, ix1, iy1), radius=(PANEL_R - BORDER) * ss, fill=fill + (255,))
    half = LINE * ss / 2
    for hz in LINES_HZ:
        x = (mock_x(hz) - x0) * ss
        d.rectangle((round(x - half), iy0, round(x + half) - 1, iy1), fill=line + (255,))
    for j in range(1, 6):
        y = (GY0 + j * (GY1 - GY0) / 6 - y0) * ss
        d.rectangle((ix0, round(y - half), ix1, round(y + half) - 1), fill=line + (255,))
    return im.resize((w // ss, h // ss), Image.LANCZOS)


def inner_mask_screen():
    """the grey area's shape at screen scale (for clipping the spectrum columns at the rounded corners)"""
    ss = 4
    m = Image.new("L", (W * ss, H * ss), 0)
    ImageDraw.Draw(m).rounded_rectangle((sc(GX0) * ss, sc(GY0) * ss, sc(GX1 + 1) * ss, sc(GY1 + 1) * ss),
                                        radius=sc(PANEL_R - BORDER) * ss, fill=255)
    return m.resize((W, H), Image.LANCZOS)


def recolor(crop, src, dst, mask=None):
    """pixels that are a mix of `src` and white/black -> the same mix of `dst` (keeps anti-aliased text and edges)"""
    a = np.asarray(crop.convert("RGB")).astype(np.float32)
    srcv, dstv = np.array(src, np.float32), np.array(dst, np.float32)
    # amount of src vs white along the channel where src differs most from white
    c = int(np.argmax(255 - srcv))
    t = np.clip((255 - a[..., c]) / (255 - srcv[c]), 0, 1)[..., None]
    out = 255 * (1 - t) + dstv * t
    im = Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB").convert("RGBA")
    if mask is not None:
        im.putalpha(mask)
    return im


def fill_recolor(crop, fill, dst):
    """button crop (fill + black text/border): fill -> dst, keeping the black parts and their edges"""
    a = np.asarray(crop.convert("RGB")).astype(np.float32)
    t = np.clip(a.mean(-1, keepdims=True) / np.mean(fill), 0, 1)
    out = np.array(dst, np.float32) * t
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB")


def text_crop(mock, x0, x1):
    return mock.crop((x0 - 1, LABEL_ROWS[0], x1 + 2, LABEL_ROWS[1]))


def make_label(mock, text):
    parts = [text_crop(mock, *GLYPHS[ch]) for ch in text]
    w = sum(p.width for p in parts) + GLYPH_GAP * (len(parts) - 1) - 2 * (len(parts) - 1)
    im = Image.new("RGB", (w, parts[0].height), (255, 255, 255))
    x = 0
    for p in parts:
        # darken-composite so overlapping white margins don't cut the neighbour
        reg = im.crop((x, 0, x + p.width, p.height))
        im.paste(Image.fromarray(np.minimum(np.asarray(reg), np.asarray(p))), (x, 0))
        x += p.width + GLYPH_GAP - 2
    return im


# ---- build ----------------------------------------------------------------------------------------------------------
def build_bg(mock, clean):
    page = clean.copy()
    d = ImageDraw.Draw(page)
    # old labels out, new grid in
    d.rectangle((PANEL[0] - 20, LABEL_ROWS[0] - 4, PANEL[2] + 20, LABEL_ROWS[1] + 6), fill=(255, 255, 255))
    d.rectangle((PANEL[0] - 4, PANEL[1] - 4, PANEL[2] + 4, PANEL[3] + 4), fill=(255, 255, 255))
    panel = panel_canvas()
    page.paste(panel, (PANEL[0], PANEL[1]), panel)
    for hz in LINES_HZ:
        lab = make_label(mock, LABELS[hz])
        x = round(mock_x(hz) - lab.width / 2)
        reg = page.crop((x, LABEL_ROWS[0], x + lab.width, LABEL_ROWS[0] + lab.height))
        page.paste(Image.fromarray(np.minimum(np.asarray(reg), np.asarray(lab))), (x, LABEL_ROWS[0]))
    # fader names (from the mockup) and the two fader lines (without their caps: those move)
    page.paste(mock.crop(NAMES), NAMES[:2])
    for x0, x1, y0, y1 in TRACKS:
        d.rectangle((x0, y0, x1, y1), fill=BLACK)
    small = page.resize((round(MOCK_W * S), H), Image.LANCZOS)
    bg = Image.new("RGB", (W, H), (255, 255, 255))
    bg.paste(small, (0, 0))
    return bg


def node_strip(mock, i):
    cx, cy = NODE_MOCK[i]
    big = 4
    r = NODE_R + 1.5
    box = (round(cx - r), round(cy - r), round(cx + r), round(cy + r))
    crop = mock.crop(box).convert("RGBA")
    # a clean blue disc (the mockup's edge has grid-line pixels in it), then the mockup's digit from well inside
    disc = Image.new("RGBA", crop.size, NODE_BLUE + (255,))
    disc.putalpha(circle_mask(crop.size, cx - box[0], cy - box[1], NODE_R, ss=big))
    inner = circle_mask(crop.size, cx - box[0], cy - box[1], NODE_R - 4, ss=big)
    disc.paste(crop, (0, 0), inner)
    pic = disc.resize((NODE_D, NODE_D), Image.LANCZOS)
    strip = Image.new("RGBA", (NODE_W, NODE_H * NODE_FRAMES), (0, 0, 0, 0))
    for j in range(NODE_FRAMES):
        v = j / (NODE_FRAMES - 1)                          # frame 0 = minimum = bottom
        ccy = NODE_PAD + (1 - v) * GRID_H                  # centre, frame-relative
        # sub-pixel placement: paste at the rounded spot (the strip is resampled by MPC anyway)
        strip.alpha_composite(pic, ((NODE_W - NODE_D) // 2, j * NODE_H + round(ccy - NODE_D / 2)))
    return strip


def fader_strip(mock):
    x0, y0, x1, y1 = CAPS[0]
    cap = mock.crop((x0, y0, x1 + 1, y1 + 1)).resize((FADER_W, CAP_H), Image.LANCZOS).convert("RGBA")
    strip = Image.new("RGBA", (FADER_W, FADER_H * FADER_FRAMES), (0, 0, 0, 0))
    travel = FADER_H - CAP_H
    for j in range(FADER_FRAMES):
        v = j / (FADER_FRAMES - 1)
        strip.alpha_composite(cap, (0, j * FADER_H + round((1 - v) * travel)))
    return strip


def spec_strips():
    """three column strips (left corner, middle, right corner): a translucent blue bar from the bottom with a solid top"""
    inner = inner_mask_screen()
    out = {}
    cols = []
    for j in range(N_BANDS):
        a, b = screen_x(F_LO * 1000 ** (j / N_BANDS)), screen_x(F_LO * 1000 ** ((j + 1) / N_BANDS))
        cols.append(round((a + b) / 2))
    for name, cx in (("l", cols[0]), ("m", None), ("r", cols[-1])):
        strip = Image.new("RGBA", (SPEC_W, GRID_H * SPEC_FRAMES), (0, 0, 0, 0))
        for f in range(SPEC_FRAMES):
            hgt = round(f / (SPEC_FRAMES - 1) * GRID_H)
            if hgt <= 0:
                continue
            fr = Image.new("RGBA", (SPEC_W, GRID_H), (0, 0, 0, 0))
            d = ImageDraw.Draw(fr)
            d.rectangle((2, GRID_H - hgt, SPEC_W - 3, GRID_H - 1), fill=NODE_BLUE + (70,))
            d.rectangle((2, GRID_H - hgt, SPEC_W - 3, min(GRID_H - 1, GRID_H - hgt + 2)), fill=NODE_BLUE + (200,))
            if cx is not None:   # clip to the rounded grid at this column's place
                x0 = cx - SPEC_W // 2
                m = inner.crop((x0, SY0, x0 + SPEC_W, SY0 + GRID_H))
                fr.putalpha(Image.fromarray(np.minimum(np.asarray(fr.getchannel("A")), np.asarray(m))))
            strip.paste(fr, (0, f * GRID_H))
        out[name] = strip
    return out, cols


def toggles(mock):
    out = {}
    for name, (cx, cy) in (("pass", PASS_C), ("diff", DIFF_C)):
        r = TOG_R + 2
        box = (round(cx - r), round(cy - r), round(cx + r), round(cy + r))
        crop = mock.crop(box)
        mask = circle_mask(crop.size, cx - box[0], cy - box[1], TOG_R, ss=4)
        red = (255, 49, 49)
        for state, col in (("off", red), ("on", GREEN), ("grey", GREY)):
            im = recolor(crop, red, col, mask).resize((TOG, TOG), Image.LANCZOS)
            out["%s_%s" % (name, state)] = im
    crop = mock.crop(LEARN)
    sz = (round(sc(LEARN[2] - LEARN[0])), round(sc(LEARN[3] - LEARN[1])))
    out["learn_off"] = crop.resize(sz, Image.LANCZOS)
    out["learn_on"] = fill_recolor(crop, GRID_FILL, GREEN).resize(sz, Image.LANCZOS)
    return out, sz


# Output knob: a black square that stays put, a white guide line that turns (270 degrees), the value in the middle
KNOB_R = 38                        # builder: knob drawn in a (2r+8) box centred in a (2r+10) frame
KNOB_BOX = 2 * KNOB_R + 8
KNOB_FRAMES = 121                  # odd: frame 60 points straight up (0 dB); 121 x 86 = 10406 px
KNOB_VS = 15                       # value text size (MPC's Titillium)
KNOB_VY = -55                      # value text offset (set from the built TUI.json so the text sits in the middle)


def knob_strip():
    ss = 4
    strip = Image.new("RGBA", (KNOB_BOX, KNOB_BOX * KNOB_FRAMES), (0, 0, 0, 0))
    for j in range(KNOB_FRAMES):
        big = Image.new("RGBA", (KNOB_BOX * ss, KNOB_BOX * ss), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        c, r = KNOB_BOX * ss / 2, KNOB_R * ss
        d.rounded_rectangle((c - r, c - r, c + r, c + r), radius=8 * ss, fill=BLACK + (255,))
        a = math.radians(-135 + 270 * j / (KNOB_FRAMES - 1))
        ux, uy = math.sin(a), -math.cos(a)
        r0, r1 = 0.80 * r, 0.94 * r
        d.line((c + ux * r0, c + uy * r0, c + ux * r1, c + uy * r1), fill=(255, 255, 255, 255), width=4 * ss)
        for rr in (r0, r1):   # round ends
            e = 2 * ss
            d.ellipse((c + ux * rr - e, c + uy * rr - e, c + ux * rr + e, c + uy * rr + e), fill=(255, 255, 255, 255))
        strip.paste(big.resize((KNOB_BOX, KNOB_BOX), Image.LANCZOS), (0, j * KNOB_BOX))
    return strip


def prog_strip(w, h):
    strip = Image.new("RGBA", (w, h * PROG_FRAMES), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    for f in range(1, PROG_FRAMES):
        d.rectangle((0, f * h, round(f / (PROG_FRAMES - 1) * w) - 1, f * h + h - 1), fill=(0, 0, 0, 255))
    return strip


# ---- presets ------------------------------------------------------------------------------------------------------
# Starting points for Threshold / Reduction / Output after a Learn (they never touch the learned noise profile or the
# node offsets); every preset turns Pass and Diff off. iZotope RX De-noise's own default is Threshold 0, Reduction
# 12 dB, which is Init here. Lighter presets keep more of the room (music, field recordings: RX's guidance to use the
# least reduction that works, to avoid musical noise); dialogue presets go deeper; Hum & Buzz raises the threshold so
# the tonal peaks the nodes snap to are taken down hard. Edit here: generate_skin.py writes src/presets_gen.h and the
# "preset" option list.
PRESETS = [   # name (<= 24 chars), threshold (dB, -30..30), reduction (dB, 0..100), output (dB, -24..24)
    ("Init", 0, 12, 0),
    ("Light Touch", -3, 6, 0),
    ("Gentle Vocal", -1, 9, 0),
    ("Music Bed", -2, 7, 0),
    ("Field Recording", 1, 10, 0),
    ("Dialogue Clean", 2, 15, 0),
    ("Podcast Voice", 3, 18, 0.5),
    ("Hiss Remover", 2, 14, 0),
    ("Old Tape", 3, 16, 0),
    ("Room Tone Removal", 4, 22, 0.5),
    ("Hum & Buzz", 6, 24, 0),
    ("Heavy Noise", 6, 30, 1),
]
PRESET_SEL = (468, 84, 437, 56)          # screen x, y, w, h: right of the title, ends with the underline (x 905)
PRESET_PANEL = (178, 224, 729, 342)      # the browser: over the grid
PRESET_STYLE = dict(ink=(0, 0, 0), weight=700, name_px=28, cell_px=21, title="Presets", title_px=24,
                    panel_fill=(228, 232, 236), border=6, radius=26, cell_fill=(255, 255, 255), cell_border=3,
                    hl_fill=(0, 77, 183), hl_ink=(255, 255, 255), sel_fill=(200, 209, 218), sel_border=(3, 3, 3),
                    sel_border_w=5, sel_radius=0, name_ink=(0, 0, 0), arrow=(0, 0, 0), arrow_on=(0, 77, 183))


def write_presets_header():
    tables, rows = [], []
    for i, (name, thr, red, out) in enumerate(PRESETS):
        assert len(name) <= 24 and -30 <= thr <= 30 and 0 <= red <= 100 and -24 <= out <= 24, name
        tables.append('static const PresetKV kPreset%d[] = {{"threshold", "%g"}, {"reduction", "%g"}, {"output", "%g"}, '
                      '{"bypass", "0"}, {"diff", "0"}};' % (i, thr, red, out))
        rows.append('    {"%s", 5, kPreset%d},' % (name, i))
    h = ["// Generated by generate_skin.py (PRESETS) -- edit that, not this.", "#pragma once",
         "struct PresetKV { const char *key, *value; };",
         "struct PresetDef { const char *name; int n; const PresetKV *values; };", *tables,
         "static const PresetDef kPresets[] = {", *rows, "};", "static const int kNumPresets = %d;" % len(PRESETS), ""]
    with open(os.path.join(HERE, "src", "presets_gen.h"), "w", newline="\n") as f:
        f.write("\n".join(h))


def preset_selector():
    names = [p[0] for p in PRESETS]
    write_presets_header()
    ppu.ensure_params(os.path.join(VST, "params.json"), names)
    return ppu.build(VST, names, ppu.Style(**PRESET_STYLE), sel=PRESET_SEL, panel=PRESET_PANEL, grid=(3, 4),
                     arrow_w=44, src_dir=os.path.join(HERE, "src"), cell_gap=9, pad=18, title_h=44, arrow_inset=8)


def main():
    os.makedirs(OUT, exist_ok=True)
    mock = Image.open(MOCKUP).convert("RGB")
    clean = Image.open(CLEAN).convert("RGB")
    assert mock.size == clean.size == (MOCK_W, MOCK_H)
    build_bg(mock, clean).save(os.path.join(OUT, "bg.png"), optimize=True)
    for i in range(6):
        node_strip(mock, i).save(os.path.join(OUT, "node%d.png" % (i + 1)), optimize=True)
    fader_strip(mock).save(os.path.join(OUT, "fader.png"), optimize=True)
    strips, cols = spec_strips()
    for k, im in strips.items():
        im.save(os.path.join(OUT, "spec_%s.png" % k), optimize=True)
    tg, learn_sz = toggles(mock)
    for k, im in tg.items():
        im.save(os.path.join(OUT, k + ".png"), optimize=True)
    lx0, ly0, lx1, ly1 = [sc(v) for v in LEARN]
    prog_w, prog_h = round(lx1 - lx0) - 14, 4
    prog_strip(prog_w, prog_h).save(os.path.join(OUT, "learn_prog.png"), optimize=True)
    knob_strip().save(os.path.join(OUT, "knob_out.png"), optimize=True)

    L = []
    A = L.append
    A("# Generated by generate_skin.py -- edit the numbers there, not here.")
    A("# Coordinates: x = screen x, y = screen y + %d (shadow_skin Y_OFF). Plugin area 1280x628." % Y_OFF)
    A("theme_bg=FFFFFF")
    A("theme_ink=000000")
    A("theme_ink_dim=000000")
    A("theme_accent=004DB7")
    A("")
    A("[tab SHHHHH]")
    A("art file=skin_assets/bg.png fit=stretch")
    A("")
    A("# spectrum preview behind the grid: 32 columns on the log axis (input level, display only)")
    for j, cx in enumerate(cols):
        strip = "spec_l" if j == 0 else "spec_r" if j == N_BANDS - 1 else "spec_m"
        A("meter cx=%d cy=%d w=%d h=%d key=sp%02d strip=skin_assets/%s.png frames=%d"
          % (cx, SY0 + GRID_H // 2 + Y_OFF, SPEC_W, GRID_H, j + 1, strip, SPEC_FRAMES))
    A("")
    A("# the six nodes: vertical sliders (drag = that node's threshold); Learn moves them sideways inside their")
    A("# segment (n<i>x picks which of the 5 copies shows). Slider centre travel = the grid's 0..-120 dB.")
    ncy = SY0 - NODE_PAD + NODE_H // 2 + Y_OFF
    for i in range(6):
        for p, hz in enumerate(node_candidates(i)):
            A("slider_v cx=%d cy=%d w=%d h=%d label=\"\" key=n%d ns=0 vs=12 bw=%d strip=skin_assets/node%d.png frames=%d when=n%dx:%d"
              % (round(screen_x(hz)), ncy, NODE_W, NODE_H, i + 1, NODE_W, i + 1, NODE_FRAMES, i + 1, p))
    A("")
    A("# faders: Threshold (moves every node), Reduction (dB taken off the noise); names baked into bg.png")
    for k, (x0, x1, y0, y1) in zip(("threshold", "reduction"), TRACKS):
        cx = round(sc((x0 + x1) / 2))
        A("slider_v cx=%d cy=%d w=%d h=%d label=\"\" key=%s ns=0 vs=17 bw=70 strip=skin_assets/fader.png frames=%d"
          % (cx, FADER_TOP + FADER_H // 2 + Y_OFF, FADER_W, FADER_H, k, FADER_FRAMES))
    A("")
    A("# Learn: grey = idle, green = listening (up to 3 s, then it turns itself off); the bar under it is the progress")
    lcx, lcy = round((lx0 + lx1) / 2), round((ly0 + ly1) / 2)
    A("toggle cx=%d cy=%d label=\"\" key=learn ns=0 w=%d h=%d img=skin_assets/learn_off.png img_on=skin_assets/learn_on.png"
      % (lcx, lcy + Y_OFF, learn_sz[0], learn_sz[1]))
    A("meter cx=%d cy=%d w=%d h=%d key=learn_prog strip=skin_assets/learn_prog.png frames=%d"
      % (lcx, round(ly1) - 9 + Y_OFF, prog_w, prog_h, PROG_FRAMES))
    A("")
    A("# Pass / Diff: red = off, green = on. While Pass is on, Diff does nothing, so it shows grey and can't be tapped.")
    pcx, pcy = round(sc(PASS_C[0])), round(sc(PASS_C[1]))
    dcx, dcy = round(sc(DIFF_C[0])), round(sc(DIFF_C[1]))
    A("toggle cx=%d cy=%d label=\"\" key=bypass ns=0 w=%d h=%d img=skin_assets/pass_off.png img_on=skin_assets/pass_on.png"
      % (pcx, pcy + Y_OFF, TOG, TOG))
    A("toggle cx=%d cy=%d label=\"\" key=diff ns=0 w=%d h=%d img=skin_assets/diff_off.png img_on=skin_assets/diff_on.png when=bypass:0"
      % (dcx, dcy + Y_OFF, TOG, TOG))
    A("art file=skin_assets/diff_grey.png x=%d y=%d w=%d h=%d fit=stretch when=bypass:1"
      % (dcx - TOG // 2, dcy - TOG // 2 + Y_OFF, TOG, TOG))
    A("")
    A("# Output volume, between Pass and Diff: the value (\"+0.0 dB\") in the middle of the square")
    kcy = round((pcy + TOG / 2 + dcy - TOG / 2) / 2)
    A("knob cx=%d cy=%d r=%d label=\"\" key=output ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF strip=skin_assets/knob_out.png frames=%d"
      % (pcx, kcy + Y_OFF, KNOB_R, KNOB_VS, KNOB_VY, KNOB_BOX, KNOB_FRAMES))
    L += preset_selector()
    A("")
    A("# Q-Links (4 per column): Threshold, Reduction, Pass, Diff | nodes 1-4 | nodes 5-6, Output")
    A('qlinks "SHHHHH" = threshold,reduction,bypass,diff,n1,n2,n3,n4,n5,n6,output')
    open(os.path.join(VST, "layout.conf"), "w").write("\n".join(L) + "\n")
    print("generate_skin: grid %dx%d at (%d,%d); nodes %d frames of %d px; fader %d frames; spectrum %d frames"
          % (SX1 - SX0, GRID_H, SX0, SY0, NODE_FRAMES, NODE_H, FADER_FRAMES, SPEC_FRAMES))
    for i in range(6):
        print("  node %d: %s Hz" % (i + 1, ", ".join("%.0f" % f for f in node_candidates(i))))


if __name__ == "__main__":
    main()
