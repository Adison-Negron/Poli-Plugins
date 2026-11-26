"""ctypes access to TeaDSP for research / tuning: process(), pink noise, meters, band helpers."""
import ctypes, os, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.environ.get("LIBTEA", "/tmp/libtea.so")
lib = ctypes.CDLL(LIB)
lib.tea_new.restype = ctypes.c_void_p
lib.tea_new.argtypes = [ctypes.c_float]
lib.tea_free.argtypes = [ctypes.c_void_p]
lib.tea_param.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_float]
lib.tea_ext.argtypes = [ctypes.c_void_p, ctypes.c_int]
FP = ctypes.POINTER(ctypes.c_float)
lib.tea_process.argtypes = [ctypes.c_void_p, FP, FP, FP, FP, ctypes.c_int, FP, FP]
lib.tea_meters.argtypes = [ctypes.c_void_p, FP]
lib.tea_latency.restype = ctypes.c_int
FS = 44100
P = dict(input=0, drive=1, low=2, mid=3, high=4, amount=5, ldrive=6, lthresh=7, output=8)
DEF = dict(input=500, drive=300, low=500, mid=500, high=500, amount=500, ldrive=200, lthresh=889, output=988)

def ptr(a): return a.ctypes.data_as(FP)

class Tea:
    def __init__(self, **pos):
        self.p = lib.tea_new(FS)
        d = dict(DEF); d.update(pos)
        for k, v in d.items(): self.set(k, v)
    def set(self, k, v): lib.tea_param(self.p, P[k], float(v))
    def ext(self, on): lib.tea_ext(self.p, int(on))
    def run(self, l, r=None, key=None):
        l = np.ascontiguousarray(l, np.float32); r = l.copy() if r is None else np.ascontiguousarray(r, np.float32)
        ol = np.zeros_like(l); orr = np.zeros_like(r)
        kl = kr = None
        if key is not None:
            kl = np.ascontiguousarray(key[0], np.float32); kr = np.ascontiguousarray(key[1], np.float32)
        lib.tea_process(self.p, ptr(l), ptr(r), ptr(ol), ptr(orr), len(l), ptr(kl) if kl is not None else None, ptr(kr) if kr is not None else None)
        return ol, orr
    def meters(self):
        o = np.zeros(6, np.float32); lib.tea_meters(self.p, ptr(o)); return o
    def __del__(self):
        try: lib.tea_free(self.p)
        except Exception: pass

def db(x): return 20 * np.log10(np.maximum(x, 1e-12))
def rms(x): return float(np.sqrt(np.mean(np.square(x.astype(np.float64)))))
def pink(n, seed=1, rms_db=-18.0):
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n)
    W = np.fft.rfft(w); f = np.arange(len(W)); f[0] = 1
    W = W / np.sqrt(f); W[0] = 0
    x = np.fft.irfft(W, n)
    return (x / rms(x) * 10 ** (rms_db / 20)).astype(np.float32)
