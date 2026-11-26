#!/usr/bin/env python3
"""Writes vst/params.json: every Poli-verb parameter, in VST index order (append only: projects store values by position).

Knobs are positions 0..1000 (the engine maps them to dB / ms / s / %; "dynamic_display" shows the real value). The
engine-owned "view" parameters exist because the toolchain's `picture` can't carry when=: the engine says "hidden".
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "presets"))
import factory  # noqa: E402

KNOBS = [  # key, name, default position
    ("mix", "Mix", 300), ("input", "Input", 500), ("output", "Output", 500), ("drive", "Drive", 0), ("low", "Low", 500),
    ("tilt", "Tilt", 500), ("high", "High", 500), ("size", "Size", 500), ("predelay", "Pre-Delay", 120),
    ("width", "Width", 500), ("er_level", "Lvl", 600), ("er_prob", "Probability", 1000), ("diffusion", "Diffusion", 800),
    ("damping", "Damping", 500), ("modulation", "Modulation", 300), ("density", "Density", 600), ("spread", "Spread", 700),
    ("bassmult", "Bass Mult", 500), ("attack", "Attack", 0), ("release", "Release", 1000), ("decay", "Decay", 500),
]
ROOM = ["Hidden", "S", "M", "L", "XL", "XXL"]


def mom(key, name):
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}


def toggle(key, name, default=0):
    return {"key": key, "name": name, "options": ["Off", "On"], "default": default, "refresh_display": True, "qlink_ticks": 4}


def views():
    """(category, page) for every browser view, in the order verb_nav.h numbers them."""
    out = []
    cats = factory.CAT_NAMES
    for c, name in enumerate(cats):
        n = len(factory.PRESETS) if c == 0 else sum(1 for d in factory.PRESETS if d["cat"] == name)
        for p in range(max(1, (n + 11) // 12)):
            out.append((c, p))
    return out


def build():
    names = [factory.menu_name(d) for d in factory.PRESETS]
    P = [{"key": "page", "name": "Page", "options": ["General", "Space", "Tail"], "default": 0, "refresh_display": True,
          "qlink_ticks": 4}]
    for k, n, d in KNOBS:
        P.append({"key": k, "name": n, "min": 0.0, "max": 1000.0, "default": float(d), "dynamic_display": True})
    P += [toggle("freeze", "Freeze"), toggle("sync", "Sync"), toggle("wet_l", "L", 1), toggle("wet_r", "R", 1),
          {"key": "type", "name": "Space", "options": factory.TYPES, "default": 0, "refresh_display": True, "qlink_ticks": 2},
          mom("type_prev", "Space <"), mom("type_next", "Space >"), mom("tab1", "General"), mom("tab2", "Space tab"),
          mom("tab3", "Tail"),
          {"key": "type_view", "name": "Space picture", "options": ["General"] + ["Tail " + r for r in ROOM[1:]] + factory.TYPES, "default": 0, "live": True},
          {"key": "room_view", "name": "Room size", "options": ROOM, "default": 0, "live": True}]
    for q in range(1, 10):
        P.append({"key": "q%d" % q, "name": "Q-Link %d" % q, "min": 0.0, "max": 1000.0, "default": 0.0,
                  "dynamic_name": True, "dynamic_display": True})
    # the preset selector and browser (verb_nav.h)
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
    # appended after 1.0.0 (params.lock is append-only): the Tail page's room-size step and its Q-Link slot
    P += [mom("room_step", "Room size step"), {"key": "q10", "name": "Q-Link 10", "min": 0.0, "max": 1000.0, "default": 0.0,
                                                 "dynamic_name": True, "dynamic_display": True}]
    return {"name": "Poli-verb", "params": P}


def main():
    path = os.path.join(HERE, "vst", "params.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        json.dump(build(), f, indent=1)
        f.write("\n")
    print("params: %d, browser views: %d" % (len(build()["params"]), len(views())))


if __name__ == "__main__":
    main()
