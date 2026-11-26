// Poli-Multiband preset tool: runs MultibandDSP (built with -DMB_STATS) over a WAV with one setting and prints what the
// compressor did, band by band, as one JSON line. presets/tune.py calls it to set thresholds and makeup from targets.
//
//   preset_tool <in.wav> <input trim dB> "<x1>,<x2>|<band0>|<band1>|<band2>" [out.wav]
//   band = in,out,thresh,attack ms,release ms,knee,ratio,up thresh,up ratio
//
// Per band, over the samples where the band is active: above -45 dB for the downward numbers (so pauses and noise
// don't count), above -60 dB for the upward ones (the upward stage works on the quiet parts):
//   down_mean / _p90 / _p97 average, 90th and 97th percentile of the downward gain reduction (dB, positive)
//   up_mean / up_p90       the same for the upward boost
// Whole signal: rms_in / rms_out (dB), peak_in / peak_out (dBFS), lim_max (the most the safety ceiling took off, dB),
// lim_frac (the share of samples where it took off more than 0.5 dB).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <string>
#include "MultibandDSP.h"

static bool readWav(const char *path, std::vector<float> &L, std::vector<float> &R) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.size() < 44 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) return false;
    size_t p = 12;
    int ch = 0, bits = 0;
    while (p + 8 <= d.size()) {
        uint32_t sz;
        memcpy(&sz, &d[p + 4], 4);
        if (!memcmp(&d[p], "fmt ", 4)) { ch = d[p + 10] | d[p + 11] << 8; bits = d[p + 22] | d[p + 23] << 8; }
        if (!memcmp(&d[p], "data", 4)) {
            if (bits != 16 || ch < 1) return false;
            size_t frames = std::min<size_t>(sz, d.size() - p - 8) / (2 * ch);
            L.resize(frames); R.resize(frames);
            const int16_t *s = (const int16_t *)&d[p + 8];
            for (size_t i = 0; i < frames; ++i) {
                L[i] = s[i * ch] / 32768.0f;
                R[i] = s[i * ch + (ch > 1)] / 32768.0f;
            }
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

static void writeWav(const char *path, const std::vector<float> &L, const std::vector<float> &R) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t frames = L.size(), data = frames * 4, riff = 36 + data, fmt = 16, rate = 44100, brate = rate * 4;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t s[2] = {(int16_t)lrintf(std::max(-32768.0f, std::min(32767.0f, L[i] * 32767.0f))),
                        (int16_t)lrintf(std::max(-32768.0f, std::min(32767.0f, R[i] * 32767.0f)))};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

static float pct(std::vector<float> &v, float q) {
    if (v.empty()) return 0.0f;
    size_t k = std::min(v.size() - 1, (size_t)(q * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: preset_tool in.wav trim_db spec [out.wav]\n"); return 2; }
    std::vector<float> L, R;
    if (!readWav(argv[1], L, R)) { fprintf(stderr, "can't read %s (16-bit WAV)\n", argv[1]); return 1; }
    const float trim = powf(10.0f, (float)atof(argv[2]) / 20.0f);
    for (size_t i = 0; i < L.size(); ++i) { L[i] *= trim; R[i] *= trim; }

    float x1, x2;
    BandSettings bs[3];
    std::string spec = argv[3];
    std::vector<std::string> parts;
    for (size_t a = 0, b; a <= spec.size(); a = b + 1) {
        b = spec.find('|', a);
        if (b == std::string::npos) b = spec.size();
        parts.push_back(spec.substr(a, b - a));
    }
    if (parts.size() != 4 || sscanf(parts[0].c_str(), "%f,%f", &x1, &x2) != 2) { fprintf(stderr, "bad spec\n"); return 2; }
    for (int b = 0; b < 3; ++b) {
        BandSettings &s = bs[b];
        if (sscanf(parts[b + 1].c_str(), "%f,%f,%f,%f,%f,%f,%f,%f,%f", &s.inputDb, &s.outputDb, &s.threshDb, &s.attackMs,
                   &s.releaseMs, &s.kneeDb, &s.ratio, &s.upThreshDb, &s.upRatio) != 9) { fprintf(stderr, "bad band\n"); return 2; }
        s.upOutputDb = 0;
    }
    MultibandDSP dsp;
    dsp.setCrossovers(x1, x2);
    for (int b = 0; b < 3; ++b) dsp.setBand(b, bs[b]);
    dsp.init(44100.0f);

    std::vector<float> oL(L.size()), oR(L.size());
    std::vector<float> down[3], up[3];
    double eIn = 0, eOut = 0;
    long nAct = 0;
    float pkIn = 0, pkOut = 0, limMin = 1.0f;
    long limN = 0;   // samples where the safety ceiling takes off more than 0.5 dB
    const int B = 128;
    float bl[B], br[B], ol[B], orr[B];
    for (size_t i0 = 0; i0 < L.size(); i0 += B) {
        // coefficients per 128-frame block as on the device; stats per sample (one sample at a time)
        int n = (int)std::min<size_t>(B, L.size() - i0);
        for (int i = 0; i < n; ++i) {
            bl[0] = L[i0 + i]; br[0] = R[i0 + i];
            dsp.process(bl, br, ol, orr, 1);
            oL[i0 + i] = ol[0]; oR[i0 + i] = orr[0];
            limMin = std::min(limMin, dsp.limitGain());
            if (dsp.limitGain() < 0.944f) ++limN;
            for (int b = 0; b < 3; ++b) {
                const BandState &t = dsp.state(b);
                if (t.lastLevel > -45.0f) down[b].push_back(-t.lastDown);
                if (t.lastLevel > -60.0f) up[b].push_back(t.lastUp);   // the upward stage works on the quiet parts
            }
            const float in = std::max(fabsf(L[i0 + i]), fabsf(R[i0 + i]));
            pkIn = std::max(pkIn, in);
            pkOut = std::max(pkOut, std::max(fabsf(ol[0]), fabsf(orr[0])));
            {
                eIn += 0.5 * (L[i0 + i] * L[i0 + i] + R[i0 + i] * R[i0 + i]);
                eOut += 0.5 * (ol[0] * ol[0] + orr[0] * orr[0]);
                ++nAct;
            }
        }
    }
    printf("{\"rms_in\": %.2f, \"rms_out\": %.2f, \"peak_in\": %.2f, \"peak_out\": %.2f, \"lim_max\": %.2f, \"lim_frac\": %.5f, \"bands\": [",
           10 * log10(eIn / nAct + 1e-20), 10 * log10(eOut / nAct + 1e-20), 20 * log10(pkIn + 1e-9), 20 * log10(pkOut + 1e-9),
           -20 * log10(limMin), (double)limN / L.size());
    for (int b = 0; b < 3; ++b) {
        double sd = 0, su = 0;
        for (float v : down[b]) sd += v;
        for (float v : up[b]) su += v;
        const size_t na = down[b].size(), nu = up[b].size();
        printf("%s{\"active\": %.3f, \"down_mean\": %.2f, \"down_p90\": %.2f, \"down_p97\": %.2f, \"up_mean\": %.2f, \"up_p90\": %.2f}", b ? ", " : "",
               (double)na / L.size(), na ? sd / na : 0.0, pct(down[b], 0.9f), pct(down[b], 0.97f), nu ? su / nu : 0.0, pct(up[b], 0.9f));
    }
    printf("]}\n");
    if (argc > 4) writeWav(argv[4], oL, oR);
    return 0;
}
