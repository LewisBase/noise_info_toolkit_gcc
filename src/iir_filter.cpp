/**
 * @file iir_filter.cpp
 * @brief IIR Filter Design and Implementation
 */

#include "iir_filter.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace noise_toolkit {

// IIRFilter implementation
IIRFilter::IIRFilter(const std::vector<double>& b, const std::vector<double>& a)
    : b_(b), a_(a) {
    if (a.empty() || b.empty()) {
        throw std::invalid_argument("Filter coefficients cannot be empty");
    }
    if (a[0] == 0.0) {
        throw std::invalid_argument("a[0] cannot be zero");
    }
    
    // Normalize by a[0]
    if (a_[0] != 1.0) {
        for (double& coeff : b_) coeff /= a_[0];
        for (double& coeff : a_) coeff /= a_[0];
    }
    
    reset();
}

void IIRFilter::reset() {
    state_b_.assign(b_.size(), 0.0);
    state_a_.assign(a_.size(), 0.0);
}

double IIRFilter::process(double input) {
    // Shift delay lines
    for (size_t i = state_b_.size() - 1; i > 0; --i) {
        state_b_[i] = state_b_[i-1];
    }
    state_b_[0] = input;
    
    for (size_t i = state_a_.size() - 1; i > 0; --i) {
        state_a_[i] = state_a_[i-1];
    }
    
    // Calculate output: y[n] = sum(b[k]*x[n-k]) - sum(a[k]*y[n-k]) for k>=1
    double output = 0.0;
    for (size_t i = 0; i < b_.size(); ++i) {
        output += b_[i] * state_b_[i];
    }
    for (size_t i = 1; i < a_.size(); ++i) {
        output -= a_[i] * state_a_[i];
    }
    
    state_a_[0] = output;
    return output;
}

std::vector<double> IIRFilter::process(const std::vector<double>& signal) {
    std::vector<double> result;
    result.reserve(signal.size());
    
    for (double x : signal) {
        result.push_back(process(x));
    }
    
    return result;
}

// BiquadFilter implementation
BiquadFilter::BiquadFilter(double b0, double b1, double b2, 
                            double a0, double a1, double a2)
    : b0_(b0/a0), b1_(b1/a0), b2_(b2/a0),
      a0_(1.0), a1_(a1/a0), a2_(a2/a0),
      x1_(0.0), x2_(0.0), y1_(0.0), y2_(0.0) {}

BiquadFilter::BiquadFilter(const BiquadCoefficients& coef)
    : BiquadFilter(coef.b0, coef.b1, coef.b2, coef.a0, coef.a1, coef.a2) {}

void BiquadFilter::reset() {
    x1_ = x2_ = y1_ = y2_ = 0.0;
}

double BiquadFilter::process(double input) {
    // Transposed Direct Form II
    double output = b0_ * input + x1_;
    x1_ = b1_ * input - a1_ * output + x2_;
    x2_ = b2_ * input - a2_ * output;
    
    return output;
}

std::vector<double> BiquadFilter::process(const std::vector<double>& signal) {
    std::vector<double> result;
    result.reserve(signal.size());
    
    for (double x : signal) {
        result.push_back(process(x));
    }
    
    return result;
}

// Filter design functions
namespace filter_design {

// Butterworth filter design using bilinear transform
IIRCoefficients butterworth(int order, 
                            double critical_freq,
                            double sample_rate,
                            const std::string& btype) {
    
    // Prewarp frequency for bilinear transform
    double T = 1.0 / sample_rate;
    double wc = 2.0 / T * std::tan(M_PI * critical_freq * T);
    
    // Generate Butterworth analog poles
    std::vector<std::complex<double>> poles;
    for (int k = 0; k < order; ++k) {
        double angle = M_PI * (2.0 * k + order + 1) / (2.0 * order);
        poles.push_back(std::complex<double>(wc * std::cos(angle), wc * std::sin(angle)));
    }
    
    // All zeros at infinity for lowpass
    std::vector<std::complex<double>> zeros;
    
    // Analog gain
    double gain = std::pow(wc, order);
    
    // Apply bilinear transform
    auto digital_coef = bilinear_transform(zeros, poles, gain, sample_rate);
    
    return digital_coef;
}

// Bilinear transform from analog to digital
IIRCoefficients bilinear_transform(const std::vector<std::complex<double>>& analog_zeros,
                                   const std::vector<std::complex<double>>& analog_poles,
                                   double gain,
                                   double sample_rate) {
    
    double T = 1.0 / sample_rate;
    double fs = 2.0 / T;
    
    // Transform zeros
    std::vector<std::complex<double>> digital_zeros;
    for (const auto& z : analog_zeros) {
        std::complex<double> num = fs + z;
        std::complex<double> den = fs - z;
        digital_zeros.push_back(num / den);
    }
    
    // Transform poles
    std::vector<std::complex<double>> digital_poles;
    for (const auto& p : analog_poles) {
        std::complex<double> num = fs + p;
        std::complex<double> den = fs - p;
        digital_poles.push_back(num / den);
    }
    
    // Convert poles/zeros to polynomial coefficients
    // This is a simplified version - full implementation needs polynomial expansion
    
    // For now, create a simple implementation
    std::vector<double> b = {gain};
    std::vector<double> a(analog_poles.size() + 1, 0.0);
    a[0] = 1.0;
    
    // Simple second-order approximation for demonstration
    // Full implementation would use proper polynomial expansion
    if (analog_poles.size() >= 2) {
        b = {gain, 2*gain, gain};  // Simplified numerator
        a = {1.0, 
             -2.0 * digital_poles[0].real(),
             std::norm(digital_poles[0])};
    }
    
    return {b, a};
}

// A-weighting filter design (IEC 61672-1:2013)
// Transfer function from analog domain:
// H(s) = K * s^4 / [(s + 129.4)^2 * (s + 676.7) * (s + 4636) * (s + 76617)^2]
// Where K is chosen for 0 dB gain at 1 kHz
std::vector<BiquadCoefficients> a_weighting_design(double sample_rate) {
    std::vector<BiquadCoefficients> sos;
    
    double T = 1.0 / sample_rate;
    double fs = 2.0 / T;  // 2*fs in bilinear transform
    
    // Analog poles and zeros for A-weighting
    // Zeros: 4 zeros at origin (s = 0)
    // Poles: -129.4 (double), -676.7, -4636, -76617 (double)
    
    // Bilinear transform coefficients
    // Each biquad section represents a pair of poles/zeros
    
    // Section 1: High-frequency poles at -76617 rad/s (double pole)
    {
        double wc = 76617.0;
        double K = std::tan(wc * T / 2.0);
        double a0 = 1.0 + 2*K + K*K;
        double a1 = 2.0 * (K*K - 1.0);
        double a2 = 1.0 - 2*K + K*K;
        
        // Numerator for differentiator-like behavior at high freq
        double b0 = 1.0;
        double b1 = -2.0;
        double b2 = 1.0;
        
        sos.push_back({b0, b1, b2, a0, a1, a2});
    }
    
    // Section 2: Mid-frequency poles at -4636 and -676.7 rad/s
    {
        double wc1 = 4636.0;
        double wc2 = 676.7;
        double K1 = std::tan(wc1 * T / 2.0);
        double K2 = std::tan(wc2 * T / 2.0);
        
        double a0 = 1.0 + K1 + K2 + K1*K2;
        double a1 = 2.0 * (K1*K2 - 1.0);
        double a2 = 1.0 - K1 - K2 + K1*K2;
        
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = -1.0;
        
        sos.push_back({b0, b1, b2, a0, a1, a2});
    }
    
    // Section 3: Low-frequency poles at -129.4 rad/s (double pole)
    {
        double wc = 129.4;
        double K = std::tan(wc * T / 2.0);
        double a0 = 1.0 + 2*K + K*K;
        double a1 = 2.0 * (K*K - 1.0);
        double a2 = 1.0 - 2*K + K*K;
        
        double b0 = 1.0;
        double b1 = -2.0;
        double b2 = 1.0;
        
        sos.push_back({b0, b1, b2, a0, a1, a2});
    }
    
    // Apply gain normalization for 0 dB at 1 kHz
    double target_gain_db = 0.0;
    double f_ref = 1000.0;
    
    // Calculate gain at 1 kHz and normalize
    double omega = 2.0 * M_PI * f_ref / sample_rate;
    std::complex<double> z(std::cos(omega), std::sin(omega));
    
    std::complex<double> H(1.0, 0.0);
    for (const auto& coef : sos) {
        std::complex<double> num = coef.b0 + coef.b1 * std::pow(z, -1.0) + coef.b2 * std::pow(z, -2.0);
        std::complex<double> den = coef.a0 + coef.a1 * std::pow(z, -1.0) + coef.a2 * std::pow(z, -2.0);
        H *= num / den;
    }
    
    double current_gain = std::abs(H);
    double norm_factor = std::pow(10.0, target_gain_db / 20.0) / current_gain;
    
    // Apply normalization to first section
    sos[0].b0 *= norm_factor;
    sos[0].b1 *= norm_factor;
    sos[0].b2 *= norm_factor;
    
    return sos;
}

// C-weighting filter design (IEC 61672-1:2013)
// Similar to A-weighting but with different pole locations
std::vector<BiquadCoefficients> c_weighting_design(double sample_rate) {
    std::vector<BiquadCoefficients> sos;
    
    double T = 1.0 / sample_rate;
    
    // C-weighting is nearly flat, only has high-frequency roll-off
    // Poles at -129.4 (double) and high frequency cutoff
    
    // Section 1: High-frequency shelf
    {
        double fc = 20000.0;  // Near Nyquist
        double wc = 2.0 * M_PI * fc;
        double K = std::tan(wc * T / 2.0);
        
        double a0 = 1.0 + K;
        double a1 = K - 1.0;
        double a2 = 0.0;
        
        double b0 = K;
        double b1 = K;
        double b2 = 0.0;
        
        sos.push_back({b0, b1, b2, a0, a1, a2});
    }
    
    // Section 2: Low-frequency poles (similar to A-weighting but less aggressive)
    {
        double wc = 129.4;
        double K = std::tan(wc * T / 2.0);
        double a0 = 1.0 + 2*K + K*K;
        double a1 = 2.0 * (K*K - 1.0);
        double a2 = 1.0 - 2*K + K*K;
        
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = -1.0;
        
        sos.push_back({b0, b1, b2, a0, a1, a2});
    }
    
    // Normalize gain at 1 kHz
    double omega = 2.0 * M_PI * 1000.0 / sample_rate;
    std::complex<double> z(std::cos(omega), std::sin(omega));
    
    std::complex<double> H(1.0, 0.0);
    for (const auto& coef : sos) {
        std::complex<double> num = coef.b0 + coef.b1 * std::pow(z, -1.0) + coef.b2 * std::pow(z, -2.0);
        std::complex<double> den = coef.a0 + coef.a1 * std::pow(z, -1.0) + coef.a2 * std::pow(z, -2.0);
        H *= num / den;
    }
    
    double current_gain = std::abs(H);
    double norm_factor = 1.0 / current_gain;  // Unity gain at 1 kHz
    
    sos[0].b0 *= norm_factor;
    sos[0].b1 *= norm_factor;
    sos[0].b2 *= norm_factor;
    
    return sos;
}

// Z-weighting is unity (flat response)
IIRCoefficients z_weighting_design(double sample_rate) {
    (void)sample_rate;  // Unused
    return {{1.0}, {1.0}};  // y[n] = x[n]
}

// Convert transfer function to second-order sections
std::vector<BiquadCoefficients> tf2sos(const std::vector<double>& b,
                                       const std::vector<double>& a) {
    // Simplified implementation
    // Full implementation would use proper polynomial factorization
    std::vector<BiquadCoefficients> sos;
    
    // For now, just return a single biquad if possible
    if (b.size() <= 3 && a.size() <= 3) {
        BiquadCoefficients coef;
        coef.b0 = b[0];
        coef.b1 = (b.size() > 1) ? b[1] : 0.0;
        coef.b2 = (b.size() > 2) ? b[2] : 0.0;
        coef.a0 = a[0];
        coef.a1 = (a.size() > 1) ? a[1] : 0.0;
        coef.a2 = (a.size() > 2) ? a[2] : 0.0;
        sos.push_back(coef);
    }
    
    return sos;
}

} // namespace filter_design

// Octave band filter design
namespace octave_filters {

// Design 1/3 octave band filter
IIRCoefficients design_third_octave(double center_freq, 
                                    double sample_rate,
                                    int order) {
    // 1/3 octave bandwidth factor
    double bandwidth = 0.231;  // log2(2^(1/3)) = 1/3 octave
    double f_lower = center_freq * std::pow(2.0, -bandwidth/2.0);
    double f_upper = center_freq * std::pow(2.0, bandwidth/2.0);
    
    // Design bandpass filter
    return filter_design::bandpass(f_lower, f_upper, sample_rate, order);
}

// Design octave band filter
IIRCoefficients design_octave(double center_freq,
                              double sample_rate,
                              int order) {
    // Full octave bandwidth
    double f_lower = center_freq / std::sqrt(2.0);
    double f_upper = center_freq * std::sqrt(2.0);
    
    return filter_design::bandpass(f_lower, f_upper, sample_rate, order);
}

// Bandpass filter design using Butterworth
IIRCoefficients bandpass(double low_freq, 
                         double high_freq,
                         double sample_rate,
                         int order) {
    // Normalize frequencies
    double nyquist = sample_rate / 2.0;
    double w_low = low_freq / nyquist;
    double w_high = high_freq / nyquist;
    
    // Clamp to valid range
    w_low = std::max(0.001, std::min(0.999, w_low));
    w_high = std::max(0.001, std::min(0.999, w_high));
    
    // Center frequency and bandwidth
    double w0 = 2.0 * M_PI * std::sqrt(low_freq * high_freq) / sample_rate;
    double bw = 2.0 * M_PI * (high_freq - low_freq) / sample_rate;
    
    // Simplified bandpass design
    // Full implementation would use proper bandpass transformation
    double Q = w0 / bw;
    
    // Use bilinear transform for bandpass
    double T = 1.0 / sample_rate;
    double wc = std::tan(w0 * T / 2.0);
    
    // Second-order bandpass coefficients
    double alpha = wc / Q;
    
    double b0 = alpha;
    double b1 = 0.0;
    double b2 = -alpha;
    double a0 = 1.0 + alpha + wc*wc;
    double a1 = 2.0 * (wc*wc - 1.0);
    double a2 = 1.0 - alpha + wc*wc;
    
    // Normalize
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0; a0 = 1.0;
    
    return {{b0, b1, b2}, {a0, a1, a2}};
}

// Get 1/3 octave center frequencies (ISO preferred numbers)
std::vector<double> third_octave_centers(double fmin, double fmax) {
    std::vector<double> centers;
    
    // ISO preferred 1/3 octave frequencies
    // Base frequency 1000 Hz, others are 1000 * 10^(n/10) where n is integer
    
    double f_base = 1000.0;
    double ratio = std::pow(10.0, 0.1);  // 10^(1/10)
    
    // Find starting n for fmin
    int n_min = static_cast<int>(std::ceil(10.0 * std::log10(fmin / f_base)));
    int n_max = static_cast<int>(std::floor(10.0 * std::log10(fmax / f_base)));
    
    for (int n = n_min; n <= n_max; ++n) {
        centers.push_back(f_base * std::pow(10.0, n / 10.0));
    }
    
    return centers;
}

// Get octave band center frequencies
std::vector<double> octave_centers(double fmin, double fmax) {
    std::vector<double> centers;
    
    // Standard octave bands: 31.5, 63, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
    double f = 31.5;
    while (f <= fmax * 1.01) {
        if (f >= fmin * 0.99) {
            centers.push_back(f);
        }
        f *= 2.0;
    }
    
    return centers;
}

} // namespace octave_filters

} // namespace noise_toolkit
