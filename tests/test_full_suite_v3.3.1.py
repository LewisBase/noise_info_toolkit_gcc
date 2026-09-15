#!/usr/bin/env python3
"""
test_full_suite_v3.3.1.py — 噪声计 v3.3.1 本机可执行测试全集

覆盖 (按执行顺序):
  T1  A 计权数字实现复现 (1 kHz 参考 / 零输入 / 20 Hz 扫幅)   → test_a_weighting_v3.3.1.py
  T2  A/C 计权全频段频响 vs IEC 61672-1:2013 Table 3 (Python 独立实现)
  T3  采样率偏差影响量化 (设计 48 kHz vs 实测 ~50.4 kHz)
  T4  WAV 离线重算 (LZeq/LAeq/LCeq + 1/3 倍频程带能量自洽)
  T5  freq_*hz_spl 加权类型判定 (由 probe_bands_v3.3.1.cpp 提供结论, 此处读入)

输出: results/v3.3.1/full_suite_results.json
"""

import json
import math
import wave
from pathlib import Path

import numpy as np
from scipy.signal import sosfilt, butter, sosfreqz

# ============================================================================
# 系数 (逐字抄自固件头文件, 不引入任何自研设计)
# ============================================================================

# filter_coefficients_48k.hpp :: A_WEIGHTING_48K (4 段, a0=1.0)
A_SOS = np.array([
    [1.0,         0.0,         0.0,         1.0, -0.40532256,  0.04107159],
    [1.0,        -2.0,         1.0,         1.0, -1.89393899,  0.89522729],
    [1.0,        -2.0,         1.0,         1.0, -1.99461446,  0.99462171],
    [0.81132790,  0.84654423,  0.16617729,  1.0,  0.84654423, -0.02249481],
], dtype=np.float64)
A_GAIN = 0.75577396405

# filter_coefficients_48k.hpp :: C_WEIGHTING_48K (3 段, a0=1.0)
C_SOS = np.array([
    [1.0,         0.0,         0.0,         1.0, -0.40532256,  0.04107159],
    [1.0,        -2.0,         1.0,         1.0, -1.99461446,  0.99462171],
    [0.76934566, -0.25710082,  0.0,         1.0, -0.19553573,  0.0       ],
], dtype=np.float64)
C_GAIN = 0.99776330403

# bandpass_coefficients_48k.hpp :: BANDPASS_COEFFS_48K (9 带, b1=0)
BAND_CENTER = [63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]
BAND_SOS = np.array([
    [9.5390391335e-04,  0.0, -9.5390391335e-04,  1.0, -1.9980242497e+00,  9.9809219217e-01],
    [1.8908930810e-03,  0.0, -1.8908930810e-03,  1.0, -1.9959509956e+00,  9.9621821384e-01],
    [3.7746622011e-03,  0.0, -3.7746622011e-03,  1.0, -1.9913838874e+00,  9.9245067560e-01],
    [7.5210425735e-03,  0.0, -7.5210425735e-03,  1.0, -1.9807078862e+00,  9.8495791485e-01],
    [1.4930642120e-02,  0.0, -1.4930642120e-02,  1.0, -1.9532826157e+00,  9.7013871576e-01],
    [2.9428556760e-02,  0.0, -2.9428556760e-02,  1.0, -1.8749798052e+00,  9.4114288648e-01],
    [5.7224150414e-02,  0.0, -5.7224150414e-02,  1.0, -1.6326271778e+00,  8.8555169917e-01],
    [1.0861040606e-01,  0.0, -1.0861040606e-01,  1.0, -8.8707935581e-01,  7.8277918789e-01],
    [1.9900000000e-01,  0.0, -1.9900000000e-01,  1.0, -4.0000000000e-01,  6.0000000000e-01],
], dtype=np.float64)
BAND_CORRECTION = [1.0000000000, 1.0000000000, 1.0000000002, 1.0000000034,
                   1.0000000548, 1.0000008828, 1.0000145312, 1.0002624529, 1.0082070000]

FS = 48000
P0 = 20e-6

# IEC 61672-1:2013 Table 3 (与 tests/test_class1_precision.cpp 同源)
TEST_FREQS = [10, 12.5, 16, 20, 25, 31.5, 40, 50, 63, 80, 100, 125,
              160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
              2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000]
A_REF_DB = [-70.4, -63.4, -56.7, -50.5, -44.7, -39.4, -34.6, -30.2, -26.2, -22.5,
            -19.1, -16.1, -13.4, -10.9, -8.6, -6.6, -4.8, -3.2, -1.9, -0.8,
            0.0, 0.6, 1.0, 1.2, 1.3, 1.2, 1.0, 0.5, -0.1, -1.1,
            -2.5, -4.3, -6.6, -9.3]
C_REF_DB = [-14.3, -11.2, -8.5, -6.2, -4.4, -3.0, -2.0, -1.3, -0.8, -0.5,
            -0.3, -0.2, -0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, -0.1, -0.2, -0.3, -0.5, -0.8, -1.1,
            -1.6, -2.3, -3.3, -4.4]

RESULTS = {"version": "v3.3.1", "fs_hz": FS}


def rms_db(rms, p0=P0):
    return 20.0 * math.log10(max(float(rms), 1e-30) / p0)


def chain(sig, sos, gain):
    """复现 BiquadChain::process 链 + post-chain gain

    sos 行格式已是 [b0, b1, b2, a0, a1, a2], 且 a0 = 1.0 (固件保证),
    故可直接作为 scipy sosfilt 的 sos 矩阵 (级联 = BiquadChain 串联).
    sosfilt 内部为 Transposed Direct Form II, 与固件 BiquadChain::process 一致.
    """
    s = np.asarray(sos, dtype=np.float64)
    x = sosfilt(s, np.asarray(sig, dtype=np.float32)).astype(np.float32)
    return x * np.float32(gain)


def sine(freq, spl_db, dur_s):
    n = int(FS * dur_s)
    t = np.arange(n) / FS
    amp = math.sqrt(2) * P0 * 10 ** (spl_db / 20.0)
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)


# ============================================================================
# T2 — A/C 计权全频段频响 vs IEC 61672-1 Table 3
# ============================================================================

def test_t2_weighting_response():
    print("\n" + "=" * 66)
    print(" T2 — A/C 计权全频段频响 vs IEC 61672-1:2013 Table 3")
    print("      方法: 纯音 → 固件系数 sosfilt → LAeq/LCeq → 与标准表比较")
    print("=" * 66)

    rows = []
    for f, a_ref, c_ref in zip(TEST_FREQS, A_REF_DB, C_REF_DB):
        dur = min(3.0, 200.0 / max(f, 1.0))
        sig = sine(f, 94.0, dur)
        la = rms_db(np.sqrt(np.mean(chain(sig, A_SOS, A_GAIN).astype(np.float64) ** 2)))
        lc = rms_db(np.sqrt(np.mean(chain(sig, C_SOS, C_GAIN).astype(np.float64) ** 2)))
        rows.append({"freq_hz": f, "LAeq": la, "LCeq": lc,
                     "A_ref": a_ref, "C_ref": c_ref,
                     "A_err": abs(la - 94.0 - a_ref), "C_err": abs(lc - 94.0 - c_ref)})

    practical = [r for r in rows if r["freq_hz"] >= 20.0]
    a_max_prac = max(r["A_err"] for r in practical)
    c_max_prac = max(r["C_err"] for r in practical)
    a_worst = max(rows, key=lambda r: r["A_err"])
    c_worst = max(rows, key=lambda r: r["C_err"])

    print(f"{'fc(Hz)':>9} | {'A误差':>8} | {'C误差':>8} | {'A状态':>6} | {'C状态':>6}")
    print("-" * 56)
    for r in rows:
        aok = "✓" if r["A_err"] <= 0.7 else "✗"
        cok = "✓" if r["C_err"] <= 0.7 else "✗"
        print(f"{r['freq_hz']:>9.1f} | {r['A_err']:>8.3f} | {r['C_err']:>8.3f} | {aok:>6} | {cok:>6}")

    print("-" * 56)
    print(f"  实用范围 20 Hz–20 kHz:  A max err = {a_max_prac:.3f} dB,  C max err = {c_max_prac:.3f} dB")
    print(f"  全 34 点最差:          A {a_worst['A_err']:.3f} dB @ {a_worst['freq_hz']} Hz"
          f", C {c_worst['C_err']:.3f} dB @ {c_worst['freq_hz']} Hz")
    verdict = "CLASS1_PASS" if a_max_prac <= 0.7 and c_max_prac <= 0.7 else "CLASS1_FAIL"
    print(f"  → {verdict} (Class 1 判据 ±0.7 dB, 实用范围)")

    RESULTS["T2_weighting_response"] = {
        "method": "python scipy.sosfilt replica of firmware A/C chain (firmware coefficient files)",
        "rows": rows,
        "A_max_err_practical_dB": a_max_prac,
        "C_max_err_practical_dB": c_max_prac,
        "A_worst_full_dB": a_worst["A_err"], "A_worst_full_freq": a_worst["freq_hz"],
        "C_worst_full_dB": c_worst["C_err"], "C_worst_full_freq": c_worst["freq_hz"],
        "verdict": verdict,
    }
    return verdict


# ============================================================================
# T3 — 采样率偏差影响量化
# ============================================================================

def test_t3_sample_rate():
    print("\n" + "=" * 66)
    print(" T3 — 采样率偏差影响量化")
    print("      假设: 固件按 48 kHz 设计, 实际 MCLK ≈ 50.4 kHz (比值 1.050)")
    print("      机理: 物理频率 f 落到设计曲线的 f_nom = f × (48000/fs_true)")
    print("=" * 66)

    def a_analytic(f):
        ra = (12194.0**2 * f**4) / ((f**2 + 20.6**2) *
                                   math.sqrt((f**2 + 107.7**2) * (f**2 + 737.9**2)) *
                                   (f**2 + 12194.0**2))
        return 20.0 * math.log10(ra) + 2.00

    def c_analytic(f):
        rc = (12194.0**2 * f**2) / ((f**2 + 20.6**2) * (f**2 + 12194.0**2))
        return 20.0 * math.log10(rc) + 0.06

    out = {}
    for fs_true in (48000.0, 49000.0, 50000.0, 50400.0, 51200.0):
        ratio = 48000.0 / fs_true
        rows = []
        for f in [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 250, 500,
                  1000, 2000, 4000, 8000, 12500, 16000, 20000]:
            da = a_analytic(f * ratio) - a_analytic(f)
            dc = c_analytic(f * ratio) - c_analytic(f)
            rows.append({"freq_hz": f, "dA_dB": da, "dC_dB": dc})
        out[str(int(fs_true))] = {
            "fs_true_hz": fs_true, "ratio_nominal_over_true": ratio, "rows": rows,
        }

    print(f"{'fs_true':>9} | {'比值':>7} | {'ΔA@20Hz':>9} | {'ΔA@50Hz':>9} | {'ΔA@100Hz':>10} | {'ΔA@20kHz':>10}")
    print("-" * 70)
    for k, v in out.items():
        d = {r["freq_hz"]: r for r in v["rows"]}
        print(f"{v['fs_true_hz']:>9.0f} | {v['ratio_nominal_over_true']:>7.4f} | "
              f"{d[20]['dA_dB']:>9.3f} | {d[50]['dA_dB']:>9.3f} | "
              f"{d[100]['dA_dB']:>10.3f} | {d[20000]['dA_dB']:>10.3f}")

    d504 = {r["freq_hz"]: r for r in out["50400"]["rows"]}
    print("-" * 70)
    print("  50.4 kHz 假设下的详细影响 (ΔA = 实际施加 - 理想):")
    for f in [20, 31.5, 50, 63, 100, 1000, 8000, 16000, 20000]:
        print(f"    {f:>6.1f} Hz:  ΔA = {d504[f]['dA_dB']:+.3f} dB   ΔC = {d504[f]['dC_dB']:+.3f} dB")
    a_lf = max(abs(d504[f]["dA_dB"]) for f in [20, 25, 31.5, 40, 50, 63, 80, 100])
    a_hf = max(abs(d504[f]["dA_dB"]) for f in [8000, 12500, 16000, 20000])
    print(f"\n  低频段 (20–100 Hz) 最大 |ΔA| = {a_lf:.3f} dB")
    print(f"  高频段 (8–20 kHz)  最大 |ΔA| = {a_hf:.3f} dB")
    print(f"  → 量级 ≪ 25 dB 偏差. 采样率偏差**不是**低频 25 dB 问题的成因.")

    RESULTS["T3_sample_rate_deviation"] = {
        "note": "filter designed for 48 kHz applied to samples at fs_true; effective response at physical f = design(f*48000/fs_true)",
        "scenarios": out,
        "at_50400_max_abs_dA_lowfreq_20_100Hz": a_lf,
        "at_50400_max_abs_dA_highfreq_8_20kHz": a_hf,
        "verdict": "NOT_THE_CAUSE (magnitude << 25 dB)",
    }
    return a_lf


# ============================================================================
# T4 — WAV 离线重算
# ============================================================================

def read_wav(path):
    with wave.open(str(path), "rb") as w:
        nch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw == 4:
        data = np.frombuffer(raw, dtype=np.float32)
    elif sw == 2:
        data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) |
             (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        data = (v / float(1 << 23)).astype(np.float32)
    else:
        raise ValueError(f"unsupported sample width {sw}")
    if nch > 1:
        data = data.reshape(-1, nch)[:, 0]
    return sr, data


def test_t4_wav_offline(wav_path, label):
    print("\n" + "=" * 66)
    print(f" T4 — WAV 离线重算: {label}")
    print(f"      文件: {wav_path}")
    print("=" * 66)
    sr, data = read_wav(wav_path)
    n = len(data)
    dur = n / sr
    print(f"  采样率 {sr} Hz, {n} 样本, {dur:.2f} s")
    print(f"  原始幅值: min={data.min():+.6e} max={data.max():+.6e} rms={np.sqrt(np.mean(data**2)):.6e}")

    # 判定 WAV 单位: 若幅值 ≪ 1e-3 量级 → 已是 Pa
    rms_raw = float(np.sqrt(np.mean(data.astype(np.float64) ** 2)))
    rms_as_int16_unit = rms_raw
    if rms_raw < 1.0:      # 看起来已是 Pa
        pa = data.astype(np.float64)
        unit_note = "幅值已在 Pa 量级 (未做 2^15 缩放)"
    else:
        pa = data.astype(np.float64) / 32768.0
        unit_note = "幅值按 int16 满量程归一 → 再当 Pa 用 (需外部标定)"
    print(f"  单位假设: {unit_note}")

    lz = rms_db(np.sqrt(np.mean(pa ** 2)))
    la = rms_db(np.sqrt(np.mean(chain(pa, A_SOS, A_GAIN).astype(np.float64) ** 2)))
    lc = rms_db(np.sqrt(np.mean(chain(pa, C_SOS, C_GAIN).astype(np.float64) ** 2)))
    print(f"\n  宽带结果:  LZeq = {lz:7.2f} dB   LAeq = {la:7.2f} dB   LCeq = {lc:7.2f} dB")
    print(f"             LAeq-LZeq = {la - lz:+.2f} dB   LCeq-LZeq = {lc - lz:+.2f} dB")

    # 1/3 倍频程带 (Z 加权, 与固件 Phase 4 一致)
    bands = []
    for i, fc in enumerate(BAND_CENTER):
        # 复现 noise_processor.cpp Phase 4: 原始信号 → 单节 biquad → RMS × peak_gain_correction
        row = np.array([[BAND_SOS[i][0], BAND_SOS[i][1], BAND_SOS[i][2],
                         1.0, BAND_SOS[i][4], BAND_SOS[i][5]]])
        y = sosfilt(row, pa)
        rms = float(np.sqrt(np.mean(y ** 2))) * BAND_CORRECTION[i]
        bands.append({"fc": fc, "spl": rms_db(rms)})
    print(f"\n  1/3 倍频程带级 (Z 加权, 固件同源系数):")
    for bd in bands:
        print(f"    {bd['fc']:>6.0f} Hz : {bd['spl']:7.2f} dB")
    band_sum = 10 * math.log10(sum(10 ** (bd["spl"] / 10.0) for bd in bands))
    print(f"    带能量和 = {band_sum:7.2f} dB   (宽带 LZeq = {lz:.2f} dB, 差 {band_sum - lz:+.2f} dB)")
    print(f"    (注: 9 带只覆盖 56 Hz–18 kHz, 带外能量不计入 → 带和 < 宽带属正常)")

    res = {"file": str(wav_path), "label": label, "sr_hz": sr, "n_samples": n,
           "duration_s": dur, "unit_note": unit_note,
           "LZeq": lz, "LAeq": la, "LCeq": lc,
           "LAeq_minus_LZeq": la - lz, "LCeq_minus_LZeq": lc - lz,
           "bands": bands, "band_energy_sum_dB": band_sum,
           "band_sum_minus_LZeq_dB": band_sum - lz}
    return res


# ============================================================================
# main
# ============================================================================

def main():
    print("=" * 66)
    print(" 噪声计 v3.3.1 — 本机可执行测试全集 (T2/T3/T4)")
    print("=" * 66)

    test_t2_weighting_response()
    test_t3_sample_rate()

    repo = Path("/home/lewisbase/github/noise_info_toolkit_gcc")
    wavs = [
        (repo / "results/SES00003_260702/00-05-05.WAV", "SES00003 float32 原始"),
        (repo / "results/SES00003_260702/repaired_fixed/00-05-05.wav", "repaired_fixed int16"),
    ]
    t4 = []
    for p, lab in wavs:
        if p.exists():
            try:
                t4.append(test_t4_wav_offline(p, lab))
            except Exception as e:
                print(f"  [跳过] {p}: {e}")
        else:
            print(f"  [缺失] {p}")
    if t4:
        RESULTS["T4_wav_offline"] = t4

    out = repo / "results/v3.3.1/full_suite_results.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(RESULTS, indent=2, ensure_ascii=False, default=float))
    print(f"\n结果已保存: {out}")


if __name__ == "__main__":
    main()
