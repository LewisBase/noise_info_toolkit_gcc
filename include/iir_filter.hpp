/**
 * @file iir_filter.hpp
 * @brief IIR Filter Design and Implementation
 * 
 * Implements standard IIR filters (Butterworth, Biquad, etc.)
 * Following scipy.signal and standard DSP algorithms
 */

#pragma once

#include <vector>
#include <complex>
#include <cmath>

namespace noise_toolkit {

/**
 * @brief IIR Filter coefficients
 */
struct IIRCoefficients {
    std::vector<double> b;  // Numerator coefficients
    std::vector<double> a;  // Denominator coefficients
};

/**
 * @brief Second-order section (biquad) coefficients
 */
struct BiquadCoefficients {
    double b0, b1, b2;  // Numerator
    double a0, a1, a2;  // Denominator (a0 usually = 1)
};

/**
 * @brief IIR Filter class
 */
class IIRFilter {
public:
    /**
     * @brief Create filter from coefficients
     */
    IIRFilter(const std::vector<double>& b, const std::vector<double>& a);
    
    /**
     * @brief Process single sample
     */
    double process(double input);
    
    /**
     * @brief Process entire signal
     */
    std::vector<double> process(const std::vector<double>& signal);
    
    /**
     * @brief Reset filter state
     */
    void reset();
    
    /**
     * @brief Get filter coefficients
     */
    IIRCoefficients get_coefficients() const { return {b_, a_}; }

private:
    std::vector<double> b_;
    std::vector<double> a_;
    std::vector<double> state_b_;  // Delay line for input
    std::vector<double> state_a_;  // Delay line for output
};

/**
 * @brief Biquad filter (second-order section)
 */
class BiquadFilter {
public:
    BiquadFilter(double b0, double b1, double b2, double a0, double a1, double a2);
    explicit BiquadFilter(const BiquadCoefficients& coef);
    
    double process(double input);
    std::vector<double> process(const std::vector<double>& signal);
    void reset();

private:
    double b0_, b1_, b2_;
    double a0_, a1_, a2_;
    double x1_, x2_;  // Input delay
    double y1_, y2_;  // Output delay
};

/**
 * @brief Filter design functions
 */
namespace filter_design {

/**
 * @brief Design Butterworth filter
 * @param order Filter order
 * @param critical_freq Critical frequency (normalized 0-1 or in Hz)
 * @param sample_rate Sample rate in Hz (if critical_freq is in Hz)
 * @param btype Filter type: "low", "high", "band", "bandstop"
 * @return Filter coefficients
 */
IIRCoefficients butterworth(int order, 
                            double critical_freq,
                            double sample_rate,
                            const std::string& btype = "low");

/**
 * @brief Design bandpass filter using Butterworth
 */
IIRCoefficients bandpass(double low_freq, 
                         double high_freq,
                         double sample_rate,
                         int order = 4);

/**
 * @brief Design A-weighting filter (IEC 61672-1:2013)
 * @param sample_rate Sample rate in Hz
 * @return Biquad sections for A-weighting
 */
std::vector<BiquadCoefficients> a_weighting_design(double sample_rate);

/**
 * @brief Design C-weighting filter (IEC 61672-1:2013)
 * @param sample_rate Sample rate in Hz
 * @return Biquad sections for C-weighting
 */
std::vector<BiquadCoefficients> c_weighting_design(double sample_rate);

/**
 * @brief Design Z-weighting filter (flat, unity gain)
 * @param sample_rate Sample rate in Hz
 * @return Unity filter coefficients
 */
IIRCoefficients z_weighting_design(double sample_rate);

/**
 * @brief Bilinear transform from analog to digital
 * @param analog_zeros Analog zeros
 * @param analog_poles Analog poles
 * @param gain Analog gain
 * @param sample_rate Sample rate
 * @return Digital filter coefficients
 */
IIRCoefficients bilinear_transform(const std::vector<std::complex<double>>& analog_zeros,
                                   const std::vector<std::complex<double>>& analog_poles,
                                   double gain,
                                   double sample_rate);

/**
 * @brief Convert filter to second-order sections (SOS)
 */
std::vector<BiquadCoefficients> tf2sos(const std::vector<double>& b,
                                       const std::vector<double>& a);

} // namespace filter_design

/**
 * @brief Octave band filter design
 */
namespace octave_filters {

/**
 * @brief Design 1/3 octave band filter
 * @param center_freq Center frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @param order Filter order
 * @return Filter coefficients
 */
IIRCoefficients design_third_octave(double center_freq, 
                                    double sample_rate,
                                    int order = 4);

/**
 * @brief Design octave band filter
 */
IIRCoefficients design_octave(double center_freq,
                              double sample_rate,
                              int order = 4);

/**
 * @brief Get 1/3 octave center frequencies
 * @param fmin Minimum frequency
 * @param fmax Maximum frequency
 * @return Vector of center frequencies
 */
std::vector<double> third_octave_centers(double fmin = 20, double fmax = 20000);

/**
 * @brief Get octave band center frequencies
 */
std::vector<double> octave_centers(double fmin = 31.5, double fmax = 16000);

} // namespace octave_filters

} // namespace noise_toolkit
