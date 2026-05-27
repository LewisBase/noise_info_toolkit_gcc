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
    float leq_threshold_db{90.0f};                          ///< LZeq 触发阈值 (dB)
    float peak_threshold_db{OVERLOAD_THRESHOLD};            ///< LZpeak 触发阈值 (dB)，默认 140
    float underrange_threshold_db{UNDERRANGE_THRESHOLD};    ///< LZeq 欠量程阈值 (dB)，默认 30

    //=== 去抖动参数（防误报控制）===
    uint8_t debounce_frames{3};  ///< 连续 N 帧异常才触发（0 视为 1）
    uint8_t cooldown_frames{5};  ///< 声级触发后 N 帧内不重复 IMPULSE（峰值过载不受限）

    //=== 参考声压 ===
    float reference_pressure{REFERENCE_PRESSURE}; ///< 参考声压 (Pa)

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
 * - Level trigger: LZeq >= threshold → IMPULSE_SUSPECT（debounce + cooldown）
 *
 * Design principles:
 * - Zero heap allocation (all state in class members)
 * - Independent from NoiseProcessor (方案 A: 独立接口)
 * - All parameters configurable via EventDetectorConfig
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

    //=== 内部计算 ===
    float compute_leq(const float* start, const float* end) const noexcept;
    float compute_peak(const float* start, const float* end) const noexcept;
    static uint8_t required_debounce_frames(uint8_t debounce_frames) noexcept;
};

} // namespace noise_toolkit
