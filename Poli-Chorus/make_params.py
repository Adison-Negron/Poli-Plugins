#!/usr/bin/env python3
"""Writes vst/params.json: every Poli-Chorus parameter, in VST index order (append only: projects store values by position).

Knobs are positions 0..1000 (the engine maps them to Hz / % / dB; "dynamic_display" shows the real value under the knob).
The display box is vl001.. (left LFO, black) and vr001.. (right LFO, red): read-only "live" values 0..1 = the frame of that column's meter.
"""
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "presets"))
import factory  # noqa: E402

KNOBS = [  # key, name, default position (ChoDSP.h kDefaultPos)
    ("rate", "Rate", 523), ("amount", "Amount", 500), ("feedback", "Feedback", 0), ("width", "Width", 600),
    ("lpf", "LP Filter", 1000), ("warmth", "Warmth", 250), ("output", "Output", 667), ("mix", "Mix", 500),
]


def mom(key, name):
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}


def views():
    """(category, page) for every browser view, in the order chorus_nav.h numbers them."""
    out = []
    for c, name in enumerate(factory.CAT_NAMES):
        n = len(factory.PRESETS) if c == 0 else sum(1 for d in factory.PRESETS if d["cat"] == name)
        for p in range(max(1, (n + 11) // 12)):
            out.append((c, p))
    return out


def viz_constants():
    """kVizCols, kVizFrames, kVizAmpMax from src/layout_gen.h (written by generate_skin.py)"""
    t = open(os.path.join(HERE, "src", "layout_gen.h")).read()
    return (int(re.search(r"kVizCols = (\d+)", t).group(1)), int(re.search(r"kVizFrames = (\d+)", t).group(1)),
            float(re.search(r"kVizAmpMax = ([\d.]+)", t).group(1)))


def viz_default(cols, frames, amp, j, right):
    """the Init preset's column (Classic, Rate 523, Amount 500, Width 600), so the box is right before the first poll"""
    cyc = (0.75 + 2.5 * 0.523) * (j + 0.5) / cols - (0.5 * 0.6 if right else 0.0)
    v = amp * 0.5 * math.sin(2 * math.pi * (cyc - math.floor(cyc)))
    return round(min(frames - 1, max(0, round((0.5 + 0.5 * v) * (frames - 1)))) / (frames - 1), 4)


def build():
    cols, frames, amp = viz_constants()
    names = [factory.menu_name(d) for d in factory.PRESETS]
    P = []
    for k, n, d in KNOBS:
        P.append({"key": k, "name": n, "min": 0.0, "max": 1000.0, "default": float(d), "dynamic_display": True})
    P += [{"key": "mode", "name": "Mode", "options": list(factory.MODES), "default": 0, "refresh_display": True, "qlink_ticks": 4},
          mom("mode_classic", "Mode Classic"), mom("mode_ensemble", "Mode Ensemble"), mom("mode_vibrato", "Mode Vibrato")]
    # the preset selector and browser (chorus_nav.h)
    P += [{"key": "preset", "name": "Preset", "options": names, "default": 0, "refresh_display": True, "qlink_ticks": 2},
          mom("preset_prev", "Preset <"), mom("preset_next", "Preset >"), mom("preset_list", "Preset list"),
          {"key": "browsing", "name": "Browsing", "options": ["Off", "On"], "default": 0, "live": True},
          {"key": "bview", "name": "Browser view", "options": ["Closed"] + ["v%d" % (i + 1) for i in range(len(views()))],
           "default": 0, "live": True},
          {"key": "pre_hl", "name": "Preset shown", "options": ["-"] + [str(i + 1) for i in range(12)], "default": 0,
           "live": True},
          mom("bpage_prev", "Page <"), mom("bpage_next", "Page >")]
    P += [mom("cat%d" % (i + 1), "Category %d" % (i + 1)) for i in range(len(factory.CAT_NAMES))]
    P += [mom("pick%d" % (i + 1), "Pick %d" % (i + 1)) for i in range(12)]
    # the display box: one value per column, left (black) then right (red)
    for side, label, right in (("l", "Wave L", False), ("r", "Wave R", True)):
        for j in range(cols):
            P.append({"key": "v%s%03d" % (side, j + 1), "name": "%s %d" % (label, j + 1), "min": 0.0, "max": 1.0,
                      "default": viz_default(cols, frames, amp, j, right), "live": True, "unit": ""})
    return {"name": "Poli-Chorus", "params": P}


def main():
    path = os.path.join(HERE, "vst", "params.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        json.dump(build(), f, indent=1)
        f.write("\n")
    print("params: %d, browser views: %d" % (len(build()["params"]), len(views())))


if __name__ == "__main__":
    main()
