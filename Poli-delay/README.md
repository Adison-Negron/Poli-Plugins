# Poli-delay

Stereo delay for the MPC Live III (Gen 2, MPC OS 3.10). UID `PlDl`, v1.0.0. Built from `imgs/BG.jpg` + `imgs/GUI.jpg`.

| Section | Controls |
|---|---|
| Header | title, preset arrows, preset name (tap it: categorized browser, 75 presets) |
| Time & Offset | **Time** 5 ms-2 s (log) or, with **Sync** on, 1/32 .. 1/1 incl. triplets/dotted from the host tempo; **Offset** toggle reveals the **Offset** knob (see below) |
| Feedback | Feedback 0-100 % (a soft ceiling keeps the loop from running away) |
| High Pass / Low Pass | 12 dB/oct filters **inside** the feedback loop: 20 Hz-2 kHz / 500 Hz-20 kHz (Off at the ends) |
| Modulation | Rate 0.05-10 Hz, Depth 0-100 %: chorus at low depth, pitch warble at high depth (both sides 90 degrees apart) |
| Saturation | Drive (tape/tube tanh with a little bias = even harmonics, in the loop), Tone (tilt around 800 Hz, +-6 dB) |
| Width | Spread 0-200 % (mid/side on the wet signal) |
| Ping-Pong | toggle: echoes alternate left / right |
| Master | Mix (equal power) |

**Offset**: with the toggle on, the Sync / Offset pills close up and an Offset knob appears under them, beside Time. -100 .. +100 %:
positive = the right side is later by that % of Time (+50 % = right is 1.5x left, e.g. 1/8 against dotted 1/8), negative = the left
side is later. With the toggle off the knob is ignored (it keeps its value).

Q-Links (Live III, 4 per page): 1 Time / Offset / Feedback / Mix, 2 High Pass / Low Pass / Drive / Tone, 3 Rate / Depth / Spread /
Ping-Pong, 4 Sync / Offset on-off.

Presets: `presets/factory.py` (single source; `PRESETS.md` lists every value). Build: `./build.sh [drive|none]`, or through
`../build-all/build.sh` (finds this folder by itself). Tests: `test/run_tests.sh`; preview PNGs: `python3 test/preview.py`.
