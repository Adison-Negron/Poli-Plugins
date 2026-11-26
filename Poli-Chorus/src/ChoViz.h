// What the display box draws, from the same numbers the DSP runs on (ChoDSP.h): two LFOs, the left one black and the right one red.
//   cycles across the box   0.75 .. 3.25 (Rate, a log knob, so evenly spread over its travel)
//   amplitude               Amount 0..100 % -> 0..kVizAmpMax of the half height (a flat line at 0 %)
//   shape                   Classic / Vibrato = sine, Ensemble = a slow sine with a faster ripple on it (ChoMap::lfo)
//   red phase               the right LFO runs Width * 180 deg behind the left one (ChoMap::thetaCycles)
// Each column of the box is a meter whose frame is the wave's height there (frame (kVizFrames-1)/2 = the centre line).
#pragma once
#include "ChoDSP.h"
#include "layout_gen.h"

namespace cho {

inline float vizCycles(float ratePos) { return 0.75f + 2.5f * ChoMap::norm(ratePos); }

inline float vizValue(int mode, float ratePos, float amountPos, float widthPos, int col, bool right) {   // -1..1
    const float x = ((float)col + 0.5f) / (float)kVizCols;
    float cyc = vizCycles(ratePos) * x;
    if (right) cyc -= ChoMap::thetaCycles(widthPos);
    return kVizAmpMax * ChoMap::norm(amountPos) * ChoMap::lfo(mode, cyc);
}

inline int vizFrame(int mode, float ratePos, float amountPos, float widthPos, int col, bool right) {
    const float f = 0.5f + 0.5f * vizValue(mode, ratePos, amountPos, widthPos, col, right);
    int k = (int)lrintf(f * (float)(kVizFrames - 1));
    return k < 0 ? 0 : k > kVizFrames - 1 ? kVizFrames - 1 : k;
}

}  // namespace cho
