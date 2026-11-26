# How the factory presets were made

```
spec.py  ──tune.py (+ preset_tool, reference audio)──▶  factory.json  ──make_presets.py──▶  src/presets.h
 intent                                                   final values                      vst/params.json "preset"
                                                                                            PRESETS.md
```

- **`spec.py`**: what each preset is for, plus the settings that define its *style*. These follow how engineers set up
  each job (sources below): crossovers, ratio, attack, release, knee, upward ratio, the tonal tilt between bands, and
  how much gain reduction or upward boost each band should do.
- **`tune.py`**: runs the real DSP (`preset_tool.cpp`, built with `-DMB_STATS`) over reference audio. For each band it
  finds the **Thresh** that gives the target reduction and the **Upward Thresh** that gives the target boost (both by
  bisection), then one **makeup** gain for all bands so the preset plays at the dry signal's loudness. The makeup is
  capped where the output's safety ceiling would have real work to do. The measured result goes into
  `factory.json` and `PRESETS.md`.
- **`make_presets.py`**: `factory.json` → the engine's table, the `preset` parameter's option list, and `PRESETS.md`.
  `build.sh` runs it, so editing `factory.json` by hand and rebuilding is enough to change a preset.

## Reference audio (not shipped)
These come from the librosa example data (github.com/librosa/data):
- vocal: two LibriVox readings (public domain), female and male, each normalised to -3 dBFS peak;
- drums: "Choice" (drum + bass) by Admiral Bob (ccMixter, CC BY), at -1 dBFS;
- mix: "Let's Go Fishin'" by Karissa Hobbs and "Vibe Ace" by Kevin MacLeod (CC BY), 60 s each, at their own level;
- 808: a synthetic saturated 808 line (pitch drop, mixed velocities), made by the script in the 1.2.0 session, at -1 dBFS.

To re-tune: put `ref_vocal.wav ref_drums.wav ref_mix.wav ref_808.wav` (16-bit) in a folder, then:
```
g++ -O2 -std=gnu++11 -ffast-math -DMB_STATS -Isrc presets/preset_tool.cpp src/MultibandDSP.cpp -o /tmp/preset_tool
python3 presets/tune.py --audio <folder> --tool /tmp/preset_tool
python3 presets/make_presets.py
```

## What the settings are based on
- **Vocals**:
  - pop/electronic up to high ratios with steady control;
  - dynamic rock/metal 4:1-6:1 with fast times and up to ~10 dB reduction;
  - soft acoustic vocals ≤ 2:1 with slow attack, long release and a few dB (Joey Sturgis Tones, *The best compression
    settings for different vocal styles*);
  - modern rap: about 3:1, fast attack and release, 5-6 dB (SoundOracle, *Compressing modern vocals*);
  - dynamic vocal compression around 5 ms attack (Midi Lifestyle, *How to compress vocals*).
- **Multiband vocal splits**:
  - ~150 hz and ~4k as a start, with heavier low-band compression for thickness (Sound On Sound, *Multi-band
    compression tips*);
  - proximity boom at 200-300 hz;
  - de-essing above ~5k for male and 6-7k for female voices: 10:1+, fastest attack, 50-60 ms release (Sweetwater,
    *Multiband compressor application: vocal track*).
- **New York compression**:
  - drums squashed hard and blended under the dry signal: 8:1, fast, low threshold (MusicRadar, *How to use New York
    compression on drums*).
  - A parallel blend raises the quiet parts and leaves the peaks, which is what upward compression does, so the NY
    presets use the upward stage instead of a mix knob.
- **Bus glue**: SSL-style 4:1, ~10 ms attack, ~300 ms release, 2-3 dB on the loud parts (Nail The Mix, *SSL bus compressor
  settings for mix glue*).
- **Mastering**:
  - gentle ratios of 1.6 / 1.5 / 1.2 / 1.1:1, slow attacks (≥ 5-10 ms), very soft knees (Music Guy Mixing, *The best
    multiband compression mastering settings*);
  - low split around 120 hz, high split ≥ 2.5k (Sound On Sound).
- **Bass**:
  - low split at 50-60 hz for tight deep bass, the upper split a few hundred hz up;
  - low-band release about twice the mid band's (Sound On Sound).
- **OTT**: Ableton's OTT preset values:
  - splits at 88.3 hz and 2.5k;
  - per band input +5.2 dB and output 10.3 / 5.7 / 10.3 dB;
  - attack 47.8 / 22.4 / 13.5 ms, release 282 / 282 / 132 ms;
  - above thresholds -33.8 / -30.2 / -35.5 dB at 66.7:1 / ∞;
  - below thresholds -40.8 / -41.8 / -40.8 dB at 1:4.17

  (github.com/garymaguiremusic-pixel/OTT, OpenMusicStuff/SUBstation #87).
