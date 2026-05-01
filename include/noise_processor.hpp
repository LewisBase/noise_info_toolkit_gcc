/**
 * @file noise_processor.hpp
 * @brief Lightweight noise metrics processor with two simple interfaces
 *
 * Interface 1 (per-segment): process_one_second(buffer_start, buffer_end, processing_duration_s)
 *   - Call with raw PCM buffer pointers and desired processing duration
 *   - Default processing_duration_s = 0.01f (10 ms)
 *   - Returns SecondMetrics (81 indicators)
 *
 * Interface 2 (aggregation): aggregate_minute_metrics(second_metrics, count, unit_duration_s)
 *   - Call with array of SecondMetrics and unit duration
 *   - Default unit_duration_s = 1.0f (1 second per metric)
 *   - Returns MinuteMetrics (aggregated indicators)
 */

#pragma once

#include "noise_metrics.hpp"
#include "dose_calculator.hpp"
#include <vector>
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
     * @param buffer_start Pointer to start of PCM buffer (float samples)
     * @param buffer_end Pointer to end of PCM buffer
     * @param processing_duration_s Duration of audio segment in seconds (default 0.01f = 10ms)
     * @return SecondMetrics containing all 81 indicators
     *
     * @note buffer must contain exactly sample_rate_ * processing_duration_s samples
     *       If buffer length doesn't match, returns default/zero metrics
     */
    SecondMetrics process_one_second(const float* buffer_start,
                                     const float* buffer_end,
                                     float processing_duration_s = 0.01f) noexcept;

    //==========================================================================
    // Interface 2: Aggregation
    //==========================================================================

    /**
     * @brief Aggregate metrics into aggregated metrics
     *
     * @param second_metrics Pointer to array of SecondMetrics
     * @param count Number of metrics to aggregate
     * @param unit_duration_s Duration each SecondMetrics represents (default 1.0f)
     * @return MinuteMetrics containing aggregated indicators
     *
     * @note Total time covered = unit_duration_s * count
     *       Example: unit_duration_s=0.01f, count=6000 -> 60 seconds total
     */
    MinuteMetrics aggregate_minute_metrics(const SecondMetrics* second_metrics,
                                            int count,
                                            float unit_duration_s = 1.0f) noexcept;

    /**
     * @brief Aggregate from std::array of SecondMetrics
     */
    template<size_t N>
    MinuteMetrics aggregate_minute_metrics(
        const std::array<SecondMetrics, N>& metrics_array,
        float unit_duration_s = 1.0f) noexcept {
        return aggregate_minute_metrics(metrics_array.data(), static_cast<int>(N), unit_duration_s);
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
    DoseCalculator dose_calculator_;

    /** @brief Calculate 1/3 octave band moments for a signal */
    void calculate_band_moments(const float* data, size_t n,
                                FreqBandMoments* out_moments);
};

} // namespace noise_toolkit