/**
 * @file event_detector.cpp
 * @brief EventDetector implementation — v3.1.2
 *
 * Zero heap allocation, all state in class members.
 * Supports peak trigger and level trigger with frame-based debounce.
 */

#include "event_detector.hpp"
#include <algorithm>
#include <limits>

namespace noise_toolkit {

EventDetector::EventDetector(const EventDetectorConfig& config) noexcept
    : config_(config),
      consecutive_anomaly_count_(0),
      cooldown_remaining_(0),
      impulse_detected_(false) {
    init_internal_chain();
}

void EventDetector::init_internal_chain() noexcept {
    const int fs = (config_.sample_rate > 0) ? config_.sample_rate : 48000;

    // 默认先填 48 kHz 常量表（保证任何情况下都有合法系数）
    for (size_t i = 0; i < A_WEIGHTING_SECTIONS; ++i) {
        a_chain_.sections[i] = A_WEIGHTING_48K.sections[i];
    }
    a_gain_ = 1.0f;

    // 优先查多采样率表（8k/16k/22.05k/32k/44.1k/48k/96k）
    if (const auto* entry = find_weighting_entry(fs)) {
        for (int i = 0; i < entry->a_count && i < static_cast<int>(A_WEIGHTING_SECTIONS); ++i) {
            a_chain_.sections[i].b0 = entry->a[i].b0;
            a_chain_.sections[i].b1 = entry->a[i].b1;
            a_chain_.sections[i].b2 = entry->a[i].b2;
            a_chain_.sections[i].a0 = 1.0f;
            a_chain_.sections[i].a1 = entry->a[i].a1;
            a_chain_.sections[i].a2 = entry->a[i].a2;
        }
        a_gain_ = entry->a_gain;
    }

    // 指数时间计权 α（IEC 61672-1 §7）
    alpha_F_ = std::exp(-1.0f / (0.125f * static_cast<float>(fs)));
    alpha_S_ = std::exp(-1.0f / (1.0f   * static_cast<float>(fs)));

    a_chain_.reset();
    laf_sq_ = 0.0f;
    las_sq_ = 0.0f;
}

float EventDetector::power_to_db(float power_sq, float p_ref) noexcept {
    if (power_sq <= 0.0f || p_ref <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }
    return 10.0f * std::log10(power_sq) - 20.0f * std::log10(p_ref);
}

uint8_t EventDetector::required_debounce_frames(uint8_t debounce_frames) noexcept {
    return debounce_frames == 0 ? 1 : debounce_frames;
}

EventCheckResult EventDetector::check_segment(const float* buffer_start,
                                               const float* buffer_end) noexcept {
    if (buffer_start >= buffer_end) {
        return EventCheckResult::NORMAL;
    }

    float lzeq = compute_leq(buffer_start, buffer_end);
    float lzpeak = compute_peak(buffer_start, buffer_end);

    // Peak trigger: highest priority, bypasses cooldown and debounce
    if (lzpeak >= config_.peak_threshold_db) {
        impulse_detected_ = true;
        cooldown_remaining_ = config_.cooldown_frames;
        consecutive_anomaly_count_ = 0;
        return EventCheckResult::OVERLOAD;
    }

    // Cooldown suppresses level trigger only (not overload above)
    if (cooldown_remaining_ > 0) {
        --cooldown_remaining_;
        if (lzeq < config_.underrange_threshold_db) {
            return EventCheckResult::UNDERRANGE;
        }
        return EventCheckResult::NORMAL;
    }

    /*
     * [v3.3.0] LZeq>90 trigger LOGIC DEPRECATED
     * Interface 3 single-trigger recommendation: ONLY LZpeak >= 140 dB → OVERLOAD
     * To restore: uncomment this block AND change leq_threshold_db default back to 90.0f
     *
    const uint8_t required_frames = required_debounce_frames(config_.debounce_frames);
    if (lzeq >= config_.leq_threshold_db) {
        if (consecutive_anomaly_count_ < UINT8_MAX) {
            ++consecutive_anomaly_count_;
        }
        if (consecutive_anomaly_count_ >= required_frames) {
            impulse_detected_ = true;
            cooldown_remaining_ = config_.cooldown_frames;
            consecutive_anomaly_count_ = 0;
            return EventCheckResult::IMPULSE_SUSPECT;
        }
    } else {
        consecutive_anomaly_count_ = 0;
    }
    */
    // [v3.3.0] LZeq-based IMPULSE_SUSPECT disabled
    // consecutive_anomaly_count_ preserved ABI-compatible but never incremented

    if (lzeq < config_.underrange_threshold_db) {
        return EventCheckResult::UNDERRANGE;
    }

    return EventCheckResult::NORMAL;
}

void EventDetector::reset() noexcept {
    consecutive_anomaly_count_ = 0;
    cooldown_remaining_ = 0;
    impulse_detected_ = false;
    // v3.3.3
    for (size_t i = 0; i < LAF_HISTORY_SIZE; ++i) {
        laf_history_[i] = 0.0f;
        time_history_[i] = 0.0f;
    }
    history_write_idx_ = 0;
    history_count_ = 0;
    cumulative_time_s_ = 0.0f;
    background_power_ = 0.0f;
    background_laeq_db_ = -std::numeric_limits<float>::infinity();
    background_initialized_ = false;
    // v3.3.3: 内置 A 计权链 + 时间计权状态
    a_chain_.reset();
    laf_sq_ = 0.0f;
    las_sq_ = 0.0f;
}

EventResult EventDetector::check_metrics(const float* start, const float* end) noexcept {
    const std::ptrdiff_t n = end - start;
    if (n <= 0) {
        return EventResult{};
    }

    const float p_ref = config_.reference_pressure;
    float sum_a_sq = 0.0f;
    float peak_z_lin = 0.0f;

    // 逐样本：A 计权（4 段 biquad） + Fast/Slow 指数时间计权
    for (const float* p = start; p < end; ++p) {
        const float z  = *p;
        const float a  = a_chain_.process(z) * a_gain_;
        const float a2 = a * a;

        sum_a_sq += a2;
        laf_sq_ = alpha_F_ * laf_sq_ + (1.0f - alpha_F_) * a2;
        las_sq_ = alpha_S_ * las_sq_ + (1.0f - alpha_S_) * a2;

        const float az = std::abs(z);
        if (az > peak_z_lin) peak_z_lin = az;
    }

    // 组装最小 SecondMetrics，复用四维度判定逻辑
    SecondMetrics m;
    const int fs = (config_.sample_rate > 0) ? config_.sample_rate : 48000;
    m.duration_s = static_cast<float>(n) / static_cast<float>(fs);
    m.n_samples  = static_cast<int32_t>(n);
    m.LAF    = power_to_db(laf_sq_, p_ref);
    m.LAS    = power_to_db(las_sq_, p_ref);
    m.LAeq   = power_to_db(sum_a_sq / static_cast<float>(n), p_ref);
    m.LZPeak = (peak_z_lin > 0.0f)
                   ? (20.0f * std::log10(peak_z_lin / p_ref))
                   : -std::numeric_limits<float>::infinity();

    return check_metrics(m);
}

//==============================================================================
// v3.3.3: LAF/LAS-based multi-dimension event detection
//==============================================================================

EventResult EventDetector::check_metrics(const SecondMetrics& m) noexcept {
    EventResult r;
    r.laf_dB = m.LAF;
    r.las_dB = m.LAS;

    const float dt = (m.duration_s > 0.0f) ? m.duration_s : 1.0f;
    const bool laf_finite = std::isfinite(m.LAF);
    const bool las_finite = std::isfinite(m.LAS);

    //--- D2: 脉冲指标 = LAF − LAS ---
    if (laf_finite && las_finite) {
        r.impulse_metric_dB = m.LAF - m.LAS;
    }

    //--- D3: LAF 上升率（相对 500 ms 前）---
    if (laf_finite && cumulative_time_s_ > 0.5f) {
        const float past = lookback_laf(0.5f);
        if (std::isfinite(past)) {
            r.laf_rise_dB = m.LAF - past;
        }
    }

    //--- D4: 背景对比（5 min 指数平均）---
    if (std::isfinite(m.LAeq)) {
        update_background(m.LAeq, dt);
    }
    if (background_initialized_ && laf_finite && std::isfinite(background_laeq_db_)) {
        r.background_delta_dB = m.LAF - background_laeq_db_;
    }

    //--- D1: LAF 阈值 ---
    const bool laf_severe   = laf_finite && (m.LAF >= config_.laf_severe_db);
    const bool laf_moderate = laf_finite && (m.LAF >= config_.laf_moderate_db);
    const bool laf_minor    = laf_finite && (m.LAF >= config_.laf_minor_db);

    //--- D2 触发 ---
    const bool impulse_severe = (r.impulse_metric_dB >= config_.impulse_severe_db);
    const bool impulse_minor  = (r.impulse_metric_dB >= config_.impulse_minor_db);

    //--- D3 触发 ---
    const bool rise_moderate = (r.laf_rise_dB >= config_.laf_rise_moderate_db);
    const bool rise_minor    = (r.laf_rise_dB >= config_.laf_rise_minor_db);

    //--- D4 触发 ---
    const bool bg_trigger = (r.background_delta_dB >= config_.background_delta_db);

    //--- 分级（取最高档）---
    if (laf_severe || impulse_severe) {
        r.event_type = EventType::SEVERE;
    } else if (laf_moderate || rise_moderate) {
        r.event_type = EventType::MODERATE;
    } else if (laf_minor || rise_minor || impulse_minor || bg_trigger) {
        r.event_type = EventType::MINOR;
    } else {
        r.event_type = EventType::NONE;
    }

    r.trigger_laf_threshold = laf_minor;
    r.trigger_impulse       = impulse_minor;
    r.trigger_onset         = rise_minor;
    r.trigger_background    = bg_trigger;

    //--- 兼容：过载直通 ---
    r.is_overload = std::isfinite(m.LZPeak) && (m.LZPeak >= config_.peak_threshold_db);
    if (r.is_overload && r.event_type < EventType::SEVERE) {
        r.event_type = EventType::SEVERE;
    }

    r.severity = grade_severity(r, config_);

    //--- 更新历史（本次样本供后续 lookback 使用）---
    if (laf_finite) {
        push_laf_history(m.LAF, dt);
    } else {
        cumulative_time_s_ += dt;
    }

    return r;
}

void EventDetector::push_laf_history(float laf_db, float dt) noexcept {
    cumulative_time_s_ += dt;
    laf_history_[history_write_idx_] = laf_db;
    time_history_[history_write_idx_] = cumulative_time_s_;
    history_write_idx_ = (history_write_idx_ + 1) % LAF_HISTORY_SIZE;
    if (history_count_ < LAF_HISTORY_SIZE) {
        ++history_count_;
    }
}

float EventDetector::lookback_laf(float seconds_ago) const noexcept {
    if (history_count_ == 0) return -std::numeric_limits<float>::infinity();

    const float target_time = cumulative_time_s_ - seconds_ago;
    if (target_time <= 0.0f) return -std::numeric_limits<float>::infinity();

    float best = -std::numeric_limits<float>::infinity();
    float best_dist = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < LAF_HISTORY_SIZE; ++i) {
        const float t = time_history_[i];
        if (t <= 0.0f) continue;  // 空槽
        const float dist = std::abs(t - target_time);
        if (dist < best_dist) {
            best_dist = dist;
            best = laf_history_[i];
        }
    }
    return best;
}

void EventDetector::update_background(float laeq_db, float dt) noexcept {
    // LAeq（dB）→ 功率域线性值
    const float power = std::pow(10.0f, laeq_db / 10.0f);

    if (!background_initialized_) {
        // 首次：直接用当前值初始化，避免长时间收敛
        background_power_ = power;
        background_initialized_ = true;
    } else {
        const float tau = (config_.background_tau_s > 0.0f) ? config_.background_tau_s : 300.0f;
        const float alpha = std::exp(-dt / tau);
        background_power_ = alpha * background_power_ + (1.0f - alpha) * power;
    }

    background_laeq_db_ = (background_power_ > 0.0f)
        ? (10.0f * std::log10(background_power_))
        : -std::numeric_limits<float>::infinity();
}

uint8_t EventDetector::grade_severity(const EventResult& r,
                                       const EventDetectorConfig& c) noexcept {
    if (r.event_type == EventType::NONE) return 0;

    // 严重程度评分（0-100）：取四个维度中“超出幅度最大者”的归一化值
    float score = 0.0f;

    // D1: LAF 从 minor 到 severe 映射到 20 → 100
    if (std::isfinite(r.laf_dB) && c.laf_severe_db > c.laf_minor_db) {
        const float t = (r.laf_dB - c.laf_minor_db) / (c.laf_severe_db - c.laf_minor_db);
        score = std::max(score, 20.0f + 80.0f * std::min(std::max(t, 0.0f), 1.0f));
    }

    // D2: 脉冲指标 从 minor 到 severe 映射到 20 → 100
    if (c.impulse_severe_db > c.impulse_minor_db) {
        const float t = (r.impulse_metric_dB - c.impulse_minor_db) /
                        (c.impulse_severe_db - c.impulse_minor_db);
        if (t > 0.0f) {
            score = std::max(score, 20.0f + 80.0f * std::min(t, 1.0f));
        }
    }

    // D3: 上升率 从 minor 到 2×moderate 映射到 20 → 80
    if (c.laf_rise_moderate_db > c.laf_rise_minor_db) {
        const float t = (r.laf_rise_dB - c.laf_rise_minor_db) /
                        (2.0f * c.laf_rise_moderate_db - c.laf_rise_minor_db);
        if (t > 0.0f) {
            score = std::max(score, 20.0f + 60.0f * std::min(t, 1.0f));
        }
    }

    // D4: 背景差
    if (c.background_delta_db > 0.0f) {
        const float t = r.background_delta_dB / (2.0f * c.background_delta_db);
        if (t > 0.0f) {
            score = std::max(score, 20.0f + 60.0f * std::min(t, 1.0f));
        }
    }

    // 过载直接封顶
    if (r.is_overload) score = 100.0f;

    if (score < 0.0f)   score = 0.0f;
    if (score > 100.0f) score = 100.0f;
    return static_cast<uint8_t>(score + 0.5f);
}

float EventDetector::compute_leq(const float* start, const float* end) const noexcept {
    float sum_sq = 0.0f;
    int count = 0;

    for (const float* p = start; p < end; ++p) {
        sum_sq += (*p) * (*p);
        ++count;
    }

    if (count == 0 || sum_sq <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }

    float rms = std::sqrt(sum_sq / static_cast<float>(count));
    if (rms <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }

    float p0 = config_.reference_pressure;
    return 20.0f * std::log10(rms / p0);
}

float EventDetector::compute_peak(const float* start, const float* end) const noexcept {
    float peak_abs = 0.0f;

    for (const float* p = start; p < end; ++p) {
        float abs_val = std::abs(*p);
        if (abs_val > peak_abs) {
            peak_abs = abs_val;
        }
    }

    if (peak_abs <= 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }

    float p0 = config_.reference_pressure;
    return 20.0f * std::log10(peak_abs / p0);
}

} // namespace noise_toolkit
