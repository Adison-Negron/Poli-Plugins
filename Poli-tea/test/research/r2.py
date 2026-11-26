import numpy as np, teapy as t
from scipy.signal import resample_poly
FS=44100
def sine(f,n,a=0.1,ph=0): return (a*np.sin(2*np.pi*f*np.arange(n)/FS+ph)).astype(np.float32)
# 3. aliasing: 9.3 kHz and 15.1 kHz sines at max drive/amount: energy not at harmonics (folded)
for f0 in (3100, 9300, 15100):
    m=t.Tea(drive=1000,amount=1000,lthresh=1000,ldrive=0,output=1000); x=sine(f0,FS*2,0.5); o,_=m.run(x)
    seg=o[FS:FS+FS]*np.blackman(FS); O=np.abs(np.fft.rfft(seg))/FS*2
    f=np.fft.rfftfreq(FS,1/FS)
    # expected harmonic bins (folded): k*f0 mod fs
    exp=set()
    for k in range(1,12):
        fr=(k*f0)%FS; fr=FS-fr if fr>FS/2 else fr
        exp.add(int(round(fr)))
    mask=np.ones(len(O),bool)
    for e in exp: mask[max(0,e-6):e+7]=False
    mask[:20]=False
    print('f0 %5d: strongest non-harmonic line %.1f dBc (fundamental %.1f dBFS)'%(f0,t.db(O[mask].max()/O[int(f0)-1:int(f0)+2].max()),t.db(O[int(f0)-1:int(f0)+2].max())))
# 4. limiter: brickwall at ceiling with true-peak (8x) measure
def tp(x,os=8): return np.abs(resample_poly(x.astype(np.float64),os,1)).max()
rng=np.random.default_rng(3)
src=t.pink(FS*6,rms_db=-10)*1.0
for name,sig in (('pink -10dB',src),('drums-ish',None),('fs/4 isp',None)):
    if name=='drums-ish':
        sig=np.zeros(FS*6,np.float32)
        for k in range(0,FS*6-4000,11025): sig[k:k+3000]+=(0.9*np.exp(-np.arange(3000)/400)*np.sin(2*np.pi*80*np.arange(3000)/FS)).astype(np.float32)
        sig+=t.pink(FS*6,rms_db=-30)
    if name=='fs/4 isp':
        n=np.arange(FS*6); sig=(0.9*np.sin(2*np.pi*(FS/4)*n/FS+np.pi/4)).astype(np.float32)*np.where((n//20000)%2==0,1,0.3).astype(np.float32)
    for thr in (1000,500,0):
        for out in (1000,988,0):
            m=t.Tea(lthresh=thr,output=out,ldrive=700,amount=500); o,_=m.run(sig)
            ceil_db=-24+out*0.024
            print('%-10s lthresh %5.1f out %5.1f: sample peak %.2f dBFS  true peak %.2f dBFS (ceiling %.2f)  RMS %.1f'%(name,-18+thr*0.018,ceil_db,t.db(np.abs(o).max()),t.db(tp(o)),ceil_db,t.db(t.rms(o[FS:]))))
