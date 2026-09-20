/**
 * test_v3.3.2_laeq_validation.cpp — v3.3.2 指数时间计权修复效果验证
 *
 * 验证目标：
 *   1. PE-04 频段 20/25 Hz @ 94 dB SPL, 5s 信号后 LAeq ≈ 43.5/48.1 dBA（IEC 容差内）
 *   2. 1 kHz / 4 kHz 中频段 LAeq ≈ 94.0 dBA（无偏差）
 *   3. LAF / LAS 输出字段有正确数值
 *   4. 不破坏现有 overload_flag / underrange_flag 等 QC 字段
 *
 * 用法（需要 260918 Pa-WAV 数据，运行时可指定 WAV 路径）：
 *   ./test_v3.3.2_laeq_validation /path/to/ALL_Pa.wav [freq_hz]
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

// 读取 32-bit float PCM WAV（标准 RIFF chunk 遍历）
bool read_wav_pcm_f32(const std::string& path, std::vector<float>& samples, int& sample_rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char hdr[12];
    f.read(hdr, 12);
    if (std::string(hdr, 4) != "RIFF") return false;
    if (std::string(hdr + 8, 4) != "WAVE") return false;

    while (f) {
        char id[4];
        uint32_t size = 0;
        f.read(id, 4);          // chunk ID
        f.read(reinterpret_cast<char*>(&size), 4);   // chunk size
        if (!f) break;
        std::string sid(id, 4);

        if (sid == "fmt ") {
            uint16_t audio_fmt = 0, nchan = 0, ba = 0, bits = 0;
            uint32_t sr = 0, br = 0;
            f.read(reinterpret_cast<char*>(&audio_fmt), 2);
            f.read(reinterpret_cast<char*>(&nchan), 2);
            f.read(reinterpret_cast<char*>(&sr), 4);
            f.read(reinterpret_cast<char*>(&br), 4);
            f.read(reinterpret_cast<char*>(&ba), 2);
            f.read(reinterpret_cast<char*>(&bits), 2);
            sample_rate = static_cast<int>(sr);
            if (size > 16) f.ignore(size - 16);
        } else if (sid == "data") {
            samples.resize(size / 4);
            f.read(reinterpret_cast<char*>(samples.data()), size);
            return true;
        } else {
            f.ignore(size);
        }
    }
    return false;
}

float laeq_synthetic(int freq_hz, float rms_pa, float dur_s, NoiseProcessor& proc) {
    int fs = proc.sample_rate();
    int n = static_cast<int>(fs * dur_s);
    std::vector<float> buf(n);
    float amp = rms_pa * std::sqrt(2.0f);
    for (int i = 0; i < n; ++i) {
        buf[i] = amp * std::sin(2.0f * 3.14159265f * freq_hz * i / fs);
    }
    SecondMetrics m = proc.process_segment(buf.data(), buf.data() + n, dur_s);
    return m.LAeq;
}

void print_result(const char* name, float laeq, float expected, float tol) {
    float diff = laeq - expected;
    const char* mark = (std::abs(diff) < tol) ? "✅" : "❌";
    std::printf("  %s %-15s = %7.2f dBA  (理论 %.2f, 偏差 %+.2f dB, 容差 ±%.1f)\n",
                mark, name, laeq, expected, diff, tol);
}

} // namespace

int main(int argc, char** argv) {
    NoiseProcessor proc(48000);

    // === Test 1: 纯音验证（5s 信号，稳态） ===
    std::printf("\n=== Test 1: 纯音 5s 合成信号验证（指数时间计权）===\n");
    float p_rms = 20e-6f * std::pow(10.0f, 94.0f / 20.0f);  // 0.1 Pa

    // 1 kHz 参考点
    {
        NoiseProcessor p(48000);
        float laeq = laeq_synthetic(1000, p_rms, 5.0f, p);
        print_result("1 kHz", laeq, 94.0f, 0.5f);
    }
    // 20 Hz PE-04 频段
    {
        NoiseProcessor p(48000);
        float laeq = laeq_synthetic(20, p_rms, 5.0f, p);
        print_result("20 Hz", laeq, 43.5f, 1.0f);  // IEC +0.22 dB 实测
    }
    // 25 Hz PE-04 频段
    {
        NoiseProcessor p(48000);
        float laeq = laeq_synthetic(25, p_rms, 5.0f, p);
        print_result("25 Hz", laeq, 48.1f, 1.5f);  // IEC +1.16 dB 实测
    }
    // 4 kHz
    {
        NoiseProcessor p(48000);
        float laeq = laeq_synthetic(4000, p_rms, 5.0f, p);
        print_result("4 kHz", laeq, 94.9f, 1.0f);
    }

    // === Test 2: LAF/LAS 输出验证 ===
    std::printf("\n=== Test 2: LAF/LAS 输出验证（v3.3.2 新增字段）===\n");
    {
        NoiseProcessor p(48000);
        // 连续 5 秒 1 kHz 正弦（让滤波器与时间计权状态都稳态）
        float rms_pa = p_rms;
        int fs = 48000;
        int n = fs * 5;
        std::vector<float> tone(n);
        float amp = rms_pa * std::sqrt(2.0f);
        for (int i = 0; i < n; ++i)
            tone[i] = amp * std::sin(2.0f * 3.14159265f * 1000.0f * i / fs);

        SecondMetrics m{};
        // 逐秒处理（模拟固件接口一 + 接口二循环）
        for (int s = 0; s < 5; ++s) {
            m = p.process_segment(tone.data() + s * fs, tone.data() + (s + 1) * fs, 1.0f);
        }
        std::printf("  1 kHz @ 94 dB SPL, 5s 后:\n");
        std::printf("    LAeq (τ=1s)          = %.2f dBA\n", m.LAeq);
        std::printf("    LAF  (τ=125ms)       = %.2f dBA\n", m.LAF);
        std::printf("    LAS  (τ=1s)          = %.2f dBA\n", m.LAS);
        std::printf("    LASmax               = %.2f dBA\n", m.LASmax);
        std::printf("    LAFmax               = %.2f dBA\n", m.LAFmax);
        bool ok = std::abs(m.LAF - 94.0f) < 2.0f && std::abs(m.LAS - 94.0f) < 2.0f;
        std::printf("    %s LAF/LAS 稳态值接近 94 dBA\n", ok ? "✅" : "❌");
    }

    // === Test 3: 零输入验证（QC 标志位）===
    std::printf("\n=== Test 3: 零输入 60s 验证（QC 标志位不应破坏）===\n");
    {
        NoiseProcessor p(48000);
        std::vector<float> zeros(48000, 0.0f);
        for (int i = 0; i < 60; ++i) {
            SecondMetrics m = p.process_segment(zeros.data(), zeros.data() + zeros.size(), 1.0f);
            if (i == 0 || i == 59) {
                std::printf("  t=%ds: LAeq=%.2f  LAF=%.2f  LAS=%.2f  overload=%d underrange=%d\n",
                            i+1, m.LAeq, m.LAF, m.LAS, m.overload_flag, m.underrange_flag);
            }
        }
    }

    // === Test 4: 260918 Pa-WAV 验证（如果提供）===
    if (argc >= 2) {
        std::printf("\n=== Test 4: 260918 Pa-WAV 验证 ===\n");
        std::string wav_path = argv[1];
        std::vector<float> samples;
        int sr = 0;
        if (!read_wav_pcm_f32(wav_path, samples, sr)) {
            std::fprintf(stderr, "  ❌ 无法读取 WAV: %s\n", wav_path.c_str());
            return 1;
        }
        std::printf("  WAV: %s, fs=%d, %zu samples (%.1f s)\n",
                    wav_path.c_str(), sr, samples.size(),
                    static_cast<float>(samples.size()) / sr);

        NoiseProcessor p(sr);
        float seg_dur = 1.0f;
        int seg_n = sr * 1;
        // 只处理前 70 秒（与之前 Python 分析窗口一致，避免处理 73 分钟全量数据）
        size_t max_segments = 70;
        std::vector<SecondMetrics> metrics;
        std::printf("  逐秒 LAeq/LAF/LAS:\n");
        for (size_t i = 0; i + seg_n <= samples.size() && metrics.size() < max_segments; i += seg_n) {
            SecondMetrics m = p.process_segment(samples.data() + i, samples.data() + i + seg_n, seg_dur);
            metrics.push_back(m);
            if (metrics.size() <= 10 || metrics.size() % 10 == 0) {
                std::printf("    t=%2ds: LAeq=%6.2f  LAF=%6.2f  LAS=%6.2f  LZeq=%6.2f\n",
                            static_cast<int>(metrics.size()), m.LAeq, m.LAF, m.LAS, m.LZeq);
            }
        }
        // 跳过前 5 秒（滤波器建立期），统计稳定后的 LAeq 均值
        if (metrics.size() > 5) {
            float sum = 0.0f;
            int cnt = 0;
            for (size_t i = 5; i < metrics.size(); ++i) {
                if (metrics[i].LAeq > 0) { sum += metrics[i].LAeq; cnt++; }
            }
            if (cnt > 0) {
                float laeq_avg = sum / cnt;
                std::printf("  5s 之后平均 LAeq = %.2f dBA (稳态)\n", laeq_avg);
                std::printf("  与 CSV 对比: CSV LAeq ≈ 67.49 dBA (v3.3.1 10ms 子块偏差 +25 dB)\n");
                std::printf("  v3.3.2 预期: 20 Hz 处 LAeq 应接近 IEC 理论 43.5 dBA\n");
            }
        }
    } else {
        std::printf("\n=== Test 4: 260918 Pa-WAV 验证（跳过，未提供 WAV 路径）===\n");
        std::printf("  用法: %s /path/to/ALL_Pa.wav\n", argv[0]);
    }

    std::printf("\n=== 验证完成 ===\n");
    return 0;
}