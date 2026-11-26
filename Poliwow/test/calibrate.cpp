// Poliwow preset balance (test build with -DPOLIWOW_PROBE): plays the dsp_test drum loop at a fixed Wow per preset and
// prints how loud each part of the wet signal is, against the dry loop, and how often the limiter works.
//   calibrate [wow 0-100]      (default 100)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include "WowDSP.h"
static const int kFs = 44100;
static const double kBpm = 128.0;
int main(int argc, char **argv) {
    const float w = (argc > 1 ? atof(argv[1]) : 100.0) / 100.0;
    const int frames = (int)(8 * 4 * 60.0 / kBpm * kFs);
    std::vector<float> L(frames), R(frames);
    uint32_t r = 1;
    const double spb = 60.0 / kBpm * kFs;
    for (int i = 0; i < frames; ++i) {   // same loop as dsp_test: kick, offbeat hat, a chord pad
        double beat = i / spb, ph = beat - floor(beat), tb = ph * spb / kFs;
        double kick = sin(2 * M_PI * (50 + 120 * exp(-tb * 30)) * tb) * exp(-tb * 7) * 0.55;
        double ho = fmod(beat + 0.5, 1.0) * spb / kFs;
        r = r * 1664525u + 1013904223u;
        double hat = ((int32_t)r / 2147483648.0) * exp(-ho * 60) * 0.12, t = (double)i / kFs, pad = 0;
        const double notes[3] = {220.0, 277.18, 329.63};
        for (double f : notes) pad += sin(2 * M_PI * f * t) * 0.06 + sin(2 * M_PI * f * 2.003 * t) * 0.02;
        L[i] = (float)(kick + hat + pad); R[i] = (float)(kick + hat * 0.8 + pad * 0.9);
    }
    double dry = 0, dryMid = 0, m1 = 0, mp = 0, am = exp(-2 * M_PI * 300.0 / kFs);
    for (int i = 0; i < frames; ++i) { dry += L[i] * L[i] + R[i] * R[i]; m1 = am * (m1 + L[i] - mp); mp = L[i]; dryMid += 2 * m1 * m1; }
    printf("Wow %.0f %%: parts in dB against the dry loop (above 300 Hz for 'out>300')\n", w * 100);
    printf("%-14s %7s %7s %7s %7s %8s %9s %8s\n", "preset", "core", "cloud", "echo", "verb", "out>300", "lim avg", "limited");
    for (int s = 0; s < kNumStyles; ++s) {
        WowDSP *d = new WowDSP();
        d->init(); d->setStyle(s); d->setWow(w); d->setMix(1);
        std::vector<float> oL(frames), oR(frames);
        for (int pass = 0; pass < 2; ++pass) {   // the first pass fills the reverb, the second is measured
            d->probe = WowDSP::Probe();
            for (int i = 0; i < frames; i += 128) {
                d->setSongPos(i / spb, kBpm, true);
                int n = std::min(128, frames - i);
                d->process(&L[i], &R[i], &oL[i], &oR[i], n);
            }
        }
        double om = 0; m1 = 0; mp = 0;
        for (int i = 0; i < frames; ++i) { m1 = am * (m1 + oL[i] - mp); mp = oL[i]; om += 2 * m1 * m1; }
        auto db = [&](double e) { return 10 * log10(e / dry + 1e-12); };
        const WowDSP::Probe &p = d->probe;
        printf("%-14s %7.1f %7.1f %7.1f %7.1f %8.1f %8.2fdB %7.1f%%\n", kStyles[s].name, db(p.core), db(p.cloud), db(p.echo),
               db(p.verb), 10 * log10(om / dryMid), 20 * log10(p.lim / p.n), 100.0 * p.limited / p.n);
        delete d;
    }
}
