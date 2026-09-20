/**
 * test_event_longrun_v3.3.3.cpp — 长时间数值稳定性验证
 *
 * 目的：验证 EventDetector 内置 A 计权链在长时间连续运行下
 *       不会出现 float32 状态累积/溢出/NaN（v3.3.1 覆辙的另一种形态）。
 *
 * 做法：把整条 WAV（~2186 秒）逐秒送进 check_metrics(buf, end)，
 *       并同时用 NoiseProcessor + check_metrics(m) 对照，
 *       全程检查 NaN / inf / 偏差漂移。
 *
 * 用法: ./test_event_longrun_v3.3.3 /path/to/ALL_Pa.wav
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s /path/to/ALL_Pa.wav\n", argv[0]);
        return 2;
    }
    std::vector<float> samples;
    int sr = 0;
    if (!read_wav_pcm_f32(argv[1], samples, sr)) {
        std::fprintf(stderr, "❌ 无法读取 WAV\n");
        return 1;
    }
    const size_t total_sec = samples.size() / sr;

    std::printf("\n=== 长时间数值稳定性验证 (v3.3.3) ===\n");
    std::printf("WAV: %s\n", argv[1]);
    std::printf("fs=%d, 总时长 %zu 秒 (%.1f 分钟)\n\n", sr, total_sec,
                static_cast<double>(total_sec) / 60.0);

    NoiseProcessor proc(sr);
    EventDetector   det_a;   // 设备侧 buf/end
    EventDetector   det_b;   // 主机侧 metrics

    int nan_count = 0, inf_count = 0, disagree = 0;
    float max_abs_dev = 0.0f;
    float laf_min = 1e30f, laf_max = -1e30f;
    float las_min = 1e30f, las_max = -1e30f;

    std::printf("  t(min) |   LAF(buf)  |   LAS(buf)  |  LAF(metrics)| 偏差  | NaN/Inf\n");
    std::printf("  -------+-------------+-------------+--------------+-------+--------\n");

    for (size_t s = 0; s < total_sec; ++s) {
        const size_t off = s * static_cast<size_t>(sr);

        EventResult ra = det_a.check_metrics(samples.data() + off,
                                             samples.data() + off + sr);
        SecondMetrics m = proc.process_segment(samples.data() + off,
                                               samples.data() + off + sr, 1.0f);
        EventResult rb = det_b.check_metrics(m);

        // NaN / Inf 检查
        const float vals[] = {ra.laf_dB, ra.las_dB, ra.laf_rise_dB,
                              ra.background_delta_dB, m.LAeq, m.LZeq};
        for (float v : vals) {
            if (std::isnan(v)) ++nan_count;
            if (std::isinf(v)) ++inf_count;
        }

        if (std::isfinite(ra.laf_dB)) {
            if (ra.laf_dB < laf_min) laf_min = ra.laf_dB;
            if (ra.laf_dB > laf_max) laf_max = ra.laf_dB;
        }
        if (std::isfinite(ra.las_dB)) {
            if (ra.las_dB < las_min) las_min = ra.las_dB;
            if (ra.las_dB > las_max) las_max = ra.las_dB;
        }

        if (std::isfinite(ra.laf_dB) && std::isfinite(rb.laf_dB)) {
            const float dev = std::abs(ra.laf_dB - rb.laf_dB);
            if (dev > max_abs_dev) max_abs_dev = dev;
        }
        if (ra.event_type != rb.event_type) ++disagree;

        const size_t min_mark = (s + 1) / 60;
        const bool print_it = (s < 3) || ((s + 1) % 300 == 0) || (s + 1 == total_sec);
        if (print_it) {
            std::printf("  %6zu | %11.3f | %11.3f | %12.3f | %5.3f | %s\n",
                        min_mark, ra.laf_dB, ra.las_dB, rb.laf_dB,
                        (std::isfinite(ra.laf_dB) && std::isfinite(rb.laf_dB))
                            ? std::abs(ra.laf_dB - rb.laf_dB) : -1.0f,
                        (std::isnan(ra.laf_dB) || std::isinf(ra.laf_dB)) ? "⚠" : "ok");
        }
    }

    std::printf("\n=== 稳定性结论 ===\n");
    std::printf("  处理秒数:        %zu\n", total_sec);
    std::printf("  NaN 计数:        %d  %s\n", nan_count, nan_count == 0 ? "✅" : "❌");
    std::printf("  Inf 计数:        %d  %s\n", inf_count, inf_count == 0 ? "✅" : "❌");
    std::printf("  LAF 范围:        [%.2f, %.2f] dBA  (跨度 %.2f dB)\n",
                laf_min, laf_max, laf_max - laf_min);
    std::printf("  LAS 范围:        [%.2f, %.2f] dBA  (跨度 %.2f dB)\n",
                las_min, las_max, las_max - las_min);
    std::printf("  两路径最大偏差:  %.6f dB  %s\n", max_abs_dev,
                max_abs_dev < 1e-3f ? "✅" : "❌");
    std::printf("  事件分级不一致:  %d 秒  %s\n", disagree, disagree == 0 ? "✅" : "❌");

    const bool ok = (nan_count == 0) && (inf_count == 0) &&
                    (max_abs_dev < 1e-3f) && (disagree == 0);
    std::printf("\n  %s\n", ok
        ? "✅ 长时间运行数值稳定，未重蹈 v3.3.1 覆辙"
        : "❌ 存在稳定性问题，需排查");

    return ok ? 0 : 1;
}
