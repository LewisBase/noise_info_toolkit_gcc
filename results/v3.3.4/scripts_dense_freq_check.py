import re, numpy as np
hdr=open('include/weighting_coefficients_multirate.hpp', encoding='utf-8').read()
def parse(n):
    m=re.search(rf'WeightingCoeffs {n}\[(\d+)\]\s*=\s*\{{(.*?)\n\}};',hdr,re.S)
    return np.array([[float(x.replace('f','')) for x in r.split(',')] for r in re.findall(r'\{([^}]*)\}',m.group(2))])
A,C=parse('A_48000'),parse('C_48000')
ga=float(re.search(r'A_48000,\s*\d+,\s*([0-9.eE+-]+)f',hdr).group(1))
gc=float(re.search(r'C_48000,\s*\d+,\s*([0-9.eE+-]+)f',hdr).group(1))
def A_ana(f):
    f2=f**2
    return 20*np.log10((12194.0**2*f2**2)/((f2+20.6**2)*np.sqrt((f2+107.7**2)*(f2+737.9**2))*(f2+12194.0**2)))+2.00
def C_ana(f):
    f2=f**2
    return 20*np.log10((12194.0**2*f2)/((f2+20.6**2)*(f2+12194.0**2)))+0.06
def resp(coef,g,fr):
    z1=np.exp(-1j*2*np.pi*np.asarray(fr,float)/48000).astype(np.complex128); r=np.ones_like(z1)
    for b0,b1,b2,a1,a2 in coef: r=r*((b0+b1*z1+b2*z1**2)/(1+a1*z1+a2*z1**2))
    return 20*np.log10(np.abs(r*g))

f=np.logspace(np.log10(10), np.log10(23900), 4000)
for nm,coef,g,ana in (("A",A,ga,A_ana),("C",C,gc,C_ana)):
    r=resp(coef,g,f); a=ana(f)
    r-= resp(coef,g,[1000.0])[0]; a-= ana(np.array([1000.0]))[0]
    d=r-a
    print(f"=== {nm} 计权：密集频点（10 Hz–23.9 kHz, 4000 点）与 IEC 解析式偏差 ===")
    print(f"    max|Δ| = {np.abs(d).max():.3f} dB @ {f[np.abs(d).argmax()]:.1f} Hz")
    for lo,hi in ((10,100),(100,1000),(1000,5000),(5000,10000),(10000,16000),(16000,20000),(20000,23900)):
        m=(f>=lo)&(f<hi)
        print(f"      {lo:>6}-{hi:<6} Hz : max|Δ| = {np.abs(d[m]).max():.3f} dB")
