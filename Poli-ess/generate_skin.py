#!/usr/bin/env python3
"""Poli-Ess skin assets: turns the Canva mockup (imgs/) into the MPC skin's images AND writes vst/layout.conf,
so every coordinate lives in this one file.

    python3 generate_skin.py            (run from the Poli-ess folder; needs Pillow)

Writes (all inside vst/, which is what build_port.sh mounts into Docker):
    vst/skin_assets/bg.png           1280x628 page background: the mockup's blobs, title and underline, the meter frame,
                                     and all 8 labels (Open Sans, imgs/fonts, fitted to the mockup's lettering)
    vst/skin_assets/knob_strip.png   63-frame brown knob filmstrip (min at the top), pointer up = middle of travel
    vst/skin_assets/diff_off.png     Diff knob drawn at its minimum (pointer at 7:30)
    vst/skin_assets/diff_on.png      Diff knob drawn at its maximum (pointer at 4:30)
    vst/skin_assets/meter_strip.png  32-frame reduction meter: frame 0 = no reduction (all white),
                                     the brown bar comes down from the top as reduction grows
    vst/layout.conf                  the page: art background, 7 knobs + the Diff toggle in a 4x2 grid, meter, Q-Links

Layout (1.0.5): row 1 = Intensity, Threshold, Freq, Diff (the original four), row 2 = Attack, Release, Knee, Mix.
The knobs are smaller than in the mockup (r 154 -> 129 mockup px, 75 screen px) so two rows fit under the title;
row 1's knob tops line up with the top of the meter frame, row 2's labels with its bottom.

Geometry rules from mpc-vst-plugins (tools/shadow_skin.py):
  * the plugin area is 1280x628; layout.conf y is "Force Shadow" y, i.e. screen y + 86 (Y_OFF)
  * a knob is drawn into a (2r+8) square box centred on cx,cy; a strip frame fills that box
  * a meter strip's frames are exactly the meter's w x h, and the whole strip must stay <= 12288 px tall
  * images and strips need "art": "html" in vst.json
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared"))
import poli_preset_ui as ppu  # noqa: E402  (the preset selector shared by the Poli plugins)

HERE = os.path.dirname(os.path.abspath(__file__))
IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")

# ---- screen ---------------------------------------------------------------------------------------------
W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H                    # uniform scale: mockup height -> plugin height (no squashing)
X0 = 0                            # mockup's left edge on screen; the rest (164 px) on the right stays white,
                                  # which keeps the meter left of x~1025 where MPC's Q-Link sidebar sits

BROWN = (100, 77, 63, 255)        # Canva brown, measured from the mockup
WHITE = (255, 255, 255, 255)
SS = 4                            # supersampling for smooth edges

# ---- mockup geometry (measured on imgs/Poli-Esser.jpg, 1920x1080) ----------------------------------------
KNOB_CX = [261.5, 570.0, 878.5, 1187.0]   # the mockup's four columns (Intensity, Threshold, Freq, Diff)
MOCK_KNOB_R = 154.0                       # the mockup's knob radius (only for the pointer proportions)
POINTER_W = 15.0 / MOCK_KNOB_R            # pointer width as a fraction of the radius
POINTER_IN = 92.0 / MOCK_KNOB_R           # pointer runs from the rim this far inwards
TITLE_BOTTOM = 270                        # mockup rows 0..270 hold the title + underline (ink ends at 244)

# 4x2 grid, in mockup px: knob radius and the two row centres; labels sit LABEL_GAP under each knob (as the mockup)
KNOB_R = 129.0                            # 75 screen px
ROW_CY = [421.0, 803.0]                   # row 1 top = 292 = meter frame top; row 2 label baseline ~ frame bottom
LABEL_GAP = 75.0                          # knob bottom -> label baseline (the mockup: 916 -> 991)
LABELS = [["Intensity", "Threshold", "Freq", "Diff OFF/ON"],
          ["Attack", "Release", "Knee", "Mix"]]
KEYS = [["intensity", "thresh", "freq", "diff"],
        ["attack", "release", "knee", "mix"]]
# lettering fitted to the mockup's labels (Open Sans, weight 650, 51 px, +1 px tracking: 0.77 overlap on "Threshold")
FONT = os.path.join(IMGS, "fonts", "OpenSans.ttf")
FONT_WEIGHT, FONT_SIZE, FONT_TRACK = 650, 51, 1.0

METER_FRAME = (1425, 292, 1748, 1002)     # the meter's brown frame in the mockup (x0, y0, x1, y1)
METER_WELL_MOCK = (1464, 331, 1711, 956)  # inside the frame: the meter's travel

# Move the meter right of where the mockup has it, in screen px (0 = as in the mockup). It is redrawn there in the
# background and the live meter follows. Past x~1025 MPC's Q-Link sidebar can cover part of it while a Q-Link is held.
METER_DX = 60
_DXM = round(METER_DX / (H / MOCK_H))     # the same shift in mockup px (whole px keeps frame and well aligned)
METER_WELL = (METER_WELL_MOCK[0] + _DXM, METER_WELL_MOCK[1], METER_WELL_MOCK[2] + _DXM, METER_WELL_MOCK[3])

KNOB_FRAMES = 63                          # odd: one frame points straight up; 63 x 190 px fits MPC
METER_FRAMES = 32                         # engine.cpp kMeterSteps must match


def sx(x):
    return X0 + x * S


def sy(y):
    return y * S


# derived screen geometry
R = round(KNOB_R * S)                     # 75
BOX = 2 * R + 8                           # knob image box (shadow_skin: 2r+8)
GRID = [[(round(sx(cx)), round(sy(cy))) for cx in KNOB_CX] for cy in ROW_CY]
MX0, MY0, MX1, MY1 = (round(sx(METER_WELL[0])), round(sy(METER_WELL[1])),
                      round(sx(METER_WELL[2])), round(sy(METER_WELL[3])))
MW, MH = MX1 - MX0, MY1 - MY0
assert MH * METER_FRAMES <= 12288, "meter strip too tall for MPC"
assert (BOX + 2) * KNOB_FRAMES <= 12288, "knob strip too tall for MPC"


def knob_image(angle_deg, size=BOX, r=R):
    """One knob position: brown disc, white pointer from the rim inwards. angle 0 = pointing up."""
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS / 2
    rr = r * SS
    d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=BROWN)
    # pointer: a rectangle along the radius, rotated about the centre
    a = math.radians(angle_deg)
    ux, uy = math.sin(a), -math.cos(a)          # unit vector toward the pointer
    px, py = -uy, ux                            # perpendicular
    hw = POINTER_W * rr / 2
    r_out, r_in = rr - 0.5 * SS, rr * (1 - POINTER_IN)
    pts = [(c + ux * r_out + px * hw, c + uy * r_out + py * hw),
           (c + ux * r_out - px * hw, c + uy * r_out - py * hw),
           (c + ux * r_in - px * hw, c + uy * r_in - py * hw),
           (c + ux * r_in + px * hw, c + uy * r_in + py * hw)]
    d.polygon(pts, fill=WHITE)
    return big.resize((size, size), Image.Resampling.LANCZOS)


def draw_label(d, cx, baseline, text, font):
    """text centred (by ink) on cx, sitting on baseline, with the fitted tracking"""
    def run(x0, draw):
        pen = x0
        for i, ch in enumerate(text):
            if draw:
                d.text((pen, baseline), ch, font=font, fill=BROWN[:3], anchor="ls")
            pen += font.getlength(ch) + FONT_TRACK
            if i + 1 < len(text):   # keep the font's kerning between the pair
                pen += font.getlength(text[i:i + 2]) - font.getlength(text[i]) - font.getlength(text[i + 1])
        return pen
    l = d.textbbox((0, 0), text[0], font=font, anchor="ls")[0]
    r = d.textbbox((0, 0), text[-1], font=font, anchor="ls")[2]
    width = run(0, False) - FONT_TRACK - font.getlength(text[-1]) + r - l
    run(cx - width / 2 - l, True)


def make_background():
    mock = Image.open(os.path.join(IMGS, "Poli-Esser.jpg")).convert("RGB")
    clean = Image.open(os.path.join(IMGS, "Poli-Esser-bg.jpg")).convert("RGB")
    assert mock.size == (MOCK_W, MOCK_H) and clean.size == (MOCK_W, MOCK_H), "mockups must be 1920x1080"

    # start from the clean blobs; keep the mockup's title and underline (the blob is the same in both up there)
    page = clean.copy()
    page.paste(mock.crop((0, 0, MOCK_W, TITLE_BOTTOM)), (0, 0))
    d = ImageDraw.Draw(page)

    # the meter frame, solid brown, METER_DX right of the mockup; the live meter covers the well inside it exactly,
    # so a brown well leaves no light halo when scaled
    fx0, fy0, fx1, fy1 = METER_FRAME
    d.rectangle((fx0 + _DXM, fy0, fx1 + _DXM, fy1), fill=BROWN[:3])

    # all 8 labels in one lettering (the bottom row has no mockup text to copy)
    font = ImageFont.truetype(FONT, FONT_SIZE)
    font.set_variation_by_axes([FONT_WEIGHT, 100])          # axes: weight, width
    for row, cy in enumerate(ROW_CY):
        for col, cx in enumerate(KNOB_CX):
            draw_label(d, cx, cy + KNOB_R + LABEL_GAP, LABELS[row][col], font)

    scaled = page.resize((round(MOCK_W * S), H), Image.Resampling.LANCZOS)
    out = Image.new("RGB", (W, H), WHITE[:3])
    out.paste(scaled, (X0, 0))
    out.save(os.path.join(OUT, "bg.png"), optimize=True)


def make_knobs():
    strip = Image.new("RGBA", (BOX, BOX * KNOB_FRAMES), (0, 0, 0, 0))
    for i in range(KNOB_FRAMES):
        ang = -135.0 + 270.0 * i / (KNOB_FRAMES - 1)
        strip.paste(knob_image(ang), (0, i * BOX))
    strip.save(os.path.join(OUT, "knob_strip.png"), optimize=True)
    knob_image(-135.0).save(os.path.join(OUT, "diff_off.png"), optimize=True)
    knob_image(135.0).save(os.path.join(OUT, "diff_on.png"), optimize=True)


def make_meter():
    strip = Image.new("RGBA", (MW, MH * METER_FRAMES), WHITE)
    d = ImageDraw.Draw(strip)
    for i in range(METER_FRAMES):
        bar = round(MH * i / (METER_FRAMES - 1))   # brown pushed down from the top
        if bar:
            d.rectangle([0, i * MH, MW - 1, i * MH + bar - 1], fill=BROWN)
    strip.save(os.path.join(OUT, "meter_strip.png"), optimize=True)


def write_layout():
    lines = [
        "# Generated by generate_skin.py -- edit the numbers there, not here.",
        "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
        "theme_bg=FFFFFF",
        "theme_ink=644D3F",
        "theme_ink_dim=8A7466",
        "theme_accent=644D3F",
        "",
        "[tab POLI-ESSER]",
        "art file=skin_assets/bg.png fit=stretch",
        "",
        "# 4x2 knob grid: labels are baked into bg.png, so no MPC name/value text (ns=0 vs=0)",
    ]
    for row in range(2):
        for col in range(4):
            key, (cx, cy) = KEYS[row][col], GRID[row][col]
            if key == "diff":
                lines.append("# Diff OFF/ON: tap (or turn its Q-Link) to flip; drawn as the same knob at min / max")
                lines.append('toggle cx=%d cy=%d label="" key=diff ns=0 w=%d h=%d img=skin_assets/diff_off.png img_on=skin_assets/diff_on.png'
                             % (cx, cy + Y_OFF, BOX, BOX))
            else:
                lines.append('knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=0 bw=%d strip=skin_assets/knob_strip.png frames=%d'
                             % (cx, cy + Y_OFF, R, key, BOX, KNOB_FRAMES))
    lines += [
        "",
        "# gain-reduction meter: display only, the engine drives it (HAS_DISPLAY_REV)",
        "meter cx=%d cy=%d w=%d h=%d key=meter strip=skin_assets/meter_strip.png frames=%d"
        % ((MX0 + MX1) // 2, (MY0 + MY1) // 2 + Y_OFF, MW, MH, METER_FRAMES),
        "",
        "# Q-Links 1-4 = the top row, 5-8 = the bottom row",
        'qlinks "POLI-ESSER" = %s' % ",".join(KEYS[0] + KEYS[1]),
        "",
    ]
    q = next(i for i, l in enumerate(lines) if l.startswith("# Q-Links"))
    lines[q:q] = preset_selector() + [""]   # after every control: the browser covers them while it is up
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(lines))


# ---- presets: the selector right of the title, the browser over the knobs ------------------------------------------
# vst/presets.json holds the factory presets (tools/make_presets.py, with their sources). The engine loads them itself
# (src/presets_gen.h, written here), so the arrows and the browser can step through them; vst.json "programs" lists
# the same names in MPC's PRESET menu.
PRESET_SEL = (446, 62, 321, 58)          # screen x, y, w, h: right of "Poli-Esser", above the underline's end (x 767)
PRESET_PANEL = (52, 150, 752, 456)       # the browser: over the 4x2 knob grid, left of the meter
PRESET_STYLE = dict(ink=BROWN[:3], weight=FONT_WEIGHT, name_px=31, cell_px=25, title="Presets", title_px=30,
                    panel_fill=(252, 209, 203), border=6, radius=30, cell_fill=(255, 255, 255), cell_border=3,
                    hl_fill=BROWN[:3], hl_ink=(255, 255, 255), arrow_shape="triangle")


def preset_names():
    return [p["name"] for p in json.load(open(os.path.join(VST, "presets.json")))["presets"]]


def write_presets_header():
    """vst/presets.json -> src/presets_gen.h: each preset as the key/value texts the engine's set_param takes."""
    presets = json.load(open(os.path.join(VST, "presets.json")))["presets"]
    rows, tables = [], []
    for i, p in enumerate(presets):
        kv = ", ".join('{"%s", "%s"}' % (k, v if isinstance(v, str) else "%g" % v) for k, v in p["values"].items())
        tables.append("static const PresetKV kPreset%d[] = {%s};" % (i, kv))
        rows.append('    {"%s", %d, kPreset%d},' % (p["name"], len(p["values"]), i))
    h = ["// Generated by generate_skin.py from vst/presets.json -- edit that (tools/make_presets.py), not this.",
         "#pragma once", "struct PresetKV { const char *key, *value; };",
         "struct PresetDef { const char *name; int n; const PresetKV *values; };", *tables,
         "static const PresetDef kPresets[] = {", *rows, "};",
         "static const int kNumPresets = %d;" % len(presets), ""]
    with open(os.path.join(HERE, "src", "presets_gen.h"), "w", newline="\n") as f:
        f.write("\n".join(h))


def preset_selector():
    names = preset_names()
    ppu.ensure_params(os.path.join(VST, "params.json"), names)
    return ppu.build(VST, names, ppu.Style(**PRESET_STYLE), sel=PRESET_SEL, panel=PRESET_PANEL, grid=(3, 4), cell_h=80,
                     arrow_w=40, src_dir=os.path.join(HERE, "src"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_background()
    make_knobs()
    make_meter()
    write_presets_header()
    write_layout()
    print("knobs r=%d, rows %s (screen), meter %dx%d at x=%d..%d y=%d..%d" % (R, GRID, MW, MH, MX0, MX1, MY0, MY1))
    print("wrote vst/skin_assets/{bg,knob_strip,diff_off,diff_on,meter_strip}.png and vst/layout.conf")
