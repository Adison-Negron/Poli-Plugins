#!/usr/bin/env python3
"""Writes vst/params.json: every Poli-tea parameter, in VST index order (append only: projects store values by position).

Knobs are positions 0..1000 (the engine maps them to dB / %; "dynamic_display" shows the real value under the knob).
Meters (live, display only) are 0..1 and snap to a strip frame (engine.cpp: 73 input frames, 49 gain-reduction frames).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "presets"))
import factory  # noqa: E402

KNOBS = [  # key, name, default position (TeaDSP.h kDefaultPos); the names are what MPC's Q-Link list shows
    ("input", "Input", 500), ("drive", "Drive", 300), ("low", "Low", 500), ("mid", "Mid", 500), ("high", "High", 500),
    ("amount", "Amount", 500), ("ldrive", "Limiter Drive", 200), ("lthresh", "Limiter Threshold", 889), ("output", "Output", 988),
]


def mom(key, name):
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}


def meter(key, name):
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "live": True}


def views():
    """(category, page) for every browser view, in the order tea_nav.h numbers them."""
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
    # Sidechain source: Internal / External. A two-state switch on a Q-Link needs refresh_display + qlink_ticks (toolchain notes, section 7)
    P.append({"key": "sc", "name": "Sidechain", "options": ["Internal", "External"], "default": 0, "refresh_display": True, "qlink_ticks": 4})
    # the preset selector and browser (tea_nav.h)
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
    # live meters (display only)
    P += [meter("m_pk", "Input Peak"), meter("m_rms", "Input RMS"), meter("m_gl", "GR Low"), meter("m_gm", "GR Mid"),
          meter("m_gh", "GR High"), meter("m_lim", "GR Limiter")]
    return {"name": "Poli-tea", "params": P}


def main():
    path = os.path.join(HERE, "vst", "params.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        json.dump(build(), f, indent=1)
        f.write("\n")
    print("params: %d, browser views: %d" % (len(build()["params"]), len(views())))


if __name__ == "__main__":
    main()
