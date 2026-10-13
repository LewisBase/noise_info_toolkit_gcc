import re, csv, numpy as np
FS=48000
hdr=open('include/weighting_coefficients_multirate.hpp',encoding='utf-8').read()
def parse(n):
    m=re.search(rf'WeightingCoeffs {n}\[(\d+)\]\s*=\s*\{{(.*?)\n\}};',hdr,re.S)
    return np.array([[float(x.replace('f','')) for x in r.split(',')] for r in re.findall(r'\{([^}]*)\}',m.group(2))])
A,C=parse('A_48000'),parse('C_48000')
ga=float(re.search(r'A_48000,\s*\d+,\s*([0-9.eE+-]+)f',hdr).group(1))
gc=float(re.search(r'C_48000,\s*\d+,\s*([0-9.eE+-]+)f',hdr).group(1))
def bq_resp(coef,g,f):
    z1=np.exp(-1j*2*np.pi*np.asarray(f,float)/FS); r=np.ones_like(z1,dtype=complex)
    for b0,b1,b2,a1,a2 in coef: r=r*((b0+b1*z1+b2*z1**2)/(1+a1*z1+a2*z1**2))
    return np.abs(r*g)
def A_ana(f):
    f2=f**2
    return 10**((20*np.log10((12194.0**2*f2**2)/((f2+20.6**2)*np.sqrt((f2+107.7**2)*(f2+737.9**2))*(f2+12194.0**2)))+2.00)/20)
def C_ana(f):
    f2=f**2
    return 10**((20*np.log10((12194.0**2*f2)/((f2+20.6**2)*(f2+12194.0**2)))+0.06)/20)

dev=list(csv.DictReader(open("results/SES00003_261010/261010-10.CSV")))
d=np.fromfile("/tmp/wav_10.f32", dtype=np.float32)
for idx in (1813, 1794, 1506):
    x=d[idx*FS:(idx+1)*FS].astype(np.float64)
    X=np.fft.rfft(x); f=np.fft.rfftfreq(FS, 1/FS)
    P=(np.abs(X)**2)*(2.0/ (FS*FS))     # 单边功率谱密度（相对均方）
    P[0]/=2
    m=f>0
    lz=10*np.log10(P[m].sum()/20e-6**2)
    ca_our=10*np.log10((P*m**0+bq_resp(C,gc,f)**2)[m].sum()/20e-6**2)
    ca_ideal=10*np.log10((P*C_ana(np.maximum(f,1))**2)[m].sum()/20e-6**2)
    aa_ideal=10*np.log10((P*A_ana(np.maximum(f,1))**2)[m].sum()/20e-6**2)
    print(f"\n=== 秒 {idx} ===")
    print(f"  LZeq:  我们 {lz:.2f} | 设备 {float(dev[idx]['LZeq_dB']):.2f}（差 {lz-float(dev[idx]['LZeq_dB']):+.2f}）")
    print(f"  LCeq:  我们(biquad) {ca_our:.2f} | IEC解析式 {ca_ideal:.2f} | 设备 {float(dev[idx]['LCeq_dB']):.2f}")
    print(f"         → 我们的C衰减 {ca_our-lz:+.2f} dB | 理论C衰减 {ca_ideal-lz:+.2f} dB | 设备C衰减 {float(dev[idx]['LCeq_dB'])-float(dev[idx]['LZeq_dB']):+.2f} dB")
    print(f"  LAeq:  IEC解析式 {aa_ideal:.2f} | 设备 {float(dev[idx]['LAeq_dB']):.2f}")
    # 倍频程能量分布
    print("  倍频程声级(由WAV FFT): ", end="")
    for fc in (63,125,250,500,1000,2000,4000,8000,16000):
        lo,hi=fc/2**0.5, fc*2**0.5
        mm=(f>=lo)&(f<hi)
        e=P[mm].sum()
        print(f"{fc}:{10*np.log10(e/20e-6**2):.1f} ", end="")
    print()
    print("  设备CSV倍频程:          ", end="")
    for k in ("freq_63hz_spl","freq_125hz_spl","freq_250hz_spl","freq_500hz_spl","freq_1khz_spl","freq_2khz_spl","freq_4khz_spl","freq_8khz_spl","freq_16khz_spl"):
        print(f"{float(dev[idx][k]):.1f} ", end="")
    print()
    # >20kHz 能量占比
    hi_m=f>=20000
    print(f"  >20 kHz 能量占比: {100*P[hi_m].sum()/P[m].sum():.2f}%  (>16kHz: {100*P[f>=16000].sum()/P[m].sum():.2f}%)")
