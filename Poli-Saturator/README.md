# Poli-Saturator

Multiband saturator for the MPC Live III (Gen 2, MPC OS 3.10). UID `PlSt`, v1.0.0. Built from `imgs/Poli-Saturator.jpg`.

| Control | What it does |
|---|---|
| **Type** (< > or Q-Link) | **Tube** (soft, asymmetric: even + odd harmonics), **Tape** (gentler curve plus a little bias and a gentle high-frequency roll-off in the added harmonics), **Clip** (hard knee, odd harmonics), **Fold** (wave-folder: the peaks fold back, bright and metallic) |
| **Saturation** (big knob) | Overall amount: input drive up to +36 dB into the shaper (curved), and the wet/dry balance. 0 % is a clean pass-through (flat within 0.1 dB). The output is level-matched, so more Saturation is more colour, not just louder |
| **Smash** | Compressor in front of the shaper (a fixed fast attack-and-release feed-forward design that keeps the transient): squashes the dynamics so the quiet parts are driven as hard as the loud ones |
| **Crunch** | Shape of the curve: bias (even harmonics) for Tube and Tape, how hard the knee is for Clip, number of folds for Fold |
| **Soften** | Low-pass on the *added* harmonics only (the dry signal keeps its top end), takes the fizz off without dulling the sound |
| **Low / Mid / High** | How hard each band (split at 220 Hz and 2.5 kHz, Linkwitz-Riley) is saturated relative to Saturation: Off, -12 dB .. 0 dB (centre) .. +12 dB |

Processing: Smash, 4x oversampling (aliasing at least 38 dB down), 3-band split, per-band drive and shaper, level match, Soften on the delta,
back to 44.1 kHz, DC blocker, soft ceiling. A Type switch crossfades over 10 ms; every knob is smoothed (no zipper noise).

Q-Links (Live III, 4 per page): 1 Saturation / Smash / Crunch / Soften, 2 Low / Mid / High / Type.

Presets: 65 factory presets in Drums, Vocals, Bass, Keys & Synth, Bus & Master, Creative (`presets/factory.py` is the single source,
`PRESETS.md` lists every value). Tap the name for the categorized browser; arrows step through all.

Build: `./build.sh [drive|none]`, or through `../build-all/build.sh`. Tests: `test/run_tests.sh` (DSP checks, UI simulation through the
real VST2 wrapper under ASan + UBSan, and the same DSP checks at -O3 for the CPU limit). Preview PNGs: `python3 test/preview.py`.
Regenerate the skin and parameters: `python3 generate_skin.py` (then `./build.sh`).

Offline-tested only until it has been loaded on the Live III.
