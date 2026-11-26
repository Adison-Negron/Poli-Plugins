import numpy as np, teapy as t, sys
from scipy.signal import resample_poly
FS=44100
def tp(x,os=8): return np.abs(resample_poly(x.astype(np.float64),os,1)).max()
src=t.pink(FS*6,rms_db=-10)
drums=np.zeros(FS*6,np.float32)
for k in range(0,FS*6-4000,11025): drums[k:k+3000]+=(0.9*np.exp(-np.arange(3000)/400)*np.sin(2*np.pi*80*np.arange(3000)/FS)).astype(np.float32)
drums+=t.pink(FS*6,rms_db=-30)
n=np.arange(FS*6)
isp=(0.9*np.sin(2*np.pi*(FS/4)*n/FS+np.pi/4)).astype(np.float32)*np.where((n//20000)%2==0,1,0.3).astype(np.float32)
mix=(src*0.5+drums*0.7+0.3*np.sin(2*np.pi*(FS*0.31)*n/FS)).astype(np.float32)
worst=-99; rows=[]
for name,sig in (('pink',src),('drums',drums),('fs/4',isp),('mix',mix)):
    for thr,out,ld in ((500,988,300),(300,988,700),(0,988,700),(0,500,1000)):
        m=t.Tea(lthresh=thr,output=out,ldrive=ld,amount=500); m.run(np.zeros(FS,np.float32))
        o,_=m.run(sig); s=o[FS//2:]; ceil=-24+out*0.024; ov=t.db(tp(s))-ceil; worst=max(worst,ov)
        rows.append((name,-18+thr*0.018,ceil,ld/10,ov,t.db(t.rms(s[FS:]))))
for r in rows: print('%-6s thr %5.1f ceil %5.1f drive %3d: TP over ceiling %+.2f dB  RMS %.1f'%r)
print('WORST TP overshoot %+.2f dB'%worst)
