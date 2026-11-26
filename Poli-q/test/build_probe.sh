#!/bin/bash
# builds test/libpoliq_probe.so (x86, for preview.py and quick checks)
set -e
cd "$(dirname "$0")/.."
MV=${MPC_VST:-../mpc-vst-plugins}
g++ -O2 -g -shared -fPIC -std=gnu++11 -Isrc -I"$MV/wrapper" src/PoliQ.cpp src/Learn.cpp src/engine.cpp test/probe.cpp -o test/libpoliq_probe.so
