# Poli-verb 1.0.0

Stereo reverb for the Akai MPC Live III (Gen 2, MPC OS 3.10), uid `PlVb`. **Not yet tested on a device.**

Three tabs (the active one is yellow), Q-Links follow the tab on screen.

- **General**: Mix, Input, Output, Drive, Freeze (toggle), EQ Low / Tilt / High.
- **Space**: Size, Pre-Delay, Width, L / R (wet channel on/off), Early reflections Lvl + Probability (0-100 %),
  Sync (pre-delay and reflections follow the host tempo: 1/32 ... 1/2), and the space picker (arrows or Q-Link):
  Hall, Room, Chamber, Plate, Spring, Cathedral, Arena, Booth, Canyon, Ambience, Cave, Tunnel, Garage, Stairwell.
- **Tail**: Diffusion, Damping, Modulation, Density, Spread, Bass Mult, Attack, Release (inf = natural), Decay
  (0.2 - 30 s), and the room-size letter (S, M, L, XL, XXL).

Every knob shows its real value inside it. Presets: 101 in 10 categories; arrows in the header step through all, a tap
on the name opens the browser (category column, 12 per page, page arrows). They are also in MPC's PRESET menu.

Build: `python3 generate_skin.py`, then `shared/build_a64_native.sh` (the cloud path; no build.sh yet for the WSL + Docker path).
Tests: `test/run_tests.sh` (DSP + UI simulation through the VST2 wrapper), `test/preview.py` (page previews).
PRESETS.md lists every preset.
