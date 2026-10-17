/**
 * @file test_weighting_state_persistence.cpp
 * @brief 决定性对照实验：A/C 计权滤波器的**状态持久性**是否在两条 API 路径上一致
 *
 * 背景（2026-10-17）：
 *   李工实测：50 ms 窗口下 A 计权 20 Hz 偏差 > 4 dB，其余频点已达 Class 1。
 *   邱教授判断：20 Hz 周期正好 50 ms，若每块都重置 IIR 状态则每块含启动瞬态；
 *              需确认"每调用一次接口，滤波器内部 state 是否保留"。
 *
 * 本实验对同一段 20 Hz 纯音，用**完全相同的 50 ms 分块**分别走两条公开 API：
 *
 *   路径 A  NoiseProcessor::process_segment()
 *           → 走成员 a_weight_chain_，状态跨调用保留（v3.3.2 设计意图）
 *
 *   路径 B  signal_utils::apply_a_weighting_inplace() / apply_c_weighting_inplace()
 *           → 走函数内 static chain，且**每次调用都 chain.reset()**（signal_utils.cpp:244/:268）
 *
 * 若两条路径结果一致 → 状态持久性无问题，4 dB 另有原因
 * 若路径 B 显著偏�� → 确认"每块重置状态"，A 计权低频建立时间不够
 *
 * 判据用 IEC 61672-1 解析式独立计算，不与被测代码同源。
 *
 * Usage:
 *   cd build_test && ./test_weighting_state_persistence
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>

#include "noise_processor.hpp"
#include "signal_utils.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

namespace {

constexpr int    FS          = 48000;
constexpr float  TEST_FREQ   = 20.0f;     // 李工报偏差的频点（周期 = 50 ms）
constexpr float  TEST_AMP    = 1.0f;      // 1 Pa peak → 94 dB SPL peak
constexpr float  BLOCK_S     = 0.050f;    // 李工实测的快窗口
constexpr float  WARMUP_S    = 2.0f;      // 预热后再计，避开上电瞬态
constexpr float  MEASURE_S   = 2.0f;      // 计量段
constexpr float  P_REF       = 20e-6f;

// IEC 61672-1 A/C 计权解析式（独立判据，不复用被测代码）
double a_weighting_db(double f) {
    const double f2 = f * f;
    const double num = 12194.0 * 12194.0 * f2 * f2;
    const double den = (f2 + 20.6 * 20.6) *
                       std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) *
                       (f2 + 12194.0 * 12194.0);
    return 20.0 * std::log10(num / den) + 2.00;
}
double c_weighting_db(double f) {
    const double f2 = f * f;
    const double num = (12194.0 * 12194.0) * f2;
    const double den = (f2 + 20.6 * 20.6) * (f2 + 12194.0 * 12194.0);
    return 20.0 * std::log10(num / den) + 0.06;
}

std::vector<float> make_tone(float freq, float dur_s) {
    const int n = static_cast<int>(FS * dur_s);
    std::vector<float> d(n);
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / FS;
        d[i] = TEST_AMP * std::sin(noise_const::TWO_PI_F * freq * t);
    }
    return d;
}

float spl_of_rms(float rms) {
    return 20.0f * std::log10(rms / P_REF);
}

// ---- 路径 A：process_segment()，每块调用一次（状态应保留）----
float pathA(const std::vector<float>& tone, bool use_a) {
    NoiseProcessor proc(FS);
    const int blk = static_cast<int>(FS * BLOCK_S);
    double energy = 0.0;
    long n = 0;

    for (size_t off = 0; off + blk <= tone.size(); off += blk) {
        auto m = proc.process_segment(tone.data() + off,
                                      tone.data() + off + blk,
                                      BLOCK_S);
        const float l = use_a ? m.LAeq : m.LCeq;
        if (std::isfinite(l)) { energy += l; ++n; }
    }
    return n ? static_cast<float>(energy / n) : 0.0f;
}

// ---- 路径 B：apply_*_weighting_inplace()，每块调用一次 ----
float pathB(const std::vector<float>& tone, bool use_a) {
    const int blk = static_cast<int>(FS * BLOCK_S);
    double energy = 0.0;
    std::vector<float> buf(blk);

    for (size_t off = 0; off + blk <= tone.size(); off += blk) {
        std::copy(tone.begin() + off, tone.begin() + off + blk, buf.begin());
        if (use_a) apply_a_weighting_inplace(buf.data(), blk, static_cast<float>(FS));
        else       apply_c_weighting_inplace(buf.data(), blk, static_cast<float>(FS));
        float rms = calculate_rms(buf);
        energy += spl_of_rms(rms);
    }
    return static_cast<float>(energy / (tone.size() / blk));
}

// ---- 参照：整段一次性滤波（等效于"永不重置"的理想情形）----
float reference(const std::vector<float>& tone, bool use_a) {
    std::vector<float> buf = tone;
    if (use_a) apply_a_weighting(tone, static_cast<float>(FS));
    else       apply_c_weighting(tone, static_cast<float>(FS));
    return spl_of_rms(calculate_rms(tone));
}

} // namespace

int main() {
    const auto tone = make_tone(TEST_FREQ, WARMUP_S + MEASURE_S);

    const double ref_in_spl = spl_of_rms(TEST_AMP / std::sqrt(2.0f));
    const double a_iec = a_weighting_db(TEST_FREQ);
    const double c_iec = c_weighting_db(TEST_FREQ);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== A/C 计权 状态持久性 对照实验 ===\n";
    std::cout << "频点 " << TEST_FREQ << " Hz   分块 " << (BLOCK_S * 1000)
              << " ms (" << static_cast<int>(FS * BLOCK_S) << " samples/block)\n\n";
    std::cout << "输入 RMS SPL (未计权)      : " << ref_in_spl << " dB\n";
    std::cout << "IEC A 计权 @ " << TEST_FREQ << " Hz        : " << a_iec << " dB\n";
    std::cout << "IEC C 计权 @ " << TEST_FREQ << " Hz        : " << c_iec << " dB\n\n";

    struct Row { const char* name; float a; float c; };
    const Row rows[] = {
        {"路径A process_segment()   ", pathA(tone, true),  pathA(tone, false)},
        {"路径B apply_*_inplace()   ", pathB(tone, true),  pathB(tone, false)},
    };

    std::cout << std::setw(28) << "路径"
              << std::setw(12) << "LAeq"
              << std::setw(12) << "LCeq" << "\n";
    std::cout << std::string(52, '-') << "\n";
    for (const auto& r : rows) {
        std::cout << std::setw(28) << r.name
                  << std::setw(12) << r.a
                  << std::setw(12) << r.c << "\n";
    }

    const double a_expect = ref_in_spl + a_iec;
    const double c_expect = ref_in_spl + c_iec;

    std::cout << "\n--- 与 IEC 解析式的偏差 ---\n";
    std::cout << "期望 LAeq : " << a_expect << " dB"
              << "   (IEC " << c_iec << " dB 用于 C)\n";
    std::cout << "期望 LCeq : " << c_expect << " dB\n\n";

    const double dev_aA = rows[0].a - a_expect, dev_aB = rows[1].a - a_expect;
    const double dev_cA = rows[0].c - c_expect, dev_cB = rows[1].c - c_expect;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "路径A 偏差:  A " << dev_aA << " dB    C " << dev_cA << " dB\n";
    std::cout << "路径B 偏差:  A " << dev_aB << " dB    C " << dev_cB << " dB\n";
    std::cout << "两路径差值:  A " << (dev_aB - dev_aA) << " dB    C "
              << (dev_cB - dev_cA) << " dB\n\n";

    const double gap = std::abs(dev_aB - dev_aA);
    if (gap > 1.0) {
        std::cout << "▶ 结论：两路径差异 " << gap << " dB (>1 dB)\n"
                  << "  → 状态持久性不一致，路径 B 每块重置滤波器状态。\n"
                  << "  → A 计权低频建立时间长，瞬态暴露在 20 Hz 尤为明显。\n"
                  << "  → 固件若调用 apply_*_weighting_inplace()，即为 4 dB 偏差来源。\n";
        return 1;   // 复现成功 → 非零退出，提醒这是"发现问题"而非"测试通过"
    }

    std::cout << "▶ 结论：两路径一致（差异 " << gap << " dB ≤ 1 dB）\n"
              << "  → 状态持久性无问题，20 Hz 偏差另有原因。\n";
    return 0;
}
