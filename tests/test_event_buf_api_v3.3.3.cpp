/**
 * test_event_buf_api_v3.3.3.cpp — 设备侧 buf/end 接口验证
 *
 * 目的：证明设备侧（只能传 buf/end）也能拿到 v3.3.3 的四维度事件检测。
 *
 * 两条路径对比：
 *   路径 A（设备侧）: detector.check_metrics(buf, buf + n)          ← 只需 buf/end
 *   路径 B（主机侧）: proc.process_segment() → detector.check_metrics(m)
 *
 * 预期：两条路径结果一致（同一 A 计权链 + 同一时间计权 + 同一判定逻辑）
 *
 * 用法: ./test_event_buf_api_v3.3.3 /path/to/ALL_Pa.wav [max_seconds] [inject_at_sec]
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
        char id[4]; uint32_t size = 0;
        f.read(id, 4);
        f.read(reinterpret_cast<char*>(&size), 4);
        if (!f) break;
        std::string sid(id, 4);
        if (sid == "fmt ") {
            uint16_t af = 0, nc = 0, ba = 0, bits = 0; uint32_t sr = 0, br = 0;
            f.read(reinterpret_cast<char*>(&af), 2);
            f.read(reinterpret_cast<char*>(&nc), 2);
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

const char* tn(EventType t) {
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
        std::fprintf(stderr, "用法: %s /path/to/ALL_Pa.wav [max_sec] [inject_at]\n", argv[0]);
        return 2;
    }
    const std::string wav = argv[1];
    const int max_sec   = (argc >= 3) ? std::atoi(argv[2]) : 70;
    const int inject_at = (argc >= 4) ? std::atoi(argv[3]) : -1;

    std::vector<float> samples;
    int sr = 0;
    if (!read_wav_pcm_f32(wav, samples, sr)) {
        std::fprintf(stderr, "❌ 无法读取 WAV\n");
        return 1;
    }

    if (inject_at >= 0 && static_cast<size_t>(inject_at + 1) * sr <= samples.size()) {
        const size_t b0 = static_cast<size_t>(inject_at) * sr + sr / 2;
        const size_t bl = sr / 10;
        const float g = std::pow(10.0f, 30.0f / 20.0f);
        for (size_t i = b0; i < b0 + bl && i < samples.size(); ++i) samples[i] *= g;
        std::printf("⚠ 已注入 100 ms / +30 dB 冲击 @ t=%ds\n", inject_at);
    }

    std::printf("\n=== 设备侧 buf/end 接口验证 (v3.3.3) ===\n");
    std::printf("WAV: %s  (fs=%d, %.0f s)\n\n", wav.c_str(), sr,
                static_cast<double>(samples.size()) / sr);

    // 两条路径各用独立 detector / processor
    EventDetector      det_a;                      // 路径 A: 设备侧（buf/end）
    NoiseProcessor     proc;                       // 路径 B: 主机侧需要 processor
    EventDetector      det_b;
    proc = NoiseProcessor(sr);

    const int seg = sr;
    int agree = 0, total = 0, diff_cnt = 0;

    std::printf("  t(s) |  路径A(buf/end)         |  路径B(metrics)          | 一致?\n");
    std::printf("       | LAF    LAS    事件  评分 | LAF    LAS    事件  评分 |\n");
    std::printf("  -----+--------------------------+--------------------------+------\n");

    int a_minor = 0, a_mod = 0, a_sev = 0;
    int b_minor = 0, b_mod = 0, b_sev = 0;

    for (int s = 0; s < max_sec; ++s) {
        const size_t off = static_cast<size_t>(s) * seg;
        if (off + seg > samples.size()) break;

        // 路径 A：只要 buf/end
        EventResult ra = det_a.check_metrics(samples.data() + off,
                                             samples.data() + off + seg);

        // 路径 B：先算指标，再判定
        SecondMetrics m = proc.process_segment(samples.data() + off,
                                                samples.data() + off + seg, 1.0f);
        EventResult rb = det_b.check_metrics(m);

        if (ra.event_type == EventType::MINOR)    ++a_minor;
        if (ra.event_type == EventType::MODERATE) ++a_mod;
        if (ra.event_type == EventType::SEVERE)   ++a_sev;
        if (rb.event_type == EventType::MINOR)    ++b_minor;
        if (rb.event_type == EventType::MODERATE) ++b_mod;
        if (rb.event_type == EventType::SEVERE)   ++b_sev;

        const bool same = (ra.event_type == rb.event_type);
        ++total;
        if (same) ++agree; else ++diff_cnt;

        const bool interesting = (ra.event_type != EventType::NONE) ||
                                 (rb.event_type != EventType::NONE) ||
                                 s < 3 || (s + 1) % 10 == 0;
        if (interesting) {
            std::printf("  %4d | %6.2f %6.2f  %s %3d | %6.2f %6.2f  %s %3d | %s\n",
                        s + 1, ra.laf_dB, ra.las_dB, tn(ra.event_type), ra.severity,
                        rb.laf_dB, rb.las_dB, tn(rb.event_type), rb.severity,
                        same ? "✅" : "❌");
        }
    }

    std::printf("\n=== 一致性 ===\n");
    std::printf("  逐秒事件分级一致: %d/%d", agree, total);
    if (diff_cnt == 0) std::printf("  ✅ 完全一致\n");
    else               std::printf("  ❌ %d 秒不一致\n", diff_cnt);

    std::printf("\n=== 事件计数 ===\n");
    std::printf("  路径 A (buf/end): MINOR=%d MODERATE=%d SEVERE=%d\n", a_minor, a_mod, a_sev);
    std::printf("  路径 B (metrics): MINOR=%d MODERATE=%d SEVERE=%d\n", b_minor, b_mod, b_sev);

    std::printf("\n=== 结论 ===\n");
    if (diff_cnt == 0) {
        std::printf("  ✅ 设备侧只需 buf/end 即可获得完整四维度事件检测\n");
        std::printf("     与主机侧指标路径结果完全一致\n");
    } else {
        std::printf("  ⚠ 两条路径存在差异，需排查\n");
    }

    return (diff_cnt == 0) ? 0 : 1;
}
