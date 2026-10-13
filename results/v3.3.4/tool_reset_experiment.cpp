// /tmp/t_reset.cpp — v3.3.4 验收 · 短窗机理对照实验
// 同一 10 ms 分块方式，两种滤波器状态策略：
//   (a) CONTINUOUS — 同一个 processor 连续处理（= 当前 v3.3.2+ 行为）
//   (b) RESET      — 每块新建 processor（= v3.3.2 之前的行为）
// 编译： g++ -O2 -Iinclude /tmp/t_reset.cpp build_test/libnoise_toolkit.a -o /tmp/t_reset
#include <cstdio>
#include <cmath>
#include <vector>
#include "noise_processor.hpp"

using namespace noise_toolkit;
static const int FS = 48000;

static std::vector<float> gen_sine(float freq, float spl_db, float dur) {
    const float rms = 20e-6f * std::pow(10.0f, spl_db / 20.0f);
    const float peak = rms * std::sqrt(2.0f);
    int n = static_cast<int>(FS * dur);
    std::vector<float> d(n);
    for (int i = 0; i < n; ++i)
        d[i] = peak * std::sin(2.0f * 3.14159265358979f * freq * i / FS);
    return d;
}

int main() {
    const float freqs[] = {10, 12.5f, 16, 20, 25, 31.5f, 63, 125, 1000, 4000};
    const int NF = 10;
    const float dur = 3.0f;
    const int blk = FS / 100;          // 10 ms
    const float skip = 1.0f;           // 前 1 s 不计入统计

    std::printf("# 10 ms 分块 · 滤波器状态策略对照（94 dB SPL 纯音，前 %.0fs 不计）\n", skip);
    std::printf("freq,A_ref,CONT_LAeq,dA_cont,RESET_LAeq,dA_reset,"
                "CONT_blk_min,CONT_blk_max,RESET_blk_min,RESET_blk_max\n");
    for (int k = 0; k < NF; ++k) {
        float f = freqs[k];
        std::vector<float> tone = gen_sine(f, 94.0f, dur);
        int nblk = static_cast<int>(tone.size()) / blk;
        int i0 = static_cast<int>(FS * skip) / blk;

        // ---- (a) 连续状态 ----
        double pa = 0; int ca = 0; float amin = 1e9f, amax = -1e9f;
        {
            NoiseProcessor proc(FS);
            for (int b = 0; b < nblk; ++b) {
                auto m = proc.process_segment(tone.data() + b*blk, tone.data() + (b+1)*blk, 0.01f);
                if (b < i0) continue;
                pa += std::pow(10.0, m.LAeq/10.0); ++ca;
                if (m.LAeq < amin) amin = m.LAeq;
                if (m.LAeq > amax) amax = m.LAeq;
            }
        }
        // ---- (b) 每块重置状态 ----
        double pb = 0; int cb = 0; float bmin = 1e9f, bmax = -1e9f;
        for (int b = 0; b < nblk; ++b) {
            NoiseProcessor proc(FS);
            auto m = proc.process_segment(tone.data() + b*blk, tone.data() + (b+1)*blk, 0.01f);
            if (b < i0) continue;
            pb += std::pow(10.0, m.LAeq/10.0); ++cb;
            if (m.LAeq < bmin) bmin = m.LAeq;
            if (m.LAeq > bmax) bmax = m.LAeq;
        }
        float la_c = static_cast<float>(10.0*std::log10(pa/ca));
        float la_r = static_cast<float>(10.0*std::log10(pb/cb));
        std::printf("%g,%.1f,%.3f,%+.3f,%.3f,%+.3f,%.2f,%.2f,%.2f,%.2f\n",
                    f, 0.0, la_c, la_c-94.0f, la_r, la_r-94.0f, amin, amax, bmin, bmax);
    }
    return 0;
}
