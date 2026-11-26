// prints the engine's display values after a Learn on hiss + a 3 kHz whine + 50 Hz hum, while a voice plays (for test/compose_preview.py)
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "engine.h"
}
static uint32_t rng = 5;
static float g() { float s = 0; for (int i = 0; i < 4; ++i) { rng = rng * 1664525u + 1013904223u; s += ((rng >> 8) / 16777216.0f) * 2 - 1; } return s * 0.866f; }
int main(int argc, char **argv) {
    const mpc_engine_t *E = mpc_engine();
    void *s = E->create(nullptr);
    int16_t in[256], out[256];
    long t = 0;
    auto blk = [&](bool voice) {
        for (int i = 0; i < 128; ++i, ++t) {
            double v = 0.004 * g() + 0.01 * sin(2 * M_PI * 3000 * t / 44100.0) + 0.01 * sin(2 * M_PI * 50 * t / 44100.0);
            if (voice) for (int h = 1; h <= 10; ++h) v += 0.1 / h * sin(2 * M_PI * 180 * h * t / 44100.0);
            in[2 * i] = in[2 * i + 1] = (int16_t)lrint(v * 32767);
        }
        E->process(s, in, out, 128);
    };
    E->set_param(s, "learn", "1");
    for (int b = 0; b < 1100; ++b) blk(false);
    if (argc > 1) E->set_param(s, "threshold", argv[1]);
    for (int b = 0; b < 60; ++b) blk(true);
    char buf[64], k[16];
    printf("{");
    for (int i = 1; i <= 6; ++i) { snprintf(k, sizeof k, "n%d", i); E->get_param(s, k, buf, 64); printf("\"%s\":%s,", k, buf); snprintf(k, sizeof k, "n%dx", i); E->get_param(s, k, buf, 64); printf("\"%s\":%s,", k, buf); }
    for (int j = 1; j <= 32; ++j) { snprintf(k, sizeof k, "sp%02d", j); E->get_param(s, k, buf, 64); printf("\"%s\":%s,", k, buf); }
    E->get_param(s, "threshold", buf, 64); printf("\"threshold\":%s,", buf);
    E->get_param(s, "reduction", buf, 64); printf("\"reduction\":%s}\n", buf);
    E->destroy(s);
}
