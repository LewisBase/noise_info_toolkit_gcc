/**
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

// A 计权 @ 48 kHz —— 4 段（第 4 段为 2 阶 RBJ 峰化修正）
// max|Δ| = 0.483 dB @ 20000 Hz（vs IEC 61672-1 参考表，33 频点）
static constexpr WeightingCoeffs A_48000[4] = {
    { 1.0000000000e+00f, 0.0000000000e+00f, 0.0000000000e+00f, -4.0532255690e-01f, 4.1071593782e-02f },
    { 1.0000000000e+00f, -2.0000000000e+00f, 1.0000000000e+00f, -1.8939389909e+00f, 8.9522728880e-01f },
    { 1.0000000000e+00f, -2.0000000000e+00f, 1.0000000000e+00f, -1.9946144593e+00f, 9.9462171026e-01f },
    { 8.3123246068e-01f, 9.3971931426e-01f, 2.5414405722e-01f, 9.3971931426e-01f, 8.5376517899e-02f },
};
// C 计权 @ 48 kHz —— 3 段（复极点最小二乘，零点锁定 z=1）
// max|Δ| = 0.151 dB @ 16000 Hz（vs IEC 61672-1 参考表，33 频点）
static constexpr WeightingCoeffs C_48000[3] = {
    { 1.0000000000e+00f, -2.0000000000e+00f, 1.0000000000e+00f, 6.6351057740e-02f, 1.2502627202e-01f },
    { 1.0000000000e+00f, 0.0000000000e+00f, 0.0000000000e+00f, -7.4920308529e-01f, 1.7222569973e-01f },
    { 1.0000000000e+00f, 0.0000000000e+00f, 0.0000000000e+00f, -1.9946356027e+00f, 9.9464279692e-01f },
};

// 仅 48 kHz 一项（v3.3.4 起；其余采样率已移除，见文件头说明）
static constexpr WeightingTableEntry WEIGHTING_TABLE[1] = {
    { A_48000, 4, 7.5534559768e-01f, C_48000, 3, 5.0568657745e-01f },
};

/**
 * 按采样率查表。v3.3.4 起仅支持 48000 Hz，其余一律返回 nullptr。
 *
 * 调用方对 nullptr 的处理（保持既有行为，未改动）：
 *   - src/noise_processor.cpp：回落到运行时设计 filter_design::a/c_weighting_design()
 *   - src/event_detector.cpp ：默认填 48 kHz 表；非 48 kHz 时沿用该表
 *     （v3.3.4 起 A_WEIGHTING_48K 副本已删除，统一走本表）
 */
static constexpr const WeightingTableEntry* find_weighting_entry(int sample_rate) {
    return (sample_rate == 48000) ? &WEIGHTING_TABLE[0] : nullptr;
}
} // namespace
