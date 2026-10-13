/**
 * @file filter_coefficients_48k.hpp
 * @brief Pre-computed A/C weighting filter coefficients for 48kHz sample rate
 *
 * Generated from the same algorithm as filter_design::a_weighting_design() and
 * filter_design::c_weighting_design() to ensure bit-exact equivalence.
 *
 * All coefficients are pre-normalized (a0 = 1.0) for use with BiquadChain,
 * which implements Transposed Direct Form II without a0 division.
 */

#pragma once

#include "iir_filter.hpp"
#include <cstddef>

namespace noise_toolkit {

/**
 * @brief Fixed-size chain of biquad sections with state
 *
 * Uses C arrays for constexpr compatibility. Implements cascade of
 * Transposed Direct Form II biquad sections.
 *
 * IMPORTANT: All coefficients must be pre-normalized (a0 = 1.0).
 * The process() function does NOT divide by a0.
 */
template<size_t SECTIONS>
struct BiquadChain {
    BiquadCoefficients sections[SECTIONS];
    float state[SECTIONS * 2];  // Transposed Direct Form II state (x1,x2 per section)

    /** @brief Reset all state to zero */
    void reset() {
        for (size_t i = 0; i < SECTIONS * 2; ++i) state[i] = 0.0f;
    }

    /** @brief Process a single sample through the chain (a0 must be 1.0) */
    float process(float input) {
        float x = input;
        for (size_t i = 0; i < SECTIONS; ++i) {
            const auto& s = sections[i];
            float* st = &state[i * 2];
            float y = s.b0 * x + st[0];
            st[0] = s.b1 * x - s.a1 * y + st[1];
            st[1] = s.b2 * x - s.a2 * y;
            x = y;
        }
        return x;
    }
};

//==============================================================================
// A-weighting 4-section biquad chain for 48kHz (pre-normalized, a0=1.0)
// v3.3.0: matched-z + 2nd-order peaking EQ correction → IEC 61672-1 Class 1
//==============================================================================

constexpr size_t A_WEIGHTING_SECTIONS = 4;

constexpr BiquadChain<A_WEIGHTING_SECTIONS> A_WEIGHTING_48K = {
    {
        // Section 0: High-frequency matched-z pole (12194 Hz analog)
        { 1.00000000f,  0.00000000f,  0.00000000f, 1.0f,
         -0.40532256f,  0.04107159f },
        // Section 1: Mid-frequency matched-z pole (737.86 + 107.65 Hz analog)
        { 1.00000000f, -2.00000000f,  1.00000000f, 1.0f,
         -1.89393899f,  0.89522729f },
        // Section 2: Low-frequency matched-z pole (20.6 Hz analog, double)
        { 1.00000000f, -2.00000000f,  1.00000000f, 1.0f,
         -1.99461446f,  0.99462171f },
        // Section 3: 2nd-order peaking EQ correction (differential evolution optimized)
        { 0.81132790f,  0.84654423f,  0.16617729f, 1.0f,
          0.84654423f, -0.02249481f }
    },
    {0}  // state: zero-initialized
};

//==============================================================================
// C-weighting 3-section biquad chain for 48kHz (pre-normalized, a0=1.0)
// v3.3.4: 复极点最小二乘拟合重设计（零点锁定 z=1）→ IEC 61672-1 Class 1
//         1 kHz 归一化增益（0.50568658）已内嵌到 Section 0 的 b 系数中。
// 修复前：C 计权高频偏高 —— 8 kHz 为 −1.09 dB（应为 −3.0 dB），与硬件实测一致。
// 修复后：max|Δ| = 0.158 dB（33 个 IEC 1/3 倍频程频点）。
// 注：A_WEIGHTING_48K 仍为未归一化形式（增益 0.75577396405 由调用方单独施加，
//     见 weighting_coefficients_multirate.hpp 的 WEIGHTING_TABLE）。
//==============================================================================

constexpr size_t C_WEIGHTING_SECTIONS = 3;

constexpr BiquadChain<C_WEIGHTING_SECTIONS> C_WEIGHTING_48K = {
    {
        // Section 0: 归一化双零点 (z=1) + 高频复极点对；b 已含 1 kHz 增益
        { 5.05686558e-01f, -1.01137312e+00f,  5.05686558e-01f, 1.0f,
          6.63510600e-02f,  1.25026270e-01f },
        // Section 1: 中频复极点对
        { 1.00000000f,  0.00000000f,  0.00000000f, 1.0f,
         -7.49203090e-01f,  1.72225700e-01f },
        // Section 2: 20.6 Hz 双重实极点
        { 1.00000000f,  0.00000000f,  0.00000000f, 1.0f,
         -1.99463560e+00f,  9.94642800e-01f }
    },
    {0}  // state: zero-initialized
};

} // namespace noise_toolkit
