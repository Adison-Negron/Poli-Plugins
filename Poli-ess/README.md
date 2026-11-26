# Poli-Ess (Poli-Esser) 1.0.5

A split-band de-esser for the MPC Live III (MPC OS 3.10, Gen2 / aarch64). The signal is split at **Freq** with a
Linkwitz-Riley crossover; only the band above it is turned down when it gets louder than **Threshold**. Below Freq
nothing is touched, and with nothing to reduce the two bands sum flat (within 0.01 dB).

## Controls (Q-Links 1-4 = top row, 5-8 = bottom row)

| | Control | Range | What it does |
|---|---|---|---|
| 1 | **Intensity** | 0-100 % | How hard sibilants are turned down. Builds steadily over the whole turn; 50 % sounds like 1.0.4. Above about 65 % it goes past a limiter and pushes S's *below* the threshold (at 100 %, about 1.5x the reduction of 1.0.4's maximum). |
| 2 | **Threshold** | -40..0 dB | Level of the high band where de-essing starts. Set this first: the meter should move on the S's only. |
| 3 | **Freq** | 2-10 kHz | Where the S band starts. Male voices ~3-6 kHz, female ~6-8 kHz, airy close-miked vocals ~8 kHz, hi-hat sizzle 9-10 kHz. |
| 4 | **Diff** | Off / On | Plays only what is being removed, to find Freq and Threshold by ear. Tap it, or turn its Q-Link (4 steps flip it). |
| 5 | **Attack** | 0.1-10 ms | How fast it grabs an S. Fast (< 1 ms) catches the start of each S; slower lets a little "t" bite through. |
| 6 | **Release** | 5-500 ms | How fast it lets go. Short for "s" (30-40 ms); longer for "sh" (60-80 ms) or a smoother, more level result. |
| 7 | **Knee** | 0-12 dB | 0 = hard (starts exactly at Threshold). Higher = starts gently below it, for a more transparent sound. |
| 8 | **Mix** | 0-100 % | How much of the reduction is applied (parallel de-essing). Done on the S band's gain, so lowering it never comb-filters. |

Attack and Release turn on a log scale (the middle is 1 ms and 50 ms); MPC shows the time in ms. The meter on the
right shows the reduction actually applied (after Mix): the brown bar comes down from the top, full = 24 dB.

## Presets (MPC's PRESET menu)

Starting points: the right Threshold depends on how loud the recording is, so after picking one, set Threshold
until the meter moves on the S's only. Every preset turns Diff off.

| Preset | Freq | Thresh | Int. | Attack | Release | Knee | Mix | Based on |
|---|---|---|---|---|---|---|---|---|
| Init | 5000 | -20 | 50 | 0.8 | 35 | 0 | 100 | the defaults |
| Male Ess | 4500 | -24 | 55 | 0.5 | 40 | 3 | 100 | Waves DeEsser "Male Ess" (4500 Hz) |
| Male Shh | 3400 | -24 | 50 | 1.0 | 70 | 4 | 100 | Waves "Male Shh" (3385 Hz); "sh" lasts longer |
| Female Ess | 6800 | -24 | 55 | 0.5 | 40 | 3 | 100 | Waves "Female Ess" (6779 Hz) |
| Female Shh | 5100 | -24 | 50 | 1.0 | 70 | 4 | 100 | Waves "Female Shh" (5077 Hz) |
| Bright Vocal 8k | 8000 | -26 | 55 | 0.5 | 40 | 3 | 100 | FabFilter Pro-DS: a typical "s" around 8-10 kHz |
| Gentle Vocal | 6000 | -18 | 30 | 1.5 | 60 | 8 | 100 | transparent: low strength, wide knee |
| Heavy Sibilance | 5500 | -30 | 85 | 0.2 | 30 | 0 | 100 | harsh, spitty vocals |
| Parallel De-Ess | 6000 | -28 | 75 | 0.3 | 40 | 4 | 50 | parallel mix (Weiss Deess, Softube DeEsser) |
| Spoken Word | 5500 | -22 | 45 | 1.0 | 50 | 6 | 100 | dialogue / podcast |
| Hi-Hat Tamer | 9000 | -26 | 60 | 0.2 | 25 | 2 | 100 | hat sizzle around 10-12 kHz (iZotope); Freq tops out at 10 kHz |
| Cymbals in a Mix | 7000 | -22 | 40 | 0.5 | 80 | 8 | 70 | full mix / drum bus (Pro-DS Allround, Softube "harsh cymbals") |

**Going back to a preset you've edited:** in the on-screen browser, tap it again. (MPC's PRESET menu ignores picking
the preset that is already selected: it does that itself when a project loads, and this keeps your saved edits.)

**On the plugin's own screen (1.0.6, 2026-11-05):** the current preset's name sits right of the
title, between two arrows. **The arrows** step to the previous / next preset (wrapping round). **Tap the name** for a
browser of every preset, drawn in this plugin's style; the current one is highlighted. Tap one to load it (this also
works on the current one: it brings an edited preset back); tap the name again, or the browser's background, to close it.
The same names are in MPC's PRESET menu, and inside PoliPlug the selector works the same on the plugin's own page.
Shared code: `../shared/preset_nav.h` (engine) and `../shared/poli_preset_ui.py` (drawing + layout), copied/used by
`generate_skin.py`.

The table lives in `tools/make_presets.py` (with sources); it writes `vst/presets.json`. Since 1.0.6 the engine loads the
presets itself (`generate_skin.py` writes `src/presets_gen.h` from that file; vst.json `"programs": {"param": "preset"}`),
and the saved state (`PES1;...;preset=N`) keeps both your values and the preset's name.

## Building

`./build.sh` from WSL: skin assets (`generate_skin.py`), the aarch64 build, `test_port.sh`, `test/run_tests.sh`,
the release zip, and the copy to the Plugin Manager stick (`D:\local-plugins`). Install from the manager's Local tab.

- `generate_skin.py`: every position in the skin. Labels use Open Sans (`imgs/fonts`, SIL OFL), fitted to the
  mockup's lettering.
- `test/run_tests.sh`: `dsp_test` (the DSP alone) and `host_sim` (through the real wrapper, driven like MPC).
