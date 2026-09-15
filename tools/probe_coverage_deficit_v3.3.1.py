import importlib.util, math
import numpy as np, wave
from scipy.signal import sosfilt, welch

spec = importlib.util.spec_from_file_location('fs', 'tests/test_full_suite_v3.3.1.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

p = "results/SES00003_260702/repaired_fixed/00-05-05.wav"
with wave.open(p) as w:
    sr = w.getframerate(); n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64) / 32768.0
print(f"文件 {p}\n  sr={sr} n={n} dur={n/sr:.2f}s  rms={np.sqrt(np.mean(x**2)):.6f}")

def db(v): return 20*math.log10(v/2e-5) if v > 0 else float('-inf')

lz = db(np.sqrt(np.mean(x**2)))
bands = []
for i, fc in enumerate(m.BAND_CENTER):
    row = np.array([[m.BAND_SOS[i][0], m.BAND_SOS[i][1], m.BAND_SOS[i][2], 1.0, m.BAND_SOS[i][4], m.BAND_SOS[i][5]]])
    rms = float(np.sqrt(np.mean(sosfilt(row, x)**2))) * m.BAND_CORRECTION[i]
    bands.append(db(rms))
band_sum = 10*math.log10(sum(10**(b/10) for b in bands))
print(f"\n[固件同源] LZeq = {lz:.2f} dB   Σband(9) = {band_sum:.2f} dB   缺口 = {band_sum-lz:+.2f} dB")

# PSD 独立核算: 哪些能量在 9 带之外
f, P = welch(x, fs=sr, nperseg=32768)
def lvl(mask):
    pw = np.trapezoid(P[mask], f[mask])
    return 10*math.log10(pw) if pw > 0 else float('-inf')
inb  = lvl((f >= 56) & (f <= 18000))
low  = lvl(f < 56)
high = lvl(f > 18000)
tot  = lvl(f > 0)
print(f"\n[PSD 独立核算]")
print(f"  总能量      {tot:8.2f} dB")
print(f"  带内 56-18k {inb:8.2f} dB   (占总能量 {inb-tot:+.2f} dB)")
print(f"  带外 <56 Hz {low:8.2f} dB")
print(f"  带外 >18 kHz{high:8.2f} dB")
comb = 10*math.log10(10**((inb)/10) + 10**((low)/10) + 10**((high)/10))
print(f"  带内+带外合成 = {comb:.2f} dB  (应变回总能量, 差 {comb-tot:+.2f} dB)")
print(f"\n  PSD 带内 vs Σband(9) 差 = {inb - band_sum:+.2f} dB   ← 应为小值(两者都只含带内)")
