// Poliwow DSP (see WowDSP.h for the signal flow).
#include "WowDSP.h"
#include <cmath>
#include <cstring>
#include <cstdint>

static const float kFs = 44100.0f;
static const float kPi = 3.14159265358979f;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float dbToLin(float db) { return powf(10.0f, db * 0.05f); }
static inline float flush(float v) { return fabsf(v) < 1e-20f ? 0.0f : v; }
static inline bool finiteBits(float v) { uint32_t u; memcpy(&u, &v, 4); return (u & 0x7f800000u) != 0x7f800000u; }
static inline float softclip(float x) { return x / sqrtf(1.0f + x * x); }   // cheap tanh-like, for feedback loops

// ---- the presets ---------------------------------------------------------------------------------------------------
// Each starts from base() (nothing happens) and switches on what it needs. Ordered soft to extreme.
static WowStyle base(const char *name) {
    WowStyle s;
    s.name = name;
    s.tiltDb = 0; s.hpfTo = 10; s.hpfQ = 0.7071f; s.hpfCurve = 1.5f; s.driveDb = 0;
    s.phaser = 0; s.phRate0 = 0.2f; s.phRate1 = 0.5f;
    s.pump = 0; s.pumpBeats0 = 1; s.pumpBeats1 = 1;
    s.dly = 0; s.dlyFb = 0.3f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.5f; s.shiftHz = 0;
    s.verb = 0; s.decay0 = 2.5f; s.decay1 = 4; s.shimmer = 0; s.shimmerSemi = 12; s.verbDamp = 9000;
    s.verbHpTo = 0; s.verbTiltDb = 0;
    s.cloud = 0; s.grainMs = 120; s.density0 = 8; s.density1 = 30; s.spray = 0.5f; s.riseSemi = 0;
    s.octaveProb = 0; s.reverseProb = 0; s.spread = 0.8f;
    s.air = 0; s.reso = 0; s.resoLo = 200; s.resoHi = 1600;
    s.width = 0; s.makeupDb = 0; s.dryCutDb = 0;
    return s;
}

// -- soft and ethereal: brighter, longer, the track lifting into the air
static WowStyle heavenLift() {   // bright lift into a long octave shimmer, echoes barely climbing
    WowStyle s = base("Heaven Lift");
    s.tiltDb = 6; s.hpfTo = 350; s.hpfQ = 0.8f;
    s.dly = 0.15f; s.dlyFb = 0.35f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.5f; s.shiftHz = 2.5f;
    s.verb = 0.7f; s.decay0 = 3; s.decay1 = 12; s.shimmer = 0.35f; s.verbDamp = 11000;
    s.width = 0.5f; s.makeupDb = 1.5f; s.dryCutDb = 2;
    return s;
}
static WowStyle softSmile() {    // the gentle one: a little air, a dotted echo, a room
    WowStyle s = base("Soft Smile");
    s.tiltDb = 4; s.hpfTo = 600; s.hpfQ = 1.0f; s.hpfCurve = 1.6f; s.driveDb = 2;
    s.dly = 0.25f; s.dlyFb = 0.35f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.75f;
    s.verb = 0.55f; s.decay0 = 2; s.decay1 = 4;
    s.width = 0.2f; s.makeupDb = 1.5f;
    return s;
}
static WowStyle cloudNine() {    // the track turns into an aerial pad: long soft grains into a big reverb
    WowStyle s = base("Cloud Nine");
    s.tiltDb = 6; s.hpfTo = 250;
    s.cloud = 1.2f; s.grainMs = 180; s.density0 = 6; s.density1 = 40; s.spray = 1.2f; s.octaveProb = 0.35f; s.spread = 1;
    s.verb = 0.6f; s.decay0 = 4; s.decay1 = 25; s.shimmer = 0.2f;
    s.width = 0.6f; s.makeupDb = 1; s.dryCutDb = 9;
    return s;
}
static WowStyle halo() {         // shimmer that almost never ends: hold Wow at the top and the track floats
    WowStyle s = base("Halo");
    s.tiltDb = 5; s.hpfTo = 400;
    s.verb = 1.3f; s.decay0 = 4; s.decay1 = 60; s.shimmer = 0.45f; s.verbDamp = 10000;
    s.cloud = 0.5f; s.grainMs = 250; s.density0 = 4; s.density1 = 12; s.spray = 1.5f; s.octaveProb = 0.5f; s.spread = 1;
    s.width = 0.7f; s.makeupDb = 1; s.dryCutDb = 6;
    return s;
}
static WowStyle risingEchoes() { // stereo echoes that climb a little on every repeat
    WowStyle s = base("Rising Echoes");
    s.tiltDb = 3; s.hpfTo = 500;
    s.dly = 0.55f; s.dlyFb = 0.55f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.5f; s.shiftHz = 6;
    s.verb = 0.35f; s.decay0 = 2; s.decay1 = 6;
    s.width = 0.5f; s.makeupDb = 1;
    return s;
}
static WowStyle angelDust() {    // sparkle: short grains, mostly an octave up, an octave-and-a-fifth shimmer
    WowStyle s = base("Angel Dust");
    s.tiltDb = 6; s.hpfTo = 800;
    s.cloud = 0.75f; s.grainMs = 50; s.density0 = 10; s.density1 = 70; s.spray = 0.4f; s.octaveProb = 0.7f; s.spread = 1;
    s.verb = 0.5f; s.decay0 = 2; s.decay1 = 8; s.shimmer = 0.25f; s.shimmerSemi = 19;
    s.width = 0.8f; s.makeupDb = 0.5f;
    return s;
}
// -- medium: the build-up starts to push
static WowStyle sunrise() {      // a slow resonant opening into shimmer, a little air, a light pump
    WowStyle s = base("Sunrise");
    s.tiltDb = 4; s.hpfTo = 1200; s.hpfQ = 2.5f; s.hpfCurve = 1.3f;
    s.phaser = 0.3f; s.phRate0 = 0.2f; s.phRate1 = 1.0f;
    s.pump = 0.2f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.5f;
    s.dly = 0.2f; s.dlyFb = 0.4f;
    s.verb = 1.0f; s.decay0 = 2; s.decay1 = 10; s.shimmer = 0.3f;
    s.air = 0.15f;
    s.width = 0.5f; s.makeupDb = 2;
    return s;
}
static WowStyle bigSmile() {
    WowStyle s = base("Big Smile");
    s.tiltDb = 2; s.hpfTo = 1500; s.hpfQ = 2.0f; s.hpfCurve = 1.4f; s.driveDb = 8;
    s.phaser = 0.3f; s.phRate0 = 0.3f; s.phRate1 = 2.0f;
    s.pump = 0.4f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.25f;
    s.dly = 0.35f; s.dlyFb = 0.5f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.75f;
    s.verb = 0.9f; s.decay0 = 2; s.decay1 = 6;
    s.air = 0.2f; s.reso = 0.15f;
    s.width = 0.5f; s.makeupDb = 1.5f;
    return s;
}
static WowStyle bouncySmile() {  // a deep pump that speeds up to 1/32 notes, like a snare roll
    WowStyle s = base("Bouncy Smile");
    s.hpfTo = 1200; s.hpfQ = 3.0f; s.hpfCurve = 1.4f; s.driveDb = 6;
    s.pump = 0.8f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.125f;
    s.dly = 0.3f; s.dlyFb = 0.45f; s.dlyBeatsL = 0.5f; s.dlyBeatsR = 0.5f;
    s.verb = 0.5f; s.decay0 = 1.5f; s.decay1 = 3;
    s.air = 0.1f;
    s.width = 0.3f; s.makeupDb = 3;
    return s;
}
static WowStyle tapeRise() {     // the track itself climbs an octave, like tape speeding up
    WowStyle s = base("Tape Rise");
    s.hpfTo = 300;
    s.cloud = 1.15f; s.grainMs = 90; s.density0 = 20; s.density1 = 50; s.spray = 0.05f; s.riseSemi = 12; s.spread = 0.4f;
    s.verb = 0.4f; s.decay0 = 2; s.decay1 = 6;
    s.width = 0.4f; s.makeupDb = 1; s.dryCutDb = 10;
    return s;
}
// -- hard
static WowStyle hardGrin() {
    WowStyle s = base("Hard Grin");
    s.hpfTo = 2500; s.hpfQ = 5.0f; s.hpfCurve = 1.3f; s.driveDb = 12;
    s.phaser = 0.3f; s.phRate0 = 0.5f; s.phRate1 = 4.0f;
    s.pump = 0.5f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.25f;
    s.dly = 0.4f; s.dlyFb = 0.55f; s.dlyBeatsL = 0.5f; s.dlyBeatsR = 0.5f;
    s.verb = 1.0f; s.decay0 = 2; s.decay1 = 6;
    s.air = 0.35f; s.reso = 0.35f; s.resoLo = 200; s.resoHi = 1600;
    s.width = 0.6f; s.makeupDb = 2;
    return s;
}
static WowStyle barberPole() {   // echoes that climb in pitch on every repeat
    WowStyle s = base("Barber Pole");
    s.hpfTo = 1800; s.hpfQ = 2.5f; s.hpfCurve = 1.4f; s.driveDb = 8;
    s.phaser = 0.6f; s.phRate0 = 0.3f; s.phRate1 = 6.0f;
    s.pump = 0.3f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.25f;
    s.dly = 0.65f; s.dlyFb = 0.7f; s.dlyBeatsL = 0.375f; s.dlyBeatsR = 0.375f; s.shiftHz = 18;
    s.verb = 1.0f; s.decay0 = 2; s.decay1 = 6; s.shimmer = 0.15f;
    s.air = 0.15f;
    s.width = 0.7f; s.makeupDb = 2.5f;
    return s;
}
static WowStyle grainStorm() {   // dense, scattered, half-backwards grains over a pump
    WowStyle s = base("Grain Storm");
    s.hpfTo = 1500; s.hpfQ = 3.0f; s.driveDb = 10;
    s.cloud = 1.1f; s.grainMs = 40; s.density0 = 30; s.density1 = 160; s.spray = 0.8f; s.riseSemi = 5;
    s.octaveProb = 0.4f; s.reverseProb = 0.5f; s.spread = 1;
    s.pump = 0.5f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.125f;
    s.verb = 0.5f; s.decay0 = 2; s.decay1 = 6;
    s.reso = 0.25f;
    s.width = 0.8f; s.makeupDb = 2.5f; s.dryCutDb = 4;
    return s;
}
static WowStyle faceMelter() {
    WowStyle s = base("Face Melter");
    s.hpfTo = 3000; s.hpfQ = 6.0f; s.hpfCurve = 1.2f; s.driveDb = 20;
    s.phaser = 0.5f; s.phRate0 = 1.0f; s.phRate1 = 8.0f;
    s.pump = 0.7f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.125f;
    s.dly = 0.5f; s.dlyFb = 0.65f; s.dlyBeatsL = 0.25f; s.dlyBeatsR = 0.25f; s.shiftHz = 8;
    s.verb = 1.0f; s.decay0 = 2; s.decay1 = 7;
    s.air = 0.45f; s.reso = 0.45f; s.resoLo = 150; s.resoHi = 3000;
    s.cloud = 0.2f; s.grainMs = 60; s.density0 = 20; s.density1 = 60; s.riseSemi = 12; s.spray = 0.2f;
    s.width = 0.8f; s.makeupDb = 2;
    return s;
}
static WowStyle liftoff() {      // everything climbs: resonance, grains and shimmer together
    WowStyle s = base("Liftoff");
    s.hpfTo = 2000; s.hpfQ = 3.0f; s.hpfCurve = 1.3f; s.driveDb = 8;
    s.pump = 0.4f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.25f;
    s.verb = 1.0f; s.decay0 = 3; s.decay1 = 20; s.shimmer = 0.4f;
    s.cloud = 0.8f; s.grainMs = 100; s.density0 = 15; s.density1 = 50; s.spray = 0.15f; s.riseSemi = 12;
    s.air = 0.3f; s.reso = 0.4f; s.resoLo = 100; s.resoHi = 2500;
    s.width = 0.9f; s.makeupDb = 3; s.dryCutDb = 3;
    return s;
}
static WowStyle endlessWow() {   // all of it at once
    WowStyle s = base("Endless Wow");
    s.hpfTo = 3500; s.hpfQ = 4.0f; s.hpfCurve = 1.2f; s.driveDb = 16;
    s.phaser = 0.4f; s.phRate0 = 0.5f; s.phRate1 = 6.0f;
    s.pump = 0.5f; s.pumpBeats0 = 0.5f; s.pumpBeats1 = 0.125f;
    s.dly = 0.55f; s.dlyFb = 0.75f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.5f; s.shiftHz = 30;
    s.verb = 1.0f; s.decay0 = 3; s.decay1 = 30; s.shimmer = 0.35f; s.shimmerSemi = 19;
    s.air = 0.4f; s.reso = 0.4f; s.resoLo = 100; s.resoHi = 4000;
    s.cloud = 0.6f; s.grainMs = 70; s.density0 = 20; s.density1 = 90; s.spray = 0.6f; s.riseSemi = 7;
    s.octaveProb = 0.5f; s.reverseProb = 0.3f; s.spread = 1;
    s.width = 1.0f; s.makeupDb = 2;
    return s;
}

// -- bright build-ups (1.2): the reverb is the star, and it gets brighter and thinner as Wow climbs. The reverb send's
//    high-pass sweeps up (verbHpTo), the return has a high shelf (verbTiltDb), the reverb's own damping is wide open.
static WowStyle glassRise() {    // a clean bright sweep: the track thins out and shimmers upward into a glassy hall
    WowStyle s = base("Glass Rise");
    s.tiltDb = 9; s.hpfTo = 3000; s.hpfQ = 1.0f; s.hpfCurve = 1.6f;
    s.dly = 0.12f; s.dlyFb = 0.35f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.5f; s.shiftHz = 3;
    s.verb = 1.1f; s.decay0 = 3; s.decay1 = 14; s.shimmer = 0.35f; s.verbDamp = 16000;
    s.verbHpTo = 3500; s.verbTiltDb = 6;
    s.air = 0.12f;
    s.cloud = 0.3f; s.grainMs = 90; s.density0 = 12; s.density1 = 50; s.spray = 0.3f; s.octaveProb = 0.5f; s.spread = 1;
    s.width = 0.8f; s.makeupDb = 1.5f; s.dryCutDb = 3;
    return s;
}
static WowStyle sparkleSweep() { // sparkling: octave-and-a-fifth shimmer, bright echoes, an airy high-pass sweep
    WowStyle s = base("Sparkle Sweep");
    s.tiltDb = 8; s.hpfTo = 4000; s.hpfQ = 1.5f; s.hpfCurve = 1.3f;
    s.dly = 0.3f; s.dlyFb = 0.5f; s.dlyBeatsL = 0.75f; s.dlyBeatsR = 0.5f; s.shiftHz = 6;
    s.verb = 1.0f; s.decay0 = 2; s.decay1 = 10; s.shimmer = 0.3f; s.shimmerSemi = 19; s.verbDamp = 16000;
    s.verbHpTo = 4500; s.verbTiltDb = 7;
    s.air = 0.2f;
    s.cloud = 0.5f; s.grainMs = 40; s.density0 = 20; s.density1 = 90; s.spray = 0.3f; s.octaveProb = 0.7f; s.spread = 1;
    s.width = 0.9f; s.makeupDb = 1; s.dryCutDb = 2;
    return s;
}
static WowStyle bloom() {        // a bright reverb that swells and opens: the build-up is the tail growing
    WowStyle s = base("Bloom");
    s.tiltDb = 6; s.hpfTo = 800; s.hpfQ = 0.8f;
    s.verb = 1.4f; s.decay0 = 4; s.decay1 = 25; s.shimmer = 0.25f; s.verbDamp = 15000;
    s.verbHpTo = 1500; s.verbTiltDb = 5;
    s.dly = 0.1f; s.dlyFb = 0.3f;
    s.width = 0.8f; s.makeupDb = 1; s.dryCutDb = 6;
    return s;
}
static WowStyle crystalRush() {  // faster and harder, but still bright: a rising pump, a thin sweep, glittering echoes
    WowStyle s = base("Crystal Rush");
    s.tiltDb = 8; s.hpfTo = 5000; s.hpfQ = 2.5f; s.hpfCurve = 1.2f; s.driveDb = 4;
    s.phaser = 0.25f; s.phRate0 = 0.3f; s.phRate1 = 3.0f;
    s.pump = 0.35f; s.pumpBeats0 = 1; s.pumpBeats1 = 0.125f;
    s.dly = 0.35f; s.dlyFb = 0.55f; s.dlyBeatsL = 0.375f; s.dlyBeatsR = 0.375f; s.shiftHz = 10;
    s.verb = 1.0f; s.decay0 = 2; s.decay1 = 8; s.shimmer = 0.2f; s.verbDamp = 16000;
    s.verbHpTo = 6000; s.verbTiltDb = 8;
    s.air = 0.3f; s.reso = 0.12f; s.resoLo = 400; s.resoHi = 4000;
    s.width = 0.9f; s.makeupDb = 2;
    return s;
}
static WowStyle aurora() {       // the track becomes a bright, slow pad: long grains an octave up into a shimmering hall
    WowStyle s = base("Aurora");
    s.tiltDb = 7; s.hpfTo = 600;
    s.cloud = 1.0f; s.grainMs = 220; s.density0 = 5; s.density1 = 30; s.spray = 1.5f; s.octaveProb = 0.45f; s.spread = 1;
    s.verb = 1.2f; s.decay0 = 5; s.decay1 = 30; s.shimmer = 0.4f; s.shimmerSemi = 19; s.verbDamp = 15000;
    s.verbHpTo = 2500; s.verbTiltDb = 6;
    s.width = 1.0f; s.makeupDb = 1; s.dryCutDb = 8;
    return s;
}

// built when the plugin loads (a plain global: no C++ runtime needed)
const WowStyle kStyles[kNumStyles] = {
    heavenLift(), softSmile(), cloudNine(), halo(), risingEchoes(), angelDust(),
    sunrise(), bigSmile(), bouncySmile(), tapeRise(),
    hardGrin(), barberPole(), grainStorm(), faceMelter(), liftoff(), endlessWow(),
    glassRise(), sparkleSweep(), bloom(), crystalRush(), aurora(),   // 1.2: bright build-ups
};

// ---- presets (1.2): a base sound + the granulator settings it starts with ---------------------------------------------------
const char *const kCats[kNumCats] = {"Bright", "Soft", "Granular", "Retrace", "Synced", "Medium", "Hard"};

enum Base { B_HEAVEN, B_SOFT, B_CLOUD, B_HALO, B_ECHOES, B_ANGEL, B_SUNRISE, B_BIGSMILE, B_BOUNCY, B_TAPE, B_GRIN, B_BARBER, B_STORM,
            B_MELTER, B_LIFTOFF, B_ENDLESS, B_GLASS, B_SPARKLE, B_BLOOM, B_CRYSTAL, B_AURORA };

// amount (extra grains), size, density (0.5 = the base sound's own), speed, split, retrace, retrace time, retrace length, variation,
// retrace mode (1 = reverse), sync (1 = on the tempo grid)
static GrainCtl G(float amount = 0, float size = 0.5f, float dens = 0.5f, float speed = 0, float split = 0, float ret = 0,
                  float rt = 0.5f, float rl = 0.5f, float var = 0, int rev = 0, int sync = 0) {
    GrainCtl g;
    g.amount = amount; g.size = size; g.density = dens; g.speed = speed; g.split = split; g.retrace = ret;
    g.retTime = rt; g.retLen = rl; g.variation = var; g.retReverse = rev != 0; g.sync = sync != 0;
    return g;
}
static WowPreset P(const char *name, int cat, int base, const GrainCtl &g) { WowPreset p; p.name = name; p.cat = cat; p.base = base; p.g = g; return p; }

const WowPreset kPresets[kNumPresets] = {
    // Bright: build-ups that rise through a bright shimmer reverb
    P("Glass Rise", 0, B_GLASS, G(.10f, .40f, .50f, .20f, .15f)),
    P("Sparkle Sweep", 0, B_SPARKLE, G(.20f, .30f, .65f, .30f, .30f)),
    P("Bloom", 0, B_BLOOM, G(.25f, .70f, .40f, .15f, .10f)),
    P("Crystal Rush", 0, B_CRYSTAL, G(.30f, .25f, .75f, .50f, .45f, 0, .5f, .5f, .30f)),
    P("Aurora", 0, B_AURORA, G(.30f, .75f, .35f, .20f, .25f)),
    P("Prism Lift", 0, B_GLASS, G(.50f, .35f, .60f, .40f, .60f)),
    P("Shimmer Veil", 0, B_BLOOM, G(.15f, .85f, .30f, .10f, .05f, 0, .5f, .5f, .20f)),
    P("Starlight Rise", 0, B_SPARKLE, G(.40f, .30f, .70f, .20f, .20f, 0, .5f, .5f, .20f, 0, 1)),
    // Soft: airy, ethereal, long tails
    P("Heaven Lift", 1, B_HEAVEN, G()),
    P("Soft Smile", 1, B_SOFT, G()),
    P("Cloud Nine", 1, B_CLOUD, G(.20f, .70f, .45f, .20f, .10f)),
    P("Halo", 1, B_HALO, G(.15f, .60f, .35f, .15f, .10f)),
    P("Angel Dust", 1, B_ANGEL, G(.30f, .25f, .70f, .35f, .35f)),
    P("Rising Echoes", 1, B_ECHOES, G()),
    // Granular: the room at work
    P("Grain Storm", 2, B_STORM, G(.30f, .45f, .70f, .60f, .40f, 0, .5f, .5f, .40f)),
    P("Room Bounce", 2, B_CLOUD, G(.60f, .50f, .55f, .80f, .50f)),
    P("Shatter", 2, B_ANGEL, G(.70f, .30f, .60f, .90f, 1.0f)),
    P("Snow Globe", 2, B_HEAVEN, G(.60f, .25f, .60f, .30f, .20f)),
    P("Confetti", 2, B_BLOOM, G(.80f, .15f, .85f, .70f, .50f, 0, .5f, .5f, .30f)),
    P("Swarm", 2, B_LIFTOFF, G(.50f, .35f, .90f, 1.0f, .30f, 0, .5f, .5f, .60f)),
    // Retrace: grains that go back in time
    P("Time Slip", 3, B_CLOUD, G(.40f, .50f, .50f, .30f, .10f, .60f, .40f, .50f)),
    P("Rewind", 3, B_HALO, G(.40f, .50f, .50f, .20f, 0, .70f, .30f, .60f, 0, 1)),
    P("Echo Back", 3, B_ECHOES, G(.30f, .50f, .50f, .10f, 0, .50f, .55f, .30f)),
    P("Backspin Rise", 3, B_TAPE, G(.30f, .50f, .50f, .20f, 0, .80f, .50f, .40f, 0, 1)),
    P("Deja Vu", 3, B_ANGEL, G(.50f, .40f, .55f, .30f, .30f, .50f, .45f, .45f, .20f)),
    // Synced: grains on the song's beat grid
    P("Pulse Cloud", 4, B_CLOUD, G(.50f, .40f, .55f, .20f, .10f, 0, .5f, .5f, 0, 0, 1)),
    P("Ticker", 4, B_STORM, G(.40f, .20f, .75f, .30f, 0, 0, .5f, .5f, .10f, 0, 1)),
    P("Stutter Rise", 4, B_BOUNCY, G(.40f, .15f, .80f, .20f, 0, .30f, .20f, .10f, 0, 0, 1)),
    P("Loose Grid", 4, B_SOFT, G(.40f, .40f, .50f, .30f, .15f, 0, .5f, .5f, .60f, 0, 1)),
    P("Half Time Haze", 4, B_HALO, G(.50f, .80f, .30f, .10f, .20f, 0, .5f, .5f, .20f, 0, 1)),
    // Medium: resonant, pumping, tape
    P("Sunrise", 5, B_SUNRISE, G(.10f, .50f, .50f, .10f, .10f)),
    P("Big Smile", 5, B_BIGSMILE, G(.10f, .50f, .55f, .20f, .10f)),
    P("Bouncy Smile", 5, B_BOUNCY, G()),
    P("Tape Rise", 5, B_TAPE, G(.10f, .50f, .50f, .10f)),
    // Hard: driven, resonant, fast
    P("Hard Grin", 6, B_GRIN, G()),
    P("Barber Pole", 6, B_BARBER, G(.10f, .40f, .60f, .40f, .20f)),
    P("Face Melter", 6, B_MELTER, G(.30f, .35f, .80f, .70f, .40f, .20f, .40f, .30f, .30f)),
    P("Liftoff", 6, B_LIFTOFF, G(.20f, .45f, .65f, .50f, .20f)),
    P("Endless Wow", 6, B_ENDLESS, G(.30f, .50f, .60f, .50f, .30f, .30f, .50f, .50f, .20f)),
};

int wowFirstPreset(int cat) {
    for (int i = 0; i < kNumPresets; ++i) if (kPresets[i].cat == cat) return i;
    return 0;
}

// ---- Hilbert pair ---------------------------------------------------------------------------------------------------
static const float kHilA[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
static const float kHilB[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};

void WowDSP::Hilbert::reset() {
    memset(x1, 0, sizeof x1); memset(x2, 0, sizeof x2); memset(y1, 0, sizeof y1); memset(y2, 0, sizeof y2);
    delayed = 0;
}

void WowDSP::Hilbert::tick(float x, float &re, float &im) {
    float a = x, b = x;
    for (int i = 0; i < 4; ++i) {   // path A in slots 0..3, path B in 4..7: y = c^2 (x + y[-2]) - x[-2]
        const float ca = kHilA[i] * kHilA[i], cb = kHilB[i] * kHilB[i];
        float ya = ca * (a + y2[i]) - x2[i];
        x2[i] = x1[i]; x1[i] = a; y2[i] = y1[i]; y1[i] = flush(ya); a = ya;
        float yb = cb * (b + y2[i + 4]) - x2[i + 4];
        x2[i + 4] = x1[i + 4]; x1[i + 4] = b; y2[i + 4] = y1[i + 4]; y1[i + 4] = flush(yb); b = yb;
    }
    re = delayed;   // path A one sample late = the in-phase part
    delayed = flush(a);
    im = b;
}

// ---- setup -------------------------------------------------------------------------------------------------------
static const int kFdnLens[WowDSP::kFdnLines] = {1999, 2137, 2269, 2399, 2549, 2687, 2833, 2971};   // 45-67 ms, primes

void WowDSP::init() {
    // the engine's memory comes from malloc: nothing here is zero until we say so
    memset(delayBufL, 0, sizeof delayBufL);
    memset(delayBufR, 0, sizeof delayBufR);
    memset(gBufL, 0, sizeof gBufL);
    memset(gBufR, 0, sizeof gBufR);
    memset(fdn, 0, sizeof fdn);
    memset(resoBuf, 0, sizeof resoBuf);
    for (int c = 0; c < 2; ++c) {
        hpf[c].reset(); tiltHp[c].reset(); cloudTilt[c].reset(); airBp[c].reset(); hfHp[c].reset(); resoHp[c].reset();
        loopHp[c].reset(); loopLp[c].reset(); hil[c].reset(); shimHp[c].reset(); shimLp[c].reset();
        vSendHp[c].reset(); vRetHp[c].reset();
        memset(shim[c].buf, 0, sizeof shim[c].buf);
        shim[c].w = 0; shim[c].ph = 0;
        for (int i = 0; i < 4; ++i) apState[c][i] = 0;
        phFb[c] = 0; hfEnv[c] = 0; shimFeed[c] = 0;
    }
    for (int i = 0; i < kFdnLines; ++i) fdnLp[i] = 0;
    for (int i = 0; i < kMaxGrains; ++i) grains[i] = Grain();
    for (int i = 0; i <= 1024; ++i) { const float s = sinf(kPi * i / 1024.0f); hann[i] = s * s; }
    st = &kStyles[0];
    wowTarget = wow = 0; mixTarget = mix = 1;
    ppq = -1; bpm = 120; playing = false; freePhase = 0;
    phLfo = 0; pumpGain = 1; pumpExp = 0; rng = 0x12345678u;
    resoPos = 0; gPos = 0; spawnAcc = 0;
    gc = GrainCtl(); levelS = 0; lvlS = 1; roomV = 0; syncK = 0; syncJit = 0; syncPrev = 0; syncN = 0; syncSeated = false;
    dPos = 0; dTimeL = 0.75f * 60.0f / 120.0f * kFs; dTimeR = 0.5f * 60.0f / 120.0f * kFs;
    shPh = 0; fdnPos = 0; decay = 2.5f; fdnLfo = 0; limGain = 1;
    lvlHpIn.reset(); lvlHpCore.reset(); lvlIn = lvlCore = 0; match = 1;
}

void WowDSP::setStyle(int s) { st = &kStyles[s < 0 ? 0 : s >= kNumStyles ? kNumStyles - 1 : s]; }

void WowDSP::setGrainCtl(const GrainCtl &g) {
    gc.level = clampf(g.level, 0, 1); gc.amount = clampf(g.amount, 0, 1); gc.size = clampf(g.size, 0, 1); gc.density = clampf(g.density, 0, 1);
    gc.speed = clampf(g.speed, 0, 1); gc.split = clampf(g.split, 0, 1); gc.retrace = clampf(g.retrace, 0, 1);
    gc.retTime = clampf(g.retTime, 0, 1); gc.retLen = clampf(g.retLen, 0, 1); gc.variation = clampf(g.variation, 0, 1);
    gc.retReverse = g.retReverse; gc.sync = g.sync;
}

void WowDSP::setSongPos(double p, double b, bool play) {
    ppq = p;
    if (b > 20 && b < 400) bpm = b;
    playing = play && p >= 0;
}

float WowDSP::noiseSample() {
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

// pitch-shift by `ratio` with two taps sliding through a 2048-sample (46 ms) window
float WowDSP::shift(Shifter &s, float x, float ratio) {
    const float W = 2048.0f;
    s.buf[s.w] = x;
    s.ph -= (ratio - 1.0f) / W;   // ratio > 1: the delay shrinks, the pitch goes up
    s.ph -= floorf(s.ph);
    float out = 0;
    for (int k = 0; k < 2; ++k) {
        float p = s.ph + 0.5f * k;
        if (p >= 1.0f) p -= 1.0f;
        const float d = 2.0f + p * W;
        float r = (float)s.w - d;
        if (r < 0) r += kShiftLen;
        const int ri = (int)r;   // r can round up to exactly kShiftLen: wrap the index, keep the fraction
        const float fr = r - ri;
        const int i0 = ri & (kShiftLen - 1);
        const int i1 = (i0 + 1) & (kShiftLen - 1);
        const float v = s.buf[i0] + (s.buf[i1] - s.buf[i0]) * fr;
        out += v * hann[(int)(p * 1024.0f)];
    }
    s.w = (s.w + 1) & (kShiftLen - 1);
    return out;
}

int WowDSP::allocGrain() {
    for (int i = 0; i < kMaxGrains; ++i) if (!grains[i].on) return i;
    return -1;
}

void WowDSP::setPan(Grain &g) {
    g.gl = cosf(g.x * 0.5f * kPi) * g.amp;
    g.gr = sinf(g.x * 0.5f * kPi) * g.amp;
}

// sizeScale: the GRAINS tab's Size (1 = the preset's grain length); syncLen > 0: the grain is that many samples long (Sync)
void WowDSP::spawnGrain(float w, float sizeScale, float syncLen) {
    const int slot = allocGrain();
    if (slot < 0) return;
    Grain &g = grains[slot];
    const float lenR = 0.75f + 0.5f * rand01();
    if (syncLen > 0.0f) g.len = (int)(syncLen * (0.9f + 0.4f * (lenR - 0.75f)));
    else g.len = (int)(st->grainMs * 0.001f * kFs * lenR * sizeScale);
    if (g.len < 256) g.len = 256;
    float semi = st->riseSemi * w + (rand01() - 0.5f) * 0.2f;   // a few cents either way keeps a cloud alive
    if (rand01() < st->octaveProb) semi += 12.0f;
    const float ratio = exp2f(semi / 12.0f);
    const bool back = rand01() < st->reverseProb;
    g.inc = back ? -ratio : ratio;
    // how far back to start: never catch up with the write head while playing forwards and up
    float D = 64.0f + st->spray * kFs * rand01();
    if (!back && ratio > 1.0f) D += g.len * (ratio - 1.0f);
    const float maxD = (float)kGrainLen - g.len * ratio - 64.0f;
    if (D > maxD) D = maxD;
    double p = (double)gPos - D;
    if (p < 0) p += kGrainLen;
    g.pos = p;
    g.age = 0;
    const float pan = 0.5f + (rand01() - 0.5f) * st->spread;
    g.x = pan; g.vx = 0; g.amp = 1; g.cool = 0; g.gen = 0; g.retAt = -1;
    g.gl = cosf(pan * 0.5f * kPi);
    g.gr = sinf(pan * 0.5f * kPi);
    // the room and the retrace: more random draws only when those controls are on, so the 1.1 sound stays the same
    if (roomV > 0.0f) g.vx = (rand01() < 0.5f ? -1.0f : 1.0f) * roomV * (0.6f + 0.8f * rand01());
    if (gc.retrace > 0.0f && rand01() < gc.retrace) g.retAt = (int)(g.len * (0.3f + 0.4f * rand01()));
    g.on = true;
}

// Grains move through the room (x = the pan position, 0..1). A wall or another grain bounces them; a bounce may split a grain.
void WowDSP::stepRoom(float dt) {
    for (int k = 0; k < kMaxGrains; ++k) {
        Grain &g = grains[k];
        if (!g.on) continue;
        if (g.cool > 0) --g.cool;
        if (g.vx == 0.0f) continue;
        g.x += g.vx * dt;
        bool hit = false;
        if (g.x < 0.0f) { g.x = -g.x; g.vx = -g.vx; hit = true; }
        else if (g.x > 1.0f) { g.x = 2.0f - g.x; g.vx = -g.vx; hit = true; }
        g.x = clampf(g.x, 0.0f, 1.0f);
        setPan(g);
        if (hit && g.cool == 0) onBounce(k);
    }
    for (int a = 0; a < kMaxGrains; ++a) {      // grains meeting in the room: they swap speeds (an elastic bounce)
        Grain &ga = grains[a];
        if (!ga.on || ga.vx == 0.0f || ga.cool > 0) continue;
        for (int b = a + 1; b < kMaxGrains; ++b) {
            Grain &gb = grains[b];
            if (!gb.on || gb.vx == 0.0f || gb.cool > 0) continue;
            const float dx = ga.x - gb.x;
            if (fabsf(dx) < 0.05f && dx * (ga.vx - gb.vx) < 0.0f) {
                const float v = ga.vx; ga.vx = gb.vx; gb.vx = v;
                onBounce(a);
                onBounce(b);
                break;
            }
        }
    }
}

void WowDSP::onBounce(int k) {
    Grain &g = grains[k];
    g.cool = 24;   // about 8 ms: one bounce, one chance to split
    if (gc.split > 0.0f && g.gen < 2 && g.age + 256 < g.len && rand01() < gc.split) splitGrain(k);
}

// A split: a child grain from where the parent is, at a pretty interval (octave, fifth, octave + fifth, octave down, two
// octaves), shorter when it is higher, a little quieter, heading the other way round the room.
void WowDSP::splitGrain(int k) {
    const int slot = allocGrain();
    if (slot < 0) return;
    Grain &p = grains[k];
    Grain &c = grains[slot];
    static const float kSemi[5] = {12.0f, 19.0f, 7.0f, -12.0f, 24.0f};
    static const float kCum[5] = {0.38f, 0.60f, 0.78f, 0.92f, 1.0f};
    const float r = rand01();
    int pick = 0;
    while (pick < 4 && r > kCum[pick]) ++pick;
    const float semi = kSemi[pick];
    const float ratio = exp2f(semi / 12.0f);
    float lenScale = semi >= 19.0f ? 0.4f : semi > 0.0f ? 0.55f : 0.9f;
    float len = (float)p.len * lenScale;
    c.inc = p.inc * ratio;
    if (c.inc > 1.0f) {   // forwards and up: never catch up with the write head
        const float D = (float)((gPos - (int)p.pos) & (kGrainLen - 1));
        if (len * (c.inc - 1.0f) + 64.0f > D) len = (D - 64.0f) / (c.inc - 1.0f);
    }
    if (len < 256.0f) return;
    c.len = (int)len;
    c.pos = p.pos;
    c.age = 0;
    c.x = p.x; c.vx = -p.vx; c.amp = semi > 0.0f ? 0.7f : 0.85f; c.cool = 24; c.gen = (unsigned char)(p.gen + 1); c.retAt = -1;
    setPan(c);
    c.on = true;
}

// A retrace: a copy of the grain starts `Time` earlier in the track and plays for `Length`: forwards (a jump back in time)
// or backwards (reversed). Its Hann window overlaps the parent's, so it is a smooth cross-fade, never a click.
void WowDSP::retraceGrain(int k) {
    const int slot = allocGrain();
    if (slot < 0) return;
    Grain &p = grains[k];
    Grain &c = grains[slot];
    const float T = 0.05f * powf(40.0f, gc.retTime) * kFs;
    float L = 0.04f * powf(25.0f, gc.retLen) * kFs;
    const float ratio = fabsf(p.inc);
    const float D = (float)((gPos - (int)p.pos) & (kGrainLen - 1));   // how far behind the write head the parent reads
    float Dc = D + T;
    const float maxD = (float)kGrainLen - 64.0f - L * ratio;
    if (Dc > maxD) Dc = maxD;
    const bool fwd = gc.retReverse ? p.inc < 0.0f : p.inc >= 0.0f;   // Reverse turns the direction round
    if (fwd && ratio > 1.0f && Dc < L * (ratio - 1.0f) + 64.0f) L = (Dc - 64.0f) / (ratio - 1.0f);
    if (L < 256.0f || Dc < 64.0f) return;
    c.len = (int)L;
    double pos = (double)gPos - Dc;
    if (pos < 0) pos += kGrainLen;
    c.pos = pos;
    c.inc = fwd ? ratio : -ratio;
    c.age = 0;
    c.x = p.x; c.vx = p.vx; c.amp = 0.9f; c.cool = 24; c.gen = 2; c.retAt = -1;
    setPan(c);
    c.on = true;
}

// ---- audio ---------------------------------------------------------------------------------------------------------
void WowDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n) {
    const float wowCoef = 1.0f - expf(-1.0f / (0.03f * kFs));    // 30 ms: no zipper noise on a fast turn
    const float timeCoef = 1.0f - expf(-1.0f / (0.25f * kFs));   // delay times glide on a tempo change
    const float pumpCoef = 1.0f - expf(-1.0f / (0.0015f * kFs));
    const float limRel = 1.0f - expf(-1.0f / (0.12f * kFs));
    const float envAtt = 1.0f - expf(-1.0f / (0.002f * kFs)), envRel = 1.0f - expf(-1.0f / (0.06f * kFs));
    const float limCeil = 0.955f;   // -0.4 dBFS
    const float lvlCoef = 1.0f - expf(-1.0f / (0.4f * kFs));   // the auto gain's ears: 0.4 s
    const float lvg = tanf(kPi * 300.0f / kFs);
    const double beatsPerSample = bpm / 60.0 / kFs;
    const float beatLen = 60.0f / (float)bpm * kFs;
    const float dTargetL = clampf(st->dlyBeatsL * beatLen, 64.0f, (float)(kDelayLen - 8));
    const float dTargetR = clampf(st->dlyBeatsR * beatLen, 64.0f, (float)(kDelayLen - 8));

    static const int kSub = 16;   // coefficients are worked out every 16 samples
    for (int s0 = 0; s0 < n; s0 += kSub) {
        const int m = n - s0 < kSub ? n - s0 : kSub;
        // ---- per sub-block: everything that follows Wow ----
        for (int i = 0; i < m; ++i) { wow = smooth(wow, wowTarget, wowCoef); mix = smooth(mix, mixTarget, wowCoef); }
        if (fabsf(wow - wowTarget) < 1e-5f) wow = wowTarget;
        if (fabsf(mix - mixTarget) < 1e-5f) mix = mixTarget;
        const float w = wow;
        const float engage = clampf(w * 25.0f, 0.0f, 1.0f);   // the core fades in over the first 4 %

        const float fc = 10.0f * powf(st->hpfTo / 10.0f, powf(w, st->hpfCurve));
        const float hg = tanf(kPi * clampf(fc, 10.0f, 18000.0f) / kFs);
        const float hk = 1.0f / lerp(0.7071f, st->hpfQ, w * w * w);   // resonance comes late, once the cutoff is up high
        const float tiltA = dbToLin(st->tiltDb * powf(w, 1.2f)) - 1.0f;
        const float tg = tanf(kPi * 2500.0f / kFs);

        const float driveDb = st->driveDb * powf(w, 1.5f);
        const float driveG = dbToLin(driveDb), driveMix = clampf(driveDb / 3.0f, 0.0f, 1.0f);
        const float driveOut = 1.0f / sqrtf(driveG);

        const float phMix = st->phaser * powf(w, 1.2f) * 0.5f;
        const float phRate = lerp(st->phRate0, st->phRate1, w);
        phLfo += phRate * m / kFs;
        phLfo -= floor(phLfo);
        float phC[2];
        for (int c = 0; c < 2; ++c) {
            const float lfo = 0.5f - 0.5f * cosf(2.0f * kPi * (float)(phLfo + 0.25 * c));
            const float f = 250.0f * powf(14.0f, lfo);   // 250 Hz .. 3.5 kHz
            const float t = tanf(kPi * f / kFs);
            phC[c] = (t - 1.0f) / (t + 1.0f);
        }

        const float pumpDepth = st->pump * powf(w, 1.2f);
        // whole note values: 1/4, 1/8, 1/16 .. like a snare roll. A little hysteresis, so a knob resting right on a
        // boundary (or a jittery Q-Link) doesn't flip the pump between two speeds every block.
        const float pumpTarget2 = log2f(st->pumpBeats0) + w * log2f(st->pumpBeats1 / st->pumpBeats0);
        if (fabsf(pumpTarget2 - pumpExp) > 0.6f) pumpExp = roundf(pumpTarget2);
        const float pumpBeats = exp2f(pumpExp);

        // air: noise, band-passed and rising, as loud as the track's own highs are
        const float airAmt = st->air * powf(w, 1.8f) * 4.0f;
        const float ag = tanf(kPi * (2000.0f * powf(5.0f, w)) / kFs), ak = 0.8f;   // 2 kHz -> 10 kHz
        const float hfg = tanf(kPi * 4000.0f / kFs);

        // the rising resonance: a comb the track rings, its pitch climbing
        const float resoAmt = st->reso * powf(w, 1.8f) * 0.5f;
        const float rf = st->resoLo * powf(st->resoHi / st->resoLo, powf(w, 1.3f));
        const float resoT = clampf(kFs / rf, 8.0f, (float)(kResoLen - 4));
        const float resoFb = 0.8f + 0.1f * w;   // rings with the track, doesn't sustain on its own
        const float rhg = tanf(kPi * 150.0f / kFs);

        const float side = 1.0f + st->width * w;
        // level: the core is matched to the dry track above the bass (auto gain, so the build-up doesn't sag when the
        // high-pass takes the body away, on any material), then the preset's lift goes on top and the dry cut comes off
        const float matchTarget = clampf(sqrtf((lvlIn + 1e-9f) / (lvlCore + 1e-9f)), 0.5f, 4.0f);   // -6 .. +12 dB
        match += (matchTarget - match) * 0.01f;   // per 16 samples: about 0.15 s
        const float makeup = lerp(1.0f, match, engage) * dbToLin(st->makeupDb * w - st->dryCutDb * w * w);

        const float dSend = st->dly * powf(w, 1.3f);
        const float dFb = clampf(st->dlyFb * powf(w, 0.6f), 0.0f, 0.9f);
        const float shiftHz = st->shiftHz * w;
        const float lhg = tanf(kPi * 250.0f / kFs), llg = tanf(kPi * 7000.0f / kFs);

        // granular cloud (1.2: the GRANULES page's Amount adds to the preset's cloud, Level is the cloud's gain; Size and Density scale its grains)
        levelS += (gc.amount - levelS) * 0.05f;
        if (fabsf(levelS - gc.amount) < 1e-5f) levelS = gc.amount;
        const float lvlT = 2.0f * gc.level;
        lvlS += (lvlT - lvlS) * 0.05f;
        if (fabsf(lvlS - lvlT) < 1e-5f) lvlS = lvlT;
        const float sizeS = exp2f((gc.size - 0.5f) * 4.0f);      // x0.25 .. x4
        const float densS = exp2f((gc.density - 0.5f) * 4.0f);
        roomV = gc.speed > 0.0f ? 0.4f * powf(30.0f, gc.speed) : (gc.split > 0.0f ? 0.5f : 0.0f);   // room widths per second
        const float cloudAmt = (st->cloud + levelS * 1.2f) * lvlS * powf(w, 1.3f);
        const float density = lerp(st->density0, st->density1, w) * densS;
        float overlap = density * st->grainMs * 0.001f * sizeS;
        if (gc.sync) overlap = clampf(overlap, 0.6f, 4.0f);
        // splits and retraces add grains: ease the level so the cloud stays about as loud
        const float cloudComp = 1.0f / sqrtf(1.0f + 0.6f * gc.split + 0.4f * gc.retrace);
        const float cloudGain = cloudAmt * 1.4f / sqrtf(overlap > 1.0f ? overlap : 1.0f) * cloudComp;
        // Sync: grain onsets on a grid of 2^n slots per beat, the one nearest the free-running density
        const float beatLenS = 60.0f / (float)bpm * kFs;
        float syncNow = 0.0f;
        if (gc.sync && cloudAmt > 0.0f) syncNow = clampf(exp2f(roundf(log2f(clampf(density, 0.1f, 400.0f) * 60.0f / (float)bpm))), 0.25f, 32.0f);
        stepRoom(m / kFs);

        // shimmer reverb: decay grows with Wow; the input is scaled down as the decay grows, so a held note doesn't pile
        // up without end (power ~ (1 - g^2)^0.8: steady sound gets a little louder, not 30 dB louder)
        const float vSend = st->verb * powf(w, 1.2f);
        const float decayTarget = st->decay0 * powf(st->decay1 / st->decay0, powf(w, 1.5f));
        decay += (decayTarget - decay) * 0.02f;   // per 16 samples: ~18 ms
        float g[kFdnLines], gsum = 0;
        for (int i = 0; i < kFdnLines; ++i) { g[i] = powf(10.0f, -3.0f * kFdnLens[i] / (decay * kFs)); gsum += g[i]; }
        const float gavg = gsum / kFdnLines;
        const float vIn = 0.5f * powf(1.0f - gavg * gavg, 0.4f);
        const float vDamp = 1.0f - expf(-2.0f * kPi * st->verbDamp / kFs);
        // 1.2 bright sweeps: the send's high-pass climbs, the return gets a high shelf
        const bool vHp = st->verbHpTo > 0.0f;
        const float vhg = vHp ? tanf(kPi * clampf(20.0f * powf(st->verbHpTo / 20.0f, powf(w, 1.3f)), 20.0f, 18000.0f) / kFs) : 0.0f;
        const float vTiltA = dbToLin(st->verbTiltDb * powf(w, 1.2f)) - 1.0f;
        const float vtg = tanf(kPi * 3500.0f / kFs);
        const float shimFb = st->shimmer * (0.4f + 0.6f * w);
        const float shimRatio = exp2f(st->shimmerSemi / 12.0f);
        const float shg = tanf(kPi * 300.0f / kFs), slg = tanf(kPi * clampf(st->verbDamp, 1000.0f, 16000.0f) / kFs);
        fdnLfo += 0.4 * m / kFs;
        fdnLfo -= floor(fdnLfo);
        float fdnMod[kFdnLines];
        for (int i = 0; i < kFdnLines; ++i) fdnMod[i] = 6.0f + 5.0f * sinf(2.0f * kPi * (float)(fdnLfo + 0.125 * i) * (1.0f + 0.1f * i));

        for (int j = 0; j < m; ++j) {
            const int i = s0 + j;
            const float xL = inL[i], xR = inR[i];
            const float x[2] = {xL, xR};

            // pump: a duck at the start of every pumpBeats, synced to the song when it plays
            double beat;
            if (playing) beat = ppq + (s0 + j) * beatsPerSample;
            else { freePhase += beatsPerSample; if (freePhase > 4096.0) freePhase -= 4096.0; beat = freePhase; }
            double ph = beat / pumpBeats;
            ph -= floor(ph);
            const float duck = (float)(1.0 - ph);
            const float pumpTarget = 1.0f - pumpDepth * duck * duck;
            pumpGain += (pumpTarget - pumpGain) * pumpCoef;

            // the cloud's memory of the input
            gBufL[gPos] = xL;
            gBufR[gPos] = xR;

            float core[2];
            for (int c = 0; c < 2; ++c) {
                float y = x[c];
                if (driveMix > 0.0f) y = lerp(y, tanhf(y * driveG) * driveOut, driveMix);
                float bp, lp;
                y = hpf[c].tick(y, hg, hk, bp, lp);
                if (tiltA != 0.0f) y += tiltA * tiltHp[c].tick(y, tg, 1.4142f, bp, lp);
                if (phMix > 0.0f) {   // 4 first-order allpasses with a little feedback
                    float p = y + phFb[c] * 0.4f;
                    for (int k = 0; k < 4; ++k) {
                        const float o = phC[c] * p + apState[c][k];
                        apState[c][k] = flush(p - phC[c] * o);
                        p = o;
                    }
                    phFb[c] = p;
                    y = y * (1.0f - phMix) + p * phMix;
                }
                if (airAmt > 0.0f) {
                    const float hf = hfHp[c].tick(x[c], hfg, 1.4142f, bp, lp);
                    const float a = fabsf(hf);
                    hfEnv[c] = flush(hfEnv[c] + (a - hfEnv[c]) * (a > hfEnv[c] ? envAtt : envRel));
                    float nbp, nlp;
                    airBp[c].tick(noiseSample(), ag, ak, nbp, nlp);
                    y += nbp * airAmt * hfEnv[c];
                }
                core[c] = y;
            }
            if (resoAmt > 0.0f) {   // y[n] = x[n] + fb * y[n - T]; we keep only the ringing (y - x)
                float rd = (float)resoPos - resoT;
                if (rd < 0) rd += kResoLen;
                for (int c = 0; c < 2; ++c) {
                    float bp, lp;
                    const float in = resoHp[c].tick(core[c], rhg, 1.4142f, bp, lp) * 0.3f;
                    const float rdc = c ? rd - 0.003f * resoT : rd;   // R a hair higher: it beats slowly, wide
                    float rr = rdc < 0 ? rdc + kResoLen : rdc;
                    const int ri = (int)rr;   // rr can round up to exactly kResoLen: wrap the index
                    const float fr = rr - ri;
                    const int r0 = ri & (kResoLen - 1);
                    const int r1 = (r0 + 1) & (kResoLen - 1);
                    const float past = resoBuf[c][r0] + (resoBuf[c][r1] - resoBuf[c][r0]) * fr;
                    const float yv = softclip(in + resoFb * past);
                    resoBuf[c][resoPos] = flush(yv);
                    core[c] += (yv - in) * resoAmt;
                }
                resoPos = (resoPos + 1) & (kResoLen - 1);
            }
            // what the auto gain listens to: input and core above 300 Hz (before width and makeup)
            {
                float bp, lp;
                const float a = lvlHpIn.tick(xL + xR, lvg, 1.4142f, bp, lp);
                const float b = lvlHpCore.tick(core[0] + core[1], lvg, 1.4142f, bp, lp);
                lvlIn = flush(lvlIn + (a * a - lvlIn) * lvlCoef);
                lvlCore = flush(lvlCore + (b * b - lvlCore) * lvlCoef);
            }
            // width, makeup, dry cut
            {
                const float mid = 0.5f * (core[0] + core[1]), sd = 0.5f * (core[0] - core[1]) * side;
                core[0] = (mid + sd) * makeup;
                core[1] = (mid - sd) * makeup;
            }

            // ---- granular cloud ----
            float cl[2] = {0, 0};
            if (cloudAmt > 0.0f) {
                if (gc.sync) {
                    // on the grid: a slot every 1/syncNow beat; Variation moves each hit off it by up to half a slot
                    const double per = 1.0 / syncNow;
                    if (!syncSeated || syncNow != syncN || beat < syncPrev - 1e-9 || beat - syncPrev > 0.25) {
                        syncN = syncNow;
                        syncK = (long long)floor(beat * syncN) + 1;
                        syncJit = gc.variation > 0.0f ? (double)gc.variation * ((double)rand01() - 0.5) * per : 0.0;
                        syncSeated = true;
                    }
                    syncPrev = beat;
                    for (int guard = 0; guard < 4 && beat >= (double)syncK * per + syncJit; ++guard) {
                        spawnGrain(w, sizeS, beatLenS / syncN * (overlap > 0.6f ? overlap : 0.6f));
                        ++syncK;
                        syncJit = gc.variation > 0.0f ? (double)gc.variation * ((double)rand01() - 0.5) * per : 0.0;
                    }
                } else {
                    syncSeated = false;
                    spawnAcc += density / kFs;
                    while (spawnAcc >= 1.0f) {
                        spawnAcc -= 1.0f;
                        if (gc.variation > 0.0f) spawnAcc -= gc.variation * 1.6f * (rand01() - 0.5f);   // uneven spacing, same average rate
                        if (rand01() < 0.85f) spawnGrain(w, sizeS, 0.0f);   // a little irregular, so it doesn't buzz at the density rate
                    }
                }
            }
            for (int k = 0; k < kMaxGrains; ++k) {
                Grain &gr = grains[k];
                if (!gr.on) continue;
                const int gi = (int)gr.pos;
                const float fr = (float)(gr.pos - gi);
                const int i0 = gi & (kGrainLen - 1);
                const int i1 = (i0 + 1) & (kGrainLen - 1);
                const float v = 0.5f * (gBufL[i0] + (gBufL[i1] - gBufL[i0]) * fr + gBufR[i0] + (gBufR[i1] - gBufR[i0]) * fr);
                if (gr.retAt >= 0 && gr.age >= gr.retAt) { gr.retAt = -1; retraceGrain(k); }
                const float env = hann[(gr.age * 1024) / gr.len];
                cl[0] += v * env * gr.gl;
                cl[1] += v * env * gr.gr;
                gr.pos += gr.inc;
                if (gr.pos >= kGrainLen) gr.pos -= kGrainLen;
                if (gr.pos < 0) gr.pos += kGrainLen;
                if (++gr.age >= gr.len) gr.on = false;
            }
            cl[0] *= cloudGain;
            cl[1] *= cloudGain;
            if (tiltA != 0.0f && cloudAmt > 0.0f) {   // the cloud gets the same bright lift as the track
                float bp, lp;
                for (int c = 0; c < 2; ++c) cl[c] += tiltA * cloudTilt[c].tick(cl[c], tg, 1.4142f, bp, lp);
            }
            gPos = (gPos + 1) & (kGrainLen - 1);

            // ---- stereo delay: cross-fed, filtered, and frequency-shifted on every pass ----
            dTimeL += (dTargetL - dTimeL) * timeCoef;
            dTimeR += (dTargetR - dTimeR) * timeCoef;
            float echo[2];
            {
                const float dt[2] = {dTimeL, dTimeR};
                const float *buf[2] = {delayBufL, delayBufR};
                for (int c = 0; c < 2; ++c) {
                    float rd = (float)dPos - dt[c];
                    if (rd < 0) rd += kDelayLen;
                    const int ri = (int)rd;   // a tiny negative rd + kDelayLen rounds to exactly kDelayLen in float
                    const float fr = rd - ri;
                    const int r0 = ri & (kDelayLen - 1);
                    const int r1 = (r0 + 1) & (kDelayLen - 1);
                    echo[c] = buf[c][r0] + (buf[c][r1] - buf[c][r0]) * fr;
                }
            }
            float loop[2] = {echo[1], echo[0]};   // cross feed: ping-pong
            float cs = 1.0f, sn = 0.0f;
            if (shiftHz > 0.0f) {
                shPh += shiftHz / kFs;
                if (shPh >= 1.0) shPh -= 1.0;
                cs = cosf(2.0f * kPi * (float)shPh);
                sn = sinf(2.0f * kPi * (float)shPh);
            }
            for (int c = 0; c < 2; ++c) {
                float bp, lp, v = loop[c];
                v = loopHp[c].tick(v, lhg, 1.4142f, bp, lp);
                loopLp[c].tick(v, llg, 1.4142f, bp, lp);
                v = lp;
                if (shiftHz > 0.0f) { float re, im; hil[c].tick(v, re, im); v = re * cs - im * sn; }
                loop[c] = softclip(v * dFb);
            }
            delayBufL[dPos] = flush(core[0] * dSend + loop[0]);
            delayBufR[dPos] = flush(core[1] * dSend + loop[1]);
            dPos = (dPos + 1) & (kDelayLen - 1);

            // ---- shimmer reverb: 8 modulated lines, damped, Hadamard-mixed ----
            float verb[2];
            {
                float tap[kFdnLines];
                for (int k = 0; k < kFdnLines; ++k) {
                    float rd = (float)fdnPos - (float)kFdnLens[k] - fdnMod[k];
                    if (rd < 0) rd += kFdnLen;
                    const int ri = (int)rd;   // a tiny negative rd + kFdnLen rounds to exactly kFdnLen in float
                    const float fr = rd - ri;
                    const int r0 = ri & (kFdnLen - 1);
                    const int r1 = (r0 + 1) & (kFdnLen - 1);
                    const float v = fdn[k][r0] + (fdn[k][r1] - fdn[k][r0]) * fr;
                    fdnLp[k] = flush(fdnLp[k] + (v - fdnLp[k]) * vDamp);
                    tap[k] = fdnLp[k];
                }
                verb[0] = 0.5f * (tap[0] - tap[2] + tap[4] - tap[6]);
                verb[1] = 0.5f * (tap[1] - tap[3] + tap[5] - tap[7]);
                float h[kFdnLines];
                for (int k = 0; k < kFdnLines; ++k) h[k] = tap[k] * g[k];
                for (int len = 1; len < kFdnLines; len <<= 1)   // fast Hadamard transform
                    for (int a = 0; a < kFdnLines; a += 2 * len)
                        for (int b = a; b < a + len; ++b) {
                            const float u = h[b], v = h[b + len];
                            h[b] = u + v;
                            h[b + len] = u - v;
                        }
                float sL = core[0] * vSend + echo[0] * 0.25f * vSend + cl[0] * 0.6f;
                float sR = core[1] * vSend + echo[1] * 0.25f * vSend + cl[1] * 0.6f;
                if (vHp) {   // a thinner and brighter wash as Wow climbs
                    float bp, lp;
                    sL = vSendHp[0].tick(sL, vhg, 1.4142f, bp, lp);
                    sR = vSendHp[1].tick(sR, vhg, 1.4142f, bp, lp);
                }
                const float sendL = sL * vIn + shimFeed[0];
                const float sendR = sR * vIn + shimFeed[1];
                for (int k = 0; k < kFdnLines; ++k) {
                    const float in = (k & 1) ? ((k & 2) ? -sendR : sendR) : ((k & 2) ? -sendL : sendL);
                    fdn[k][fdnPos] = flush(h[k] * 0.35355339f + in);
                }
                fdnPos = (fdnPos + 1) & (kFdnLen - 1);
                // the shimmer: the reverb's own sound an octave (or the preset's interval) up, back into the reverb
                for (int c = 0; c < 2; ++c) {
                    if (shimFb > 0.0f) {
                        float bp, lp;
                        float v = shift(shim[c], verb[c], shimRatio);
                        v = shimHp[c].tick(v, shg, 1.4142f, bp, lp);
                        shimLp[c].tick(v, slg, 1.4142f, bp, lp);
                        shimFeed[c] = flush(softclip(lp * shimFb * 2.0f) * vIn);   // scaled like the input: stable at any decay
                    } else {
                        shimFeed[c] = 0.0f;
                    }
                }
                verb[0] *= 1.6f;
                verb[1] *= 1.6f;
                if (vTiltA != 0.0f) {
                    float bp, lp;
                    for (int c = 0; c < 2; ++c) verb[c] += vTiltA * vRetHp[c].tick(verb[c], vtg, 1.4142f, bp, lp);
                }
            }

            // ---- out: dry / (core + cloud + tails), all through the pump, then the limiter ----
            float o[2];
            for (int c = 0; c < 2; ++c) {
                const float wet = pumpGain * (engage * core[c] + cl[c] + echo[c] + verb[c]) + (1.0f - engage) * x[c];
                o[c] = x[c] + mix * (wet - x[c]);
            }
            // limiter: instant down, slow back up. Its ceiling is -0.4 dBFS or the dry signal's own peak, whichever is
            // higher, so it never touches the dry signal (a 0 dBFS track at Wow 0 stays bit for bit). The way back up
            // takes a minimum step, or the last fraction of a dB would round away and the gain would never reach 1.
            const float pk = fabsf(o[0]) > fabsf(o[1]) ? fabsf(o[0]) : fabsf(o[1]);
            const float dryPk = fabsf(xL) > fabsf(xR) ? fabsf(xL) : fabsf(xR);
            const float top = limCeil > dryPk ? limCeil : dryPk;
            float gl = limGain;
            if (gl < 1.0f) {
                const float up = (1.0f - gl) * limRel;
                gl += up > 2e-6f ? up : 2e-6f;
                if (gl > 1.0f) gl = 1.0f;
            }
            if (pk * gl > top) gl = top / pk;   // checked after the release step, so the output never passes the ceiling
            limGain = gl;
#ifdef POLIWOW_PROBE
            probe.core += engage * engage * (core[0] * core[0] + core[1] * core[1]);
            probe.cloud += cl[0] * cl[0] + cl[1] * cl[1];
            probe.echo += echo[0] * echo[0] + echo[1] * echo[1];
            probe.verb += verb[0] * verb[0] + verb[1] * verb[1];
            probe.lim += gl;
            probe.limited += gl < 0.97f;
            ++probe.n;
#endif
            outL[i] = o[0] * gl;
            outR[i] = o[1] * gl;
        }
    }
    if (playing) ppq += n * beatsPerSample;

    // Safety net: if anything ever went non-finite (it shouldn't: the inputs are checked), don't stay silent or
    // blast noise forever. Pass that block's input through and start the effect state fresh. Bit tests, because
    // -ffast-math lets the compiler assume isnan() is always false.
    bool bad = !finiteBits(limGain) || !finiteBits(dTimeL) || !finiteBits(dTimeR) || !finiteBits(pumpGain) || !finiteBits(decay) || !finiteBits(levelS) || !finiteBits(lvlS);
    for (int i = 0; i < n && !bad; ++i) bad = !finiteBits(outL[i]) || !finiteBits(outR[i]);
    if (bad) {
        for (int i = 0; i < n; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
        const WowStyle *keepSt = st;
        const float keepW = wowTarget, keepM = mixTarget;
        const double keepBpm = bpm;
        const GrainCtl keepGc = gc;
        init();
        st = keepSt;
        gc = keepGc;
        wowTarget = wow = finiteBits(keepW) ? keepW : 0.0f;
        mixTarget = mix = finiteBits(keepM) ? keepM : 1.0f;
        bpm = finiteBits((float)keepBpm) && keepBpm > 20 && keepBpm < 400 ? keepBpm : 120.0;
        ++resets;
    }
}
