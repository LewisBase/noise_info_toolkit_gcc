import numpy as np
from scipy.signal import butter, freqz, sosfreqz, sosfilt, lfilter, welch
import warnings; warnings.filterwarnings("ignore")

fs=48000.0
fc_list=[63,125,250,500,1000,2000,4000,8000,16000]
# current header coeffs (b0,b1,b2,a1,a2)
CUR=[(9.5390391335e-04,0.0,-9.5390391335e-04,-1.9980242497e+00,9.9809219217e-01),
(1.8908930810e-03,0.0,-1.8908930810e-03,-1.9959509956e+00,9.9621821384e-01),
(3.7746622011e-03,0.0,-3.7746622011e-03,-1.9913838874e+00,9.9245067560e-01),
(7.5210425735e-03,0.0,-7.5210425735e-03,-1.9807078862e+00,9.8495791485e-01),
(1.4930642120e-02,0.0,-1.4930642120e-02,-1.9532826157e+00,9.7013871576e-01),
(2.9428556760e-02,0.0,-2.9428556760e-02,-1.8749798052e+00,9.4114288648e-01),
(5.7224150414e-02,0.0,-5.7224150414e-02,-1.6326271778e+00,8.8555169917e-01),
(1.0861040606e-01,0.0,-1.0861040606e-01,-8.8707935581e-01,7.8277918789e-01),
(1.9830691563e-01,0.0,-1.9830691563e-01,8.4578766884e-01,6.0338616874e-01)]

N=1<<16
f=np.linspace(0,fs/2,N,endpoint=False)
def enbw(H): return np.trapezoid(np.abs(H)**2,x=f)/ (fs/2) * (fs/2)  # = integral

def H_cur(i):
    b0,b1,b2,a1,a2=CUR[i]; return freqz([b0,b1,b2],[1.0,a1,a2],worN=f,fs=fs)[1]

def design(order, half_oct):
    """half_oct: band edge ratio exponent (1/6 -> 1/3-oct wide; 1/2 -> full octave)"""
    r=2.0**half_oct
    return [butter(order,[fc/r,fc*r],btype='band',fs=fs,output='sos') for fc in fc_list]

idealoct=np.array([fc*(2**0.5-2**-0.5) for fc in fc_list])
print("="*100)
print("表 1  当前实现 vs 真倍频程：等效噪声带宽 (ENBW) 与频谱覆盖率")
print("="*100)
cur_e=np.array([enbw(H_cur(i)) for i in range(9)])
o3=design(3,0.5); o1oct=design(1,0.5); o2oct=design(2,0.5)
o3e=np.array([enbw(sosfreqz(s,worN=f,fs=fs)[1]) for s in o3])
o1oe=np.array([enbw(sosfreqz(s,worN=f,fs=fs)[1]) for s in o1oct])
print(f"{'fc':>6} | {'当前(1/3oct边,1节)':>18} | {'真倍频程边+1节':>14} | {'真倍频程边+3节':>14} | {'理想倍频程带宽':>13}")
for i,fc in enumerate(fc_list):
    print(f"{fc:>6} | {cur_e[i]:>18.1f} | {o1oe[i]:>14.1f} | {o3e[i]:>14.1f} | {idealoct[i]:>13.1f}")
print(f"{'Σ':>6} | {cur_e.sum():>18.0f} | {o1oe.sum():>14.0f} | {o3e.sum():>14.0f} | {idealoct.sum():>13.0f}")
print(f"{'覆盖率':>6} | {cur_e.sum()/24000:>18.3f} | {o1oe.sum()/24000:>14.3f} | {o3e.sum()/24000:>14.3f} | {idealoct.sum()/24000:>13.3f}")
print()
print(f"白噪预测缺口(Σband - LZeq):")
for nm,e in [("当前",cur_e),("真倍频程边+1节",o1oe),("真倍频程边+3节",o3e),("理想砖墙",idealoct)]:
    print(f"   {nm:>16}: {10*np.log10(e.sum()/24000):>+7.2f} dB")
print()

print("="*100)
print("表 2  阶数扫描（真倍频程边 ±1/2 oct）")
print("="*100)
print(f"{'阶数':>4} | {'biquad节数':>9} | {'ΣENBW':>8} | {'白噪缺口':>9} | {'1kHz带宽':>9}")
for o in [1,2,3,4,5,6]:
    ss=design(o,0.5); e=np.array([enbw(sosfreqz(s,worN=f,fs=fs)[1]) for s in ss])
    bwn=[sum(1 for s2 in s if True) for s in ss]
    print(f"{o:>4} | {o:>9} | {e.sum():>8.0f} | {10*np.log10(e.sum()/24000):>+8.2f}dB | {e[4]:>9.0f}")

print()
print("="*100)
print("表 3  真实录音上实测（repaired_fixed/00-05-05.wav, 48k, 9s）")
print("="*100)
from scipy.io import wavfile
sr,x=wavfile.read("/home/lewisbase/github/noise_info_toolkit_gcc/results/SES00003_260702/repaired_fixed/00-05-05.wav")
x=np.asarray(x,dtype=np.float64)
if x.ndim>1: x=x[:,0]
x/=32768.0
lzeq=10*np.log10(np.mean(x**2))
print(f"LZeq(宽带) = {lzeq:.2f} dBFS")
def sumbands(kind):
    tot=0.0
    for i in range(9):
        if kind=="cur":
            b0,b1,b2,a1,a2=CUR[i]; y=lfilter([b0,b1,b2],[1.0,a1,a2],x)
        elif kind=="o1oct":
            y=sosfilt(o1oct[i],x)
        else:
            y=sosfilt(o3[i],x)
        tot+=np.mean(y**2)
    return 10*np.log10(tot)
for nm in ["cur","o1oct","o3"]:
    lab={"cur":"当前(1/3oct边,1节)","o1oct":"真倍频程边+1节","o3":"真倍频程边+3节"}[nm]
    v=sumbands(nm)
    print(f"{lab:>20}: Σband = {v:>7.2f} dBFS   →  Σband - LZeq = {v-lzeq:>+6.2f} dB")

print()
print("="*100)
print("表 4  相邻带重叠检查（真倍频程 O3）：两带在交叉点各给多少")
print("="*100)
for i in range(8):
    fx=fc_list[i]*2**0.5
    h1=np.abs(sosfreqz(o3[i],worN=np.array([fx]),fs=fs)[1])**2
    h2=np.abs(sosfreqz(o3[i+1],worN=np.array([fx]),fs=fs)[1])**2
    print(f"交叉 {fx:>7.1f} Hz : band{i} = {10*np.log10(h1[0]):>+6.2f} dB, band{i+1} = {10*np.log10(h2[0]):>+6.2f} dB, 功率和 = {10*np.log10(h1[0]+h2[0]):>+6.2f} dB")
