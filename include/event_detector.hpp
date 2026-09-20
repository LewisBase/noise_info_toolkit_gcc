/**
 * @file event_detector.hpp
 * @brief Lightweight event detector for impact noise detection
 *
 * v3.1.2 — Simplified event detection for embedded platforms.
 * Zero heap allocation, all state in class members.
 * Returns anomaly type or NORMAL per segment.
 */

#pragma once

#include <cstdint>
#include <cmath>
#include "noise_metrics.hpp"
#include "filter_coefficients_48k.hpp"
#include "weighting_coefficients_multirate.hpp"

namespace noise_toolkit {

//==============================================================================
// Event Detection Result
//==============================================================================

/**
 * @brief Event detection result enumeration
 *
 * Used by embedded engineers to mark event boundaries:
 * - NORMAL: no anomaly, pass through
 * - OVERLOAD: peak exceeds threshold, mark as start
 * - UNDERRANGE: signal too weak, check sensor
 * - IMPULSE_SUSPECT:疑似冲击噪声，mark as start
 */
enum class EventCheckResult : uint8_t {
    NORMAL = 0,         ///< 无异常，正常通过
    OVERLOAD = 1,       ///< 过载（峰值超标）
    UNDERRANGE = 2,     ///< 信号太弱（低于阈值）
    IMPULSE_SUSPECT = 3 ///< 疑似冲击噪声（需要标记起始点）
};

//==============================================================================
// v3.3.3: LAF/LAS-based Event Grading
//==============================================================================

/**
 * @brief Event severity grade (v3.3.3)
 *
 * 基于 LAF（Fast 时间加权 SPL，τ=125ms）的四维度判定结果分级。
 * 与 EventCheckResult 共存：旧接口返回类别，新接口返回分级 + 诊断细节。
 */
enum class EventType : uint8_t {
    NONE     = 0,  ///< 无事件
    MINOR    = 1,  ///< 轻微事件（LAF > 85 dB 或上升 > 10 dB 或脉冲 > 6 dB）
    MODERATE = 2,  ///< 中度事件（LAF > 95 dB 或上升 > 15 dB）
    SEVERE   = 3,  ///< 严重事件（LAF > 110 dB 或脉冲 > 12 dB）
};

/**
 * @brief Detailed event detection result (v3.3.3)
 *
 * 同时给出最终分级和四个维度的原始判定值，便于嵌入式端记录/调试。
 */
struct EventResult {
    EventType event_type{EventType::NONE};  ///< 最终事件分级
    uint8_t severity{0};                    ///< 严重程度评分 0-100

    //=== 四个维度的原始值 ===
    float laf_dB{-INFINITY};              ///< 当前 A 加权 Fast 时间加权 SPL
    float las_dB{-INFINITY};              ///< 当前 A 加权 Slow 时间加权 SPL
    float impulse_metric_dB{0.0f};        ///< LAF − LAS（脉冲指标）
    float laf_rise_dB{0.0f};              ///< LAF(t) − LAF(t−500ms)（上升率）
    float background_delta_dB{0.0f};      ///< LAF − LAeq(5min 背景)

    //=== 触发标志（哪几个维度命中）===
    bool trigger_laf_threshold{false};    ///< D1: LAF 超阈值
    bool trigger_impulse{false};          ///< D2: 脉冲指标超阈值
    bool trigger_onset{false};            ///< D3: LAF 上升率超阈值
    bool trigger_background{false};       ///< D4: 背景对比超阈值

    //=== 兼容字段 ===
    bool is_overload{false};              ///< LZPeak 过载（与旧接口保持一致）
};

//==============================================================================
// Event Detection Configuration
//==============================================================================

/**
 * @brief Event detector configuration structure
 *
 * All parameters configurable at runtime or compile-time.
 * Design consistency: thresholds + debounce counters both via config.
 *
 * Input buffer must be Z-weighted samples in Pa (same as NoiseProcessor raw path).
 * Peak trigger compares LZpeak; level trigger compares LZeq.
 * Defaults align with NoiseProcessor overload / underrange thresholds.
 */
struct EventDetectorConfig {
    //=== 触发阈值（触发灵敏度控制）===
    float leq_threshold_db{INFINITY};  // [v3.3.0] LZeq trigger disabled; restore to 90.0f to re-enable
    float peak_threshold_db{OVERLOAD_THRESHOLD};            ///< LZpeak 触发阈值 (dB)，默认 140
    float underrange_threshold_db{UNDERRANGE_THRESHOLD};    ///< LZeq 欠量程阈值 (dB)，默认 30

    //=== 去抖动参数（防误报控制）===
    uint8_t debounce_frames{3};  ///< 连续 N 帧异常才触发（0 视为 1）
    uint8_t cooldown_frames{5};  ///< 声级触发后 N 帧内不重复 IMPULSE（峰值过载不受限）

    //=== 参考声压（保持原有位置，避免位置初始化错位）===
    float reference_pressure{REFERENCE_PRESSURE}; ///< 参考声压 (Pa)

    //=== v3.3.3 新增：采样率（供 buf/end 重载计算 α）===
    int sample_rate{48000};   ///< 采样率（Hz）

    //=== v3.3.3 新增：LAF/LAS 事件检测阈值（追加在末尾，前向兼容）===
    // D1: LAF 阈值分级（工业噪声合规常用 85/95/110 三档）
    float laf_minor_db{85.0f};      ///< 轻微事件：LAF ≥ 85 dB
    float laf_moderate_db{95.0f};   ///< 中度事件：LAF ≥ 95 dB
    float laf_severe_db{110.0f};    ///< 严重事件：LAF ≥ 110 dB（听力损伤风险）

    // D2: 脉冲指标（LAF − LAS）
    float impulse_minor_db{6.0f};   ///< 脉冲事件：LAF−LAS ≥ 6 dB（IEC/商用通用阈值）
    float impulse_severe_db{12.0f}; ///< 严重脉冲：LAF−LAS ≥ 12 dB

    // D3: LAF 上升率（相对于 500 ms 前）
    float laf_rise_minor_db{10.0f};    ///< 事件起点：LAF 上升 ≥ 10 dB
    float laf_rise_moderate_db{15.0f}; ///< 中度上升：LAF 上升 ≥ 15 dB

    // D4: 背景对比（相对于 5 min 指数平均背景）
    float background_delta_db{15.0f};  ///< 显著事件：LAF − 背景 ≥ 15 dB
    float background_tau_s{300.0f};    ///< 背景平均时间常数（默认 5 min）

    EventDetectorConfig() = default;
};

//==============================================================================
// Event Detector
//==============================================================================

/**
 * @brief Lightweight event detector for embedded platforms
 *
 * Detects anomaly events in audio segments:
 * - Peak trigger: LZpeak >= threshold → OVERLOAD（不受 cooldown 抑制）
 * - Level trigger: [v3.3.0 disabled] LZeq >= threshold → IMPULSE_SUSPECT（debounce + cooldown）
 *
 * Design principles:
 * - Zero heap allocation (all state in class members)
 * - Independent from NoiseProcessor (方案 A: 独立接口)
 * - All parameters configurable via EventDetectorConfig
 *
 * [v3.3.0] LZeq-based IMPULSE_SUSPECT trigger DISABLED by default
 * (leq_threshold_db defaults to INFINITY). OVERLOAD via LZpeak >= 140 dB
 * remains the recommended single-trigger interface. See src/event_detector.cpp
 * for the commented-out legacy LZeq trigger logic.
 */
class EventDetector {
public:
    /**
     * @brief Constructor
     * @param config Event detection configuration (default: all defaults)
     */
    explicit EventDetector(const EventDetectorConfig& config = EventDetectorConfig{}) noexcept;

    /**
     * @brief Check if current segment has anomaly
     *
     * Lightweight interface: returns anomaly type or NORMAL.
     * Zero heap allocation, all state in class members.
     *
     * @param buffer_start Pointer to start of PCM buffer (float samples, Z-weighted Pa)
     * @param buffer_end Pointer to end of PCM buffer
     * @return EventCheckResult: NORMAL or anomaly type
     */
    EventCheckResult check_segment(const float* buffer_start,
                                    const float* buffer_end) noexcept;

    /**
     * @brief v3.3.3: 基于 LAF/LAS 的多维度事件检测（已算好的指标）
     *
     * 四个判定维度：
     *   D1  LAF 阈值（85 / 95 / 110 dB 三档）
     *   D2  脉冲指标（LAF − LAS > 6 / 12 dB）
     *   D3  LAF 上升率（相对 500 ms 前 > 10 / 15 dB）
     *   D4  背景对比（LAF − 5 min 指数平均背景 > 15 dB）
     *
     * 输入为 v3.3.2 起 NoiseProcessor 输出的 SecondMetrics（含 LAF/LAS 字段）。
     * 本接口与 NoiseProcessor 解耦（调用方负责把 result 写回 metric.event_type）。
     *
     * @param m 当前秒的 SecondMetrics（使用 LAF / LAS / LAeq / LZPeak / duration_s）
     * @return EventResult（分级 + 四维度原始值 + 触发标志）
     */
    EventResult check_metrics(const SecondMetrics& m) noexcept;

    /**
     * @brief v3.3.3: 基于 LAF/LAS 的多维度事件检测（原始缓冲区）
     *
     * 与上一个重载等价的 **设备侧接口**：只需传 buf/end，无需 NoiseProcessor。
     * 内部用自带的轻量 A 计权 biquad 链 + Fast/Slow 时间计权状态，
     * 逐样本算出 LAF / LAS / LAeq / LZPeak，再走同一个四维度判定。
     *
     * 内存开销（与 NoiseProcessor 无关）：
     *   4 段 biquad 系数 96 B + 状态 32 B + 时间计权 16 B ≈ 150 B
     *
     * @param start 缓冲区起始（float，Pa，Z 计权）
     * @param end   缓冲区结尾
     * @return EventResult
     */
    EventResult check_metrics(const float* start, const float* end) noexcept;

    /**
     * @brief Reset detector state (clear all counters and flags)
     */
    void reset() noexcept;

    /**
     * @brief Check if an IMPULSE_SUSPECT or OVERLOAD was detected and not yet cleared
     * @return true until clear_impulse_flag() or reset()
     */
    bool was_impulse_detected() const noexcept { return impulse_detected_; }

    /**
     * @brief Clear impulse flag (call after marking start point)
     */
    void clear_impulse_flag() noexcept { impulse_detected_ = false; }

    /**
     * @brief Get current configuration reference
     */
    const EventDetectorConfig& config() const { return config_; }

private:
    EventDetectorConfig config_;  ///< Configuration (copied for independence)

    //=== 去抖动状态 ===
    uint8_t consecutive_anomaly_count_{0}; ///< 连续异常帧数
    uint8_t cooldown_remaining_{0};        ///< 冷却剩余帧数

    //=== 标志 ===
    bool impulse_detected_{false}; ///< 已触发且未清除

    //=== v3.3.3: LAF 上升率历史（环形缓冲）===
    static constexpr size_t LAF_HISTORY_SIZE = 16;
    float laf_history_[LAF_HISTORY_SIZE]{};   ///< LAF 历史（dB）
    float time_history_[LAF_HISTORY_SIZE]{};  ///< 对应累计时间（s）
    size_t history_write_idx_{0};
    size_t history_count_{0};
    float cumulative_time_s_{0.0f};           ///< 累计已处理时长

    //=== v3.3.3: 5 min 指数平均背景 ===
    float background_power_{0.0f};       ///< LAeq 的功率域指数平均
    float background_laeq_db_{-INFINITY}; ///< 背景 LAeq（dB）
    bool background_initialized_{false};

    //=== v3.3.3: 内置轻量 A 计权链（供 buf/end 重载使用）===
    BiquadChain<A_WEIGHTING_SECTIONS> a_chain_;  ///< A 计权 4 段 biquad（自带状态）
    float a_gain_{1.0f};                          ///< 1 kHz 归一化增益
    float laf_sq_{0.0f};                          ///< Fast 时间计权功率状态
    float las_sq_{0.0f};                          ///< Slow 时间计权功率状态
    float alpha_F_{0.0f};                         ///< Fast α = exp(−1/(0.125·fs))
    float alpha_S_{0.0f};                         ///< Slow α = exp(−1/(1.0·fs))

    //=== 内部计算 ===
    float compute_leq(const float* start, const float* end) const noexcept;
    float compute_peak(const float* start, const float* end) const noexcept;
    static uint8_t required_debounce_frames(uint8_t debounce_frames) noexcept;

    //=== v3.3.3 内部辅助 ===
    void init_internal_chain() noexcept;
    static float power_to_db(float power_sq, float p_ref) noexcept;
    void push_laf_history(float laf_db, float dt) noexcept;
    float lookback_laf(float seconds_ago) const noexcept;
    void update_background(float laeq_db, float dt) noexcept;
    static uint8_t grade_severity(const EventResult& r,
                                   const EventDetectorConfig& c) noexcept;
};

} // namespace noise_toolkit
