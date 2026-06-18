/**
 * @file noise_processor.hpp
 * @brief Lightweight noise metrics processor with two simple interfaces
 *
 * v3.1 — Streaming architecture: zero heap allocation in hot path.
 * A/C weighting and 1/3 octave bandpass filters are persistent across
 * process_segment() calls, maintaining state for continuous audio streams.
 *
 * Interface 1 (per-segment): process_segment(buffer_start, buffer_end, duration_s)
 *   - Call with raw PCM buffer pointers and desired processing duration
 *   - Default duration_s = 0.01f (10 ms)
 *   - Returns SecondMetrics (81 indicators)
 *
 * Interface 2 (aggregation): aggregate_metrics(metrics, count, unit_duration_s)
 *   - Call with array of SecondMetrics and unit duration
 *   - Default unit_duration_s = 1.0f (1 second per metric)
 *   - Returns MinuteMetrics (aggregated indicators)
 */

#pragma once

#include "noise_metrics.hpp"
#include "dose_calculator.hpp"
#include "iir_filter.hpp"
#include "filter_coefficients_48k.hpp"
#include "bandpass_coefficients_48k.hpp"
#include <array>

namespace noise_toolkit {

//==============================================================================
// Noise Processor - Two-Interface Design
//==============================================================================

class NoiseProcessor {
public:
    /**
     * @brief Constructor
     * @param sample_rate Audio sample rate (default 48000 Hz)
     * @param reference_pressure Reference sound pressure (default 20 μPa)
     */
    explicit NoiseProcessor(int sample_rate = 48000,
                            float reference_pressure = REFERENCE_PRESSURE);

    ~NoiseProcessor() = default;

    //==========================================================================
    // Interface 1: Per-Segment Processing
    //==========================================================================

    /**
     * @brief Process audio segment from raw PCM buffer (float)
     *
     * Streaming implementation: A/C weighting and bandpass filters process
     * each sample in-place with zero heap allocation. Filter state persists
     * across calls for continuous audio streams.
     *
     * @param buffer_start Pointer to start of PCM buffer (float samples)
     * @param buffer_end Pointer to end of PCM buffer
     * @param duration_s Duration of audio segment in seconds (default 0.01f = 10ms)
     * @return SecondMetrics containing all 81 indicators
     *
     * @note buffer must contain exactly sample_rate_ * duration_s samples
     *       If buffer length doesn't match, returns default/zero metrics
     */
    SecondMetrics process_segment(const float* buffer_start,
                                   const float* buffer_end,
                                   float duration_s = 0.01f) noexcept;

    //==========================================================================
    // Interface 2: Aggregation
    //==========================================================================

    /**
     * @brief Aggregate multiple metrics into aggregated metrics
     *
     * @param metrics Pointer to array of SecondMetrics
     * @param count Number of metrics to aggregate
     * @param unit_duration_s Duration each SecondMetrics represents (default 1.0f)
     * @return MinuteMetrics containing aggregated indicators
     *
     * @note Total time covered = unit_duration_s * count
     *       Example: unit_duration_s=0.01f, count=6000 -> 60 seconds total
     */
    MinuteMetrics aggregate_metrics(const SecondMetrics* metrics,
                                     int count,
                                     float unit_duration_s = 1.0f) noexcept;

    /**
     * @brief Aggregate from std::array of SecondMetrics
     */
    template<size_t N>
    MinuteMetrics aggregate_metrics(
        const std::array<SecondMetrics, N>& metrics_array,
        float unit_duration_s = 1.0f) noexcept {
        return aggregate_metrics(metrics_array.data(), static_cast<int>(N), unit_duration_s);
    }

    //==========================================================================
    // Utility
    //==========================================================================

    /** @brief Get configured sample rate */
    int sample_rate() const { return sample_rate_; }

    /** @brief Get configured reference pressure */
    float reference_pressure() const { return reference_pressure_; }

private:
    int sample_rate_;
    float reference_pressure_;

    // === Persistent filter state (v3.1 streaming architecture) ===

    // A/C weighting biquad chains (pre-computed for 48kHz)
    BiquadChain<A_WEIGHTING_SECTIONS> a_weight_chain_;
    BiquadChain<C_WEIGHTING_SECTIONS> c_weight_chain_;

    // v3.2.1: 1kHz normalization factors applied AFTER biquad chain output
    // (separate from biquad b/a coefficients — see weighting_coefficients_multirate.hpp)
    float a_weight_gain_ = 1.0f;
    float c_weight_gain_ = 1.0f;

    // 9 × 1/3 octave bandpass filters (persistent)
    BiquadFilter band_filters_[9] = {
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        BiquadFilter(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f)
    };
};

} // namespace noise_toolkit
