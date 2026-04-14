/**
 * @file main.cpp
 * @brief Example usage of noise_info_toolkit C++ library (v2.0)
 * 
 * This example demonstrates the simplified two-interface design:
 * - Interface 1: process_one_second() - per-second audio processing
 * - Interface 2: aggregate_minute_metrics() - per-minute aggregation
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <cmath>
#include <random>
#include <chrono>

#include "noise_processor.hpp"

using namespace noise_toolkit;

// Generate test signal: white noise at specified dB SPL
std::vector<double> generate_noise(double spl_db, int sample_rate, double duration_s) {
    size_t n = static_cast<size_t>(sample_rate * duration_s);
    std::vector<double> data(n);
    
    // Convert SPL to RMS pressure
    double rms = REFERENCE_PRESSURE * std::pow(10.0, spl_db / 20.0);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> dis(0.0, rms);
    
    for (size_t i = 0; i < n; ++i) {
        data[i] = dis(gen);
    }
    return data;
}

// Generate sine wave at specified frequency and dB SPL
std::vector<double> generate_sine(double freq_hz, double spl_db, 
                                   int sample_rate, double duration_s) {
    size_t n = static_cast<size_t>(sample_rate * duration_s);
    std::vector<double> data(n);
    
    double rms = REFERENCE_PRESSURE * std::pow(10.0, spl_db / 20.0);
    double amplitude = rms * std::sqrt(2.0);
    
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        data[i] = amplitude * std::sin(2.0 * M_PI * freq_hz * t);
    }
    return data;
}

void print_second_metrics(const SecondMetrics& m) {
    std::cout << "  LAeq=" << std::fixed << std::setprecision(1) << m.LAeq << " dB  "
              << "LCeq=" << m.LCeq << " dB  "
              << "LZeq=" << m.LZeq << " dB\n";
    std::cout << "  LAFmax=" << m.LAFmax << " dB  "
              << "LZpeak=" << m.LZpeak << " dB  "
              << "LCpeak=" << m.LCpeak << " dB\n";
    std::cout << "  kurtosis=" << std::setprecision(2) << m.kurtosis_total 
              << "  beta=" << m.beta_kurtosis << "\n";
    std::cout << "  dose: niosh=" << std::setprecision(4) << m.dose_frac_niosh
              << "  osha_pel=" << m.dose_frac_osha_pel
              << "  osha_hca=" << m.dose_frac_osha_hca
              << "  eu_iso=" << m.dose_frac_eu_iso << "\n";
    std::cout << "  QC: overload=" << (m.overload_flag ? "Y" : "N")
              << "  underrange=" << (m.underrange_flag ? "Y" : "N")
              << "  wearing=" << (m.wearing_state ? "Y" : "N") << "\n";
    std::cout << "  1/3 octave SPLs (dB): "
              << "63Hz=" << m.freq_63hz_spl << " "
              << "500Hz=" << m.freq_500hz_spl << " "
              << "1kHz=" << m.freq_1khz_spl << " "
              << "4kHz=" << m.freq_4khz_spl << " "
              << "8kHz=" << m.freq_8khz_spl << "\n";
    std::cout << "  raw_moments: n=" << m.n_samples
              << "  S1=" << std::setprecision(6) << m.sum_x
              << "  S2=" << m.sum_x2 << "\n";
}

void print_minute_metrics(const MinuteMetrics& m) {
    std::cout << "\n=== Minute Aggregated Metrics ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  LAeq=" << m.LAeq << " dB  "
              << "LCeq=" << m.LCeq << " dB  "
              << "LZeq=" << m.LZeq << " dB\n";
    std::cout << "  LAFmax=" << m.LAFmax << " dB  "
              << "LZpeak=" << m.LZpeak << " dB\n";
    std::cout << "  dose: niosh=" << std::setprecision(4) << m.dose_frac_niosh
              << "  osha_pel=" << m.dose_frac_osha_pel << "\n";
    std::cout << "  QC: overload_count=" << m.overload_count
              << "  underrange_count=" << m.underrange_count
              << "  valid_seconds=" << m.valid_seconds << "\n";
    std::cout << "  kurtosis=" << std::setprecision(2) << m.kurtosis_total
              << "  n_samples=" << m.n_samples << "\n";
    std::cout << "  1/3 octave SPLs: "
              << "63Hz=" << m.freq_63hz_spl << " "
              << "1kHz=" << m.freq_1khz_spl << " "
              << "4kHz=" << m.freq_4khz_spl << "\n";
}

// Test Interface 1: Per-second processing
void test_interface1() {
    std::cout << "\n========== Test Interface 1: Per-Second Processing ==========\n";
    
    NoiseProcessor processor(48000);
    
    // Generate 1 second of 1kHz sine at 85 dB SPL
    auto data = generate_sine(1000.0, 85.0, 48000, 1.0);
    
    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
    
    std::cout << "Processed 1 second of 1kHz tone at 85 dB SPL:\n";
    print_second_metrics(m);
    
    // Generate 1 second of white noise at 90 dB
    std::cout << "\nProcessed 1 second of white noise at 90 dB SPL:\n";
    auto noise_data = generate_noise(90.0, 48000, 1.0);
    SecondMetrics m2 = processor.process_one_second(noise_data.data(), 
                                                     noise_data.data() + noise_data.size());
    print_second_metrics(m2);
}

// Test Interface 2: Per-minute aggregation
void test_interface2() {
    std::cout << "\n========== Test Interface 2: Per-Minute Aggregation ==========\n";
    
    NoiseProcessor processor(48000);
    
    // Simulate 60 seconds with varying levels
    std::array<SecondMetrics, 60> seconds;
    
    for (int i = 0; i < 60; ++i) {
        // Vary level from 70 to 95 dB over the minute
        double spl = 70.0 + 25.0 * (i / 59.0);
        auto data = generate_noise(spl, 48000, 1.0);
        seconds[i] = processor.process_one_second(data.data(), data.data() + data.size());
        seconds[i].timestamp = static_cast<double>(i);
    }
    
    MinuteMetrics minute = processor.aggregate_minute_metrics(seconds);
    print_minute_metrics(minute);
    
    // Test with std::array template version
    MinuteMetrics minute2 = processor.aggregate_minute_metrics(seconds);
    (void)minute2; // suppress unused warning
}

// Test frequency band analysis
void test_frequency_bands() {
    std::cout << "\n========== Test Frequency Band Analysis ==========\n";
    
    NoiseProcessor processor(48000);
    
    // Test with tones at different frequencies
    std::vector<std::pair<double, double>> tones = {
        {63.0, 85.0},    // 63 Hz tone
        {1000.0, 85.0},  // 1 kHz tone
        {8000.0, 85.0},  // 8 kHz tone
    };
    
    for (const auto& [freq, spl] : tones) {
        auto data = generate_sine(freq, spl, 48000, 1.0);
        SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
        
        std::cout << std::fixed << std::setprecision(1);
        std::cout << freq << " Hz tone: ";
        std::cout << "band_63Hz=" << m.freq_63hz_spl << " "
                  << "band_1kHz=" << m.freq_1khz_spl << " "
                  << "band_8kHz=" << m.freq_8khz_spl << "\n";
    }
}

// Test QC flags
void test_qc_flags() {
    std::cout << "\n========== Test QC Flags ==========\n";
    
    NoiseProcessor processor(48000);
    
    // Very quiet signal (should trigger underrange)
    auto quiet = generate_noise(20.0, 48000, 1.0);
    SecondMetrics m_quiet = processor.process_one_second(quiet.data(), quiet.data() + quiet.size());
    std::cout << "Quiet signal (20 dB): underrange=" << m_quiet.underrange_flag << "\n";
    
    // Normal signal
    auto normal = generate_noise(85.0, 48000, 1.0);
    SecondMetrics m_normal = processor.process_one_second(normal.data(), normal.data() + normal.size());
    std::cout << "Normal signal (85 dB): underrange=" << m_normal.underrange_flag 
              << " wearing=" << m_normal.wearing_state << "\n";
}

// Test sample rate variations
void test_sample_rates() {
    std::cout << "\n========== Test Sample Rate Variations ==========\n";
    
    std::vector<int> rates = {8000, 16000, 44100, 48000};
    
    for (int sr : rates) {
        NoiseProcessor processor(sr);
        auto data = generate_sine(1000.0, 85.0, sr, 1.0);
        SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
        
        std::cout << std::fixed << std::setprecision(1);
        std::cout << sr << " Hz: n_samples=" << m.n_samples 
                  << " LAeq=" << m.LAeq << " dB\n";
    }
}

int main() {
    std::cout << "================================================\n";
    std::cout << "  Noise Info Toolkit C++ v2.0\n";
    std::cout << "  Two-Interface Design Demo\n";
    std::cout << "================================================\n";
    
    test_interface1();
    test_interface2();
    test_frequency_bands();
    test_qc_flags();
    test_sample_rates();
    
    std::cout << "\n================================================\n";
    std::cout << "  All tests completed\n";
    std::cout << "================================================\n";
    
    return 0;
}
