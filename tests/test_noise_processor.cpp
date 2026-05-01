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

std::vector<float> generate_sine(float freq_hz, float amplitude,
                                   int sample_rate, float duration_s) {
    int n = static_cast<int>(sample_rate * duration_s);
    std::vector<float> data(n);
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        data[i] = amplitude * std::sin(2.0f * M_PI * freq_hz * t);
    }
    return data;
}

std::vector<float> generate_noise(float amplitude, int n) {
    std::vector<float> data(n);
    for (int i = 0; i < n; ++i) {
        data[i] = amplitude * (2.0f * (static_cast<float>(rand()) / RAND_MAX) - 1.0f);
    }
    return data;
}

void test_second_metrics_basic() {
    std::cout << "Test 1: Basic second metrics computation... ";

    NoiseProcessor processor(48000);

    float spl_target = 94.0f;
    float rms = 20e-6f * std::pow(10.0f, spl_target / 20.0f);
    float amplitude = rms * std::sqrt(2.0f);

    auto data = generate_sine(1000.0f, amplitude, 48000, 1.0f);

    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

    assert(m.LAeq > 85.0f && m.LAeq < 105.0f);
    assert(m.n_samples == 48000);
    assert(m.duration_s > 0.99f && m.duration_s < 1.01f);
    assert(!std::isnan(m.kurtosis_total));
    assert(m.kurtosis_total > 1.0f);

    std::cout << "PASSED (LAeq=" << m.LAeq << " dB, kurtosis=" << m.kurtosis_total << ")\n";
}

void test_kurtosis_from_moments() {
    std::cout << "Test 2: Kurtosis from moments... ";

    int n = 48000;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;

    for (int i = 0; i < n; ++i) {
        float x = 0.0f;
        for (int j = 0; j < 12; ++j) {
            x += (2.0f * (static_cast<float>(rand()) / RAND_MAX) - 1.0f);
        }
        x = x / 12.0f;

        s1 += x;
        s2 += x * x;
        s3 += x * x * x;
        s4 += x * x * x * x;
    }

    float beta = calculate_kurtosis_from_moments(n, s1, s2, s3, s4);

    assert(std::abs(beta - 3.0f) < 0.5f);

    assert(std::isnan(calculate_kurtosis_from_moments(0, 0.0f, 0.0f, 0.0f, 0.0f)));

    assert(std::isnan(calculate_kurtosis_from_moments(100, 10.0f, 100.0f, 1000.0f, 10000.0f)));

    std::cout << "PASSED (beta=" << beta << ")\n";
}

void test_frequency_bands() {
    std::cout << "Test 3: Frequency band analysis... ";

    NoiseProcessor processor(48000);

    auto data = generate_sine(1000.0f, 1.0f, 48000, 1.0f);
    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

    assert(m.freq_1khz_spl > m.freq_63hz_spl);
    assert(m.freq_1khz_spl > m.freq_8khz_spl);

    assert(m.freq_1khz_n > 0);
    assert(m.freq_1khz_s1 != 0.0f || m.freq_1khz_s2 != 0.0f);

    std::cout << "PASSED (1kHz SPL=" << m.freq_1khz_spl << " dB)\n";
}

void test_minute_aggregation() {
    std::cout << "Test 4: Per-minute aggregation... ";

    NoiseProcessor processor(48000);

    std::array<SecondMetrics, 60> seconds;

    for (int i = 0; i < 60; ++i) {
        float spl = 70.0f + 20.0f * (i / 60.0f);
        float rms = 20e-6f * std::pow(10.0f, spl / 20.0f);
        float amplitude = rms * std::sqrt(2.0f);

        auto data = generate_sine(1000.0f, amplitude, 48000, 1.0f);
        seconds[i] = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);
        seconds[i].timestamp = static_cast<float>(i);
    }

    MinuteMetrics minute = processor.aggregate_minute_metrics(seconds.data(), 60, 1.0f);

    assert(minute.duration_s > 59.0f && minute.duration_s < 61.0f);
    assert(minute.valid_seconds == 60);
    assert(minute.LAeq > 70.0f && minute.LAeq < 95.0f);
    assert(minute.dose_frac_niosh > 0.0f);
    assert(minute.freq_1khz_spl > 0.0f);

    assert(minute.freq_1khz_n > 0);
    assert(minute.freq_1khz_s1 != 0.0f || minute.freq_1khz_s2 != 0.0f);

    std::cout << "PASSED (LAeq=" << minute.LAeq << " dB, dose_niosh="
              << minute.dose_frac_niosh << ")\n";
}

void test_qc_flags() {
    std::cout << "Test 5: QC flags... ";

    NoiseProcessor processor(48000);

    auto quiet = generate_sine(1000.0f, 1e-6f, 48000, 1.0f);
    SecondMetrics m_quiet = processor.process_one_second(quiet.data(), quiet.data() + quiet.size(), 1.0f);
    assert(m_quiet.underrange_flag == true);

    auto normal = generate_sine(1000.0f, 1.0f, 48000, 1.0f);
    SecondMetrics m_normal = processor.process_one_second(normal.data(), normal.data() + normal.size(), 1.0f);
    assert(m_normal.underrange_flag == false);
    assert(m_normal.wearing_state == true);

    std::cout << "PASSED\n";
}

void test_dose_calculation() {
    std::cout << "Test 6: Dose calculation... ";

    NoiseProcessor processor(48000);

    float rms_85 = 20e-6f * std::pow(10.0f, 85.0f / 20.0f);
    auto data = generate_sine(1000.0f, rms_85 * std::sqrt(2.0f), 48000, 1.0f);
    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

    assert(m.dose_frac_niosh > 0.0f);
    assert(m.dose_frac_niosh < 0.01f);

    float rms_91 = 20e-6f * std::pow(10.0f, 91.0f / 20.0f);
    auto data_91 = generate_sine(1000.0f, rms_91 * std::sqrt(2.0f), 48000, 1.0f);
    SecondMetrics m_91 = processor.process_one_second(data_91.data(), data_91.data() + data_91.size(), 1.0f);

    assert(m_91.dose_frac_niosh > m.dose_frac_niosh);

    std::cout << "PASSED (85dB: " << m.dose_frac_niosh << ", 91dB: " << m_91.dose_frac_niosh << ")\n";
}

void test_buffer_interfaces() {
    std::cout << "Test 7: Float buffer interface... ";

    NoiseProcessor processor(48000);

    std::vector<float> data = generate_sine(1000.0f, 1.0f, 48000, 1.0f);

    SecondMetrics m_float = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

    assert(!std::isnan(m_float.LAeq));
    assert(!std::isnan(m_float.kurtosis_total));

    std::cout << "PASSED\n";
}

void test_empty_buffer() {
    std::cout << "Test 8: Empty buffer handling... ";

    NoiseProcessor processor(48000);

    std::vector<float> empty;
    SecondMetrics m = processor.process_one_second(empty.data(), empty.data(), 0.01f);

    assert(std::isnan(m.LAeq) || m.LAeq == 0.0f);
    assert(m.n_samples == 0);

    std::cout << "PASSED\n";
}

void test_sample_rates() {
    std::cout << "Test 9: Sample rate variations... ";

    std::vector<int> sample_rates = {8000, 16000, 44100, 48000};

    for (int sr : sample_rates) {
        NoiseProcessor processor(sr);
        auto data = generate_sine(1000.0f, 1.0f, sr, 1.0f);
        SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

        assert(m.n_samples == sr);
        assert(m.duration_s > 0.99f && m.duration_s < 1.01f);
        assert(!std::isnan(m.LAeq));
    }

    std::cout << "PASSED (tested 4 sample rates)\n";
}

void test_metrics_field_count() {
    std::cout << "Test 10: Metrics field count verification... ";

    SecondMetrics m;

    m.timestamp = 1234567890.0f;
    m.duration_s = 1.0f;
    m.LAeq = 85.0f;
    m.LCeq = 86.0f;
    m.LZeq = 84.0f;
    m.LAFmax = 88.0f;
    m.LZPeak = 100.0f;
    m.LCPeak = 99.0f;
    m.dose_frac_niosh = 0.01f;
    m.dose_frac_osha_pel = 0.005f;
    m.dose_frac_osha_hca = 0.008f;
    m.dose_frac_eu_iso = 0.007f;
    m.overload_flag = false;
    m.underrange_flag = false;
    m.wearing_state = true;
    m.kurtosis_total = 3.0f;
    m.kurtosis_a_weighted = 3.0f;
    m.kurtosis_c_weighted = 3.0f;
    m.beta_kurtosis = 3.0f;
    m.n_samples = 48000;
    m.sum_x = 0.0f;
    m.sum_x2 = 1.0f;
    m.sum_x3 = 0.0f;
    m.sum_x4 = 1.0f;

    float* freq_spls[] = {
        &m.freq_63hz_spl, &m.freq_125hz_spl, &m.freq_250hz_spl,
        &m.freq_500hz_spl, &m.freq_1khz_spl, &m.freq_2khz_spl,
        &m.freq_4khz_spl, &m.freq_8khz_spl, &m.freq_16khz_spl
    };
    for (int i = 0; i < 9; ++i) {
        *freq_spls[i] = 60.0f + i;
    }

    for (int b = 0; b < 9; ++b) {
        FreqBandMoments* mp = band_moments_ptr(m, b);
        assert(mp != nullptr);
        mp->n = 48000;
        mp->s1 = 0.0f;
        mp->s2 = 1.0f;
        mp->s3 = 0.0f;
        mp->s4 = 1.0f;
    }

    assert(m.freq_16khz_spl == 68.0f);
    assert(m.freq_1khz_spl == 64.0f);

    std::cout << "PASSED (all fields accessible)\n";
}

void test_flexible_duration_processing() {
    std::cout << "Test 11: Flexible duration processing (10ms)... ";

    NoiseProcessor processor(48000);

    int samples_per_10ms = 480;
    auto data = generate_sine(1000.0f, 1.0f, 48000, 0.01f);

    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 0.01f);

    assert(m.n_samples == samples_per_10ms);
    assert(m.duration_s > 0.009f && m.duration_s < 0.011f);
    assert(!std::isnan(m.LAeq));

    std::cout << "PASSED (10ms processing: n_samples=" << m.n_samples << ")\n";
}

void test_aggregation_with_different_unit_durations() {
    std::cout << "Test 12: Aggregation with different unit durations... ";

    NoiseProcessor processor(48000);

    std::array<SecondMetrics, 100> metrics_10ms;

    for (int i = 0; i < 100; ++i) {
        auto data = generate_sine(1000.0f, 1.0f, 48000, 0.01f);
        metrics_10ms[i] = processor.process_one_second(data.data(), data.data() + data.size(), 0.01f);
        metrics_10ms[i].timestamp = static_cast<float>(i) * 0.01f;
    }

    MinuteMetrics minute = processor.aggregate_minute_metrics(metrics_10ms.data(), 100, 0.01f);

    assert(minute.duration_s > 0.9f && minute.duration_s < 1.1f);
    assert(minute.valid_seconds == 100);

    std::cout << "PASSED (aggregated 100 x 10ms = " << minute.duration_s << "s)\n";
}

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
        test_flexible_duration_processing();
        test_aggregation_with_different_unit_durations();

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