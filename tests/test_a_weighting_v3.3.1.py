#!/usr/bin/env python3
"""
test_a_weighting_v3.3.1.py — Python 复现固件 A 计权, 验证 68.7 dBA 低频底限假说

目的:
  对应噪声计工程 v3.3.1 排查阶段 (嵌工+邱教授诊断 → 第一步本机验证).
  用 scipy.signal.sosfilt (内部 = Transposed Direct Form II, 与固件 BiquadChain
  ::process 完全一致) 复现固件 A 计权, 跑三个判定测试.

系数出处:
  - filter_coefficients_48k.hpp     → A_WEIGHTING_48K (4 段 biquad, a0=1.0)
  - weighting_coefficients_multirate.hpp → 48000 entry → a_gain = 0.75577396405

判定逻辑:
  - Test A (1 kHz 参考):  diff 应 < 0.5 dB, 否则系数抄错
  - Test B (零输入):     输出应 ~ -∞ dBA. 若 > -40 dBA → 算法层有 floor/clamp
  - Test C (20 Hz 扫):   输出 - 输入 应 ≈ -50.5 dB (正确)
                                   或 ≈ -25.0 dB (旁路泄漏假说, Qiu)
"""

import json
import numpy as np
from pathlib import Path
from scipy.signal import sosfilt

# ---- 系数 (v3.3.1, 与固件 include/filter_coefficients_48k.hpp A_WEIGHTING_48K 一致) ----
A_SOS = np.array([
    # [b0, b1, b2, a0, a1, a2]
    [1.0,           0.0,            0.0,            1.0, -0.40532256,  0.04107159],   # HF matched-z pole 12194 Hz
    [1.0,          -2.0,            1.0,            1.0, -1.89393899,  0.89522729],   # MF matched-z pole 737.86+107.65 Hz
    [1.0,          -2.0,            1.0,            1.0, -1.99461446,  0.99462171],   # LF matched-z pole 20.6 Hz double
    [0.81132790,   0.84654423,     0.16617729,     1.0,  0.84654423, -0.02249481],   # 2nd-order peaking EQ
], dtype=np.float64)

A_GAIN = 0.75577396405   # 来自 weighting_coefficients_multirate.hpp 48000 entry


def a_weighting(x: np.ndarray) -> np.ndarray:
    """复现 noise_processor.cpp Phase 2: a_buf[i] = chain.process(a_buf[i]) * a_gain"""
    out = x.astype(np.float32)
    for sec in A_SOS:
        out = sosfilt(sec, out).astype(np.float32)
    return out * A_GAIN


def rms_db(rms_pa: float, p0: float = 20e-6) -> float:
    return 20.0 * np.log10(max(rms_pa, 1e-30) / p0)


def spl_to_pa_amplitude(spl_dB: float, freq_hz: float, fs: int, duration_s: float):
    """给定声压级 → 生成正弦样本 (Pa 振幅, 对应固件 PCM 数值=Pa×ADC_gain)"""
    t = np.arange(int(fs * duration_s)) / fs
    p_rms = 20e-6 * 10**(spl_dB / 20.0)
    amp = np.sqrt(2) * p_rms
    return amp * np.sin(2 * np.pi * freq_hz * t)


# ============================================================================
# Test A: 1 kHz 参考校准 (A 加权在 1 kHz 处应 0 dB 衰减)
# ============================================================================
def test_a_1khz_calibration(fs=48000):
    t = np.arange(fs * 10) / fs
    p_rms = 20e-6 * 10**(94.1 / 20.0)
    x = np.sqrt(2) * p_rms * np.sin(2 * np.pi * 1000 * t)
    y = a_weighting(x)
    rms_in = np.sqrt(np.mean(x**2))
    rms_out = np.sqrt(np.mean(y**2))
    return {
        "input_spl_dB": 94.1,
        "out_rms_dBA": float(rms_db(rms_out)),
        "expected_dBA": 94.1,
        "diff_dB": float(rms_db(rms_out) - 94.1),
        "verdict": "OK" if abs(rms_db(rms_out) - 94.1) < 0.5 else "FAIL (系数抄错或 a_gain 不对)",
    }


# ============================================================================
# Test B: 零输入 (60 秒) — 看算法层是否有 floor
# ============================================================================
def test_b_zero_input(fs=48000, duration_s=60):
    n = fs * duration_s
    x = np.zeros(n, dtype=np.float32)
    y = a_weighting(x)
    rms = float(np.sqrt(np.mean(y**2)))
    return {
        "duration_s": duration_s,
        "n_samples": n,
        "out_max_abs": float(np.max(np.abs(y))),
        "out_rms_dBA": float(rms_db(rms)),
        "verdict": (
            "OK (算法干净)" if rms_db(rms) < -60
            else f"FLOOR (算法层有底噪, 约 {rms_db(rms):.1f} dBA)"
        ),
    }


# ============================================================================
# Test C: 20 Hz 多幅度扫描 — 区分"算法正确" vs "旁路泄漏"
# ============================================================================
def test_c_20hz_sweep(fs=48000, spl_levels=(74, 84, 94, 104), duration_s=10):
    rows = []
    for spl in spl_levels:
        x = spl_to_pa_amplitude(spl, freq_hz=20, fs=fs, duration_s=duration_s).astype(np.float32)
        y = a_weighting(x)
        out_rms = np.sqrt(np.mean(y**2))
        out_dBA = rms_db(out_rms)
        # 两种假说
        h_correct = spl - 50.5      # 算法正确: 严格按 IEC 61672-1
        h_bypass = spl - 25.0       # 邱教授旁路泄漏假说: 输入 − 25 dB
        rows.append({
            "spl_input_dB": float(spl),
            "out_rms_dBA": float(out_dBA),
            "hypothesis_correct_dBA": float(h_correct),
            "hypothesis_bypass_dBA": float(h_bypass),
            "diff_vs_correct": float(out_dBA - h_correct),
            "diff_vs_bypass": float(out_dBA - h_bypass),
        })
    # 综合判定: 看 diff 接近哪个假说
    avg_correct = float(np.mean([r["diff_vs_correct"] for r in rows]))
    avg_bypass = float(np.mean([r["diff_vs_bypass"] for r in rows]))
    if abs(avg_correct) < 5:
        verdict = "ALGORITHM_CORRECT (算法层无 25 dB 旁路, bug 在硬件或更上层)"
    elif abs(avg_bypass) < 5:
        verdict = "BYPASS_LEAK_CONFIRMED (算法层有 ~25 dB 旁路, 与邱教授诊断一致, bug 在固件)"
    else:
        verdict = f"NEITHER (正确假说偏 {avg_correct:+.1f} dB, 旁路假说偏 {avg_bypass:+.1f} dB)"
    return {"per_level": rows, "avg_diff_vs_correct_dB": avg_correct,
            "avg_diff_vs_bypass_dB": avg_bypass, "verdict": verdict}


# ============================================================================
def main():
    OUT = Path(__file__).resolve().parent.parent / "results" / "v3.3.1"
    OUT.mkdir(parents=True, exist_ok=True)

    print("=" * 64)
    print(" A-Weighting 算法 Python 复现测试 — 版本 v3.3.1")
    print("=" * 64)
    print(f" 系数源: include/filter_coefficients_48k.hpp A_WEIGHTING_48K (4 段)")
    print(f" 增益源: include/weighting_coefficients_multirate.hpp 48000 entry")
    print(f"        a_gain = {A_GAIN}")
    print(f" 实现:   scipy.signal.sosfilt = Transposed Direct Form II")
    print(f" 采样率: {48000} Hz")

    print("\n--- Test A: 1 kHz 参考校准 ---")
    a = test_a_1khz_calibration()
    print(f"  输入 94.1 dB SPL @ 1 kHz → 输出 {a['out_rms_dBA']:.2f} dBA "
          f"(diff {a['diff_dB']:+.2f} dB)  → {a['verdict']}")

    print("\n--- Test B: 零输入 (60 s) ---")
    b = test_b_zero_input()
    print(f"  输出最大幅值: {b['out_max_abs']:.2e}")
    print(f"  输出 RMS:     {b['out_rms_dBA']:.2f} dBA")
    print(f"  → {b['verdict']}")

    print("\n--- Test C: 20 Hz 多幅度 (10 s / level) ---")
    c = test_c_20hz_sweep()
    print(f"  {'输入 SPL':>9} | {'输出 dBA':>9} | {'理论A':>9} | {'旁路假说':>9} | "
          f"{'vs理论':>7} | {'vs旁路':>7}")
    print(f"  {'-'*9} | {'-'*9} | {'-'*9} | {'-'*9} | {'-'*7} | {'-'*7}")
    for r in c["per_level"]:
        print(f"  {r['spl_input_dB']:>9.1f} | {r['out_rms_dBA']:>9.2f} | "
              f"{r['hypothesis_correct_dBA']:>9.2f} | {r['hypothesis_bypass_dBA']:>9.2f} | "
              f"{r['diff_vs_correct']:>+7.2f} | {r['diff_vs_bypass']:>+7.2f}")
    print(f"\n  平均偏差 vs 理论: {c['avg_diff_vs_correct_dB']:+.2f} dB")
    print(f"  平均偏差 vs 旁路: {c['avg_diff_vs_bypass_dB']:+.2f} dB")
    print(f"  → {c['verdict']}")

    print("\n" + "=" * 64)
    print(" 综合判定")
    print("=" * 64)
    overall = []
    if "OK" in a["verdict"]: overall.append("✓ A: 系数/增益抄得对")
    else: overall.append("✗ A: 系数或增益不对, 先修这个")
    overall.append(f"{'✓' if 'OK' in b['verdict'] else '✗'} B: {b['verdict']}")
    overall.append(f"{'✓' if 'CORRECT' in c['verdict'] else ('✗' if 'CONFIRMED' in c['verdict'] else '?')} C: {c['verdict']}")
    for line in overall:
        print(f"  {line}")

    # ---- 落盘 ----
    payload = {
        "version": "v3.3.1",
        "fs_hz": 48000,
        "coefficients_source": "include/filter_coefficients_48k.hpp A_WEIGHTING_48K",
        "a_gain_source": "include/weighting_coefficients_multirate.hpp 48000 entry (a_gain=0.75577396405)",
        "implementation": "scipy.signal.sosfilt (Transposed Direct Form II, matches noise_toolkit::BiquadChain::process)",
        "test_a_1khz": a,
        "test_b_zero_input": b,
        "test_c_20hz_sweep": c,
    }
    js = OUT / "a_weighting_test_results.json"
    js.write_text(json.dumps(payload, ensure_ascii=False, indent=2))
    print(f"\n 结果已保存: {js}")


if __name__ == "__main__":
    main()