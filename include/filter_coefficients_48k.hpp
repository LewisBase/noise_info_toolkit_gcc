/**
 * @file filter_coefficients_48k.hpp
 * @brief Biquad chain helper + A/C weighting section counts (48 kHz)
 *
 * v3.3.4 —— 本文件原先内嵌了一份 48 kHz 的 A/C 计权系数副本
 * （A_WEIGHTING_48K / C_WEIGHTING_48K）。那两份副本与
 * include/weighting_coefficients_multirate.hpp 的 WEIGHTING_TABLE 重复，
 * 其中 A 副本还是「未归一化」形式（1 kHz 增益需调用方单独施加），于是：
 *   - src/signal_utils.cpp 的 A 计权路径漏乘增益 → 偏高 +2.43 dB
 *   - src/event_detector.cpp 的非 48 kHz 回落路径把增益写死为 1.0 → 同样偏高
 *   - 且随着 v3.3.4 重新设计系数，两份副本会不断漂移（第二份 C 副本在 8 kHz
 *     同样给出 -1.09 dB，正是硬件实测偏高 +2.0 dB 的来源之一）
 * 故 v3.3.4 删除这两份副本，全部调用点统一改用 WEIGHTING_TABLE
 * （系数与 1 kHz 增益成对提供，单一数据源）。
 *
 * 本文件现在只保留与「链结构」有关的内容：
 *   - BiquadChain<SECTIONS> 模板（转置直接 II 型，零堆分配）
 *   - A_WEIGHTING_SECTIONS / C_WEIGHTING_SECTIONS 段数常量
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

// A/C 计权链的最大段数（链路容量上限；实际系数见 weighting_coefficients_multirate.hpp）
// 注意：生成器输出必须与这里的容量对齐，否则装表时会被静默截断。
constexpr size_t A_WEIGHTING_SECTIONS = 4;
constexpr size_t C_WEIGHTING_SECTIONS = 3;

} // namespace noise_toolkit
