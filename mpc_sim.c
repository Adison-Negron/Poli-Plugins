/* Mock MPC host for checking what the screen shows vs what the engine holds (see MPC_PLUGIN_TOOLCHAIN.md section 7).
 * Build (from C:\MPC-PLUGINS, after gen_vst.py <Plugin>/vst/vst.json --params-h):
 *   g++ -c -I<Plugin>/src -Impc-vst-plugins/wrapper <Plugin>/src/*.cpp; gcc -c -I<Plugin>/vst/build -Impc-vst-plugins/wrapper mpc-vst-plugins/wrapper/vst2_wrap.c
 *   gcc -c mpc_sim.c; g++ *.o -lm -lpthread -o mpc_sim
 * Param 3 is the toggle under test: change the index for another plugin. */
/* Mock of MPC's host for one 2-option toggle (Diff, param 3): MPC keeps the value it last sent and draws the
 * toggle from it (on = >= 0.5); it re-reads the plugin only when the plugin asks for audioMasterUpdateDisplay
 * (screen rebuild) or reports a value with audioMasterAutomate. A Q-Link event = MPC's value +/- 1/128. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
  void (*process)(AEffect *, float **, float **, int32_t); void (*setParameter)(AEffect *, int32_t, float);
  float (*getParameter)(AEffect *, int32_t); int32_t numPrograms, numParams, numInputs, numOutputs, flags;
  intptr_t resvd1, resvd2; int32_t initialDelay, realQualities, offQualities; float ioRatio; void *object, *user;
  int32_t uniqueID, version; void (*processReplacing)(AEffect *, float **, float **, int32_t);
  void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t); char future[56]; };
AEffect *VSTPluginMain(audioMasterCallback);
static float host_val = 0; static int updates = 0; static AEffect *fx;
static intptr_t master(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float opt) {
  if (op == 0 && idx == 3) host_val = opt;          /* automate: host takes the reported value */
  if (op == 42) { updates++; if (fx) host_val = fx->getParameter(fx, 3); }   /* rebuild: re-read */
  if (op == 1) return 2400;                          /* version */
  return 0; }
int main(void) {
  fx = VSTPluginMain(master);
  fx->dispatcher(fx, 0, 0, 0, 0, 0);                 /* open */
  float bufL[128] = {0}, bufR[128] = {0}, oL[128], oR[128], *in[2] = {bufL, bufR}, *out[2] = {oL, oR};
  for (int b = 0; b < 4; b++) fx->processReplacing(fx, in, out, 128);
  host_val = fx->getParameter(fx, 3);
  int mismatch = 0, flips = 0, last_eng = fx->getParameter(fx, 3) >= 0.5f;
  int dirs[] = {+1, +1, +1, +1, +1, +1, -1, -1, -1, -1, -1, -1, +1, +1, +1, +1, -1, -1, -1, -1};
  for (unsigned k = 0; k < sizeof dirs / sizeof *dirs; k++) {
    float v = host_val + dirs[k] / 128.0f; if (v < 0) v = 0; if (v > 1) v = 1;
    host_val = v; fx->setParameter(fx, 3, v);
    for (int b = 0; b < 40; b++) fx->processReplacing(fx, in, out, 128);   /* ~116 ms: deferred host calls run */
    int eng = fx->getParameter(fx, 3) >= 0.5f, shown = host_val >= 0.5f;
    if (eng != last_eng) flips++; last_eng = eng;
    printf("Q-Link %+d: engine %-3s  screen %-3s %s\n", dirs[k], eng ? "ON" : "OFF", shown ? "ON" : "OFF", eng != shown ? "<-- INVERTED" : "");
    mismatch += eng != shown;
  }
  printf("=> %d inverted states, %d flips in %zu Q-Link events, %d UpdateDisplay\n", mismatch, flips, sizeof dirs / sizeof *dirs, updates);
  return 0; }
