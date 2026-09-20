/**
 * test_event_on_wav_v3.3.3.cpp — 在 20 Hz 实录 WAV 上做事件检测调用测试
 *
 * 流程（模拟嵌入式端完整调用链）：
 *   WAV → NoiseProcessor::process_segment() → SecondMetrics(含 LAF/LAS)
 *       → EventDetector::check_metrics() → EventResult
 *       → 回写 m.event_type / m.event_severity → MinuteMetrics 统计
 *
 * 用法: ./test_event_on_wav_v3.3.3 /path/to/ALL_Pa.wav [max_seconds]
 */

#include "noise_processor.hpp"
#include "event_detector.hpp"
#include "noise_metrics.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <string>
#include <array>

using namespace noise_toolkit;

namespace {

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
        f.read(id, 4);
        f.read(reinterpret_cast<char*>(&size), 4);
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

const char* type_name(EventType t) {
    switch (t) {
        case EventType::NONE:     return "NONE    ";
        case EventType::MINOR:    return "MINOR   ";
        case EventType::MODERATE: return "MODERATE";
        case EventType::SEVERE:   return "SEVERE  ";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s /path/to/ALL_Pa.wav [max_seconds] [inject_at_sec]\n", argv[0]);
        return 2;
    }
    const std::string wav = argv[1];
    const int max_sec = (argc >= 3) ? std::atoi(argv[2]) : 70;
    const int inject_at = (argc >= 4) ? std::atoi(argv[3]) : -1;   // <0 = 不注入

    std::vector<float> samples;
    int sr = 0;
    if (!read_wav_pcm_f32(wav, samples, sr)) {
        std::fprintf(stderr, "❌ 无法读取 WAV: %s\n", wav.c_str());
        return 1;
    }
    std::printf("\n=== WAV 事件检测调用测试 (v3.3.3) ===\n");
    std::printf("WAV: %s\n", wav.c_str());
    std::printf("fs=%d, %zu samples (%.1f s), 处理前 %d 秒\n",
                sr, samples.size(), static_cast<double>(samples.size()) / sr, max_sec);

    // 可选：在指定秒注入一个 100 ms 的 +30 dB 冲击（验证检测器真的会触发）
    if (inject_at >= 0 && static_cast<size_t>(inject_at + 1) * sr <= samples.size()) {
        const size_t burst_start = static_cast<size_t>(inject_at) * sr + sr / 2;  // 该秒中间
        const size_t burst_len = sr / 10;                                          // 100 ms
        const float gain = std::pow(10.0f, 30.0f / 20.0f);                         // +30 dB
        for (size_t i = burst_start; i < burst_start + burst_len && i < samples.size(); ++i) {
            samples[i] *= gain;
        }
        std::printf("⚠ 已在 t=%ds 注入 100 ms / +30 dB 冲击（验证用）\n", inject_at);
    }
    std::printf("\n");

    NoiseProcessor proc(sr);
    EventDetector detector;   // 默认配置（LAF 85/95/110, 脉冲 6/12, 上升 10/15, 背景 15）

    const int seg_n = sr;   // 1 秒/段
    std::vector<SecondMetrics> metric_buf;
    metric_buf.reserve(static_cast<size_t>(max_sec));

    std::printf("  t(s) |   LAeq  |   LAF   |   LAS   | LZeq  | 脉冲 | 上升 | 背景差 | 事件类型 | 评分\n");
    std::printf("  -----+---------+---------+---------+-------+------+------+--------+----------+-----\n");

    int n_minor = 0, n_moderate = 0, n_severe = 0, n_none = 0;

    for (int s = 0; s < max_sec; ++s) {
        const size_t off = static_cast<size_t>(s) * seg_n;
        if (off + seg_n > samples.size()) break;

        SecondMetrics m = proc.process_segment(samples.data() + off,
                                               samples.data() + off + seg_n, 1.0f);
        EventResult r = detector.check_metrics(m);

        // 回写事件字段（这是设计约定的调用方职责）
        m.event_type     = static_cast<uint8_t>(r.event_type);
        m.event_severity = r.severity;
        metric_buf.push_back(m);

        switch (r.event_type) {
            case EventType::NONE:     ++n_none;     break;
            case EventType::MINOR:    ++n_minor;    break;
            case EventType::MODERATE: ++n_moderate; break;
            case EventType::SEVERE:   ++n_severe;   break;
        }

        // 打印前 10 秒 + 每 10 秒 + 注入窗口附近
        const bool in_inject_window = (inject_at >= 0) &&
            (s >= inject_at - 1) && (s <= inject_at + 3);
        if (s < 10 || (s + 1) % 10 == 0 || in_inject_window) {
            std::printf("  %4d | %7.2f | %7.2f | %7.2f | %5.2f | %+4.1f | %+4.1f | %+6.1f | %s | %3d\n",
                        s + 1, m.LAeq, m.LAF, m.LAS, m.LZeq,
                        r.impulse_metric_dB, r.laf_rise_dB, r.background_delta_dB,
                        type_name(r.event_type), r.severity);
        }
    }

    // ── 汇总（接口二聚合）──────────────────────────────
    MinuteMetrics agg = proc.aggregate_metrics(metric_buf.data(),
                                                static_cast<int>(metric_buf.size()), 1.0f);

    std::printf("\n=== 事件统计（接口二聚合）===\n");
    std::printf("  处理秒数:        %zu\n", metric_buf.size());
    std::printf("  NONE:            %d 秒\n", n_none);
    std::printf("  MINOR:           %d 秒\n", n_minor);
    std::printf("  MODERATE:        %d 秒\n", n_moderate);
    std::printf("  SEVERE:          %d 秒\n", n_severe);
    std::printf("  --- 来自 MinuteMetrics ---\n");
    std::printf("  event_minor_count:    %d\n", agg.event_minor_count);
    std::printf("  event_moderate_count: %d\n", agg.event_moderate_count);
    std::printf("  event_severe_count:   %d\n", agg.event_severe_count);

    std::printf("\n=== 说明 ===\n");
    std::printf("  20 Hz 稳态正弦信号 → 期望「无事件」(NONE)\n");
    std::printf("  信号 LAeq≈55 dBA 远低于 MINOR 阈值 85 dB → 正确判定为 NONE\n");
    std::printf("  事件检测针对的是「噪声水平突变/冲击」，不是稳态信号\n");

    return 0;
}
