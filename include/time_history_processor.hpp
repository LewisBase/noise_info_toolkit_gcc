/**
 * @file time_history_processor.hpp
 * @brief Time history data processor
 * 
 * Processes audio data per second and stores to TimeHistory table
 */

#pragma once

#include "noise_toolkit.hpp"
#include "signal_utils.hpp"
#include "dose_calculator.hpp"
#include <vector>
#include <functional>
#include <chrono>
#include <optional>

namespace noise_toolkit {

/**
 * @brief Single second metrics data
 */
struct SecondMetrics {
    std::chrono::system_clock::time_point timestamp;
    double duration_s;
    
    // Sound levels (dB)
    double LAeq;
    double LCeq;
    double LZeq;
    std::optional<double> LAFmax;
    double LZpeak;
    double LCpeak;
    
    // Dose increments
    double dose_frac_niosh;
    double dose_frac_osha_pel;
    double dose_frac_osha_hca;
    double dose_frac_eu_iso;
    
    // Quality control
    bool overload_flag;
    bool underrange_flag;
    bool wearing_state;
    
    // Kurtosis
    std::optional<double> kurtosis_total;
    std::optional<double> kurtosis_a_weighted;
    
    // Environmental (optional)
    std::optional<double> temp_C;
    std::optional<double> humidity_pct;
    std::optional<double> pressure_hPa;
    
    SecondMetrics()
        : duration_s(1.0),
          LAeq(0.0), LCeq(0.0), LZeq(0.0),
          LZpeak(0.0), LCpeak(0.0),
          dose_frac_niosh(0.0), dose_frac_osha_pel(0.0),
          dose_frac_osha_hca(0.0), dose_frac_eu_iso(0.0),
          overload_flag(false), underrange_flag(false),
          wearing_state(true) {}
};

/**
 * @brief Session aggregated metrics
 */
struct SessionMetrics {
    double total_duration_h;
    double total_dose_pct;
    double LAeq_T;           // Overall LAeq for entire session
    double TWA;              // Time Weighted Average
    double LEX_8h;           // Daily noise exposure level
    double peak_max_dB;
    int overload_count;
    int underrange_count;
    int total_seconds;
};

/**
 * @brief Time history processor class
 */
class TimeHistoryProcessor {
public:
    // Overload threshold (dB)
    static constexpr double OVERLOAD_THRESHOLD = 140.0;
    // Underrange threshold (dB)
    static constexpr double UNDERRANGE_THRESHOLD = 30.0;
    
    /**
     * @brief Constructor
     */
    explicit TimeHistoryProcessor(double reference_pressure = REFERENCE_PRESSURE,
                                   std::function<void(const SecondMetrics&)> callback = nullptr);
    
    /**
     * @brief Process signal per second
     */
    std::vector<SecondMetrics> process_signal_per_second(
        const Signal& signal,
        std::optional<std::chrono::system_clock::time_point> start_time = std::nullopt);
    
    /**
     * @brief Process raw audio data per second
     */
    std::vector<SecondMetrics> process_raw_data_per_second(
        const std::vector<double>& samples,
        double sample_rate,
        std::optional<std::chrono::system_clock::time_point> start_time = std::nullopt);
    
    /**
     * @brief Aggregate session metrics
     */
    static SessionMetrics aggregate_session_metrics(
        const std::vector<SecondMetrics>& time_history,
        DoseStandard profile = DoseStandard::NIOSH);

private:
    double reference_pressure_;
    std::function<void(const SecondMetrics&)> callback_;
    DoseCalculator dose_calculator_;
    
    SecondMetrics calculate_second_metrics(const std::vector<double>& data,
                                           double sr,
                                           std::chrono::system_clock::time_point timestamp,
                                           double duration);
};

} // namespace noise_toolkit
