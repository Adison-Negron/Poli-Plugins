# Shhhhh (Poli-Shhhh) 1.0.2: a learn-the-noise de-noiser for the MPC Live III

A spectral de-noiser in the spirit of iZotope RX Spectral / Voice De-noise, built for MPC OS 3.10 (Gen2, aarch64).

## Using it
1. Insert it on the track and play something with the noise in it: a few seconds of noise alone is best, but
   it also works while the voice is talking over the noise.
2. Tap **Learn**. It turns green and fills its bar for about 3 s, then switches off by itself. Tap it again early to stop:
   it uses what it heard (at least 0.2 s).
3. The six **nodes** move onto the learned noise floor. A node jumps sideways inside its own segment onto a
   noise peak (a hum, a whine) when there is one. Drag a node up to take more out at that frequency, or down to protect it.
4. **Threshold** moves all six nodes up or down together. Everything under the nodes is treated as noise.
   **Reduction** sets how many dB the noise is turned down, 0 to 100 (default 12). Both faders show their value
   underneath. Threshold is 0 dB in the middle of its fader.
   **Output** (the square knob between Pass and Diff) sets the volume, -24 to +24 dB. It doesn't apply in Pass.
5. **Pass** (green) = the input untouched. **Diff** (green) = hear only what is being removed (use it to check
   that no voice is being taken out). While Pass is on, Diff does nothing and is greyed out.

6. **Presets** (1.0.3): the box right of the title shows the current preset between two arrows. Presets set Threshold,
   Reduction and Output as starting points for a Learn (they never touch the learned noise or your node moves) and turn
   Pass and Diff off. Learn first, then pick one.

| Preset | Threshold | Reduction | Output | For |
|---|---|---|---|---|
| Init | 0 | 12 | 0 | RX De-noise's own default |
| Light Touch | -3 | 6 | 0 | barely there: keeps the room |
| Gentle Vocal | -1 | 9 | 0 | singing over light noise |
| Music Bed | -2 | 7 | 0 | music: least reduction, least musical noise |
| Field Recording | +1 | 10 | 0 | ambience you want to keep some of |
| Dialogue Clean | +2 | 15 | 0 | speech in a noisy room |
| Podcast Voice | +3 | 18 | +0.5 | voice-over, home studio |
| Hiss Remover | +2 | 14 | 0 | tape / preamp hiss |
| Old Tape | +3 | 16 | 0 | older recordings |
| Room Tone Removal | +4 | 22 | +0.5 | aircon, fans, room rumble |
| Hum & Buzz | +6 | 24 | 0 | mains hum: the nodes snap onto its peaks |
| Heavy Noise | +6 | 30 | +1 | loud steady noise; check with Diff |

They are starting points, not measurements: set them by ear (and Diff) on your material. Edit the table in
`generate_skin.py` (`PRESETS`).

**On the plugin's own screen (1.0.3, 2026-11-05):** the current preset's name sits right of the
title, between two arrows, in a
box drawn like Learn. **The arrows** step to the previous / next preset (wrapping round). **Tap the name** for a
browser of every preset, drawn in this plugin's style; the current one is highlighted. Tap one to load it (this also
works on the current one: it brings an edited preset back); tap the name again, or the browser's background, to close it.
The same names are in MPC's PRESET menu, and inside PoliPlug the selector works the same on the plugin's own page.
Shared code: `../shared/preset_nav.h` (engine) and `../shared/poli_preset_ui.py` (drawing + layout), copied/used by
`generate_skin.py`.

The grid shows the input spectrum live (32 columns, 20 Hz - 20 kHz, 0 to -120 dB, 20 dB per row). Each node shows
the threshold at its frequency.

## How it works
- A 512-point STFT with low-delay asymmetric windows (Mauler & Martin). It uses 86 Hz bins but delays the sound
  by only **128 samples (2.9 ms)**. MPC does no plugin delay compensation, so a plain 512 window would have added 8.7 ms.
- Both channels go through one complex FFT, with one stereo-linked gain per bin (the stereo image stays stable).
- The gain (since 1.0.2) uses a decision-directed a-priori SNR with a second refining step (Plapous' two-step
  SNR, which removes the one-frame lag that blurs consonants) and the MMSE log-spectral amplitude rule
  (Ephraim-Malah). It is smoothed over about 1/6 octave, wider at high frequencies, has a 40 ms release, and has a
  psychoacoustic floor, so noise isn't pushed below what the voice masks anyway (like RX's Masking). That 0..1 noise/voice decision scales the Reduction in dB, so the top of the
  fader really removes the noise. A plain floor stalled at about -30 dB.
- Learn keeps the quietest quarter of 32 sub-windows of 93 ms per bin (minimum statistics), so the voice doesn't
  get into the noise profile.
- The settings glide over 10 ms, and Pass and Diff crossfade over 10 ms with an S-curve. Pass is delay-matched, so
  A/B comparisons don't jump in time.

## Building
- WSL + Docker: `./build.sh` (skin, aarch64 build, host test, own tests, release zip, copy to `D:\local-plugins`).
- Without Docker: `MPC_VST=/path/to/mpc-vst-plugins ./build_a64_native.sh` (needs `g++-aarch64-linux-gnu` and Playwright).
- `-U__FAST_MATH__` in the cflags: with -ffast-math, glibc 2.39 headers turn sinf/logf loops into libmvec calls, so
  the plugin needed `libmvec.so.1`. That library is not verified to exist on the device, so the flag avoids it.
- Install: put the zip from `dist/` in `local-plugins/` on the Plugin Manager stick, then go to the Local tab and
  tap Install, then APPLY.

## Tests (offline, 2026-11-04)
`test/run_tests.sh` runs `dsp_test` (51 checks, ASan + UBSan + LeakSanitizer), `ui_sim` (26 checks, the real
vst2_wrap.c with a mock MPC) and the same DSP checks at release flags. `tools/test_port.sh` PASSED. Both tests also
pass on ARM64 under qemu. Previews are in `test/preview/` (`compose_preview.py` draws the skin from engine values).

## Evaluating changes
`python3 test/eval/run_eval.py <librosa data/audio> <eval binary> [config ...]` (needs pesq, pystoi and MS-SNSD in
`$SNSD`). Build the binary with `g++ -O2 -Isrc test/eval/eval.cpp src/ShhDSP.cpp -o eval`.

## Files
`src/ShhDSP.*` is the DSP, `src/engine.cpp` the MPC engine contract and `generate_skin.py` makes all skin art plus
`vst/layout.conf`. `vst/skin_post.py` checks the built filmstrips.

## Changes
- 1.0.3 (2026-11-05): 12 presets, in MPC's PRESET menu and in a selector on the screen (name between arrows, tap for a
  browser). The saved state keeps the preset's name (`;pre=N`); 1.0.2 projects load as Init.
- 1.0.2: better gain stage, tested on 180 noisy speech and music conditions (`test/eval/`, results in
  `src/ShhDSP.h`). At the same Reduction setting, the voice keeps about 3 dB more of itself (less distortion) and
  intelligibility (STOI) rises. PESQ is +0.1 at Reduction 30 and +0.15 at 60, and there are fewer musical-noise
  chirps. The pauses go just as deep, and a little more noise is left under the voice, where it is masked.
  `test/demo/` has a before/after (air conditioner at 5 dB SNR, Reduction 30). CPU is about 0.6 % of one x86 core.
- 1.0.1:
  - Value text under both faders ("+0.0 dB", "12 dB").
  - Reduction now goes 0-100 dB and is applied in dB.
  - New Output knob (square, value in the middle, white guide line), on Q-Link 11.
  - Lighter grid: fill and lines at about half strength; the frame stays black.
  - Cleaner node circles (the mockup's grid lines no longer show at their edges).
- 1.0.0: first build.
