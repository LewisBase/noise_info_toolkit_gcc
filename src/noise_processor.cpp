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

constexpr float BAND_FACTOR = std::pow(2.0f, 1.0f / 6.0f);

constexpr std::array<float, 9> BAND_CENTER_FREQS = {
    63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

inline float db_to_power(float db) {
    if (db <= -100.0f) return 0.0f;
    return std::pow(10.0f, db / 10.0f);
}

inline float power_to_db(float power) {
    if (power <= 0.0f) return -INFINITY;
    return 10.0f * std::log10(power);
}

} // anonymous namespace

NoiseProcessor::NoiseProcessor(int sample_rate, float reference_pressure)
    : sample_rate_(sample_rate),
      reference_pressure_(reference_pressure) {
}

SecondMetrics NoiseProcessor::process_one_second(const float* buffer_start,
                                                  const float* buffer_end,
                                                  float processing_duration_s) noexcept {
    size_t n = buffer_end - buffer_start;
    size_t expected_n = static_cast<size_t>(sample_rate_ * processing_duration_s);

    if (n != expected_n || n == 0) {
        return SecondMetrics{};
    }

    SecondMetrics m;

    m.duration_s = processing_duration_s;
    m.n_samples = static_cast<int32_t>(n);

    float sum_x = 0.0f, sum_x2 = 0.0f, sum_x3 = 0.0f, sum_x4 = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float x = buffer_start[i];
        float x2 = x * x;
        sum_x += x;
        sum_x2 += x2;
        sum_x3 += x2 * x;
        sum_x4 += x2 * x2;
    }
    m.sum_x = sum_x;
    m.sum_x2 = sum_x2;
    m.sum_x3 = sum_x3;
    m.sum_x4 = sum_x4;

    m.beta_kurtosis = calculate_kurtosis_from_moments(m.n_samples, sum_x, sum_x2, sum_x3, sum_x4);

    std::vector<float> a_weighted = ::noise_toolkit::apply_a_weighting(
        std::vector<float>(buffer_start, buffer_end), static_cast<float>(sample_rate_));
    std::vector<float> c_weighted = ::noise_toolkit::apply_c_weighting(
        std::vector<float>(buffer_start, buffer_end), static_cast<float>(sample_rate_));

    auto calc_leq = [this](const float* data, size_t n_) -> float {
        if (n_ == 0) return -INFINITY;
        float sum_sq = 0.0f;
        for (size_t i = 0; i < n_; ++i) sum_sq += data[i] * data[i];
        float rms = std::sqrt(sum_sq / n_);
        if (rms <= 0) return -INFINITY;
        return 20.0f * std::log10(rms / reference_pressure_);
    };

    auto calc_peak = [this](const float* data, size_t n_) -> float {
        if (n_ == 0) return -INFINITY;
        float peak = 0.0f;
        for (size_t i = 0; i < n_; ++i) peak = std::max(peak, std::abs(data[i]));
        if (peak <= 0) return -INFINITY;
        return 20.0f * std::log10(peak / reference_pressure_);
    };

    m.LAeq = calc_leq(a_weighted.data(), a_weighted.size());
    m.LCeq = calc_leq(c_weighted.data(), c_weighted.size());
    m.LZeq = calc_leq(buffer_start, n);
    m.LZPeak = calc_peak(buffer_start, n);
    m.LCPeak = calc_peak(c_weighted.data(), c_weighted.size());

    m.LAFmax = m.LAeq + 3.0f;

    auto calc_kurtosis = [](const float* data, size_t n_) -> float {
        if (n_ < 4) return 3.0f;
        float mean = 0.0f;
        for (size_t i = 0; i < n_; ++i) mean += data[i];
        mean /= n_;
        float m2 = 0.0f, m4 = 0.0f;
        for (size_t i = 0; i < n_; ++i) {
            float d = data[i] - mean;
            float d2 = d * d;
            m2 += d2;
            m4 += d2 * d2;
        }
        m2 /= n_;
        m4 /= n_;
        if (m2 <= 0) return 3.0f;
        return m4 / (m2 * m2);
    };

    m.kurtosis_total = calc_kurtosis(buffer_start, n);
    m.kurtosis_a_weighted = calc_kurtosis(a_weighted.data(), a_weighted.size());
    m.kurtosis_c_weighted = calc_kurtosis(c_weighted.data(), c_weighted.size());

    if (m.LAeq > 0) {
        auto prof_n = dose_calculator_.get_profile(DoseStandard::NIOSH);
        auto prof_p = dose_calculator_.get_profile(DoseStandard::OSHA_PEL);
        auto prof_h = dose_calculator_.get_profile(DoseStandard::OSHA_HCA);
        auto prof_e = dose_calculator_.get_profile(DoseStandard::EU_ISO);

        m.dose_frac_niosh    = dose_calculator_.calculate_dose_increment(m.LAeq, processing_duration_s, prof_n) / 100.0f;
        m.dose_frac_osha_pel = dose_calculator_.calculate_dose_increment(m.LAeq, processing_duration_s, prof_p) / 100.0f;
        m.dose_frac_osha_hca = dose_calculator_.calculate_dose_increment(m.LAeq, processing_duration_s, prof_h) / 100.0f;
        m.dose_frac_eu_iso   = dose_calculator_.calculate_dose_increment(m.LAeq, processing_duration_s, prof_e) / 100.0f;
    }

    m.overload_flag = (m.LZPeak > OVERLOAD_THRESHOLD);
    m.underrange_flag = (m.LAeq < UNDERRANGE_THRESHOLD);
    m.wearing_state = (m.LAeq > 40.0f);

    FreqBandMoments band_moments[9] = {};

    for (int i = 0; i < 9; ++i) {
        float fc = BAND_CENTER_FREQS[i];
        float fc_low = fc / BAND_FACTOR;
        float fc_high = fc * BAND_FACTOR;

        auto coef = filter_design::bandpass(fc_low, fc_high, sample_rate_, 2);
        IIRFilter filter(coef.b, coef.a);
        std::vector<float> band_data = filter.process(std::vector<float>(buffer_start, buffer_end));

        float sum_sq_band = 0.0f;
        size_t bn = band_data.size();
        band_moments[i].n = static_cast<int32_t>(bn);

        for (size_t j = 0; j < bn; ++j) {
            float x = band_data[j];
            float x2 = x * x;
            sum_sq_band += x2;
            band_moments[i].s1 += x;
            band_moments[i].s2 += x2;
            band_moments[i].s3 += x2 * x;
            band_moments[i].s4 += x2 * x2;
        }

        float band_rms = std::sqrt(sum_sq_band / bn);
        float spl = (band_rms > 0) ? (20.0f * std::log10(band_rms / reference_pressure_)) : -INFINITY;

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

    for (int i = 0; i < 9; ++i) {
        *band_moments_ptr(m, i) = band_moments[i];
    }

    return m;
}

MinuteMetrics NoiseProcessor::aggregate_minute_metrics(const SecondMetrics* second_metrics,
                                                        int count,
                                                        float unit_duration_s) noexcept {
    MinuteMetrics result;
    if (count <= 0 || second_metrics == nullptr) return result;

    result.timestamp = second_metrics[0].timestamp;
    result.duration_s = 0.0f;
    result.valid_seconds = count;

    float sum_power_laeq = 0.0f, sum_power_lceq = 0.0f, sum_power_lzeq = 0.0f;
    result.LAFmax = -INFINITY;
    result.LZPeak = -INFINITY;

    int64_t total_n = 0;
    float total_s1 = 0.0f, total_s2 = 0.0f, total_s3 = 0.0f, total_s4 = 0.0f;

    FreqBandMoments agg_band_moments[9] = {};

    float band_power[9] = {0.0f};

    for (int i = 0; i < count; ++i) {
        const SecondMetrics& m = second_metrics[i];

        result.duration_s += m.duration_s;

        sum_power_laeq += db_to_power(m.LAeq);
        sum_power_lceq += db_to_power(m.LCeq);
        sum_power_lzeq += db_to_power(m.LZeq);

        result.LAFmax = std::max(result.LAFmax, m.LAFmax);
        result.LZPeak = std::max(result.LZPeak, m.LZPeak);

        result.dose_frac_niosh    += m.dose_frac_niosh;
        result.dose_frac_osha_pel += m.dose_frac_osha_pel;
        result.dose_frac_osha_hca += m.dose_frac_osha_hca;
        result.dose_frac_eu_iso   += m.dose_frac_eu_iso;

        if (m.overload_flag) result.overload_count++;
        if (m.underrange_flag) result.underrange_count++;

        if (m.n_samples > 0) {
            total_s1 += m.sum_x;
            total_s2 += m.sum_x2;
            total_s3 += m.sum_x3;
            total_s4 += m.sum_x4;
            total_n += m.n_samples;
        }

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

    float count_f = static_cast<float>(count);
    result.LAeq = power_to_db(sum_power_laeq / count_f);
    result.LCeq = power_to_db(sum_power_lceq / count_f);
    result.LZeq = power_to_db(sum_power_lzeq / count_f);

    result.freq_63hz_spl  = power_to_db(band_power[0] / count_f);
    result.freq_125hz_spl = power_to_db(band_power[1] / count_f);
    result.freq_250hz_spl = power_to_db(band_power[2] / count_f);
    result.freq_500hz_spl = power_to_db(band_power[3] / count_f);
    result.freq_1khz_spl  = power_to_db(band_power[4] / count_f);
    result.freq_2khz_spl  = power_to_db(band_power[5] / count_f);
    result.freq_4khz_spl  = power_to_db(band_power[6] / count_f);
    result.freq_8khz_spl  = power_to_db(band_power[7] / count_f);
    result.freq_16khz_spl = power_to_db(band_power[8] / count_f);

    result.n_samples = static_cast<int32_t>(total_n);
    result.sum_x = total_s1;
    result.sum_x2 = total_s2;
    result.sum_x3 = total_s3;
    result.sum_x4 = total_s4;

    if (total_n > 0) {
        result.kurtosis_total = calculate_kurtosis_from_moments(
            result.n_samples, total_s1, total_s2, total_s3, total_s4);
    }

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

void NoiseProcessor::calculate_band_moments(const float* data, size_t n,
                                             FreqBandMoments* out_moments) {
    for (int i = 0; i < 9; ++i) {
        float fc = BAND_CENTER_FREQS[i];
        float fc_low = fc / BAND_FACTOR;
        float fc_high = fc * BAND_FACTOR;

        auto coef = filter_design::bandpass(fc_low, fc_high, sample_rate_, 2);
        IIRFilter filter(coef.b, coef.a);
        std::vector<float> band_data = filter.process(std::vector<float>(data, data + n));

        size_t bn = band_data.size();
        out_moments[i].n = static_cast<int32_t>(bn);

        for (size_t j = 0; j < bn; ++j) {
            float x = band_data[j];
            float x2 = x * x;
            out_moments[i].s1 += x;
            out_moments[i].s2 += x2;
            out_moments[i].s3 += x2 * x;
            out_moments[i].s4 += x2 * x2;
        }
    }
}

} // namespace noise_toolkit