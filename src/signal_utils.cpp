/**
 * @file signal_utils.cpp
 * @brief Signal processing utilities implementation
 *
 * Uses proper IIR filter design for accurate A/C weighting and octave analysis
 */

#include "signal_utils.hpp"
#include "iir_filter.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <complex>

namespace noise_toolkit {

// Reference third-octave center frequencies
const std::vector<float> THIRD_OCTAVE_FREQUENCIES = {
    20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
    200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f,
    1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f,
    6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
};

// Standard octave bands
const std::vector<float> STANDARD_OCTAVE_BANDS = {
    63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

// A-weighting gains at standard octave bands (IEC 61672-1)
const std::vector<float> A_WEIGHTING_GAINS = {
    -26.2f, -16.1f, -8.6f, -3.2f, 0.0f, 1.2f, 1.0f, -1.1f, -6.6f
};

// C-weighting gains at standard octave bands
const std::vector<float> C_WEIGHTING_GAINS = {
    -0.8f, -0.2f, 0.0f, 0.0f, 0.0f, -0.2f, -0.8f, -3.0f, -8.5f
};

// WeightedSignalProcessor class to manage SOS filters
class WeightedSignalProcessor {
public:
    WeightedSignalProcessor() {}

    void init_a_weighting(float sample_rate) {
        sos_a_ = filter_design::a_weighting_design(sample_rate);
        filters_a_.clear();
        for (const auto& coef : sos_a_) {
            filters_a_.emplace_back(coef);
        }
    }

    void init_c_weighting(float sample_rate) {
        sos_c_ = filter_design::c_weighting_design(sample_rate);
        filters_c_.clear();
        for (const auto& coef : sos_c_) {
            filters_c_.emplace_back(coef);
        }
    }

    std::vector<float> apply_a(const std::vector<float>& signal) {
        std::vector<float> result = signal;
        for (auto& filter : filters_a_) {
            result = filter.process(result);
        }
        return result;
    }

    std::vector<float> apply_c(const std::vector<float>& signal) {
        std::vector<float> result = signal;
        for (auto& filter : filters_c_) {
            result = filter.process(result);
        }
        return result;
    }

private:
    std::vector<BiquadCoefficients> sos_a_;
    std::vector<BiquadCoefficients> sos_c_;
    std::vector<BiquadFilter> filters_a_;
    std::vector<BiquadFilter> filters_c_;
};

// Thread-local processor instance
thread_local WeightedSignalProcessor g_processor;

// Apply A-weighting filter using proper IEC 61672-1 design
std::vector<float> apply_a_weighting(const std::vector<float>& signal, float sample_rate) {
    g_processor.init_a_weighting(sample_rate);
    return g_processor.apply_a(signal);
}

// Apply C-weighting filter using proper IEC 61672-1 design
std::vector<float> apply_c_weighting(const std::vector<float>& signal, float sample_rate) {
    g_processor.init_c_weighting(sample_rate);
    return g_processor.apply_c(signal);
}

// Calculate RMS value
float calculate_rms(const std::vector<float>& signal) {
    if (signal.empty()) return 0.0f;

    float sum_squares = 0.0f;
    for (float x : signal) {
        sum_squares += x * x;
    }
    return std::sqrt(sum_squares / signal.size());
}

// Calculate equivalent sound pressure level (Leq)
float calculate_leq(const std::vector<float>& signal, float reference_pressure) {
    float rms = calculate_rms(signal);
    if (rms <= 0) return -std::numeric_limits<float>::infinity();
    return 20.0f * std::log10(rms / reference_pressure);
}

// Calculate peak sound pressure level
float calculate_lpeak(const std::vector<float>& signal, float reference_pressure) {
    if (signal.empty()) return -std::numeric_limits<float>::infinity();

    float peak = 0.0f;
    for (float x : signal) {
        peak = std::max(peak, std::abs(x));
    }
    if (peak <= 0) return -std::numeric_limits<float>::infinity();
    return 20.0f * std::log10(peak / reference_pressure);
}

// Time-averaged sound level with specified averaging time
std::vector<float> time_average(const std::vector<float>& signal,
                                   float sample_rate,
                                   float averaging_time_s) {
    int samples_per_avg = static_cast<int>(sample_rate * averaging_time_s);
    if (samples_per_avg <= 0) samples_per_avg = 1;

    std::vector<float> result;

    for (size_t i = 0; i + samples_per_avg <= signal.size(); i += samples_per_avg) {
        std::vector<float> window(signal.begin() + i, signal.begin() + i + samples_per_avg);
        result.push_back(calculate_rms(window));
    }

    // Handle remaining samples
    size_t remaining = signal.size() % samples_per_avg;
    if (remaining > 0 && !signal.empty()) {
        size_t start = signal.size() - remaining;
        std::vector<float> window(signal.begin() + start, signal.end());
        result.push_back(calculate_rms(window));
    }

    return result;
}

// Fast time weighting (125ms) for LAF
std::vector<float> fast_time_weighting(const std::vector<float>& signal, float sample_rate) {
    float tau = 0.125f;  // 125ms
    float alpha = std::exp(-1.0f / (sample_rate * tau));

    std::vector<float> result(signal.size());
    float y_prev = signal.empty() ? 0.0f : signal[0] * signal[0];

    for (size_t i = 0; i < signal.size(); ++i) {
        float x_sq = signal[i] * signal[i];
        y_prev = alpha * y_prev + (1.0f - alpha) * x_sq;
        result[i] = std::sqrt(y_prev);
    }

    return result;
}

// Slow time weighting (1s) for LAS
std::vector<float> slow_time_weighting(const std::vector<float>& signal, float sample_rate) {
    float tau = 1.0f;  // 1 second
    float alpha = std::exp(-1.0f / (sample_rate * tau));

    std::vector<float> result(signal.size());
    float y_prev = signal.empty() ? 0.0f : signal[0] * signal[0];

    for (size_t i = 0; i < signal.size(); ++i) {
        float x_sq = signal[i] * signal[i];
        y_prev = alpha * y_prev + (1.0f - alpha) * x_sq;
        result[i] = std::sqrt(y_prev);
    }

    return result;
}

// Impulse time weighting (35ms) for LAI
std::vector<float> impulse_time_weighting(const std::vector<float>& signal, float sample_rate) {
    float tau = 0.035f;  // 35ms
    float alpha = std::exp(-1.0f / (sample_rate * tau));

    std::vector<float> result(signal.size());
    float y_prev = signal.empty() ? 0.0f : signal[0] * signal[0];

    for (size_t i = 0; i < signal.size(); ++i) {
        float x_sq = signal[i] * signal[i];
        y_prev = alpha * y_prev + (1.0f - alpha) * x_sq;
        result[i] = std::sqrt(y_prev);
    }

    return result;
}

// Calculate kurtosis
// fisher=false: Pearson kurtosis (normal distribution = 3)
// fisher=true: Excess kurtosis (normal distribution = 0)
float calculate_kurtosis(const std::vector<float>& signal, bool fisher) {
    size_t n = signal.size();
    if (n < 4) return fisher ? 0.0f : 3.0f;

    // Calculate mean
    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / n;

    // Calculate moments
    float m2 = 0.0f;  // Second moment (variance)
    float m4 = 0.0f;  // Fourth moment

    for (float x : signal) {
        float d = x - mean;
        float d2 = d * d;
        m2 += d2;
        m4 += d2 * d2;
    }

    m2 /= n;
    m4 /= n;

    if (m2 <= 0) return fisher ? 0.0f : 3.0f;

    // Pearson kurtosis
    float kurtosis = m4 / (m2 * m2);

    return fisher ? (kurtosis - 3.0f) : kurtosis;
}

// Calculate excess kurtosis (kurtosis - 3)
float calculate_excess_kurtosis(const std::vector<float>& signal) {
    return calculate_kurtosis(signal, true);
}

// 1/3 octave band analysis using proper filter banks
std::pair<std::vector<float>, std::vector<float>>
    third_octave_analysis(const std::vector<float>& signal, float sample_rate) {

    std::vector<float> center_freqs;
    std::vector<float> band_levels;

    // Get 1/3 octave center frequencies within valid range
    auto all_centers = octave_filters::third_octave_centers(20.0, sample_rate / 2.0);

    // Filter signal for each 1/3 octave band
    for (double fc : all_centers) {
        // Skip if above Nyquist frequency
        if (fc >= sample_rate / 2.0) continue;

        // Design 1/3 octave filter
        auto coef = octave_filters::design_third_octave(fc, sample_rate, 4);

        // Apply filter
        IIRFilter filter(coef.b, coef.a);
        auto filtered = filter.process(signal);

        // Calculate band level
        float band_rms = calculate_rms(filtered);
        float band_db = pressure_to_db(band_rms);

        center_freqs.push_back(static_cast<float>(fc));
        band_levels.push_back(band_db);
    }

    return {center_freqs, band_levels};
}

// Standard octave band analysis
std::pair<std::vector<float>, std::vector<float>>
    octave_analysis(const std::vector<float>& signal, float sample_rate) {

    std::vector<float> center_freqs;
    std::vector<float> band_levels;

    for (float fc : STANDARD_OCTAVE_BANDS) {
        if (fc >= sample_rate / 2.0f) continue;

        auto coef = octave_filters::design_octave(fc, sample_rate, 4);

        IIRFilter filter(coef.b, coef.a);
        auto filtered = filter.process(signal);

        float band_rms = calculate_rms(filtered);
        float band_db = pressure_to_db(band_rms);

        center_freqs.push_back(fc);
        band_levels.push_back(band_db);
    }

    return {center_freqs, band_levels};
}

// FFT implementation using Cooley-Tukey algorithm (O(N log N))
std::vector<std::complex<float>> fft(const std::vector<float>& signal) {
    size_t N = signal.size();

    // Pad to power of 2
    size_t N_padded = 1;
    while (N_padded < N) N_padded <<= 1;

    std::vector<std::complex<float>> x(N_padded);
    for (size_t i = 0; i < N; ++i) {
        x[i] = std::complex<float>(signal[i], 0.0f);
    }
    for (size_t i = N; i < N_padded; ++i) {
        x[i] = std::complex<float>(0.0f, 0.0f);
    }

    // Bit-reversal permutation
    for (size_t i = 0, j = 0; i < N_padded; ++i) {
        if (i < j) std::swap(x[i], x[j]);

        size_t bit = N_padded >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
    }

    // Cooley-Tukey FFT
    for (size_t len = 2; len <= N_padded; len <<= 1) {
        float ang = 2.0f * M_PI / len;
        std::complex<float> wlen(std::cos(ang), std::sin(ang));

        for (size_t i = 0; i < N_padded; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    // Return only original size (remove padding)
    x.resize(N);
    return x;
}

// Inverse FFT
std::vector<float> ifft(const std::vector<std::complex<float>>& spectrum) {
    size_t N = spectrum.size();

    // Conjugate
    std::vector<std::complex<float>> x(N);
    for (size_t i = 0; i < N; ++i) {
        x[i] = std::conj(spectrum[i]);
    }

    // Forward FFT (simplified - just return real parts)
    std::vector<float> result(N);
    for (size_t i = 0; i < N; ++i) {
        result[i] = x[i].real();
    }

    return result;
}

// Apply IIR filter
template<typename T>
std::vector<float> apply_iir_filter(const std::vector<float>& signal,
                                      const std::vector<float>& b,
                                      const std::vector<float>& a) {
    IIRFilter filter(b, a);
    return filter.process(signal);
}

// Bandpass filter design and application
std::vector<float> bandpass_filter(const std::vector<float>& signal,
                                     float sample_rate,
                                     float low_freq,
                                     float high_freq) {
    auto coef = filter_design::bandpass(low_freq, high_freq, sample_rate, 4);

    IIRFilter filter(coef.b, coef.a);
    return filter.process(signal);
}

// Highpass filter
std::vector<float> highpass_filter(const std::vector<float>& signal,
                                    float sample_rate,
                                    float cutoff_freq,
                                    int order) {
    auto coef = filter_design::butterworth(order, cutoff_freq, sample_rate, "high");

    IIRFilter filter(coef.b, coef.a);
    return filter.process(signal);
}

// Lowpass filter
std::vector<float> lowpass_filter(const std::vector<float>& signal,
                                   float sample_rate,
                                   float cutoff_freq,
                                   int order) {
    auto coef = filter_design::butterworth(order, cutoff_freq, sample_rate, "low");

    IIRFilter filter(coef.b, coef.a);
    return filter.process(signal);
}

// Spectral analysis using FFT
std::vector<float> calculate_spectrum(const std::vector<float>& signal,
                                      float sample_rate,
                                      std::vector<float>& frequencies) {
    auto spec = fft(signal);

    size_t n = spec.size();
    size_t n_freq = n / 2 + 1;

    std::vector<float> magnitude(n_freq);
    frequencies.resize(n_freq);

    float df = sample_rate / n;  // Frequency resolution

    for (size_t i = 0; i < n_freq; ++i) {
        magnitude[i] = std::abs(spec[i]) / n;  // Normalize
        frequencies[i] = static_cast<float>(i) * df;
    }

    return magnitude;
}

// Calculate power spectral density (PSD)
std::vector<float> calculate_psd(const std::vector<float>& signal,
                                 float sample_rate,
                                 std::vector<float>& frequencies) {
    auto magnitude = calculate_spectrum(signal, sample_rate, frequencies);

    // PSD = |FFT|^2 / (N * fs)
    float scale = 1.0f / (signal.size() * sample_rate);

    for (auto& m : magnitude) {
        m = m * m / scale;
    }

    return magnitude;
}

} // namespace noise_toolkit