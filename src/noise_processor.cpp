/**
 * @file noise_processor.cpp
 * @brief NoiseProcessor implementation — v3.1 streaming architecture
 *
 * Zero heap allocation in hot path. A/C weighting and bandpass filters
 * process each sample in-place. Filter state persists across calls.
 */

#include "noise_processor.hpp"
#include "signal_utils.hpp"
#include "iir_filter.hpp"
#include "math_constants.hpp"
#include "weighting_coefficients_multirate.hpp"
#include <algorithm>
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

    // Initialize A/C weighting filter chains
    // === v3.2 Bug B fix: use pre-computed table for 7 supported sample rates
    // (8k/16k/22.05k/32k/44.1k/48k/96k); unknown rates fall back to a/c_weighting_design()
    // === v3.2.1 Bug fix: 1kHz normalization is now a SEPARATE FACTOR (entry->a_gain / c_gain)
    // applied AFTER the biquad chain, not baked into sos[0].b (which distorted high-f response).
    const auto* entry = find_weighting_entry(sample_rate_);
    if (entry != nullptr) {
        // Table hit: install pre-computed coefficients (zero runtime cost)
        for (int i = 0; i < entry->a_count && i < A_WEIGHTING_SECTIONS; ++i) {
            a_weight_chain_.sections[i].b0 = entry->a[i].b0;
            a_weight_chain_.sections[i].b1 = entry->a[i].b1;
            a_weight_chain_.sections[i].b2 = entry->a[i].b2;
            a_weight_chain_.sections[i].a0 = 1.0f;
            a_weight_chain_.sections[i].a1 = entry->a[i].a1;
            a_weight_chain_.sections[i].a2 = entry->a[i].a2;
        }
        for (int i = 0; i < entry->c_count && i < C_WEIGHTING_SECTIONS; ++i) {
            c_weight_chain_.sections[i].b0 = entry->c[i].b0;
            c_weight_chain_.sections[i].b1 = entry->c[i].b1;
            c_weight_chain_.sections[i].b2 = entry->c[i].b2;
            c_weight_chain_.sections[i].a0 = 1.0f;
            c_weight_chain_.sections[i].a1 = entry->c[i].a1;
            c_weight_chain_.sections[i].a2 = entry->c[i].a2;
        }
        // v3.2.1: store 1kHz gain factor separately (applied post-filter)
        a_weight_gain_ = entry->a_gain;
        c_weight_gain_ = entry->c_gain;
    } else {
        // Fallback: runtime design (方案 A，pre-warp 修正回退)
        // v3.2.1: dynamic path also uses gain factor separation
        float gain_a = 1.0f, gain_c = 1.0f;
        auto sos_a = filter_design::a_weighting_design(static_cast<float>(sample_rate_), &gain_a);
        for (size_t i = 0; i < A_WEIGHTING_SECTIONS && i < sos_a.size(); ++i) {
            a_weight_chain_.sections[i] = sos_a[i];
        }
        auto sos_c = filter_design::c_weighting_design(static_cast<float>(sample_rate_), &gain_c);
        for (size_t i = 0; i < C_WEIGHTING_SECTIONS && i < sos_c.size(); ++i) {
            c_weight_chain_.sections[i] = sos_c[i];
        }
        a_weight_gain_ = gain_a;
        c_weight_gain_ = gain_c;
    }
    a_weight_chain_.reset();
    c_weight_chain_.reset();

    // Initialize 9 bandpass filters from pre-computed coefficients (48kHz)
    // or compute at runtime for other sample rates
    if (sample_rate_ == 48000) {
        for (int i = 0; i < 9; ++i) {
            const auto& c = BANDPASS_COEFFS_48K[i];
            band_filters_[i] = BiquadFilter(c.b0, c.b1, c.b2, 1.0f, c.a1, c.a2);
        }
    } else {
        for (int i = 0; i < 9; ++i) {
            float fc = BAND_CENTER_FREQS[i];
            float fc_low = fc / BAND_FACTOR;
            float fc_high = fc * BAND_FACTOR;
            auto coef = filter_design::bandpass(fc_low, fc_high,
                                                 static_cast<float>(sample_rate_), 2);
            if (coef.b.size() >= 3 && coef.a.size() >= 3) {
                band_filters_[i] = BiquadFilter(coef.b[0], coef.b[1], coef.b[2],
                                                 coef.a[0], coef.a[1], coef.a[2]);
            }
        }
    }
}

SecondMetrics NoiseProcessor::process_segment(const float* buffer_start,
                                                const float* buffer_end,
                                                float duration_s) noexcept {
    size_t n = buffer_end - buffer_start;
    size_t expected_n = static_cast<size_t>(sample_rate_ * duration_s);

    if (n != expected_n || n == 0) {
        return SecondMetrics{};
    }

    SecondMetrics m;
    m.duration_s = duration_s;
    m.n_samples = static_cast<int32_t>(n);

    // ---------------------------------------------------------------
    // Phase 1: Copy input to scratch buffers for A/C weighting
    // Stack-allocated VLAs sized to actual sample count (zero heap alloc).
    // For n ≤ 48000 this fits easily on most stacks (e.g. 8000 samples =
    // ~64 KiB for a typical 10 ms @ 48 kHz block — well within limits).
    // ---------------------------------------------------------------
    float a_buf[n];
    float c_buf[n];

    // Copy input to scratch buffers
    for (size_t i = 0; i < n; ++i) {
        a_buf[i] = buffer_start[i];
        c_buf[i] = buffer_start[i];
    }

    // ---------------------------------------------------------------
    // Phase 2: A/C weighting — streaming in-place (zero heap alloc)
    // ---------------------------------------------------------------
    // Reset filter chains for each segment to match original behavior
    // (fresh filter = zero initial state = no carryover from previous segment)
    a_weight_chain_.reset();
    c_weight_chain_.reset();

    // v3.2.1: apply 1kHz gain factor AFTER the biquad chain (separate from b/a coefficients)
    for (size_t i = 0; i < n; ++i) {
        a_buf[i] = a_weight_chain_.process(a_buf[i]) * a_weight_gain_;
        c_buf[i] = c_weight_chain_.process(c_buf[i]) * c_weight_gain_;
    }

    // ---------------------------------------------------------------
    // Phase 3: Compute all metrics in a single pass (zero heap alloc)
    // ---------------------------------------------------------------
    float sum_x = 0.0f, sum_x2 = 0.0f, sum_x3 = 0.0f, sum_x4 = 0.0f;
    float sum_a_sq = 0.0f, sum_c_sq = 0.0f, sum_z_sq = 0.0f;
    float peak_z = 0.0f, peak_c = 0.0f;
    float mean_a = 0.0f, mean_c = 0.0f, mean_z = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        float z = buffer_start[i];
        float a = a_buf[i];
        float c = c_buf[i];

        // Raw moments (Z signal)
        float z2 = z * z;
        sum_x += z;
        sum_x2 += z2;
        sum_x3 += z2 * z;
        sum_x4 += z2 * z2;

        // Energy accumulators for Leq
        sum_a_sq += a * a;
        sum_c_sq += c * c;
        sum_z_sq += z2;

        // Peak tracking
        float abs_z = std::abs(z);
        float abs_c = std::abs(c);
        if (abs_z > peak_z) peak_z = abs_z;
        if (abs_c > peak_c) peak_c = abs_c;

        // Kurtosis means
        mean_a += a;
        mean_c += c;
        mean_z += z;
    }

    // Store raw moments
    m.sum_x = sum_x;
    m.sum_x2 = sum_x2;
    m.sum_x3 = sum_x3;
    m.sum_x4 = sum_x4;

    // Beta kurtosis from raw moments
    m.beta_kurtosis = calculate_kurtosis_from_moments(m.n_samples, sum_x, sum_x2, sum_x3, sum_x4);

    // Leq calculations
    auto calc_leq_from_sum_sq = [this](float sum_sq, size_t n_) -> float {
        if (n_ == 0) return -INFINITY;
        float rms = std::sqrt(sum_sq / n_);
        if (rms <= 0) return -INFINITY;
        return 20.0f * std::log10(rms / reference_pressure_);
    };

    m.LAeq = calc_leq_from_sum_sq(sum_a_sq, n);
    m.LCeq = calc_leq_from_sum_sq(sum_c_sq, n);
    m.LZeq = calc_leq_from_sum_sq(sum_z_sq, n);

    // Peak calculations
    m.LZPeak = (peak_z > 0) ? (20.0f * std::log10(peak_z / reference_pressure_)) : -INFINITY;
    m.LCPeak = (peak_c > 0) ? (20.0f * std::log10(peak_c / reference_pressure_)) : -INFINITY;

    // LAFmax approximation (Leq + 3 dB as in original)
    m.LAFmax = m.LAeq + 3.0f;

    // Kurtosis calculations
    auto calc_kurtosis_from_data = [](const float* data, size_t n_, float mean_val) -> float {
        if (n_ < 4) return 3.0f;
        float m2 = 0.0f, m4 = 0.0f;
        for (size_t i = 0; i < n_; ++i) {
            float d = data[i] - mean_val;
            float d2 = d * d;
            m2 += d2;
            m4 += d2 * d2;
        }
        m2 /= n_;
        m4 /= n_;
        if (m2 <= 0) return 3.0f;
        return m4 / (m2 * m2);
    };

    float inv_n = 1.0f / static_cast<float>(n);
    m.kurtosis_a_weighted = calc_kurtosis_from_data(a_buf, n, mean_a * inv_n);
    m.kurtosis_c_weighted = calc_kurtosis_from_data(c_buf, n, mean_c * inv_n);
    m.kurtosis_total = calc_kurtosis_from_data(buffer_start, n, mean_z * inv_n);

    // Dose calculations
    if (m.LAeq > 0) {
        const auto& prof_n = DoseCalculator::get_profile(DoseStandard::NIOSH);
        const auto& prof_p = DoseCalculator::get_profile(DoseStandard::OSHA_PEL);
        const auto& prof_h = DoseCalculator::get_profile(DoseStandard::OSHA_HCA);
        const auto& prof_e = DoseCalculator::get_profile(DoseStandard::EU_ISO);

        m.dose_frac_niosh    = DoseCalculator::calculate_dose_increment(m.LAeq, duration_s, prof_n) / 100.0f;
        m.dose_frac_osha_pel = DoseCalculator::calculate_dose_increment(m.LAeq, duration_s, prof_p) / 100.0f;
        m.dose_frac_osha_hca = DoseCalculator::calculate_dose_increment(m.LAeq, duration_s, prof_h) / 100.0f;
        m.dose_frac_eu_iso   = DoseCalculator::calculate_dose_increment(m.LAeq, duration_s, prof_e) / 100.0f;
    }

    // QC flags
    m.overload_flag = (m.LZPeak > OVERLOAD_THRESHOLD);
    m.underrange_flag = (m.LAeq < UNDERRANGE_THRESHOLD);
    m.wearing_state = (m.LAeq > 40.0f);

    // ---------------------------------------------------------------
    // Phase 4: 1/3 octave band analysis — streaming (zero heap alloc)
    // Each band filter processes each sample and accumulates moments inline
    // ---------------------------------------------------------------
    FreqBandMoments band_moments[9] = {};

    // === v3.2 Bug A fix (SPL-level peak gain correction) ===
    // The 2nd-order bandpass biquad has peak gain |H(fc)| ∝ fc², causing
    // low-frequency bands to report SPL ~25 dB lower than high-frequency
    // bands when fed a signal at their center frequency. We apply a
    // per-band correction factor to the RMS so the reported SPL is
    // physically correct. The correction is pre-computed for 48 kHz
    // and computed on-the-fly for other sample rates.
    float band_corrections[9] = {};
    {
        const float fs = static_cast<float>(sample_rate_);
        const float BAND_FACTOR = 1.12246204830937f;  // 2^(1/6)
        for (int b = 0; b < 9; ++b) {
            float fc = BAND_CENTER_FREQS[b];
            // For 48 kHz, use the pre-computed correction
            if (sample_rate_ == 48000) {
                band_corrections[b] = BANDPASS_COEFFS_48K[b].peak_gain_correction;
            } else {
                // Compute |H(fc)| for the dynamically-designed biquad
                float b0 = band_filters_[b].b0();
                float b1 = band_filters_[b].b1();
                float b2 = band_filters_[b].b2();
                float a0 = band_filters_[b].a0();
                float a1 = band_filters_[b].a1();
                float a2 = band_filters_[b].a2();
                double w0 = 2.0 * noise_const::PI_F * fc / fs;
                double cos_w = std::cos(w0);
                double sin_w = std::sin(w0);
                // H(z=e^jω) numerator/denominator
                double num_re = b0 + b1 * cos_w + b2 * std::cos(2.0 * w0);
                double num_im = -b1 * sin_w - b2 * std::sin(2.0 * w0);
                double den_re = a0 + a1 * cos_w + a2 * std::cos(2.0 * w0);
                double den_im = -a1 * sin_w - a2 * std::sin(2.0 * w0);
                double num_mag = std::sqrt(num_re * num_re + num_im * num_im);
                double den_mag = std::sqrt(den_re * den_re + den_im * den_im);
                double peak_gain = (den_mag > 1e-30) ? (num_mag / den_mag) : 1.0;
                band_corrections[b] = (peak_gain > 1e-30)
                    ? static_cast<float>(1.0 / peak_gain) : 1.0f;
            }
        }
    }

    for (int b = 0; b < 9; ++b) {
        // Reset band filter for each segment (matches original behavior)
        band_filters_[b].reset();

        float sum_sq_band = 0.0f;
        band_moments[b].n = static_cast<int32_t>(n);

        for (size_t i = 0; i < n; ++i) {
            // Filter each sample in-place through the bandpass filter
            float x = band_filters_[b].process(buffer_start[i]);
            float x2 = x * x;
            sum_sq_band += x2;
            band_moments[b].s1 += x;
            band_moments[b].s2 += x2;
            band_moments[b].s3 += x2 * x;
            band_moments[b].s4 += x2 * x2;
        }

        float band_rms = std::sqrt(sum_sq_band / n);
        // Apply v3.2 Bug A peak-gain correction (multiply RMS by correction
        // factor before converting to dB; equivalent to adding 20·log10(corr) to SPL)
        float corrected_rms = band_rms * band_corrections[b];
        float spl = (corrected_rms > 0) ? (20.0f * std::log10(corrected_rms / reference_pressure_)) : -INFINITY;

        switch (b) {
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

    // Store band moments
    for (int i = 0; i < 9; ++i) {
        *band_moments_ptr(m, i) = band_moments[i];
    }

    return m;
}

MinuteMetrics NoiseProcessor::aggregate_metrics(const SecondMetrics* second_metrics,
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

} // namespace noise_toolkit
