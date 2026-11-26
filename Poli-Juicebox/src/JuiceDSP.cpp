// Poli-Juicebox DSP implementation. See JuiceDSP.h for the signal flow.
//
// The shifter is a TD-PSOLA with its own tracker:
//   * pitch: YIN on a 4x-decimated copy every 64 samples (70 .. 920 Hz), continuity + hysteresis, voiced / unvoiced flag;
//   * analysis marks: one per period, each placed where the waveform best matches the previous period (normalised
//     cross-correlation, so no glottal-epoch detector and no polarity problem); unvoiced stretches get a mark every 5 ms;
//   * synthesis: a Hann grain per synthesis mark, centred on the analysis mark nearest in time. Pitch = the spacing of the
//     synthesis marks (T / ratio); formants = the grain is read at `phi` times the speed (source half-span phi * Lo), so the
//     spectral envelope moves by phi while the harmonics stay where the spacing puts them. Overlap-add is normalised by the
//     window sum, so the level stays put.
//   * the grain length adapts to the look-ahead budget (kLatency): (1 + phi) * Lo + T / 2 must fit, which is why very deep
//     voices (< ~110 Hz) get grains shorter than 2 periods.
#include "JuiceDSP.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>

namespace jb {
namespace {

const float kPi = 3.14159265358979323846f;
const float kFsDefault = 44100.0f;

inline float flushf(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }

// ---------------------------------------------------------------------------------------------------------------- biquad
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear() { z1 = z2 = 0; }
    void flush() { z1 = flushf(z1); z2 = flushf(z2); }
    void set(float nb0, float nb1, float nb2, float a0, float na1, float na2) {
        const float r = 1.0f / a0;
        b0 = nb0 * r; b1 = nb1 * r; b2 = nb2 * r; a1 = na1 * r; a2 = na2 * r;
    }
    void lp(float f, float q, float fs) {
        const float w = 2 * kPi * f / fs, c = cosf(w), al = sinf(w) / (2 * q);
        set((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void hp(float f, float q, float fs) {
        const float w = 2 * kPi * f / fs, c = cosf(w), al = sinf(w) / (2 * q);
        set((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void bp(float f, float q, float fs) {   // constant 0 dB peak
        const float w = 2 * kPi * f / fs, c = cosf(w), al = sinf(w) / (2 * q);
        set(al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak(float f, float q, float dB, float fs) {
        const float A = powf(10.0f, dB / 40.0f), w = 2 * kPi * f / fs, c = cosf(w), al = sinf(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void hshelf(float f, float dB, float fs) {   // RBJ high shelf, slope 1
        const float A = powf(10.0f, dB / 40.0f), w = 2 * kPi * f / fs, c = cosf(w), s = sinf(w);
        const float al = s * 0.70710678f, sa = 2 * sqrtf(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
            (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

inline float coefTau(float ms, float fs) { return 1.0f - expf(-1000.0f / (ms * fs)); }

struct Rng {
    uint32_t s = 22222u;
    inline float next() {   // -1 .. 1
        s = s * 1664525u + 1013904223u;
        return (float)(int32_t)s * (1.0f / 2147483648.0f);
    }
};

// ------------------------------------------------------------------------------------------------------------------ Psola

// ---- Learn: listens to the input for a few seconds (the tracker is forced on with a wide range) and derives the Tracking page and the
// de-ess threshold from what it heard. Statistics only (histograms): nothing is stored per sample, nothing allocates.
struct Learner {
    static const int FH = 64, CH = 21, GH = 71, SPH = 100, DB = 100;
    static const int kVoicedFrames = 4134;                  // ~6 s of voice (64-sample tracker frames at 44.1 kHz)
    static const int kMaxFrames = 14470;                    // ~21 s wall: stop even if the input was mostly silent
    static const int kMinVoiced = 700;                      // ~1 s of voice: less than that is not enough to learn from
    bool on = false, finished = false;
    long long frames = 0, vframes = 0;
    int fh[FH], ch[CH], gh[GH], sp[SPH], sibV[DB], sibU[DB];
    int nGap = 0, nSp = 0, nSibV = 0, nSibU = 0, octJ = 0, gapRun = 0;
    bool seen = false;
    float prevT = 0, prevC = 0;
    float fsr = 44100.0f;

    void start(float fs) {
        fsr = fs;
        memset(fh, 0, sizeof fh); memset(ch, 0, sizeof ch); memset(gh, 0, sizeof gh); memset(sp, 0, sizeof sp);
        memset(sibV, 0, sizeof sibV); memset(sibU, 0, sizeof sibU);
        nGap = nSp = nSibV = nSibU = octJ = gapRun = 0;
        frames = vframes = 0; seen = false; prevT = prevC = 0; finished = false; on = true;
    }
    float progress() const {
        if (!on) return 0.0f;
        return clampf(std::max((float)vframes / (float)kVoicedFrames, (float)frames / (float)kMaxFrames), 0.0f, 1.0f);
    }
    // one tracker frame: the best period (samples at the full rate) and its periodicity
    void frame(float conf, float T) {
        if (!on) return;
        ++frames;
        if (conf > 0.5f && T > 1.0f) {
            ++vframes;
            ++ch[std::min(CH - 1, (int)(conf * 20.0f))];
            if (seen && gapRun > 0 && gapRun <= GH - 1) { ++gh[gapRun]; ++nGap; }
            gapRun = 0; seen = true;
            if (conf > 0.85f) {
                const float f0 = fsr / T;
                if (f0 > 50.0f) { const int b = (int)(12.0f * log2f(f0 / 50.0f)); if (b >= 0 && b < FH) ++fh[b]; }
            }
            if (prevT > 0 && prevC > 0.6f && conf > 0.6f) {
                const float a = fabsf(12.0f * log2f(T / prevT));
                if ((a > 10.5f && a < 13.5f) || (a > 18.5f && a < 20.5f)) ++octJ;
                else if (a < 0.5f) { ++sp[(int)(a * 200.0f)]; ++nSp; }
            }
            prevT = T; prevC = conf;
        } else {
            if (seen) ++gapRun;
            if (gapRun > GH - 1) prevT = 0;
        }
        if (vframes >= kVoicedFrames || frames >= kMaxFrames) finished = true;
    }
    // the sibilance band level (dB) seen while the input is voiced / unvoiced (only blocks that are not silence)
    void sib(bool voiced, float db) {
        if (!on) return;
        const int b = clampi((int)(db + 90.0f), 0, DB - 1);
        if (voiced) { ++sibV[b]; ++nSibV; } else { ++sibU[b]; ++nSibU; }
    }
    static float quantile(const int *h, int n, int total, float q) {   // bin index (fractional) where the cumulative count reaches q
        if (total <= 0) return 0.0f;
        const float target = q * (float)total;
        float acc = 0;
        for (int i = 0; i < n; ++i) { acc += (float)h[i]; if (acc >= target) return (float)i; }
        return (float)(n - 1);
    }
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
    static float posOfLog(float f, float lo, float hi) { return clampf(1000.0f * logf(f / lo) / logf(hi / lo), 0.0f, 1000.0f); }
    bool enough() const { return vframes >= kMinVoiced; }
    // the learned knob positions: set[k] says whether this one was learned
    void result(float *pos, bool *set) const {
        for (int k = 0; k < P_COUNT; ++k) set[k] = false;
        if (!enough()) return;
        // range of the voice
        int nf = 0; for (int i = 0; i < FH; ++i) nf += fh[i];
        float floorHz = 70.0f, ceilHz = 700.0f;
        if (nf > 100) {
            const float lo = 50.0f * exp2f(quantile(fh, FH, nf, 0.05f) / 12.0f);
            const float hi = 50.0f * exp2f((quantile(fh, FH, nf, 0.95f) + 1.0f) / 12.0f);
            floorHz = clampf(0.8f * lo, 70.0f, 200.0f);
            ceilHz = clampf(1.5f * hi, 250.0f, 920.0f);
            if (ceilHz < 2.0f * floorHz) ceilHz = clampf(2.0f * floorHz, 250.0f, 920.0f);
            pos[P_FLOOR] = posOfLog(floorHz, 70.0f, 200.0f); set[P_FLOOR] = true;
            pos[P_CEIL] = posOfLog(ceilHz, 250.0f, 920.0f); set[P_CEIL] = true;
        }
        // rasp from the median periodicity of the voiced frames: clean ~0.95 -> 20 %, raspy ~0.70 -> 78 %
        const float med = (quantile(ch, CH, (int)vframes, 0.5f) + 0.5f) * 0.05f;
        const float rasp = clampf(20.0f + (0.95f - med) / 0.25f * 58.0f, 15.0f, 90.0f);
        pos[P_RASP] = rasp * 10.0f; set[P_RASP] = true;
        pos[P_MORPH] = clampf(1000.0f * logf((25.0f + 0.35f * rasp) / 5.0f) / logf(40.0f), 0.0f, 1000.0f); set[P_MORPH] = true;
        pos[P_OVERLAP] = clampf((30.0f + 0.3f * rasp) * 10.0f, 0.0f, 1000.0f); set[P_OVERLAP] = true;
        // hold: the gaps inside a voiced stretch (80th percentile + a margin)
        if (nGap >= 5) {
            const float ms = quantile(gh, GH, nGap, 0.8f) * 64.0f / fsr * 1000.0f + 10.0f;
            pos[P_HOLD] = clampf(ms, 40.0f, 140.0f) / 0.15f; set[P_HOLD] = true;
        }
        // glide: fast pitch movement (runs, trills) wants a quick tracker
        if (nSp >= 100) {
            const float st10 = (quantile(sp, SPH, nSp, 0.95f) + 1.0f) / 200.0f * 6.9f;   // semitones per 10 ms
            pos[P_GLIDE] = clampf(55.0f - 25.0f * st10, 15.0f, 60.0f) * 10.0f; set[P_GLIDE] = true;
        }
        // octave guard: more octave jumps per second of voice -> a firmer guard
        {
            const float sec = (float)vframes * 64.0f / fsr;
            pos[P_GUARD] = clampf(50.0f + 8.0f * (float)octJ / sec, 40.0f, 90.0f) * 10.0f; set[P_GUARD] = true;
        }
        // de-ess threshold between the voiced level and the unvoiced (s, sh, t) level of the sibilance band
        if (nSibV >= 200 && nSibU >= 200) {
            const float v = quantile(sibV, DB, nSibV, 0.5f) - 90.0f, u = quantile(sibU, DB, nSibU, 0.9f) - 90.0f;
            if (u > v + 3.0f) {
                const float thr = clampf(v + 0.75f * (u - v), -50.0f, -20.0f);
                pos[P_THRESH] = (thr + 60.0f) / 0.06f; set[P_THRESH] = true;
            }
        }
    }
};

struct Psola {
    static const int RB = 8192, MASK = RB - 1;
    int LAT = JuiceDSP::latencyFor(JuiceDSP::kLatencyDefault);   // the look-ahead budget (a mode of the plugin)
    static const int DEC = 4, DRB = 1024, DW = 192, TAUMIN = 12, TAUMAX = 160;
    static const int MAXMARKS = 64;
    static const int UNV = 224;

    float xin[RB], xsrc[RB], acc[RB], wacc[RB], trg[RB], dryL[RB], dryR[RB];
    float dec[DRB];
    float hann[1025];
    long long nIn = 0, rOut = -LAT;
    long long nd = 0;

    // pitch tracker
    Biquad dl1, dl2, sl1, sl2;
    float Ttrack = 0, conf = 0;
    bool voiced = false;
    int prevTau = 0;
    float cmAvg[TAUMAX + 2];                                      // the normalised difference function averaged over a few frames (the multiples decision)
    bool cmValid = false;
    int lastTau = 0, lastAge = 1000;                              // memory of the last good period: a new voiced stretch starts from it (no octave lock at an onset)
    float rmsIn = 0;
    float trackAlpha = 0.3f;
    // Tracking page
    int tauLo = TAUMIN, tauHi = TAUMAX - 1;                       // search range (from Floor / Ceiling)
    float rasp = 0.5f, guard = 0.6f;
    int holdFrames = 41, holdCnt = 0, jumpCnt = 0;
    int jumpTau = 0;
    bool rawVoiced = false;
    float vw = 0, vwUp = 0.1f, vwDown = 0.2f;                     // voicing weight (0..1), smoothed: the pitch shift fades in / out with it
    float rhoLast = 1.0f;

    // transient detector
    float tHp = 0, tFast = 0, tSlow = 0, tHold = 0;
    float tCoefs[6];

    // marks
    struct Mark { long long pos; float T; bool voiced; float vw; };
    Mark marks[MAXMARKS];
    int nm = 0;                                                   // total marks created (ring index = i % MAXMARKS)
    // synthesis
    double tsNext = 0;
    bool started = false;
    Learner lr;
    bool active = true;                                           // false while the shifter is bypassed (Pitch and Formant at 0)
    float jn = 0, sn = 0;
    Rng rng;
    float fs = kFsDefault;

    Psola() {
        for (int i = 0; i <= 1024; ++i) hann[i] = 0.5f * (1.0f + cosf(kPi * (float)i / 1024.0f));
        init(kFsDefault);
    }

    void init(float sr) {
        fs = sr;
        memset(xin, 0, sizeof xin); memset(xsrc, 0, sizeof xsrc); memset(acc, 0, sizeof acc); memset(wacc, 0, sizeof wacc);
        memset(trg, 0, sizeof trg); memset(dryL, 0, sizeof dryL); memset(dryR, 0, sizeof dryR); memset(dec, 0, sizeof dec);
        nIn = 0; rOut = -LAT; nd = 0;
        dl1.lp(2000.0f, 0.5412f, fs); dl2.lp(2000.0f, 1.3066f, fs);
        dl1.clear(); dl2.clear();
        setSrcCutoff(1.0f);
        cmValid = false;
        Ttrack = 0; conf = 0; voiced = false; prevTau = 0; lastTau = 0; lastAge = 1000; rmsIn = 0; rawVoiced = false; holdCnt = 0; jumpCnt = 0; vw = 0;
        tHp = tFast = tSlow = tHold = 0;
        tCoefs[0] = coefTau(0.3f, fs); tCoefs[1] = coefTau(3.0f, fs); tCoefs[2] = coefTau(8.0f, fs);
        tCoefs[3] = coefTau(60.0f, fs); tCoefs[4] = coefTau(6.0f, fs); tCoefs[5] = 1.0f - expf(-2 * kPi * 2000.0f / fs);
        nm = 1; marks[0].pos = 0; marks[0].T = (float)UNV; marks[0].voiced = false; marks[0].vw = 0;
        tsNext = 0; started = false; jn = sn = 0;
    }

    // low-pass in front of the grain source: keeps formant up-shifts from aliasing (cut-off = 0.45 fs / phi)
    void setSrcCutoff(float phi) {
        const float fc = std::min(0.45f * fs, 0.45f * fs / std::max(1.0f, phi));
        sl1.lp(fc, 0.5412f, fs); sl2.lp(fc, 1.3066f, fs);
    }

    // a new look-ahead budget: the output restarts at nIn - LAT (the input rings keep their history; the pending grains are dropped)
    void setLatency(int n) {
        if (n == LAT) return;
        LAT = n;
        memset(acc, 0, sizeof acc); memset(wacc, 0, sizeof wacc);
        rOut = nIn - LAT;
        tsNext = (double)std::max<long long>(rOut, 0);
        started = true;
    }

    // -------- input
    inline void push(float l, float r) {
        const float m = 0.5f * (l + r);
        const int i = (int)(nIn & MASK);
        xin[i] = m; dryL[i] = l; dryR[i] = r;
        xsrc[i] = sl2.run(sl1.run(m));
        const float dv = dl2.run(dl1.run(m));
        if ((nIn & (DEC - 1)) == DEC - 1) { dec[nd & (DRB - 1)] = dv; ++nd; }
        // transient detector: fast / slow envelope of the part above ~2 kHz
        tHp += tCoefs[5] * (m - tHp);
        const float a = fabsf(m - tHp);
        tFast += (a > tFast ? tCoefs[0] : tCoefs[1]) * (a - tFast);
        tSlow += (a > tSlow ? tCoefs[2] : tCoefs[3]) * (a - tSlow);
        if (tFast > 2.2f * tSlow + 4e-4f) tHold = 1.0f; else tHold -= tCoefs[4] * tHold;
        trg[i] = tHold;
        ++nIn;
        if (active && (nIn & 63) == 0) yin();
    }
    // (re)start the analysis / synthesis after a bypass: the rings keep filling while bypassed, so the tracker has its history at once
    void setActive(bool on) {
        if (on == active) return;
        active = on;
        if (!on) return;
        memset(acc, 0, sizeof acc); memset(wacc, 0, sizeof wacc);
        voiced = false; prevTau = 0; lastTau = 0; lastAge = 1000; Ttrack = 0; rawVoiced = false; holdCnt = 0; jumpCnt = 0; vw = 0;
        const long long p0 = std::max<long long>(std::max<long long>(rOut, 0) - UNV, 0);
        nm = 1; marks[0] = Mark{p0, (float)UNV, false, 0.0f};
        tsNext = (double)std::max<long long>(rOut, 0); started = true;
    }

    // -------- pitch tracker (YIN on the decimated signal)
    void yin() {
        if (nd < DW + TAUMAX) return;
        float b[DW + TAUMAX];
        const long long t0 = nd - (DW + TAUMAX);
        double e = 0;
        for (int j = 0; j < DW + TAUMAX; ++j) { b[j] = dec[(t0 + j) & (DRB - 1)]; }
        for (int j = TAUMAX; j < DW + TAUMAX; ++j) e += (double)b[j] * b[j];
        rmsIn = sqrtf((float)(e / DW));
        if (rmsIn < 3e-4f) { cmValid = false; voiced = false; rawVoiced = false; conf = 0; prevTau = 0; holdCnt = 0; jumpCnt = 0; vw += vwDown * (0.0f - vw); return; }
        float d[TAUMAX + 2], cm[TAUMAX + 2];
        for (int tau = 1; tau <= TAUMAX; ++tau) {
            float s = 0;
            for (int j = 0; j < DW; ++j) { const float df = b[j] - b[j + tau]; s += df * df; }
            d[tau] = s;
        }
        float run = 0;
        cm[0] = 1;
        for (int tau = 1; tau <= TAUMAX; ++tau) { run += d[tau]; cm[tau] = run > 1e-12f ? d[tau] * tau / run : 1.0f; }
        const int lo0 = std::max(TAUMIN, tauLo), hi0 = std::min(TAUMAX - 1, std::max(tauHi, lo0 + 4));
        const float dipThr = 0.15f + 0.05f * rasp;                // a raspy voice has shallower dips
        int best = -1;
        for (int tau = lo0; tau < hi0; ++tau) {
            if (cm[tau] < dipThr) { while (tau + 1 < hi0 && cm[tau + 1] < cm[tau]) ++tau; best = tau; break; }
        }
        if (best < 0) {
            float mn = 10;
            for (int tau = lo0; tau < hi0; ++tau) if (cm[tau] < mn) { mn = cm[tau]; best = tau; }
        }
        {   // sub-harmonic check: the first dip under the threshold can sit at 1/2 .. 1/4 of the period when F1 falls on a strong harmonic; a multiple of
            // the lag whose dip is clearly deeper is the fundamental. A GOOD short-lag dip is never traded for a multiple unless the multiple is much
            // deeper (a voice is as periodic at 2 x its period as at the period: that is how the tracker used to fall to a sub-octave on fast notes,
            // place grains every 2-4 periods and buzz). A lag near the top of the search range (above ~0.65 x Ceiling) has to prove itself.
            for (int k = 2; k <= 4; ++k) {
                const int c0 = best * k, w = std::max(2, c0 / 25);
                if (c0 + w >= hi0) break;
                int bt = c0 - w;
                for (int t2 = c0 - w; t2 <= c0 + w; ++t2) if (cm[t2] < cm[bt]) bt = t2;
                const float pen = 0.15f * std::max(0.0f, log2f((float)tauLo / 0.65f / (float)best));
                if (cm[bt] + 0.04f < cm[best] + pen && cm[bt] < 0.6f * cm[best] + pen) best = bt;
            }
        }
        if (lastAge < 1000) ++lastAge;
        const int ref = prevTau > 0 ? prevTau : (lastAge < 300 ? lastTau : 0);       // the period being followed: the current one, or the one from before a short gap
        const bool tracking = ref > 0;
        if (tracking) {   // continuity: stay on the previous period unless the candidate is clearly better / persistently better
            const float tol = 0.10f + 0.06f * guard;
            const int lo = std::max(lo0, (int)(ref * (1.0f - tol))), hi = std::min(hi0, (int)(ref * (1.0f + tol)) + 1);
            int bp = lo;
            for (int tau = lo; tau <= hi; ++tau) if (cm[tau] < cm[bp]) bp = tau;
            const int cand = best;
            if (cand >= lo && cand <= hi) { jumpCnt = 0; }                                       // inside the tolerance: just follow
            else {
                const float gain = cm[bp] - cm[cand];                                              // how much better the candidate matches than the followed lag
                const float jr = fabsf(logf((float)cand / (float)ref)) * 1.4427f;             // octaves
                // an octave, a fifth (the typical vocal-fry / period-doubling / harmonic errors) must persist; a smaller jump (a quick note change) only
                // has to persist for a few frames; evidence beats waiting: the followed lag no longer fits at all and the new one fits well = a real note change
                const int need = jr > 0.3f ? 2 + (int)(guard * 14.0f) : 1 + (int)(guard * 3.0f);
                const bool clear = jr <= 0.3f && (gain > 0.12f + 0.20f * guard || (cm[bp] > 0.32f && cm[cand] < 0.14f + 0.06f * rasp));   // never for octave-size jumps
                if (gain > 0.03f) {
                    if (jumpTau > 0 && fabsf(logf((float)cand / (float)jumpTau)) * 1.4427f < 0.12f) ++jumpCnt; else { jumpTau = cand; jumpCnt = 1; }
                } else jumpCnt = 0;
                if (clear || (gain > 0.03f && jumpCnt >= need)) jumpCnt = 0;                       // follow the candidate
                else best = bp;                                                                    // stay
            }
        } else jumpCnt = 0;
        {   // sub-octave recovery: the voice is periodic at 1/2, 1/3 or 1/4 of the lag being followed with about the same quality: the longer lag is a
            // multiple (the tracker dropped an octave or two on a fast note), the shorter one is the pitch
            for (int k = 4; k >= 2; --k) {
                const int c0 = (best + k / 2) / k, w = std::max(1, c0 / 14);
                if (c0 - w < lo0) continue;
                int bt = c0 - w;
                for (int t2 = c0 - w; t2 <= c0 + w; ++t2) if (cm[t2] < cm[bt]) bt = t2;
                if (cm[bt] < 0.20f + 0.05f * rasp && cm[bt] < cm[best] + 0.06f) { best = bt; jumpCnt = 0; break; }
            }
        }
        const float c = 1.0f - cm[best];
        float tauF = (float)best;
        if (best > 1 && best < TAUMAX) {   // parabolic refinement
            const float y0 = d[best - 1], y1 = d[best], y2 = d[best + 1], den = y0 - 2 * y1 + y2;
            if (den > 1e-12f) tauF += clampf(0.5f * (y0 - y2) / den, -1.0f, 1.0f);
        }
        conf = clampf(c, 0.0f, 1.0f);
        lr.frame(conf, tauF * DEC);
        const float onT = 0.70f - 0.25f * rasp, offT = 0.50f - 0.25f * rasp;   // Rasp lowers the periodicity a voice must show to count as voiced
        if (conf > onT) rawVoiced = true; else if (conf < offT) rawVoiced = false;
        if (rawVoiced) {
            holdCnt = holdFrames;
            voiced = true;
            const float T = tauF * DEC;
            if (Ttrack <= 0 || prevTau == 0) Ttrack = T; else Ttrack *= powf(T / Ttrack, trackAlpha);   // smoothed in the log domain
            prevTau = best;
            if (conf > 0.6f) { lastTau = best; lastAge = 0; }
        } else if (holdCnt > 0 && Ttrack > 0) {   // a dip in periodicity inside a voiced stretch (rasp, fry, a dropped pulse): keep going on the held period
            --holdCnt;
            voiced = true;
        } else { voiced = false; prevTau = 0; }
        vw += (voiced ? vwUp : vwDown) * ((voiced ? 1.0f : 0.0f) - vw);
    }

    // -------- analysis marks
    inline const Mark &mark(int i) const { return marks[i % MAXMARKS]; }
    void copyRange(long long start, int len, float *dst) const {
        for (int k = 0; k < len; ++k) { const long long i = start + k; dst[k] = (i < 0 || i >= nIn) ? 0.0f : xin[i & MASK]; }
    }
    void createMarks() {
        for (int it = 0; it < 12; ++it) {
            const Mark &lm = mark(nm - 1);
            if (!(voiced || vw > 0.3f) || Ttrack < 30.0f) {
                const long long np = lm.pos + (long long)((float)UNV * (1.0f + 0.30f * rng.next()));   // irregular: a fixed 197 Hz spacing would buzz
                if (np + 1 > nIn) return;
                marks[nm % MAXMARKS] = Mark{np, (float)UNV, false, vw};
                ++nm;
                continue;
            }
            const float Tt = clampf(Ttrack, 40.0f, 640.0f);
            const int Ti = (int)(Tt + 0.5f);
            const int h = std::max(12, Ti / 3), R = std::max(3, Ti / 4);
            const long long p = lm.pos + Ti;
            if (p + R + h + 2 > nIn) return;
            float ref[2 * 220 + 8], cand[2 * 220 + 2 * 160 + 16];
            const int H = std::min(h, 220), RR = std::min(R, 160);
            copyRange(lm.pos - H, 2 * H, ref);
            copyRange(p - RR - H, 2 * H + 2 * RR + 1, cand);
            float er = 0;
            for (int k = 0; k < 2 * H; ++k) er += ref[k] * ref[k];
            float bestN = -2, bestScore = -2;
            int bestD = 0;
            // sliding energy of the candidate window
            float ec = 0;
            for (int k = 0; k < 2 * H; ++k) ec += cand[k] * cand[k];
            for (int dlt = -RR; dlt <= RR; ++dlt) {
                const float *cw = cand + (dlt + RR);
                if (dlt > -RR) { const float out = cand[dlt + RR - 1], in = cand[dlt + RR + 2 * H - 1]; ec += in * in - out * out; }
                float s = 0;
                for (int k = 0; k < 2 * H; ++k) s += cw[k] * ref[k];
                const float ncc = s / sqrtf(er * ec + 1e-12f);
                const float score = ncc - 0.15f * fabsf((float)dlt) / (float)RR;   // a small pull towards the expected period
                if (score > bestScore) { bestScore = score; bestN = ncc; bestD = dlt; }
            }
            long long np = p + bestD;
            if (np <= lm.pos + Ti / 2) np = lm.pos + Ti / 2 + 1;
            const float Tloc = (float)(np - lm.pos);
            marks[nm % MAXMARKS] = Mark{np, 0.5f * (Tloc + Tt), true, vw};
            ++nm;
        }
    }
    // the analysis mark nearest to time ts among those created
    const Mark &nearest(double ts) const {
        int i = nm - 1, lim = std::max(0, nm - MAXMARKS + 1);
        while (i > lim && (double)mark(i).pos > ts) --i;
        if (i + 1 < nm && fabs((double)mark(i + 1).pos - ts) < fabs((double)mark(i).pos - ts)) ++i;
        return mark(i);
    }

    // -------- synthesis
    static inline float cubic(const float *ring, long long i, float f) {
        const float y0 = ring[(i - 1) & MASK], y1 = ring[i & MASK], y2 = ring[(i + 1) & MASK], y3 = ring[(i + 2) & MASK];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    inline float hannAt(float u) const {   // u in -1..1
        float x = fabsf(u) * 1024.0f;
        if (x >= 1024.0f) return 0.0f;
        const int i = (int)x;
        const float f = x - (float)i;
        return hann[i] + f * (hann[i + 1] - hann[i]);
    }
    void grain(double ts, const Mark &mk, float Lo, float phi, float gain) {
        const long long first = std::max<long long>(std::max<long long>(rOut, 0), (long long)ceil(ts - Lo));
        const long long last = (long long)floor(ts + Lo);
        const float invLo = 1.0f / Lo;
        for (long long i = first; i <= last; ++i) {
            const float o = (float)((double)i - ts);
            const float w = hannAt(o * invLo);
            const double q = (double)mk.pos + (double)phi * o;
            const long long qi = (long long)floor(q);
            const float v = cubic(xsrc, qi, (float)(q - (double)qi));
            acc[i & MASK] += w * v * gain;
            wacc[i & MASK] += w;
        }
    }
    void synth(float rho, float phi, float widen, float jit, float shim) {
        const long long rTarget = nIn - LAT;
        if (nm < 2) return;
        if (!started) { tsNext = (double)std::max<long long>(rTarget, 0); started = true; }
        for (int guard = 0; guard < 64; ++guard) {
            if (tsNext < (double)rOut) tsNext = (double)std::max<long long>(rOut, 0);
            const Mark &mk = nearest(tsNext);
            const bool v = mk.voiced;
            const float T = mk.T;
            const float rhoE = expf(logf(rho) * mk.vw);   // the pitch shift follows the (smoothed) voicing weight: no sudden jump at a voiced / unvoiced change
            rhoLast = v ? rhoE : 1.0f;
            const float Ts = v ? T / rhoE : T * (1.0f + 0.30f * rng.next());
            float Lo = std::max(T / phi, Ts) * widen;
            const float cap = std::max(24.0f, ((float)LAT - 0.5f * T - 8.0f) / (1.0f + phi));
            Lo = std::min(Lo, cap);
            Lo = std::max(Lo, 12.0f);
            if (tsNext - Lo > (double)rTarget) break;
            // the source must exist
            float Lsrc = Lo;
            const double avail = (double)(nIn - 3 - mk.pos);
            if ((double)phi * Lsrc > avail) Lsrc = (float)std::max(8.0, avail / phi);
            float g = 1.0f, jj = 0.0f;
            if (v) {   // a little natural jitter / shimmer on voiced grains only (unvoiced grains must stay phase-coherent)
                g = 1.0f + shim * sn;
                jj = jit * jn;
                jn += 0.3f * (rng.next() - jn);
                sn += 0.3f * (rng.next() - sn);
            }
            grain(tsNext, mk, Lsrc, phi, g);
            tsNext += (double)Ts * (1.0 + (double)jj);
        }
    }

    // -------- output
    inline void pull(float &dl, float &dr, float &dm, float &wet, float &tp) {
        if (rOut < 0) { dl = dr = dm = wet = tp = 0.0f; ++rOut; return; }
        const int i = (int)(rOut & MASK);
        const float w = wacc[i];
        wet = acc[i] / (w > 1.0f ? w : 1.0f);
        acc[i] = 0; wacc[i] = 0;
        dl = dryL[i]; dr = dryR[i]; dm = xin[i]; tp = trg[i];
        ++rOut;
    }
};

// out-of-class definitions (C++11: std::min / std::max take references)
const int Psola::RB; const int Psola::MASK; const int Psola::DEC; const int Psola::DRB; const int Psola::DW;
const int Psola::TAUMIN; const int Psola::TAUMAX; const int Psola::MAXMARKS; const int Psola::UNV;

}  // namespace

// ------------------------------------------------------------------------------------------------------------------ Impl
struct JuiceDSP::Impl {
    float fs = kFsDefault;
    float pos[P_COUNT], sm[P_COUNT];
    bool link = false;
    float linkS = 0;
    float ctlA = 0.1f;

    Psola v;
    Rng noiseRng;

    // derived (per control block)
    float rho = 1, phi = 1, widen = 1, jit = 0, shim = 0, tpAmt = 0, ident = 1;
    float nat = 0;

    // spectral tamer
    Biquad sibHp, sibLp, lowLp;
    float sibEnv = 0, sibGain = 1, sibGainTarget = 1, sibGr = 0;
    float lowPow = 0, lowGain = 1, lowGainTarget = 1, lowGr = 0;
    float sibAtk = 0, sibRel = 0, lowAtk = 0, lowRel = 0, sibThr = 0, lowThr = 0;
    bool lowOn = false;
    // harmonic shaper
    Biquad hHp, hLp1, hLp;
    float hx1 = 0, hCs = 1, hSn = 1, oddAmt = 0, evenAmt = 0, h5w = 0;
    // formant filters
    Biquad ff[3];
    bool ffOn[3] = {false, false, false};
    // air
    Biquad airShelf, airHp, airDet;
    float airEnv = 0, airEnvCoef = 0, airAmt = 0;
    float mixW = 1, mixD = 0;
    // level match of the shifter's output to the delayed input (the grains lose a little level at low pitches / long overlaps)
    float lmIn = 0, lmWet = 0, lmGain = 1, lmCoef = 0, lmGCoef = 0;
    int flushCnt = 0;

    Impl() {
        for (int i = 0; i < P_COUNT; ++i) pos[i] = sm[i] = kDefaultPos[i];
        init(kFsDefault);
    }

    void init(float sr) {
        fs = sr;
        ctlA = 1.0f - expf(-16.0f / (fs * 0.012f));
        v.init(fs);
        for (int i = 0; i < P_COUNT; ++i) sm[i] = pos[i];
        linkS = link ? 1.0f : 0.0f;
        sibHp.clear(); sibLp.clear(); lowLp.clear(); hHp.clear(); hLp1.clear(); hLp.clear(); hx1 = 0;
        for (auto &b : ff) b.clear();
        airShelf.clear(); airHp.clear(); airDet.clear();
        lmIn = lmWet = 0; lmGain = 1;
        sibEnv = 0; sibGain = sibGainTarget = 1; sibGr = 0; lowPow = 0; lowGain = lowGainTarget = 1; lowGr = 0; airEnv = 0;
        updateControl(true);
    }

    static inline float toDb(float x) { return 20.0f * log10f(x > 1e-9f ? x : 1e-9f); }

    // reads the smoothed parameters and refreshes every derived coefficient (every 16 samples)
    void updateControl(bool first) {
        if (!first) for (int i = 0; i < P_COUNT; ++i) sm[i] += ctlA * (pos[i] - sm[i]);
        linkS += ctlA * ((link ? 1.0f : 0.0f) - linkS);
        const float pSt = JuiceMap::semis(sm[P_PITCH]);
        float fSt = JuiceMap::semis(sm[P_FORMANT]) + linkS * (sm[P_FTRACK] * 0.001f) * pSt;
        fSt = clampf(fSt, -14.0f, 14.0f);
        rho = exp2f(pSt / 12.0f);
        phi = exp2f(fSt / 12.0f);
        // Pitch and Formant both at 0: the shifter is a perfect bypass (the dry path is delayed to match), crossfaded over one knob position
        ident = clampf(1.0f - (fabsf(pSt) + fabsf(fSt) - 0.012f) * (1.0f / 0.024f), 0.0f, 1.0f);
        const float fsm = sm[P_FSMOOTH] * 0.001f;
        nat = sm[P_NATURAL] * 0.001f;
        widen = 1.0f + 0.30f * fsm + 0.35f * sm[P_OVERLAP] * 0.001f;
        tpAmt = 0.9f * fsm;
        // Tracking page
        v.rasp = sm[P_RASP] * 0.001f;
        v.guard = sm[P_GUARD] * 0.001f;
        v.trackAlpha = 0.04f + 0.56f * (1.0f - sm[P_GLIDE] * 0.001f);
        v.holdFrames = (int)(JuiceMap::holdMs(sm[P_HOLD]) * 0.001f * fs / 64.0f + 0.5f);
        {
            const float mo = JuiceMap::morphMs(sm[P_MORPH]) * 0.001f;
            v.vwUp = 1.0f - expf(-64.0f / (fs * 0.25f * mo));   // a voice starting is shifted soon, a voice ending fades slowly
            v.vwDown = 1.0f - expf(-64.0f / (fs * 0.5f * mo));
        }
        const bool learning = v.lr.on;                                   // Learn listens with a wide range and neutral Rasp / Guard
        v.tauLo = std::max(Psola::TAUMIN, (int)(fs / (float)Psola::DEC / (learning ? 800.0f : JuiceMap::ceilHz(sm[P_CEIL]))));
        v.tauHi = std::min(Psola::TAUMAX - 1, (int)(fs / (float)Psola::DEC / (learning ? 60.0f : JuiceMap::floorHz(sm[P_FLOOR]))));
        if (learning) { v.rasp = 0.5f; v.guard = 0.6f; }
        jit = 0.006f * nat;
        shim = 0.05f * nat;
        v.setSrcCutoff(phi);

        // Spectral Tamer
        const float thr = JuiceMap::thresholdDb(sm[P_THRESH]);
        sibThr = thr;
        lowThr = thr + 10.0f;
        const float atk = JuiceMap::attackMs(sm[P_ATTACK]), rel = JuiceMap::releaseMs(sm[P_RELEASE]);
        sibAtk = coefTau(atk, fs); sibRel = coefTau(rel, fs);
        lowAtk = coefTau(std::max(atk, 5.0f), fs); lowRel = coefTau(std::max(rel, 40.0f), fs);
        sibHp.hp(5000.0f, 0.7071f, fs); sibLp.lp(10000.0f, 0.7071f, fs);
        lowOn = !JuiceMap::lowEndOff(sm[P_LOWEND]);
        lowLp.lp(JuiceMap::lowEndHz(sm[P_LOWEND]), 0.7071f, fs);

        // Harmonic Shaper
        const float colorHz = JuiceMap::colorHz(sm[P_COLOR]);
        hHp.hp(colorHz / 1.5f, 0.7071f, fs);                 // the focus band: 2 octaves around Color, 12 dB/oct skirts
        hLp1.lp(std::min(colorHz * 1.5f, 0.45f * fs), 0.7071f, fs);
        hLp.lp(14000.0f, 0.7071f, fs);
        {   // instantaneous amplitude / phase of the band from two neighbouring samples (exact for a sine at Color)
            const float w0 = 2.0f * kPi * colorHz / fs;
            hCs = 1.0f / cosf(0.5f * w0);
            hSn = 1.0f / (2.0f * sinf(0.5f * w0));
        }
        oddAmt = 0.5f * sm[P_ODD] * 0.001f;
        evenAmt = 0.5f * sm[P_EVEN] * 0.001f;
        h5w = clampf((3000.0f - colorHz) / 2000.0f, 0.0f, 1.0f) * 0.25f;   // a little 5th below 2.5 kHz only (it would alias above)

        // Natural Formant Filter
        const int fp[3] = {P_F1C, P_F2C, P_F3C};
        for (int k = 0; k < 3; ++k) {
            const float qp = sm[fp[k] + 1];
            const float hz = k == 0 ? JuiceMap::f1Hz(sm[fp[k]]) : k == 1 ? JuiceMap::f2Hz(sm[fp[k]]) : JuiceMap::f3Hz(sm[fp[k]]);
            const float g = JuiceMap::qOff(qp) ? 0.0f : JuiceMap::qGainDb(qp);
            ffOn[k] = g > 0.01f;
            if (ffOn[k]) ff[k].peak(hz, JuiceMap::q(qp), g, fs);
        }

        // Air
        airAmt = sm[P_AIR] * 0.001f;
        airShelf.hshelf(8000.0f, 7.0f * airAmt, fs);
        airHp.hp(6500.0f, 0.7071f, fs);
        airDet.hp(3000.0f, 0.7071f, fs);
        airEnvCoef = coefTau(10.0f, fs);
        lmCoef = coefTau(40.0f, fs); lmGCoef = coefTau(60.0f, fs);

        // Mix (equal power)
        const float m = sm[P_MIX] * 0.001f;
        mixW = sinf(m * kPi * 0.5f);
        mixD = cosf(m * kPi * 0.5f);
        if (m >= 0.999f) { mixW = 1; mixD = 0; }
    }

    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        int done = 0;
        while (done < n) {
            const int c = std::min(16, n - done);
            updateControl(false);
            for (int i = 0; i < c; ++i) v.push(inL[done + i], inR[done + i]);
            v.setActive(ident < 1.0f || v.lr.on);
            if (v.active) {
                v.createMarks();
                v.synth(rho, phi, widen, jit, shim);
            }
            // tamer gain targets from the envelopes, once per control block
            const float sibLevel = toDb(sibEnv);
            if (v.lr.on && lmIn > 1e-5f) v.lr.sib(v.voiced, sibLevel);   // blocks above about -50 dB only
            const float over = sibLevel - sibThr;
            const float knee = 6.0f;
            float grS = 0;
            if (over > -knee * 0.5f) {
                const float x = over > knee * 0.5f ? over : (over + knee * 0.5f) * (over + knee * 0.5f) / (2 * knee);
                grS = clampf(-x * (1.0f - 1.0f / 6.0f), -20.0f, 0.0f);
            }
            sibGr = grS;
            const float tgtS = powf(10.0f, grS / 20.0f);
            const float lowLevel = 10.0f * log10f(lowPow > 1e-18f ? lowPow : 1e-18f);   // power -> dB
            const float overL = lowOn ? lowLevel - lowThr : -100.0f;
            float grL = 0;
            if (overL > -3.0f) {
                const float x = overL > 3.0f ? overL : (overL + 3.0f) * (overL + 3.0f) / 12.0f;
                grL = clampf(-x * (1.0f - 1.0f / 3.0f), -12.0f, 0.0f);
            }
            lowGr = grL;
            const float tgtL = powf(10.0f, grL / 20.0f);
            const float dsg = (tgtS - sibGain) * (1.0f / 16.0f), dlg = (tgtL - lowGain) * (1.0f / 16.0f);
            for (int i = 0; i < c; ++i) {
                float dl, dr, dm, wet, tp;
                v.pull(dl, dr, dm, wet, tp);
                lmIn += lmCoef * (dm * dm - lmIn);
                lmWet += lmCoef * (wet * wet - lmWet);
                const float lmT = (lmIn > 1e-7f && lmWet > 1e-8f) ? clampf(sqrtf(lmIn / lmWet), 0.7f, 2.2f) : 1.0f;
                lmGain += lmGCoef * (lmT - lmGain);
                float wetL = wet * lmGain;
                wetL += (dm - wetL) * ident;                                  // bypass blend
                float y = wetL + (dm - wetL) * (tpAmt * tp);                  // transient preserve
                // ---- Spectral Tamer
                const float sb = sibLp.run(sibHp.run(y));
                const float as = fabsf(sb);
                sibEnv += (as > sibEnv ? sibAtk : sibRel) * (as - sibEnv);
                sibGain += dsg;
                y -= sb * (1.0f - sibGain);
                const float lb = lowLp.run(y);
                lowPow += (lb * lb > lowPow ? lowAtk : lowRel) * (lb * lb - lowPow);
                lowGain += dlg;
                if (lowOn) y -= lb * (1.0f - lowGain);
                // ---- Harmonic Shaper
                if (oddAmt > 1e-4f || evenAmt > 1e-4f) {
                    // phase-based generator: band signal ~ e * sin(phi); add e * sin(k phi): exact 2nd / 3rd (..) harmonics of the
                    // band, no fundamental and no DC, level proportional to the band (a fixed ratio, whatever the input level)
                    const float xf = hLp1.run(hHp.run(y));
                    const float xm = 0.5f * (xf + hx1), dx = xf - hx1;
                    hx1 = xf;
                    const float pp = xm * hCs, qq = dx * hSn;
                    const float e2 = pp * pp + qq * qq;
                    float add = 0.0f;
                    if (e2 > 1e-14f) {
                        const float inv = 1.0f / sqrtf(e2);
                        const float u = pp * inv, v = qq * inv, e = e2 * inv;
                        const float s2 = 2 * u * v, c2 = v * v - u * u;
                        const float s3 = 3 * u - 4 * u * u * u, c3 = 4 * v * v * v - 3 * v;
                        const float s4 = 2 * s2 * c2, s5 = s3 * c2 + c3 * s2;
                        add = e * (oddAmt * (s3 + h5w * s5) + evenAmt * (s2 + 0.3f * s4));
                    }
                    y += hLp.run(add);
                }
                // ---- Natural Formant Filter
                for (int k = 0; k < 3; ++k) if (ffOn[k]) y = ff[k].run(y);
                // ---- Air
                if (airAmt > 1e-4f) {
                    const float hf = airDet.run(y);
                    const float ah = fabsf(hf);
                    airEnv += airEnvCoef * (ah - airEnv);
                    y = airShelf.run(y);
                    y += airHp.run(noiseRng.next()) * (0.5f * airAmt * airEnv);
                }
                // ---- Mix, ceiling
                float L = dl * mixD + y * mixW, R = dr * mixD + y * mixW;
                const float lim = 0.95f;
                if (L > lim) L = lim + 0.05f * tanhf((L - lim) * 20.0f); else if (L < -lim) L = -lim - 0.05f * tanhf((-L - lim) * 20.0f);
                if (R > lim) R = lim + 0.05f * tanhf((R - lim) * 20.0f); else if (R < -lim) R = -lim - 0.05f * tanhf((-R - lim) * 20.0f);
                outL[done + i] = L;
                outR[done + i] = R;
            }
            done += c;
            if (++flushCnt >= 8) {
                flushCnt = 0;
                sibHp.flush(); sibLp.flush(); lowLp.flush(); hHp.flush(); hLp1.flush(); hLp.flush(); hx1 = flushf(hx1);
                for (auto &b : ff) b.flush();
                airShelf.flush(); airHp.flush(); airDet.flush();
                v.dl1.flush(); v.dl2.flush(); v.sl1.flush(); v.sl2.flush();
                lmIn = flushf(lmIn); lmWet = flushf(lmWet); sibEnv = flushf(sibEnv); lowPow = flushf(lowPow); airEnv = flushf(airEnv);
                v.tFast = flushf(v.tFast); v.tSlow = flushf(v.tSlow); v.tHp = flushf(v.tHp); v.tHold = flushf(v.tHold);
            }
        }
    }
};

JuiceDSP::JuiceDSP() : d(new Impl()) {}
JuiceDSP::~JuiceDSP() { delete d; }
void JuiceDSP::init(float sr) { d->init(sr); }
void JuiceDSP::clear() { d->init(d->fs); }
void JuiceDSP::setParam(int id, float p) { if (id >= 0 && id < P_COUNT) d->pos[id] = clampf(p, 0.0f, 1000.0f); }
void JuiceDSP::setLink(bool on) { d->link = on; }
void JuiceDSP::setLatencyMode(int mode) { d->v.setLatency(latencyFor(mode < 0 ? 0 : mode > 2 ? 2 : mode)); }
int JuiceDSP::latencySamples() const { return d->v.LAT; }
void JuiceDSP::process(const float *inL, const float *inR, float *outL, float *outR, int n) { d->process(inL, inR, outL, outR, n); }
float JuiceDSP::pitchHz() const { return d->v.voiced && d->v.Ttrack > 1 ? d->fs / d->v.Ttrack : 0.0f; }
float JuiceDSP::rhoEffective() const { return d->v.rhoLast; }
int JuiceDSP::voicedState() const { return d->v.voiced ? 1 : 0; }
float JuiceDSP::grSibDb() const { return d->sibGr; }
float JuiceDSP::grLowDb() const { return d->lowGr; }
void JuiceDSP::startLearn() { d->v.lr.start(d->fs); }
void JuiceDSP::stopLearn() { d->v.lr.on = false; }
bool JuiceDSP::learning() const { return d->v.lr.on; }
float JuiceDSP::learnProgress() const { return d->v.lr.progress(); }
bool JuiceDSP::learnFinished() {
    if (!d->v.lr.on || !d->v.lr.finished) return false;
    return true;
}
bool JuiceDSP::learnResult(float *pos, bool *set) const {
    d->v.lr.result(pos, set);
    return d->v.lr.enough();
}

}  // namespace jb
