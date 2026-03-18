/**
 * @file audio_processor.hpp
 * @brief Audio processing core module
 * 
 * Supports noise metrics calculation, dose calculation, spectral analysis, etc.
 */

#pragma once

#include "noise_toolkit.hpp"
#include "signal_utils.hpp"
#include "dose_calculator.hpp"
#include <vector>
#include <map>
#include <string>
#include <optional>

namespace noise_toolkit {

/**
 * @brief Frequency metrics structure
 */
struct FrequencyMetrics {
    std::vector<double> center_frequencies;  // Hz
    std::vector<double> spl_values;          // dB
    std::vector<double> kurtosis_values;     // unitless
};

/**
 * @brief Complete noise metrics result
 */
struct NoiseMetrics {
    // Original signal
    Signal signal;
    
    // Frequency analysis
    FrequencyMetrics frequency_metrics;
    
    // Overall metrics
    double total_kurtosis;       // Z-weighted kurtosis
    double a_weighted_kurtosis;  // A-weighted kurtosis
    double c_weighted_kurtosis;  // C-weighted kurtosis
    
    // Sound levels
    double leq;      // Linear equivalent level
    double laeq;     // A-weighted equivalent level
    double lceq;     // C-weighted equivalent level
    
    // Peak levels
    double peak_spl;   // Z-weighted peak
    double peak_aspl;  // A-weighted peak
    double peak_cspl;  // C-weighted peak
    
    // Time-weighted max
    std::optional<double> lafmax;  // LAFmax - fast time weighted A-level max
    
    // Sampling info
    double sampling_rate;
    double duration;
    int channels;
    
    // Dose metrics (%)
    double dose_niosh;
    double dose_osha_pel;
    double dose_osha_hca;
    double dose_eu_iso;
    
    // TWA (dBA)
    double twa_niosh;
    double twa_osha_pel;
    double twa_osha_hca;
    double twa_eu_iso;
    
    // LEX,8h (dBA)
    double lex_niosh;
    double lex_osha_pel;
    double lex_osha_hca;
    double lex_eu_iso;
};

/**
 * @brief Audio processor class
 * 
 * Processes audio files and calculates noise metrics including:
 * - 1/3 octave band analysis
 * - Overall sound levels (Leq, LAeq, LCeq)
 * - Peak levels
 * - Kurtosis analysis
 * - Dose calculation (NIOSH, OSHA, EU_ISO standards)
 * - TWA and LEX,8h
 */
class AudioProcessor {
public:
    /**
     * @brief Constructor
     * @param reference_pressure Reference pressure in Pa (default: 20 μPa)
     */
    explicit AudioProcessor(double reference_pressure = REFERENCE_PRESSURE);
    
    /**
     * @brief Process audio data and calculate all metrics
     * @param signal Input signal
     * @return NoiseMetrics Complete metrics structure
     */
    NoiseMetrics process_signal(const Signal& signal);
    
    /**
     * @brief Process WAV file
     * @param file_path Path to WAV file
     * @return NoiseMetrics Complete metrics structure
     */
    NoiseMetrics process_wav_file(const std::string& file_path);
    
    /**
     * @brief Process raw audio data
     * @param samples Audio samples
     * @param sample_rate Sample rate in Hz
     * @param channels Number of channels
     * @return NoiseMetrics Complete metrics structure
     */
    NoiseMetrics process_raw_data(const std::vector<double>& samples,
                                   double sample_rate,
                                   int channels = 1);
    
    /**
     * @brief Get reference pressure
     */
    double get_reference_pressure() const { return reference_pressure_; }
    
    /**
     * @brief Set reference pressure
     */
    void set_reference_pressure(double p0) { reference_pressure_ = p0; }

private:
    double reference_pressure_;
    DoseCalculator dose_calculator_;
    
    // Helper methods
    FrequencyMetrics calculate_frequency_metrics(const Signal& signal);
    void calculate_overall_metrics(const Signal& signal, NoiseMetrics& metrics);
    void calculate_dose_metrics(const Signal& signal, double laeq, NoiseMetrics& metrics);
};

/**
 * @brief Convenience function to process a WAV file
 */
NoiseMetrics process_wav_file(const std::string& file_path, 
                              double reference_pressure = REFERENCE_PRESSURE);

} // namespace noise_toolkit
