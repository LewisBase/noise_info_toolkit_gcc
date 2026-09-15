/**
 * @file probe_bands_v3.3.1.cpp
 * @brief 判定实验: freq_*hz_spl 列到底是 Z 加权还是 A 加权?
 *
 * 背景:
 *   noise_processor.cpp Phase 4 里 band filter 的输入是 buffer_start[i] (原始 PCM),
 *   不是 a_buf[i] (A 加权后). 因此从代码看 freq_*_spl 应该是 Z 加权 (未加权) 频带声级.
 *   但历史分析曾假设它是 A 加权 (dBA). 本实验用单频纯音做判定.
 *
 * 判定逻辑:
 *   喂 63 Hz @ 94 dB SPL 纯音:
 *     - 若 freq_63hz_spl ≈ 94 dB  → Z 加权 (未加权)
 *     - 若 freq_63hz_spl ≈ 67.8 dB → A 加权 (94 - 26.2)
 *   同时对照 LAeq / LCeq / LZeq 作为三角验证.
 *
 * 编译:
 *   g++ -std=c++17 -O2 -I include tools/probe_bands_v3.3.1.cpp \
 *       build_test/libnoise_toolkit.a -o /tmp/probe_bands
 */

#include <cmath>
#include <cstdio>
#include <vector>

#include "noise_processor.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

namespace {

constexpr int FS = 48000;
constexpr float REF_PA = 20e-6f;
constexpr float SPL_DB = 94.0f;
constexpr float DUR_S  = 4.0f;

// IEC 61672-1 A 计权标称值 (dB) @ 各频带中心
constexpr float A_W_REF[9] = { -26.2f, -16.1f, -8.6f, -3.2f, 0.0f, 1.2f, 1.0f, -1.1f, -6.6f };
// IEC 61672-1 C 计权标称值 (dB)
constexpr float C_W_REF[9] = { -0.8f, -0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2f, -3.3f };

const float BAND_FC[9] = { 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f,
                           2000.0f, 4000.0f, 8000.0f, 16000.0f };

std::vector<float> sine(float freq, float spl_db, float dur_s) {
    size_t n = static_cast<size_t>(FS * dur_s);
    std::vector<float> d(n);
    float peak = REF_PA * std::pow(10.f, spl_db / 20.f) * std::sqrt(2.f);
    for (size_t i = 0; i < n; ++i)
        d[i] = peak * std::sin(noise_const::TWO_PI_F * freq * static_cast<float>(i) / FS);
    return d;
}

const float* band_spl_ptr(const SecondMetrics& m) {
    static float arr[9];
    arr[0] = m.freq_63hz_spl;   arr[1] = m.freq_125hz_spl;  arr[2] = m.freq_250hz_spl;
    arr[3] = m.freq_500hz_spl;  arr[4] = m.freq_1khz_spl;   arr[5] = m.freq_2khz_spl;
    arr[6] = m.freq_4khz_spl;   arr[7] = m.freq_8khz_spl;   arr[8] = m.freq_16khz_spl;
    return arr;
}

} // namespace

int main() {
    std::printf("================================================================\n");
    std::printf(" freq_*hz_spl 加权类型判定实验 (v3.3.1)\n");
    std::printf(" 输入: 各频带中心单频纯音 @ %.1f dB SPL, %.1fs\n", SPL_DB, DUR_S);
    std::printf(" 固件: NoiseProcessor (libnoise_toolkit.a, fs=%d)\n", FS);
    std::printf("================================================================\n\n");

    std::printf("--- 判定实验 1: 63 Hz 纯音 (A 计权 -26.2 dB, C 计权 -0.8 dB) ---\n");
    {
        auto d = sine(63.0f, SPL_DB, DUR_S);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(d.data(), d.data() + d.size(), DUR_S);
        std::printf("  LAeq = %8.2f dB   (若 A 计权正确应 ~%.1f)\n", m.LAeq, SPL_DB - 26.2f);
        std::printf("  LCeq = %8.2f dB   (若 C 计权正确应 ~%.1f)\n", m.LCeq, SPL_DB - 0.8f);
        std::printf("  LZeq = %8.2f dB   (应 ~%.1f)\n", m.LZeq, SPL_DB);
        std::printf("  freq_63hz_spl = %8.2f dB   (Z假设~%.1f / A假设~%.1f)\n",
                    m.freq_63hz_spl, SPL_DB, SPL_DB - 26.2f);
    }

    std::printf("\n--- 判定实验 2: 16 kHz 纯音 (A 计权 -6.6 dB, C 计权 -3.3 dB) ---\n");
    {
        auto d = sine(16000.0f, SPL_DB, DUR_S);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(d.data(), d.data() + d.size(), DUR_S);
        std::printf("  LAeq = %8.2f dB   (若 A 计权正确应 ~%.1f)\n", m.LAeq, SPL_DB - 6.6f);
        std::printf("  LCeq = %8.2f dB   (若 C 计权正确应 ~%.1f)\n", m.LCeq, SPL_DB - 3.3f);
        std::printf("  LZeq = %8.2f dB   (应 ~%.1f)\n", m.LZeq, SPL_DB);
        std::printf("  freq_16khz_spl = %8.2f dB  (Z假设~%.1f / A假设~%.1f)\n",
                    m.freq_16khz_spl, SPL_DB, SPL_DB - 6.6f);
    }

    std::printf("\n================================================================\n");
    std::printf(" 全频带扫描: 各频带中心纯音 @ 94 dB SPL\n");
    std::printf("================================================================\n");
    std::printf("%9s | %8s | %8s | %9s | %10s | %10s | %10s\n",
                "fc(Hz)", "LAeq", "LCeq", "LZeq", "band_spl", "spl-A_REF", "spl-C_REF");
    std::printf("--------- | -------- | -------- | --------- | ---------- | ---------- | ----------\n");

    float max_z_err = 0.0f, max_a_err = 0.0f, max_c_err = 0.0f;
    for (int b = 0; b < 9; ++b) {
        auto d = sine(BAND_FC[b], SPL_DB, DUR_S);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(d.data(), d.data() + d.size(), DUR_S);
        const float* bs = band_spl_ptr(m);
        float spl = bs[b];
        float dev_z = std::fabs(spl - SPL_DB);
        float dev_a = std::fabs(spl - (SPL_DB + A_W_REF[b]));
        float dev_c = std::fabs(spl - (SPL_DB + C_W_REF[b]));
        max_z_err = std::max(max_z_err, dev_z);
        max_a_err = std::max(max_a_err, dev_a);
        max_c_err = std::max(max_c_err, dev_c);
        std::printf("%9.0f | %8.2f | %8.2f | %9.2f | %10.2f | %10.2f | %10.2f\n",
                    BAND_FC[b], m.LAeq, m.LCeq, m.LZeq, spl,
                    spl - (SPL_DB + A_W_REF[b]), spl - (SPL_DB + C_W_REF[b]));
    }

    std::printf("\n---------------- 判定 (band_spl vs 三种加权假设) ----------------\n");
    std::printf("  max|band_spl - 94.0|              = %6.2f dB   (Z 加权假设)\n", max_z_err);
    std::printf("  max|band_spl - (94.0 + A_REF)|    = %6.2f dB   (A 加权假设)\n", max_a_err);
    std::printf("  max|band_spl - (94.0 + C_REF)|    = %6.2f dB   (C 加权假设)\n", max_c_err);
    std::printf("\n  判定: ");
    if (max_z_err < max_a_err && max_z_err < max_c_err) {
        std::printf("freq_*hz_spl = **Z 加权 (未加权) 频带声级**\n");
    } else if (max_a_err < max_z_err && max_a_err < max_c_err) {
        std::printf("freq_*hz_spl = **A 加权 (dBA) 频带声级**\n");
    } else {
        std::printf("freq_*hz_spl = **C 加权频带声级**\n");
    }
    std::printf("  (注意: 单频纯音落在相邻带边缘时会有泄漏, 此处只看中心带自身读数)\n");
    return 0;
}
