#!/usr/bin/env python3
"""Offline picture of the built skin, driven by the real engine (test/libpoliq_probe.so, build_probe.sh):

    python3 test/preview.py out.png [key=value ...] [--noise SECONDS] [--audio file.wav]

Sets the given parameters (as MPC would: positions, dB, option indices), plays pink noise (or a file) through the
engine so the analyser has something to show, then draws the skin (vst/build/skin/...) in that state with
shared/skin_render.py: which when= parts show, the curve/dot/spectrum frames, knob frames and the value text."""
import ctypes
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "shared"))
from skin_render import render  # noqa: E402

SKIN = os.path.join(ROOT, "vst", "build", "skin", "Poli - VST - Poli-q", "Plugin Skins")


class Engine:
    def __init__(self):
        self.lib = ctypes.CDLL(os.path.join(HERE, "libpoliq_probe.so"))
        L = self.lib
        L.pq_create.restype = ctypes.c_void_p
        L.pq_destroy.argtypes = [ctypes.c_void_p]
        L.pq_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        L.pq_get.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
        L.pq_process.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
        L.pq_changed.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.p = L.pq_create()

    def set(self, k, v):
        self.lib.pq_set(self.p, k.encode(), str(v).encode())

    def get(self, k):
        b = ctypes.create_string_buffer(8192)
        n = self.lib.pq_get(self.p, k.encode(), b, 8192)
        return b.value.decode() if n > 0 else None

    def process(self, x):
        """x: int16 array (n, 2); n a multiple of 128"""
        x = np.ascontiguousarray(x, dtype=np.int16)
        y = np.zeros_like(x)
        for i in range(0, len(x), 128):
            self.lib.pq_process(self.p, x[i:i + 128].ctypes.data, y[i:i + 128].ctypes.data, 128)
        return y

    def changed(self):
        out = []
        b = ctypes.create_string_buffer(64)
        while self.lib.pq_changed(self.p, b, 64) > 0:
            out.append(b.value.decode())
        return out


def pink(n, seed=1, level_db=-18):
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n + 4096)
    f = np.fft.rfft(w)
    k = np.arange(len(f))
    k[0] = 1
    f /= np.sqrt(k)
    x = np.fft.irfft(f)[:n]
    x *= 10 ** (level_db / 20) / np.sqrt(np.mean(x * x))
    s = np.clip(x * 32767, -32768, 32767).astype(np.int16)
    return np.stack([s, s], 1)


def state_of(eng, params):
    state, texts = {}, {}
    for i, p in enumerate(params):
        v = eng.get(p["key"])
        if v is None:
            continue
        if p.get("options"):
            o = int(round(float(v)))
            state[i] = o
            state[("v", i)] = o / max(1, len(p["options"]) - 1)
        else:
            try:
                f = float(v)
            except ValueError:   # a text-only value (the Learn result line): its readout shows it
                f = 0.0
            lo, hi = p.get("min", 0), p.get("max", 1)
            state[("v", i)] = (f - lo) / (hi - lo) if hi > lo else 0
        if p.get("display") == "string":
            texts[i] = v
        elif p.get("dynamic_display"):
            t = eng.get(p["key"] + "_display")
            if t:
                texts[i] = t
    return state, texts


def main():
    out = sys.argv[1]
    params = json.load(open(os.path.join(ROOT, "vst", "params.json")))["params"]
    eng = Engine()
    secs = 1.0
    args = sys.argv[2:]
    while args:
        a = args.pop(0)
        if a == "--noise":
            secs = float(args.pop(0))
        elif "=" in a:
            k, v = a.split("=", 1)
            eng.set(k, v)
    if secs > 0:
        n = int(secs * 44100) // 128 * 128
        eng.process(pink(n))
    state, texts = state_of(eng, params)
    render(SKIN, state, texts, out)
    print(out)


if __name__ == "__main__":
    main()
