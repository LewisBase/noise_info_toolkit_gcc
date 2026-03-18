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
extern const std::vector<double> THIRD_OCTAVE_FREQUENCIES;

// Standard octave bands (63Hz to 16kHz)
extern const std::vector<double> STANDARD_OCTAVE_BANDS;

// A-weighting gains at standard octave bands (IEC 61672-1)
extern const std::vector<double> A_WEIGHTING_GAINS;

// C-weighting gains at standard octave bands
extern const std::vector<double> C_WEIGHTING_GAINS;

/**
 * @brief Signal structure for audio processing
 */
struct Signal {
    std::vector<double> data;
    double sample_rate;
    int channels;
    
    Signal() : sample_rate(48000), channels(1) {}
    Signal(const std::vector<double>& d, double sr, int ch = 1)
        : data(d), sample_rate(sr), channels(ch) {}
    
    double duration() const {
        return static_cast<double>(data.size()) / (sample_rate * channels);
    }
    
    size_t size() const { return data.size(); }
};

/**
 * @brief Apply A-weighting filter to signal (IEC 61672-1)
 */
std::vector<double> apply_a_weighting(const std::vector<double>& signal, double sample_rate);

/**
 * @brief Apply C-weighting filter to signal (IEC 61672-1)
 */
std::vector<double> apply_c_weighting(const std::vector<double>& signal, double sample_rate);

/**
 * @brief Calculate RMS of signal
 */
double calculate_rms(const std::vector<double>& signal);

/**
 * @brief Calculate equivalent sound pressure level (Leq)
 */
double calculate_leq(const std::vector<double>& signal, 
                     double reference_pressure = REFERENCE_PRESSURE);

/**
 * @brief Calculate peak sound pressure level
 */
double calculate_lpeak(const std::vector<double>& signal,
                       double reference_pressure = REFERENCE_PRESSURE);

/**
 * @brief Calculate time-averaged sound level with specified averaging time
 */
std::vector<double> time_average(const std::vector<double>& signal, 
                                   double sample_rate,
                                   double averaging_time_s);

/**
 * @brief Calculate kurtosis
 * fisher=false: Pearson kurtosis (normal = 3)
 * fisher=true: Excess kurtosis (normal = 0)
 */
double calculate_kurtosis(const std::vector<double>& signal, bool fisher = false);

/**
 * @brief Calculate excess kurtosis (kurtosis - 3)
 */
double calculate_excess_kurtosis(const std::vector<double>& signal);

/**
 * @brief Compute 1/3 octave band analysis using proper filter banks
 * @return Pair of (center_frequencies, band_levels)
 */
std::pair<std::vector<double>, std::vector<double>> 
    third_octave_analysis(const std::vector<double>& signal,
                          double sample_rate);

/**
 * @brief Compute octave band analysis
 */
std::pair<std::vector<double>, std::vector<double>>
    octave_analysis(const std::vector<double>& signal, double sample_rate);

/**
 * @brief Compute FFT using Cooley-Tukey algorithm
 */
std::vector<std::complex<double>> fft(const std::vector<double>& signal);

/**
 * @brief Compute Inverse FFT
 */
std::vector<double> ifft(const std::vector<std::complex<double>>& spectrum);

/**
 * @brief Apply bandpass filter
 */
std::vector<double> bandpass_filter(const std::vector<double>& signal,
                                     double sample_rate,
                                     double low_freq,
                                     double high_freq);

/**
 * @brief Apply highpass filter
 */
std::vector<double> highpass_filter(const std::vector<double>& signal,
                                    double sample_rate,
                                    double cutoff_freq,
                                    int order = 4);

/**
 * @brief Apply lowpass filter
 */
std::vector<double> lowpass_filter(const std::vector<double>& signal,
                                   double sample_rate,
                                   double cutoff_freq,
                                   int order = 4);

/**
 * @brief Fast time weighting (125ms) - for LAFmax
 */
std::vector<double> fast_time_weighting(const std::vector<double>& signal,
                                         double sample_rate);

/**
 * @brief Slow time weighting (1s) - for LAS
 */
std::vector<double> slow_time_weighting(const std::vector<double>& signal,
                                        double sample_rate);

/**
 * @brief Impulse time weighting (35ms) - for LAI
 */
std::vector<double> impulse_time_weighting(const std::vector<double>& signal,
                                          double sample_rate);

/**
 * @brief Calculate spectrum using FFT
 * @return Magnitude spectrum
 */
std::vector<double> calculate_spectrum(const std::vector<double>& signal, 
                                      double sample_rate,
                                      std::vector<double>& frequencies);

/**
 * @brief Calculate power spectral density (PSD)
 */
std::vector<double> calculate_psd(const std::vector<double>& signal,
                                 double sample_rate,
                                 std::vector<double>& frequencies);

/**
 * @brief Convert dB to pressure
 */
inline double db_to_pressure(double db, double p0) {
    return p0 * std::pow(10.0, db / 20.0);
}

/**
 * @brief Convert pressure to dB
 */
inline double pressure_to_db(double pressure, double p0) {
    if (pressure <= 0) return -std::numeric_limits<double>::infinity();
    return 20.0 * std::log10(pressure / p0);
}

} // namespace noise_toolkit
