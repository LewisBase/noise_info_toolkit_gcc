/**
 * @file event_detector.cpp
 * @brief EventDetector implementation — v3.1.2
 *
 * Zero heap allocation, all state in class members.
 * Supports peak trigger and level trigger with frame-based debounce.
 */

#include "event_detector.hpp"
#include <algorithm>
#include <limits>

namespace noise_toolkit {

EventDetector::EventDetector(const EventDetectorConfig& config) noexcept
    : config_(config),
      consecutive_anomaly_count_(0),
      cooldown_remaining_(0),
      impulse_detected_(false) {}

uint8_t EventDetector::required_debounce_frames(uint8_t debounce_frames) noexcept {
    return debounce_frames == 0 ? 1 : debounce_frames;
}

EventCheckResult EventDetector::check_segment(const float* buffer_start,
                                               const float* buffer_end) noexcept {
    if (buffer_start >= buffer_end) {
        return EventCheckResult::NORMAL;
    }

    float lzeq = compute_leq(buffer_start, buffer_end);
    float lzpeak = compute_peak(buffer_start, buffer_end);

    // Peak trigger: highest priority, bypasses cooldown and debounce
    if (lzpeak >= config_.peak_threshold_db) {
        impulse_detected_ = true;
        cooldown_remaining_ = config_.cooldown_frames;
        consecutive_anomaly_count_ = 0;
        return EventCheckResult::OVERLOAD;
    }

    // Cooldown suppresses level trigger only (not overload above)
    if (cooldown_remaining_ > 0) {
        --cooldown_remaining_;
        if (lzeq < config_.underrange_threshold_db) {
            return EventCheckResult::UNDERRANGE;
        }
        return EventCheckResult::NORMAL;
    }

    const uint8_t required_frames = required_debounce_frames(config_.debounce_frames);

    if (lzeq >= config_.leq_threshold_db) {
        if (consecutive_anomaly_count_ < UINT8_MAX) {
            ++consecutive_anomaly_count_;
        }
        if (consecutive_anomaly_count_ >= required_frames) {
            impulse_detected_ = true;
            cooldown_remaining_ = config_.cooldown_frames;
            consecutive_anomaly_count_ = 0;
            return EventCheckResult::IMPULSE_SUSPECT;
        }
    } else {
        consecutive_anomaly_count_ = 0;
    }

    if (lzeq < config_.underrange_threshold_db) {
        return EventCheckResult::UNDERRANGE;
    }

    return EventCheckResult::NORMAL;
}

void EventDetector::reset() noexcept {
    consecutive_anomaly_count_ = 0;
    cooldown_remaining_ = 0;
    impulse_detected_ = false;
}

float EventDetector::compute_leq(const float* start, const float* end) const noexcept {
    float sum_sq = 0.0f;
    int count = 0;

    for (const float* p = start; p < end; ++p) {
        sum_sq += (*p) * (*p);
        ++count;
    }

    if (count == 0 || sum_sq <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }

    float rms = std::sqrt(sum_sq / static_cast<float>(count));
    if (rms <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }

    float p0 = config_.reference_pressure;
    return 20.0f * std::log10(rms / p0);
}

float EventDetector::compute_peak(const float* start, const float* end) const noexcept {
    float peak_abs = 0.0f;

    for (const float* p = start; p < end; ++p) {
        float abs_val = std::abs(*p);
        if (abs_val > peak_abs) {
            peak_abs = abs_val;
        }
    }

    if (peak_abs <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }

    float p0 = config_.reference_pressure;
    return 20.0f * std::log10(peak_abs / p0);
}

} // namespace noise_toolkit
