import numpy as np
from scipy.signal import butter, sosfreqz
import warnings; warnings.filterwarnings("ignore")
fs=48000.0
fc_list=[63,125,250,500,1000,2000,4000,8000,16000]
N=1<<15
f=np.linspace(0,fs/2,N,endpoint=False)
def tot(soslist):
    e=[]
    for s in soslist:
        H=sosfreqz(s,worN=f,fs=fs)[1]; e.append(np.trapezoid(np.abs(H)**2,x=f))
    return np.array(e)

print("A) 只改阶数、不改边频（仍 ±1/6 oct）——证明「修阶数不解决问题」")
for o in [1,3]:
    r=2**(1/6); ss=[butter(o,[fc/r,fc*r],btype='band',fs=fs,output='sos') for fc in fc_list]
    e=tot(ss)
    print(f"   order {o}, ±1/6 oct 边:  ΣENBW={e.sum():>7.0f} Hz  白噪缺口={10*np.log10(e.sum()/24000):>+6.2f} dB")

print()
print("B) 只改边频、不改阶数（±1/2 oct, order 1）——证明「边频才是主因」")
r=2**0.5; ss=[butter(1,[fc/r,fc*r],btype='band',fs=fs,output='sos') for fc in fc_list]
e=tot(ss); print(f"   ΣENBW={e.sum():>7.0f} Hz  白噪缺口={10*np.log10(e.sum()/24000):>+6.2f} dB")

print()
print("C) 若产品真要 1/3 倍频程：应有 27 个带（2^(1/3) 间隔），9 个带覆盖不全")
c=[]
for k in range(-13,14):
    c.append(1000.0*2**(k/3))
c=[x for x in c if 20<=x<=20000]
r=2**(1/6); ss=[butter(3,[x/r,x*r],btype='band',fs=fs,output='sos') for x in c]
e=tot(ss)
print(f"   标准 1/3oct 中心频率个数 = {len(c)}  (20 Hz–20 kHz)")
print(f"   ΣENBW={e.sum():>7.0f} Hz  覆盖率={e.sum()/24000:.3f}  白噪缺口={10*np.log10(e.sum()/24000):>+6.2f} dB")
print(f"   只保留 9 个倍频程中心处的 1/3oct 带 (即当前设计):")
keep=[i for i,x in enumerate(c) if any(abs(x-fc)/fc<0.06 for fc in fc_list)]
print(f"      保留 {len(keep)} 个, ΣENBW={e[keep].sum():>7.0f} Hz  白噪缺口={10*np.log10(e[keep].sum()/24000):>+6.2f} dB")

print()
print("D) 资源代价（nRF54L15, float32）")
for nm,nb,sec in [("当前 9 带 × 1 节",9,1),("真倍频程 9 带 × 3 节",9,3),("1/3oct 27 带 × 3 节",27,3)]:
    print(f"   {nm:>22}: biquad={nb*sec:>3} 个, 状态变量={nb*sec*4*4:>5} 字节, 系数={nb*sec*5*4:>5} 字节, 每样本 MAC≈{nb*sec*5:>4} 次")
