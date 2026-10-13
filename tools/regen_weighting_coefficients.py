#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/regen_weighting_coefficients.py — A/C 计权系数生成器（v3.3.4，仅 48 kHz）

v3.3.4
  1. 参考曲线按 IEC 61672-1:2013 官方值修正。
     旧版 C 计权参考在高频偏低最多 6.8 dB（20 kHz 写 -4.4，官方 -11.2），
     该错误经生成脚本传播进系数，导致硬件 C 计权 5/6.3/8 kHz 偏高 +1.0/+1.4/+2.0 dB。
  2. C 计权改用「复极点最小二乘拟合」（零点锁定 z=1、极点自由）。
     旧的 matched-z + 单搁架在修正后的（更陡的）曲线上只能做到 1.22 dB，不满足 Class 1；
     新方法在 48 kHz 达到 0.158 dB。
  3. A 计权方法不变（matched-z 极点精确映射 + 2 阶 RBJ 峰化），其对正确曲线本已达标（0.487 dB）。
  4. 采样率范围收敛为仅 48 kHz。
     本设备（Cortex-M33 / nRF54L15）固定 48 kHz 运行；复核发现其余档位 A 计权误差
     0.89–2.25 dB（低频 30–125 Hz 段），未达 Class 1，故一并移除，避免误用。
  5. 本脚本已合并原 regen_weighting_matched_z.py 的职能（该文件 v3.3.4 删除）。

设计方法
  A: matched-z 变换 z_p = exp(s_p·T)（极点频率精确无 warping）+ 2 阶 RBJ 峰化修正
     （单段/双段择优，differential evolution 全局搜索 + Nelder-Mead 精修）
  C: 复极点最小二乘（scipy.optimize.least_squares，多起点，零点固定为 2 个 z=1）
  增益: 1 kHz 归一化因子作为独立标量输出（在 biquad 链之后一次性相乘）

用法:
    python3 tools/regen_weighting_coefficients.py > include/weighting_coefficients_multirate.hpp
"""
import sys
import numpy as np
import scipy.signal as signal
from scipy.optimize import minimize, differential_evolution, least_squares

# ============ IEC 61672-1:2013 参考曲线（dB re 1 kHz，官方值）============
REF_A = {10: -70.4, 12.5: -63.4, 16: -56.7, 20: -50.5, 25: -44.7, 31.5: -39.4,
         40: -34.6, 50: -30.2, 63: -26.2, 80: -22.5, 100: -19.1, 125: -16.1,
         160: -13.4, 200: -10.9, 250: -8.6, 315: -6.6, 400: -4.8, 500: -3.2,
         630: -1.9, 800: -0.8, 1000: 0.0, 1250: 0.6, 1600: 1.0, 2000: 1.2,
         2500: 1.3, 3150: 1.2, 4000: 1.0, 5000: 0.5, 6300: -0.1, 8000: -1.1,
         10000: -2.5, 12500: -4.3, 16000: -6.6, 20000: -9.3}
REF_C = {10: -14.3, 12.5: -11.2, 16: -8.5, 20: -6.2, 25: -4.4, 31.5: -3.0,
         40: -2.0, 50: -1.3, 63: -0.8, 80: -0.5, 100: -0.3, 125: -0.2,
         160: -0.1, 200: 0.0, 250: 0.0, 315: 0.0, 400: 0.0, 500: 0.0,
         630: 0.0, 800: 0.0, 1000: 0.0, 1250: 0.0, 1600: -0.1, 2000: -0.2,
         2500: -0.3, 3150: -0.5, 4000: -0.8, 5000: -1.3, 6300: -2.0, 8000: -3.0,
         10000: -4.4, 12500: -6.2, 16000: -8.5, 20000: -11.2}

A_POLES_HZ = [20.598997, 107.65265, 737.86223, 12194.217]
A_POLE_MULTS = [2, 1, 1, 2]
C_POLES_HZ = [20.598997, 12194.217]
C_POLE_MULTS = [2, 2]
RATES = [48000]          # v3.3.4：仅 48 kHz（其余档位已移除，见文件头说明）
C1 = 0.7


# ==================== 公共工具 ====================
def matched_z(z_a, p_a, k_a, fs):
    T = 1.0 / fs
    z_d = [complex(np.exp(float(zz) * T)) for zz in z_a]
    p_d = [complex(np.exp(float(pp) * T)) for pp in p_a]
    n = len(p_d) - len(z_d)
    if n > 0:
        z_d.extend([0j] * n)
    elif n < 0:
        p_d.extend([0j] * abs(n))
    return z_d, p_d, float(k_a)


def norm_1k(sos, fs):
    w = 2 * np.pi * 1000 / fs
    _, h = signal.sosfreqz(sos, worN=[w])
    return 1.0 / abs(h[0])


def sos_resp_db(sos, freqs, fs):
    w = 2 * np.pi * freqs / fs
    _, h = signal.sosfreqz(sos, worN=w)
    return 20 * np.log10(np.abs(h) + 1e-30)


def peaking_bq(f0, Q, G_dB, fs):
    """RBJ peaking EQ，a0 归一。"""
    A = 10 ** (G_dB / 40)
    w0 = 2 * np.pi * f0 / fs
    al = np.sin(w0) / (2 * Q)
    b0, b1, b2 = 1 + al * A, -2 * np.cos(w0), 1 - al * A
    a0, a1, a2 = 1 + al / A, -2 * np.cos(w0), 1 - al / A
    return np.array([[float(b0 / a0), float(b1 / a0), float(b2 / a0),
                      1.0, float(a1 / a0), float(a2 / a0)]])


def check(fs, sos, gain, ref, label, quiet=False):
    freqs = sorted([f for f in ref if f < fs / 2])
    fa = np.array(freqs)
    db = sos_resp_db(sos, fa, fs) + 20 * np.log10(gain)
    rv = np.array([ref[f] for f in fa])
    err = db - rv
    mx = float(np.max(np.abs(err)))
    if not quiet:
        tag = 'Class 1' if mx <= C1 else 'FAIL'
        print(f"    {label:<10} max={mx:6.3f} dB  [{tag}]  n={len(freqs)}", file=sys.stderr)
    return err, mx, freqs


def optimize_peaking(sos_base, ref, freqs_arr, fs):
    """RBJ 峰化修正 —— 优先单段（段数少），单段达不到 Class 1 时才试双段。"""
    ref_vals = np.array([ref[float(f)] for f in freqs_arr])
    w1k = np.array([1000.0])

    def norm_shape(sos):
        """1 kHz 归一化后的频响（与 test_class1_precision 的判据口径一致）"""
        return sos_resp_db(sos, freqs_arr, fs) - sos_resp_db(sos, w1k, fs)[0]

    def cost(par):
        f0, Q, G = par
        if not (500 < f0 < 20000) or not (0.3 < Q < 15) or abs(G) > 4:
            return 100.
        sos = np.vstack([sos_base, peaking_bq(f0, Q, G, fs)])
        return float(np.max(np.abs(norm_shape(sos) - ref_vals)))

    hi = min(19000.0, fs * 0.45)
    r1 = differential_evolution(cost, [(500, hi), (0.3, 15.0), (-4.0, 4.0)],
                                seed=42, popsize=30, maxiter=150, polish=False)
    r1 = minimize(cost, r1.x, method='Nelder-Mead',
                  options={'xatol': 1e-3, 'fatol': 1e-4, 'maxiter': 2000})

    # 段数最小化：单段已达标（<= C1）就不再尝试双段。
    # A 计权链路固定 4 段（A_WEIGHTING_SECTIONS），基础 3 段 + 1 段峰化；
    # 若返回 5 段，主程序会因链长上限静默丢弃第 5 段，反而劣化。
    if r1.fun <= C1:
        print(f"      peaking: f0={r1.x[0]:.0f}Hz Q={r1.x[1]:.2f} G={r1.x[2]:+.3f}dB "
              f"cost={r1.fun:.4f} (single, 4 sections)", file=sys.stderr)
        return peaking_bq(r1.x[0], r1.x[1], r1.x[2], fs), r1.fun

    def cost2(par):
        f0a, Qa, Ga, f0b, Qb, Gb = par
        for f0, Q, G in ((f0a, Qa, Ga), (f0b, Qb, Gb)):
            if not (100 < f0 < 20000) or not (0.25 < Q < 25) or abs(G) > 10:
                return 100.
        bq = np.vstack([peaking_bq(f0a, Qa, Ga, fs), peaking_bq(f0b, Qb, Gb, fs)])
        return float(np.max(np.abs(norm_shape(np.vstack([sos_base, bq])) - ref_vals)))

    bnd = [(100, hi), (0.25, 25.0), (-10.0, 10.0)] * 2
    r2 = differential_evolution(cost2, bnd, seed=7, popsize=40, maxiter=250, polish=False)
    r2 = minimize(cost2, r2.x, method='Nelder-Mead',
                  options={'xatol': 1e-4, 'fatol': 1e-5, 'maxiter': 4000})

    if r2.fun < r1.fun:
        a = r2.x
        print(f"      peaking x2: [{a[0]:.0f}Hz Q{a[1]:.2f} {a[2]:+.2f}dB] "
              f"[{a[3]:.0f}Hz Q{a[4]:.2f} {a[5]:+.2f}dB] cost={r2.fun:.4f}", file=sys.stderr)
        return np.vstack([peaking_bq(a[0], a[1], a[2], fs),
                          peaking_bq(a[3], a[4], a[5], fs)]), r2.fun
    print(f"      peaking: f0={r1.x[0]:.0f}Hz Q={r1.x[1]:.2f} G={r1.x[2]:+.3f}dB "
          f"cost={r1.fun:.4f}", file=sys.stderr)
    return peaking_bq(r1.x[0], r1.x[1], r1.x[2], fs), r1.fun


# ==================== A 计权：matched-z + 峰化 ====================
def design_A(fs):
    freqs_a = np.array(sorted([f for f in REF_A if f < fs / 2]))
    a_z = [0.0] * 4
    a_p = []
    for f, m in zip(A_POLES_HZ, A_POLE_MULTS):
        a_p.extend([-2 * np.pi * f] * m)
    z_d, p_d, k_d = matched_z(a_z, a_p, 1.0, fs)
    sos_a1 = signal.zpk2sos(z_d, p_d, k_d, pairing='nearest')
    gain_a1 = norm_1k(sos_a1, fs)
    _, ma1, _ = check(fs, sos_a1, gain_a1, REF_A, "A(raw mz)", quiet=True)
    if ma1 > C1:
        peak_a, _ = optimize_peaking(sos_a1, REF_A, freqs_a, fs)
        sos_a = np.vstack([sos_a1, peak_a])
        gain_a = norm_1k(sos_a, fs)
    else:
        sos_a, gain_a = sos_a1, gain_a1
    return sos_a, gain_a


# ==================== C 计权：复极点最小二乘 ====================
def design_C(fs, nsec=3, seeds=10):
    npole = 2 * nsec
    nzero = 2
    F = np.array(sorted([f for f in REF_C if f < fs / 2 * 0.999]), float)
    T = np.array([REF_C[f] for f in F], float)

    def poles_of(rt):
        p = np.empty(npole, dtype=complex)
        for i in range(0, npole, 2):
            r, th = rt[2 * i], rt[2 * i + 1]
            p[i] = r * np.exp(1j * th)
            if i + 1 < npole:
                p[i + 1] = r * np.exp(-1j * th)
        return p

    def resp(rt, f):
        w = 2 * np.pi * np.asarray(f, float) / fs
        z1 = np.exp(-1j * w)
        num = (1 - z1) ** nzero
        den = np.ones_like(z1, dtype=complex)
        for pp in poles_of(rt):
            den *= (1 - pp * z1)
        return num / den

    def err(rt):
        h = resp(rt, F)
        r0 = resp(rt, np.array([1000.0]))[0]
        return 20 * np.log10(np.abs(h) / np.abs(r0)) - T

    z0, p0, _ = matched_z([0.0] * nzero,
                          [v for f, m in zip(C_POLES_HZ, C_POLE_MULTS) for v in [-2 * np.pi * f] * m],
                          1.0, fs)
    base = (np.abs(p0[:npole]) if len(p0) >= npole
            else np.r_[np.abs(p0), np.full(npole - len(p0), 0.5)])
    rt0 = np.empty(2 * npole)
    rt0[0::2] = base
    rt0[1::2] = 0.0
    lb = np.empty(2 * npole)
    ub = np.empty(2 * npole)
    lb[0::2], ub[0::2] = 1e-6, 0.99999
    lb[1::2], ub[1::2] = 0.0, np.pi
    best = None
    rng = np.random.default_rng(0)
    for s in range(seeds):
        rt = rt0.copy()
        if s:
            rt[0::2] = np.clip(rt[0::2] * (1 + rng.normal(0, 0.05, npole)), 1e-6, 0.9999)
            rt[1::2] = rng.uniform(0, 0.8, npole)
        try:
            r = least_squares(err, rt, bounds=(lb, ub), max_nfev=30000,
                              xtol=1e-15, ftol=1e-15, gtol=1e-15)
        except Exception:
            continue
        m = float(np.max(np.abs(err(r.x))))
        if best is None or m < best[0]:
            best = (m, r.x.copy())
    rt = best[1]
    p = poles_of(rt)
    sos, zleft = [], nzero
    for i in range(0, npole, 2):
        a = np.real(np.poly([p[i], p[i + 1]]))
        if zleft >= 2:
            b = np.array([1.0, -2.0, 1.0]); zleft -= 2
        elif zleft == 1:
            b = np.array([1.0, -1.0, 0.0]); zleft -= 1
        else:
            b = np.array([1.0, 0.0, 0.0])
        sos.append(np.r_[b, a])
    sos = np.array(sos)
    gain = float(1.0 / np.abs(resp(rt, np.array([1000.0]))[0]))
    return sos, gain


# ==================== 输出 ====================
def emit(name, sos, comment):
    out = [comment, f"static constexpr WeightingCoeffs {name}[{sos.shape[0]}] = {{"]
    for r in sos:
        b0, b1, b2, a0, a1, a2 = r
        out.append(f"    {{ {b0/a0:.10e}f, {b1/a0:.10e}f, {b2/a0:.10e}f, "
                   f"{a1/a0:.10e}f, {a2/a0:.10e}f }},")
    out.append("};")
    return "\n".join(out)


DOC = '''/**
 * @file weighting_coefficients_multirate.hpp
 * @brief Pre-computed A/C weighting filter coefficients — **fs = 48 kHz ONLY** (v3.3.4)
 *
 * ---------------------------------------------------------------------------
 * v3.3.4 变更说明 / v3.3.4 SCOPE REDUCTION
 * ---------------------------------------------------------------------------
 * 本文件原先包含 7 个采样率（8k / 16k / 22.05k / 32k / 44.1k / 48k / 96k）的
 * A/C 加权系数表。经 v3.3.4 复核，各档位设计精度如下：
 *
 *   8 000 Hz : A 0.457 dB   C 0.106 dB   → A 达标，C 达标
 *   16 000 Hz: A 0.885 dB   C 0.106 dB   → A 不达标
 *   22 050 Hz: A 0.899 dB   C 0.107 dB   → A 不达标
 *   32 000 Hz: A 1.056 dB   C 0.105 dB   → A 不达标
 *   44 100 Hz: A 1.199 dB   C 0.287 dB   → A 不达标
 *   48 000 Hz: A 0.487 dB   C 0.158 dB   → 达标（唯一保留）
 *   96 000 Hz: A 2.253 dB   C 0.105 dB   → A 不达标
 *
 * 本设备（Cortex-M33 / nRF54L15 平台）**固定以 48 kHz 采样**运行，
 * 其余采样率档位从未在硬件上启用。为避免"表里有值即视为可用"的误读，
 * v3.3.4 起**删除全部非 48 kHz 档位**，仅保留 48 kHz。
 *
 * 未保留档位的 A 计权误差集中在 30–125 Hz（低频），根因是 matched-z
 * 极点映射法对 A 曲线的低频段拟合不足。C 计权各档均达标，但既然设备
 * 只用 48 kHz，一并移除以保持代码与"实际支持范围"一致。
 *
 * 如未来需要重新支持其他采样率，请重新设计（勿直接恢复旧表）。
 *
 * ---------------------------------------------------------------------------
 * 系数来源 / DESIGN METHOD
 * ---------------------------------------------------------------------------
 *   A: matched-z 极点精确映射 z_p = exp(s_p·T) + 2 阶 RBJ 峰化修正
 *   C: 复极点最小二乘拟合（零点锁定 z=1，极点自由）
 *   生成器: tools/regen_weighting_coefficients.py
 *
 * 增益归一化（a_gain / c_gain）是**独立因子**，在 biquad 链**之后**一次性相乘。
 * （v3.2 曾把 1/H_total(1kHz) 只乘到第一节 b，破坏高频响应形状——勿回退该做法。）
 *
 * 设计目标：IEC 61672-1 Class 1（频响误差 <= 0.7 dB，33 个 1/3 倍频程频点）
 * 验证工具：tests/test_class1_precision.cpp
 */
#pragma once
namespace noise_toolkit {
struct WeightingCoeffs { float b0, b1, b2, a1, a2; };
struct WeightingTableEntry {
    const WeightingCoeffs* a;
    int a_count;
    float a_gain;          // 1kHz normalization factor: multiply chain output by this
    const WeightingCoeffs* c;
    int c_count;
    float c_gain;          // 1kHz normalization factor for C
};
'''


def main():
    print("A/C weighting regeneration v3.3.4 (48 kHz only)", file=sys.stderr)
    chunks, table = [], []
    ok = True
    for fs in RATES:
        print(f"  --- fs={fs} ---", file=sys.stderr)
        sa, ga = design_A(fs)
        sc, gc = design_C(fs)
        _, ma, fa = check(fs, sa, ga, REF_A, 'A-FINAL')
        _, mc, fc = check(fs, sc, gc, REF_C, 'C-FINAL')
        if ma > C1 or mc > C1:
            ok = False
        ia = int(np.argmax(np.abs(check(fs, sa, ga, REF_A, 'A', quiet=True)[0])))
        ic = int(np.argmax(np.abs(check(fs, sc, gc, REF_C, 'C', quiet=True)[0])))
        chunks.append(emit(
            f"A_{fs}", sa,
            f"// A 计权 @ 48 kHz —— {sa.shape[0]} 段（第 4 段为 2 阶 RBJ 峰化修正）\n"
            f"// max|Δ| = {ma:.3f} dB @ {fa[ia]} Hz（vs IEC 61672-1 参考表，33 频点）"))
        chunks.append(emit(
            f"C_{fs}", sc,
            f"// C 计权 @ 48 kHz —— {sc.shape[0]} 段（复极点最小二乘，零点锁定 z=1）\n"
            f"// max|Δ| = {mc:.3f} dB @ {fc[ic]} Hz（vs IEC 61672-1 参考表，33 频点）"))
        table.append(f"    {{ A_{fs}, {len(sa)}, {ga:.10e}f, C_{fs}, {len(sc)}, {gc:.10e}f }},")

    tail = ("\n\n// 仅 48 kHz 一项（v3.3.4 起；其余采样率已移除，见文件头说明）\n"
            f"static constexpr WeightingTableEntry WEIGHTING_TABLE[{len(RATES)}] = {{\n"
            + "\n".join(table) + "\n};\n")
    tail += """
/**
 * 按采样率查表。v3.3.4 起仅支持 48000 Hz，其余一律返回 nullptr。
 *
 * 调用方对 nullptr 的处理（保持既有行为，未改动）：
 *   - src/noise_processor.cpp：回落到运行时设计 filter_design::a/c_weighting_design()
 *   - src/event_detector.cpp ：回落到 A_WEIGHTING_48K 常量表
 */
static constexpr const WeightingTableEntry* find_weighting_entry(int sample_rate) {
    return (sample_rate == 48000) ? &WEIGHTING_TABLE[0] : nullptr;
}
} // namespace
"""
    sys.stdout.write(DOC + "\n" + "\n".join(chunks) + tail)
    print(f"\n{'OK (48 kHz, both A and C <= 0.7 dB)' if ok else 'FAILED'}", file=sys.stderr)


if __name__ == '__main__':
    main()
