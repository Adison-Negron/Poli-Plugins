// Poliwow demo renders: a 124 bpm loop with space in it (drums, chord stabs, a sung-style lead), so tails, pads and
// shimmer can be heard filling the gaps. One WAV per preset into the folder given:
//   16 bars: Wow 0 -> 100 % over bars 1-12, held for bars 13-14, then the drop (Wow 0) for bars 15-16.
//   render_demos <dir>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include "WowDSP.h"

static const int kFs = 44100;
static const double kBpm = 124.0;

static void writeWav(const std::string &path, const std::vector<int16_t> &s) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return; }
    uint32_t data = (uint32_t)(s.size() * 2), riff = 36 + data, sr = kFs, br = kFs * 4, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s.data(), 2, s.size(), f);
    fclose(f);
}

static double mtof(double m) { return 440.0 * pow(2.0, (m - 69) / 12.0); }

// the loop: Am - F - C - G, a stab on 1, the "and" of 2 and 4; kick on every beat, clap on 2 and 4, 16th hats;
// a lead line over two bars
static void makeLoop(std::vector<float> &L, std::vector<float> &R, int frames) {
    L.assign(frames, 0); R.assign(frames, 0);
    const double spb = 60.0 / kBpm * kFs;
    const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {48, 52, 55}, {55, 59, 62}};
    const int lead[16] = {76, -1, 72, 74, 76, -1, 79, 76, 74, -1, 72, 71, 72, -1, -1, -1};   // quarter notes, 2 bars
    const double stabAt[3] = {0.0, 1.5, 3.0};
    uint32_t r = 7;
    double hp = 0, prev = 0;
    for (int i = 0; i < frames; ++i) {
        const double beat = i / spb, t = (double)i / kFs;
        const int bar = (int)(beat / 4);
        const double inBar = beat - bar * 4;
        double l = 0, rr = 0;
        // kick
        { double tb = (beat - floor(beat)) * spb / kFs;
          double k = sin(2 * M_PI * (48 + 110 * exp(-tb * 32)) * tb) * exp(-tb * 8) * 0.5; l += k; rr += k; }
        // clap on 2 and 4
        { double cb = fmod(beat + 3.0, 2.0); if (cb < 0.5) { double tc = cb * spb / kFs;
          r = r * 1664525u + 1013904223u; double n = (int32_t)r / 2147483648.0;
          hp = 0.92 * (hp + n - prev); prev = n; double c = hp * exp(-tc * 22) * 0.22; l += c; rr += c * 0.9; } }
        // 16th hats
        { double hb = fmod(beat * 4, 1.0) * spb / 4 / kFs; r = r * 1664525u + 1013904223u;
          double h = ((int32_t)r / 2147483648.0) * exp(-hb * 90) * 0.05; l += h * 0.7; rr += h; }
        // chord stabs (saw-ish, 6 harmonics), with a short decay: gaps between them
        for (double at : stabAt) {
            double ts = (inBar - at) * spb / kFs;
            if (ts < 0 || ts > 0.6) continue;
            double env = (1 - exp(-ts * 300)) * exp(-ts * 7);
            for (int k = 0; k < 3; ++k) {
                double f = mtof(chords[bar % 4][k]), v = 0;
                for (int hN = 1; hN <= 6; ++hN) v += sin(2 * M_PI * f * hN * t * (1 + 0.0015 * k)) / hN;
                l += v * env * 0.045 * (k == 0 ? 1.2 : 0.8); rr += v * env * 0.045 * (k == 2 ? 1.2 : 0.8);
            }
        }
        // lead: a sung-ish line, sine + harmonics, gentle vibrato
        { double lb = fmod(beat, 8.0); int idx = (int)lb; int m = lead[idx % 16 < 16 ? idx % 16 : 0];
          if (idx < 16 && m >= 0) { double tn = (lb - idx) * spb / kFs;
            double env = (1 - exp(-tn * 60)) * exp(-tn * 2.2), f = mtof(m) * (1 + 0.006 * sin(2 * M_PI * 5.5 * t));
            double v = sin(2 * M_PI * f * t) + 0.35 * sin(4 * M_PI * f * t) + 0.12 * sin(6 * M_PI * f * t);
            l += v * env * 0.07; rr += v * env * 0.07; } }
        L[i] = (float)l; R[i] = (float)rr;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: render_demos <dir>\n"); return 1; }
    const std::string dir = argv[1];
    const double spb = 60.0 / kBpm * kFs;
    const int frames = (int)(16 * 4 * spb);
    std::vector<float> L, R;
    makeLoop(L, R, frames);
    std::vector<int16_t> dry(2 * frames);
    for (int i = 0; i < frames; ++i) {
        dry[2 * i] = (int16_t)lrintf(fmaxf(-32768, fminf(32767, L[i] * 32768)));
        dry[2 * i + 1] = (int16_t)lrintf(fmaxf(-32768, fminf(32767, R[i] * 32768)));
    }
    writeWav(dir + "/00-dry-loop.wav", dry);
    const int upEnd = (int)(12 * 4 * spb), dropAt = (int)(14 * 4 * spb);
    std::vector<float> oL(frames), oR(frames);
    for (int s = 0; s < kNumPresets; ++s) {
        WowDSP *d = new WowDSP();
        d->init(); d->setStyle(kPresets[s].base); d->setGrainCtl(kPresets[s].g); d->setMix(1);   // the preset as picked: base sound + its Granules settings
        for (int i = 0; i < frames; i += 128) {
            const int n = std::min(128, frames - i);
            d->setWow(i < upEnd ? (float)i / upEnd : i < dropAt ? 1.0f : 0.0f);
            d->setSongPos(i / spb, kBpm, true);
            d->process(&L[i], &R[i], &oL[i], &oR[i], n);
        }
        std::vector<int16_t> out(2 * frames);
        for (int i = 0; i < frames; ++i) {
            out[2 * i] = (int16_t)lrintf(fmaxf(-32768, fminf(32767, oL[i] * 32768)));
            out[2 * i + 1] = (int16_t)lrintf(fmaxf(-32768, fminf(32767, oR[i] * 32768)));
        }
        std::string name = kPresets[s].name;
        for (auto &c : name) if (c == ' ') c = '-';
        char num[8];
        snprintf(num, sizeof num, "%02d-", s + 1);
        writeWav(dir + "/" + num + name + ".wav", out);
        delete d;
    }
    printf("wrote %d build-ups + the dry loop to %s\n", kNumPresets, dir.c_str());
    return 0;
}
