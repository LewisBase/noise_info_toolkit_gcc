/**
 * @file event_detector.cpp
 * @brief EventDetector implementation — v3.1.2
 *
 * Zero heap allocation, all state in class members.
 * Supports peak trigger and level trigger with frame-based debounce.
 */

#include "event_detector.hpp"
#include "math_constants.hpp"
#include <algorithm>

namespace noise_toolkit {

EventDetector::EventDetector(const EventDetectorConfig& config, int sample_rate) noexcept
    : config_(config),
      sample_rate_(sample_rate),
      consecutive_anomaly_count_(0),
      cooldown_remaining_(0),
      impulse_detected_(false) {
    // All state initialized in header (no dynamic allocation)
}

EventCheckResult EventDetector::check_segment(const float* buffer_start,
                                               const float* buffer_end) noexcept {
    // Calculate LZeq and LCpeak for this segment
    float lzeq = compute_leq(buffer_start, buffer_end);
    float lcpeak = compute_peak(buffer_start, buffer_end);

    // Check cooldown period first
    if (cooldown_remaining_ > 0) {
        --cooldown_remaining_;
        impulse_detected_ = false;
        return EventCheckResult::NORMAL;
    }

    // === Peak trigger (highest priority) ===
    if (lcpeak >= config_.peak_threshold_db) {
        impulse_detected_ = true;
        cooldown_remaining_ = config_.cooldown_frames;
        return EventCheckResult::OVERLOAD;
    }

    // === Level trigger ===
    if (lzeq >= config_.leq_threshold_db) {
        ++consecutive_anomaly_count_;
        if (consecutive_anomaly_count_ >= config_.debounce_frames) {
            impulse_detected_ = true;
            cooldown_remaining_ = config_.cooldown_frames;
            consecutive_anomaly_count_ = 0;
            return EventCheckResult::IMPULSE_SUSPECT;
        }
    } else {
        consecutive_anomaly_count_ = 0;
    }

    // === Underrange check ===
    if (lzeq < config_.underrange_threshold_db) {
        return EventCheckResult::UNDERRANGE;
    }

    impulse_detected_ = false;
    return EventCheckResult::NORMAL;
}

void EventDetector::reset() noexcept {
    consecutive_anomaly_count_ = 0;
    cooldown_remaining_ = 0;
    impulse_detected_ = false;
}

float EventDetector::compute_leq(const float* start, const float* end) const noexcept {
    // Calculate RMS from samples (Z-weighted, in Pa)
    float sum_sq = 0.0f;
    int count = 0;

    for (const float* p = start; p < end; ++p) {
        sum_sq += (*p) * (*p);
        ++count;
    }

    if (count == 0 || sum_sq <= 0.0f) {
        return -INFINITY;
    }

    float rms = std::sqrt(sum_sq / static_cast<float>(count));
    if (rms <= 0.0f) {
        return -INFINITY;
    }

    // Convert to dB: SPL = 20 * log10(rms / reference_pressure)
    float p0 = config_.reference_pressure;
    return 20.0f * std::log10(rms / p0);
}

float EventDetector::compute_peak(const float* start, const float* end) const noexcept {
    // Find absolute maximum and convert to dB
    float peak_abs = 0.0f;

    for (const float* p = start; p < end; ++p) {
        float abs_val = std::abs(*p);
        if (abs_val > peak_abs) {
            peak_abs = abs_val;
        }
    }

    if (peak_abs <= 0.0f) {
        return -INFINITY;
    }

    float p0 = config_.reference_pressure;
    return 20.0f * std::log10(peak_abs / p0);
}

} // namespace noise_toolkit