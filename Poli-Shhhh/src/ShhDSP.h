// Shhhhh: a learn-the-noise spectral de-noiser (in the spirit of iZotope RX Spectral / Voice De-noise) for the MPC.
//
// Signal path, per 128-sample hop (one MPC block):
//   * STFT, 512-point, with Mauler & Martin's low-delay asymmetric windows: the analysis window is 512 samples long
//     (86 Hz bins, enough to separate hum harmonics) but the synthesis window only covers the last 256, so the
//     latency is one hop: 128 samples = 2.9 ms (a symmetric 512 window would be 384 = 8.7 ms; MPC does no plugin
//     delay compensation, so this matters).
//   * Both channels in ONE complex FFT (left = real, right = imaginary). The gain is real and the same for both
//     channels (stereo-linked, so the image never wobbles), so it is applied to the packed spectrum directly and one
//     inverse FFT gives both outputs back.
//   * Per bin: threshold curve T = learned noise profile x Threshold x node offsets (interpolated between the 6 nodes
//     in log frequency). Gain = decision-directed Wiener (Ephraim-Malah a-priori SNR), the classic cure for "musical
//     noise" (since 1.0.2: two-step SNR + MMSE log-spectral amplitude, smoothing widening with frequency, and a
//     psychoacoustic floor: no noise pushed below what the voice masks), then a ~40 ms release. The 0..1 decision s scales the
//     Reduction in dB: gain = -Reduction x (1 - s) dB (0..100 dB). Output (-24..+24 dB) then sets the volume.
//   * Learn (up to 3 s): 32 sub-windows of 93 ms; the profile is the mean of the quietest quarter of them per bin,
//     so it finds the noise floor even when the voice is talking over it (minimum-statistics idea, Martin 2001).
//     Then each node jumps to the most prominent noise peak in its segment (hum, a whine), or stays at its default.
//   * Pass (bypass) = the input delayed by the same 2.9 ms (A/B without a timing jump); Diff = input - output, what is
//     being removed. Both crossfade over 10 ms. Pass wins over Diff.
// Real time: no allocation, no locks, fixed work per hop (2 FFTs of 512 + ~257 bins of arithmetic).
#pragma once
#include <cstdint>

namespace shh {

static const int kN = 512;            // FFT / analysis window
static const int kHop = 128;          // = MPC block = latency
static const int kBins = kN / 2 + 1;  // 257
static const int kNodes = 6;
static const int kNodePos = 5;        // horizontal positions a node can take inside its segment (index 2 = default)
static const int kBands = 32;         // spectrum preview columns
static const int kSubHops = 32;       // hops per learn sub-window (93 ms)
static const int kSubs = 32;          // sub-windows in a full learn (2.97 s)
static const float kSR = 44100.0f;

static const float kGridTopDb = 0.0f, kGridBotDb = -120.0f;   // the grid's level scale (20 dB per row)
static const float kDefaultProfileDb = -60.0f;                // before any Learn: a flat gate at the default nodes

extern const float kNodeDefaultHz[kNodes];

// The gain stage's choices. The defaults are what ships (1.0.2); test/eval/run_eval.py switches them to compare.
// Chosen on 180 conditions (8 speech clips + a song, 10 noises from white hiss to a cafe, 5/15 dB SNR, Learn on the
// noise first), against 1.0.1 at the same fader position (PESQ-WB / STOI / speech distortion SDR / noise removed NR,
// test/eval/run_eval.py, PYTHONHASHSEED=0):
//   Reduction 12: PESQ 1.972 vs 1.988, STOI .930 vs .922, SDR 20.2 vs 17.1 dB, NR  5.8 vs  6.7 dB
//   Reduction 30: PESQ 2.163 vs 2.058, STOI .924 vs .904, SDR 15.6 vs 12.5 dB, NR 10.4 vs 12.0 dB
//   Reduction 60: PESQ 1.897 vs 1.746, STOI .913 vs .882, SDR 13.1 vs 10.1 dB, NR 12.8 vs 14.9 dB
// The pauses go exactly as deep as before; under the voice a little more noise is left (where the voice masks it).
// Tried and dropped: cepstral smoothing (speech distortion up 5 dB), speech-presence gains (little noise removed),
// a knee and a slower attack (no less musical noise), adaptive noise tracking (with a good Learn it only takes voice).
struct Tuning {
    float alpha = 0.95f;      // decision-directed smoothing (0.98 in 1.0.1; 0.95 + TSNR keeps more voice)
    int rule = 1;             // 0 Wiener, 1 MMSE log-spectral amplitude (Ephraim & Malah 1985)
    int tsnr = 1;             // 1: two-step SNR (Plapous 2006): re-estimate xi from the first gain, no DD lag
    int presence = 0;         // 1: s = speech presence probability (Gerkmann & Hendriks 2012) instead of the gain
    float xiH1Db = 15.0f;     //    its fixed a-priori SNR under speech presence
    int omlsa = 0;            // 1: gain = g^s * floor^(1-s) (OM-LSA, Cohen 2002) instead of floor^(1-s)
    float relMs = 40.0f;      // release of the gain (0 = none)
    int freqSmooth = 1;       // [1/4 1/2 1/4] across bins (when wideHf is 0)
    int wideHf = 1;           // 1: smoothing across +-1/12 octave (at least +-1 bin), half with the bin itself
    int mask = 1;             // 1: don't push noise below what the voice masks anyway (Bark-band masking threshold)
    float maskOffDb = 18.0f;  //    masking offset below the spread voice energy
    float redScale = 1.0f;    // fader dB -> internal dB (1: the fader is the dB taken off in the pauses)
    int track = 0;            // 1: follow slow changes of the noise after Learn (Gerkmann & Hendriks 2012 MMSE noise
                              //    PSD), within +-trackDb of the learned profile; 2: also without a Learn
    float trackDb = 6.0f;
    float trackMs = 40.0f;
};

class ShhDSP {
public:
    Tuning tuning;
    const float *gains() const { return gain_; }   // the last hop's gains (test/eval's shadow filtering)
    const float *analysisWindow() const { return wa_; }
    const float *synthesisWindow() const { return ws_; }
    void fftPublic(float *re, float *im) const { fft(re, im); }
    void init();

    // parameters (smoothed internally)
    void setThreshold(float db);         // -30..+30
    void setReduction(float db);         // 0..100
    void setOutput(float db);            // -24..+24: output volume (not applied in Pass, which stays untouched)
    void setBypass(bool on) { bypass_ = on; }
    void setDiff(bool on) { diff_ = on; }
    bool bypass() const { return bypass_; }
    bool diff() const { return diff_; }

    // Learn
    void startLearn();
    void stopLearn();                    // commits what was heard (>= 2 sub-windows), else keeps the old profile
    bool learning() const { return learning_; }
    float learnProgress() const { return learning_ ? (float)subsDone_ / kSubs : 0.0f; }
    bool profileLearned() const { return learned_; }

    // nodes: level = where the node is drawn (dB on the grid scale) = profile at its frequency + Threshold + offset
    float nodeLevel(int i) const;        // clamped to the grid
    void setNodeLevel(int i, float db);  // a user drag: sets that node's offset
    int nodePos(int i) const { return nodePos_[i]; }
    void setNodePos(int i, int p);
    float nodeHz(int i) const { return candHz_[i][nodePos_[i]]; }
    float nodeOffset(int i) const { return offT_[i]; }
    void setNodeOffset(int i, float db);

    // spectrum preview: input level per column, 0..1 of the grid height (0 = -120 dB, 1 = 0 dB)
    float band(int j) const { return dispDb_[j] <= kGridBotDb ? 0.0f : (dispDb_[j] - kGridBotDb) / (kGridTopDb - kGridBotDb); }
    static float bandLoHz(int j);

    // saved profile
    float profileDb(int k) const;
    void setProfileDb(int k, float db);
    void setLearned(bool l) { learned_ = l; }

    // one hop: 128 samples per channel, in and out may not alias
    void process(const float *inL, const float *inR, float *outL, float *outR);

    unsigned resets = 0;                 // safety net count (non-finite state found and cleared)

private:
    void fft(float *re, float *im) const;
    void computeThresholdCurve();
    void finishLearn(bool commit);
    void estimateProfile(float *dstPow) const;
    void placeNodes(const float *pow);
    float localDb(const float *pow, float hz, float halfOct) const;
    void rebuildNodeInterp();
    void clearState();
    void gainStage(const float *P, float redK);
    void trackNoise(const float *P);
    void maskFloor(const float *P, float *g) const;

    // tables
    float wa_[kN], ws_[kN];                       // analysis / synthesis windows
    float cosT_[kN / 2], sinT_[kN / 2];           // twiddles
    uint16_t rev_[kN];                            // bit reversal
    float norm_;                                  // power normalisation: a full-scale sine's bin = 0 dB
    float candHz_[kNodes][kNodePos];
    int bandK0_[kBands], bandK1_[kBands];         // bins in a spectrum column ([k0,k1), or k1 = -1: interpolate)
    float bandW_[kBands];
    int interpA_[kBins];                          // node offsets interpolated per bin: offA*w + offB*(1-w)
    float interpW_[kBins];
    int interpB_[kBins];

    // signal state
    float histL_[kN], histR_[kN];
    float re_[kN], im_[kN];
    float olaL_[kHop], olaR_[kHop];
    float gPrev_[kBins], gamPrev_[kBins], gRel_[kBins], gain_[kBins];
    float spp_[kBins], relS_[kBins];
    float noise_[kBins], thrScale_[kBins], sppT_[kBins];   // tracked noise, Threshold x node curve, tracker's p
    int bark_[kBins];                                     // critical band of each bin (masking)
    int nBark_ = 0;
    float spreadTab_[32][32];                             // Schroeder spreading, linear power
    int smW_[kBins];                                      // wideHf: half-width of the gain smoothing per bin

    // profile and threshold
    float profPow_[kBins];
    float thrPow_[kBins];
    bool learned_ = false;
    float thrT_ = 0, thrS_ = 0;                   // target / smoothed Threshold (dB)
    float redT_ = 12, redS_ = 12;                 // Reduction (dB)
    float offT_[kNodes], offS_[kNodes];           // node offsets (dB)
    int nodePos_[kNodes];
    bool curveDirty_ = true;

    // learn
    bool learning_ = false;
    int subHop_ = 0, subsDone_ = 0;
    float subAcc_[kBins];
    float subMean_[kSubs][kBins];
    float previewPow_[kBins];                     // live estimate while learning (drives the nodes on screen)
    bool previewValid_ = false;

    // modes
    bool bypass_ = false, diff_ = false;
    float aPass_ = 0, aDiff_ = 0;                 // crossfade positions
    float outT_ = 1, outS_ = 1;                   // output gain (linear), glided per sample

    // spectrum preview
    float bandPeak_[kBands], dispDb_[kBands];
    int dispHop_ = 0;
};

}  // namespace shh
