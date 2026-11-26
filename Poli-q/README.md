# Poli-q

An 8-band dynamic equalizer for the Akai MPC Live III (Gen 2, MPC OS 3.10), built from the Poli-q Canva mockup
(`imgs/Poli-q.jpg`). Insert effect, stereo, no added latency. 1.1.0 (2026-11-05) adds per-band dynamics, **Learn** and
150 Hz low cuts on the vocal presets (see "Changes in 1.1.0").

## The screen

- **Title row:** the preset selector beside "Poli-q" (arrows step through the presets, tap the name for the
  browser), **Visualizer** (the spectrum behind the grid on/off), **Learn** (below), the **Q-Link bank** switch (1-4 / 5-8) and **Pass**
  (green = the EQ is working, red = bypassed: the dry signal comes out untouched).
- **The grid:** 20 Hz to 20 kHz on a true log axis (the Hz numbers along the bottom), +-18 dB (3 dB per row, the dB
  numbers on the left).
  - The black line is the EQ curve: the sum of every band that is on, drawn from the filters' exact response, with a
    soft fill between it and the 0 dB line.
  - Each band has a dot in its section's colour at its frequency and gain. A band that is off shows a hollow ring on
    the 0 dB line.
  - Behind the curve, the visualizer shows the output's spectrum (40 columns, +4.5 dB/oct tilt so a typical mix reads
    flat). It only runs while the plugin's screen is shown (see below).
- **The 8 sections** (as the mockup: Low Cut, Low Shelf, 4 Bells, High Shelf, High Cut):
  - **Freq / Gain / Q** knobs, with the value inside each knob (Freq in Hz: "200", "1.5k"; Gain in dB: "+3.0";
    "--" where the type has no gain or Q).
  - The **colour circle** switches the band on and off (filled = on, hollow = off). The mockup's "Color" label reads
    "On".
  - The **type name** (tap it): a browser with every type and a little picture of its shape: Bell, Notch, Band Pass,
    Tilt, Low Shelf, High Shelf, Low Cut 6/12/24/48, High Cut 6/12/24/48 (dB per octave). Tap a type to pick it; tap
    the background or the name again to close.
- **The bar under four sections** shows which four the Q-Links drive.

- **Dynamics panel (right):** pick a band with the arrows (the pill shows its number in its colour), then
  **Threshold**, **Range**, **Attack**, **Release** for that band. Range 0 = Off (a plain static band); below 0 the band
  cuts up to that much more while its own frequency range is louder than Threshold (de-essing, taming boom or harshness
  only when it happens), above 0 it boosts. The full Range is reached 10 dB over Threshold. **Gain now** shows what the
  band is doing at this moment, and the band's dot on the grid moves with it. Works on Bell, Shelves, Band Pass and Tilt
  (types with a gain).

## Learn

Inspired by Auto-Tune Vocal EQ's Learn, and since 1.1.2 it works on **voices, drums, instruments (leads, keys, guitars,
pads, chords) and basses**. Pick the preset closest to what you want (a Vox preset for a voice, Drm for drums...), tap
**Learn**, and play, sing or talk normally for about 6 seconds (it waits through silences, up to about 20 s; sparse drum
hits take a little longer). The button shows its progress and switches itself off when it has heard enough. It decides
what it heard and names it under the button, then moves the preset onto it:

| Heard | How it knows | Shown as |
|---|---|---|
| **Voice** | mostly pitched, with s / t / sh sounds between the vowels | "Low male / Alto/Tenor / Soprano  110-240 Hz" |
| **Drums** | mostly unpitched, and a new loud sound starts often | "Drums  low end 45 Hz  bright" |
| **Instrument** | pitched (a lead, a bass line) or a chord / pad | "Instrument  220-660 Hz  balanced", or "low end 76 Hz" for chords |
| **Bass** | low pitch, or mostly low end (and a Bass preset loaded) | "Bass  low end 40 Hz" |

The preset's family (Vox / Drm / Bass / Inst) breaks ties: a Vox preset treats pitched sound as a voice, and only clearly
unpitched sound as drums.
- **Low cut:** just under the lowest note (voice 70-200 Hz, instruments and basses under their lowest pitch, drums under
  the lowest content, e.g. a kick's); never more than a factor of two away from the preset's own cut;
- **cutting bells** (boom, mud, box, ring, harsh, fizz) move onto the source's own resonances, deeper for stronger ones;
  the regions follow the source (a snare's box is not a voice's);
- **boosts:** body at the voice's or lead's fundamentals (a snare's shell or a pad's body: its strongest bump), sub / punch
  at a kick's or bass's strongest low peak and scaled by how much low end there is, presence into the source's own dip;
- **Air shelf** by how bright it already is; every dynamic band's **Threshold** from the level of its own region
  (voices: the de-esser's Threshold sits between the vowels and the s's).

Learn always starts from the preset as saved, so learning twice doesn't drift. Picking the preset again in the
browser undoes it. Nothing is analysed unless Learn is on (about +0.3 % of a core while it listens, 0 otherwise).
Under about 0.7 s of sound (one hit) it says "Not enough sound" and changes nothing.

## Q-Links

The 16 Q-Links are 4 columns, one per section, top to bottom: **Freq, Gain, Q, Type**. The 1-4 / 5-8 switch moves
them between sections 1-4 and 5-8; MPC's Q-Link names follow ("1 Freq" .. "8 Type").

## Types

| Type | Gain knob | Q knob |
|---|---|---|
| Bell | boost / cut at Freq | width |
| Low Shelf, High Shelf | shelf level | the shelf's slope (0.71 = no overshoot) |
| Low Cut / High Cut 12, 24, 48 | - | resonance at Freq (0.71 = flat Butterworth) |
| Low Cut / High Cut 6 | - | - |
| Notch | - | width |
| Band Pass | level of what passes | width |
| Tilt | highs +gain, lows -gain, pivot at Freq | slope |

## Presets

43 presets (`PRESETS.md` lists every value): Init; Vocals (Pop, Rap, R&B, Podcast, Air, De-Mud, Backing); Drums
(Kick Punch, Kick Sub, Snare Crack, Claps, Hi-Hats, Overheads, Drum Bus); Bass (808 Clean, 808 Knock, Bass Guitar,
Synth Bass); Instruments (Acoustic / Electric Guitar, Piano, Rhodes & Keys, Pads & Strings, Synth Lead); Tone (Airy,
Darker, Warm, Bright, Smile Curve, Clarity, Mid Focus); Mix (Master Polish, Low End Tidy, Sample Clean, Loop Sit Back);
FX (Telephone, AM Radio, Lo-Fi, Walkie Talkie, Megaphone, Underwater, Cassette). They are also in MPC's PRESET menu.
Picking the current preset again in the browser restores it after tweaks.

## The visualizer and the screen

The visualizer only works while the screen is up, to save CPU: MPC keeps asking for the knobs' value text while the
plugin's screen is shown, and stops when it is closed. After about 2.5 s without that, the visualizer pauses; it starts
again as soon as the screen comes back (or a Q-Link turns). It only pauses once it has seen MPC's polling running for
3 seconds, so on a host that doesn't poll it just keeps running instead of freezing. The Visualizer switch turns it off
completely: the columns empty out and are hidden, and they come back the moment it is switched on.

## Changes in 1.1.2 (2026-11-09, offline)
- **Learn works on drums, instruments and basses**, not only voices (above). It used to need ~1.6 s of sung pitch, so drums and
  synths ended with "Not enough voice".
- **Dynamics band pill:** the "Band n" pill is now one filmstrip drawn like the type names, not 8 swapped pictures; swapping
  them left a black bar over (part of) the pill after a band change.
- Tests: dsp_test 101 checks (drum loop, synth lead, pad, sub bass, one hit) and ui_sim 28 PASSED.

## Changes in 1.1.1 (2026-11-05, offline)

- The Dynamics panel's knobs are bigger (r 30 instead of 16, value text 15 px instead of 9, names 14 px) and easier to
  touch (104 px wide touch boxes), in the same places.

## Changes in 1.1.0 (2026-11-05, offline)

- **Poli-q inside PoliPlug:** fixed on PoliPlug's side (PoliPlug 1.3.1 hosts effects with up to 384 parameters).
- **Type names** under the knobs are one shared filmstrip now instead of 112 separate images (a black square there was
  MPC's image cache; fewer images means less to go wrong, and a restart of MPC clears it if it ever shows again).
- **Vox presets:** every one has its low cut at 150 Hz (Backing Vocals 200 Hz); the de-esser and mud bands are dynamic.
- **Analyzer** is now called **Visualizer**; new **Learn** and **Dynamics** panel (above).
- Tests: dsp_test 87 checks, ui_sim 28 checks (dynamics panel, Learn switching itself off, save + reload) PASSED.
- Toolchain fix found here: a `picture` whose parameter has digit option names ("1".."8") showed the wrong image
  (`when=key:1` matched the option *named* "1"). `shadow_skin.py` now addresses pictures by index (`key:@i`).

## Build

From WSL: `cd /mnt/c/MPC-PLUGINS/Poli-q && ./build.sh` (skin + params from `generate_skin.py`, aarch64 build, the
toolchain's host test, `test/run_tests.sh`, release zip, copy to the stick). Install: Plugin Manager stick, Local tab,
Poli-q, APPLY.

- `generate_skin.py` holds every number: geometry, presets, the parameter list. It writes `vst/params.json`,
  `vst/layout.conf`, `src/layout_gen.h`, `src/presets_gen.h`, `PRESETS.md` and the images.
- `test/preview.py out.png key=value ...` draws the built skin in any state, driven by the real engine
  (`test/build_probe.sh` first).
- The lower-risk fallback if MPC is slow to open the screen: lower `DOT_POS` (dot places, 40) and `CURVE_COLS` (80)
  in `generate_skin.py`; everything else follows.
