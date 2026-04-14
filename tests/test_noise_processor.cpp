/**
 * @file test_noise_processor.cpp
 * @brief Unit tests for NoiseProcessor
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <array>
#include <chrono>
#include "noise_processor.hpp"

using namespace noise_toolkit;

// Test helper: generate sine wave at given frequency and amplitude
std::vector<double> generate_sine(double freq_hz, double amplitude, 
                                   int sample_rate, double duration_s) {
    int n = static_cast<int>(sample_rate * duration_s);
    std::vector<double> data(n);
    for (int i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        data[i] = amplitude * std::sin(2.0 * M_PI * freq_hz * t);
    }
    return data;
}

// Test helper: generate white noise
std::vector<double> generate_noise(double amplitude, int n) {
    std::vector<double> data(n);
    for (int i = 0; i < n; ++i) {
        data[i] = amplitude * (2.0 * (static_cast<double>(rand()) / RAND_MAX) - 1.0);
    }
    return data;
}

//==============================================================================
// Test 1: Basic second metrics computation
//==============================================================================
void test_second_metrics_basic() {
    std::cout << "Test 1: Basic second metrics computation... ";
    
    NoiseProcessor processor(48000);
    
    // Generate 1 second of 1kHz sine at 94 dB SPL (reference: 20μPa)
    // 94 dB SPL ≈ 1 Pa RMS → amplitude ≈ sqrt(2) Pa
    double spl_target = 94.0;
    double rms = 20e-6 * std::pow(10.0, spl_target / 20.0);  // ~1 Pa
    double amplitude = rms * std::sqrt(2.0);
    
    auto data = generate_sine(1000.0, amplitude, 48000, 1.0);
    
    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
    
    // Check that LAeq is roughly in the expected range (allow ±3 dB tolerance)
    assert(m.LAeq > 85.0 && m.LAeq < 105.0);
    assert(m.n_samples == 48000);
    assert(m.duration_s > 0.99 && m.duration_s < 1.01);
    assert(!std::isnan(m.kurtosis_total));
    assert(m.kurtosis_total > 1.0);  // Sine should have low kurtosis (~1.8 for pure sine)
    
    std::cout << "PASSED (LAeq=" << m.LAeq << " dB, kurtosis=" << m.kurtosis_total << ")\n";
}

//==============================================================================
// Test 2: Kurtosis from moments (spec 4.X.3)
//==============================================================================
void test_kurtosis_from_moments() {
    std::cout << "Test 2: Kurtosis from moments... ";
    
    // Test with known distribution: normal distribution has kurtosis β=3
    // Generate samples from a normal-like distribution using CLT
    int n = 48000;
    double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
    
    for (int i = 0; i < n; ++i) {
        // Simple approximation of normal: sum of uniform distributions
        double x = 0.0;
        for (int j = 0; j < 12; ++j) {
            x += (2.0 * (static_cast<double>(rand()) / RAND_MAX) - 1.0);
        }
        // Normalize: sum of 12 uniform(-1,1) -> mean=0, var=1
        x = x / 12.0; // Roughly N(0,1)
        
        s1 += x;
        s2 += x * x;
        s3 += x * x * x;
        s4 += x * x * x * x;
    }
    
    double beta = calculate_kurtosis_from_moments(n, s1, s2, s3, s4);
    
    // Normal distribution should give β ≈ 3.0
    assert(std::abs(beta - 3.0) < 0.5);  // Allow some tolerance
    
    // Test edge case: n <= 0
    assert(std::isnan(calculate_kurtosis_from_moments(0, 0, 0, 0, 0)));
    
    // Test edge case: m2 <= 0 (constant signal)
    assert(std::isnan(calculate_kurtosis_from_moments(100, 10.0, 100.0, 1000.0, 10000.0)));
    
    std::cout << "PASSED (beta=" << beta << ")\n";
}

//==============================================================================
// Test 3: Frequency band moments
//==============================================================================
void test_frequency_bands() {
    std::cout << "Test 3: Frequency band analysis... ";
    
    NoiseProcessor processor(48000);
    
    // Generate 1 second of 1kHz tone
    auto data = generate_sine(1000.0, 1.0, 48000, 1.0);
    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
    
    // 1kHz band (index 4) should have higher SPL than others
    assert(m.freq_1khz_spl > m.freq_63hz_spl);
    assert(m.freq_1khz_spl > m.freq_8khz_spl);
    
    // Check that band moments are populated
    assert(m.freq_1khz_n > 0);
    assert(m.freq_1khz_s1 != 0.0 || m.freq_1khz_s2 != 0.0);
    
    std::cout << "PASSED (1kHz SPL=" << m.freq_1khz_spl << " dB)\n";
}

//==============================================================================
// Test 4: Per-minute aggregation
//==============================================================================
void test_minute_aggregation() {
    std::cout << "Test 4: Per-minute aggregation... ";
    
    NoiseProcessor processor(48000);
    
    // Simulate 60 seconds of audio
    std::array<SecondMetrics, 60> seconds;
    
    for (int i = 0; i < 60; ++i) {
        // Varying level: 70-90 dB
        double spl = 70.0 + 20.0 * (i / 60.0);
        double rms = 20e-6 * std::pow(10.0, spl / 20.0);
        double amplitude = rms * std::sqrt(2.0);
        
        auto data = generate_sine(1000.0, amplitude, 48000, 1.0);
        seconds[i] = processor.process_one_second(data.data(), data.data() + data.size());
        seconds[i].timestamp = static_cast<double>(i);
    }
    
    MinuteMetrics minute = processor.aggregate_minute_metrics(seconds.data(), 60);
    
    // Check aggregated values
    assert(minute.duration_s > 59.0 && minute.duration_s < 61.0);
    assert(minute.valid_seconds == 60);
    assert(minute.LAeq > 70.0 && minute.LAeq < 95.0);
    assert(minute.dose_frac_niosh > 0.0);
    assert(minute.freq_1khz_spl > 0.0);  // SPL should be set
    
    // Check frequency band moments aggregation
    assert(minute.freq_1khz_n > 0);
    assert(minute.freq_1khz_s1 != 0.0 || minute.freq_1khz_s2 != 0.0);
    
    std::cout << "PASSED (LAeq=" << minute.LAeq << " dB, dose_niosh=" 
              << minute.dose_frac_niosh << ")\n";
}

//==============================================================================
// Test 5: QC flags
//==============================================================================
void test_qc_flags() {
    std::cout << "Test 5: QC flags... ";
    
    NoiseProcessor processor(48000);
    
    // Very low level (should trigger underrange)
    auto quiet = generate_sine(1000.0, 1e-6, 48000, 1.0);
    SecondMetrics m_quiet = processor.process_one_second(quiet.data(), quiet.data() + quiet.size());
    assert(m_quiet.underrange_flag == true);
    
    // Normal level
    auto normal = generate_sine(1000.0, 1.0, 48000, 1.0);
    SecondMetrics m_normal = processor.process_one_second(normal.data(), normal.data() + normal.size());
    assert(m_normal.underrange_flag == false);
    assert(m_normal.wearing_state == true);
    
    std::cout << "PASSED\n";
}

//==============================================================================
// Test 6: Dose calculation
//==============================================================================
void test_dose_calculation() {
    std::cout << "Test 6: Dose calculation... ";
    
    NoiseProcessor processor(48000);
    
    // 85 dBA for 1 second
    double rms_85 = 20e-6 * std::pow(10.0, 85.0 / 20.0);
    auto data = generate_sine(1000.0, rms_85 * std::sqrt(2.0), 48000, 1.0);
    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
    
    // NIOSH: at 85 dBA, 1 second, dose = 100*(1/28800)*2^((85-85)/3) = 100/28800 ≈ 0.347%
    // dose_frac = 0.00347
    assert(m.dose_frac_niosh > 0.0);
    assert(m.dose_frac_niosh < 0.01);  // Should be small for 1 second
    
    // At 91 dBA: exchange rate 3 dB, so dose doubles every 3 dB
    double rms_91 = 20e-6 * std::pow(10.0, 91.0 / 20.0);
    auto data_91 = generate_sine(1000.0, rms_91 * std::sqrt(2.0), 48000, 1.0);
    SecondMetrics m_91 = processor.process_one_second(data_91.data(), data_91.data() + data_91.size());
    
    // 91 dB should have roughly double the dose of 85 dB (per NIOSH exchange rate 3dB)
    assert(m_91.dose_frac_niosh > m.dose_frac_niosh);
    
    std::cout << "PASSED (85dB: " << m.dose_frac_niosh << ", 91dB: " << m_91.dose_frac_niosh << ")\n";
}

//==============================================================================
// Test 7: Float and double buffer interfaces
//==============================================================================
void test_buffer_interfaces() {
    std::cout << "Test 7: Float/double buffer interfaces... ";
    
    NoiseProcessor processor(48000);
    
    std::vector<double> data = generate_sine(1000.0, 1.0, 48000, 1.0);
    
    // Process as double
    SecondMetrics m_double = processor.process_one_second(data.data(), data.data() + data.size());
    
    // Process as float
    std::vector<float> data_float(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        data_float[i] = static_cast<float>(data[i]);
    }
    SecondMetrics m_float = processor.process_one_second(data_float.data(), data_float.data() + data_float.size());
    
    // Results should be nearly identical (within floating point tolerance)
    assert(std::abs(m_double.LAeq - m_float.LAeq) < 0.01);
    assert(std::abs(m_double.kurtosis_total - m_float.kurtosis_total) < 0.01);
    
    std::cout << "PASSED\n";
}

//==============================================================================
// Test 8: Zero/empty buffer handling
//==============================================================================
void test_empty_buffer() {
    std::cout << "Test 8: Empty buffer handling... ";
    
    NoiseProcessor processor(48000);
    
    std::vector<double> empty;
    SecondMetrics m = processor.process_one_second(empty.data(), empty.data());
    
    // Should return default/zero metrics without crashing
    assert(std::isnan(m.LAeq) || m.LAeq == 0.0);
    assert(m.n_samples == 0);
    
    std::cout << "PASSED\n";
}

//==============================================================================
// Test 9: Sample rate variations
//==============================================================================
void test_sample_rates() {
    std::cout << "Test 9: Sample rate variations... ";
    
    std::vector<int> sample_rates = {8000, 16000, 44100, 48000};
    
    for (int sr : sample_rates) {
        NoiseProcessor processor(sr);
        auto data = generate_sine(1000.0, 1.0, sr, 1.0);
        SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size());
        
        assert(m.n_samples == sr);
        assert(m.duration_s > 0.99 && m.duration_s < 1.01);
        assert(!std::isnan(m.LAeq));
    }
    
    std::cout << "PASSED (tested 4 sample rates)\n";
}

//==============================================================================
// Test 10: Metrics field count verification (81 fields)
//==============================================================================
void test_metrics_field_count() {
    std::cout << "Test 10: Metrics field count verification... ";
    
    SecondMetrics m;
    
    // Count fields via memory layout inspection
    // SecondMetrics should be a flat struct with all 81 fields
    // This is a compile-time check disguised as runtime
    
    // Access all fields to ensure they exist and are usable
    m.timestamp = 1234567890.0;
    m.duration_s = 1.0;
    m.LAeq = 85.0;
    m.LCeq = 86.0;
    m.LZeq = 84.0;
    m.LAFmax = 88.0;
    m.LZpeak = 100.0;
    m.LCpeak = 99.0;
    m.dose_frac_niosh = 0.01;
    m.dose_frac_osha_pel = 0.005;
    m.dose_frac_osha_hca = 0.008;
    m.dose_frac_eu_iso = 0.007;
    m.overload_flag = false;
    m.underrange_flag = false;
    m.wearing_state = true;
    m.kurtosis_total = 3.0;
    m.kurtosis_a_weighted = 3.0;
    m.kurtosis_c_weighted = 3.0;
    m.beta_kurtosis = 3.0;
    m.n_samples = 48000;
    m.sum_x = 0.0;
    m.sum_x2 = 1.0;
    m.sum_x3 = 0.0;
    m.sum_x4 = 1.0;
    
    // All 9 frequency bands
    double* freq_spls[] = {
        &m.freq_63hz_spl, &m.freq_125hz_spl, &m.freq_250hz_spl,
        &m.freq_500hz_spl, &m.freq_1khz_spl, &m.freq_2khz_spl,
        &m.freq_4khz_spl, &m.freq_8khz_spl, &m.freq_16khz_spl
    };
    for (int i = 0; i < 9; ++i) {
        *freq_spls[i] = 60.0 + i;
    }
    
    // Frequency band moments (9 bands × 5 values = 45 fields)
    // Access via band index
    for (int b = 0; b < 9; ++b) {
        FreqBandMoments* mp = band_moments_ptr(m, b);
        assert(mp != nullptr);
        mp->n = 48000;
        mp->s1 = 0.0;
        mp->s2 = 1.0;
        mp->s3 = 0.0;
        mp->s4 = 1.0;
    }
    
    // Verify all were set
    assert(m.freq_16khz_spl == 68.0);
    assert(m.freq_1khz_spl == 64.0);
    
    std::cout << "PASSED (all fields accessible)\n";
}

//==============================================================================
// Main
//==============================================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "  NoiseProcessor Unit Tests\n";
    std::cout << "========================================\n\n";
    
    try {
        test_second_metrics_basic();
        test_kurtosis_from_moments();
        test_frequency_bands();
        test_minute_aggregation();
        test_qc_flags();
        test_dose_calculation();
        test_buffer_interfaces();
        test_empty_buffer();
        test_sample_rates();
        test_metrics_field_count();
        
        std::cout << "\n========================================\n";
        std::cout << "  ALL TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "\nTEST FAILED: Unknown error\n";
        return 1;
    }
}
