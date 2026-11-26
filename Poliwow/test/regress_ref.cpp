// Regression: renders every preset the 1.1.1 had through WowDSP with a synthetic loop and writes the float samples to a file.
// Built twice (the 1.1.1 sources and the current ones) and compared with cmp: with the GRAINS tab at its defaults the
// two must be identical, bit for bit.   usage: regress_ref <out.raw> [presets]
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <vector>
#include "WowDSP.h"
int main(int argc, char **argv) {
    const int nPre = argc > 2 ? atoi(argv[2]) : 16;
    FILE *f = fopen(argv[1], "wb");
    static WowDSP dsp;
    uint32_t r = 1;
    auto rnd = [&]() { r = r * 1664525u + 1013904223u; return (float)(int32_t)r / 2147483648.0f; };
    const int N = 44100 * 10, B = 128;
    std::vector<float> inL(B), inR(B), oL(B), oR(B);
    for (int s = 0; s < nPre; ++s) {
        dsp.init(); dsp.setStyle(s); dsp.setMix(1.0f);
        for (int i = 0; i < N; i += B) {
            const float w = i < 44100 * 8 ? (float)i / (44100 * 8) : 1.0f;
            dsp.setWow(w);
            for (int j = 0; j < B; ++j) {
                const int t = i + j;
                const float env = ((t / 5512) % 4 == 0) ? 1.0f : 0.25f;
                inL[j] = 0.25f * env * sinf(6.2831853f * 220.0f * t / 44100.0f) + 0.1f * rnd() * ((t / 11025) % 2);
                inR[j] = 0.25f * env * sinf(6.2831853f * 331.0f * t / 44100.0f) + 0.1f * rnd() * ((t / 11025) % 2);
            }
            dsp.process(inL.data(), inR.data(), oL.data(), oR.data(), B);
            fwrite(oL.data(), 4, B, f); fwrite(oR.data(), 4, B, f);
        }
    }
    fclose(f);
    return 0;
}
