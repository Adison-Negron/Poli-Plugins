# PoliPlug

An effect rack for the MPC Live III (MPC OS 3.10, aarch64): one track insert that runs up to **16 of the effects
installed on your MPC** in series, plus up to 16 **parallel tracks**, each with its own Send, On/Off and sidechain
ducking, so a track is no longer limited by MPC's own insert slots. Each effect opens with **its own skin**, and its presets are in MPC's PRESET
menu and in a preset bar over its page.

## What it can host

Every **VST effect** registered on the MPC whose `.so` is for the Live III's CPU and that has a skin folder. On the
Live III, the 1.0.1 scan (2026-11-04) found 10:

- AW Ambience, AW Consoles, AW Dynamics (Airwindows)
- Boris Granular
- Liminal Hz
- Shapey EQ, Tuney
- Cwo, Delay (bombest)
- Poli-Ess

Instruments are left out, and so is PoliPlug itself.

**Stock MPC effects (AIR, Akai) can't be hosted.** The scan found their skin folders, but none of them has a plugin file:
they're built into the MPC program. MPC's own submix inserts (4 more per Sub) are the stock way to add more of those.

## Why the hosted skins show (and didn't in the earlier attempt)

MPC draws a plugin's screen from `Plugin Skins/TUI.json` in that plugin's own folder. It finds the folder by the name
of the plugin **it** inserted, and binds the skin when the instance is inserted. A plugin that another plugin loads is
invisible to MPC, so its skin is never read.

PoliPlug carries the effects' pages inside its own skin, built **on the MPC** (`src/discover.c`):

1. **When PoliPlug starts** (once per MPC start), it reads `MPC.settings` and keeps the hostable effects. Each one gets a
   fixed range of page numbers (`Plugin Skins/pp_registry.txt`), so its pages stay where they were across restarts.
2. **If that list or any of those skins changed**, it rewrites its own `Plugin Skins/TUI.json` from `pp_base.json`
   (the rack) plus every effect's pages:
   - Each component is shown only while PoliPlug's `view` parameter is on that page. This is the same mechanism
     `when=` uses, already proven on MPC hardware.
   - The effect's `Parameter N` becomes PoliPlug's proxy `eN`.
   - Its images stay where they are, in its own folder: PoliPlug's skin points at them (`../../<its folder>/...`).
   - A sub-tab drawn the same as an earlier one (Shapey EQ's three, which only switch the Q-Link bank) is merged once.
   - The file is written to a `.tmp` first, then renamed into place. A reinstall of PoliPlug is merged again.
3. **MPC shows a rewritten skin from the next time PoliPlug is inserted** (or the next MPC start). The status line
   says so. Installing an effect restarts MPC anyway.
4. **Open** sets `view` to that effect's page, and the engine routes `eN` to that slot's effect. **< RACK** goes back.
   The Q-Links follow the opened or last-added effect's own Q-Link layout.

## Using it

- **+ Add plugin** lists the installed effects, 8 per page. Turn pages with the **◀ ▶** arrows. Tap one to add it at
  the end of the chain. Tap + Add plugin again to cancel.
- Each row has **Open** (its own page), **up/down** (move it in the chain; a dimmed arrow is the end of the chain) and
  **Bye** (remove it).
- **◀ 1 / 2 ▶** pages through the rack, 4 rows per page, up to 16 effects.
- Saving the project saves the chain and each effect's settings. A project naming an effect that's no longer installed
  loads without it, and the status line says which one was skipped.

## Presets of the opened effect (1.3.0)

Before 1.3.0 a hosted effect's presets were out of reach: MPC's PRESET menu only ever shows the presets of the plugin
**it** inserted (PoliPlug), and PoliPlug had none. Now:

- **MPC's PRESET menu lists the opened effect's presets** (the last one opened or added, the one the Q-Links follow).
  Picking one loads it in that effect. The list is rebuilt every time you open another effect (vst.json `"programs":
  hpreset` + `PROG_DYNAMIC`: the toolchain wrapper refreshes `numPrograms` before each screen update). With nothing open
  it shows one entry, "(open an effect)".
- **A preset bar over the opened page**, in the top strip next to < RACK: `<  name  >`. The arrows step through its
  presets; **tap the name** for a browser of all of them (16 per page, `< 1 / 2 >` pages, the current one highlighted).
  A pick loads it and closes the browser; tapping the name or the browser's background closes it too.
- The bar is hidden for effects that draw **their own selector** (Poli-Ess, Poli-Multiband, Shhhhh from their 2026-11-05
  builds, and Poliwow): PoliPlug sees a parameter named "Preset <" and leaves the selector on their page to them. Their
  presets are still in MPC's PRESET menu.
- Effects with one preset or none show no bar.

## Inserts and Parallel tracks (1.4.0)

Right of the rack: the **Inserts** / **Parallel** tabs. The tab you are on decides what the rows, **+ Add plugin** and the
page arrows work on.

Every row: **On / Off** (green / red; Off fades the effect out in 10 ms and stops running it), the name, **Open**, the
move arrows and **Bye**.

- **Inserts**: effects in series on the track, as before.
- **Parallel**: every effect is its **own track**, side by side (1.3 chained them behind one Send). Each Parallel row has:
  - **Send** (knob): how much of the vocal (what comes out of the Inserts) goes into that effect. Its output is added
    back to the Inserts' output, like an aux send and return. It glides (about 10 ms), so turning it doesn't zipper.
  - **SC** (sidechain): opens that track's **Sidechain** panel over the rack: **Off / On**, **Threshold**, **Attack**,
    **Release**, **Knee**, **Ratio** and a **Ducking now** meter. While it is On, the track's return is turned down
    whenever the vocal itself is over Threshold, like a compressor keyed by the vocal (a reverb or delay that ducks under
    the words and blooms in the gaps). The SC button is green while a track's sidechain is On. Close with X or a tap
    outside the panel.
  - While the panel is up, **Q-Links 1-4 = Threshold, Attack, Release, Knee; 5 = Ratio; 6 = the track's Send**. When it
    closes they go back to the Sends (below).

**Q-Links (1.4.2):** they follow what is on screen. The Live III has 4 Q-Link knobs and pages through the 16 Q-Links
4 at a time: Q-Link page 1 = Q-Links 13, 9, 5, 1 (top to bottom), page 2 = 14, 10, 6, 2, page 3 = 15, 11, 7, 3, page 4 =
16, 12, 8, 4 (seen on the device). PoliPlug counts by page and knob:
- **An opened effect's page**: that effect's own Q-Links.
- **The rack, Parallel tab**: Q-Link page 1 = tracks 1-4's **Send** (top knob = track 1), page 2 = tracks 5-8, and so on:
  each Q-Link page matches a rack page. Named "P1 Send".., the row's knob follows. Empty tracks: nothing.
- **The rack, Inserts tab** (1.5.0): the **macro knobs**: Q-Link page 1 = macros 1-4 (top knob first), page 2 = 5-8,
  and so on (16 in all). Empty macros: nothing.
- **The Sidechain panel**: page 1 = Threshold, Attack, Release, Knee; page 2, top knob = Ratio; the rest nothing.
- **MPC's PRESET menu** follows the screen too: the opened effect's presets on its page, "(open an effect)" on the rack.
- Defaults: Send 50 %, sidechain Off, Threshold -24 dB, Attack 5 ms, Release 250 ms, Knee 6 dB, Ratio 4:1. Ranges:
  Threshold -60..0 dB, Attack 0.1-100 ms, Release 10 ms-2 s, Knee 0-24 dB, Ratio 1-20:1 (ducking up to 60 dB).
- Send is applied *before* the effect (a real send): a compressor on a Parallel track sees a quieter signal at lower
  Send settings.
- Saved state: `PP4|...`: as PP3, plus after every effect `|=on,send,sc on,threshold,attack,release,knee,ratio`.
  Projects from 1.3 (`PP3`) load with their single Send on every Parallel track; **their Parallel effects are now side by
  side instead of chained**, so a 1.3 project with two or more Parallel effects sounds different. Projects from 1.2
  (`PP2`) load into Inserts.

## Limits

- **16 effects in Inserts and 16 in Parallel, 384 parameters each, 95 pages across all effects.**
- MPC's PRESET menu shows at most 128 presets of the opened effect.
- **Automation and MIDI learn on `eN` / `qN` follow whichever effect is open.** Use them live.
- **Saved state is each effect's own saved state** (what MPC would save for it on its own: Liminal Hz keeps its IR), or
  its parameter values for an effect that has none. It all goes through MPC's 8 KB plugin state, so a rack too big for
  that loses its last effects on reload (about 50 typical effects' worth; Shapey EQ takes ~300 bytes).
- Audio between stages is clipped at 0 dBFS, because the toolchain plugins work in int16.
- Every effect runs on the track's audio thread, one after another, so check the CPU with a full rack.
- `hosted.json` is no longer used. You can delete it.

## Scan report

On the first insert after an MPC start, PoliPlug writes `MPC Documents/PoliPlug-scan.txt`. It lists every registered
plugin and whether PoliPlug can host it. It only reads files; it never loads a plugin.

## Files

```
generate.py            mockup -> vst/params.json, vst/layout.conf, vst/skin_assets/, src/pp_gen.h
build.sh               the whole pipeline (WSL): generate, build_port aarch64, tests, release, USB stick
src/poliplug.c         the engine: VST2 host (dlopen + VSTPluginMain), chain, rack, effect list, proxies, state
src/discover.c         installed effects from MPC.settings; rebuilds Plugin Skins/TUI.json with their pages
src/scan.c             the scan report (PoliPlug-scan.txt), read-only, on its own thread
src/mpc_settings.h     MPC.settings helpers shared by both
src/cJSON.c/.h         JSON parser (MIT, src/cJSON.LICENSE)
vst/skin_post.py       keeps the built rack skin as pp_base.json
test/run_tests.sh      x86 + ASan/UBSan: the toolchain host test, then a fake MPC with ../Poli-ess installed
test/preview.py        pictures of the rack, the effect list and an opened effect (test/preview/*.png)
test/stress_test.c     random use from two threads (taps, moves, tabs, preset bar/browser, Sends, On/Off, sidechain, audio of every block size,
                       damaged projects)
test/preset_fx.c       a plain VST2 test effect with 20 presets and no selector of its own (a third-party stand-in)
test/bad_inputs.py     broken installs: damaged skins, bad plugin files, too many effects, no settings, unwritable skin
test/perf_test.c       audio cost per effect and for a full rack (non-sanitizer builds)
test/real_plugins.sh   optional: real installed effects' skins merged and compared page by page, and (from their
                       sources) hosted at run time and compared with the same plugin on its own
```

## Macro knobs (1.5.0)

Right of the rack, on the Inserts tab only (on the Parallel tab the Q-Links are the Sends, so the column is hidden):
- **4 dots** on top = which Q-Link page the 4 knobs show (16 macros = 4 pages of 4). MPC doesn't tell a plugin which
  Q-Link page its knobs are on, so the dots follow the **last Q-Link you turned**; tap a dot to show that page.
- **4 knobs**, top to bottom = the Q-Link knobs of that page. Each drives one parameter of any effect in the rack. Under
  its name: the effect ("1 Poli-Ess", "P2 Delay" for a Parallel one). An empty knob reads "Empty / Map to assign".
- **Map**: tap it (green): the first empty knob of the page is picked (green ring); turn another knob or its Q-Link to
  pick that one instead (nothing moves while Map is on). Then open an effect and move any control (on screen or with
  a Q-Link): it goes on the picked knob and Map turns itself off. **Clear** (shown while Map is on) empties the picked knob.
- A macro follows its parameter when it moves some other way (the effect's page, its preset). An effect removed with
  Bye takes its macros with it. Saved in the project (an `|M=` item after the effects; 1.4.x skips it).
- The section tabs moved into the top strip, between + Add plugin and the page control (which moved right, with a white
  ring so it reads over the blob); the hint text that was under the tabs is gone.

## Status

- **1.5.0 (2026-11-09), offline:** section tabs in the top strip, hint removed, macro column (4 knobs r 40, Q-Link page
  dots, Map / Clear), Q-Links on the Inserts tab = macros by Q-Link page. New host_chain checks (shown/hidden per tab,
  Map arms the first empty knob, turning a knob under Map moves nothing, the next control moved on an effect's page is
  mapped, knob drag drives it with 0 UpdateDisplay and its Q-Link follows, Q-Link 13 turns macro 1, Q-Link 14 shows
  page 2, a dot goes back, the macro follows its parameter, state `|M=` + reload, Clear, Bye drops it); stress_test taps
  the new controls and fuzzes `|M=`. All suites + real_plugins (Poli-q) PASSED.

- **1.4.2 (2026-11-06), offline:** device report on 1.4.1: the Q-Links didn't match (they were numbered as Q-Links 1-6, but
  the Live III shows Q-Link 13, 9, 5, 1 on its first page), and the PRESET name kept the last effect's preset ("Hard
  Tune") on the rack. Q-Links now go by Live III page and knob (`q_slot()`), Parallel Sends one Q-Link page per rack page,
  the panel Threshold..Knee on page 1 and Ratio on page 2 (the panel's Q-Link Send is gone); the PRESET menu reads
  "(open an effect)" on the rack and ignores a program pick there. New checks for each; all suites + real_plugins PASSED.

- **1.4.1 (2026-11-06), offline:** bigger Sidechain panel (868x456, the five knobs in one row at r 62, value text 30,
  names 27; Off/On 150x52; the meter beside the track name) and bigger row Send knobs (r 26 -> 32). Q-Links: none on
  the rack's Inserts tab (they no longer stay on the last opened effect), Q-Links 1-4 = the Sends of the Parallel page shown, the
  panel's knobs while it is up. New checks: Back to the rack takes the Q-Links off the effect (turning one leaves it alone);
  Parallel Q-Links named "P1 Send".., Q-Link 2 turns track 2's Send with its row knob following and 0 UpdateDisplays,
  the row knob moves its Q-Link, Inserts Q-Links touch nothing, panel close returns Q-Link 1 to "P1 Send".
  real_plugins.sh with Poli-q PASSED.

- **1.4.0 (2026-11-05), offline:** a Send per Parallel track (the single Send knob under the tabs is gone), On/Off on
  every row, and a sidechain (ducking) panel per Parallel track with its Q-Links. New checks (host_chain_test): each
  track's Send adds 0 / 0.5 / 1 x on its own; two tracks add up; a Send drag rebuilds nothing (0 UpdateDisplays); Off
  fades without a click and adds nothing; both Inserts Off pass the input untouched; the panel names the track and puts
  Threshold..Send on Q-Links 1-6 (and back after X); a -6 dBFS vocal ducks a track 18 dB at -30 dB / 4:1 (measured
  17.6), a -40 dBFS one not at all; the meter reaches MPC without a screen rebuild; PP4 save + reload plays the same;
  a PP3 project gives its Send to every track. stress_test also taps the new buttons and knobs; real_plugins.sh with
  Poli-q 1.1 PASSED.

- **1.3.2 (2026-11-05), offline:** the Send knob "sometimes didn't respond": on the Parallel tab the status line showed
  "Send 40 %", and every change of that polled text made the wrapper send an UpdateDisplay, so each step of a Send drag
  rebuilt MPC's screen and the touch drag was dropped. The status no longer shows Send (the knob shows its own value).
  New check: a Send drag on the Parallel tab causes 0 UpdateDisplays (it was 6 in half a second).

- **1.3.1 (2026-11-05), offline:** effects with more than 256 parameters show up again (Poli-q 1.1 has 281; PoliPlug's
  proxies went from 256 to 384, the new 128 appended after `send` so projects still load; Q-Links on hosted parameters
  past 127 are kept). The Send knob is no longer cut off at the bottom (its box is now as tall as the knob).
  `test/run_tests.sh` PASSED; `test/real_plugins.sh` with Poli-q 1.1: skin identical alone and inside PoliPlug, 281
  parameters, names, values, texts, audio and saved state all match.

- **1.3.0 (2026-11-05), offline:** presets of the opened effect (PRESET menu + bar + browser) and the Parallel section.
  `test/run_tests.sh` PASSED (x86, ASan/UBSan): the toolchain host test, the chain test with 23 new checks (PRESET menu
  re-listed per opened effect, bar, arrows, browser pages and picks, hidden for Poli-Ess with its own selector, a menu pick
  reaching the hosted Poli-Ess, Parallel tab and list, Send 0/50/100 % adding 0/0.5/1x, Send jump without zipper, state
  with both sections, a 1.2 project loading into Inserts), two stress runs that now also hit the tabs, the bar, the
  browser, Send and MPC's program picks, and the broken-install checks. aarch64 build clean. **Not yet seen on the
  device.**

- **Offline (2026-11-04), 1.2.2:** all of the below PASSED, x86 under ASan/UBSan unless noted.
  - `test/run_tests.sh`: toolchain host test, chain test, skin checks, two 40 s stress runs (16-effect rack reached,
    ~11,000 random actions, 450,000 audio blocks, 190 damaged projects loaded), broken installs (30 checks).
  - Real effects (`test/real_plugins.sh`): all 10 skins merged and compared page by page, pixel-identical; AW Ambience,
    Boris Granular, Cwo, Delay, Liminal Hz, Poli-Ess, Shapey EQ, Tuney hosted and compared with the plugin on its own
    (104 checks); 150 s stress with them (17,600 actions).
  - ThreadSanitizer, screen and audio threads at once: no race in PoliPlug's own code. (It reports small ones inside
    the toolchain's shared wrapper, vst2_wrap.c, which every toolchain plugin has; not changed here.)
  - Audio cost (x86, -O2): 0.006-0.03 ms per block per effect; 16 effects 0.18 ms of the 2.9 ms budget (6 %).
  - aarch64 cross-build clean with -Wall -Wextra.
- **Fixed in 1.2.2:**
  - A hosted effect calling PoliPlug back while it was still starting could hand it a garbage pointer.
  - Adding an effect for the first time loaded its library while the audio waited (AW Ambience 18 ms: a dropout if
    you add it while playing). Libraries now load in the background when + Add plugin opens; an add is under 1.1 ms.
  - Saved state is more compact (16 Boris Granular: 7.6 KB, near MPC's 8 KB, now 6.2 KB); a rack that doesn't fit
    says so on the status line instead of losing effects silently. Names with # are saved correctly.
  - Problems found at start (couldn't write the skin, too many effects, an effect with over 256 parameters) show on the
    status line instead of nowhere. An effect whose skin has no usable page, or binds parameters past 255, isn't listed.
  - A missing image in an effect's skin could show one of PoliPlug's own pictures of the same name.
  - A damaged pp_registry.txt could put two effects on one page.
  - Vendor or effect names with & < > " are found (their skin folder was looked up with the escaped name).
- **Fixed in 1.2.1:** AW value texts overflowed a 64-byte buffer (crash on AW pages); an empty value showed "0.0";
  saved state now uses each effect's own state (Shapey EQ bands, Liminal Hz's IR); identical sub-tabs merged once
  (skin 3.3 MB -> 2.3 MB); a reinstall that put the bare rack skin back is merged again.
- **On the device**, check: the list shows your 10 effects; after re-inserting PoliPlug, each effect opens on its own
  page and its controls work; < RACK comes back; the Q-Link names follow the opened effect; a saved project reloads
  the chain with its settings; adding an effect while playing doesn't click.
