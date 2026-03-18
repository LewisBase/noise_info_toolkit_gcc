/**
 * @file audio_processor_v2.cpp
 * @brief Complete audio processor implementation
 * 
 * Uses proper filter design for accurate noise metrics calculation
 */

#include "audio_processor.hpp"
#include "wav_reader.hpp"
#include "iir_filter.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace noise_toolkit {

// Standard octave band labels
const char* const OCTAVE_BAND_LABELS[] = {
    "63 Hz", "125 Hz", "250 Hz", "500 Hz", "1000 Hz", 
    "2000 Hz", "4000 Hz", "8000 Hz", "16000 Hz"
};

AudioProcessor::AudioProcessor(double reference_pressure)
    : reference_pressure_(reference_pressure) {}

NoiseMetrics AudioProcessor::process_signal(const Signal& signal) {
    NoiseMetrics metrics;
    metrics.signal = signal;
    metrics.sampling_rate = signal.sample_rate;
    metrics.duration = signal.duration();
    metrics.channels = signal.channels;
    
    // Calculate frequency metrics with proper 1/3 octave analysis
    metrics.frequency_metrics = calculate_frequency_metrics(signal);
    
    // Calculate overall metrics
    calculate_overall_metrics(signal, metrics);
    
    // Calculate dose metrics
    calculate_dose_metrics(signal, metrics.laeq, metrics);
    
    return metrics;
}

NoiseMetrics AudioProcessor::process_wav_file(const std::string& file_path) {
    // Load WAV file using WAVReader
    Signal signal = WAVReader::read(file_path);
    return process_signal(signal);
}

NoiseMetrics AudioProcessor::process_raw_data(const std::vector<double>& samples,
                                               double sample_rate,
                                               int channels) {
    Signal signal(samples, sample_rate, channels);
    return process_signal(signal);
}

FrequencyMetrics AudioProcessor::calculate_frequency_metrics(const Signal& signal) {
    FrequencyMetrics metrics;
    
    // Get proper 1/3 octave band analysis
    auto [center_freqs, band_levels] = third_octave_analysis(signal.data, signal.sample_rate);
    
    // Select the 9 standard octave bands used in noise analysis
    // These correspond to: 63, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
    std::vector<double> target_freqs = {63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    
    for (double target_freq : target_freqs) {
        // Find closest frequency in analysis results
        auto it = std::min_element(center_freqs.begin(), center_freqs.end(),
            [target_freq](double a, double b) {
                return std::abs(a - target_freq) < std::abs(b - target_freq);
            });
        
        if (it != center_freqs.end()) {
            size_t idx = std::distance(center_freqs.begin(), it);
            
            // Design octave band filter for this specific band
            auto coef = octave_filters::design_octave(target_freq, signal.sample_rate, 4);
            IIRFilter filter(coef.b, coef.a);
            auto band_signal = filter.process(signal.data);
            
            // Calculate band kurtosis
            double band_kurtosis = calculate_kurtosis(band_signal, false);
            
            metrics.center_frequencies.push_back(target_freq);
            metrics.spl_values.push_back(band_levels[idx]);
            metrics.kurtosis_values.push_back(band_kurtosis);
        }
    }
    
    return metrics;
}

void AudioProcessor::calculate_overall_metrics(const Signal& signal, NoiseMetrics& metrics) {
    // Apply weighting filters using proper IEC 61672-1 design
    std::vector<double> a_weighted = apply_a_weighting(signal.data, signal.sample_rate);
    std::vector<double> c_weighted = apply_c_weighting(signal.data, signal.sample_rate);
    
    // Calculate equivalent levels
    metrics.leq = calculate_leq(signal.data, reference_pressure_);
    metrics.laeq = calculate_leq(a_weighted, reference_pressure_);
    metrics.lceq = calculate_leq(c_weighted, reference_pressure_);
    
    // Calculate peak levels
    metrics.peak_spl = calculate_lpeak(signal.data, reference_pressure_);
    metrics.peak_aspl = calculate_lpeak(a_weighted, reference_pressure_);
    metrics.peak_cspl = calculate_lpeak(c_weighted, reference_pressure_);
    
    // Calculate kurtosis (Pearson kurtosis, fisher=false)
    metrics.total_kurtosis = calculate_kurtosis(signal.data, false);
    metrics.a_weighted_kurtosis = calculate_kurtosis(a_weighted, false);
    metrics.c_weighted_kurtosis = calculate_kurtosis(c_weighted, false);
    
    // Calculate LAFmax (fast time-weighted max of A-weighted signal)
    auto laf_fast = fast_time_weighting(a_weighted, signal.sample_rate);
    if (!laf_fast.empty()) {
        double lafmax = *std::max_element(laf_fast.begin(), laf_fast.end());
        metrics.lafmax = 20.0 * std::log10(lafmax / reference_pressure_);
    }
}

void AudioProcessor::calculate_dose_metrics(const Signal& signal, 
                                            double laeq, 
                                            NoiseMetrics& metrics) {
    double duration_s = signal.duration();
    
    // Calculate dose for all standards
    auto dose_results = dose_calculator_.calculate_multi_standard(laeq, duration_s);
    
    // Extract dose percentages
    metrics.dose_niosh = dose_results["NIOSH"].dose_pct;
    metrics.dose_osha_pel = dose_results["OSHA_PEL"].dose_pct;
    metrics.dose_osha_hca = dose_results["OSHA_HCA"].dose_pct;
    metrics.dose_eu_iso = dose_results["EU_ISO"].dose_pct;
    
    // Get profiles for TWA calculation
    auto profile_niosh = dose_calculator_.get_profile("NIOSH");
    auto profile_osha_pel = dose_calculator_.get_profile("OSHA_PEL");
    auto profile_osha_hca = dose_calculator_.get_profile("OSHA_HCA");
    auto profile_eu_iso = dose_calculator_.get_profile("EU_ISO");
    
    // Calculate TWA
    metrics.twa_niosh = dose_calculator_.calculate_twa(metrics.dose_niosh, profile_niosh);
    metrics.twa_osha_pel = dose_calculator_.calculate_twa(metrics.dose_osha_pel, profile_osha_pel);
    metrics.twa_osha_hca = dose_calculator_.calculate_twa(metrics.dose_osha_hca, profile_osha_hca);
    metrics.twa_eu_iso = dose_calculator_.calculate_twa(metrics.dose_eu_iso, profile_eu_iso);
    
    // Calculate LEX,8h
    metrics.lex_niosh = dose_calculator_.calculate_lex(metrics.dose_niosh, profile_niosh);
    metrics.lex_osha_pel = dose_calculator_.calculate_lex(metrics.dose_osha_pel, profile_osha_pel);
    metrics.lex_osha_hca = dose_calculator_.calculate_lex(metrics.dose_osha_hca, profile_osha_hca);
    metrics.lex_eu_iso = dose_calculator_.calculate_lex(metrics.dose_eu_iso, profile_eu_iso);
}

// Convenience function
NoiseMetrics process_wav_file(const std::string& file_path, double reference_pressure) {
    AudioProcessor processor(reference_pressure);
    return processor.process_wav_file(file_path);
}

} // namespace noise_toolkit
