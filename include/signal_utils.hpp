/**
 * @file signal_utils.hpp
 * @brief Signal processing utilities
 */

#pragma once

#include "noise_toolkit.hpp"
#include <vector>
#include <cmath>
#include <complex>

namespace noise_toolkit {

// Third-octave band center frequencies (20Hz - 20kHz)
extern const std::vector<float> THIRD_OCTAVE_FREQUENCIES;

// Standard octave bands (63Hz to 16kHz)
extern const std::vector<float> STANDARD_OCTAVE_BANDS;

// A-weighting gains at standard octave bands (IEC 61672-1)
extern const std::vector<float> A_WEIGHTING_GAINS;

// C-weighting gains at standard octave bands
extern const std::vector<float> C_WEIGHTING_GAINS;

/**
 * @brief Signal structure for audio processing
 */
struct Signal {
    std::vector<float> data;
    float sample_rate;
    int channels;

    Signal() : sample_rate(48000.0f), channels(1) {}
    Signal(const std::vector<float>& d, float sr, int ch = 1)
        : data(d), sample_rate(sr), channels(ch) {}

    float duration() const {
        return static_cast<float>(data.size()) / (sample_rate * channels);
    }

    size_t size() const { return data.size(); }
};

/**
 * @brief Apply A-weighting filter to signal (IEC 61672-1)
 */
std::vector<float> apply_a_weighting(const std::vector<float>& signal, float sample_rate);

/**
 * @brief Apply C-weighting filter to signal (IEC 61672-1)
 */
std::vector<float> apply_c_weighting(const std::vector<float>& signal, float sample_rate);

/**
 * @brief Calculate RMS of signal
 */
float calculate_rms(const std::vector<float>& signal);

/**
 * @brief Calculate equivalent sound pressure level (Leq)
 */
float calculate_leq(const std::vector<float>& signal,
                     float reference_pressure = REFERENCE_PRESSURE);

/**
 * @brief Calculate peak sound pressure level
 */
float calculate_lpeak(const std::vector<float>& signal,
                        float reference_pressure = REFERENCE_PRESSURE);

/**
 * @brief Calculate time-averaged sound level with specified averaging time
 */
std::vector<float> time_average(const std::vector<float>& signal,
                                    float sample_rate,
                                    float averaging_time_s);

/**
 * @brief Calculate kurtosis
 * fisher=false: Pearson kurtosis (normal = 3)
 * fisher=true: Excess kurtosis (normal = 0)
 */
float calculate_kurtosis(const std::vector<float>& signal, bool fisher = false);

/**
 * @brief Calculate excess kurtosis (kurtosis - 3)
 */
float calculate_excess_kurtosis(const std::vector<float>& signal);

/**
 * @brief Compute 1/3 octave band analysis using proper filter banks
 * @return Pair of (center_frequencies, band_levels)
 */
std::pair<std::vector<float>, std::vector<float>>
    third_octave_analysis(const std::vector<float>& signal,
                          float sample_rate);

/**
 * @brief Compute octave band analysis
 */
std::pair<std::vector<float>, std::vector<float>>
    octave_analysis(const std::vector<float>& signal, float sample_rate);

/**
 * @brief Compute FFT using Cooley-Tukey algorithm
 */
std::vector<std::complex<float>> fft(const std::vector<float>& signal);

/**
 * @brief Compute Inverse FFT
 */
std::vector<float> ifft(const std::vector<std::complex<float>>& spectrum);

/**
 * @brief Apply bandpass filter
 */
std::vector<float> bandpass_filter(const std::vector<float>& signal,
                                     float sample_rate,
                                     float low_freq,
                                     float high_freq);

/**
 * @brief Apply highpass filter
 */
std::vector<float> highpass_filter(const std::vector<float>& signal,
                                    float sample_rate,
                                    float cutoff_freq,
                                    int order = 4);

/**
 * @brief Apply lowpass filter
 */
std::vector<float> lowpass_filter(const std::vector<float>& signal,
                                   float sample_rate,
                                   float cutoff_freq,
                                   int order = 4);

/**
 * @brief Fast time weighting (125ms) - for LAFmax
 */
std::vector<float> fast_time_weighting(const std::vector<float>& signal,
                                          float sample_rate);

/**
 * @brief Slow time weighting (1s) - for LAS
 */
std::vector<float> slow_time_weighting(const std::vector<float>& signal,
                                        float sample_rate);

/**
 * @brief Impulse time weighting (35ms) - for LAI
 */
std::vector<float> impulse_time_weighting(const std::vector<float>& signal,
                                          float sample_rate);

/**
 * @brief Calculate spectrum using FFT
 * @return Magnitude spectrum
 */
std::vector<float> calculate_spectrum(const std::vector<float>& signal,
                                      float sample_rate,
                                      std::vector<float>& frequencies);

/**
 * @brief Calculate power spectral density (PSD)
 */
std::vector<float> calculate_psd(const std::vector<float>& signal,
                                 float sample_rate,
                                 std::vector<float>& frequencies);

/**
 * @brief Convert dB to pressure
 */
inline float db_to_pressure(float db, float p0) {
    return p0 * std::pow(10.0f, db / 20.0f);
}

/**
 * @brief Convert pressure to dB
 */
inline float pressure_to_db(float pressure, float p0) {
    if (pressure <= 0) return -std::numeric_limits<float>::infinity();
    return 20.0f * std::log10(pressure / p0);
}

} // namespace noise_toolkit