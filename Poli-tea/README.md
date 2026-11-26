# Poli-tea

Master bus / mixing processor for the MPC Live III (Gen 2, MPC OS 3.10), inspired by the "one-knob" God Particle workflow. UID `PlTe`,
v1.0.0. Built from `imgs/Poli-tea.jpg`.

Signal flow: Input -> Focus EQ -> parallel tape + tube excitation (2x oversampled) -> 3-band compression (fixed LR4 crossovers at
120 Hz / 2.5 kHz, program-dependent ballistics) -> auto-makeup -> band saturation -> stereo matrix (sub-120 Hz mono lock, widened highs)
-> Limiter Drive / Threshold -> soft clipper -> look-ahead true-peak limiter -> Output (the ceiling).

| Section | Controls |
|---|---|
| Header | title, preset arrows and name (tap the name: categorized browser, 57 presets) |
| Front | **Input** -18..+18 dB, **Drive** 0-100 % (excitation), peak + RMS meters with a green sweet-spot zone (-21..-15 dBFS RMS: where the compressors are tuned) |
| Shape | **Low** / **Mid** / **High** focus EQ (weight / body / air; +-6 / +-4 / +-6 dB) |
| Core | **Amount** 0-200 % (macro: saturation, compression ratio/threshold/makeup, widening). 100 % = typical |
| Sidechain | Internal / External key (see note) |
| Gain Reduction | Low / Mid / High band and limiter meters; green zone = the usual 1.5-4.5 dB |
| Limiter | **Drive** 0-100 % (push into the clipper/limiter), **Threshold** -18..0 dB |
| Output | -24..0 dB: the true-peak ceiling and the volume |

Notes
- MPC has no external sidechain bus to plugins, so "External" keys the compressors from the raw track input (before Input, EQ and
  saturation). The DSP already accepts a separate key signal (`keyL/keyR`) for hosts that provide one.
- The ceiling is true-peak aware (4x detector); typical overshoot is 0.1-0.25 dB at the most extreme settings.
- Auto-makeup is tuned for material around -18 dBFS RMS (green zone); much quieter material comes out a little louder with Amount.
- Latency 105 samples (2.4 ms), reported by `TeaDSP::latencySamples()`.

Q-Links (Live III, 4 per page): 1 Amount / Input / Drive / Output, 2 Low / Mid / High / Sidechain, 3 Limiter Drive / Limiter Threshold.

Presets: `presets/factory.py` (single source; `PRESETS.md` lists every value). Build: `./build.sh [drive|none]`, or through
`../build-all/build.sh` (finds this folder by itself). Tests: `test/run_tests.sh`; preview PNGs: `python3 test/preview.py`.
