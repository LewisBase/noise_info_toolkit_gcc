/**
 * @file probe_bandsum_v3.3.1.cpp
 * @brief 1/3 倍频程滤波器组自洽性定量实验 (v3.3.1)
 *
 * 目的:
 *   量化固件 9 带滤波器组的能量自洽性误差:
 *     err_Z = 10·log10(Σ 10^(L_band/10)) − LZeq
 *     err_A = 10·log10(Σ 10^((L_band+A_ref)/10)) − LAeq
 *   用受控信号 (白噪声 / 粉红噪声 / 各频带纯音) 分离出:
 *     (a) 带外能量损失 (63 Hz–16 kHz 之外)
 *     (b) 单节 biquad 带通滤波器的带泄漏
 *
 * 判定:
 *   - 若带级列是 Z 加权, 白噪声下 err_Z 应 ≈ 0 (带覆盖内有能量)
 *   - 若带级列是 A 加权, err_A 应 ≈ 0
 *
 * 编译:
 *   g++ -std=c++17 -O2 -I include tools/probe_bandsum_v3.3.1.cpp \
 *       build_test/libnoise_toolkit.a -o /tmp/probe_bandsum
 */

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "noise_processor.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

namespace {

constexpr int FS = 48000;
constexpr float REF_PA = 20e-6f;
constexpr float DUR_S = 10.0f;

constexpr float A_W_REF[9] = { -26.2f, -16.1f, -8.6f, -3.2f, 0.0f, 1.2f, 1.0f, -1.1f, -6.6f };
const float BAND_FC[9] = { 63.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };

void get_bands(const SecondMetrics& m, float out[9]) {
    out[0] = m.freq_63hz_spl;  out[1] = m.freq_125hz_spl; out[2] = m.freq_250hz_spl;
    out[3] = m.freq_500hz_spl; out[4] = m.freq_1khz_spl;  out[5] = m.freq_2khz_spl;
    out[6] = m.freq_4khz_spl;  out[7] = m.freq_8khz_spl;  out[8] = m.freq_16khz_spl;
}

float sum_pow(const float b[9], bool a_weighted) {
    float s = 0.0f;
    for (int i = 0; i < 9; ++i) {
        float v = b[i] + (a_weighted ? A_W_REF[i] : 0.0f);
        s += std::pow(10.0f, v / 10.0f);
    }
    return 10.0f * std::log10(s);
}

struct Case { const char* name; std::vector<float> data; float nominal_spl; };

std::vector<float> sine(float freq, float spl_db) {
    size_t n = static_cast<size_t>(FS * DUR_S);
    std::vector<float> d(n);
    float peak = REF_PA * std::pow(10.f, spl_db / 20.f) * std::sqrt(2.f);
    for (size_t i = 0; i < n; ++i)
        d[i] = peak * std::sin(noise_const::TWO_PI_F * freq * static_cast<float>(i) / FS);
    return d;
}

std::vector<float> white(float spl_db) {
    size_t n = static_cast<size_t>(FS * DUR_S);
    std::vector<float> d(n);
    float rms = REF_PA * std::pow(10.f, spl_db / 20.f);
    std::mt19937 gen(1234);
    std::normal_distribution<float> dis(0.f, rms);
    for (size_t i = 0; i < n; ++i) d[i] = dis(gen);
    return d;
}

// 白噪声经 A 计权链 → 频谱接近 A 加权曲线, 用于检验 A 加权假设
std::vector<float> pinkish(float spl_db) {
    size_t n = static_cast<size_t>(FS * DUR_S);
    std::vector<float> d(n);
    std::mt19937 gen(99);
    std::normal_distribution<float> dis(0.f, 1.f);
    // 简单 1/f 整形 (一阶低通叠加近似), 归一化到目标 RMS
    float y = 0.f;
    for (size_t i = 0; i < n; ++i) {
        y = 0.98f * y + dis(gen);
        d[i] = y;
    }
    float rms = 0.f;
    for (float v : d) rms += v * v;
    rms = std::sqrt(rms / n);
    float target = REF_PA * std::pow(10.f, spl_db / 20.f);
    for (float& v : d) v = v / rms * target;
    return d;
}

} // namespace

int main() {
    std::printf("================================================================\n");
    std::printf(" 1/3 倍频程滤波器组自洽性实验 (v3.3.1)\n");
    std::printf(" 输入: 受控信号 @ %.0fs, fs=%d\n", DUR_S, FS);
    std::printf(" 判据: 带级列若为 Z 加权 → err_Z≈0; 若为 A 加权 → err_A≈0\n");
    std::printf("================================================================\n\n");

    std::vector<Case> cases;
    cases.push_back({"白噪声 @ 85 dB", white(85.f), 85.f});
    cases.push_back({"粉红噪声 @ 85 dB", pinkish(85.f), 85.f});
    cases.push_back({"63 Hz 纯音 @ 94 dB", sine(63.f, 94.f), 94.f});
    cases.push_back({"1 kHz 纯音 @ 94 dB", sine(1000.f, 94.f), 94.f});
    cases.push_back({"4 kHz 纯音 @ 94 dB", sine(4000.f, 94.f), 94.f});
    cases.push_back({"16 kHz 纯音 @ 94 dB", sine(16000.f, 94.f), 94.f});
    // IEC 参考复音: 63+125+250+500+1k+2k+4k+8k+16k 各 1/9 能量, 总 94 dB
    {
        size_t n = static_cast<size_t>(FS * DUR_S);
        std::vector<float> d(n, 0.f);
        float per = 94.f - 10.f * std::log10(9.0f);
        for (int i = 0; i < 9; ++i) {
            auto s = sine(BAND_FC[i], per);
            for (size_t k = 0; k < n; ++k) d[k] += s[k];
        }
        cases.push_back({"9 带复音 (各 1/9 能量) @ 94 dB", d, 94.f});
    }

    std::printf("%-30s | %7s | %7s | %8s | %8s | %8s\n",
                "信号", "LZeq", "LAeq", "Σband_Z", "Σband_A", "err_Z / err_A");
    std::printf("%-30s-|-%7s-|-%7s-|-%8s-|-%8s-|-%8s\n",
                "------------------------------", "-------", "-------", "--------",
                "--------", "--------");

    for (auto& c : cases) {
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(c.data.data(), c.data.data() + c.data.size(), DUR_S);
        float b[9];
        get_bands(m, b);
        float sz = sum_pow(b, false);
        float sa = sum_pow(b, true);
        float ez = sz - m.LZeq;
        float ea = sa - m.LAeq;
        std::printf("%-30s | %7.2f | %7.2f | %8.2f | %8.2f | %+8.2f / %+8.2f\n",
                    c.name, m.LZeq, m.LAeq, sz, sa, ez, ea);
    }

    std::printf("\n---------------- 说明 ----------------\n");
    std::printf("  Σband_Z = 10·log10(Σ10^(L_band/10))        (带级原值, 视作 Z 加权)\n");
    std::printf("  Σband_A = 10·log10(Σ10^((L_band+A_ref)/10))(带级 + A 修正)\n");
    std::printf("  err_Z = Σband_Z − LZeq ;  err_A = Σband_A − LAeq\n");
    std::printf("  → 9 带只覆盖 56 Hz–18 kHz; 纯音在带中心时带外泄漏被单节 biquad 放大\n");
    return 0;
}
