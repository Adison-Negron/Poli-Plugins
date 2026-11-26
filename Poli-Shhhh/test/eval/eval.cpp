// Offline evaluation of the gain stage: test/eval/run_eval.py writes noisy/clean/noise as raw float32 mono at 44.1 kHz,
// this runs ShhDSP over the noisy file (Learn on the noise-only lead-in) and "shadow filters" the clean and noise files
// with the very same per-hop gains, so speech distortion and residual noise can be measured separately.
//   eval noisy.f32 clean.f32 noise.f32 outdir learn_seconds reduction threshold [key=value tuning ...]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "ShhDSP.h"
using namespace shh;
static std::vector<float> load(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    std::vector<float> v(n); if (fread(v.data(), 4, n, f) != (size_t)n) exit(1); fclose(f); return v;
}
static void save(const std::string &p, const std::vector<float> &v) { FILE *f = fopen(p.c_str(), "wb"); fwrite(v.data(), 4, v.size(), f); fclose(f); }
struct Shadow {   // the plugin's own STFT, applying given gains
    float hist[kN] = {0}, ola[kHop] = {0}, re[kN], im[kN];
    void hop(const ShhDSP &d, const float *in, float *out) {
        memmove(hist, hist + kHop, sizeof(float) * (kN - kHop));
        memcpy(hist + kN - kHop, in, sizeof(float) * kHop);
        const float *wa = d.analysisWindow(), *ws = d.synthesisWindow(), *g = d.gains();
        for (int n = 0; n < kN; ++n) { re[n] = hist[n] * wa[n]; im[n] = 0; }
        d.fftPublic(re, im);
        for (int k = 0; k < kBins; ++k) { re[k] *= g[k]; im[k] *= g[k]; if (k && k < kN / 2) { re[kN - k] *= g[k]; im[kN - k] *= g[k]; } }
        d.fftPublic(im, re);
        for (int i = 0; i < kHop; ++i) {
            const int n = kN - 2 * kHop + i, n2 = kN - kHop + i;
            out[i] = re[n] * ws[n] / kN + ola[i];
            ola[i] = re[n2] * ws[n2] / kN;
        }
    }
};
int main(int argc, char **argv) {
    if (argc < 8) { fprintf(stderr, "usage\n"); return 1; }
    auto noisy = load(argv[1]), clean = load(argv[2]), noise = load(argv[3]);
    std::string od = argv[4];
    const double learnS = atof(argv[5]);
    static ShhDSP d;   // big: keep it off the stack
    d.init();
    d.setReduction((float)atof(argv[6]));
    d.setThreshold((float)atof(argv[7]));
    for (int a = 8; a < argc; ++a) {
        char k[64]; float v;
        if (sscanf(argv[a], "%63[^=]=%f", k, &v) != 2) continue;
        Tuning &t = d.tuning;
        if (!strcmp(k, "alpha")) t.alpha = v; else if (!strcmp(k, "rule")) t.rule = (int)v;
        else if (!strcmp(k, "tsnr")) t.tsnr = (int)v; else if (!strcmp(k, "presence")) t.presence = (int)v;
        else if (!strcmp(k, "xiH1Db")) t.xiH1Db = v; else if (!strcmp(k, "omlsa")) t.omlsa = (int)v;
        else if (!strcmp(k, "relMs")) t.relMs = v; else if (!strcmp(k, "freqSmooth")) t.freqSmooth = (int)v;
        else if (!strcmp(k, "track")) t.track = (int)v; else if (!strcmp(k, "trackDb")) t.trackDb = v;
        else if (!strcmp(k, "trackMs")) t.trackMs = v; else if (!strcmp(k, "mask")) t.mask = (int)v;
        else if (!strcmp(k, "maskOffDb")) t.maskOffDb = v; else if (!strcmp(k, "wideHf")) t.wideHf = (int)v;
        else if (!strcmp(k, "redScale")) t.redScale = v;
        else { fprintf(stderr, "unknown %s\n", k); return 1; }
    }
    const size_t n = noisy.size() / kHop * kHop;
    std::vector<float> out(n), sc(n), sn(n);
    Shadow shc, shn;
    float L[kHop], o1[kHop], o2[kHop];
    const long learnHops = (long)(learnS * kSR / kHop);
    if (learnHops > 0) d.startLearn();
    for (size_t h = 0; h * kHop < n; ++h) {
        if ((long)h == learnHops) d.stopLearn();
        memcpy(L, &noisy[h * kHop], sizeof L);
        d.process(L, L, o1, o2);
        memcpy(&out[h * kHop], o1, sizeof o1);
        shc.hop(d, &clean[h * kHop], &sc[h * kHop]);
        shn.hop(d, &noise[h * kHop], &sn[h * kHop]);
    }
    save(od + "/out.f32", out); save(od + "/sclean.f32", sc); save(od + "/snoise.f32", sn);
    return 0;
}
