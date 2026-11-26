# Poli effects for the MPC

Twelve native effect plugins for second-generation Akai MPCs (MPC Live III, MPC One G2, MPC Key G2, MPC XL) on MPC OS 3.x,
built with the [open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins) toolchain. Each one has its own
touchscreen skin, Q-Link pages and presets.

| Plugin | What it does |
|---|---|
| Poli-Ess | De-esser: dynamic sibilance suppressor for vocals |
| Poli-q | 8-band dynamic EQ with a live curve, visualizer and Learn |
| Poli-Multiband | 3-band compressor, downward and upward per band |
| Poli-Saturator | Tube, Tape, Clip and Fold saturation with Smash and three-band drive |
| Poli-Chorus | Chorus-ensemble: Classic, Ensemble and Vibrato |
| Poli-delay | Stereo delay: offset, ping-pong, tempo sync, filtered and saturated feedback |
| Poli-verb | Stereo reverb with 14 spaces and tempo-synced pre-delay |
| Shhhhh | Learn-the-noise spectral de-noiser |
| Poli-Juicebox | Low-latency voice shaper: pitch, formant, tamer, harmonics, air |
| Poli-tea | Master bus: saturation, multiband compression, widening, true-peak limiter |
| Poliwow | One-knob build-up effect for risers, sweeps and tails |
| PoliPlug | Effect rack: up to 16 installed effects in series plus a parallel section |

## Install

**From the catalog (easiest):** open the [Open MPC catalog](https://open-mpc.github.io/) or the **Plugin Manager** on your
MPC, find the **Poli effects** collection, tick the plugins you want (or Pick all) and press **Apply**.

**By hand (MPC OS 3.10.1 or later):** download a plugin's `*-mpc-aarch64.zip` from this repo's
[Releases](../../releases), unzip it, and copy the folder inside `portable/` (for example `Poli - VST - Poli-Ess`) into a
folder called `VST2 Plugins` at the root of the MPC's internal drive, an SD card or a USB drive. Turn on
**Preferences, Enhancements, External Plugin Support** and restart the MPC. The effects are listed under
**External VST Plugins, Poli**.

These are Gen2 (64-bit ARM) builds only. They do not run on first-generation MPCs or the Force.

## Build

Each plugin folder is self-contained: `vst/vst.json` (the plugin), `src/` (DSP and the `mpc_engine()` contract),
`generate_skin.py` and `imgs/` (the skin), `test/` (offline tests) and `build.sh` (the whole pipeline).
[docs/TOOLCHAIN.md](docs/TOOLCHAIN.md) explains the layout, skin rules and the testing ladder.

1. Clone the toolchain **beside** this repo and apply the two small changes these plugins need (PoliPlug's run-time program
   list, and picture widgets keyed by option index):
   ```bash
   git clone https://github.com/open-mpc/mpc-vst-plugins.git
   git -C mpc-vst-plugins apply --ignore-whitespace ../Poli-Plugins/toolchain/poli-local.patch
   ```
   Or point `MPC_VST` at a checkout somewhere else.
2. Build one plugin: `cd Poli-Plugins/Poli-ess && ./build.sh none` (Docker, as the toolchain's `build_port.sh` uses), or all
   of them with `build-all/build.sh none all`. `none` skips copying the zip to a USB stick.
3. Without Docker, on Linux with `g++-aarch64-linux-gnu`: see `shared/build_a64_native.sh`. Newer Ubuntu compilers link
   glibc symbols the MPC doesn't have; force-include `shared/glibc239_compat.h` and check
   `readelf -V <so> | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1` is 2.36 or lower.

`vst/params.json` is append-only and `vst/params.lock` must stay committed: saved MPC projects store parameter values by
position. A plugin's `uid` in `vst/vst.json` never changes.

## Releasing

Each plugin is its own GitHub release, tagged `<catalog id>-v<version>` (for example `poli-ess-v1.0.6`), with one asset,
`<Name>-<version>-mpc-aarch64.zip`. After building:

```bash
python3 tools/release_all.py <github-owner>          # release/<tag>/<zip> + release/RELEASES.md, all checked with catalog_check --catalog
python3 tools/make_catalog_entries.py <github-owner> # catalog/collections/poli.json + catalog/plugins/*.json for the catalog PR
```

Test each zip on a device before publishing its release. The catalog picks up new releases by itself every night.

## License

MIT, see [LICENSE](LICENSE). PoliPlug includes [cJSON](https://github.com/DaveGamble/cJSON) (MIT). The fonts used for the
skins (Open Sans, Titillium Web, Poppins) are under the SIL Open Font License 1.1, with the license beside each.
