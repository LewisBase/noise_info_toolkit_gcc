/**
 * @file time_history_processor.cpp
 * @brief Time history processor implementation
 */

#include "time_history_processor.hpp"
#include <algorithm>
#include <cmath>

namespace noise_toolkit {

TimeHistoryProcessor::TimeHistoryProcessor(double reference_pressure,
                                            std::function<void(const SecondMetrics&)> callback)
    : reference_pressure_(reference_pressure),
      callback_(callback) {}

std::vector<SecondMetrics> TimeHistoryProcessor::process_signal_per_second(
    const Signal& signal,
    std::optional<std::chrono::system_clock::time_point> start_time) {
    
    if (!start_time.has_value()) {
        start_time = std::chrono::system_clock::now();
    }
    
    std::vector<SecondMetrics> results;
    double sr = signal.sample_rate;
    size_t total_samples = signal.data.size();
    size_t samples_per_second = static_cast<size_t>(sr);
    
    size_t total_seconds = (total_samples + samples_per_second - 1) / samples_per_second;
    
    for (size_t second_idx = 0; second_idx < total_seconds; ++second_idx) {
        size_t start_sample = second_idx * samples_per_second;
        size_t end_sample = std::min((second_idx + 1) * samples_per_second, total_samples);
        
        if (start_sample >= total_samples) break;
        
        std::vector<double> second_data(
            signal.data.begin() + start_sample,
            signal.data.begin() + end_sample
        );
        
        auto timestamp = start_time.value() + std::chrono::seconds(second_idx);
        double duration = static_cast<double>(end_sample - start_sample) / sr;
        
        SecondMetrics metrics = calculate_second_metrics(second_data, sr, timestamp, duration);
        results.push_back(metrics);
        
        if (callback_) {
            callback_(metrics);
        }
    }
    
    return results;
}

std::vector<SecondMetrics> TimeHistoryProcessor::process_raw_data_per_second(
    const std::vector<double>& samples,
    double sample_rate,
    std::optional<std::chrono::system_clock::time_point> start_time) {
    
    Signal signal(samples, sample_rate, 1);
    return process_signal_per_second(signal, start_time);
}

SecondMetrics TimeHistoryProcessor::calculate_second_metrics(
    const std::vector<double>& data,
    double sr,
    std::chrono::system_clock::time_point timestamp,
    double duration) {
    
    SecondMetrics metrics;
    metrics.timestamp = timestamp;
    metrics.duration_s = duration;
    
    Signal s(data, sr, 1);
    
    // Apply weighting
    std::vector<double> a_weighted = apply_a_weighting(data, sr);
    std::vector<double> c_weighted = apply_c_weighting(data, sr);
    
    // Calculate equivalent sound levels
    metrics.LAeq = calculate_leq(a_weighted, reference_pressure_);
    metrics.LCeq = calculate_leq(c_weighted, reference_pressure_);
    metrics.LZeq = calculate_leq(data, reference_pressure_);
    
    // Calculate peak levels
    metrics.LZpeak = calculate_lpeak(data, reference_pressure_);
    metrics.LCpeak = calculate_lpeak(c_weighted, reference_pressure_);
    
    // Calculate LAFmax (fast time-weighted max)
    std::vector<double> fast_avg = fast_time_weighting(a_weighted, sr);
    if (!fast_avg.empty()) {
        metrics.LAFmax = *std::max_element(fast_avg.begin(), fast_avg.end());
    }
    
    // Calculate kurtosis
    metrics.kurtosis_total = calculate_kurtosis(data, false);
    metrics.kurtosis_a_weighted = calculate_kurtosis(a_weighted, false);
    
    // Calculate dose increments for each second
    DoseProfile profile_niosh = dose_calculator_.get_profile(DoseStandard::NIOSH);
    DoseProfile profile_osha_pel = dose_calculator_.get_profile(DoseStandard::OSHA_PEL);
    DoseProfile profile_osha_hca = dose_calculator_.get_profile(DoseStandard::OSHA_HCA);
    DoseProfile profile_eu_iso = dose_calculator_.get_profile(DoseStandard::EU_ISO);
    
    metrics.dose_frac_niosh = dose_calculator_.calculate_dose_increment(
        metrics.LAeq, 1.0, profile_niosh) / 100.0;  // Convert to fraction
    metrics.dose_frac_osha_pel = dose_calculator_.calculate_dose_increment(
        metrics.LAeq, 1.0, profile_osha_pel) / 100.0;
    metrics.dose_frac_osha_hca = dose_calculator_.calculate_dose_increment(
        metrics.LAeq, 1.0, profile_osha_hca) / 100.0;
    metrics.dose_frac_eu_iso = dose_calculator_.calculate_dose_increment(
        metrics.LAeq, 1.0, profile_eu_iso) / 100.0;
    
    // Quality control checks
    metrics.overload_flag = metrics.LZpeak > OVERLOAD_THRESHOLD;
    metrics.underrange_flag = metrics.LAeq < UNDERRANGE_THRESHOLD;
    
    // Wearing state detection (simplified threshold-based)
    metrics.wearing_state = metrics.LAeq > 40.0;
    
    return metrics;
}

SessionMetrics TimeHistoryProcessor::aggregate_session_metrics(
    const std::vector<SecondMetrics>& time_history,
    DoseStandard profile) {
    
    SessionMetrics result{};
    
    if (time_history.empty()) {
        return result;
    }
    
    // Get dose attribute based on profile
    auto get_dose = [profile](const SecondMetrics& m) -> double {
        switch (profile) {
            case DoseStandard::NIOSH: return m.dose_frac_niosh;
            case DoseStandard::OSHA_PEL: return m.dose_frac_osha_pel;
            case DoseStandard::OSHA_HCA: return m.dose_frac_osha_hca;
            case DoseStandard::EU_ISO: return m.dose_frac_eu_iso;
        }
        return m.dose_frac_niosh;
    };
    
    // Calculate total duration
    double total_duration_s = 0.0;
    for (const auto& m : time_history) {
        total_duration_s += m.duration_s;
    }
    result.total_duration_h = total_duration_s / 3600.0;
    
    // Calculate total dose
    double total_dose = 0.0;
    for (const auto& m : time_history) {
        total_dose += get_dose(m);
    }
    result.total_dose_pct = total_dose * 100.0;
    
    // Calculate overall LAeq
    // LAeq_total = 10 * log10( (1/n) * sum(10^(LAeq_i/10)) )
    double sum_power = 0.0;
    for (const auto& m : time_history) {
        sum_power += std::pow(10.0, m.LAeq / 10.0);
    }
    result.LAeq_T = 10.0 * std::log10(sum_power / time_history.size());
    
    // Calculate TWA and LEX,8h
    DoseCalculator calculator;
    DoseProfile dose_profile = calculator.get_profile(profile);
    result.TWA = calculator.calculate_twa(result.total_dose_pct, dose_profile);
    result.LEX_8h = calculator.calculate_lex(result.total_dose_pct, dose_profile);
    
    // Find peak max
    double peak_max = 0.0;
    for (const auto& m : time_history) {
        peak_max = std::max(peak_max, m.LZpeak);
    }
    result.peak_max_dB = peak_max;
    
    // Count overloads and underranges
    result.overload_count = 0;
    result.underrange_count = 0;
    for (const auto& m : time_history) {
        if (m.overload_flag) result.overload_count++;
        if (m.underrange_flag) result.underrange_count++;
    }
    
    result.total_seconds = static_cast<int>(time_history.size());
    
    return result;
}

} // namespace noise_toolkit
