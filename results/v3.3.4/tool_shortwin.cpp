// /tmp/t_shortwin.cpp — v3.3.4 验收 · 任务1
// 纯音扫频 × 不同时间窗口：检查 A/C 计权在各频率下的误差，
// 特别是「短窗口下 A 计权低频偏差 / C 计权高频偏差」是否仍存在。
// 编译： g++ -O2 -Iinclude /tmp/t_shortwin.cpp build_test/libnoise_toolkit.a -o /tmp/t_shortwin
#include <cstdio>
#include <cmath>
#include <vector>
#include "noise_processor.hpp"

using namespace noise_toolkit;

static const int NF = 34;
static const float FREQ[NF] = {
    10, 12.5f, 16, 20, 25, 31.5f,
    40, 50, 63, 80, 100, 125,
    160, 200, 250, 315, 400, 500,
    630, 800, 1000, 1250, 1600, 2000,
    2500, 3150, 4000, 5000, 6300, 8000,
    10000, 12500, 16000, 20000
};
static const float AREF[NF] = {
    -70.4f, -63.4f, -56.7f, -50.5f, -44.7f, -39.4f,
    -34.6f, -30.2f, -26.2f, -22.5f, -19.1f, -16.1f,
    -13.4f, -10.9f, -8.6f, -6.6f, -4.8f, -3.2f,
    -1.9f, -0.8f, 0.0f, 0.6f, 1.0f, 1.2f,
    1.3f, 1.2f, 1.0f, 0.5f, -0.1f, -1.1f,
    -2.5f, -4.3f, -6.6f, -9.3f
};
static const float CREF[NF] = {
    -14.3f, -11.2f, -8.5f, -6.2f, -4.4f, -3.0f,
    -2.0f, -1.3f, -0.8f, -0.5f, -0.3f, -0.2f,
    -0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, -0.1f, -0.2f,
    -0.3f, -0.5f, -0.8f, -1.3f, -2.0f, -3.0f,
    -4.4f, -6.2f, -8.5f, -11.2f
};

static const int   FS  = 48000;
static const float DUR = 3.0f;   // 每个频点生成的纯音时长
static const float SKIP = 1.0f;  // 前 1 s 视为滤波器建立期，不计入统计

static std::vector<float> gen_sine(float freq, float spl_db, float dur) {
    const float rms  = 20e-6f * std::pow(10.0f, spl_db / 20.0f);
    const float peak = rms * std::sqrt(2.0f);
    int n = static_cast<int>(FS * dur);
    std::vector<float> d(n);
    for (int i = 0; i < n; ++i)
        d[i] = peak * std::sin(2.0f * 3.14159265358979f * freq * i / FS);
    return d;
}

// 把 tone 按 blk 样本切块，用同一个 processor 连续处理（滤波器状态连续），
// 跳过前 SKIP 秒，返回能量平均的 LAeq / LCeq。
static void run_blocks(const std::vector<float>& tone, int blk, float& laeq_avg, float& lceq_avg, int& nblk) {
    NoiseProcessor proc(FS);
    const int nskip = static_cast<int>(FS * SKIP);
    double pa = 0.0, pc = 0.0;
    int cnt = 0;
    for (int s = 0; s + blk <= static_cast<int>(tone.size()); s += blk) {
        auto m = proc.process_segment(tone.data() + s, tone.data() + s + blk,
                                      static_cast<float>(blk) / FS);
        if (s + blk <= nskip) continue;
        pa += std::pow(10.0, m.LAeq / 10.0);
        pc += std::pow(10.0, m.LCeq / 10.0);
        ++cnt;
    }
    nblk = cnt;
    laeq_avg = (cnt > 0) ? static_cast<float>(10.0 * std::log10(pa / cnt)) : -999.0f;
    lceq_avg = (cnt > 0) ? static_cast<float>(10.0 * std::log10(pc / cnt)) : -999.0f;
}

int main() {
    const float wins[3] = {0.010f, 0.100f, 1.000f};
    const char* wname[3] = {"10ms", "100ms", "1s"};

    std::printf("# v3.3.4 纯音扫频 × 时间窗口（94 dB SPL 输入；前 %.0fs 建立期不计）\n", SKIP);
    std::printf("# freq_hz,win,LAeq_mean,dA_dB,LCeq_mean,dC_dB,n_blocks\n");

    // 同时输出三个窗长下"最差频点"，便于快速定位
    float worstA[3] = {0, 0, 0}, worstC[3] = {0, 0, 0};
    float wfA[3] = {0, 0, 0}, wfC[3] = {0, 0, 0};

    for (int i = 0; i < NF; ++i) {
        std::vector<float> tone = gen_sine(FREQ[i], 94.0f, DUR);
        for (int w = 0; w < 3; ++w) {
            int blk = static_cast<int>(FS * wins[w]);
            float la, lc; int nb;
            run_blocks(tone, blk, la, lc, nb);
            float dA = la - (94.0f + AREF[i]);
            float dC = lc - (94.0f + CREF[i]);
            std::printf("%g,%s,%.3f,%+.3f,%.3f,%+.3f,%d\n",
                        FREQ[i], wname[w], la, dA, lc, dC, nb);
            if (std::fabs(dA) > worstA[w]) { worstA[w] = std::fabs(dA); wfA[w] = FREQ[i]; }
            if (std::fabs(dC) > worstC[w]) { worstC[w] = std::fabs(dC); wfC[w] = FREQ[i]; }
        }
    }

    std::printf("\n# 汇总：各窗长下最大绝对误差\n");
    std::printf("# win,A_max_err,A_at,C_max_err,C_at\n");
    for (int w = 0; w < 3; ++w)
        std::printf("%s,%.3f,%g,%.3f,%g\n", wname[w], worstA[w], wfA[w], worstC[w], wfC[w]);
    return 0;
}
