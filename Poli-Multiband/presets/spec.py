"""Poli-Multiband factory presets: the design intent. presets/tune.py turns this into presets/factory.json.

What is fixed here, from how engineers set these up (sources in presets/README.md):
  crossovers, ratio, attack, release, knee, upward ratio, tilt (each band's output offset, dB).
What tune.py measures and sets on reference audio:
  Thresh, to land the gain reduction target (dn, dB: the 90th percentile while the band is active,
         i.e. roughly what a GR meter shows during the loud parts; "dn97" = 97th percentile, for de-essing);
  Upward Thresh, to land the upward boost target (up, dB, 90th percentile);
  Output, as tilt + one makeup for all bands, so the preset plays at the same loudness as the dry signal
         (plus "louder" dB where a preset is meant to be louder).
"fixed": True presets are taken as written (Init, the OTT copy), apart from one output trim if they'd clip.
"lim": (dB, share) lets a preset's safety ceiling work harder than the default (2 dB, 0.2 % of samples).

Reference audio (tune.py --audio): vocal = speech, female + male, each peaking at -3 dBFS; drums = a hip hop drum + bass
loop at -1 dBFS; 808 = a saturated 808 line at -1 dBFS; mix = two finished songs at their own level (~ -0.3 dBFS).

Band keys: ratio, att (ms), rel (ms), knee (dB), uratio, dn / dn97, up, tilt, input (dB, default 0); thr / uthr set by
hand instead of tuned (where the reference audio has nothing in that band).
Names: at most 23 characters (MPC's PRESET menu). The prefix is the category.
"""

D = dict  # shorthand

PRESETS = [
    # ---- Init ---------------------------------------------------------------------------------------------------------
    D(name="Init", cat="Init", fixed=True, x1=500, x2=4000,
      about="Every band at 1:1: the plugin does nothing. Start here to build your own.",
      bands=[D(input=0, out=0, thr=0, att=3, rel=100, knee=0, ratio=1, uthr=-40, uratio=1)] * 3),

    # ---- Vocals -------------------------------------------------------------------------------------------------------
    D(name="Vox: Pop Lead", cat="Vocal", material="vocal", x1=150, x2=6000,
      about="Modern pop lead: steady and forward. The body is held firmly (4:1, about 6 dB), the low end below 150 hz "
            "is kept from booming, and the top is lightly controlled with a little air added.",
      bands=[D(ratio=3, att=8, rel=120, knee=6, dn=4, tilt=-1),
             D(ratio=4, att=5, rel=70, knee=6, dn=6, uratio=1.5, up=2),
             D(ratio=4, att=1, rel=60, knee=6, dn=4, tilt=1.5)]),
    D(name="Vox: Rap Upfront", cat="Vocal", material="vocal", x1=120, x2=5500,
      about="Hip hop lead: fast attack and release keep every word locked in front of the beat, and the upward stage "
            "brings the ends of words up.",
      bands=[D(ratio=4, att=5, rel=80, knee=3, dn=5, tilt=-1.5),
             D(ratio=5, att=2, rel=40, knee=3, dn=8, uratio=2, up=3),
             D(ratio=5, att=0.5, rel=40, knee=3, dn=5, tilt=1)]),
    D(name="Vox: Trap Hook Dense", cat="Vocal", material="vocal", x1=150, x2=5000,
      about="Melodic trap and hooks: very dense, almost OTT on the voice. Heavy downward plus strong upward compression "
            "in the mids and highs, so breaths and tails sit as loud as the words.",
      bands=[D(ratio=4, att=3, rel=80, knee=6, dn=6, tilt=-2),
             D(ratio=6, att=1.5, rel=50, knee=6, dn=9, uratio=2.5, up=6),
             D(ratio=4, att=0.5, rel=40, knee=6, dn=5, uratio=2, up=4, tilt=1)]),
    D(name="Vox: R&B Silk", cat="Vocal", material="vocal", x1=180, x2=7000,
      about="Smooth R&B / soul: soft knees and slower times so the voice is even without sounding squeezed. A gentle "
            "upward lift brings out breathy detail and air.",
      bands=[D(ratio=2.5, att=15, rel=150, knee=12, dn=3, tilt=-0.5),
             D(ratio=3, att=10, rel=100, knee=12, dn=5, uratio=1.8, up=3),
             D(ratio=3, att=2, rel=80, knee=12, dn=3, uratio=1.5, up=2, tilt=1)]),
    D(name="Vox: Ballad Gentle", cat="Vocal", material="vocal", x1=150, x2=6000,
      about="Soft vocals in a sparse mix (acoustic, ballads): 2:1, slow attack and long release, a few dB at most, so "
            "the performance keeps its dynamics.",
      bands=[D(ratio=2, att=20, rel=200, knee=12, dn=2),
             D(ratio=2, att=20, rel=200, knee=12, dn=3),
             D(ratio=2, att=10, rel=150, knee=12, dn=2)]),
    D(name="Vox: Rock Grit", cat="Vocal", material="vocal", x1=250, x2=4000,
      about="Rock / metal vocals: fast attack and release with the mids pushed into about 10 dB of reduction at 6:1. "
            "Brings out the rasp and keeps a screamed line as loud as a sung one.",
      bands=[D(ratio=4, att=3, rel=60, knee=3, dn=5, tilt=-1),
             D(ratio=6, att=1, rel=50, knee=0, dn=10, tilt=1),
             D(ratio=4, att=1, rel=50, knee=3, dn=5)]),
    D(name="Vox: NY Parallel Vox", cat="Vocal", material="vocal", x1=150, x2=6000,
      about="New York (parallel) compression for vocals. Parallel compression lifts the quiet parts and leaves the peaks "
            "alone, which is exactly what upward compression does: thick and present, with the transients intact.",
      bands=[D(ratio=1.5, att=10, rel=100, knee=6, dn=1, uratio=2, up=3),
             D(ratio=2, att=10, rel=100, knee=6, dn=2, uratio=3, up=6),
             D(ratio=1.5, att=5, rel=80, knee=6, dn=1, uratio=2.5, up=5)]),
    D(name="Vox: Air & Presence", cat="Vocal", material="vocal", x1=200, x2=8000,
      about="Brings a dull vocal forward: upward compression on the top band (above 8k) lifts the air and breath, the "
            "mids are evened out gently and the lows are trimmed.",
      bands=[D(ratio=2, att=10, rel=120, knee=6, dn=2, tilt=-1),
             D(ratio=2.5, att=8, rel=80, knee=6, dn=3),
             D(ratio=2, att=2, rel=80, knee=6, dn=2, uratio=3, up=6, tilt=2)]),
    D(name="Vox: Backing Tuck", cat="Vocal", material="vocal", x1=250, x2=5000,
      about="Backing vocals and doubles: compressed harder than the lead so they stay at one level behind it, with the "
            "low end thinned out and the esses pulled back so they don't fight the lead.",
      bands=[D(ratio=3, att=8, rel=100, knee=6, dn=6, tilt=-5),
             D(ratio=5, att=3, rel=60, knee=6, dn=7),
             D(ratio=4, att=0.5, rel=50, knee=6, dn=6, tilt=-2)]),
    D(name="Vox: Podcast Leveler", cat="Vocal", material="vocal", x1=200, x2=6000,
      about="Spoken word, skits and podcasts: evens out loud and quiet words (downward and upward together), and controls "
            "proximity boom when the talker leans in.",
      bands=[D(ratio=3, att=10, rel=150, knee=6, dn=5, tilt=-1),
             D(ratio=3, att=10, rel=150, knee=9, dn=6, uratio=2, up=6),
             D(ratio=3, att=1, rel=60, knee=6, dn=4)]),
    D(name="Vox: De-Ess Male", cat="Vocal", material="vocal", x1=1000, x2=5000,
      about="De-esser for lower voices: only the band above 5k works, fast (0.1 ms) at 8:1, so only the esses and "
            "harsh t's are pulled down. The rest of the voice is untouched.",
      bands=[D(ratio=1), D(ratio=1),
             D(ratio=8, att=0.1, rel=50, knee=3, dn97=8)]),
    D(name="Vox: De-Ess Female", cat="Vocal", material="vocal", x1=1000, x2=7000,
      about="De-esser for higher voices: the same as De-Ess Male with the split at 7k, where higher voices' sibilance "
            "sits.",
      bands=[D(ratio=1), D(ratio=1),
             D(ratio=8, att=0.1, rel=50, knee=3, dn97=8)]),

    # ---- Drums --------------------------------------------------------------------------------------------------------
    D(name="Drm: NY Smash", cat="Drums", material="drums", x1=100, x2=5000,
      about="New York drum compression in one plugin: strong upward compression on every band lifts the room, ghost "
            "notes and tails (the parallel 'smash'), while a 10 ms attack lets the hits through untouched.",
      bands=[D(ratio=4, att=10, rel=120, knee=6, dn=3, uratio=3, up=8),
             D(ratio=4, att=10, rel=120, knee=6, dn=3, uratio=3, up=8),
             D(ratio=4, att=10, rel=120, knee=6, dn=3, uratio=3, up=6)]),
    D(name="Drm: Bus Glue", cat="Drums", material="drums", x1=120, x2=6000,
      about="Classic bus-compressor glue (SSL-style 4:1, 10 ms attack, 250 ms release, 2-3 dB) on each band, so the kick "
            "can't pull the hats down.",
      bands=[D(ratio=4, att=10, rel=250, knee=6, dn=3)] * 3),
    D(name="Drm: Kick Punch", cat="Drums", material="drums", x1=120, x2=3000,
      about="Punch: a 25 ms attack on the low band lets the kick's front through and clamps the body, so the hit stands "
            "out. Boxiness is held back and a little click is lifted.",
      bands=[D(ratio=4, att=25, rel=80, knee=3, dn=5, tilt=1.5),
             D(ratio=3, att=10, rel=80, knee=6, dn=3, tilt=-1),
             D(ratio=2, att=5, rel=60, knee=6, dn=2, uratio=2, up=3, tilt=0.5)]),
    D(name="Drm: Snare Crack", cat="Drums", material="drums", x1=180, x2=4500,
      about="Snare and claps: the body band (180 hz-4.5k) gets a slow-ish attack for crack, and the top band lifts the "
            "ring and air between hits.",
      bands=[D(ratio=2, att=10, rel=100, knee=6, dn=2, tilt=-1),
             D(ratio=4, att=15, rel=100, knee=3, dn=6, tilt=1),
             D(ratio=3, att=2, rel=80, knee=6, dn=3, uratio=2.5, up=4, tilt=1)]),
    D(name="Drm: Dusty Break", cat="Drums", material="drums", x1=150, x2=3500,
      about="Chopped breaks the boom bap way: hard, fast compression that pumps, the quiet ghost notes pulled up, and a "
            "darker, dusty top.",
      bands=[D(ratio=6, att=5, rel=60, knee=3, dn=6, tilt=1),
             D(ratio=8, att=1, rel=40, knee=3, dn=9, uratio=2, up=4),
             D(ratio=6, att=0.5, rel=40, knee=3, dn=8, tilt=-4)]),

    # ---- Bass ---------------------------------------------------------------------------------------------------------
    D(name="Bass: 808 Control", cat="Bass", material="808", x1=100, x2=1200,
      about="808s: the sub (below 100 hz) is held even with a long release, so it doesn't distort, and the harmonics "
            "and knock (100 hz-1.2k) are lifted so the 808 is heard on phones and laptops.",
      bands=[D(ratio=4, att=10, rel=200, knee=6, dn=5),
             D(ratio=3, att=5, rel=120, knee=6, dn=3, uratio=2.5, up=6, tilt=1),
             D(ratio=2, att=1, rel=60, knee=6, thr=-30)]),   # the 808 reference has no top end to tune on
    D(name="Bass: Tight Low End", cat="Bass", material="drums", x1=60, x2=800,
      about="Bass guitar and synth bass: the deep end (below 60 hz) is held tightly and the body (60-800 hz) evened "
            "out. The low band releases at about twice the mid band's time, so it doesn't pump.",
      bands=[D(ratio=4, att=10, rel=160, knee=6, dn=4),
             D(ratio=3, att=15, rel=80, knee=6, dn=3),
             D(ratio=2, att=10, rel=80, knee=6, dn=1.5)]),
    D(name="Bass: Growl Lift", cat="Bass", material="808", x1=100, x2=2500,
      about="Reese, growl and distorted bass: upward compression on the mid and top bands brings the grit and movement "
            "forward, and the sub stays steady.",
      bands=[D(ratio=3, att=10, rel=150, knee=6, dn=3),
             D(ratio=4, att=5, rel=80, knee=6, dn=4, uratio=3, up=6, tilt=1.5),
             D(ratio=3, att=2, rel=60, knee=6, thr=-30, uratio=2, uthr=-42)]),   # the 808 reference has no top end
                                                                                 # to tune on: set by hand

    # ---- Mix bus / master ---------------------------------------------------------------------------------------------
    D(name="Mix: Gentle Glue", cat="Mix", material="mix", x1=120, x2=5000,
      about="Mix bus glue: 2:1 with slow attack and a soft knee, about 2 dB per band. Holds a beat together without "
            "changing its sound.",
      bands=[D(ratio=2, att=30, rel=200, knee=12, dn=2)] * 3),
    D(name="Mst: Hip Hop Master", cat="Master", material="mix", x1=120, x2=5000,
      about="Mastering for hip hop and trap: a tighter low end, gently held mids with a touch of upward density, and "
            "smooth highs.",
      bands=[D(ratio=2.5, att=30, rel=150, knee=9, dn=3, tilt=0.5),
             D(ratio=1.8, att=20, rel=120, knee=12, dn=2, uratio=1.3, up=1.5),
             D(ratio=2, att=10, rel=100, knee=12, dn=2)]),
    D(name="Mst: Clean Polish", cat="Master", material="mix", x1=120, x2=6000,
      about="Transparent mastering: low ratios (1.6 / 1.5 / 1.2:1) with very soft knees, 1-1.5 dB per band, attack slow "
            "enough to keep the transients.",
      bands=[D(ratio=1.6, att=30, rel=150, knee=18, dn=1.5),
             D(ratio=1.5, att=25, rel=150, knee=24, dn=1.5),
             D(ratio=1.2, att=10, rel=120, knee=24, dn=1)]),

    # ---- Creative -----------------------------------------------------------------------------------------------------
    # Ableton's OTT preset (Multiband Dynamics): LR4 splits at 88.3 hz and 2.5k; per band (low/mid/high) input +5.2 dB,
    # output 10.3/5.7/10.3, attack 47.8/22.4/13.5 ms, release 282/282/132 ms, above -33.8/-30.2/-35.5 dB at 66.7:1 / inf
    # (20:1 here, the plugin's top), below -40.8/-41.8/-40.8 dB at 1:4.17.
    D(name="Fx: OTT Full", cat="Creative", fixed=True, x1=88.3, x2=2500, lim=(3.0, 0.01),   # loud is the point:
                                                                    # the ceiling may work harder (3 dB, 1 % of samples)
      about="Ableton's OTT preset at 100 % depth: extreme upward and downward compression on all three bands. Huge, "
            "bright and loud; for sound design, risers, synths and EDM drums.",
      bands=[D(input=5.2, out=10.3, thr=-33.8, att=47.8, rel=282, knee=0, ratio=20, uthr=-40.8, uratio=4.17),
             D(input=5.2, out=5.7, thr=-30.2, att=22.4, rel=282, knee=0, ratio=20, uthr=-41.8, uratio=4.17),
             D(input=5.2, out=10.3, thr=-35.5, att=13.5, rel=132, knee=0, ratio=20, uthr=-40.8, uratio=4.17)]),
    D(name="Fx: OTT Lite", cat="Creative", material="drums", louder=1, x1=88.3, x2=2500,
      about="OTT the way most producers use it (Depth around 30-40 %): the same splits and times as OTT Full, with "
            "milder ratios, for extra energy without wrecking the sound.",
      bands=[D(input=5.2, ratio=3, att=47.8, rel=282, knee=3, dn=4, uratio=1.6, up=4),
             D(input=5.2, ratio=3, att=22.4, rel=282, knee=3, dn=4, uratio=1.6, up=4),
             D(input=5.2, ratio=3, att=13.5, rel=132, knee=3, dn=4, uratio=1.6, up=4, tilt=1)]),
    D(name="Fx: Lo-Fi Phone", cat="Creative", material="vocal", x1=400, x2=3000,
      about="Telephone / radio voice: the lows and highs are turned way down with the band outputs, and the mid band "
            "is squashed flat (10:1). Good for intros, ad-libs and skits.",
      bands=[D(ratio=1, tilt=-24),
             D(ratio=10, att=1, rel=50, knee=3, dn=10, uratio=2, up=4),
             D(ratio=1, tilt=-18)]),
]
