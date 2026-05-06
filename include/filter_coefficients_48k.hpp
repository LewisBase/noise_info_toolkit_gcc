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
// A-weighting 3-section biquad chain for 48kHz (pre-normalized, a0=1.0)
//==============================================================================

constexpr size_t A_WEIGHTING_SECTIONS = 3;

constexpr BiquadChain<A_WEIGHTING_SECTIONS> A_WEIGHTING_48K = {
    {
        // Section 0: High-frequency poles at -76617 rad/s (double pole)
        // Original a0=4.10353756, normalized
        { 4.91024574f, -9.82049148f, 4.91024574f, 1.0f,
          0.02539252f, 0.00016120f },
        // Section 1: Mid-frequency poles at -4636 and -676.7 rad/s
        // Original a0=1.055719018, normalized
        { 0.94722174f, 0.0f, -0.94722174f, 1.0f,
          -1.89379803f, 0.89508891f },
        // Section 2: Low-frequency poles at -129.4 rad/s (double pole)
        // Original a0=1.002697587, normalized
        { 0.99730967f, -1.99461934f, 0.99730967f, 1.0f,
          -1.99461577f, 0.99462285f }
    },
    {0}  // state: zero-initialized
};

//==============================================================================
// C-weighting 2-section biquad chain for 48kHz (pre-normalized, a0=1.0)
//==============================================================================

constexpr size_t C_WEIGHTING_SECTIONS = 2;

constexpr BiquadChain<C_WEIGHTING_SECTIONS> C_WEIGHTING_48K = {
    {
        // Section 0: High-frequency shelf
        // Original a0=4.732050419, normalized
        { 0.05172280f, 0.05172280f, 0.0f, 1.0f,
          0.57735023f, 0.0f },
        // Section 1: Low-frequency poles at -129.4 rad/s
        // Original a0=1.002697587, normalized
        { 0.99730967f, 0.0f, -0.99730967f, 1.0f,
          -1.99461577f, 0.99462285f }
    },
    {0}  // state: zero-initialized
};

} // namespace noise_toolkit
