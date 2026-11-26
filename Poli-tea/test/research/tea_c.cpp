// C wrapper around TeaDSP for the Python research / tuning scripts (ctypes). Not part of the plugin.
#include "TeaDSP.h"
using namespace tea;
extern "C" {
void *tea_new(float sr) { TeaDSP *d = new TeaDSP(); d->init(sr); return d; }
void tea_free(void *p) { delete static_cast<TeaDSP *>(p); }
void tea_param(void *p, int id, float pos) { static_cast<TeaDSP *>(p)->setParam(id, pos); }
void tea_ext(void *p, int on) { static_cast<TeaDSP *>(p)->setExternalSidechain(on != 0); }
void tea_process(void *p, const float *l, const float *r, float *ol, float *or_, int n, const float *kl, const float *kr) {
    TeaDSP *d = static_cast<TeaDSP *>(p);
    for (int i = 0; i < n; i += 128) {
        int m = n - i < 128 ? n - i : 128;
        d->process(l + i, r + i, ol + i, or_ + i, m, kl ? kl + i : nullptr, kr ? kr + i : nullptr);
    }
}
void tea_meters(void *p, float *out6) { Meters m; static_cast<TeaDSP *>(p)->takeMeters(m); out6[0]=m.inPeak; out6[1]=m.inRms; out6[2]=m.grLow; out6[3]=m.grMid; out6[4]=m.grHigh; out6[5]=m.grLim; }
int tea_latency() { return TeaDSP::latencySamples(); }
}
