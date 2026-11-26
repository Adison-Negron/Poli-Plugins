# Poli-Chorus

Chorus-Ensemble for the MPC Live III (Gen 2, MPC OS 3.10), modelled on the Ableton Chorus-Ensemble layout and classic hardware choruses.
Built from the Poli-Saturator template (same preset browser, skin pipeline and tests).

**Status: tested offline only** (DSP tests, mock MPC host through the real wrapper, layout checks). Not yet loaded on a device.

## Modes (buttons under the display, the lit one is red)
- **Classic** - 2 modulated voices (7.5 / 11.5 ms), the familiar stereo chorus.
- **Ensemble** - 3 voices (9.5 / 11 / 12.5 ms), slow sine plus faster ripple: thick string-machine movement.
- **Vibrato** - 1 voice at 6 ms, wet only in effect: pitch wobble, no comb.

## Knobs (value shown under each)
Rate 0.05-10 Hz | Amount (depth) | Feedback | Width | LP Filter 800 Hz-20 kHz (Off at max) | Warmth (soft saturation) | Output -24..+12 dB | Mix (equal power).
The display draws the left (black) and right (red) LFO from the real Mode, Rate, Amount and Width values. At Width 0 the red wave hides under the black one.

## Q-Links
Page 1: Rate, Amount, Feedback, Mix. Page 2: Width, LP Filter, Warmth, Output. Page 3: Mode.

## Presets
51 factory presets in 6 categories (Classic, Ensemble, Vibrato, Guitar & Keys, Vocals & Pads, Creative); see PRESETS.md. Edit `presets/factory.py`, then rebuild.

## Build (WSL)
    cd /mnt/c/MPC-PLUGINS/Poli-Chorus && ./build.sh [drive]
or `build-all` (picks it up automatically). Runs generate_skin.py, the aarch64 build, test_port.sh, `test/check_layout.py`, `test/run_tests.sh`, packages the zip and copies it to the Plugin Manager stick. Install: Plugin Manager, Local tab, APPLY.
