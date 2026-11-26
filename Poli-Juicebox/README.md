# Poli-Juicebox

Low-latency voice shaper for the MPC Live III (Gen 2, MPC OS 3.10): natural pitch and formant shifting (MtF / FtM and character
voices) with a polish chain. Stereo in, mono-summed voice out (identical on both channels). Latency is fixed at 768 samples (17.4 ms).

## Controls (28 knobs + a switch; the right side has two pages, Shape and Tracking, switched by the tabs in the header)
| Section | Controls |
|---|---|
| Shift | Pitch -12..+12 st, Formant -12..+12 st, F-Track 0-100 %, **P&F Link** Off / Variable |
| Spectral Tamer | Threshold -60..0 dB (5-10 kHz sibilance), Low end 80-350 Hz (Off at the left), Attack 0.5-20 ms, Release 10-200 ms |
| Harmonic Shape | Odd drive, Even drive, Color 200 Hz-4 kHz (the band the harmonics grow from) |
| Natural Formant Filter | F1 200 Hz-1 kHz, F2 1-3 kHz, F3 2.5-4.5 kHz, each with Q 0.7-14 (Off at the left) |
| Smoothen | F-Smooth (keeps plosives and attacks), Air (8 kHz shelf + breath), Natural (voiced jitter / shimmer), Mix (equal-power dry/wet) |

| Tracking page | Floor / Ceiling (the pitch range the tracker searches: set them just around your voice), Glide (smooths the tracked pitch), Oct guard (an octave jump must persist before it is followed), Rasp (how little periodicity still counts as voiced), Hold (keeps pitch-shifting through short dips: fry, a dropped pulse), Morph (how softly the shift fades in / out at a voiced / unvoiced change), Overlap (grain width) |

P&F Link = Variable: the formant moves with the pitch, formant = Formant knob + F-Track x Pitch. With Link Off, F-Track does nothing.
At Pitch = Formant = 0 the shifter is a perfect bypass (the polish chain still works). 36 factory presets (the pitch stays put, or goes an octave: they change the tone, the resonance and the shape of the voice; the shaping presets are linked) in 5 categories: see PRESETS.md.

## Q-Links
1 Pitch / Formant / F-Track / P&F Link, 2 Mix / Rasp / Hold / Glide, 3 Threshold / Low end / Odd / Even, 4 F-Smooth / Air / Natural / Color.

## Build (WSL, Docker)
`cd /mnt/c/MPC-PLUGINS/Poli-Juicebox && ./build.sh` (or `build-all`): presets, params, skin, aarch64 build, host tests, release zip, copy to the
Plugin Manager stick (`local-plugins`). Offline tests only: `test/run_tests.sh`. Layout preview: `python3 test/preview.py`.

## Layout of the source
`src/JuiceDSP.*` DSP (TD-PSOLA shifter + YIN tracker, tamer, harmonic generator, formant filter, air) - `src/engine.cpp` MPC engine,
`src/juice_nav.h` preset selector / browser - `presets/factory.py` the presets - `generate_skin.py` skin + `vst/layout.conf` -
`test/` DSP tests, synthetic-voice evaluation, mock MPC host.

## Known limits
Voices below ~80-110 Hz (or shifted below that) get grains shorter than two periods and sound pulsy: the latency budget is the cap.
Tested on synthetic voices only; tune the presets by ear on your own voice.

## Help window (1.2.0)
The **?** button after the preset selector opens a two-page help window over the plugin (page 1 Shift & Tracking, page 2 Polish, with
Presets and Q-Link tips); the arrows flip pages, a tap on the **?** or opening the preset browser closes it. Engine params `help_btn`,
`help_next` (momentary) and `hview` (live) were appended after the 1.1.0 parameters, so existing indices are unchanged. Text lives in
`HELP_PAGES` in `generate_skin.py`.

## 1.3.0
- **Tracker:** on fast notes, runs and trills the pitch tracker could fall to a sub-octave or a strong harmonic, which put grains every 2-4 periods (or
  at the harmonic) and made the shifted voice buzz robotically. Fixes: a short-lag dip is only traded for a multiple when the multiple is much deeper;
  a sub-octave lock is recovered at once when the voice is as periodic at 1/2, 1/3, 1/4 of the lag; the octave guard never accepts an octave-size jump
  on "clear evidence" any more, while smaller note changes follow after 1-4 frames; the period before a short gap is remembered (no octave lock at an onset).
  `test/run_eval.py` (fast runs / trills / leaps, known f0): tracker octave errors 41 % -> 14 %, harmonic lock at the target pitch 0.7 -> 0.9.
- **Latency:** the look-ahead is now a mode. **Balanced 11.6 ms** (512 samples) is the default (was 17.4 ms), **Fast 8.7 ms** (384) and **Smooth 17.4 ms** (768)
  are on the Tracking page footer. Pitch accuracy on the synthetic voices is as good or better at 512; the price of a shorter look-ahead is shorter grains
  for very deep voices with large formant moves (Smooth keeps the most room). The mode is saved with the project (not with presets); switching it
  while playing leaves a very short gap.
- **Presets:** 36 (was 52). The pitch stays put (0) or goes an octave (+/-12); the presets change the tone: Formant, the three resonant peaks, harmonics, air.
  P&F Link is on with an F-Track, so moving Pitch afterwards takes the formants with it. Near-duplicates removed.
- The straw's tip is a clean slanted cut now.

## 1.4.0

- **Learn button** (header, left of the ?): sing or speak into the plugin; it listens until it has heard about 6 s of voice (or ~21 s have passed), switches itself off and sets
  Floor, Ceiling, Rasp, Hold, Glide, Guard, Morph, Overlap and the de-ess Threshold to fit that voice. Pitch, Formant and Mix are never touched. Switching it Off early uses
  what it heard if that was at least ~1 s of voice, otherwise nothing changes. The bar under the button is the progress. While it listens the tracker runs wide open (60-800 Hz).
  The values are absolute (from the statistics of what it heard), so learning twice does not drift.
  - Floor/Ceiling: 0.8 x the 5th and 1.5 x the 95th percentile of the confident f0s. Rasp: median periodicity (clean ~20 %, raspy ~78 %). Hold: gaps inside voiced stretches.
    Glide: how fast the pitch moves (runs, trills -> quicker tracker). Guard: octave jumps seen. Threshold: between the sibilance-band level while voiced and while unvoiced.
- Header re-laid out (preset selector narrower) to make room. The help window has a Learn cell.
- `test/real_eval.py` and `juice_cli learn=1` for checking real recordings; `test/real/` holds three vocals (not shipped).

## 1.5.0

- **Q-Links follow the page.** Q-Link page 1 is always the Smoothen panel (F-Smooth / Air / Natural / Mix). Pages 2-4 follow the tab on show: Shape = Pitch / Formant / F-Track / P&F Link; Tone = Threshold / Low end / Attack / Release, then Odd / Even / Color; Filter = the three Freqs, then the three Qs; Tracking = Floor / Ceiling / Glide / Oct guard, then Rasp / Hold / Morph / Overlap. The Q-Link names and values on the MPC follow too (ql01..ql16 are proxies; the table is `QLEFT` / `QPAGES` in make_params.py, which also writes src/qlinks_gen.h for the engine).
- **Q-Link indicator.** Every knob that has a Q-Link carries a numbered dot (1-4 = which Q-Link, colour = which Q-Link page); each section heading has a "Q-Link N" pill.
- **Four tabs: Shape, Tone, Filter, Tracking.** Shape is now just Pitch / Formant / F-Track and P&F Link, big; Tone holds Spectral Tamer and Harmonic Shape; Filter the Natural Formant Filter (Freq row, Q row); Tracking is unchanged (+ latency pills).
- Header: the ? sits after the title and straw, then the preset selector, Learn and the four tabs.
- params.lock: tab3, tab4 and ql01..ql16 are appended (86 params); a project saved with 1.4.0 loads as before.
