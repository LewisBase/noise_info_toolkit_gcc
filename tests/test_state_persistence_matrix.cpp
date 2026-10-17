/**
 * @file test_state_persistence_matrix.cpp
 * @brief 50 ms 窗口下 A 计权 20 Hz 偏差的**根因矩阵实验**
 *
 * 背景（2026-10-17）：
 *   李工实测 50 ms/1 s 窗口：C 计权已达标，A 计权 20 Hz 偏差 > 4 dB，其余频点 Class 1。
 *   v3.3.4 自带验证（10/100/1000 ms）全部正常 → 50 ms 是新暴露的窗口。
 *
 * 本实验区分**三种固件集成方式**（同一 20 Hz 纯音、同一分块大小）：
 *
 *   M1  单一 NoiseProcessor 实例，连续 process_segment()   ← v3.3.2 设计意图（正确）
 *   M2  每个 block 新建 NoiseProcessor 再 process_segment() ← 固件常见误用（状态每次清零）
 *   M3  每个 block 调 apply_*_weighting_inplace()           ← signal_utils 路径（内部 reset）
 *
 * 对每种方式跑 {50, 100, 1000} ms 分块，给出：
 *   - 能量平均 LAeq/LCeq 与 IEC 61672-1 解析式的偏差
 *   - 单块读数的离散度（min/max/极差），用于判断"单次读数"是否会误判为偏差
 *
 * 判据为 IEC 解析式独立计算，不与被测代码同源。
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

#include "noise_processor.hpp"
#include "signal_utils.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

namespace {

constexpr int   FS        = 48000;
constexpr float FREQ      = 20.0f;
constexpr float AMP       = 1.0f;
constexpr float P_REF     = 20e-6f;
constexpr float WARMUP_S  = 3.0f;
constexpr float MEASURE_S = 3.0f;

double a_db(double f) {
    const double f2 = f * f;
    const double num = 12194.0 * 12194.0 * f2 * f2;
    const double den = (f2 + 20.6 * 20.6) *
                       std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) *
                       (f2 + 12194.0 * 12194.0);
    return 20.0 * std::log10(num / den) + 2.00;
}
double c_db(double f) {
    const double f2 = f * f;
    const double num = (12194.0 * 12194.0) * f2;
    const double den = (f2 + 20.6 * 20.6) * (f2 + 12194.0 * 12194.0);
    return 20.0 * std::log10(num / den) + 0.06;
}

std::vector<float> tone(float dur_s) {
    const int n = static_cast<int>(FS * dur_s);
    std::vector<float> d(n);
    for (int i = 0; i < n; ++i)
        d[i] = AMP * std::sin(noise_const::TWO_PI_F * FREQ * i / FS);
    return d;
}

float rms_spl(float rms) { return 20.0f * std::log10(rms / P_REF); }

// 能量平均（正确做法），输入为逐块 dB 值
float energy_avg(const std::vector<float>& dbs) {
    double s = 0.0;
    for (float l : dbs) s += std::pow(10.0, l / 10.0);
    return static_cast<float>(10.0 * std::log10(s / dbs.size()));
}

struct Result {
    float energy_mean;   // 能量平均后的 Leq
    float min_db, max_db;
    int   n;
};

// M1: 单一实例连续
Result M1(float block_s, bool use_a) {
    NoiseProcessor proc(FS);
    const int blk = static_cast<int>(FS * block_s);
    const auto t = tone(WARMUP_S + MEASURE_S);
    std::vector<float> vals;
    for (size_t off = 0; off + blk <= t.size(); off += blk) {
        auto m = proc.process_segment(t.data() + off, t.data() + off + blk, block_s);
        const float l = use_a ? m.LAeq : m.LCeq;
        if (std::isfinite(l) && l > -20.0f) vals.push_back(l);   // 跳过上电瞬态
    }
    return {energy_avg(vals), *std::min_element(vals.begin(), vals.end()),
            *std::max_element(vals.begin(), vals.end()), (int)vals.size()};
}

// M2: 每块新建实例
Result M2(float block_s, bool use_a) {
    const int blk = static_cast<int>(FS * block_s);
    const auto t = tone(WARMUP_S + MEASURE_S);
    std::vector<float> vals;
    for (size_t off = 0; off + blk <= t.size(); off += blk) {
        NoiseProcessor proc(FS);          // ← 每块重建 → 状态清零
        auto m = proc.process_segment(t.data() + off, t.data() + off + blk, block_s);
        const float l = use_a ? m.LAeq : m.LCeq;
        if (std::isfinite(l) && l > -20.0f) vals.push_back(l);
    }
    return {energy_avg(vals), *std::min_element(vals.begin(), vals.end()),
            *std::max_element(vals.begin(), vals.end()), (int)vals.size()};
}

// M3: 每块调 inplace
Result M3(float block_s, bool use_a) {
    const int blk = static_cast<int>(FS * block_s);
    const auto t = tone(WARMUP_S + MEASURE_S);
    std::vector<float> buf(blk), vals;
    for (size_t off = 0; off + blk <= t.size(); off += blk) {
        std::copy(t.begin() + off, t.begin() + off + blk, buf.begin());
        if (use_a) apply_a_weighting_inplace(buf.data(), blk, (float)FS);
        else       apply_c_weighting_inplace(buf.data(), blk, (float)FS);
        vals.push_back(rms_spl(calculate_rms(buf)));
    }
    return {energy_avg(vals), *std::min_element(vals.begin(), vals.end()),
            *std::max_element(vals.begin(), vals.end()), (int)vals.size()};
}

void row(const std::string& name, const Result& r, float expect, float tol) {
    const float dev = r.energy_mean - expect;
    std::cout << std::left  << std::setw(26) << name
              << std::right << std::setw(9) << r.energy_mean
              << std::setw(10) << dev
              << std::setw(9)  << r.min_db
              << std::setw(9)  << r.max_db
              << std::setw(9)  << (r.max_db - r.min_db)
              << std::setw(9)  << r.n
              << (std::abs(dev) <= tol ? "  OK" : "  ✗") << "\n";
}

} // namespace

int main() {
    const float in_spl = rms_spl(AMP / std::sqrt(2.0f));
    const float expA = in_spl + (float)a_db(FREQ);
    const float expC = in_spl + (float)c_db(FREQ);
    constexpr float TOL = 0.5f;   // Class 1

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== 20 Hz 偏差根因矩阵 ===\n";
    std::cout << "输入 " << in_spl << " dB | IEC A=" << a_db(FREQ)
              << " C=" << c_db(FREQ) << " → 期望 LAeq=" << expA
              << " LCeq=" << expC << "\n";
    std::cout << "Class 1 容差 ±" << TOL << " dB\n\n";

    const float blocks[] = {0.050f, 0.100f, 1.000f};
    for (float b : blocks) {
        std::cout << "──────── 分块 " << (b * 1000) << " ms"
                  << "  (20 Hz 周期 " << (1000.0f / FREQ) << " ms = "
                  << b * FREQ << " 个周期) ────────\n";
        std::cout << std::left << std::setw(26) << "方式"
                  << std::right << std::setw(9) << "能量均值"
                  << std::setw(10) << "偏差dB"
                  << std::setw(9) << "最小"
                  << std::setw(9) << "最大"
                  << std::setw(9) << "极差"
                  << std::setw(9) << "块数" << "\n";
        row("M1 连续(正确)   A", M1(b, true),  expA, TOL);
        row("M2 每块新建     A", M2(b, true),  expA, TOL);
        row("M3 inplace      A", M3(b, true),  expA, TOL);
        row("M1 连续(正确)   C", M1(b, false), expC, TOL);
        row("M2 每块新建     C", M2(b, false), expC, TOL);
        row("M3 inplace      C", M3(b, false), expC, TOL);
        std::cout << "\n";
    }
    return 0;
}
