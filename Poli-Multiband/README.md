# Poli-Multiband (v1.2.2, uid `PlMb`)

A 3-band compressor for the MPC Live III (MPC OS 3.10, aarch64) with a downward stage ("Comp") and an upward stage
("Upwards Comp") in every band, laid out like Ableton's Multiband Dynamics.

## Using it
- **BAND 1 / 2 / 3** (bottom tabs): picks the band the knobs show. The selected tab grows. Every knob, its value text
  and the Q-Links switch to that band's values.
- **Pass** (top right): green = running, red = bypassed (10 ms crossfade, so no click).
- **Split** = the crossover, as in Ableton's Multiband Dynamics. Two frequencies cut the sound into three bands:

  | Band | Range | Its Split knob |
  |---|---|---|
  | 1 (low) | 20 hz up to Split 1 | **Up To**: sets Split 1 (default 500 hz) |
  | 2 (mid) | Split 1 to Split 2 | **Range**: no knob of its own. It shows its range ("500 hz - 4k"), set by Bands 1 and 3 |
  | 3 (high) | Split 2 up to 20k | **From**: sets Split 2 (default 4k) |

  Split 1 always stays below Split 2 (at least a factor 1.25 apart): a knob turned past the other snaps back. Over
  1000 hz the text reads 1k, 1.5k, ...
- **Comp** (turns loud parts down): above **Thresh** (under the white dot), the band is turned down by **Ratio**
  (2:1 = 2 dB over the threshold comes out as 1 dB over). **Knee** softens the corner, **Attack** and **Release** set how
  fast it reacts and lets go, and **Input** / **Output** set the band's level going in and out.
- **Upwards Comp** (brings quiet parts up): below its **Thresh** (under the brown dot), the band is turned up by its
  **Ratio** (at most +24 dB, and never on silence). **Its Thresh can never go above Comp's Thresh**: lowering Comp's
  pushes it down, and raising it past Comp's snaps back. It uses the band's Attack, Release and Knee.
- **Meter** (the band on screen, -60 dB at the bottom to 0 dB at the top):
  - green = the band's level after compression;
  - thin grey line = its level before compression, so the gap is the gain change (green below the line =
    compressed, above it = lifted by the upward stage);
  - white bar = Comp Thresh (the white dot over its knob), brown bar = Upward Thresh (the brown dot over its knob).
- Q-Links: Comp row 1 (Input, Output, Thresh, Attack), Comp row 2 (Release, Knee, Ratio, Split), then Upwards Thresh,
  Upwards Ratio, Band and Pass.

## Presets (1.2.0)
26 factory presets plus Init, grouped by category with a prefix in the name. On the MPC, open the plugin and tap the
**PRESET** name at the top of its screen. MPC's PRESET menu lists them; tap one to load it.

**On the plugin's own screen (1.2.1, 2026-11-05):** the current preset's name sits in a black pill right of
the title, between two arrows. **The arrows** step to the previous / next preset (wrapping round). **Tap the name** for a
browser of every preset, drawn in this plugin's style; the current one is highlighted. Tap one to load it (this also
works on the current one: it brings an edited preset back); tap the name again, or the browser's background, to close it.
The same names are in MPC's PRESET menu, and inside PoliPlug the selector works the same on the plugin's own page.
Shared code: `../shared/preset_nav.h` (engine) and `../shared/poli_preset_ui.py` (drawing + layout), copied/used by
`generate_skin.py`.
The browser groups them under Start, Vocals, Drums, Bass, Mix, Master and FX headings (without the prefix).

| Category | Presets |
|---|---|
| **Vox:** vocals | Pop Lead, Rap Upfront, Trap Hook Dense, R&B Silk, Ballad Gentle, Rock Grit, NY Parallel Vox, Air & Presence, Backing Tuck, Podcast Leveler, De-Ess Male, De-Ess Female |
| **Drm:** drums | NY Smash, Bus Glue, Kick Punch, Snare Crack, Dusty Break |
| **Bass:** | 808 Control, Tight Low End, Growl Lift |
| **Mix: / Mst:** bus and master | Gentle Glue, Hip Hop Master, Clean Polish |
| **Fx:** creative | OTT Full, OTT Lite, Lo-Fi Phone |

- **What happens when you pick one:** it loads all three bands and both splits. The band on screen and Pass stay as
  they are, so you can A/B with Pass. Knob changes after a pick are yours; the menu keeps the preset's name, and a project
  reload brings back both the name and your changes. To go back to the preset as it was, pick another one and then it
  again.
- **Levels:** every preset was tuned on reference audio (vocals peaking around -3 dBFS, drums at -1, finished mixes)
  so it plays at about the same loudness as the dry sound. On a much quieter or louder track, move each band's
  **Thresh** until the green meter goes past the white bar about as much as **PRESETS.md** says.
- **New York compression** (NY Smash, NY Parallel Vox) is done with the upward stage. A parallel blend lifts the quiet
  parts and leaves the peaks alone, which is exactly what upward compression does, so no mix knob is needed.
- Every value, and what each preset measured on its reference, is in **PRESETS.md**. How they were made, and the sources:
  **presets/README.md**. Before/after demos are in **test/demo/** (`00_*_dry.mp3` is the input).

### Changes in 1.2.2 (2026-11-05)
- **Feedback with a live mic on the upward presets: fixed in the compressor, not the presets.** The upward stage lifted
  everything quiet by up to +24 dB, including room noise and monitor bleed between words, so with the mic open and
  monitoring on, the loop gain went over 1 and it howled (less as the ratio went down, as reported). The upward boost now
  fades out between -50 and -62 dBFS, so anything that quiet (room, bleed, hiss) is never lifted, and quiet singing
  above that still is. New checks: room noise at -70 dBFS gets no boost on any preset, and a closed loop (output back
  into the input 10 ms later) grows by at most 6 dB (it was +32 dB before). Presets unchanged.

### Changes in 1.2.0 (2026-11-04)
- The factory presets, as MPC programs: a new `preset` parameter, appended last (so projects from 1.1 still load and open
  on "Init"), with vst.json `"programs": {"param": "preset"}`.
- **Safety ceiling** on the output: a peak limiter at -0.1 dBFS (instant attack, 50 ms release). The plugin's output is
  16-bit, so makeup gain could clip it before; the limiter does nothing to a signal that stays under -0.1 dBFS, and
  Pass (bypass) doesn't go through it.
- Text the engine can't use ("nan", "inf", garbage, huge numbers, a broken saved state) is ignored or clamped instead of
  reaching the DSP.
- The layout is unchanged.

### Changes in 1.1.0 (2026-11-04, after the first device test)
- Knobs were cut flat at the bottom. The builder made each knob's box shorter than its picture once the value text
  moved into the knob, so vst/skin_post.py now makes every knob box as tall as the knob.
- Freq became **Split** (one crossover per outer band, as in Ableton). The mid band's Freq knobs are gone, and its range
  is shown instead.
- The knobs under the two dots are now the two **Thresh** knobs, since the dots are their lines in the meter.
- The Upwards Freq and Upwards Output knobs were removed: Split and the band's Output already do those jobs.
- A one-line description was added under each section title.
- "0 db dB" fixed: the parameters no longer carry a unit, because the value text already has it.
- Parameters were renumbered (1.0.0 was never released): projects saved with 1.0.0 won't restore Poli-Multiband.

## Building (WSL + Docker, same toolchain as Poli-Ess)
```
cd /mnt/c/MPC-PLUGINS/Poli-Multiband && ./build.sh
```
It renders the skin from `imgs/` (`generate_skin.py`), builds the aarch64 `.so`, runs the toolchain's host test plus
`test/run_tests.sh` (DSP with test tones, and a mock MPC host for tabs/Q-Links/meter/Pass), packages
`dist/Poli-Multiband-<version>-mpc-aarch64.zip` and copies it to `D:\local-plugins\`. Then in the Plugin Manager: Local tab,
Install, APPLY. Bump `version` in `vst/vst.json` for every new device build. `params.lock` is committed: never reorder
`vst/params.json`, only append.

`build.sh` first runs `presets/make_presets.py` (presets/factory.json → src/presets.h, the preset list, PRESETS.md).

`dist/` holds a 1.2.0 zip built offline (2026-11-04). The skin was rendered by the same toolchain, and the `.so` was
cross-compiled for aarch64 (zig, glibc 2.36 target; it exports only `VSTPluginMain`). `test/run_tests.sh` also passed as
ARM64 binaries under qemu-aarch64.

## Files
- `src/MultibandDSP.*`: crossovers (Linkwitz-Riley 4th order; the low band also gets the allpass at the upper crossover, so
  the bands sum flat: within 0.01 dB, 40 hz to 12k), per-band envelope, soft-knee static curve for both stages.
- `src/engine.cpp`: the parameters. The knobs are bound to "proxy" parameters for the band on screen. All three bands
  live in the saved state (chunk), and switching bands reports the new values to MPC.
- `generate_skin.py`: every coordinate and every skin image. `vst/skin_post.py`: 111-frame knob strip, and keeps the
  value text's case ("500 hz", not "500 HZ").
- `test/compose_preview.py <band> <pass> out.png`: a picture of the page in a given state.
- `presets/`: `spec.py` (what each preset is for), `tune.py` + `preset_tool.cpp` (tune thresholds and makeup on
  reference audio), `factory.json` (the values), `make_presets.py` (→ the engine's table).

## Not yet verified on the device
Everything above was checked offline only. On the Live III, check:
- the tabs switch and the knobs/value text follow the band (tap and Band Q-Link);
- the value text sits inside the knobs, in lower case;
- the four meter layers stack in order (green under the lines) and move;
- Pass changes colour;
- CPU use;
- save and reload a project (all three bands come back);
- the PRESET menu lists the 27 presets, picking one moves the knobs of the band on screen, and after a project reload
  the menu shows the preset's name.
