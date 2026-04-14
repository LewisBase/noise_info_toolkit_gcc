/**
 * @file noise_processor.hpp
 * @brief Lightweight noise metrics processor with two simple interfaces
 * 
 * Interface 1 (per-second): process_one_second(buffer_start, buffer_end)
 *   - Call once per second with raw PCM buffer pointers
 *   - Returns SecondMetrics (81 indicators)
 * 
 * Interface 2 (per-minute): aggregate_minute_metrics(second_metrics_array, count)
 *   - Call once per minute with 60 SecondMetrics
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
                            double reference_pressure = REFERENCE_PRESSURE);
    
    ~NoiseProcessor() = default;
    
    //==========================================================================
    // Interface 1: Per-Second Processing
    //==========================================================================
    
    /**
     * @brief Process one second of audio from raw PCM buffer
     * 
     * @param buffer_start Pointer to start of PCM buffer (float samples)
     * @param buffer_end Pointer to end of PCM buffer
     * @return SecondMetrics containing all 81 indicators
     * 
     * @note buffer should contain exactly sample_rate_ samples (1 second at given SR)
     *       If buffer is shorter/longer, only complete samples within buffer are processed
     */
    SecondMetrics process_one_second(const float* buffer_start, 
                                     const float* buffer_end) noexcept;
    
    /**
     * @brief Process one second from double buffer
     */
    SecondMetrics process_one_second(const double* buffer_start,
                                     const double* buffer_end) noexcept;
    
    //==========================================================================
    // Interface 2: Per-Minute Aggregation
    //==========================================================================
    
    /**
     * @brief Aggregate per-second metrics into per-minute metrics
     * 
     * @param second_metrics Pointer to array of SecondMetrics (should be 60 items)
     * @param count Actual number of valid SecondMetrics (typically 60)
     * @return MinuteMetrics containing aggregated indicators
     */
    MinuteMetrics aggregate_minute_metrics(const SecondMetrics* second_metrics,
                                            int count) noexcept;
    
    /**
     * @brief Aggregate from std::array of SecondMetrics
     */
    template<size_t N>
    MinuteMetrics aggregate_minute_metrics(
        const std::array<SecondMetrics, N>& metrics_array) noexcept {
        return aggregate_minute_metrics(metrics_array.data(), static_cast<int>(N));
    }
    
    //==========================================================================
    // Utility
    //==========================================================================
    
    /** @brief Get configured sample rate */
    int sample_rate() const { return sample_rate_; }
    
    /** @brief Get configured reference pressure */
    double reference_pressure() const { return reference_pressure_; }

private:
    int sample_rate_;
    double reference_pressure_;
    DoseCalculator dose_calculator_;
    
    /** @brief Calculate 1/3 octave band moments for a signal */
    void calculate_band_moments(const double* data, size_t n,
                                FreqBandMoments* out_moments);
};

} // namespace noise_toolkit
