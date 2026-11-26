#!/usr/bin/env python3
"""Writes vst/params.json: every Poli-delay parameter, in VST index order (append only: projects store values by position).

Knobs are positions 0..1000 (the engine maps them to ms / Hz / %; "dynamic_display" shows the real value inside the knob).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "presets"))
import factory  # noqa: E402

KNOBS = [  # key, name, default position (DelayDSP.h kDefaultPos)
    ("time", "Time", 683), ("offset_amt", "Offset", 500), ("feedback", "Feedback", 350), ("hp", "High Pass", 200),
    ("lp", "Low Pass", 850), ("mix", "Mix", 300), ("rate", "Mod Rate", 450), ("depth", "Mod Depth", 0), ("drive", "Drive", 0),
    ("tone", "Tone", 500), ("spread", "Spread", 500),
]


def mom(key, name):
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}


def toggle(key, name, default=0):
    return {"key": key, "name": name, "options": ["Off", "On"], "default": default, "refresh_display": True, "qlink_ticks": 4}


def views():
    """(category, page) for every browser view, in the order delay_nav.h numbers them."""
    out = []
    for c, name in enumerate(factory.CAT_NAMES):
        n = len(factory.PRESETS) if c == 0 else sum(1 for d in factory.PRESETS if d["cat"] == name)
        for p in range(max(1, (n + 11) // 12)):
            out.append((c, p))
    return out


def build():
    names = [factory.menu_name(d) for d in factory.PRESETS]
    P = []
    for k, n, d in KNOBS:
        P.append({"key": k, "name": n, "min": 0.0, "max": 1000.0, "default": float(d), "dynamic_display": True})
    P += [toggle("sync", "Sync"), toggle("offset", "Offset On"), toggle("pingpong", "Ping-Pong")]
    # the preset selector and browser (delay_nav.h)
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
    return {"name": "Poli-delay", "params": P}


def main():
    path = os.path.join(HERE, "vst", "params.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        json.dump(build(), f, indent=1)
        f.write("\n")
    print("params: %d, browser views: %d" % (len(build()["params"]), len(views())))


if __name__ == "__main__":
    main()
