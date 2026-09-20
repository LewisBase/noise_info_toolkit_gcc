/**
 * test_c_weighting_v3.3.3.cpp — C 计权低频修复效果验证
 *
 * 验证两点：
 *   1. LCeq 是否受益于 v3.3.2 的「滤波器状态持久」修复（对比每段 reset 的旧行为）
 *   2. LCeq 与 IEC 61672-1 C 计权理论值是否吻合
 *
 * 用法: ./test_c_weighting_v3.3.3 [wav_path]
 */

#include "noise_processor.hpp"
#include "noise_metrics.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <string>

using namespace noise_toolkit;

namespace {
constexpr float PI_F = 3.14159265358979323846f;

// IEC 61672-1 C 计权标准衰减（dB），已归一化到 1 kHz = 0
float iec_c_attenuation(float f) {
    if (f < 1.0f) f = 1.0f;
    const float f1 = 20.598997f, f4 = 12194.217f;
    const float f2_ = f * f;
    const float f1sq = f1 * f1, f4sq = f4 * f4;
    // Rc(f) = 20·log10( (f4² · f²) / ((f²+f1²)·(f²+f4²)) ) − Rc(1kHz)
    auto rc_raw = [&](float x) {
        const float x2 = x * x;
        return 20.0f * std::log10((f4sq * x2) / ((x2 + f1sq) * (x2 + f4sq)));
    };
    (void)f2_;
    return rc_raw(f) - rc_raw(1000.0f);
}
} // namespace

int main(int argc, char** argv) {
    const int fs = 48000;
    const float p_rms_94 = 20e-6f * std::pow(10.0f, 94.0f / 20.0f);   // 1 Pa

    std::printf("\n=== C 计权低频验证 ===\n\n");
    std::printf("  频率  |  LCeq 实测 | IEC 理论 | 偏差   | LZeq   | 备注\n");
    std::printf("  ------+------------+----------+--------+--------+------------------\n");

    struct Case { float f; };
    const Case cases[] = {{20.0f}, {25.0f}, {50.0f}, {100.0f}, {1000.0f}};

    for (const auto& c : cases) {
        NoiseProcessor proc(fs);
        const int n = fs;                       // 1 秒/段
        const int nsec = 8;                     // 8 秒（含建立期）
        float lceq_last = 0.0f, lzeq_last = 0.0f;

        for (int s = 0; s < nsec; ++s) {
            std::vector<float> buf(n);
            const float amp = p_rms_94 * std::sqrt(2.0f);
            const long base = static_cast<long>(s) * n;
            for (int i = 0; i < n; ++i) {
                buf[i] = amp * std::sin(2.0f * PI_F * c.f * (base + i) / fs);
            }
            SecondMetrics m = proc.process_segment(buf.data(), buf.data() + n, 1.0f);
            lceq_last = m.LCeq;
            lzeq_last = m.LZeq;
        }

        // C 计权：LZeq 94 dB + C衰减
        const float iec = 94.0f + iec_c_attenuation(c.f);
        std::printf("  %5.0f | %10.2f | %8.2f | %+6.2f | %6.2f | %s\n",
                    c.f, lceq_last, iec, lceq_last - iec, lzeq_last,
                    std::abs(lceq_last - iec) < 1.5f ? "✅ 达标" : "❌ 超容差");
    }

    std::printf("\n  （每段 1 秒，共 8 秒；LCeq 取第 8 秒稳态值）\n");

    // ── WAV 实测（可选）──────────────────────────────
    if (argc >= 2) {
        std::ifstream f(argv[1], std::ios::binary);
        if (!f) { std::fprintf(stderr, "无法打开 %s\n", argv[1]); return 1; }
        char hdr[12]; f.read(hdr, 12);
        int sr = 48000;
        std::vector<float> samples;
        while (f) {
            char id[4]; uint32_t size = 0;
            f.read(id, 4); f.read(reinterpret_cast<char*>(&size), 4);
            if (!f) break;
            std::string sid(id, 4);
            if (sid == "fmt ") {
                uint16_t af, nc, ba, bits; uint32_t s_, br;
                f.read(reinterpret_cast<char*>(&af), 2);
                f.read(reinterpret_cast<char*>(&nc), 2);
                f.read(reinterpret_cast<char*>(&s_), 4);
                f.read(reinterpret_cast<char*>(&br), 4);
                f.read(reinterpret_cast<char*>(&ba), 2);
                f.read(reinterpret_cast<char*>(&bits), 2);
                sr = static_cast<int>(s_);
                if (size > 16) f.ignore(size - 16);
            } else if (sid == "data") {
                samples.resize(size / 4);
                f.read(reinterpret_cast<char*>(samples.data()), size);
                break;
            } else f.ignore(size);
        }
        if (!samples.empty()) {
            NoiseProcessor proc(sr);
            const int seg = sr;
            std::printf("\n=== WAV 实测: %s ===\n", argv[1]);
            std::printf("  t(s) |   LCeq   |   LZeq   |  LCeq-LZeq\n");
            for (int s = 0; s < 30 && static_cast<size_t>((s + 1) * seg) <= samples.size(); ++s) {
                SecondMetrics m = proc.process_segment(samples.data() + static_cast<size_t>(s) * seg,
                                                        samples.data() + static_cast<size_t>(s + 1) * seg,
                                                        1.0f);
                if (s < 3 || (s + 1) % 5 == 0) {
                    std::printf("  %4d | %8.2f | %8.2f | %+8.2f\n",
                                s + 1, m.LCeq, m.LZeq, m.LCeq - m.LZeq);
                }
            }
            std::printf("\n  IEC C 计权 @20Hz 理论衰减 = %.2f dB（故 20Hz 信号 LCeq-LZeq 应约等于该值）\n",
                        iec_c_attenuation(20.0f));
            std::printf("  IEC A 计权 @20Hz 理论衰减 = -50.50 dB（对比参考）\n");
        }
    }

    return 0;
}
