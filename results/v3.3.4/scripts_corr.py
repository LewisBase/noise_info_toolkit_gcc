import re, csv, numpy as np
FS=48000
d=np.fromfile("/tmp/wav_10.f32", dtype=np.float32)
dev=list(csv.DictReader(open("results/SES00003_261010/261010-10.CSV")))
our=list(csv.DictReader(open("/tmp/v334_10.csv")))
n=min(len(dev),len(our))
f=np.fft.rfftfreq(FS,1/FS); mask=f>=5
hf=mask&(f>=20000); band=mask&(f<20000)
steps=list(range(0,n,10))
rows=[]
for i in steps:
    x=d[i*FS:(i+1)*FS].astype(np.float64)
    if x.size<FS: break
    P=(np.abs(np.fft.rfft(x))**2)*(2.0/(FS*FS))
    tot=P[mask].sum()
    if tot<=0: continue
    hf_frac=P[hf].sum()/tot
    dd=float(dev[i]['LCeq_dB'])-float(our[i]['LCeq_dB'])
    da=float(dev[i]['LAeq_dB'])-float(our[i]['LAeq_dB'])
    rows.append((hf_frac, dd, da, float(dev[i]['LZeq_dB'])))
R=np.array(rows)
print(f"样本 {len(R)} 秒（每 10 秒取 1）")
print(f"  >20kHz 能量占比: 中位 {np.median(R[:,0])*100:.2f}%  均值 {R[:,0].mean()*100:.2f}%  max {R[:,0].max()*100:.1f}%")
for name,col in (("LCeq差(设备-v334)",1),("LAeq差(设备-v334)",2)):
    c=np.corrcoef(R[:,0],R[:,col])[0,1]
    print(f"  {name} 与 >20kHz占比 的相关系数 r = {c:+.3f}")
lo=R[:,0]<0.1; hi=R[:,0]>0.5
print(f"  分组：>20kHz占比<10% 的 {lo.sum()} 秒 → LCeq差 均值{R[lo,1].mean():+.3f} 标准差{R[lo,1].std():.3f}")
if hi.sum(): print(f"        >20kHz占比>50% 的 {hi.sum()} 秒 → LCeq差 均值{R[hi,1].mean():+.3f} 标准差{R[hi,1].std():.3f}")
lo2=R[:,0]<0.1
print(f"        （同一分组）LAeq差 均值{R[lo2,2].mean():+.3f} 标准差{R[lo2,2].std():.3f}")
