"""The preset selector shared by the Poli plugins: the current preset's name between two arrows, and a browser of every
preset that opens when the name is tapped. Each plugin's generate_skin.py calls this with its own colours, font and
placement, so the selector is drawn in that plugin's style. The engine side is shared/preset_nav.h (copied into src/).

    import poli_preset_ui as ppu            (sys.path: ../shared)
    ppu.check_params(params_json, names)    the parameters exist, in place (append-only), with the right options
    lines = ppu.build(vst_dir, names, style, sel=..., panel=..., columns=...)
    -> layout.conf lines, to go after every other control (the browser has to sit on top of them)

Parameters (appended to params.json once, by ensure_params()):
    preset (option list = names; Multiband already had it), preset_prev, preset_next, preset_list, browsing, pre_hl,
    pick1..pickN. The momentary ones carry MPC-facing names ("Preset <", "Preset >", "Preset list"); PoliPlug looks for
    "Preset <" to know a hosted plugin draws its own selector.

Widgets (layout.conf, all of them buttons or a picture, which bake nothing, so when= is safe on them):
    picture   the name: one image per preset, in the plugin's own font (MPC would draw a readout in its own)
    button    preset_list over the name (transparent), preset_prev / preset_next arrows
    button    the browser's panel (key preset_list: a tap on its background closes it), when=browsing:On
    button    one cell per preset (key pick<n>), when=browsing:On; the same cell highlighted, when=pre_hl:<n>
Coordinates given to build() are SCREEN pixels (1280x628); layout.conf y = screen y + 86.
"""
import json
import os
import shutil

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OPEN_SANS = os.path.join(HERE, "fonts", "OpenSans.ttf")   # variable (wght 300..800), SIL OFL (fonts/OFL.txt)
Y_OFF = 86
SS = 3   # supersampling for every drawing


class Style:
    def __init__(self, ink, font=OPEN_SANS, weight=700, name_px=30, cell_px=24, title="Presets", title_px=30,
                 panel_fill=(255, 255, 255), panel_border=None, border=5, radius=26, panel_alpha=255,
                 cell_fill=None, cell_border=3, cell_radius=None, hl_fill=None, hl_ink=(255, 255, 255),
                 press_fill=None, sel_fill=None, sel_border=None, sel_border_w=0, sel_radius=0, name_ink=None,
                 arrow=None, arrow_on=None, arrow_shape="triangle", head_fill=None, head_ink=(255, 255, 255),
                 head_px=None):
        self.ink, self.font, self.weight = ink, font, weight
        self.name_px, self.cell_px, self.title, self.title_px = name_px, cell_px, title, title_px
        self.panel_fill, self.panel_border, self.border, self.radius = panel_fill, panel_border or ink, border, radius
        self.panel_alpha = panel_alpha
        self.cell_fill, self.cell_border = cell_fill, cell_border
        self.cell_radius = cell_radius
        self.hl_fill, self.hl_ink = hl_fill or ink, hl_ink
        self.press_fill = press_fill
        self.sel_fill, self.sel_border, self.sel_border_w, self.sel_radius = sel_fill, sel_border, sel_border_w, sel_radius
        self.name_ink = name_ink or ink
        self.arrow, self.arrow_on = arrow or self.name_ink, arrow_on
        self.arrow_shape = arrow_shape
        self.head_fill, self.head_ink, self.head_px = head_fill or ink, head_ink, head_px or cell_px


def font(style, px, weight=None):
    f = ImageFont.truetype(style.font, int(round(px)))
    try:
        axes = f.get_variation_axes()
        f.set_variation_by_axes([weight or style.weight] + [a["default"] for a in axes[1:]])
    except (OSError, AttributeError):
        pass   # not a variable font
    return f


def fitted(style, text, max_w, px, weight=None):
    """The largest font size <= px whose rendering of text is at most max_w wide."""
    while px > 10:
        f = font(style, px * SS, weight)
        if f.getlength(text) <= max_w * SS:
            return f
        px -= 1
    return font(style, 10 * SS, weight)


def centred_text(d, box, text, f, fill):
    x0, y0, x1, y1 = box
    l, t, r, b = d.textbbox((0, 0), text, font=f, anchor="ls")
    asc, desc = f.getmetrics()
    # centre on the cap height, not the box of this string (descenders would push names up and down)
    cap = f.getbbox("H", anchor="ls")
    ch = cap[3] - cap[1]
    d.text(((x0 + x1) / 2 - (r - l) / 2 - l, (y0 + y1) / 2 + ch / 2), text, font=f, fill=fill, anchor="ls")


def down(im, w, h):
    return im.resize((w, h), Image.Resampling.LANCZOS)


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


def triangle(w, h, left, colour, shape="triangle"):
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = min(w, h) * SS
    cx, cy = w * SS / 2, h * SS / 2
    if shape == "chevron":   # an open ">" stroke
        lw = max(3, s // 7)
        a, b = s * 0.22, s * 0.36
        pts = [(cx + a / 1.3, cy - b), (cx - a, cy), (cx + a / 1.3, cy + b)] if left else \
              [(cx - a / 1.3, cy - b), (cx + a, cy), (cx - a / 1.3, cy + b)]
        d.line(pts, fill=rgba(colour), width=int(lw), joint="curve")
        for p in (pts[0], pts[2]):
            d.ellipse([p[0] - lw / 2, p[1] - lw / 2, p[0] + lw / 2, p[1] + lw / 2], fill=rgba(colour))
    else:
        a, b = s * 0.30, s * 0.34
        pts = [(cx + a * 0.8, cy - b), (cx + a * 0.8, cy + b), (cx - a, cy)] if left else \
              [(cx - a * 0.8, cy - b), (cx - a * 0.8, cy + b), (cx + a, cy)]
        d.polygon(pts, fill=rgba(colour))
    return down(im, w, h)


def shade(c, f):
    return tuple(int(v * f) for v in c[:3])


def mixc(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


# ---- parameters --------------------------------------------------------------------------------------------------

def wanted_params(names):
    n = len(names)
    mom = lambda k, nm: {"key": k, "name": nm, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}
    P = [{"key": "preset", "name": "Preset", "options": list(names), "default": 0, "refresh_display": True,
          "qlink_ticks": 2},
         mom("preset_prev", "Preset <"), mom("preset_next", "Preset >"), mom("preset_list", "Preset list"),
         {"key": "browsing", "name": "Browsing", "options": ["Off", "On"], "default": 0, "live": True},
         {"key": "pre_hl", "name": "Preset shown", "options": ["-"] + [str(i + 1) for i in range(n)], "default": 0,
          "live": True}]
    P += [mom("pick%d" % (i + 1), "Pick %d" % (i + 1)) for i in range(n)]
    return P


def ensure_params(params_path, names):
    """Append the selector's parameters to params.json (append-only: projects store values by position). A parameter
    already there keeps its place; its options are brought up to date (the preset list may grow at the end)."""
    d = json.load(open(params_path))
    P = d["params"]
    have = {p["key"]: p for p in P}
    for w in wanted_params(names):
        if w["key"] in have:
            p = have[w["key"]]
            if "options" in w:
                old = p.get("options") or []
                assert w["options"][:len(old)] == old or w["key"] == "pre_hl", \
                    "%s: presets can only be added at the end (%s)" % (w["key"], params_path)
                p["options"] = w["options"]
            continue
        P.append(w)
    keys = [p["key"] for p in P]
    for i in range(len(names), 999):   # picks beyond the list are never shown; keep them (append-only), they do nothing
        if "pick%d" % (i + 1) not in keys:
            break
    with open(params_path, "w", newline="\n") as f:
        json.dump(d, f, indent=1)
        f.write("\n")


# ---- art + layout ------------------------------------------------------------------------------------------------

def build(vst_dir, names, style, sel, panel, columns=None, grid=None, shown=None, arrow_w=None, gap=10, src_dir=None,
          cell_h=None, cell_gap=10, pad=22, title_h=None, arrow_inset=0):
    """Draw the selector and the browser into vst/skin_assets/preset_*.png and return their layout.conf lines.
    sel = (x, y, w, h): the selector (arrows inside at both ends); panel = (x, y, w, h): the browser.
    columns = [[("h", "Heading") | ("p", index), ...], ...] (grouped), or grid = (cols, rows) in preset order.
    shown = the names as the cells and the selector show them (default: names)."""
    out = os.path.join(vst_dir, "skin_assets")
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.startswith("preset_"):
            os.remove(os.path.join(out, f))
    if src_dir:   # the engine half, beside the engine's sources (the Docker build only sees the plugin's own folder)
        shutil.copy2(os.path.join(HERE, "preset_nav.h"), os.path.join(src_dir, "preset_nav.h"))
    shown = shown or names
    n = len(names)
    L = ["", "# preset selector (shared/poli_preset_ui.py): the name, arrows either side; tap the name for the browser"]
    sx, sy, sw, sh = sel
    aw = arrow_w or sh
    # name pictures: the selector box (if the style has one) with the name centred between the arrows
    files = []
    for i, nm in enumerate(shown):
        im = Image.new("RGBA", (sw * SS, sh * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        if style.sel_fill is not None or style.sel_border is not None:
            bw = style.sel_border_w * SS
            d.rounded_rectangle([bw / 2, bw / 2, sw * SS - 1 - bw / 2, sh * SS - 1 - bw / 2], radius=style.sel_radius * SS,
                                fill=rgba(style.sel_fill) if style.sel_fill is not None else None,
                                outline=rgba(style.sel_border) if style.sel_border is not None else None, width=int(bw))
        f = fitted(style, nm, sw - 2 * (aw + arrow_inset + gap), style.name_px)
        centred_text(d, (aw * SS, 0, (sw - aw) * SS, sh * SS), nm, f, rgba(style.name_ink))
        name = "preset_name_%d.png" % i
        down(im, sw, sh).save(os.path.join(out, name), optimize=True)
        files.append("skin_assets/" + name)
    L.append('picture x=%d y=%d w=%d h=%d key=preset files="%s" fit=stretch' % (sx, sy + Y_OFF, sw, sh, ",".join(files)))
    Image.new("RGBA", (8, 8), (0, 0, 0, 0)).save(os.path.join(out, "preset_hit.png"))
    L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/preset_hit.png '
             'img_on=skin_assets/preset_hit.png' % (sx + sw // 2, sy + sh // 2 + Y_OFF, sw - 2 * (aw + arrow_inset), sh))
    on = style.arrow_on or (mixc(style.arrow, (255, 255, 255), 0.45) if sum(style.arrow) < 600 else shade(style.arrow, 0.6))
    for side, left, x in (("prev", True, sx + arrow_inset), ("next", False, sx + sw - aw - arrow_inset)):
        triangle(aw, sh, left, style.arrow, style.arrow_shape).save(os.path.join(out, "preset_%s.png" % side))
        triangle(aw, sh, left, on, style.arrow_shape).save(os.path.join(out, "preset_%s_on.png" % side))
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=preset_%s img=skin_assets/preset_%s.png '
                 'img_on=skin_assets/preset_%s_on.png' % (x + aw // 2, sy + sh // 2 + Y_OFF, aw, sh, side, side, side))

    # the browser: panel (title, headings), then the cells
    px, py, pw, ph = panel
    th = title_h if title_h is not None else (style.title_px + 26 if style.title else 0)
    if columns is None:
        cols, rows = grid
        columns = [[("p", c * rows + r) for r in range(rows) if c * rows + r < n] for c in range(cols)]
    nrows = max(len(c) for c in columns)
    ncol = len(columns)
    cw = (pw - 2 * pad - (ncol - 1) * cell_gap) // ncol
    ch = cell_h or min(70, (ph - th - 2 * pad - (nrows - 1) * cell_gap) // nrows)
    ystart = py + th + pad
    pan = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(pan)
    b = style.border * SS
    d.rounded_rectangle([b / 2, b / 2, pw * SS - 1 - b / 2, ph * SS - 1 - b / 2], radius=style.radius * SS,
                        fill=rgba(style.panel_fill, style.panel_alpha), outline=rgba(style.panel_border) if b else None,
                        width=int(b))
    if style.title:
        tf = font(style, style.title_px * SS)
        d.text(((pad + 4) * SS, (pad - 4) * SS + style.title_px * SS * 0.9), style.title, font=tf, fill=rgba(style.ink),
               anchor="ls")
    cells = {}
    for c, col in enumerate(columns):
        for r, (kind, v) in enumerate(col):
            x0 = px + pad + c * (cw + cell_gap)
            y0 = ystart + r * (ch + cell_gap)
            if kind == "h":   # a heading: a small filled pill, baked into the panel
                hf = fitted(style, v, cw - 24, style.head_px)
                tw = hf.getlength(v) / SS + 28
                hh = int(ch * 0.78)
                hy = y0 - px * 0 + (ch - hh)
                bx0, by0 = (x0 - px) * SS, (hy - py) * SS
                d.rounded_rectangle([bx0, by0, bx0 + tw * SS, by0 + hh * SS], radius=hh * SS * 0.3, fill=rgba(style.head_fill))
                centred_text(d, (bx0, by0, bx0 + tw * SS, by0 + hh * SS), v, hf, rgba(style.head_ink))
            else:
                cells[v] = (x0, y0)
    pan = down(pan, pw, ph)
    pan.save(os.path.join(out, "preset_panel.png"), optimize=True)
    L += ["", "# the preset browser: on top of everything, shown while browsing (a tap on its background closes it)",
          'button cx=%d cy=%d w=%d h=%d label="" key=preset_list img=skin_assets/preset_panel.png '
          'img_on=skin_assets/preset_panel.png when=browsing:On' % (px + pw // 2, py + ph // 2 + Y_OFF, pw, ph)]
    rad = style.cell_radius if style.cell_radius is not None else ch * 0.3
    press = style.press_fill or mixc(style.hl_fill, (255, 255, 255), 0.55)
    hl_lines = []
    cell_font = min((fitted(style, t, cw - 24, style.cell_px) for t in shown), key=lambda f: f.size)   # one size for all
    for i in range(n):
        if i not in cells:
            raise SystemExit("poli_preset_ui: preset %d (%s) has no cell in the browser" % (i, names[i]))
        x0, y0 = cells[i]
        for variant, fill, ink, outline in (("", style.cell_fill, style.ink, style.ink), ("_on", press, style.ink, style.ink),
                                           ("_hl", style.hl_fill, style.hl_ink, style.hl_fill),
                                           ("_hlon", shade(style.hl_fill, 0.75) if sum(style.hl_fill) > 150 else mixc(style.hl_fill, (255, 255, 255), 0.3),
                                            style.hl_ink, style.hl_fill)):
            im = Image.new("RGBA", (cw * SS, ch * SS), (0, 0, 0, 0))
            dd = ImageDraw.Draw(im)
            cb = style.cell_border * SS
            dd.rounded_rectangle([cb / 2, cb / 2, cw * SS - 1 - cb / 2, ch * SS - 1 - cb / 2], radius=rad * SS,
                                 fill=rgba(fill) if fill is not None else None, outline=rgba(outline) if cb else None,
                                 width=int(cb))
            centred_text(dd, (0, 0, cw * SS, ch * SS), shown[i], cell_font, rgba(ink))
            down(im, cw, ch).save(os.path.join(out, "preset_cell_%d%s.png" % (i, variant)), optimize=True)
        L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/preset_cell_%d.png '
                 'img_on=skin_assets/preset_cell_%d_on.png when=browsing:On' % (x0 + cw // 2, y0 + ch // 2 + Y_OFF, cw, ch, i + 1, i, i))
        hl_lines.append('button cx=%d cy=%d w=%d h=%d label="" key=pick%d img=skin_assets/preset_cell_%d_hl.png '
                        'img_on=skin_assets/preset_cell_%d_hlon.png when=pre_hl:%d' % (x0 + cw // 2, y0 + ch // 2 + Y_OFF, cw, ch, i + 1, i, i, i + 1))
    L += ["# the current preset, highlighted (over its cell)"] + hl_lines
    return L


def preview(vst_dir, bg_png, layout_lines, preset=0, browsing=False, out=None):
    """A quick picture of the selector (and the browser) over a background, for checking placement offline."""
    im = Image.open(bg_png).convert("RGBA").resize((1280, 628))
    import re
    import shlex
    for line in layout_lines:
        if not line or line.startswith("#"):
            continue
        parts = shlex.split(line)
        kind, kv = parts[0], dict(p.split("=", 1) for p in parts[1:] if "=" in p)
        when = kv.get("when")
        if when:
            k, v = when.split(":")
            if k == "browsing" and (v == "On") != browsing:
                continue
            if k == "pre_hl" and (not browsing or int(v) != preset + 1):
                continue
        if kind == "picture" and kv.get("key") == "preset":
            f = kv["files"].split(",")[preset]
            p = Image.open(os.path.join(vst_dir, f)).convert("RGBA")
            im.alpha_composite(p, (int(kv["x"]), int(kv["y"]) - Y_OFF))
        elif kind == "button" and "img" in kv:
            p = Image.open(os.path.join(vst_dir, kv["img"])).convert("RGBA")
            w, h = int(kv["w"]), int(kv["h"])
            p = p.resize((w, h)) if p.size != (w, h) else p
            im.alpha_composite(p, (int(kv["cx"]) - w // 2, int(kv["cy"]) - Y_OFF - h // 2))
    if out:
        im.convert("RGB").save(out)
    return im
