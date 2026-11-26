#!/usr/bin/env python3
"""Native re-layout helpers for the mockup-based skins (Poli-verb, Poli-delay, Poli-Multiband).

The mockups are 1920x1080 pictures whose design ends near x=1054 of the 1280 px plugin area. Stretching the picture
sideways spreads the controls but also squashes nothing and stretches everything: letters get wide, rings get thick sides.
This module does it the other way round: the flat parts (colour blobs, panel fill) are stretched (they are shapeless),
and everything with a shape of its own is lifted off the mockup as a sprite and put back at its natural aspect:

    * text / labels / pills  -> sprites, scaled uniformly by S, centred on their stretched x position
    * rules and underlines   -> redrawn as bars from the stretched start to the stretched end
    * the panel's outline    -> redrawn as a rounded rectangle with a constant stroke
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# No scipy: the build machines (WSL) only have numpy + Pillow, so the few image-processing helpers are written here.


def _box_count(mask, kx, ky):
    """number of True pixels in the (2kx+1) x (2ky+1) window around every pixel (zero outside)"""
    m = np.pad(mask.astype(np.int32), ((ky + 1, ky), (kx + 1, kx)))
    c = m.cumsum(0).cumsum(1)
    h, w = mask.shape
    return c[2 * ky + 1:2 * ky + 1 + h, 2 * kx + 1:2 * kx + 1 + w] - c[:h, 2 * kx + 1:2 * kx + 1 + w] \
        - c[2 * ky + 1:2 * ky + 1 + h, :w] + c[:h, :w]


def dilate(mask, kx, ky=None):
    ky = kx if ky is None else ky
    return _box_count(mask, kx, ky) > 0


def erode(mask, kx, ky=None):
    ky = kx if ky is None else ky
    return _box_count(~mask, kx, ky) == 0


def opening(mask, k):      # k odd
    return dilate(erode(mask, k // 2), k // 2)


def closing(mask, k):
    return erode(dilate(mask, k // 2), k // 2)


def label(mask):
    """4-connected components of a boolean mask -> (labels 1..n, n). Run based union-find: fast enough for 1920x1080."""
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    parent = [0]

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    prev = []        # (x0, x1, id) of the previous row
    for y in range(h):
        row = mask[y]
        if not row.any():
            prev = []
            continue
        d = np.diff(np.concatenate(([0], row.astype(np.int8), [0])))
        starts, ends = np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]
        cur = []
        j = 0
        for x0, x1 in zip(starts, ends):
            rid = len(parent)
            parent.append(rid)
            while j < len(prev) and prev[j][1] <= x0:
                j += 1
            k = j
            while k < len(prev) and prev[k][0] < x1:
                a, b = find(rid), find(prev[k][2])
                if a != b:
                    parent[max(a, b)] = min(a, b)
                k += 1
            cur.append((x0, x1, rid))
            lab[y, x0:x1] = rid
        prev = cur
    roots = np.array([find(i) for i in range(len(parent))])
    uniq, inv = np.unique(roots[1:], return_inverse=True) if len(parent) > 1 else (np.array([]), np.array([], int))
    remap = np.zeros(len(parent), np.int32)
    remap[1:] = inv + 1
    return remap[lab], len(uniq)


def objects(lab, n):
    """bounding slices of labels 1..n (like find_objects)"""
    out = [None] * n
    ys, xs = np.nonzero(lab)
    ids = lab[ys, xs] - 1
    if not len(ids):
        return out
    for i in range(n):
        pass
    order = np.argsort(ids, kind="stable")
    ids, ys, xs = ids[order], ys[order], xs[order]
    cuts = np.concatenate(([0], np.nonzero(np.diff(ids))[0] + 1))
    for c, st in enumerate(cuts):
        en = cuts[c + 1] if c + 1 < len(cuts) else len(ids)
        out[ids[st]] = (slice(ys[st:en].min(), ys[st:en].max() + 1), slice(xs[st:en].min(), xs[st:en].max() + 1))
    return out


def nearest_fill(arr, unknown):
    """arr HxWx3: every pixel in `unknown` takes the colour of the nearest known pixel (jump flooding)"""
    if not unknown.any():
        return arr.copy()
    h, w = unknown.shape
    yy, xx = np.mgrid[0:h, 0:w]
    iy = np.where(unknown, -10 ** 6, yy)          # index of the seed each pixel points at (far away while unknown)
    ix = np.where(unknown, -10 ** 6, xx)
    step = 1 << (max(h, w).bit_length() - 1)
    while step >= 1:
        for _ in range(2 if step == 1 else 1):
            best_d = (iy - yy) ** 2 + (ix - xx) ** 2
            for dy in (-step, 0, step):
                for dx in (-step, 0, step):
                    if not dy and not dx:
                        continue
                    sy_, sx_ = np.clip(yy + dy, 0, h - 1), np.clip(xx + dx, 0, w - 1)
                    cy, cx = iy[sy_, sx_], ix[sy_, sx_]
                    d = (cy - yy) ** 2 + (cx - xx) ** 2
                    better = d < best_d
                    iy, ix, best_d = np.where(better, cy, iy), np.where(better, cx, ix), np.where(better, d, best_d)
        step //= 2
    return arr[np.clip(iy, 0, h - 1), np.clip(ix, 0, w - 1)]


def box_mask(shape, boxes, grow=0):
    m = np.zeros(shape[:2], bool)
    for x0, y0, x1, y1 in boxes:
        m[max(0, int(y0 - grow)):int(y1 + grow), max(0, int(x0 - grow)):int(x1 + grow)] = True
    return m


def lift(mock, base, boxes, thr=42, merge=(22, 6), line_ratio=14, pad=2, open_px=0):
    """sprites (and bars) found in `boxes` of the mockup: pixels that differ from `base`.
    Returns a list of dicts: kind 'sprite' (box, rgba) or 'bar' (box, colour)."""
    m = np.asarray(mock.convert("RGB"), np.int16)
    b = np.asarray(base.convert("RGB"), np.int16)
    diff = np.abs(m - b).max(axis=2) > thr
    diff &= box_mask(diff.shape, boxes)
    if open_px:
        diff = opening(diff, open_px | 1)     # drops hairlines where a base edge moved
    lab, n = label(dilate(diff, merge[0], merge[1]))
    out = []
    for i, sl in enumerate(objects(lab, n), 1):
        comp = diff[sl] & (lab[sl] == i)
        if comp.sum() < 12:
            continue
        ys, xs = np.nonzero(comp)
        x0, x1 = xs.min() + sl[1].start, xs.max() + sl[1].start + 1
        y0, y1 = ys.min() + sl[0].start, ys.max() + sl[0].start + 1
        w, h = x1 - x0, y1 - y0
        pix = m[y0:y1, x0:x1][comp[ys.min():ys.max() + 1, xs.min():xs.max() + 1]]
        if w >= line_ratio * h:
            out.append(dict(kind="bar", box=(x0, y0, x1, y1), colour=tuple(int(v) for v in np.median(pix, axis=0))))
            continue
        x0p, y0p, x1p, y1p = x0 - pad, y0 - pad, x1 + pad, y1 + pad
        crop = np.asarray(mock.convert("RGB").crop((x0p, y0p, x1p, y1p)))
        cm = np.zeros((y1p - y0p, x1p - x0p), bool)
        cm[pad:pad + h, pad:pad + w] = comp[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        cm = dilate(cm, 1)
        alpha = Image.fromarray((cm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7))
        rgba = Image.fromarray(crop).convert("RGBA")
        rgba.putalpha(alpha)
        out.append(dict(kind="sprite", box=(x0, y0, x1, y1), rgba=rgba, pad=pad))
    return out


def place(canvas, items, mapx, S, anchor=None, dy=0.0):
    """paste lifted items on `canvas` (RGB screen image). mapx: mock x -> screen x. anchor(item) -> 'l' | 'c' | 'r'
    (default centre). Sprites keep their natural aspect (scaled by S); bars run from mapx(x0) to mapx(x1)."""
    d = ImageDraw.Draw(canvas)
    for it in items:
        x0, y0, x1, y1 = it["box"]
        if it["kind"] == "bar":
            sx0, sx1 = mapx(x0), mapx(x1)
            sy0, sy1 = y0 * S + dy, max(y1 * S, y0 * S + 2) + dy
            d.rectangle([round(sx0), round(sy0), round(sx1) - 1, round(max(sy1, sy0 + 1.5)) - 1], fill=it["colour"])
            continue
        spr = it["rgba"]
        w2, h2 = max(1, round(spr.width * S)), max(1, round(spr.height * S))
        spr = spr.resize((w2, h2), Image.Resampling.LANCZOS)
        a = anchor(it) if anchor else "c"
        cx = (x0 + x1) / 2
        if a == "l":
            px = mapx(x0) - it["pad"] * S
        elif a == "r":
            px = mapx(x1) - w2 + it["pad"] * S
        else:
            px = mapx(cx) - w2 / 2
        py = (y0 + y1) / 2 * S + dy - h2 / 2
        canvas.paste(spr, (round(px), round(py)), spr)


def ring(canvas, box, thick, radius, colour, SS=4):
    """a rounded-rectangle outline of constant stroke (box: screen px, the centre line of the stroke)"""
    x0, y0, x1, y1 = box
    w, h = int(x1 - x0 + thick * 2 + 4), int(y1 - y0 + thick * 2 + 4)
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    o = thick + 2
    d.rounded_rectangle([(o - thick / 2) * SS, (o - thick / 2) * SS, (o + (x1 - x0) + thick / 2) * SS, (o + (y1 - y0) + thick / 2) * SS],
                        radius=(radius + thick / 2) * SS, fill=255)
    d.rounded_rectangle([(o + thick / 2) * SS, (o + thick / 2) * SS, (o + (x1 - x0) - thick / 2) * SS, (o + (y1 - y0) - thick / 2) * SS],
                        radius=max(1, (radius - thick / 2)) * SS, fill=0)
    mask = big.resize((w, h), Image.Resampling.LANCZOS)
    canvas.paste(Image.new("RGB", (w, h), colour), (int(x0 - o), int(y0 - o)), mask)


def panel_fill(canvas, box, radius, colour, SS=4):
    x0, y0, x1, y1 = box
    w, h = int(x1 - x0), int(y1 - y0)
    big = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(big).rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=radius * SS, fill=255)
    mask = big.resize((w, h), Image.Resampling.LANCZOS)
    canvas.paste(Image.new("RGB", (w, h), colour), (int(x0), int(y0)), mask)


def blob_fill(arr, box, light, dark, ctx=60, close=17, tol=90):
    """repaint `box` as the two-colour blob picture it sits on: pixels near `light` stay light after a closing (which swallows
    letters drawn in `dark` on top of it), everything else is `dark`. For titles lying across a blob's edge."""
    h, w = arr.shape[:2]
    x0, y0, x1, y1 = [int(v) for v in box]
    X0, Y0, X1, Y1 = max(0, x0 - ctx), max(0, y0 - ctx), min(w, x1 + ctx), min(h, y1 + ctx)
    win = arr[Y0:Y1, X0:X1].astype(int)
    lit = np.abs(win - np.array(light)).sum(axis=2) < tol
    pad = close * 2
    litp = np.pad(lit, pad, mode="edge")
    closed = closing(litp, close | 1)[pad:-pad, pad:-pad]
    closed = ~opening(~closed, 9)      # and slivers of dark inside the light go too
    out = arr.copy()
    sub = np.where(closed[..., None], np.array(light, np.uint8), np.array(dark, np.uint8))
    out[y0:y1, x0:x1] = sub[y0 - Y0:y1 - Y0, x0 - X0:x1 - X0]
    return out
