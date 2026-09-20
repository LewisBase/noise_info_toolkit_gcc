/**
 * test_event_detector_v3.3.3.cpp — v3.3.3 LAF/LAS 事件检测验证
 *
 * 覆盖四个判定维度：
 *   D1  LAF 阈值（85 / 95 / 110 dB 三档）
 *   D2  脉冲指标（LAF − LAS > 6 / 12 dB）
 *   D3  LAF 上升率（相对 500 ms 前 > 10 / 15 dB）
 *   D4  背景对比（LAF − 5 min 背景 > 15 dB）
 *
 * 用法: ./test_event_detector_v3.3.3
 */

#include "event_detector.hpp"
#include "noise_metrics.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace noise_toolkit;

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* name, const char* detail = "") {
    if (cond) {
        std::printf("  ✅ %s %s\n", name, detail);
        ++g_pass;
    } else {
        std::printf("  ❌ %s %s\n", name, detail);
        ++g_fail;
    }
}

const char* type_name(EventType t) {
    switch (t) {
        case EventType::NONE:     return "NONE";
        case EventType::MINOR:    return "MINOR";
        case EventType::MODERATE: return "MODERATE";
        case EventType::SEVERE:   return "SEVERE";
    }
    return "?";
}

/// 构造一条标准 SecondMetrics（LAF/LAS/LAeq 可指定）
SecondMetrics make_metric(float laf, float las, float laeq = -INFINITY,
                          float lzpeak = -INFINITY, float dur = 1.0f) {
    SecondMetrics m;
    m.duration_s = dur;
    m.LAF = laf;
    m.LAS = las;
    m.LAeq = (std::isfinite(laeq) ? laeq : las);
    m.LZPeak = lzpeak;
    return m;
}

} // namespace

int main() {
    std::printf("\n=== v3.3.3 LAF/LAS 事件检测验证 ===\n");

    // ── D1: LAF 阈值分级 ────────────────────────────────
    std::printf("\n--- D1: LAF 阈值（85 / 95 / 110 dB）---\n");
    {
        EventDetector ed;
        struct Case { float laf; EventType expect; };
        const Case cases[] = {
            {70.0f, EventType::NONE},
            {87.0f, EventType::MINOR},
            {100.0f, EventType::MODERATE},
            {115.0f, EventType::SEVERE},
        };
        for (const auto& c : cases) {
            EventDetector d;   // 每条用例独立 detector
            SecondMetrics m = make_metric(c.laf, c.laf - 1.0f);
            EventResult r = d.check_metrics(m);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "(LAF=%.0f → %s, 期望 %s)",
                          c.laf, type_name(r.event_type), type_name(c.expect));
            check(r.event_type == c.expect, "LAF 阈值分级", buf);
        }
        (void)ed;
    }

    // ── D2: 脉冲指标（LAF − LAS）─────────────────────────
    std::printf("\n--- D2: 脉冲指标（LAF − LAS）---\n");
    {
        // 稳态：LAF ≈ LAS → 无脉冲
        {
            EventDetector d;
            SecondMetrics m = make_metric(60.0f, 60.0f);
            EventResult r = d.check_metrics(m);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "(脉冲=%.1f dB, type=%s)",
                          r.impulse_metric_dB, type_name(r.event_type));
            check(r.impulse_metric_dB < 6.0f, "稳态无脉冲", buf);
        }
        // 冲击：LAF 骤升而 LAS 滞后 → 脉冲指标大
        {
            EventDetector d;
            SecondMetrics m = make_metric(95.0f, 70.0f);
            EventResult r = d.check_metrics(m);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "(脉冲=%.1f dB, type=%s)",
                          r.impulse_metric_dB, type_name(r.event_type));
            check(r.impulse_metric_dB > 12.0f, "冲击脉冲指标 > 12 dB", buf);
            check(r.trigger_impulse, "脉冲触发标志", "");
        }
        // 中度脉冲：LAF − LAS = 8 dB（between 6 and 12）
        {
            EventDetector d;
            SecondMetrics m = make_metric(70.0f, 62.0f);   // 70 < 85 → 不触发 D1
            EventResult r = d.check_metrics(m);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "(脉冲=%.1f dB, type=%s, 期望 MINOR)",
                          r.impulse_metric_dB, type_name(r.event_type));
            check(r.event_type == EventType::MINOR, "脉冲 MINOR 级", buf);
        }
    }

    // ── D3: LAF 上升率 ──────────────────────────────────
    std::printf("\n--- D3: LAF 上升率（相对 500 ms 前）---\n");
    {
        EventDetector d;
        // 先跑 1 秒稳态 70 dB
        for (int i = 0; i < 2; ++i) {
            SecondMetrics m = make_metric(70.0f, 70.0f, 70.0f, -INFINITY, 0.5f);
            d.check_metrics(m);
        }
        // 突然升到 90 dB
        SecondMetrics jump = make_metric(90.0f, 72.0f, 90.0f, -INFINITY, 0.5f);
        EventResult r = d.check_metrics(jump);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "(上升=%.1f dB, type=%s)",
                      r.laf_rise_dB, type_name(r.event_type));
        check(r.laf_rise_dB > 15.0f, "上升率 > 15 dB", buf);
        check(r.trigger_onset, "onset 触发标志", "");
    }
    {
        // 稳态信号不应触发上升率
        EventDetector d;
        EventResult r;
        for (int i = 0; i < 5; ++i) {
            SecondMetrics m = make_metric(70.0f, 70.0f, 70.0f, -INFINITY, 0.5f);
            r = d.check_metrics(m);
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "(上升=%.2f dB, type=%s)",
                      r.laf_rise_dB, type_name(r.event_type));
        check(std::abs(r.laf_rise_dB) < 1.0f, "稳态上升率 ≈ 0", buf);
    }

    // ── D4: 背景对比 ────────────────────────────────────
    std::printf("\n--- D4: 背景对比（LAF − 5 min 背景）---\n");
    {
        EventDetector d;
        // 背景建立：跑 20 秒 50 dB
        for (int i = 0; i < 20; ++i) {
            SecondMetrics m = make_metric(50.0f, 50.0f, 50.0f, -INFINITY, 1.0f);
            d.check_metrics(m);
        }
        // 突然 70 dB（比背景高 20 dB）
        SecondMetrics spike = make_metric(70.0f, 65.0f, 70.0f, -INFINITY, 1.0f);
        EventResult r = d.check_metrics(spike);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "(背景差=%.1f dB, type=%s)",
                      r.background_delta_dB, type_name(r.event_type));
        check(r.background_delta_dB > 15.0f, "背景差 > 15 dB", buf);
        check(r.trigger_background, "背景触发标志", "");
    }

    // ── 零输入 / 静音 ───────────────────────────────────
    std::printf("\n--- 边界：零输入 / 静音 ---\n");
    {
        EventDetector d;
        SecondMetrics m = make_metric(-INFINITY, -INFINITY, -INFINITY);
        EventResult r = d.check_metrics(m);
        check(r.event_type == EventType::NONE, "零输入 → NONE", "");
        check(r.severity == 0, "零输入 severity = 0", "");
    }

    // ── 过载直通 ────────────────────────────────────────
    std::printf("\n--- 兼容：过载直通（LZPeak >= 140 dB）---\n");
    {
        EventDetector d;
        SecondMetrics m = make_metric(70.0f, 70.0f, 70.0f, 145.0f);
        EventResult r = d.check_metrics(m);
        check(r.is_overload, "过载标志", "");
        check(r.event_type == EventType::SEVERE, "过载 → SEVERE", "");
        check(r.severity == 100, "过载 severity = 100", "");
    }

    // ── reset() 清空状态 ────────────────────────────────
    std::printf("\n--- reset() 状态清空 ---\n");
    {
        EventDetector d;
        for (int i = 0; i < 5; ++i) {
            SecondMetrics m = make_metric(90.0f, 90.0f, 90.0f, -INFINITY, 1.0f);
            d.check_metrics(m);
        }
        d.reset();
        SecondMetrics m = make_metric(90.0f, 90.0f, 90.0f, -INFINITY, 1.0f);
        EventResult r = d.check_metrics(m);
        check(r.laf_rise_dB == 0.0f, "reset 后上升率 = 0", "");
        check(r.background_delta_dB == 0.0f, "reset 后背景 = 0", "");
    }

    // ── 旧接口回归（check_segment 仍可用）───────────────
    std::printf("\n--- 回归：旧接口 check_segment 仍可用 ---\n");
    {
        EventDetector d;
        std::vector<float> silence(48000, 0.0f);
        EventCheckResult r = d.check_segment(silence.data(), silence.data() + silence.size());
        check(r == EventCheckResult::UNDERRANGE, "静音 → UNDERRANGE", "");

        std::vector<float> loud(48000, 1.0f);   // 1 Pa ≈ 94 dB → 不触发 140 dB 过载
        EventCheckResult r2 = d.check_segment(loud.data(), loud.data() + loud.size());
        check(r2 != EventCheckResult::OVERLOAD, "94 dB → 非过载", "");
    }

    std::printf("\n=== 结果: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
