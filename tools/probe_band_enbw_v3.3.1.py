import importlib.util, math
import numpy as np
spec = importlib.util.spec_from_file_location('fs','tests/test_full_suite_v3.3.1.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

print("频带中心:", [int(c) for c in m.BAND_CENTER], "  ← 倍频程间隔(1/1)")
print()
fs = 48000.0
f = np.linspace(1, fs/2, 200000)
tot_enbw = 0.0
print(" fc(Hz) | 峰值增益 | 等效噪声带宽 | 理想倍频程带宽 | 覆盖比")
for i, fc in enumerate(m.BAND_CENTER):
    b0,b1,b2,_,a1,a2 = m.BAND_SOS[i]
    z = np.exp(-2j*np.pi*f/fs)
    H = (b0 + b1*z + b2*z**2) / (1 + a1*z + a2*z**2)
    H2 = np.abs(H)**2
    # 峰值增益
    pk = H2.max()
    # 等效噪声带宽 (Hz)
    enbw = np.trapezoid(H2, f)
    lo, hi = fc/math.sqrt(2), fc*math.sqrt(2)
    ideal = hi - lo
    tot_enbw += enbw
    print(f"{fc:7.0f} | {math.sqrt(pk):8.4f} | {enbw:11.0f} | {ideal:13.0f} | {enbw/ideal:7.3f}")

# 理想: 9 个倍频程带盖 44.7Hz .. 22.6kHz, 白噪下 Σ = 全带宽
print()
print(f"Σ ENBW(9) = {tot_enbw:.0f} Hz ;  Nyquist = {fs/2:.0f} Hz")
print(f"白噪预测缺口 = 10·log10(ΣENBW / (fs/2)) = {10*math.log10(tot_enbw/(fs/2)):+.2f} dB")
