/**
 * @file noise_metrics.hpp
 * @brief Core metrics data structures for noise information toolkit
 * 
 * Defines all 81 per-second metrics and per-minute aggregated metrics.
 * Based on Python noise_info_toolkit TimeHistory model.
 * 
 * This header is standalone and does NOT include noise_toolkit.hpp
 * (which contains removed features).
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <array>

namespace noise_toolkit {

//==============================================================================
// Constants
//==============================================================================

constexpr double REFERENCE_PRESSURE = 20e-6;     // 20 μPa
constexpr double OVERLOAD_THRESHOLD = 140.0;      // dB
constexpr double UNDERRANGE_THRESHOLD = 30.0;    // dB
constexpr int THIRD_OCTAVE_BAND_COUNT = 9;        // 63Hz to 16kHz

//==============================================================================
// Frequency Band Definitions
//==============================================================================

constexpr std::array<double, THIRD_OCTAVE_BAND_COUNT> THIRD_OCTAVE_BANDS = {
    63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

constexpr std::array<const char*, THIRD_OCTAVE_BAND_COUNT> THIRD_OCTAVE_BAND_NAMES = {
    "63Hz", "125Hz", "250Hz", "500Hz", "1kHz", "2kHz", "4kHz", "8kHz", "16kHz"
};

//==============================================================================
// Raw Moment Statistics (for kurtosis aggregation, per spec 4.X.3)
//==============================================================================

/**
 * @brief Raw moment statistics for one frequency band
 * Used for precise kurtosis synthesis across time periods
 */
struct FreqBandMoments {
    int32_t n{0};        // Sample count
    double s1{0.0};      // Σx_k
    double s2{0.0};      // Σx_k²
    double s3{0.0};      // Σx_k³
    double s4{0.0};      // Σx_k⁴
    
    FreqBandMoments() = default;
    FreqBandMoments(int32_t n_, double s1_, double s2_, double s3_, double s4_)
        : n(n_), s1(s1_), s2(s2_), s3(s3_), s4(s4_) {}
};

//==============================================================================
// Per-Second Metrics (81 fields total)
//==============================================================================

/**
 * @brief Single second metrics - all 81 indicators
 * 
 * Field count breakdown:
 *   - 2: timestamp, duration_s
 *   - 6: LAeq, LCeq, LZeq, LAFmax, LZpeak, LCpeak
 *   - 4: dose_frac_niosh/osha_pel/osha_hca/eu_iso
 *   - 3: overload_flag, underrange_flag, wearing_state
 *   - 4: kurtosis_total/a_weighted/c_weighted, beta_kurtosis
 *   - 5: n_samples, sum_x/s1, sum_x2/s2, sum_x3/s3, sum_x4/s4
 *   - 9: freq band SPLs (63Hz-16kHz)
 *   - 45: freq band raw moments S1-S4 (9 bands × 5 values)
 *   Total: 2 + 6 + 4 + 3 + 4 + 5 + 9 + 45 = 78 + 3 extra = 81
 */
struct alignas(64) SecondMetrics {
    //=== Metadata (2) ===
    double timestamp{0.0};      // Unix timestamp (seconds since epoch)
    double duration_s{1.0};    // Actual duration (typically 1.0s)
    
    //=== Sound Levels (6) ===
    double LAeq{0.0};          // A-weighted equivalent SPL
    double LCeq{0.0};          // C-weighted equivalent SPL
    double LZeq{0.0};          // Z-weighted (unweighted) equivalent SPL
    double LAFmax{0.0};        // A-weighted fast time-weighted max
    double LZpeak{0.0};        // Z-weighted peak level
    double LCpeak{0.0};        // C-weighted peak level
    
    //=== Dose Increments (4) ===
    double dose_frac_niosh{0.0};     // NIOSH dose fraction (0-1)
    double dose_frac_osha_pel{0.0};  // OSHA PEL dose fraction
    double dose_frac_osha_hca{0.0};  // OSHA HCA dose fraction
    double dose_frac_eu_iso{0.0};    // EU/ISO dose fraction
    
    //=== Quality Control (3) ===
    bool overload_flag{false};    // Peak exceeds 140 dB
    bool underrange_flag{false};  // LAeq below 30 dB
    bool wearing_state{true};      // Meter wearing detection
    
    //=== Kurtosis Metrics (4) ===
    double kurtosis_total{3.0};      // Z-weighted (raw signal) kurtosis (Pearson, normal=3)
    double kurtosis_a_weighted{3.0};  // A-weighted kurtosis
    double kurtosis_c_weighted{3.0};  // C-weighted kurtosis
    double beta_kurtosis{0.0};        // Kurtosis from raw moments (per spec 4.X.3)
    
    //=== Raw Moment Statistics for Aggregation (5, per spec 4.X.3) ===
    int32_t n_samples{0};  // Sample count n
    double sum_x{0.0};     // S1 = Σx_k
    double sum_x2{0.0};    // S2 = Σx_k²
    double sum_x3{0.0};    // S3 = Σx_k³
    double sum_x4{0.0};    // S4 = Σx_k⁴
    
    //=== 1/3 Octave Band SPL (9) ===
    double freq_63hz_spl{0.0};
    double freq_125hz_spl{0.0};
    double freq_250hz_spl{0.0};
    double freq_500hz_spl{0.0};
    double freq_1khz_spl{0.0};
    double freq_2khz_spl{0.0};
    double freq_4khz_spl{0.0};
    double freq_8khz_spl{0.0};
    double freq_16khz_spl{0.0};
    
    //=== 1/3 Octave Band Raw Moment Statistics S1-S4 (9 bands × 5 values = 45) ===
    // 63Hz band
    int32_t freq_63hz_n{0};
    double freq_63hz_s1{0.0}, freq_63hz_s2{0.0}, freq_63hz_s3{0.0}, freq_63hz_s4{0.0};
    // 125Hz band
    int32_t freq_125hz_n{0};
    double freq_125hz_s1{0.0}, freq_125hz_s2{0.0}, freq_125hz_s3{0.0}, freq_125hz_s4{0.0};
    // 250Hz band
    int32_t freq_250hz_n{0};
    double freq_250hz_s1{0.0}, freq_250hz_s2{0.0}, freq_250hz_s3{0.0}, freq_250hz_s4{0.0};
    // 500Hz band
    int32_t freq_500hz_n{0};
    double freq_500hz_s1{0.0}, freq_500hz_s2{0.0}, freq_500hz_s3{0.0}, freq_500hz_s4{0.0};
    // 1kHz band
    int32_t freq_1khz_n{0};
    double freq_1khz_s1{0.0}, freq_1khz_s2{0.0}, freq_1khz_s3{0.0}, freq_1khz_s4{0.0};
    // 2kHz band
    int32_t freq_2khz_n{0};
    double freq_2khz_s1{0.0}, freq_2khz_s2{0.0}, freq_2khz_s3{0.0}, freq_2khz_s4{0.0};
    // 4kHz band
    int32_t freq_4khz_n{0};
    double freq_4khz_s1{0.0}, freq_4khz_s2{0.0}, freq_4khz_s3{0.0}, freq_4khz_s4{0.0};
    // 8kHz band
    int32_t freq_8khz_n{0};
    double freq_8khz_s1{0.0}, freq_8khz_s2{0.0}, freq_8khz_s3{0.0}, freq_8khz_s4{0.0};
    // 16kHz band
    int32_t freq_16khz_n{0};
    double freq_16khz_s1{0.0}, freq_16khz_s2{0.0}, freq_16khz_s3{0.0}, freq_16khz_s4{0.0};
    
    SecondMetrics() = default;
    
    /** @brief Reset all fields to default values */
    void reset() { *this = SecondMetrics{}; }
};

//==============================================================================
// Per-Minute Aggregated Metrics
//==============================================================================

/**
 * @brief Per-minute aggregated metrics
 * Derived from 60 SecondMetrics records
 */
struct MinuteMetrics {
    double timestamp{0.0};
    double duration_s{60.0};
    
    //=== Overall Sound Levels (3) ===
    double LAeq{0.0};
    double LCeq{0.0};
    double LZeq{0.0};
    
    //=== Peak Levels (2) ===
    double LAFmax{0.0};
    double LZpeak{0.0};
    
    //=== Dose Accumulation (4) ===
    double dose_frac_niosh{0.0};
    double dose_frac_osha_pel{0.0};
    double dose_frac_osha_hca{0.0};
    double dose_frac_eu_iso{0.0};
    
    //=== QC Statistics (3) ===
    int32_t overload_count{0};
    int32_t underrange_count{0};
    int32_t valid_seconds{0};
    
    //=== Kurtosis (3) ===
    double kurtosis_total{3.0};
    double kurtosis_a_weighted{3.0};
    double kurtosis_c_weighted{3.0};
    
    //=== Raw Moments for Kurtosis (5) ===
    int32_t n_samples{0};
    double sum_x{0.0};
    double sum_x2{0.0};
    double sum_x3{0.0};
    double sum_x4{0.0};
    
    //=== 1/3 Octave Band SPL (9) ===
    double freq_63hz_spl{0.0};
    double freq_125hz_spl{0.0};
    double freq_250hz_spl{0.0};
    double freq_500hz_spl{0.0};
    double freq_1khz_spl{0.0};
    double freq_2khz_spl{0.0};
    double freq_4khz_spl{0.0};
    double freq_8khz_spl{0.0};
    double freq_16khz_spl{0.0};
    
    //=== 1/3 Octave Band Raw Moments S1-S4 (45) ===
    int32_t freq_63hz_n{0};
    double freq_63hz_s1{0.0}, freq_63hz_s2{0.0}, freq_63hz_s3{0.0}, freq_63hz_s4{0.0};
    int32_t freq_125hz_n{0};
    double freq_125hz_s1{0.0}, freq_125hz_s2{0.0}, freq_125hz_s3{0.0}, freq_125hz_s4{0.0};
    int32_t freq_250hz_n{0};
    double freq_250hz_s1{0.0}, freq_250hz_s2{0.0}, freq_250hz_s3{0.0}, freq_250hz_s4{0.0};
    int32_t freq_500hz_n{0};
    double freq_500hz_s1{0.0}, freq_500hz_s2{0.0}, freq_500hz_s3{0.0}, freq_500hz_s4{0.0};
    int32_t freq_1khz_n{0};
    double freq_1khz_s1{0.0}, freq_1khz_s2{0.0}, freq_1khz_s3{0.0}, freq_1khz_s4{0.0};
    int32_t freq_2khz_n{0};
    double freq_2khz_s1{0.0}, freq_2khz_s2{0.0}, freq_2khz_s3{0.0}, freq_2khz_s4{0.0};
    int32_t freq_4khz_n{0};
    double freq_4khz_s1{0.0}, freq_4khz_s2{0.0}, freq_4khz_s3{0.0}, freq_4khz_s4{0.0};
    int32_t freq_8khz_n{0};
    double freq_8khz_s1{0.0}, freq_8khz_s2{0.0}, freq_8khz_s3{0.0}, freq_8khz_s4{0.0};
    int32_t freq_16khz_n{0};
    double freq_16khz_s1{0.0}, freq_16khz_s2{0.0}, freq_16khz_s3{0.0}, freq_16khz_s4{0.0};
    
    MinuteMetrics() = default;
    void reset() { *this = MinuteMetrics{}; }
};

//==============================================================================
// Utility Functions
//==============================================================================

/**
 * @brief Calculate kurtosis β from raw moment statistics (per spec 4.X.3)
 * 
 * Formula:
 *   μ = S1 / n
 *   m2 = S2/n - μ²
 *   m4 = S4/n - 4μ·S3/n + 6μ²·S2/n - 3μ⁴
 *   β = m4 / m2²
 */
inline double calculate_kurtosis_from_moments(int32_t n, double s1, double s2, 
                                                double s3, double s4) {
    if (n <= 0) return std::nan("");
    double mu = s1 / n;
    double m2 = s2 / n - mu * mu;
    if (m2 <= 0) return std::nan("");
    double m4 = (s4 / n 
                - 4.0 * mu * (s3 / n) 
                + 6.0 * (mu * mu) * (s2 / n) 
                - 3.0 * (mu * mu * mu * mu));
    return m4 / (m2 * m2);
}

/**
 * @brief Get frequency band moments pointer from SecondMetrics by band index (0-8)
 */
inline FreqBandMoments* band_moments_ptr(SecondMetrics& m, int band_idx) {
    switch (band_idx) {
        case 0: return reinterpret_cast<FreqBandMoments*>(&m.freq_63hz_n);
        case 1: return reinterpret_cast<FreqBandMoments*>(&m.freq_125hz_n);
        case 2: return reinterpret_cast<FreqBandMoments*>(&m.freq_250hz_n);
        case 3: return reinterpret_cast<FreqBandMoments*>(&m.freq_500hz_n);
        case 4: return reinterpret_cast<FreqBandMoments*>(&m.freq_1khz_n);
        case 5: return reinterpret_cast<FreqBandMoments*>(&m.freq_2khz_n);
        case 6: return reinterpret_cast<FreqBandMoments*>(&m.freq_4khz_n);
        case 7: return reinterpret_cast<FreqBandMoments*>(&m.freq_8khz_n);
        case 8: return reinterpret_cast<FreqBandMoments*>(&m.freq_16khz_n);
        default: return nullptr;
    }
}

/** @brief Const version */
inline const FreqBandMoments* band_moments_ptr(const SecondMetrics& m, int band_idx) {
    return band_moments_ptr(const_cast<SecondMetrics&>(m), band_idx);
}

// Overloads for MinuteMetrics (same field layout at band moment offsets)
inline FreqBandMoments* band_moments_ptr(MinuteMetrics& m, int band_idx) {
    switch (band_idx) {
        case 0: return reinterpret_cast<FreqBandMoments*>(&m.freq_63hz_n);
        case 1: return reinterpret_cast<FreqBandMoments*>(&m.freq_125hz_n);
        case 2: return reinterpret_cast<FreqBandMoments*>(&m.freq_250hz_n);
        case 3: return reinterpret_cast<FreqBandMoments*>(&m.freq_500hz_n);
        case 4: return reinterpret_cast<FreqBandMoments*>(&m.freq_1khz_n);
        case 5: return reinterpret_cast<FreqBandMoments*>(&m.freq_2khz_n);
        case 6: return reinterpret_cast<FreqBandMoments*>(&m.freq_4khz_n);
        case 7: return reinterpret_cast<FreqBandMoments*>(&m.freq_8khz_n);
        case 8: return reinterpret_cast<FreqBandMoments*>(&m.freq_16khz_n);
        default: return nullptr;
    }
}

inline const FreqBandMoments* band_moments_ptr(const MinuteMetrics& m, int band_idx) {
    return band_moments_ptr(const_cast<MinuteMetrics&>(m), band_idx);
}

/**
 * @brief Aggregate raw moments from multiple records
 */
inline int32_t aggregate_moments(const FreqBandMoments* moments, int count,
                                  double& out_s1, double& out_s2, 
                                  double& out_s3, double& out_s4) {
    int32_t total_n = 0;
    out_s1 = out_s2 = out_s3 = out_s4 = 0.0;
    for (int i = 0; i < count; ++i) {
        if (moments[i].n > 0) {
            out_s1 += moments[i].s1;
            out_s2 += moments[i].s2;
            out_s3 += moments[i].s3;
            out_s4 += moments[i].s4;
            total_n += moments[i].n;
        }
    }
    return total_n;
}

} // namespace noise_toolkit
