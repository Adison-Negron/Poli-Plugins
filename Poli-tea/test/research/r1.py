import numpy as np, teapy as t
from scipy.signal import resample_poly
FS=44100
def sine(f,n,a=0.1,ph=0): return (a*np.sin(2*np.pi*f*np.arange(n)/FS+ph)).astype(np.float32)
def level(o,s=slice(FS,None)): return t.rms(o[s])
print("latency samples:",t.lib.tea_latency())
# 1. flatness with everything neutral
flat=dict(amount=0,drive=0,lthresh=1000,ldrive=0,output=1000)
worst=0
for f in (25,40,60,100,120,140,200,500,1000,2000,2500,3000,5000,8000,12000,16000,19000):
    m=t.Tea(**flat); x=sine(f,FS*2); o,_=m.run(x)
    d=t.db(level(o))-t.db(level(x)); worst=max(worst,abs(d)); print(f,'Hz %.3f dB'%d, end=' | ')
print("\nworst flat dev %.3f dB"%worst)
# bit-exact-ish dry? null test vs delayed input
m=t.Tea(**flat); x=t.pink(FS*3,rms_db=-14); o,_=m.run(x)
# flatness of band sum via spectral ratio
X=np.fft.rfft(x[FS:FS*3]); O=np.fft.rfft(o[FS:FS*3]); f=np.fft.rfftfreq(FS*2,1/FS)
H=np.abs(O/X); sel=(f>30)&(f<19000)
print("spectral |H| range 30..19k: %.3f .. %.3f dB"%(t.db(H[sel].min()),t.db(H[sel].max())))
# 2. saturation: THD of 1k sine at various drive
for drv in (0,300,600,1000):
    for amt in (500,1000):
        m=t.Tea(drive=drv,amount=amt,lthresh=1000,ldrive=0,output=1000); x=sine(1000,FS*2,0.3); o,_=m.run(x)
        O=np.abs(np.fft.rfft(o[FS:FS+FS]*np.hanning(FS)))/FS; fs_=np.arange(len(O))
        h=[O[k*1000] for k in (1,2,3,4,5)]; 
        print('drive %d%% amount %d%%: H2 %.1f H3 %.1f H4 %.1f H5 %.1f dBc'%(drv/10,amt/5,*[t.db(v/h[0]) for v in h[1:]]))
