#!/usr/bin/env python3
"""PoliPlug: everything that comes from the mockup, generated in one place.

    python3 generate.py            (run from the PoliPlug folder; needs Pillow)

Reads
    imgs/PoliPlug.jpg, imgs/Poli-plug-bg.jpg   the mockup (1920x1080) and the same picture without the rows
Writes
    vst/params.json          PoliPlug's parameters (rack, effect list, 16 Q-Link proxies, 64 editor proxies)
    vst/layout.conf          the rack page
    vst/skin_assets/*.png    background, row pill, Open/Bye/arrow buttons cut from the mockup, + Add plugin, list
                             buttons, editor bar buttons
    src/pp_gen.h             the parameter indices for the engine

The hosted plugins are NOT chosen here: they are the effects installed on the MPC, found when PoliPlug starts
(src/discover.c), which also puts their skins into PoliPlug's own skin on the device. Why: MPC binds a skin to the
plugin it inserted, found by that plugin's own name (docs/SKIN_VARIANTS.md, docs/NOTES.md 2026-10-27); a plugin loaded
*inside* another plugin never gets its own skin shown. So PoliPlug's TUI.json carries each installed effect's pages,
each shown only while the parameter "view" is at that page (IndexedEnabling, the mechanism behind when=), with the
effect's "Parameter N" pointed at PoliPlug's editor proxy e<N>.

Geometry: the 1920x1080 mockup is scaled uniformly by 628/1080 and left-aligned (as Poli-Ess). layout.conf y = screen
y + 86 (shadow_skin Y_OFF).
"""
import json
import math
import os
import re
import shutil
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
IMGS = os.path.join(HERE, "imgs")
VST = os.path.join(HERE, "vst")
OUT = os.path.join(VST, "skin_assets")
SRC = os.path.join(HERE, "src")
MV = os.environ.get("MPC_VST", os.path.normpath(os.path.join(HERE, "..", "mpc-vst-plugins")))

# ---- capacity -----------------------------------------------------------------------------------------------
MAX_SLOTS = 16          # plugins in one PoliPlug (rows: 4 per page)
ROWS = 4
PAGES = (MAX_SLOTS + ROWS - 1) // ROWS
MAX_EDIT = 384          # editor proxies: a hosted plugin's parameters 0..383 are reachable on its page (Shapey EQ has 219,
                        # Poli-q 270+). e0..e255 sit where 1.2 put them; e256.. were appended in 1.3.1
E_LO = 256
NQ = 16                 # Q-Link proxies
NPICK = 8               # effects per page of the + Add plugin list
VIEWS = 96              # options of "view" (0 = rack): must equal PP_MAX_VIEWS in src/discover.h
NPROG = 128             # hosted presets PoliPlug can list in MPC's PRESET menu (vst.json programs: "hpreset")
NHPRE = 16              # rows of the hosted preset browser (2 columns x 8), paged

# ---- screen ---------------------------------------------------------------------------------------------------
W, H, Y_OFF = 1280, 628, 86
MOCK_W, MOCK_H = 1920, 1080
S = H / MOCK_H
BLUE = (92, 110, 160)
BLUE_DARK = (62, 76, 118)
DIM = (202, 212, 239)
WHITE = (255, 255, 255)
FONT = os.path.join(MV, "tools", "html_art", "fonts", "TitilliumWeb-Bold.ttf")

# ---- mockup geometry (measured on imgs/PoliPlug.jpg, 1920x1080) ----------------------------------------------------
ROW_TOP = 378            # row 1 pill top; rows are 122 tall, every 136 px
ROW_H, ROW_PITCH = 122, 136
PILL = (170, 1720)       # pill crop x range (a few px of the blue box each side, for clean edges)
NAME = (228, 860)        # where the plugin name goes (left part of the pill, up to the Open button)
OPEN = (868, 1214, 19, 102)    # x0, x1, y0, y1 (y relative to the row's pill top), cut from row 2
BYE = (1488, 1653, 27, 104)
UP = (1240, 1332, 0, 121)
DOWN = (1358, 1450, 0, 122)
STRIP_Y = (210, 288)     # white strip between the title and the box: the Add field and the page control live there
ADD_X = (150, 620)
PAGE_X = (1440, 1800)    # 1.5.0: moved right (was 1010-1410) to make room for the section tabs
STATUS_Y = 938           # status line inside the box, under row 4


# 1.5.0: the section tabs sit in the top strip between + Add plugin and the page control (they were right of the box,
# with a hint under them). Screen px.
TAB_W, TAB_H = 190, 45
TAB_CX = (492, 700)             # Inserts, Parallel
KNOB_FRAMES = 81                # odd: one frame points straight up (50 %)

# 1.5.0: right of the rack box (screen px): 4 macro knobs (Inserts tab only), the Q-Link page dots above them, Map / Clear
# under them. Each knob is mapped to any parameter of a hosted effect; Q-Link page p = macros (p-1)*4+1 .. +4.
COL_CX = 1170
MK_DOT_CY, MK_DOT_R, MK_DOT_DX = 146, 9, 30          # 4 dots centred on COL_CX
MK_KX, MK_KR, MK_KVS = 1106, 40, 16                  # knob centre x, radius, value text size
MK_KY = (212, 300, 388, 476)                         # knob centres, top to bottom = Q-Link knobs 1-4
MK_LBL_X0, MK_LBL_X1 = 1152, 1276                    # the mapped parameter's name and its effect, right of the knob
MK_MAP = (1118, 554, 94, 38)                         # Map: cx, cy, w, h
MK_CLR = (1224, 554, 94, 38)                         # Clear (only while mapping)

# 1.4.0: the row's own controls (screen px, x; rows keep their mockup y). Inserts and Parallel rows: On/Off; Parallel rows
# also: Send (how much of the track goes to this effect) and SC (its sidechain ducking panel)
ROW_NAME = (132, 335)
TOG_CX, TOG_W, TOG_H = 372, 62, 34
RSEND_CAP_CX = 428              # "Send", baked into the Parallel rows' pill
RSEND_CX, RSEND_R = 482, 32     # 1.4.1: r 26 -> 32 (the row is 71 px tall): small knobs are hard to read on the MPC
RSEND_VS = 17                   # its value text
SCB_CX, SCB_W, SCB_H = 552, 62, 36
OPEN_CX, OPEN_W, OPEN_H = 652, 120, 44
RED = (222, 88, 88)
GREEN = (62, 168, 96)

# 1.4.0: the sidechain panel (screen px), over the rack, left of x 1025 (MPC's Q-Link sidebar covers the right edge)
SCP = (150, 158, 868, 456)      # x, y, w, h (1.4.1: was 470x405 with r 34 knobs, too small to read on the MPC)
SCP_TITLE_CY, SCP_LINE_Y = 198, 230
SCP_X = (984, 194, 46)          # close: cx, cy, size
SCP_TOG = (290, 282, 150, 52)   # Off / On: cx, cy, w, h
SCP_NAME = (560, 282, 330, 46)  # the track it belongs to
SCP_KR = 62
SCP_KX = (237, 410, 584, 758, 931)
SCP_KY = (438,)
SCP_KVS = 25                    # the value text inside the knobs
SCP_LABEL_DY = 90               # knob centre -> its name
SCP_METER = (870, 292, 220, 24)   # cx, cy, w, h: Ducking now
SCP_METER_FRAMES = 25             # 0 .. 24 dB of ducking, 1 dB a frame
SCP_HINT_CY = 584
# its knobs: (key, label, row, column); Q-Links 1-4 = the four asked for, 5 Ratio, 6 the track's Send
SC_KNOBS = (("sc_thr", "Threshold", 0, 0), ("sc_att", "Attack", 0, 1), ("sc_rel", "Release", 0, 2),
            ("sc_knee", "Knee", 0, 3), ("sc_ratio", "Ratio", 0, 4))
SC_ATT = (0.1, 100.0)   # ms, log knob (position 0..100)
SC_REL = (10.0, 2000.0)
SC_RATIO = (1.0, 20.0)


def logpos(v, lo, hi):
    return 100.0 * math.log(v / lo) / math.log(hi / lo)
# the editor bar's preset selector (shown over a hosted page for effects that don't draw their own) and its browser
HPRE_NAME_W, HPRE_ARROW_W, HPRE_H = 280, 40, 28
BROWSER = (140, 40, 1000, 560)  # x, y, w, h of the hosted preset browser
BROW_ROW_W, BROW_ROW_H, BROW_ROWS = 462, 52, 8


def sx(x):
    return round(x * S)


def sy(y):
    return round(y * S)


def ly(y):
    """mockup y -> layout.conf y"""
    return sy(y) + Y_OFF


def scaled(im):
    return im.resize((max(1, round(im.width * S)), max(1, round(im.height * S))), Image.Resampling.LANCZOS)


def save(im, name):
    im.save(os.path.join(OUT, name), optimize=True)
    return "skin_assets/" + name


def darker(im, f=0.78):
    """The pressed look: the same picture, blue and red parts darker (white stays white)."""
    px = im.convert("RGB").load()
    out = im.convert("RGB").copy()
    po = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b = px[x, y]
            if min(r, g, b) < 235:
                po[x, y] = (int(r * f), int(g * f), int(b * f))
    return out


def pill(w, h, fill, text=None, size=None, ink=WHITE, ss=4):
    """A rounded pill (the mockup's button shape) drawn with supersampling, optional centred text."""
    im = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius=h * ss // 2, fill=fill)
    if text:
        f = ImageFont.truetype(FONT, (size or int(h * 0.55)) * ss)
        tb = d.textbbox((0, 0), text, font=f)
        d.text(((w * ss - (tb[2] - tb[0])) / 2 - tb[0], (h * ss - (tb[3] - tb[1])) / 2 - tb[1]), text, font=f, fill=ink)
    return im.resize((w, h), Image.Resampling.LANCZOS)


def outlined_pill(w, h, text, size, ss=4):
    """An inactive tab: white pill, blue outline and text."""
    im = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([2 * ss, 2 * ss, w * ss - 1 - 2 * ss, h * ss - 1 - 2 * ss], radius=h * ss // 2, fill=WHITE + (255,),
                        outline=BLUE + (255,), width=4 * ss)
    f = ImageFont.truetype(FONT, size * ss)
    tb = d.textbbox((0, 0), text, font=f)
    d.text(((w * ss - (tb[2] - tb[0])) / 2 - tb[0], (h * ss - (tb[3] - tb[1])) / 2 - tb[1]), text, font=f, fill=BLUE)
    return im.resize((w, h), Image.Resampling.LANCZOS)


def send_knob(angle, R, ss=4):
    """A PoliPlug knob: a blue disc with a white pointer near the rim (the value text sits in the middle), angle 0 =
    straight up. 1.3.0's Send knob was r=56; 1.4.1 has r=32 (a row's Send) and r=62 (the sidechain panel)."""
    box = 2 * R + 8
    im = Image.new("RGBA", (box * ss, box * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c, r = box * ss / 2, R * ss
    d.ellipse([c - r, c - r, c + r, c + r], fill=BLUE + (255,), outline=BLUE_DARK + (255,), width=max(2, round(R / 18)) * ss)
    a = math.radians(angle)
    r0, r1 = r * 0.80, r * 0.94
    d.line([(c + r0 * math.sin(a), c - r0 * math.cos(a)), (c + r1 * math.sin(a), c - r1 * math.cos(a))],
           fill=WHITE + (255,), width=int(max(3.0, R / 8) * ss))
    return im.resize((box, box), Image.Resampling.LANCZOS)


def knob_strip(R, name):
    box = 2 * R + 8
    strip = Image.new("RGBA", (box, box * KNOB_FRAMES), (0, 0, 0, 0))
    for i in range(KNOB_FRAMES):
        strip.paste(send_knob(-135.0 + 270.0 * i / (KNOB_FRAMES - 1), R), (0, i * box))
    return save(strip, name)


def text_c(d, cx, cy, text, px, ink, ss=1):
    f = ImageFont.truetype(FONT, px * ss)
    tb = d.textbbox((0, 0), text, font=f)
    d.text((cx * ss - (tb[2] - tb[0]) / 2 - tb[0], cy * ss - (tb[3] - tb[1]) / 2 - tb[1]), text, font=f, fill=ink)


def triangle(d, cx, cy, size, left, fill):
    if left:
        d.polygon([(cx + size * 0.5, cy - size * 0.6), (cx + size * 0.5, cy + size * 0.6), (cx - size * 0.55, cy)], fill=fill)
    else:
        d.polygon([(cx - size * 0.5, cy - size * 0.6), (cx - size * 0.5, cy + size * 0.6), (cx + size * 0.55, cy)], fill=fill)


# ---- parameters ----------------------------------------------------------------------------------------------------

def params():
    P = []
    mom = lambda k, n, **kw: dict({"key": k, "name": n, "momentary": True}, **kw)
    P.append({"key": "page", "name": "Page", "options": [str(i + 1) for i in range(PAGES)], "default": 0})
    P.append(mom("page_prev", "Page <"))   # the engine steps "page" itself: it only goes through pages that hold plugins
    P.append(mom("page_next", "Page >"))
    P.append({"key": "page_text", "name": "Page", "display": "string", "live": True})
    P.append(mom("add_open", "Add plugin"))   # shows / hides the list of installed effects
    P.append({"key": "picking", "name": "Picking", "options": ["Off", "On"], "default": 0, "live": True})
    P.append({"key": "status", "name": "Status", "display": "string", "live": True})
    for r in range(1, ROWS + 1):
        P += [
            {"key": "row%d" % r, "name": "Row %d" % r, "display": "string", "live": True},
            {"key": "row%d_used" % r, "name": "Row %d used" % r, "options": ["Off", "On"], "default": 0, "live": True},
            {"key": "row%d_up" % r, "name": "Row %d up" % r, "options": ["Hidden", "Dim", "On"], "default": 0, "live": True},
            {"key": "row%d_down" % r, "name": "Row %d down" % r, "options": ["Hidden", "Dim", "On"], "default": 0, "live": True},
            mom("row%d_open" % r, "Open %d" % r),
            mom("row%d_mvup" % r, "Move up %d" % r),
            mom("row%d_mvdn" % r, "Move down %d" % r),
            mom("row%d_bye" % r, "Remove %d" % r),
        ]
    P.append({"key": "view", "name": "View", "options": ["Rack"] + ["Page %d" % i for i in range(1, VIEWS)], "default": 0,
              "live": True})
    P.append({"key": "editing", "name": "Editing", "options": ["Off", "On"], "default": 0, "live": True})
    P.append({"key": "pages", "name": "Pages", "options": ["Off", "On"], "default": 0, "live": True})
    P.append(mom("view_back", "Back"))
    P.append(mom("view_prev", "Prev page"))
    P.append(mom("view_next", "Next page"))
    for n in range(1, NPICK + 1):
        P += [mom("pick%d" % n, "Pick %d" % n),
              {"key": "pick%d_name" % n, "name": "Effect %d" % n, "display": "string", "live": True},
              {"key": "pick%d_shown" % n, "name": "Effect %d shown" % n, "options": ["Off", "On"], "default": 0, "live": True}]
    for n in range(1, NQ + 1):
        P.append({"key": "q%d" % n, "name": "Q-Link %d" % n, "min": 0, "max": 1, "default": 0,
                  "dynamic_name": True, "dynamic_display": True})
    for n in range(E_LO):
        P.append({"key": "e%d" % n, "name": "Edit %d" % n, "min": 0, "max": 1, "default": 0,
                  "dynamic_name": True, "dynamic_display": True})
    # ---- 1.3.0, appended (projects store values by position) ----
    # the opened effect's presets: MPC's PRESET menu (vst.json "programs": hpreset, PROG_DYNAMIC: as many programs as
    # the opened effect has), and a selector + browser in the editor bar for effects that don't draw their own
    # "legacy": on no screen (only MPC's PRESET menu sets it), so the toolchain's host test doesn't step it like a knob:
    # with nothing open it rightly stays at 0
    P.append({"key": "hpreset", "name": "Effect preset", "min": 0, "max": NPROG - 1, "default": 0, "display": "int",
              "legacy": True})
    P.append({"key": "hpre_name", "name": "Effect preset name", "display": "string", "live": True})
    P.append({"key": "hpre_shown", "name": "Preset bar", "options": ["Off", "On"], "default": 0, "live": True})
    P += [mom("hpre_prev", "Effect preset <"), mom("hpre_next", "Effect preset >"), mom("hpre_list", "Effect presets")]
    P.append({"key": "hbrowsing", "name": "Preset browser", "options": ["Off", "On"], "default": 0, "live": True})
    P += [mom("hpre_pprev", "Preset page <"), mom("hpre_pnext", "Preset page >")]
    P.append({"key": "hpre_page", "name": "Preset page", "display": "string", "live": True})
    for n in range(1, NHPRE + 1):
        P += [mom("hpick%d" % n, "Preset pick %d" % n),
              {"key": "hpre%d_name" % n, "name": "Preset %d" % n, "display": "string", "live": True},
              {"key": "hpre%d_state" % n, "name": "Preset %d shown" % n, "options": ["Hidden", "Shown", "Current"],
               "default": 0, "live": True}]
    # the parallel section: a second chain, fed "send" of the track, added back to the main (insert) chain's output
    P.append({"key": "section", "name": "Section", "options": ["Inserts", "Parallel"], "default": 0, "live": True})
    P += [mom("sec_ins", "Inserts"), mom("sec_par", "Parallel")]
    # 1.4.0: replaced by a Send per Parallel track (row<r>_send); kept so projects keep their indices, on no screen
    P.append({"key": "send", "name": "Parallel send", "min": 0, "max": 100, "default": 50, "dynamic_display": True,
              "legacy": True})
    # ---- 1.3.1: more editor proxies (Poli-q has 270+ parameters; PoliPlug left it out of the list)
    for n in range(E_LO, MAX_EDIT):
        P.append({"key": "e%d" % n, "name": "Edit %d" % n, "min": 0, "max": 1, "default": 0,
                  "dynamic_name": True, "dynamic_display": True})
    # ---- 1.4.0: each row's On/Off, a Send per Parallel track, its sidechain (a ducking compressor keyed by the track)
    for r in range(1, ROWS + 1):
        P += [{"key": "row%d_onst" % r, "name": "Row %d on" % r, "options": ["Hidden", "Off", "On"], "default": 0, "live": True},
              mom("row%d_tog" % r, "On/Off %d" % r),
              {"key": "row%d_par" % r, "name": "Row %d parallel" % r, "options": ["Off", "On"], "default": 0, "live": True},
              {"key": "row%d_send" % r, "name": "Send %d" % r, "min": 0, "max": 100, "default": 50, "dynamic_display": True},
              {"key": "row%d_scst" % r, "name": "Row %d sidechain" % r, "options": ["Hidden", "Off", "On"], "default": 0,
               "live": True},
              mom("row%d_sc" % r, "Sidechain %d" % r)]
    P.append({"key": "scing", "name": "Sidechain panel", "options": ["Off", "On"], "default": 0, "live": True})
    P += [mom("sc_close", "Close sidechain"), mom("sc_panel", "Sidechain panel tap"), mom("sc_tog", "Sidechain on/off")]
    P.append({"key": "sc_onst", "name": "Sidechain on", "options": ["Hidden", "Off", "On"], "default": 0, "live": True})
    P.append({"key": "sc_name", "name": "Sidechain track", "display": "string", "live": True})
    P.append({"key": "sc_thr", "name": "SC Threshold", "min": -60, "max": 0, "default": -24, "dynamic_display": True})
    P.append({"key": "sc_att", "name": "SC Attack", "min": 0, "max": 100, "default": round(logpos(5.0, *SC_ATT), 3),
              "dynamic_display": True})
    P.append({"key": "sc_rel", "name": "SC Release", "min": 0, "max": 100, "default": round(logpos(250.0, *SC_REL), 3),
              "dynamic_display": True})
    P.append({"key": "sc_knee", "name": "SC Knee", "min": 0, "max": 24, "default": 6, "dynamic_display": True})
    P.append({"key": "sc_ratio", "name": "SC Ratio", "min": 0, "max": 100, "default": round(logpos(4.0, *SC_RATIO), 3),
              "dynamic_display": True})
    P.append({"key": "sc_gr", "name": "SC Ducking", "min": 0, "max": 1, "default": 0, "live": True})
    # ---- 1.5.0: macro knobs (Inserts tab): 16 macros, 4 shown (the Q-Link page), each mapped to a hosted parameter
    P.append({"key": "mk_shown", "name": "Macros shown", "options": ["Off", "On"], "default": 0, "live": True})
    P.append({"key": "mk_page", "name": "Macro page", "options": ["Hidden", "P1", "P2", "P3", "P4"], "default": 0, "live": True})
    P += [mom("mk_pg%d" % n, "Macro page %d" % n) for n in range(1, 5)]
    P.append(mom("mk_map", "Map"))
    P.append({"key": "mk_mapst", "name": "Map state", "options": ["Hidden", "Off", "On"], "default": 0, "live": True})
    P.append(mom("mk_clear", "Clear macro"))
    P.append({"key": "mk_clrst", "name": "Clear shown", "options": ["Off", "On"], "default": 0, "live": True})
    for k in range(1, 5):
        P += [{"key": "mk%d" % k, "name": "Macro %d" % k, "min": 0, "max": 1, "default": 0, "dynamic_display": True},
              {"key": "mk%d_st" % k, "name": "Macro %d state" % k, "options": ["Hidden", "Empty", "Mapped", "Armed"],
               "default": 0, "live": True},
              {"key": "mk%d_name" % k, "name": "Macro %d name" % k, "display": "string", "live": True},
              {"key": "mk%d_fx" % k, "name": "Macro %d effect" % k, "display": "string", "live": True}]
    return P


# ---- artwork ---------------------------------------------------------------------------------------------------

def row_top(r):
    return ROW_TOP + ROW_PITCH * r


def crop_row(mock, box, r):
    x0, x1, y0, y1 = box
    return mock.crop((x0, row_top(r) + y0, x1, row_top(r) + y1))


def make_art():
    mock = Image.open(os.path.join(IMGS, "PoliPlug.jpg")).convert("RGB")
    clean = Image.open(os.path.join(IMGS, "Poli-plug-bg.jpg")).convert("RGB")
    assert mock.size == (MOCK_W, MOCK_H) and clean.size == (MOCK_W, MOCK_H), "mockups must be 1920x1080"
    A = {}

    # background: the clean mockup + the top strip's two pills (their text and arrows are live / buttons)
    bg = clean.copy()
    d = ImageDraw.Draw(bg)
    y0, y1 = STRIP_Y
    # 1.5.0: it now sits over the blue blob: a white ring keeps its shape readable there
    d.rounded_rectangle([PAGE_X[0] - 7, y0 - 7, PAGE_X[1] + 7, y1 + 7], radius=(y1 - y0 + 14) // 2, fill=WHITE)
    d.rounded_rectangle([PAGE_X[0], y0, PAGE_X[1], y1], radius=(y1 - y0) // 2, fill=BLUE)

    page = Image.new("RGB", (W, H), WHITE)
    page.paste(scaled(bg), (0, 0))
    A["bg"] = save(page, "bg.png")

    # the row pill, empty: row 2's pill with everything on it painted white
    p = mock.crop((PILL[0], row_top(1) - 6, PILL[1], row_top(1) + ROW_H + 6))
    ImageDraw.Draw(p).rectangle([226 - PILL[0], 12, 1660 - PILL[0], ROW_H], fill=WHITE)
    A["pill"] = save(scaled(p), "row_pill.png")
    A["pill_box"] = (PILL[0], -6, PILL[1], ROW_H + 6)

    # buttons cut from the mockup: row 2 has both arrows lit; row 1's up and row 4's down are the dimmed ones
    for name, box, r in (("open", OPEN, 1), ("bye", BYE, 1), ("up", UP, 1), ("down", DOWN, 1)):
        im = scaled(crop_row(mock, box, r))
        A[name] = (save(im, name + ".png"), save(darker(im), name + "_on.png"))
    A["up_dim"] = save(scaled(crop_row(mock, UP, 0)), "up_dim.png")
    A["down_dim"] = save(scaled(crop_row(mock, DOWN, 3)), "down_dim.png")

    # page arrows: drawn into their own images (the pill is in the background); the pressed look is darker
    ah = sy(y1 - y0)
    aw = ah + 6
    for side in ("prev", "next"):
        for on in (0, 1):
            im = Image.new("RGB", (aw * 4, ah * 4), BLUE_DARK if on else BLUE)
            triangle(ImageDraw.Draw(im), aw * 2, ah * 2, ah * 1.3, side == "prev", WHITE)
            A["page_%s%s" % (side, "_on" if on else "")] = save(im.resize((aw, ah), Image.Resampling.LANCZOS),
                                                                  "page_%s%s.png" % (side, "_on" if on else ""))
    A["page_arrow"] = (aw, ah)

    # + Add plugin: a button with its caption drawn in (pressed / list open = darker)
    aw_, ah_ = sx(ADD_X[1] - ADD_X[0]), sy(STRIP_Y[1] - STRIP_Y[0])
    A["add"] = (save(pill(aw_, ah_, BLUE + (255,), "+ Add plugin", 27), "add.png"),
                save(pill(aw_, ah_, BLUE_DARK + (255,), "+ Add plugin", 27), "add_on.png"), aw_, ah_)
    # the effect list's buttons: white pills (the name is live text on top), pressed = the dimmed blue
    pw, ph = 430, 66
    A["pick"] = (save(pill(pw, ph, WHITE + (255,)), "pick.png"), save(pill(pw, ph, DIM + (255,)), "pick_on.png"), pw, ph)
    # under a hosted page (src/discover.c), so no rack art shows through
    Image.new("RGB", (W, H), WHITE).save(os.path.join(OUT, "pp_backdrop.png"), optimize=True)

    # section tabs (active = filled); 1.5.0: in the top strip, no hint text any more
    for name, text in (("tab_ins", "Inserts"), ("tab_par", "Parallel")):
        A[name] = (save(pill(TAB_W, TAB_H, BLUE + (255,), text, 26), name + "_on.png"),   # active / pressed
                   save(outlined_pill(TAB_W, TAB_H, text, 26), name + ".png"))
    for stale in ("knob_send.png",):
        if os.path.exists(os.path.join(OUT, stale)):
            os.remove(os.path.join(OUT, stale))

    # 1.5.0: the macro column: knob strip, armed ring, Q-Link page dots, Map / Clear
    A["knob_mk"] = knob_strip(MK_KR, "knob_mk.png")
    rb = 2 * MK_KR + 16
    ring = Image.new("RGBA", (rb * 4, rb * 4), (0, 0, 0, 0))
    ImageDraw.Draw(ring).ellipse([6, 6, rb * 4 - 7, rb * 4 - 7], outline=GREEN + (255,), width=20)
    A["mk_ring"] = (save(ring.resize((rb, rb), Image.Resampling.LANCZOS), "mk_ring.png"), rb)
    db = 2 * MK_DOT_R + 4
    for name, filled in (("mk_dot", False), ("mk_dot_on", True)):
        im = Image.new("RGBA", (db * 4, db * 4), (0, 0, 0, 0))
        ImageDraw.Draw(im).ellipse([8, 8, db * 4 - 9, db * 4 - 9], fill=(BLUE + (255,)) if filled else (WHITE + (0,)),
                                   outline=BLUE + (255,), width=10)
        A[name] = (save(im.resize((db, db), Image.Resampling.LANCZOS), name + ".png"), db)
    _, _, mw, mh = MK_MAP
    A["mk_map_off"] = (save(outlined_pill(mw, mh, "Map", 22), "mk_map_off.png"),
                       save(pill(mw, mh, DIM + (255,), "Map", 22, BLUE), "mk_map_off_dn.png"))
    A["mk_map_on"] = (save(pill(mw, mh, GREEN + (255,), "Map", 22), "mk_map_on.png"),
                      save(pill(mw, mh, tuple(int(c * 0.78) for c in GREEN) + (255,), "Map", 22), "mk_map_on_dn.png"))
    _, _, cw, ch = MK_CLR
    A["mk_clr"] = (save(pill(cw, ch, RED + (255,), "Clear", 22), "mk_clr.png"),
                   save(pill(cw, ch, tuple(int(c * 0.78) for c in RED) + (255,), "Clear", 22), "mk_clr_dn.png"))

    # 1.4.0: the rows' own controls
    A["pill_par"] = save(pill_with_caption(A["pill"]), "row_pill_par.png")
    for name, fill, text in (("tog_off", RED, "Off"), ("tog_on", GREEN, "On")):
        A[name] = (save(pill(TOG_W, TOG_H, fill + (255,), text, 20), name + ".png"),
                   save(pill(TOG_W, TOG_H, tuple(int(c * 0.78) for c in fill) + (255,), text, 20), name + "_dn.png"))
    A["scb_off"] = (save(outlined_pill(SCB_W, SCB_H, "SC", 20), "scb_off.png"),
                    save(pill(SCB_W, SCB_H, DIM + (255,), "SC", 20, BLUE), "scb_off_dn.png"))
    A["scb_on"] = (save(pill(SCB_W, SCB_H, GREEN + (255,), "SC", 20), "scb_on.png"),
                   save(pill(SCB_W, SCB_H, tuple(int(c * 0.78) for c in GREEN) + (255,), "SC", 20), "scb_on_dn.png"))
    A["open2"] = (save(pill(OPEN_W, OPEN_H, BLUE + (255,), "Open", 27), "open2.png"),
                  save(pill(OPEN_W, OPEN_H, BLUE_DARK + (255,), "Open", 27), "open2_on.png"))
    A["knob_row"] = knob_strip(RSEND_R, "knob_row.png")
    A["knob_sc"] = knob_strip(SCP_KR, "knob_sc.png")
    sc_panel_art(A)

    # the hosted preset selector: arrows and the name pill in the editor bar's style; the browser panel and its rows
    for name, text in (("hpre_prev", "<"), ("hpre_next", ">")):
        A[name] = (save(pill(HPRE_ARROW_W, HPRE_H, BLUE + (235,), text, 18), name + ".png"),
                   save(pill(HPRE_ARROW_W, HPRE_H, BLUE_DARK + (235,), text, 18), name + "_on.png"))
    A["hpre_name"] = (save(pill(HPRE_NAME_W, HPRE_H, BLUE + (235,)), "hpre_name.png"),
                      save(pill(HPRE_NAME_W, HPRE_H, BLUE_DARK + (235,)), "hpre_name_on.png"))
    bx, by, bw, bh = BROWSER
    pan = Image.new("RGBA", (bw * 4, bh * 4), (0, 0, 0, 0))
    dp = ImageDraw.Draw(pan)
    dp.rounded_rectangle([0, 0, bw * 4 - 1, bh * 4 - 1], radius=36 * 4, fill=BLUE + (255,), outline=(0, 0, 0, 255), width=12)
    ft = ImageFont.truetype(FONT, 34 * 4)
    dp.text((40 * 4, 18 * 4), "Presets", font=ft, fill=WHITE)
    A["browser"] = save(pan.resize((bw, bh), Image.Resampling.LANCZOS), "hpre_panel.png")
    A["brow_row"] = (save(pill(BROW_ROW_W, BROW_ROW_H, WHITE + (255,)), "hpre_row.png"),
                     save(pill(BROW_ROW_W, BROW_ROW_H, DIM + (255,)), "hpre_row_on.png"))
    A["brow_cur"] = (save(pill(BROW_ROW_W, BROW_ROW_H, BLUE_DARK + (255,)), "hpre_cur.png"),
                     save(pill(BROW_ROW_W, BROW_ROW_H, (40, 50, 80, 255)), "hpre_cur_on.png"))
    for name, text in (("hpg_prev", "<"), ("hpg_next", ">")):
        A[name] = (save(pill(56, 44, WHITE + (255,), text, 28, BLUE), name + ".png"),
                   save(pill(56, 44, DIM + (255,), text, 28, BLUE), name + "_on.png"))
    A["hpg_text"] = (save(pill(150, 44, WHITE + (255,)), "hpg_text.png"), save(pill(150, 44, DIM + (255,)), "hpg_text_on.png"))

    # the editor bar, drawn over a hosted plugin's page: Back to the rack, and prev/next for plugins with several pages
    # a thin strip along the very top edge, where plugins keep their title: over a control it would take its touches
    # (Shapey EQ has BYPASS top right)
    for name, text, w_, h_ in (("back", "< RACK", 104, 28), ("vprev", "<", 40, 28), ("vnext", ">", 40, 28)):
        A[name] = (save(pill(w_, h_, BLUE + (235,), text, 18), name + ".png"),
                   save(pill(w_, h_, BLUE_DARK + (235,), text, 18), name + "_on.png"), w_, h_)
    return A


def pill_with_caption(pill_path):
    """the row pill with "Send" baked left of the row's Send knob (Parallel rows only: its own when= art line)"""
    im = Image.open(os.path.join(VST, pill_path)).convert("RGB")
    ss = 4
    big = im.resize((im.width * ss, im.height * ss), Image.Resampling.LANCZOS)
    d = ImageDraw.Draw(big)
    text_c(d, RSEND_CAP_CX - sx(PILL[0]), im.height / 2, "Send", 17, BLUE, ss)
    return big.resize(im.size, Image.Resampling.LANCZOS)


def sc_panel_art(A):
    """The sidechain panel (1.4.0), drawn like Poli-q's Dynamics panel in PoliPlug's colours: a white card with a dark blue
    border, "Sidechain" underlined, Off/On, the knob names, the ducking meter's scale."""
    ss = 4
    x0, y0, w, h = SCP
    im = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius=30 * ss, fill=WHITE + (255,), outline=BLUE_DARK + (255,),
                        width=5 * ss)
    lx = lambda x: x - x0
    lyy = lambda y: y - y0
    text_c(d, w / 2, lyy(SCP_TITLE_CY), "Sidechain", 38, BLUE, ss)
    d.line([(40 * ss, lyy(SCP_LINE_Y) * ss), ((w - 40) * ss, lyy(SCP_LINE_Y) * ss)], fill=BLUE + (255,), width=3 * ss)
    for key, label, row, col in SC_KNOBS:
        text_c(d, lx(SCP_KX[col]), lyy(SCP_KY[row] + SCP_LABEL_DY), label, 27, BLUE, ss)
    mcx, mcy, mw, mh = SCP_METER
    text_c(d, lx(mcx), lyy(mcy - mh / 2 - 17), "Ducking now", 22, BLUE, ss)
    for t, x in (("0", mcx - mw / 2 + 6), ("-24 dB", mcx + mw / 2 - 26)):
        text_c(d, lx(x), lyy(mcy + mh / 2 + 14), t, 17, BLUE_DARK, ss)
    text_c(d, w / 2, lyy(SCP_HINT_CY), "Q-Link page 1: Threshold, Attack, Release, Knee   |   page 2: Ratio", 19, BLUE_DARK, ss)
    A["sc_panel"] = save(im.resize((w, h), Image.Resampling.LANCZOS), "sc_panel.png")
    # the close button, the track name's (invisible) field, the backdrop that dims the rack and closes on a tap
    cx, cy, sz = SCP_X
    xi = Image.new("RGBA", (sz * ss, sz * ss), (0, 0, 0, 0))
    dx = ImageDraw.Draw(xi)
    dx.ellipse([0, 0, sz * ss - 1, sz * ss - 1], fill=BLUE + (255,))
    m = sz * ss * 0.32
    for a, b in (((m, m), (sz * ss - m, sz * ss - m)), ((m, sz * ss - m), (sz * ss - m, m))):
        dx.line([a, b], fill=WHITE + (255,), width=4 * ss)
    A["sc_x"] = save(xi.resize((sz, sz), Image.Resampling.LANCZOS), "sc_x.png")
    _, _, nw, nh = SCP_NAME
    A["sc_namebg"] = save(pill(nw, nh, WHITE + (255,)), "sc_namebg.png")
    A["sc_back"] = save(Image.new("RGBA", (W, H), (20, 26, 48, 90)), "sc_back.png")
    for name, fill, text in (("sct_off", RED, "Off"), ("sct_on", GREEN, "On")):
        tw, th = SCP_TOG[2], SCP_TOG[3]
        A[name] = (save(pill(tw, th, fill + (255,), text, 30), name + ".png"),
                   save(pill(tw, th, tuple(int(c * 0.78) for c in fill) + (255,), text, 30), name + "_dn.png"))
    # the meter: frame k = k dB of ducking, the bar fills from the left
    strip = Image.new("RGBA", (mw, mh * SCP_METER_FRAMES), (0, 0, 0, 0))
    for k in range(SCP_METER_FRAMES):
        fr = Image.new("RGBA", (mw * ss, mh * ss), (0, 0, 0, 0))
        df = ImageDraw.Draw(fr)
        df.rounded_rectangle([0, 0, mw * ss - 1, mh * ss - 1], radius=mh * ss // 2, fill=(236, 240, 250, 255),
                             outline=BLUE_DARK + (255,), width=2 * ss)
        if k:
            fw = max(mh, (mw - 4) * k / (SCP_METER_FRAMES - 1))
            df.rounded_rectangle([2 * ss, 2 * ss, (2 + fw) * ss - 1, (mh - 2) * ss - 1], radius=(mh - 4) * ss // 2,
                                 fill=BLUE + (255,))
        strip.paste(fr.resize((mw, mh), Image.Resampling.LANCZOS), (0, k * mh))
    A["sc_meter"] = save(strip, "sc_meter.png")


# ---- layout.conf ---------------------------------------------------------------------------------------------------

def layout(A):
    L = ["# Generated by generate.py -- edit the numbers there, not here.",
         "# Coordinates: x = screen x, y = screen y + 86 (shadow_skin Y_OFF). Plugin area 1280x628.",
         "theme_bg=FFFFFF", "theme_ink=5C6EA0", "theme_ink_dim=8A97BD", "theme_accent=5C6EA0",
         "theme_lcd=FFFFFF", "theme_line=CAD4EF", "theme_seg_inactive=FFFFFF", "theme_seg_active=5C6EA0",
         "theme_seg_active_tx=FFFFFF", "",
         "[tab RACK]", "art file=%s fit=stretch" % A["bg"], ""]
    px0, py0, px1, py1 = A["pill_box"]
    for r in range(ROWS):
        n = r + 1
        top = row_top(r)
        cyr = top + ROW_H / 2
        L.append("# row %d" % n)
        L.append("art file=%s x=%d y=%d w=%d h=%d fit=stretch when=row%d_used:On"
                 % (A["pill"], sx(px0), ly(top + py0), sx(px1) - sx(px0), sy(top + py1) - sy(top + py0), n))
        L.append("art file=%s x=%d y=%d w=%d h=%d fit=stretch when=row%d_par:On"
                 % (A["pill_par"], sx(px0), ly(top + py0), sx(px1) - sx(px0), sy(top + py1) - sy(top + py0), n))
        # Only the pill is baked art (a when= art line becomes its own background crop, which would paint over anything
        # baked next to it); every other part of a row is a button image, which bakes nothing. A dimmed arrow is a
        # button too: the engine ignores "move up" on the first slot and "move down" on the last.
        for key, box, param, when in (("bye", BYE, "bye", "used:On"),
                                      ("up", UP, "mvup", "up:On"), ("down", DOWN, "mvdn", "down:On"),
                                      ("up_dim", UP, "mvup", "up:Dim"), ("down_dim", DOWN, "mvdn", "down:Dim")):
            x0, x1, y0, y1 = box
            img, img_on = (A[key], A[key]) if key.endswith("_dim") else A[key]
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=row%d_%s img=%s img_on=%s when=row%d_%s'
                     % (sx((x0 + x1) / 2), ly(top + (y0 + y1) / 2), sx(x1) - sx(x0), sy(y1 - y0), n, param, img, img_on, n, when))
        cy_ = round(ly(cyr))
        img, img_on = A["open2"]
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=row%d_open img=%s img_on=%s when=row%d_used:On'
                 % (OPEN_CX, cy_, OPEN_W, OPEN_H, n, img, img_on, n))
        # 1.4.0: On/Off (red / green), and on Parallel rows the track's Send and its sidechain (SC: green while ducking)
        for st, k in (("Off", "tog_off"), ("On", "tog_on")):
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=row%d_tog img=%s img_on=%s when=row%d_onst:%s'
                     % (TOG_CX, cy_, TOG_W, TOG_H, n, A[k][0], A[k][1], n, st))
        L.append('knob cx=%d cy=%d r=%d label="" key=row%d_send ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF strip=%s frames=%d '
                 'when=row%d_par:On' % (RSEND_CX, cy_, RSEND_R, n, RSEND_VS, -(13 + RSEND_R + 2), 2 * RSEND_R + 10, A["knob_row"],
                                        KNOB_FRAMES, n))
        for st, k in (("Off", "scb_off"), ("On", "scb_on")):
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=row%d_sc img=%s img_on=%s when=row%d_scst:%s'
                     % (SCB_CX, cy_, SCB_W, SCB_H, n, A[k][0], A[k][1], n, st))
        L.append('readout cx=%d cy=%d w=%d h=%d label="" key=row%d box=0 tsize=32 tweight=bold tcolor=5C6EA0 talign=left '
                 'tpad=0' % ((ROW_NAME[0] + ROW_NAME[1]) // 2, cy_, ROW_NAME[1] - ROW_NAME[0], sy(ROW_H - 20), n))
        L.append("")
    y0, y1 = STRIP_Y
    cy = ly((y0 + y1) / 2)
    # (no when= on readouts or the popup: they bake nothing, but a when= line still gets a background crop over its box,
    # which covered row 1. The hosted pages cover all of this anyway, and the engine blanks the row texts meanwhile.)
    L += ["# top strip: add a plugin (a drawn option list), page through the rack",
          ]
    img, img_on, bw_, bh_ = A["add"]
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=add_open img=%s img_on=%s'
             % (sx((ADD_X[0] + ADD_X[1]) / 2), cy, bw_, bh_, img, img_on))
    # the effect list: 2 columns x 4 rows over the rack box, names from the engine (installed effects, found at start)
    img, img_on, pw, ph = A["pick"]
    for n in range(NPICK):
        col, row = n // ROWS, n % ROWS
        cx_ = sx(PILL[0] + 10) + pw // 2 + col * (pw + 26)
        cy_ = ly(row_top(row) + ROW_H / 2)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=%s img_on=%s tsize=34 get=pick%d_name tcolor=5C6EA0 '
                 'tweight=bold when=pick%d_shown:On' % (cx_, cy_, pw, ph, n + 1, img, img_on, n + 1, n + 1))
    aw, ah = A["page_arrow"]
    for side, x in (("prev", PAGE_X[0] + 22), ("next", PAGE_X[1] - 22)):
        cx = sx(x) + (aw // 2 if side == "prev" else -aw // 2)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=page_%s img=%s img_on=%s when=editing:Off'
                 % (cx, cy, aw, ah, side, A["page_" + side], A["page_%s_on" % side]))
    L.append('readout cx=%d cy=%d w=%d h=%d label="" key=page_text box=0 tsize=30 tweight=bold tcolor=FFFFFF'
             % (sx((PAGE_X[0] + PAGE_X[1]) / 2), cy, sx(PAGE_X[1] - PAGE_X[0]) - 2 * aw - 20, sy(y1 - y0)))
    L.append('readout cx=%d cy=%d w=%d h=%d label="" key=status box=0 tsize=22 tweight=semibold tcolor=FFFFFF'
             % (sx(960), ly(STATUS_Y), sx(1500), 30))
    # the section tabs (1.5.0: in the top strip, between + Add plugin and the page control). The tabs switch which chain
    # the rows, + Add plugin and the page control show.
    L += ["", "# sections: Inserts (in series) / Parallel (fed by Send, added back): the active tab is filled"]
    for (key, act), tcx in zip((("sec_ins", "Inserts"), ("sec_par", "Parallel")), TAB_CX):
        on, off = A["tab_ins" if key == "sec_ins" else "tab_par"]
        other = "Parallel" if act == "Inserts" else "Inserts"
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=%s img_on=%s when=section:%s'
                 % (tcx, cy, TAB_W, TAB_H, key, on, on, act))
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=%s img=%s img_on=%s when=section:%s'
                 % (tcx, cy, TAB_W, TAB_H, key, off, on, other))
    # 1.5.0: macro knobs right of the box (Inserts tab only: on the Parallel tab the Q-Links are the Sends)
    L += ["", "# macros: dots = the Q-Link page shown (tap to switch), 4 knobs, Map (learn) / Clear"]
    dimg, db = A["mk_dot"]
    fimg, _ = A["mk_dot_on"]
    for n in range(4):
        dcx = COL_CX + round((n - 1.5) * MK_DOT_DX)
        L.append("art file=%s x=%d y=%d w=%d h=%d fit=stretch when=mk_page:P%d"
                 % (fimg, dcx - db // 2, MK_DOT_CY + Y_OFF - db // 2, db, db, n + 1))
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=mk_pg%d img=%s img_on=%s when=mk_shown:On'
                 % (dcx, MK_DOT_CY + Y_OFF, db, db, n + 1, dimg, fimg))
    rimg, rb = A["mk_ring"]
    for k, kcy in enumerate(MK_KY):
        n = k + 1
        L.append("art file=%s x=%d y=%d w=%d h=%d fit=stretch when=mk%d_st:Armed"
                 % (rimg, MK_KX - rb // 2, kcy + Y_OFF - rb // 2, rb, rb, n))
        L.append('knob cx=%d cy=%d r=%d label="" key=mk%d ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF strip=%s frames=%d '
                 'when=mk_shown:On' % (MK_KX, kcy + Y_OFF, MK_KR, n, MK_KVS, -(13 + MK_KR + 2), 2 * MK_KR + 10,
                                       A["knob_mk"], KNOB_FRAMES))
        lw, lcx = MK_LBL_X1 - MK_LBL_X0, (MK_LBL_X0 + MK_LBL_X1) // 2
        L.append('readout cx=%d cy=%d w=%d h=%d label="" key=mk%d_name box=0 tsize=20 tweight=bold tcolor=5C6EA0 talign=left '
                 'tpad=0' % (lcx, kcy + Y_OFF - 12, lw, 26, n))
        L.append('readout cx=%d cy=%d w=%d h=%d label="" key=mk%d_fx box=0 tsize=17 tweight=semibold tcolor=7483B0 '
                 'talign=left tpad=0' % (lcx, kcy + Y_OFF + 13, lw, 22, n))
    mcx, mcy, mw, mh = MK_MAP
    for st, k in (("Off", "mk_map_off"), ("On", "mk_map_on")):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=mk_map img=%s img_on=%s when=mk_mapst:%s'
                 % (mcx, mcy + Y_OFF, mw, mh, A[k][0], A[k][1], st))
    ccx, ccy, cw, ch = MK_CLR
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=mk_clear img=%s img_on=%s when=mk_clrst:On'
             % (ccx, ccy + Y_OFF, cw, ch, A["mk_clr"][0], A["mk_clr"][1]))
    # 1.4.0: the sidechain panel of one Parallel track, over the rack (a tap outside it, or X, closes it); last of the rack's
    # lines so it is on top. Every part is a button, knob or meter: they bake nothing, so when= is safe on them.
    x0, y0, w, h = SCP
    L += ["", "# sidechain panel (row SC button): dims the rack, Q-Links 1-6 follow its knobs while it is up"]
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=sc_close img=%s img_on=%s when=scing:On'
             % (W // 2, H // 2 + Y_OFF, W, H, A["sc_back"], A["sc_back"]))
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=sc_panel img=%s img_on=%s when=scing:On'
             % (x0 + w // 2, y0 + h // 2 + Y_OFF, w, h, A["sc_panel"], A["sc_panel"]))
    cx, cy, sz = SCP_X
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=sc_close img=%s img_on=%s when=scing:On'
             % (cx, cy + Y_OFF, sz, sz, A["sc_x"], A["sc_x"]))
    tcx, tcy, tw, th = SCP_TOG
    for st, k in (("Off", "sct_off"), ("On", "sct_on")):
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=sc_tog img=%s img_on=%s when=sc_onst:%s'
                 % (tcx, tcy + Y_OFF, tw, th, A[k][0], A[k][1], st))
    ncx, ncy, nw, nh = SCP_NAME
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=sc_panel img=%s img_on=%s tsize=28 get=sc_name tcolor=5C6EA0 '
             'tweight=bold when=scing:On' % (ncx, ncy + Y_OFF, nw, nh, A["sc_namebg"], A["sc_namebg"]))
    for key, label, row, col in SC_KNOBS:
        L.append('knob cx=%d cy=%d r=%d label="" key=%s ns=0 vs=%d vy=%d bw=%d ink_dim=FFFFFF strip=%s frames=%d when=scing:On'
                 % (SCP_KX[col], SCP_KY[row] + Y_OFF, SCP_KR, key, SCP_KVS, -(13 + SCP_KR + 2), 2 * SCP_KR + 30, A["knob_sc"],
                    KNOB_FRAMES))
    mcx, mcy, mw, mh = SCP_METER
    L.append('meter cx=%d cy=%d w=%d h=%d key=sc_gr strip=%s frames=%d when=scing:On'
             % (mcx, mcy + Y_OFF, mw, mh, A["sc_meter"], SCP_METER_FRAMES))
    L += ["",
          "# editor bar: shown over a hosted plugin's page (skin_post.py puts the hosted pages just below these lines)"]
    img, img_on, bw, bh = A["back"]
    top = Y_OFF + 2 + bh // 2
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=view_back img=%s img_on=%s when=editing:On'
             % (W - bw // 2 - 4, top, bw, bh, img, img_on))
    x = W - 4 - bw - 6   # page arrows (plugins with several pages) to the left of it, same strip
    for k in ("vnext", "vprev"):
        img, img_on, w_, h_ = A[k]
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=view_%s img=%s img_on=%s when=pages:On'
                 % (x - w_ // 2, top, w_, h_, k[1:], img, img_on))
        x -= w_ + 4
    # the opened effect's presets (effects that draw no selector of their own): < name > left of the page arrows
    x = W - 4 - bw - 6 - 2 * (40 + 4) - 6   # right edge, past the page arrows' room
    L += ["", "# the opened effect's presets: tap the name for the browser (hidden for effects with their own selector)"]
    img, img_on = A["hpre_next"]
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=hpre_next img=%s img_on=%s when=hpre_shown:On'
             % (x - HPRE_ARROW_W // 2, top, HPRE_ARROW_W, HPRE_H, img, img_on))
    x -= HPRE_ARROW_W + 4
    img, img_on = A["hpre_name"]
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=hpre_list img=%s img_on=%s tsize=17 get=hpre_name tcolor=FFFFFF '
             'tweight=bold when=hpre_shown:On' % (x - HPRE_NAME_W // 2, top, HPRE_NAME_W, HPRE_H, img, img_on))
    x -= HPRE_NAME_W + 4
    img, img_on = A["hpre_prev"]
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=hpre_prev img=%s img_on=%s when=hpre_shown:On'
             % (x - HPRE_ARROW_W // 2, top, HPRE_ARROW_W, HPRE_H, img, img_on))
    bx, by, bw_, bh_ = BROWSER
    L += ["", "# the preset browser over the hosted page (a tap on its background closes it): 2 x 8 names, paged"]
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=hpre_list img=%s img_on=%s when=hbrowsing:On'
             % (bx + bw_ // 2, by + bh_ // 2 + Y_OFF, bw_, bh_, A["browser"], A["browser"]))
    hy = by + 42 + Y_OFF
    for k, cx_ in (("hpg_prev", bx + bw_ - 290), ("hpg_next", bx + bw_ - 60)):
        img, img_on = A[k]
        L.append('button cx=%d cy=%d w=56 h=44 label="" key=hpre_%s img=%s img_on=%s when=hbrowsing:On'
                 % (cx_, hy, "pprev" if k == "hpg_prev" else "pnext", img, img_on))
    img, img_on = A["hpg_text"]
    L.append('button cx=%d cy=%d w=150 h=44 label="" key=hpre_pnext img=%s img_on=%s tsize=24 get=hpre_page tcolor=5C6EA0 '
             'tweight=bold when=hbrowsing:On' % (bx + bw_ - 175, hy, img, img_on))
    y0 = by + 92
    gap = (bh_ - 92 - 16 - BROW_ROWS * BROW_ROW_H) / (BROW_ROWS - 1)
    for n in range(NHPRE):
        col, row = n // BROW_ROWS, n % BROW_ROWS
        cx_ = bx + 24 + BROW_ROW_W // 2 + col * (BROW_ROW_W + 28)
        cy_ = round(y0 + BROW_ROW_H / 2 + row * (BROW_ROW_H + gap)) + Y_OFF
        for state, (img, img_on), ink in (("Shown", A["brow_row"], "5C6EA0"), ("Current", A["brow_cur"], "FFFFFF")):
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=hpick%d img=%s img_on=%s tsize=26 get=hpre%d_name tcolor=%s '
                     'tweight=bold when=hpre%d_state:%s' % (cx_, cy_, BROW_ROW_W, BROW_ROW_H, n + 1, img, img_on, n + 1, ink,
                                                            n + 1, state))
    # Q-Links: our proxy q<n> sits on Q-Link n (shadow_skin's slot order: slot s -> Q-Link KNOB_QLINKS[s % 8] + 2 * (s // 8)),
    # so the engine can map Q-Link n straight to the hosted plugin's own Q-Link n
    kq = [13, 9, 5, 1, 14, 10, 6, 2]
    order = ["q%d" % (kq[s % 8] + 2 * (s // 8)) for s in range(16)]
    L += ["", 'qlinks "RACK" = ' + ",".join(order), ""]
    with open(os.path.join(VST, "layout.conf"), "w", newline="\n") as f:
        f.write("\n".join(L))


# ---- the engine's tables -------------------------------------------------------------------------------------------

def c_str(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def gen_header(P):
    idx = {p["key"]: i for i, p in enumerate(P)}
    L = ["/* generated by generate.py from hosted.json: do not edit */", "#pragma once",
         "#define PP_MAX_SLOTS %d" % MAX_SLOTS, "#define PP_ROWS %d" % ROWS, "#define PP_PAGES %d" % PAGES,
         "#define PP_MAX_EDIT %d" % MAX_EDIT, "#define PP_NQ %d" % NQ, "#define PP_NPICK %d" % NPICK,
         "#define PP_NVIEW_OPTS %d" % VIEWS, "#define PP_NKEYS %d" % len(P), ""]
    L += ["#define PP_NPROG %d" % NPROG, "#define PP_NHPRE %d" % NHPRE]
    for k in ("page", "page_prev", "page_next", "page_text", "add_open", "picking", "status", "view", "editing", "pages", "view_back", "view_prev", "view_next",
              "hpreset", "hpre_name", "hpre_shown", "hbrowsing", "hpre_page", "section", "send",
              "scing", "sc_onst", "sc_name", "sc_thr", "sc_att", "sc_rel", "sc_knee", "sc_ratio", "sc_gr",
              "mk_shown", "mk_page", "mk_mapst", "mk_clrst"):
        L.append("#define PI_%s %d" % (k.upper(), idx[k]))
    L += ["#define PI_ROW(r) (%d + (r) * 8)    /* r = 0..PP_ROWS-1; then +1 used, +2 up, +3 down, +4 open, +5 mvup, +6 mvdn, +7 bye */"
          % idx["row1"],
          "#define PI_Q(n) (%d + (n))          /* n = 0..PP_NQ-1 (q1..q16) */" % idx["q1"],
          "#define PI_E(n) ((n) < %d ? %d + (n) : %d + (n) - %d)   /* n = 0..PP_MAX_EDIT-1 (two runs: 1.2's and 1.3.1's) */"
          % (E_LO, idx["e0"], idx["e%d" % E_LO], E_LO),
          "static const char *const PP_KEYS[PP_NKEYS] = {" + ", ".join(c_str(p["key"]) for p in P) + "};",
          "#define PI_PICK(n) (%d + (n) * 3)  /* n = 0..PP_NPICK-1; +1 name, +2 shown */" % idx["pick1"],
          "#define PI_HPICK(n) (%d + (n) * 3)  /* n = 0..PP_NHPRE-1; +1 name, +2 state */" % idx["hpick1"],
          "#define PI_RX(r) (%d + (r) * 6)    /* 1.4.0, r = 0..PP_ROWS-1: +0 onst, +1 tog, +2 par, +3 send, +4 scst, +5 sc */"
          % idx["row1_onst"],
          "#define PP_SC_ATT_LO %.1ff\n#define PP_SC_ATT_HI %.1ff\n#define PP_SC_REL_LO %.1ff\n#define PP_SC_REL_HI %.1ff"
          % (SC_ATT + SC_REL),
          "#define PP_SC_RATIO_LO %.1ff\n#define PP_SC_RATIO_HI %.1ff\n#define PP_SC_METER_FRAMES %d" % (SC_RATIO + (SCP_METER_FRAMES,)),
          "#define PI_MK(k) (%d + (k) * 4)    /* 1.5.0, k = 0..3 (the knob shown): +0 value, +1 state, +2 name, +3 effect */"
          % idx["mk1"],
          "#define PP_NMACRO 16",
          ""]
    assert [P[idx["mk1"] + 4 * k + j]["key"] for k in range(4) for j in range(4)] == \
        ["mk%d%s" % (k + 1, w) for k in range(4) for w in ("", "_st", "_name", "_fx")]
    assert [P[idx["row1_onst"] + 6 * r + k]["key"] for r in range(ROWS) for k in range(6)] == \
        ["row%d_%s" % (r + 1, w) for r in range(ROWS) for w in ("onst", "tog", "par", "send", "scst", "sc")]

    with open(os.path.join(SRC, "pp_gen.h"), "w", newline="\n") as f:
        f.write("\n".join(L))


def main():
    for d in (OUT, SRC):
        os.makedirs(d, exist_ok=True)
    P = params()
    with open(os.path.join(VST, "params.json"), "w", newline="\n") as f:
        json.dump({"name": "PoliPlug", "params": P}, f, indent=1)
    A = make_art()
    layout(A)
    gen_header(P)
    hdr = open(os.path.join(SRC, "discover.h")).read()
    assert "#define PP_MAX_VIEWS %d " % VIEWS in hdr, "VIEWS must match PP_MAX_VIEWS in src/discover.h"
    print("PoliPlug: %d params; hosted effects are found on the MPC at start" % len(P))


if __name__ == "__main__":
    main()
