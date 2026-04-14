/**
 * @file noise_processor.cpp
 * @brief NoiseProcessor implementation
 */

#include "noise_processor.hpp"
#include "signal_utils.hpp"
#include "iir_filter.hpp"
#include <algorithm>
#include <vector>
#include <cmath>

namespace noise_toolkit {

namespace {

// Frequency boundaries for 1/3 octave bands
// Nominal bandwidth: fc * 2^(-1/6) to fc * 2^(1/6)
constexpr double BAND_FACTOR = std::pow(2.0, 1.0 / 6.0);

constexpr std::array<double, 9> BAND_CENTER_FREQS = {
    63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

// Helper: convert dB to linear power
inline double db_to_power(double db) {
    if (db <= -100.0) return 0.0;
    return std::pow(10.0, db / 10.0);
}

// Helper: convert linear power to dB
inline double power_to_db(double power) {
    if (power <= 0.0) return -INFINITY;
    return 10.0 * std::log10(power);
}

} // anonymous namespace

NoiseProcessor::NoiseProcessor(int sample_rate, double reference_pressure)
    : sample_rate_(sample_rate),
      reference_pressure_(reference_pressure) {
}

//------------------------------------------------------------------------------
// Per-Second Processing
//------------------------------------------------------------------------------

SecondMetrics NoiseProcessor::process_one_second(const float* buffer_start,
                                                  const float* buffer_end) noexcept {
    size_t n = buffer_end - buffer_start;
    if (n == 0) return SecondMetrics{};
    
    std::vector<double> data(n);
    for (size_t i = 0; i < n; ++i) {
        data[i] = static_cast<double>(buffer_start[i]);
    }
    
    return process_one_second(data.data(), data.data() + n);
}

SecondMetrics NoiseProcessor::process_one_second(const double* buffer_start,
                                                  const double* buffer_end) noexcept {
    SecondMetrics m;
    size_t n = buffer_end - buffer_start;
    if (n == 0) return m;
    
    m.duration_s = static_cast<double>(n) / sample_rate_;
    if (m.duration_s <= 0) m.duration_s = 1.0;
    
    // --- Raw moments S1-S4 from Z-weighted (raw) signal (per spec 4.X.3) ---
    m.n_samples = static_cast<int32_t>(n);
    double sum_x = 0.0, sum_x2 = 0.0, sum_x3 = 0.0, sum_x4 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double x = buffer_start[i];
        double x2 = x * x;
        sum_x += x;
        sum_x2 += x2;
        sum_x3 += x2 * x;
        sum_x4 += x2 * x2;
    }
    m.sum_x = sum_x;
    m.sum_x2 = sum_x2;
    m.sum_x3 = sum_x3;
    m.sum_x4 = sum_x4;
    
    // Beta kurtosis from raw moments (spec 4.X.3)
    m.beta_kurtosis = calculate_kurtosis_from_moments(m.n_samples, sum_x, sum_x2, sum_x3, sum_x4);
    
    // --- A and C weighting ---
    std::vector<double> a_weighted = ::noise_toolkit::apply_a_weighting(
        std::vector<double>(buffer_start, buffer_end), static_cast<double>(sample_rate_));
    std::vector<double> c_weighted = ::noise_toolkit::apply_c_weighting(
        std::vector<double>(buffer_start, buffer_end), static_cast<double>(sample_rate_));
    
    // --- SPL calculation helper ---
    auto calc_leq = [this](const double* data, size_t n_) -> double {
        if (n_ == 0) return -INFINITY;
        double sum_sq = 0.0;
        for (size_t i = 0; i < n_; ++i) sum_sq += data[i] * data[i];
        double rms = std::sqrt(sum_sq / n_);
        if (rms <= 0) return -INFINITY;
        return 20.0 * std::log10(rms / reference_pressure_);
    };
    
    // --- Peak calculation helper ---
    auto calc_peak = [this](const double* data, size_t n_) -> double {
        if (n_ == 0) return -INFINITY;
        double peak = 0.0;
        for (size_t i = 0; i < n_; ++i) peak = std::max(peak, std::abs(data[i]));
        if (peak <= 0) return -INFINITY;
        return 20.0 * std::log10(peak / reference_pressure_);
    };
    
    // --- Calculate levels ---
    m.LAeq = calc_leq(a_weighted.data(), a_weighted.size());
    m.LCeq = calc_leq(c_weighted.data(), c_weighted.size());
    m.LZeq = calc_leq(buffer_start, n);
    m.LZpeak = calc_peak(buffer_start, n);
    m.LCpeak = calc_peak(c_weighted.data(), c_weighted.size());
    
    // LAFmax: fast time-weighted (125ms) max - approximation: LAeq + 3 dB
    m.LAFmax = m.LAeq + 3.0;
    
    // --- Kurtosis (Pearson, normal=3) ---
    auto calc_kurtosis = [](const double* data, size_t n_) -> double {
        if (n_ < 4) return 3.0;
        double mean = 0.0;
        for (size_t i = 0; i < n_; ++i) mean += data[i];
        mean /= n_;
        double m2 = 0.0, m4 = 0.0;
        for (size_t i = 0; i < n_; ++i) {
            double d = data[i] - mean;
            double d2 = d * d;
            m2 += d2;
            m4 += d2 * d2;
        }
        m2 /= n_;
        m4 /= n_;
        if (m2 <= 0) return 3.0;
        return m4 / (m2 * m2);
    };
    
    m.kurtosis_total = calc_kurtosis(buffer_start, n);
    m.kurtosis_a_weighted = calc_kurtosis(a_weighted.data(), a_weighted.size());
    m.kurtosis_c_weighted = calc_kurtosis(c_weighted.data(), c_weighted.size());
    
    // --- Dose increments (per-second fractions) ---
    if (m.LAeq > 0) {
        auto prof_n = dose_calculator_.get_profile(DoseStandard::NIOSH);
        auto prof_p = dose_calculator_.get_profile(DoseStandard::OSHA_PEL);
        auto prof_h = dose_calculator_.get_profile(DoseStandard::OSHA_HCA);
        auto prof_e = dose_calculator_.get_profile(DoseStandard::EU_ISO);
        
        m.dose_frac_niosh   = dose_calculator_.calculate_dose_increment(m.LAeq, 1.0, prof_n) / 100.0;
        m.dose_frac_osha_pel = dose_calculator_.calculate_dose_increment(m.LAeq, 1.0, prof_p) / 100.0;
        m.dose_frac_osha_hca = dose_calculator_.calculate_dose_increment(m.LAeq, 1.0, prof_h) / 100.0;
        m.dose_frac_eu_iso  = dose_calculator_.calculate_dose_increment(m.LAeq, 1.0, prof_e) / 100.0;
    }
    
    // --- QC flags ---
    m.overload_flag = (m.LZpeak > OVERLOAD_THRESHOLD);
    m.underrange_flag = (m.LAeq < UNDERRANGE_THRESHOLD);
    m.wearing_state = (m.LAeq > 40.0);
    
    // --- 1/3 octave band analysis ---
    FreqBandMoments band_moments[9] = {};
    
    for (int i = 0; i < 9; ++i) {
        double fc = BAND_CENTER_FREQS[i];
        double fc_low = fc / BAND_FACTOR;
        double fc_high = fc * BAND_FACTOR;
        
        // Bandpass filter for SPL and moments
        auto coef = filter_design::bandpass(fc_low, fc_high, sample_rate_, 2);
        IIRFilter filter(coef.b, coef.a);
        std::vector<double> band_data = filter.process(std::vector<double>(buffer_start, buffer_end));
        
        // SPL: RMS in dB
        double sum_sq_band = 0.0;
        size_t bn = band_data.size();
        band_moments[i].n = static_cast<int32_t>(bn);
        
        for (size_t j = 0; j < bn; ++j) {
            double x = band_data[j];
            double x2 = x * x;
            sum_sq_band += x2;
            band_moments[i].s1 += x;
            band_moments[i].s2 += x2;
            band_moments[i].s3 += x2 * x;
            band_moments[i].s4 += x2 * x2;
        }
        
        double band_rms = std::sqrt(sum_sq_band / bn);
        double spl = (band_rms > 0) ? (20.0 * std::log10(band_rms / reference_pressure_)) : -INFINITY;
        
        switch (i) {
            case 0: m.freq_63hz_spl = spl; break;
            case 1: m.freq_125hz_spl = spl; break;
            case 2: m.freq_250hz_spl = spl; break;
            case 3: m.freq_500hz_spl = spl; break;
            case 4: m.freq_1khz_spl = spl; break;
            case 5: m.freq_2khz_spl = spl; break;
            case 6: m.freq_4khz_spl = spl; break;
            case 7: m.freq_8khz_spl = spl; break;
            case 8: m.freq_16khz_spl = spl; break;
        }
    }
    
    // Write band moments to SecondMetrics
    for (int i = 0; i < 9; ++i) {
        *band_moments_ptr(m, i) = band_moments[i];
    }
    
    return m;
}

//------------------------------------------------------------------------------
// Per-Minute Aggregation
//------------------------------------------------------------------------------

MinuteMetrics NoiseProcessor::aggregate_minute_metrics(const SecondMetrics* second_metrics,
                                                        int count) noexcept {
    MinuteMetrics result;
    if (count <= 0 || second_metrics == nullptr) return result;
    
    result.timestamp = second_metrics[0].timestamp;
    result.duration_s = 0.0;
    result.valid_seconds = count;
    
    double sum_power_laeq = 0.0, sum_power_lceq = 0.0, sum_power_lzeq = 0.0;
    result.LAFmax = -INFINITY;
    result.LZpeak = -INFINITY;
    
    // Aggregate raw moments
    int64_t total_n = 0;
    double total_s1 = 0.0, total_s2 = 0.0, total_s3 = 0.0, total_s4 = 0.0;
    
    // Frequency band moments aggregation
    FreqBandMoments agg_band_moments[9] = {};
    
    // Frequency band SPL energy accumulation
    double band_power[9] = {0.0};
    
    for (int i = 0; i < count; ++i) {
        const SecondMetrics& m = second_metrics[i];
        
        result.duration_s += m.duration_s;
        
        sum_power_laeq += db_to_power(m.LAeq);
        sum_power_lceq += db_to_power(m.LCeq);
        sum_power_lzeq += db_to_power(m.LZeq);
        
        result.LAFmax = std::max(result.LAFmax, m.LAFmax);
        result.LZpeak = std::max(result.LZpeak, m.LZpeak);
        
        result.dose_frac_niosh   += m.dose_frac_niosh;
        result.dose_frac_osha_pel += m.dose_frac_osha_pel;
        result.dose_frac_osha_hca += m.dose_frac_osha_hca;
        result.dose_frac_eu_iso  += m.dose_frac_eu_iso;
        
        if (m.overload_flag) result.overload_count++;
        if (m.underrange_flag) result.underrange_count++;
        
        // Aggregate raw moments
        if (m.n_samples > 0) {
            total_s1 += m.sum_x;
            total_s2 += m.sum_x2;
            total_s3 += m.sum_x3;
            total_s4 += m.sum_x4;
            total_n += m.n_samples;
        }
        
        // Aggregate frequency band moments
        for (int b = 0; b < 9; ++b) {
            const FreqBandMoments* mp = band_moments_ptr(m, b);
            if (mp && mp->n > 0) {
                agg_band_moments[b].n += mp->n;
                agg_band_moments[b].s1 += mp->s1;
                agg_band_moments[b].s2 += mp->s2;
                agg_band_moments[b].s3 += mp->s3;
                agg_band_moments[b].s4 += mp->s4;
            }
        }
        
        // Accumulate band SPL for averaging
        band_power[0] += db_to_power(m.freq_63hz_spl);
        band_power[1] += db_to_power(m.freq_125hz_spl);
        band_power[2] += db_to_power(m.freq_250hz_spl);
        band_power[3] += db_to_power(m.freq_500hz_spl);
        band_power[4] += db_to_power(m.freq_1khz_spl);
        band_power[5] += db_to_power(m.freq_2khz_spl);
        band_power[6] += db_to_power(m.freq_4khz_spl);
        band_power[7] += db_to_power(m.freq_8khz_spl);
        band_power[8] += db_to_power(m.freq_16khz_spl);
    }
    
    // Calculate overall Leq from energy sum
    result.LAeq = power_to_db(sum_power_laeq / count);
    result.LCeq = power_to_db(sum_power_lceq / count);
    result.LZeq = power_to_db(sum_power_lzeq / count);
    
    // Average frequency band SPLs
    result.freq_63hz_spl  = power_to_db(band_power[0] / count);
    result.freq_125hz_spl = power_to_db(band_power[1] / count);
    result.freq_250hz_spl = power_to_db(band_power[2] / count);
    result.freq_500hz_spl = power_to_db(band_power[3] / count);
    result.freq_1khz_spl  = power_to_db(band_power[4] / count);
    result.freq_2khz_spl  = power_to_db(band_power[5] / count);
    result.freq_4khz_spl  = power_to_db(band_power[6] / count);
    result.freq_8khz_spl  = power_to_db(band_power[7] / count);
    result.freq_16khz_spl = power_to_db(band_power[8] / count);
    
    // Aggregate kurtosis from moments
    result.n_samples = static_cast<int32_t>(total_n);
    result.sum_x = total_s1;
    result.sum_x2 = total_s2;
    result.sum_x3 = total_s3;
    result.sum_x4 = total_s4;
    
    if (total_n > 0) {
        result.kurtosis_total = calculate_kurtosis_from_moments(
            result.n_samples, total_s1, total_s2, total_s3, total_s4);
    }
    
    // Write aggregated band moments to MinuteMetrics
    *band_moments_ptr(result, 0) = agg_band_moments[0];
    *band_moments_ptr(result, 1) = agg_band_moments[1];
    *band_moments_ptr(result, 2) = agg_band_moments[2];
    *band_moments_ptr(result, 3) = agg_band_moments[3];
    *band_moments_ptr(result, 4) = agg_band_moments[4];
    *band_moments_ptr(result, 5) = agg_band_moments[5];
    *band_moments_ptr(result, 6) = agg_band_moments[6];
    *band_moments_ptr(result, 7) = agg_band_moments[7];
    *band_moments_ptr(result, 8) = agg_band_moments[8];
    
    return result;
}

//------------------------------------------------------------------------------
// Private Methods
//------------------------------------------------------------------------------

void NoiseProcessor::calculate_band_moments(const double* data, size_t n,
                                             FreqBandMoments* out_moments) {
    for (int i = 0; i < 9; ++i) {
        double fc = BAND_CENTER_FREQS[i];
        double fc_low = fc / BAND_FACTOR;
        double fc_high = fc * BAND_FACTOR;
        
        auto coef = filter_design::bandpass(fc_low, fc_high, sample_rate_, 2);
        IIRFilter filter(coef.b, coef.a);
        std::vector<double> band_data = filter.process(std::vector<double>(data, data + n));
        
        size_t bn = band_data.size();
        out_moments[i].n = static_cast<int32_t>(bn);
        
        for (size_t j = 0; j < bn; ++j) {
            double x = band_data[j];
            double x2 = x * x;
            out_moments[i].s1 += x;
            out_moments[i].s2 += x2;
            out_moments[i].s3 += x2 * x;
            out_moments[i].s4 += x2 * x2;
        }
    }
}

} // namespace noise_toolkit
