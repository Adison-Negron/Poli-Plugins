// juice_cli: run the DSP on a raw float32 mono file.  juice_cli in.raw out.raw [key=value ...]   (keys in PHYSICAL units)
//   pitch=+5 (st) formant=+2 (st) ftrack=50 link=1 thresh=-30 lowend=150 (Hz, 0=off) attack=3 release=40 odd=50 even=50 color=1000
//   f1c=500 f1q=3 (0=off) f2c= f2q= f3c= f3q= fsmooth=30 air=0 natural=40 mix=100
//   learn=1 (listen like the Learn button, print what it would set on stdout)
//   lat=1 (0 Fast 384, 1 Balanced 512, 2 Smooth 768 samples)  hold=60 (ms) rasp=50 glide=50 guard=60 morph=40 (ms) floor=70 (Hz) ceil=700 (Hz) overlap=30 (%)   trace=file (time voiced f0 rho per block)
#include "JuiceDSP.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
using namespace jb;
static float logPos(float v, float lo, float hi) { return 1000.0f * logf(v / lo) / logf(hi / lo); }
int main(int argc, char **argv) {
    if (argc < 3) return 1;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    std::vector<float> in; float buf[4096]; size_t n;
    while ((n = fread(buf, 4, 4096, f)) > 0) in.insert(in.end(), buf, buf + n);
    fclose(f);
    JuiceDSP d; d.init(44100.0f);
    const char *tracePath = nullptr;
    bool learn = false;
    for (int i = 2; i < argc; ++i) {
        char key[32]; float v;
        if (!strncmp(argv[i], "trace=", 6)) { tracePath = argv[i] + 6; continue; }
        if (sscanf(argv[i], "%31[^=]=%f", key, &v) != 2) continue;
        auto set = [&](int id, float p) { d.setParam(id, p); };
        if (!strcmp(key, "pitch")) set(P_PITCH, 500 + v / 0.024f);
        else if (!strcmp(key, "formant")) set(P_FORMANT, 500 + v / 0.024f);
        else if (!strcmp(key, "ftrack")) set(P_FTRACK, v * 10);
        else if (!strcmp(key, "link")) d.setLink(v > 0.5f);
        else if (!strcmp(key, "thresh")) set(P_THRESH, (v + 60) / 0.06f);
        else if (!strcmp(key, "lowend")) set(P_LOWEND, v <= 0 ? 0 : logPos(v, 80, 350));
        else if (!strcmp(key, "attack")) set(P_ATTACK, logPos(v, 0.5f, 20));
        else if (!strcmp(key, "release")) set(P_RELEASE, logPos(v, 10, 200));
        else if (!strcmp(key, "odd")) set(P_ODD, v * 10);
        else if (!strcmp(key, "even")) set(P_EVEN, v * 10);
        else if (!strcmp(key, "color")) set(P_COLOR, logPos(v, 200, 4000));
        else if (!strcmp(key, "f1c")) set(P_F1C, logPos(v, 200, 1000));
        else if (!strcmp(key, "f2c")) set(P_F2C, logPos(v, 1000, 3000));
        else if (!strcmp(key, "f3c")) set(P_F3C, logPos(v, 2500, 4500));
        else if (!strcmp(key, "f1q")) set(P_F1Q, v <= 0 ? 0 : 6 + 994 * logf(v / 0.7f) / logf(20.0f));
        else if (!strcmp(key, "f2q")) set(P_F2Q, v <= 0 ? 0 : 6 + 994 * logf(v / 0.7f) / logf(20.0f));
        else if (!strcmp(key, "f3q")) set(P_F3Q, v <= 0 ? 0 : 6 + 994 * logf(v / 0.7f) / logf(20.0f));
        else if (!strcmp(key, "fsmooth")) set(P_FSMOOTH, v * 10);
        else if (!strcmp(key, "air")) set(P_AIR, v * 10);
        else if (!strcmp(key, "natural")) set(P_NATURAL, v * 10);
        else if (!strcmp(key, "mix")) set(P_MIX, v * 10);
        else if (!strcmp(key, "hold")) set(P_HOLD, v / 0.15f);
        else if (!strcmp(key, "rasp")) set(P_RASP, v * 10);
        else if (!strcmp(key, "glide")) set(P_GLIDE, v * 10);
        else if (!strcmp(key, "guard")) set(P_GUARD, v * 10);
        else if (!strcmp(key, "morph")) set(P_MORPH, logPos(v, 5, 200));
        else if (!strcmp(key, "floor")) set(P_FLOOR, logPos(v, 70, 200));
        else if (!strcmp(key, "ceil")) set(P_CEIL, logPos(v, 250, 920));
        else if (!strcmp(key, "overlap")) set(P_OVERLAP, v * 10);
        else if (!strcmp(key, "learn")) learn = v > 0.5f;
        else if (!strcmp(key, "lat")) d.setLatencyMode((int)v);   // 0 Fast (384), 1 Balanced (512, default), 2 Smooth (768)
        else fprintf(stderr, "unknown key %s\n", key);
    }
    d.init(44100.0f);   // init() keeps the positions, resets the smoothers to them
    if (learn) d.startLearn();
    auto report = [&](const char *why) {
        float p[P_COUNT]; bool st[P_COUNT];
        const bool ok = d.learnResult(p, st);
        printf("LEARN %s progress=%.2f enough=%d", why, (double)d.learnProgress(), ok ? 1 : 0);
        if (ok) {
            if (st[P_FLOOR]) printf(" floor=%.0f", (double)JuiceMap::floorHz(p[P_FLOOR]));
            if (st[P_CEIL]) printf(" ceil=%.0f", (double)JuiceMap::ceilHz(p[P_CEIL]));
            if (st[P_RASP]) printf(" rasp=%.0f", (double)(p[P_RASP] * 0.1f));
            if (st[P_HOLD]) printf(" hold=%.0f", (double)JuiceMap::holdMs(p[P_HOLD]));
            if (st[P_GLIDE]) printf(" glide=%.0f", (double)(p[P_GLIDE] * 0.1f));
            if (st[P_GUARD]) printf(" guard=%.0f", (double)(p[P_GUARD] * 0.1f));
            if (st[P_MORPH]) printf(" morph=%.0f", (double)JuiceMap::morphMs(p[P_MORPH]));
            if (st[P_OVERLAP]) printf(" overlap=%.0f", (double)(p[P_OVERLAP] * 0.1f));
            if (st[P_THRESH]) printf(" thresh=%.1f", (double)JuiceMap::thresholdDb(p[P_THRESH]));
        }
        printf("\n");
        d.stopLearn();
    };
    std::vector<float> out(in.size());
    std::vector<float> oR(in.size());
    FILE *tr = tracePath ? fopen(tracePath, "w") : nullptr;
    for (size_t i = 0; i < in.size(); i += 128) {
        int c = (int)std::min<size_t>(128, in.size() - i);
        d.process(&in[i], &in[i], &out[i], &oR[i], c);
        if (d.learning() && d.learnFinished()) report("finished");
        if (tr) fprintf(tr, "%.5f %d %.2f %.4f\n", (double)(i + c) / 44100.0, d.voicedState(), (double)d.pitchHz(), (double)d.rhoEffective());
    }
    if (d.learning()) report("end of file");
    if (tr) fclose(tr);
    f = fopen(argv[2], "wb"); fwrite(out.data(), 4, out.size(), f); fclose(f);
    return 0;
}
