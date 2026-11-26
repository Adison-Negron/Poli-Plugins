# Poliwow

A one-knob build-up effect for the Akai MPC Live III (Gen 2, MPC OS 3.10), in the spirit of Dada Life's Endless Smile.
Turn **Wow** up and the track builds. Snap Wow back to 0 for the drop: the reverb, echoes and grains ring out instead
of being cut off.

**1.1: everything is made from the track itself**, so it blends in rather than sounding like a riser playing over
the top:
- the **air** is noise shaped by the track's own hi-hats and S's;
- the **rising tone** is a resonance that the track rings, its pitch climbing;
- the **pads** are grains of the track (a granular cloud, in the spirit of Finisher Micro's "Infinity" mode) and an
  octave-up **shimmer reverb** whose decay grows with Wow, up to near-infinite in Halo.

The core of the sound is level-matched to the dry track as the high-pass takes the body away, so the build-up rises
steadily on any material instead of sagging at the top.

**1.2: a Granules page, a help window and bright build-ups.** The **Settings** pill (top right) switches the screen between **WOW** (the two faces,
as before) and **GRANULES** (a granulator whose grains live in a room); the pill turns yellow on the Granules page, tap it again to go back. The **?** button after the title opens a two-page help window that explains every parameter and preset family (tap it to close, the arrows turn the page). The screens follow the mockups `imgs/1.jpg` and `imgs/2.jpg`. Five new bright presets build with shimmer reverb
instead of a noise riser. Everything new starts neutral: the 16 older presets sound exactly as in 1.1.1 until a GRAINS
knob is touched, and a project saved in 1.1 reloads as it was.

**1.3: 39 presets in 7 categories that use the granulator, an Amount knob, and a full-width screen.** Presets now carry their own Granules settings; a new **Amount** knob (Settings page) sets how many grains there are, and **Level** is now the cloud's volume. The preset name opens a browser like Poli-Tea's (categories left, presets right), bigger faces and the whole MPC screen are used. Amount is on the Settings page only. Projects saved in 1.1 / 1.2 load by preset name (1.1) with the grains neutral; presets are numbered differently from 1.2, so a 1.2 project may show another preset.

## Controls

| Control | What it does |
|---|---|
| **Wow** (big face) | 0–100 %: how far the build-up has gone. The face's colour changes with it (purple-green → yellow-orange → red). At 0 % the audio passes untouched, sample for sample. |
| **Mix** (yellow face) | 0–100 %: dry / wet. Default 100 %. |
| **◀ preset ▶** | Under the title: the preset's name between two arrows (steps through all presets, wraps round). **Tap the name** to open the preset browser: categories (All, Bright, Soft, Granular, Retrace, Synced, Medium, Hard) on the left, the presets of the one you chose on the right (12 to a page, page arrows at the top right). Tap a preset to load it; the browser closes. Tap the name or the panel's edge to close it without picking. The same presets are in MPC's **PRESET** menu. A preset also sets the Granules knobs. |

Both knobs turn from 9 o'clock (0 %) over the top (50 %) to 3 o'clock (100 %), and show their exact value in the middle
of the face. Q-Links: 1 = Wow, 2 = Mix, 3 = Preset.

### Granules page

The grains of the cloud move about in a room (left wall to right wall, which is also their stereo position). When a grain
bounces off a wall or another grain it can split. All knobs are 0–100 %. They work together with the preset's own cloud
and only sound while Wow is up; at Wow 0 the audio still passes untouched.

| Control | What it does |
|---|---|
| **Amount** (big knob, left) | How many grains: extra grains on top of the preset's own (0 = none extra). Gives presets that have no grains some. |
| **Level** | The cloud's volume: 50 % = as the preset has it, 100 % = double, 0 = silent. |
| **Size** | Grain length, x0.25 … x4 (50 % = the preset's own). Shown as "1x", "2.5x". |
| **Density** | Grains per second, x0.25 … x4 (50 % = the preset's own). In Sync, the nearest note value. |
| **Speed** | How fast grains move through the room. 0 = they stay where they were born (with Split up they drift slowly so they can still meet). |
| **Split** | Chance that a bounce splits a grain in two: a child at another harmonic (octave, fifth, fourth, octave down) and another size, a little quieter. Children can split once more, then stop. |
| **Retrace** | Chance that a grain goes back in time while it plays. |
| **Time** | How far back a retrace goes, 50 ms … 2 s. |
| **Length** | How long the retraced piece plays, 40 ms … 1 s. |
| **Variation** | Sync off: how uneven the spacing of grains is. Sync on: how far each grain lands off the beat grid. |
| **Retrace: Jump / Reverse** | Jump: the grain skips back and goes on playing forwards. Reverse: the grain plays backwards from the point it jumped to. |
| **Sync: Off / On** | On: grains are born on the song's beat grid (a power-of-two number of grains per beat, nearest to Density) while the sequencer plays. Off: free-running. |

Q-Links follow the page on screen. WOW: Wow, Mix, Preset. GRANULES (the rows of the screen): Size, Density, Speed, Variation |
Retrace, Split, Time, Length | Amount, Level, Retrace mode (above 50 % = Reverse), Sync (above 50 % = On).

Tempo: the pump and the echoes follow MPC's tempo. With the sequencer playing, the pump locks to the beat.

## Presets (39, in 7 categories)

Each preset is a base sound (the 21 sounds of 1.2) plus its own **Granules settings**. Picking a preset sets the Amount, Level, Size ... Sync knobs to the preset's values (you see them move); from then on the knobs are yours, and they are saved with the project. Picking the same preset again leaves them alone. The browser (tap the preset name) shows them by category.

| Category | Presets | Character |
|---|---|---|
| Bright | Glass Rise, Sparkle Sweep, Bloom, Crystal Rush, Aurora, Prism Lift, Shimmer Veil, Starlight Rise | A bright shimmer reverb sweeps up as Wow rises: the build-up sweeps. Little or no pump. |
| Soft | Heaven Lift, Soft Smile, Cloud Nine, Halo, Angel Dust, Rising Echoes | Airy and ethereal: long tails, soft grains, no noise riser. |
| Granular | Grain Storm, Room Bounce, Shatter, Snow Globe, Confetti, Swarm | The grain room is the sound: swarms, bounces, splits and scatter. Amount is high. |
| Retrace | Time Slip, Rewind, Echo Back, Backspin Rise, Deja Vu | Grains that jump back in time or play backwards (Reverse presets). |
| Synced | Pulse Cloud, Ticker, Stutter Rise, Loose Grid, Half Time Haze | Grains on the beat grid while the sequencer plays (Sync on); they lock to MPC's tempo. |
| Medium | Sunrise, Big Smile, Bouncy Smile, Tape Rise | Resonant openings, a speeding pump, or the track climbing like tape. |
| Hard | Hard Grin, Barber Pole, Face Melter, Liftoff, Endless Wow | Resonance, drive, a fast pump and climbing echoes up to the extreme. |

The bright presets rise through the **reverb** itself: its send and its return are high-passed more and more, and its tone tilts brighter, as Wow goes up. That is what makes them usable as a build-up sweep rather than a noise riser.

`test/demo-1.2/` has a 16-bar build-up for each preset (regenerated by `run_tests.sh`), plus the dry loop.

## Build and install

From WSL: `cd /mnt/c/MPC-PLUGINS/Poliwow && ./build.sh`. Then on the MPC: Plugin Manager stick, **Local** tab,
install Poliwow, **APPLY**. 1.3.0: run `build.sh` (it writes `dist/Poliwow-1.3.0-mpc-aarch64.zip` and copies it to the stick).
`dist/` still holds the built 1.1.1 zip; remove older Poliwow zips from the stick.

`build_a64_native.sh` is the same build without Docker (Ubuntu's aarch64 cross compiler and a local Playwright); the
released zips were built with it. Both scripts stop if the plugin ever needs `libmvec` (see `-U__FAST_MATH__` in
`vst/vst.json`).

Projects saved with 1.0.x: the preset list changed in 1.1, so a project reopens with whichever 1.1 preset now sits at
its old number. Wow and Mix come back as saved.

## Files

| Path | What |
|---|---|
| `src/WowDSP.*` | the effect chain; each preset is a small function by name (`heavenLift()`, `halo()`, ...) |
| `src/engine.cpp` | the MPC engine contract: params, value text, preset arrows, pages, page-following Q-Link slots, saved state (`PWW2`; `PWW1` from 1.1 still loads) |
| `generate_skin.py` | mockups (`imgs/`) → `vst/skin_assets/*.png` + `vst/layout.conf` |
| `vst/skin_post.py` | swaps in the knob filmstrips with exact frame counts, unclips the knobs |
| `test/` | `dsp_test.cpp`, `ui_sim.c`, `stress_test.cpp`, `grain_test.cpp`, `render_demos.cpp`, `calibrate.cpp`, `run_tests.sh`, `compose_preview.py`, previews |

## Tests (`test/run_tests.sh`, run by `build.sh`, about 5 minutes)

- **grain_test** (1.2): every preset under extreme GRAINS settings stays finite and bounded; Wow 0 stays bit-exact with every GRAINS knob at 100; Split, Retrace (Jump forwards, Reverse backwards), Speed and Level do what they say; Sync locks grain births to the tempo grid and Variation moves them off it; CPU.

- **dsp_test**: Wow 0 passes audio bit for bit; every preset's build-up gets brighter and wider, keeps its level above
  the bass and doesn't clip; tails ring after the drop; silence stays silent; state, arrows, "%" text.
- **ui_sim**: the real MPC wrapper with a mock MPC: preset arrows (with the 250 ms tap debounce), PRESET menu, value
  text, save and reload of a project, broken saved data.
- **stress_test**, the hiccup hunt:
  - a 0 dBFS track at Wow 0 stays bit-exact;
  - the output is an exact copy of the input again 2.5-7 s after Wow returns to 0 (the longest in Halo);
  - 60 s of full-scale noise and square waves per preset;
  - no denormals after silence;
  - "nan" / "inf" / garbage values and broken states;
  - 20 000 fuzzed blocks;
  - no clicks on instant Wow / Mix / preset / tempo jumps;
  - the NaN safety net;
  - pump-speed hysteresis;
  - speed.
- **calibrate** (by hand: `g++ -DPOLIWOW_PROBE -Isrc test/calibrate.cpp src/WowDSP.cpp`): per preset, how loud the
  core, grains, echoes and reverb are against the dry loop, and how often the limiter works. This is how the presets
  were balanced.
