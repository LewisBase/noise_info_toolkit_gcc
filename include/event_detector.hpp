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
 */
struct EventDetectorConfig {
    //=== 触发阈值（触发灵敏度控制）===
    float leq_threshold_db{90.0f};        ///< LZeq 触发阈值 (dB)
    float peak_threshold_db{130.0f};      ///< LCpeak 触发阈值 (dB)
    float underrange_threshold_db{30.0f}; ///< 信号太弱阈值 (dB)

    //=== 去抖动参数（防误报控制）===
    uint8_t debounce_frames{3};  ///< 连续 N 帧异常才触发（默认 3 帧）
    uint8_t cooldown_frames{5}; ///< 触发后 N 帧内不重复触发（默认 5 帧）

    //=== 参考声压 ===
    float reference_pressure{20e-6f}; ///< 参考声压 (Pa)

    EventDetectorConfig() = default;
};

//==============================================================================
// Event Detector
//==============================================================================

/**
 * @brief Lightweight event detector for embedded platforms
 *
 * Detects anomaly events in audio segments:
 * - Peak trigger: LCpeak >= threshold → OVERLOAD
 * - Level trigger: LZeq >= threshold → IMPULSE_SUSPECT
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
     * @param sample_rate Sample rate in Hz (default: 48000)
     */
    explicit EventDetector(const EventDetectorConfig& config = EventDetectorConfig{},
                           int sample_rate = 48000) noexcept;

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
     * @brief Check if last segment was flagged as IMPULSE_SUSPECT or OVERLOAD
     * @return true if impulse was detected in last check
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
    int sample_rate_;             ///< Sample rate in Hz

    //=== 去抖动状态 ===
    uint8_t consecutive_anomaly_count_{0}; ///< 连续异常帧数
    uint8_t cooldown_remaining_{0};         ///< 冷却剩余帧数

    //=== 标志 ===
    bool impulse_detected_{false}; ///< 上次检测是否触发

    //=== 内部计算 ===
    float compute_leq(const float* start, const float* end) const noexcept;
    float compute_peak(const float* start, const float* end) const noexcept;
};

} // namespace noise_toolkit