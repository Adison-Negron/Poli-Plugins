// Poli-Saturator DSP tests (offline): clean pass at 0 %, band flatness, harmonics per Type and Crunch, level match, Smash,
// Soften, Low / Mid / High, aliasing, Type switches, clicks while knobs move, extremes, silence, every preset bounded, CPU.
#include "SatDSP.h"
#include "presets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <ctime>
#include <cmath>
#include <cstring>
using namespace sat;
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static const double fs = 44100;
typedef std::vector<float> V;
static void run(SatDSP &d, const V &iL, const V &iR, V &oL, V &oR) {
    size_t n = iL.size(); oL.resize(n); oR.resize(n);
    for (size_t i = 0; i < n; i += 128) { int c = (int)std::min<size_t>(128, n - i); d.process(&iL[i], &iR[i], &oL[i], &oR[i], c); }
}
static bool bounded(const V &a, float lim) { for (float x : a) { if (!(x == x) || x > lim || x < -lim) return false; } return true; }
static float peak(const V &a, size_t from = 0, size_t to = (size_t)-1) { float p = 0; for (size_t i = from; i < a.size() && i < to; i++) p = std::max(p, fabsf(a[i])); return p; }
static double rms(const V &a, size_t from, size_t to) { double s = 0; size_t n = 0; for (size_t i = from; i < to && i < a.size(); i++) { s += (double)a[i] * a[i]; n++; } return n ? sqrt(s / n) : 0; }
static double bin(const V &a, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < a.size(); i++) { double ph = 2 * M_PI * hz * (double)i / fs; re += a[i] * cos(ph); im += a[i] * sin(ph); }
    return 2 * sqrt(re * re + im * im) / (std::min(to, a.size()) - from);
}
static V sine(size_t n, double hz, float amp) { V v(n); for (size_t i = 0; i < n; i++) v[i] = amp * (float)sin(2 * M_PI * hz * i / fs); return v; }
static V noise(size_t n, float level, unsigned seed) {   // pinkish noise scaled to an RMS of `level`
    V v(n); srand(seed); double lp = 0;
    for (auto &x : v) { double g = 0; for (int k = 0; k < 12; k++) g += rand() / (double)RAND_MAX; g -= 6; lp += 0.2 * (g - lp); x = (float)(g * 0.5 + lp * 1.5); }
    double r = 0; for (float x : v) r += x * x; r = sqrt(r / n); for (auto &x : v) x *= level / (float)r; return v;
}
static double db(double x) { return 20 * log10(std::max(x, 1e-12)); }
static double third_diff_peak(const V &a, size_t from) {   // the largest |3rd difference|: a click shows up as a spike
    double p = 0; for (size_t i = from + 3; i < a.size(); i++) p = std::max(p, (double)fabsf(a[i] - 3 * a[i - 1] + 3 * a[i - 2] - a[i - 3])); return p;
}
static void setAll(SatDSP &d, const float *pos, int type) { for (int i = 0; i < P_COUNT; ++i) d.setParam(i, pos[i]); d.setType(type); }

int main() {
    // 1. Saturation 0 %: a clean pass (flat magnitude through the band split and the oversampler)
    {
        SatDSP d; d.setParam(P_SAT, 0); d.init(fs);
        double worst = 0, worstHz = 0;
        for (double hz : {40.0, 100.0, 220.0, 400.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0, 19000.0}) {
            V in = sine(44100, hz, 0.3f), oL, oR; run(d, in, in, oL, oR);
            const double g = db(bin(oL, 22050, 44100, hz) / 0.3);
            if (fabs(g) > fabs(worst)) { worst = g; worstHz = hz; }
        }
        CHECK(fabs(worst) < 0.1, "Saturation 0 %%: flat to %.3f dB across 40 Hz..19 kHz (worst at %.0f Hz)", worst, worstHz);
        V in = noise(44100, 0.2f, 1), oL, oR; run(d, in, in, oL, oR);
        V sil(44100, 0.f); run(d, sil, sil, oL, oR);
        CHECK(peak(oL, 30000) < 2e-5f, "silence in = silence out after the tail (%.2e)", peak(oL, 30000));
    }
    // 2. Harmonics: Tube makes even + odd, the others odd only (symmetric); Crunch moves them
    {
        double h2[4], h3[4];
        for (int t = 0; t < 4; t++) {
            SatDSP d; d.setType(t); d.setParam(P_SAT, 500); d.setParam(P_CRUNCH, 400); d.init(fs);
            V in = sine(88200, 1000, 0.25f), oL, oR; run(d, in, in, oL, oR);
            const double f = bin(oL, 22050, 88200, 1000);
            h2[t] = db(bin(oL, 22050, 88200, 2000) / f); h3[t] = db(bin(oL, 22050, 88200, 3000) / f);
        }
        CHECK(h2[T_TUBE] > -26 && h3[T_TUBE] > -30, "Tube: 2nd %.1f dB, 3rd %.1f dB (both present)", h2[T_TUBE], h3[T_TUBE]);
        CHECK(h2[T_TAPE] > -34, "Tape: 2nd %.1f dB (a little bias, so a little even)", h2[T_TAPE]);
        CHECK(h2[T_CLIP] < -80 && h3[T_CLIP] > -30, "Clip: no 2nd (%.0f dB), 3rd %.1f dB", h2[T_CLIP], h3[T_CLIP]);
        CHECK(h2[T_FOLD] < -80 && h3[T_FOLD] > -20, "Fold: no 2nd (%.0f dB), strong 3rd %.1f dB", h2[T_FOLD], h3[T_FOLD]);
        double e0, e1;
        for (int k = 0; k < 2; k++) {
            SatDSP d; d.setType(T_TUBE); d.setParam(P_SAT, 500); d.setParam(P_CRUNCH, k ? 1000 : 0); d.init(fs);
            V in = sine(88200, 1000, 0.25f), oL, oR; run(d, in, in, oL, oR);
            (k ? e1 : e0) = db(bin(oL, 22050, 88200, 2000) / bin(oL, 22050, 88200, 1000));
        }
        CHECK(e1 > e0 + 10, "Tube Crunch 0 -> 100 %%: 2nd harmonic %.1f -> %.1f dB (bias)", e0, e1);
        // Clip: Crunch hardens the knee: 5th harmonic up
        double k0, k1;
        for (int k = 0; k < 2; k++) {
            SatDSP d; d.setType(T_CLIP); d.setParam(P_SAT, 700); d.setParam(P_CRUNCH, k ? 1000 : 0); d.init(fs);
            V in = sine(88200, 1000, 0.25f), oL, oR; run(d, in, in, oL, oR);
            (k ? k1 : k0) = db(bin(oL, 22050, 88200, 7000) / bin(oL, 22050, 88200, 1000));
        }
        CHECK(k1 > k0 + 6, "Clip Crunch 0 -> 100 %%: 7th harmonic %.1f -> %.1f dB (harder knee)", k0, k1);
    }
    // 3. Level match: pink noise at -18 dBFS RMS stays within +-3 dB from 0 to 100 % in every Type; loud input never exceeds the ceiling
    {
        double lo = 99, hi = -99;
        for (int t = 0; t < 4; t++) for (int sat : {0, 250, 500, 750, 1000}) {
            SatDSP d; d.setType(t); d.setParam(P_SAT, sat); d.init(fs);
            V in = noise(88200, 0.12f, 7), oL, oR; run(d, in, in, oL, oR);
            const double g = db(rms(oL, 22050, 88200) / rms(in, 22050, 88200));
            lo = std::min(lo, g); hi = std::max(hi, g);
        }
        CHECK(lo > -3.0 && hi < 3.0, "level at -18 dBFS RMS: %.1f .. %+.1f dB over Saturation 0-100 %% and all four Types", lo, hi);
        double lo2 = 99, hi2 = -99;
        for (int t = 0; t < 4; t++) for (int sat : {500, 1000}) {
            SatDSP d; d.setType(t); d.setParam(P_SAT, sat); d.init(fs);
            V in = noise(88200, 0.3f, 8), oL, oR; run(d, in, in, oL, oR);
            const double g = db(rms(oL, 22050, 88200) / rms(in, 22050, 88200));
            lo2 = std::min(lo2, g); hi2 = std::max(hi2, g);
        }
        CHECK(lo2 > -5.0 && hi2 < 2.0, "level at -10 dBFS RMS: %.1f .. %+.1f dB", lo2, hi2);
    }
    // 4. Smash: static compression, level-neutral for peaks near -14 dBFS, faster/harder with more Smash
    {
        double gr[5], lev[5]; int i = 0;
        for (int sm : {0, 250, 500, 750, 1000}) {
            SatDSP d; d.setParam(P_SAT, 0); d.setParam(P_SMASH, sm); d.init(fs);
            V in = sine(44100 * 2, 300, 0.4f), oL, oR; run(d, in, in, oL, oR);       // -8 dBFS peaks
            gr[i] = d.grDbNow();
            lev[i++] = db(rms(oL, 44100, 88200) / rms(in, 44100, 88200));
        }
        CHECK(gr[0] == 0 && lev[0] > -0.15 && lev[0] < 0.15, "Smash 0: no gain change (%.2f dB)", lev[0]);
        CHECK(lev[1] < lev[0] + 0.2 && lev[4] < lev[1] && lev[4] < -2.0, "Smash squashes a -8 dBFS sine: %+.1f %+.1f %+.1f %+.1f %+.1f dB", lev[0], lev[1], lev[2], lev[3], lev[4]);
        // dynamic range: a loud and a quiet burst get closer together
        double drIn = 0, drOut[2];
        for (int k = 0; k < 2; k++) {
            SatDSP d; d.setParam(P_SAT, 0); d.setParam(P_SMASH, k ? 800 : 0); d.init(fs);
            V in(44100 * 4); V a = sine(in.size(), 300, 1.0f);
            for (size_t j = 0; j < in.size(); j++) in[j] = a[j] * ((j / 22050) % 2 ? 0.05f : 0.5f);
            V oL, oR; run(d, in, in, oL, oR);
            const double loud = rms(oL, 44100 + 1000, 44100 + 20000), quiet = rms(oL, 22050 + 5000, 44100 - 1000);
            drOut[k] = db(quiet == 0 ? 1e-9 : loud / quiet);
            drIn = db(0.5 / 0.05);
        }
        CHECK(drOut[1] < drOut[0] - 8, "Smash 80 %%: a %.0f dB loud/quiet difference becomes %.1f dB (Smash 0: %.1f)", drIn, drOut[1], drOut[0]);
        // attack: a burst's first 2 ms are not yet fully squashed at Smash 100, 30 ms later they are
        SatDSP d; d.setParam(P_SAT, 0); d.setParam(P_SMASH, 1000); d.init(fs);
        V in(44100, 0.f), a = sine(44100, 500, 0.9f); for (size_t j = 22050; j < 44100; j++) in[j] = a[j];
        V oL, oR; run(d, in, in, oL, oR);
        const double early = peak(oL, 22050, 22050 + 44), late = peak(oL, 22050 + 2000, 22050 + 6000);
        CHECK(early > late * 1.15, "Smash keeps the attack: first peaks %.2f, settled peaks %.2f", early, late);
    }
    // 5. Soften: darkens the added harmonics, not the clean highs
    {
        double hf[3];
        int i = 0;
        for (int so : {0, 500, 1000}) {
            SatDSP d; d.setType(T_CLIP); d.setParam(P_SAT, 800); d.setParam(P_CRUNCH, 800); d.setParam(P_SOFTEN, so); d.init(fs);
            V in = sine(88200, 1000, 0.3f), oL, oR; run(d, in, in, oL, oR);
            hf[i++] = db(bin(oL, 22050, 88200, 9000) / bin(oL, 22050, 88200, 1000));
        }
        CHECK(hf[1] < hf[0] - 6 && hf[2] < hf[1] - 6, "Soften 0/50/100 %%: 9th harmonic %.1f / %.1f / %.1f dB", hf[0], hf[1], hf[2]);
        // a clean 15 kHz sine at low drive keeps its level at full Soften (the dry highs are not touched)
        SatDSP d; d.setParam(P_SAT, 0); d.setParam(P_SOFTEN, 1000); d.init(fs);
        V in = sine(44100, 15000, 0.3f), oL, oR; run(d, in, in, oL, oR);
        CHECK(fabs(db(bin(oL, 22050, 44100, 15000) / 0.3)) < 0.1, "Soften 100 %% with Saturation 0: 15 kHz untouched (%.3f dB)", db(bin(oL, 22050, 44100, 15000) / 0.3));
    }
    // 6. Low / Mid / High: each band takes its share of the saturation; Off leaves it clean
    {
        // a low sine (100 Hz) is shaped by Low only; a 5 kHz sine by High only
        auto thd = [&](double hz, int lo, int mid, int hi) {
            SatDSP d; d.setType(T_CLIP); d.setParam(P_SAT, 700); d.setParam(P_CRUNCH, 600); d.setParam(P_LOW, lo); d.setParam(P_MID, mid); d.setParam(P_HIGH, hi); d.init(fs);
            V in = sine(88200, hz, 0.3f), oL, oR; run(d, in, in, oL, oR);
            return db(bin(oL, 22050, 88200, 3 * hz) / bin(oL, 22050, 88200, hz));
        };
        const double lowOn = thd(100, 500, 0, 0), lowOff = thd(100, 0, 500, 500);
        const double hiOn = thd(2000 * 2, 0, 0, 500), hiOff = thd(2000 * 2, 500, 500, 0);
        CHECK(lowOn > lowOff + 25, "Low band: 100 Hz 3rd harmonic %.1f dB with Low on, %.1f dB with Low Off", lowOn, lowOff);
        CHECK(hiOn > hiOff + 25, "High band: 4 kHz 3rd harmonic %.1f dB with High on, %.1f dB with High Off", hiOn, hiOff);
        const double midOn = thd(800, 0, 500, 0), midOff = thd(800, 500, 0, 500);
        CHECK(midOn > midOff + 20, "Mid band: 800 Hz 3rd harmonic %.1f dB with Mid on, %.1f dB with Mid Off", midOn, midOff);
        const double a = thd(100, 250, 0, 0), b = thd(100, 500, 0, 0), c = thd(100, 1000, 0, 0);
        CHECK(a < b && b < c, "Low knob 25 / 50 / 100 %%: more drive, more harmonics (%.1f %.1f %.1f dB)", a, b, c);
        SatDSP d; d.setParam(P_SAT, 1000); d.setParam(P_LOW, 0); d.setParam(P_MID, 0); d.setParam(P_HIGH, 0); d.init(fs);
        V in = noise(88200, 0.2f, 4), oL, oR; run(d, in, in, oL, oR);
        CHECK(fabs(db(rms(oL, 22050, 88200) / rms(in, 22050, 88200))) < 0.5, "all three bands Off at Saturation 100 %%: level unchanged (%.2f dB)", db(rms(oL, 22050, 88200) / rms(in, 22050, 88200)));
    }
    // 7. Aliasing: 4x oversampling keeps what a 5 kHz sine folds back down low
    {
        for (int t = 0; t < 3; t++) {
            SatDSP d; d.setType(t); d.setParam(P_SAT, 1000); d.setParam(P_CRUNCH, 700); d.init(fs);
            V in = sine(88200, 5000, 0.5f), oL, oR; run(d, in, in, oL, oR);
            double tot = 0; for (size_t i = 22050; i < 88200; i++) tot += oL[i] * oL[i]; tot /= (88200 - 22050);
            double hp = 0; for (int h = 1; h <= 4; h++) { const double a = bin(oL, 22050, 88200, 5000.0 * h); hp += a * a / 2; }
            const double al = 10 * log10(std::max(tot - hp, 1e-20) / tot);
            CHECK(al < -38, "%s at full drive: non-harmonic energy %.1f dB below the total", kTypeName[t], al);
        }
    }
    // 8. Type switch: a crossfade, no click (compared with the tones' own slope)
    {
        for (int to = 1; to < 4; to++) {
            SatDSP d; d.setParam(P_SAT, 500); d.init(fs);
            V in = sine(44100 * 2, 220, 0.4f), oL, oR; run(d, in, in, oL, oR);   // settle on Tube
            V a(22050), b;
            SatDSP e; e.setParam(P_SAT, 500); e.init(fs);
            V ref = sine(44100, 220, 0.4f); V r1, r2; run(e, ref, ref, r1, r2);
            SatDSP g; g.setType(to); g.setParam(P_SAT, 500); g.init(fs);
            V q1, q2; run(g, ref, ref, q1, q2);
            const double base = std::max(third_diff_peak(r1, 22050), third_diff_peak(q1, 22050));   // the louder of the two steady tones
            e.setType(to);
            V o1, o2; run(e, ref, ref, o1, o2);
            const double sw = third_diff_peak(o1, 3);
            CHECK(sw < base * 3.5 + 1e-3 && bounded(o1, 1.0f), "Tube -> %s: switch spike %.4f vs the steady tones' own %.4f", kTypeName[to], sw, base);
        }
    }
    // 9. Knob moves in Q-Link steps do not click (3rd difference while a knob sweeps, vs the same tone with no move)
    {
        const int ids[] = {P_SAT, P_SMASH, P_CRUNCH, P_SOFTEN, P_LOW, P_MID, P_HIGH};
        const char *names[] = {"Saturation", "Smash", "Crunch", "Soften", "Low", "Mid", "High"};
        for (int t = 0; t < 4; t += 3) for (int k = 0; k < 7; k++) {
            SatDSP d, still; d.setType(t); still.setType(t);
            d.setParam(P_SAT, 500); still.setParam(P_SAT, 500); d.init(fs); still.init(fs);
            V in = sine(44100, 330, 0.4f), a1, a2, b1, b2; run(still, in, in, b1, b2);
            double pos = 300;
            if (ids[k] == P_SAT) pos = 300;
            d.setParam(ids[k], pos);
            V in2 = sine(44100 * 2, 330, 0.4f), o1(in2.size()), o2(in2.size());
            for (size_t i = 0; i < in2.size(); i += 128) {
                if (i >= 44100 && ((i / 128) % 4) == 0) { pos = std::min(1000.0, pos + 128.0 / 1000.0 * 1000.0 / 8.0); d.setParam(ids[k], (float)pos); }   // 1/8 of 128 per step: a slow Q-Link turn
                d.process(&in2[i], &in2[i], &o1[i], &o2[i], (int)std::min<size_t>(128, in2.size() - i));
            }
            double settled = 0;   // the roughest steady tone anywhere in the swept range
            for (double at : {300.0, 500.0, 700.0, 1000.0}) {
                SatDSP ref; ref.setType(t); ref.setParam(P_SAT, 500); ref.setParam(ids[k], (float)at); ref.init(fs);
                V r1, r2; run(ref, in2, in2, r1, r2);
                settled = std::max(settled, third_diff_peak(r1, 44100 + 10));
            }
            const double sweep = third_diff_peak(o1, 44100 + 10);
            // a wavefolder's output is FM-like while its drive moves (that is what folding is), so Fold + Saturation gets a wider margin
            const double margin = (t == T_FOLD && ids[k] == P_SAT) ? 8.0 : 1.5;
            CHECK(sweep < settled * margin + 0.01 && bounded(o1, 1.0f), "%s %s sweep: 3rd diff %.4f (roughest steady tone in the range: %.4f)", kTypeName[t], names[k], sweep, settled);
        }
    }
    // 10. Extremes: full-scale square, DC, garbage settings, every combination bounded and finite
    {
        bool allOk = true; double worst = 0;
        srand(11);
        for (int trial = 0; trial < 96; trial++) {
            SatDSP d; d.setType(trial % 4);
            for (int i = 0; i < P_COUNT; i++) d.setParam(i, (float)(rand() % 3 == 0 ? (rand() % 2) * 1000 : rand() % 1001));
            d.init(fs);
            V in(44100), oL, oR;
            for (size_t i = 0; i < in.size(); i++) in[i] = (trial % 3 == 0) ? ((i / 50) % 2 ? 1.0f : -1.0f) : (trial % 3 == 1 ? 0.9f : ((rand() / (float)RAND_MAX) * 2 - 1));
            run(d, in, in, oL, oR);
            allOk = allOk && bounded(oL, 1.0001f) && bounded(oR, 1.0001f);
            worst = std::max(worst, (double)peak(oL));
        }
        CHECK(allOk, "96 random settings x square / DC / full-scale noise: finite, never above the ceiling (peak %.4f)", worst);
        SatDSP d; d.setParam(P_SAT, 1000); d.setParam(P_SMASH, 1000); d.init(fs);
        V dc(44100, 0.8f), oL, oR; run(d, dc, dc, oL, oR);
        CHECK(fabs(oL.back()) < 0.05, "DC in: blocked (%.4f after 1 s)", oL.back());
        for (int i = 0; i < 20; i++) { d.setType(i % 4); }   // rapid type switching
        V n2 = noise(44100, 0.5f, 5); run(d, n2, n2, oL, oR);
        CHECK(bounded(oL, 1.0001f), "20 Type switches in a row: stable");
    }
    // 11. Stereo: identical inputs give identical outputs, independent channels stay independent
    {
        SatDSP d; d.setParam(P_SAT, 700); d.setParam(P_SMASH, 300); d.init(fs);
        V a = noise(44100, 0.2f, 21), oL, oR; run(d, a, a, oL, oR);
        double diff = 0; for (size_t i = 0; i < a.size(); i++) diff = std::max(diff, (double)fabsf(oL[i] - oR[i]));
        CHECK(diff < 1e-6, "L = R in -> L = R out (max difference %.2e)", diff);
        V b = noise(44100, 0.05f, 22), p, q; SatDSP e; e.setParam(P_SAT, 700); e.init(fs); run(e, a, b, p, q);
        CHECK(rms(p, 22050, 44100) > 0.05 && rms(q, 22050, 44100) > 0.01 && rms(q, 22050, 44100) < rms(p, 22050, 44100), "different L / R stay different");
    }
    // 12. Every preset: bounded, loud enough, not much louder than the input
    {
        bool ok = true; double lo = 99, hi = -99; int n = 0;
        V in = noise(44100 * 2, 0.15f, 31), oL, oR;
        for (int i = 0; i < kNumPresets; i++) {
            const SatPreset &p = kPresets[i];
            float pos[P_COUNT]; for (int k = 0; k < P_COUNT; k++) pos[k] = (float)p.pos[k];
            SatDSP d; setAll(d, pos, p.type); d.init(fs);
            run(d, in, in, oL, oR);
            ok = ok && bounded(oL, 1.0001f);
            const double g = db(rms(oL, 44100, 88200) / rms(in, 44100, 88200));
            lo = std::min(lo, g); hi = std::max(hi, g); n++;
        }
        CHECK(ok && lo > -9.0 && hi < 8.0, "%d presets: bounded; level change %.1f .. %+.1f dB on -16 dBFS RMS noise", n, lo, hi);
    }
    // 13. CPU
    {
        double worstX = 1e9; const char *wn = "";
        for (int t = 0; t < 4; t++) {
            SatDSP d; d.setType(t); d.setParam(P_SAT, 1000); d.setParam(P_SMASH, 500); d.init(fs);
            V in = noise(44100 * 6, 0.2f, 5), oL, oR; clock_t c0 = clock(); run(d, in, in, oL, oR);
            const double x = 6.0 / ((double)(clock() - c0) / CLOCKS_PER_SEC);
            if (x < worstX) { worstX = x; wn = kTypeName[t]; }
        }
        printf("info CPU: slowest Type (%s) %.0fx real time at Saturation 100 %% + Smash 50 %%\n", wn, worstX);
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
        printf("info CPU limit skipped (sanitizer build); the optimized build is measured by test/bench.sh\n");
        (void)worstX;
#else
        CHECK(worstX > 15, "CPU: %.0fx real time (stereo, 4x oversampled, three bands)", worstX);
#endif
    }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
