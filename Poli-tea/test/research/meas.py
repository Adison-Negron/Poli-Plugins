import numpy as np, teapy as t, sys
def avg_gr(sig, amt, **kw):
    m=t.Tea(amount=amt, drive=0, lthresh=1000, ldrive=0, output=1000, **kw)
    x=sig; B=128; grs=[]
    ol=np.zeros_like(x)
    for i in range(0,len(x)-B,B):
        o,_=m.run(x[i:i+B]); ol[i:i+B]=o
        grs.append(m.meters()[2:5].copy())
    grs=np.array(grs); k=int(2.5*44100/B)
    # mean of block-min is slightly pessimistic but consistent
    return grs[k:].mean(0), t.db(t.rms(ol[44100*2:]))-t.db(t.rms(x[44100*2:]))
if __name__=='__main__':
    x=t.pink(44100*8)
    for amt in (0,125,250,375,500,625,750,875,1000):
        g,d=avg_gr(x,amt)
        print('amount %3d%%  mean block-min GR low/mid/high %s  out-in %.2f dB'%(amt/5,np.round(g,2),d))
