// probe: the engine as a small C library for test/preview.py (ctypes) and quick checks. Not shipped.
#include <cstdint>
#include <cstring>
extern "C" {
#include "engine.h"
int mpc_engine_changed(void *inst, char *key, int len);
void *pq_create() { return mpc_engine()->create(nullptr); }
void pq_destroy(void *p) { mpc_engine()->destroy(p); }
void pq_set(void *p, const char *k, const char *v) { mpc_engine()->set_param(p, k, v); }
int pq_get(void *p, const char *k, char *buf, int len) { return mpc_engine()->get_param(p, k, buf, len); }
void pq_process(void *p, const int16_t *in, int16_t *out, int frames) { mpc_engine()->process(p, in, out, frames); }
int pq_changed(void *p, char *k, int len) { return mpc_engine_changed(p, k, len); }
}
