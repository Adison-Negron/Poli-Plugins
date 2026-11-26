// Poli-q Learn (1.1, source-aware since 1.1.2): listens for a few seconds and moves the current preset onto what it heard:
// a voice, drums, an instrument (leads, keys, guitars, pads, chords) or a bass.
//
// Idea taken from Antares Auto-Tune Vocal EQ, whose Learn finds the singer's range (low male, alto/tenor, soprano...)
// and highlights it, with dynamic bands (Threshold / Range / Attack / Release) and bands that can follow the voice.
// Poli-q's Learn measures, for any source:
//   - the long-term spectrum of everything it heard (1/6 octave) against its own 1-octave smooth: resonances (boom, mud,
//     box, ring, harsh) stick out as peaks, missing presence as dips; the lowest content that matters (where the low cut
//     can go), the brightness (how much air to add), the low-end balance (how much sub / punch to add);
//   - the pitch (YIN on the input, 61 Hz - 1 kHz): the range of a voice, a lead or a bass line; where the low cut can go
//     without thinning it, where the body / warmth sits;
//   - for a voice, the unvoiced loud frames (s, sh, t): the sibilance peak and the de-esser's Threshold;
//   - each dynamic band's level over the take, so its Threshold sits where the loud part starts.
// It decides what the source is from three numbers: how much of it is pitched, how often a new loud sound starts per second
// of sound (drums: lots, unpitched), and whether s / t / sh sounds sit between the pitched parts (a voice). The preset's
// family (Vox / Drm / Bass / Inst) breaks ties. It only runs while Learn is on (the button turns itself off when it has
// heard enough): nothing is analysed otherwise.
// Cost while learning: one 2048-point FFT + a 512-sample YIN every 1024 samples (about 23 ms), on the audio thread.
#pragma once
#include "PoliQ.h"

namespace pq {

constexpr int kLBins = 52;   // 1/6 octave from 40 Hz: centres 40 * 2^((i + 0.5) / 6), up to ~16 kHz

enum Source { kSrcNone = 0, kSrcVoice, kSrcDrums, kSrcInstrument, kSrcBass };
enum Hint { kHintNone = 0, kHintVoice, kHintDrums, kHintBass, kHintInstrument };   // from the preset's family

struct LearnResult {
    bool ok = false;               // enough sound was heard (and its kind decided)
    Source src = kSrcNone;
    int framesSignal = 0, framesVoiced = 0, framesUnvoiced = 0;
    float f0Lo = 0, f0Med = 0, f0Hi = 0;     // 5th / 50th / 95th percentile of the voiced pitch, Hz (0 = too little pitch)
    float ltasV[kLBins], devV[kLBins];       // voiced frames: mean level per bin (dB), and minus its 1-octave smooth
    float ltasU[kLBins], devU[kLBins];       // unvoiced loud frames (sibilance); -200 when there were none
    float ltasA[kLBins], devA[kLBins];       // every frame with signal (what drums and instruments are measured on)
    float pitchedFrac = 0;         // voiced frames / frames with signal
    float sibFrac = 0;             // unvoiced loud (s, sh, t) frames / frames with signal
    float onsetRate = 0;           // loud new sounds per second of sound
    float centroid = 0;            // spectral centroid of the long-term spectrum, Hz
    float lowHz = 0;               // lowest bin within 30 dB of the long-term spectrum's peak, Hz
    float bright = 0;              // 6-12 kHz minus 1-3 kHz, dB
};

// one band of a preset, as Learn sees it (and changes it)
struct AdaptBand {
    int type;
    bool on;
    float hz, gain, q;
    DynSettings dyn;
};

class Learner {
public:
    static constexpr int kN = 2048, kHop = 1024;
    static constexpr int kTargetFrames = 260;   // ~6 s of signal (voice or sibilance), then Learn switches itself off
    static constexpr int kWallFrames = 900;     // ~21 s in all, with silences: it stops with what it has
    static constexpr int kMaxFrames = 300;

    void start();
    void stop() { active_ = false; }
    bool active() const { return active_; }
    void push(const float *l, const float *r, int n);   // returns at once when not active
    float progress() const { return active_ ? (float)nStored_ / kTargetFrames : 0.0f; }
    bool done() const { return nStored_ >= kTargetFrames || hops_ >= kWallFrames; }
    int stored() const { return nStored_; }
    // hint: the preset's family (Hint), only used to break ties between a voice and an instrument
    void analyse(LearnResult &r, int hint = kHintNone) const;
    // the level (dB, like the dynamic band's detector) of every stored frame through a detector of this kind:
    // mode 1 band pass (hz, q), 2 low pass, 3 high pass, 4 everything. which: 0 all, 1 voiced only, 2 unvoiced only.
    int bandLevels(int mode, float hz, float q, int which, float *out, int max) const;
    static float binHz(int i);
    // move the bands onto what was heard; returns the text for the screen ("Alto/Tenor  140-310 Hz", "Drums  low end 45 Hz")
    static void adapt(AdaptBand b[kBands], const LearnResult &r, const Learner &L, char *note, int len);

private:
    void frame();
    float yin(const float *x) const;   // pitch in Hz of the decimated frame, 0 = unvoiced
    bool active_ = false;
    float ring_[kN] = {};
    int wpos_ = 0, sinceHop_ = 0, hops_ = 0, nStored_ = 0;
    int hopsSig_ = 0, onsets_ = 0;     // hops with sound, and how many of them started a new loud sound
    float prevLev_ = -120.0f;
    float win_[kN] = {}, winPow_ = 1;
    float re_[kN] = {}, im_[kN] = {};
    float frame_[kN] = {}, dec_[512] = {};   // scratch (kept off the audio thread's stack)
    float cosT_[kN / 2] = {}, sinT_[kN / 2] = {};
    int rev_[kN] = {};
    bool tables_ = false;
    float pow_[kMaxFrames][kLBins];    // mean-square per bin of each stored frame
    float f0_[kMaxFrames];             // its pitch (0 = unvoiced)
    unsigned char kind_[kMaxFrames];   // 1 voiced, 2 unvoiced loud (sibilance), 3 other signal
};

}  // namespace pq
