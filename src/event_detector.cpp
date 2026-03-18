/**
 * @file event_detector.cpp
 * @brief Event detector implementation
 */

#include "event_detector.hpp"
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

namespace noise_toolkit {

// Helper function to generate UUID-like string
static std::string generate_event_id() {
    static const char* chars = "0123456789ABCDEF";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::string id = "EVT-";
    for (int i = 0; i < 12; ++i) {
        id += chars[dis(gen)];
    }
    return id;
}

// SlidingWindowCalculator implementation
SlidingWindowCalculator::SlidingWindowCalculator(double window_duration_s, int sample_rate)
    : window_duration_s_(window_duration_s),
      sample_rate_(sample_rate),
      reference_pressure_(REFERENCE_PRESSURE) {
    window_samples_ = static_cast<int>(window_duration_s * sample_rate);
}

std::optional<double> SlidingWindowCalculator::add_sample(double sample) {
    buffer_.push_back(sample);
    
    if (buffer_.size() < static_cast<size_t>(window_samples_)) {
        return std::nullopt;
    }
    
    // Calculate window RMS
    double sum_squares = 0.0;
    for (double x : buffer_) {
        sum_squares += x * x;
    }
    double p_rms = std::sqrt(sum_squares / buffer_.size());
    
    // Convert to dB
    double leq_db = (p_rms > 0) ? 20.0 * std::log10(p_rms / reference_pressure_) : 0.0;
    
    return leq_db;
}

void SlidingWindowCalculator::reset() {
    buffer_.clear();
}

// EventDetector implementation
EventDetector::EventDetector(double leq_threshold,
                              double peak_threshold,
                              double slope_threshold,
                              double debounce_s,
                              int sample_rate,
                              double reference_pressure)
    : leq_threshold_(leq_threshold),
      peak_threshold_(peak_threshold),
      slope_threshold_(slope_threshold),
      debounce_s_(debounce_s),
      sample_rate_(sample_rate),
      reference_pressure_(reference_pressure),
      event_counter_(0),
      is_in_event_(false),
      event_lzpeak_max_(0.0),
      event_lcpeak_max_(0.0),
      leq_125_calculator_(0.125, sample_rate) {
    
    slope_window_samples_ = static_cast<int>(0.05 * sample_rate);  // 50ms
}

void EventDetector::add_event_start_callback(std::function<void(const EventInfo&)> callback) {
    event_start_callbacks_.push_back(callback);
}

void EventDetector::add_event_end_callback(std::function<void(const EventInfo&)> callback) {
    event_end_callbacks_.push_back(callback);
}

std::optional<EventInfo> EventDetector::process_sample(double sample_z,
                                                        double sample_c,
                                                        std::chrono::system_clock::time_point current_time,
                                                        const std::string& session_id) {
    // Update sliding window
    auto lzeq_125_opt = leq_125_calculator_.add_sample(sample_z);
    
    // Calculate peaks
    double lzpeak = (sample_z != 0) ? 
        20.0 * std::log10(std::abs(sample_z) / reference_pressure_) : 0.0;
    double lcpeak = (sample_c != 0) ? 
        20.0 * std::log10(std::abs(sample_c) / reference_pressure_) : 0.0;
    
    // Update slope history
    if (lzeq_125_opt.has_value()) {
        leq_history_.push_back(lzeq_125_opt.value());
    }
    
    // Calculate slope
    std::optional<double> slope = std::nullopt;
    if (leq_history_.size() >= static_cast<size_t>(slope_window_samples_)) {
        slope = leq_history_.back() - leq_history_.front();
    }
    
    // Process based on state
    if (!is_in_event_) {
        auto [triggered, trigger_type] = detect_trigger(
            lzeq_125_opt.value_or(0.0), lcpeak, slope);
        
        if (triggered && check_debounce(current_time)) {
            start_event(current_time, session_id, trigger_type, lzpeak, lcpeak);
        }
    } else {
        // Update event peaks
        update_event(lzpeak, lcpeak);
        
        // Check for event end (level drops below threshold - 10dB hysteresis)
        if (lzeq_125_opt.has_value() && lzeq_125_opt.value() < leq_threshold_ - 10.0) {
            return end_event(current_time);
        }
    }
    
    return std::nullopt;
}

bool EventDetector::check_debounce(std::chrono::system_clock::time_point current_time) const {
    if (!last_event_time_.has_value()) {
        return true;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        current_time - last_event_time_.value()).count();
    return elapsed >= debounce_s_;
}

std::pair<bool, TriggerType> EventDetector::detect_trigger(double lzeq_125, 
                                                           double lcpeak,
                                                           std::optional<double> slope) const {
    // Check peak trigger first (highest priority)
    if (lcpeak >= peak_threshold_) {
        return {true, TriggerType::PEAK};
    }
    
    // Check LEQ trigger
    if (lzeq_125 >= leq_threshold_) {
        return {true, TriggerType::LEQ};
    }
    
    // Check slope trigger
    if (slope.has_value() && slope.value() >= slope_threshold_) {
        return {true, TriggerType::SLOPE};
    }
    
    return {false, TriggerType::UNKNOWN};
}

void EventDetector::start_event(std::chrono::system_clock::time_point start_time,
                                const std::string& session_id,
                                TriggerType trigger_type,
                                double lzpeak, double lcpeak) {
    event_counter_++;
    last_event_time_ = start_time;
    is_in_event_ = true;
    
    event_lzpeak_max_ = lzpeak;
    event_lcpeak_max_ = lcpeak;
    
    EventInfo info;
    info.event_id = generate_event_id();
    info.session_id = session_id;
    info.start_time = start_time;
    info.trigger_type = trigger_type;
    info.lzpeak_db = lzpeak;
    info.lcpeak_db = lcpeak;
    
    current_event_info_ = info;
    
    // Call callbacks
    for (const auto& callback : event_start_callbacks_) {
        callback(info);
    }
}

void EventDetector::update_event(double lzpeak, double lcpeak) {
    if (lzpeak > event_lzpeak_max_) {
        event_lzpeak_max_ = lzpeak;
    }
    if (lcpeak > event_lcpeak_max_) {
        event_lcpeak_max_ = lcpeak;
    }
}

EventInfo EventDetector::end_event(std::chrono::system_clock::time_point end_time) {
    if (!current_event_info_.has_value()) {
        return EventInfo();
    }
    
    EventInfo info = current_event_info_.value();
    info.end_time = end_time;
    info.duration_s = std::chrono::duration_cast<std::chrono::duration<double>>(
        end_time - info.start_time).count();
    info.lzpeak_db = event_lzpeak_max_;
    info.lcpeak_db = event_lcpeak_max_;
    
    // Estimate LAeq_event (simplified: LZpeak - 10 dB)
    info.laeq_event_db = event_lzpeak_max_ - 10.0;
    
    // Calculate SEL: SEL = LAeq + 10*log10(duration)
    if (info.duration_s > 0) {
        info.sel_lae_db = info.laeq_event_db + 10.0 * std::log10(info.duration_s);
    }
    
    // Call callbacks
    for (const auto& callback : event_end_callbacks_) {
        callback(info);
    }
    
    // Reset state
    is_in_event_ = false;
    current_event_info_ = std::nullopt;
    
    return info;
}

std::optional<EventInfo> EventDetector::force_end_event(std::chrono::system_clock::time_point end_time) {
    if (is_in_event_) {
        return end_event(end_time);
    }
    return std::nullopt;
}

EventDetector::Stats EventDetector::get_stats() const {
    Stats stats;
    stats.total_events = event_counter_;
    stats.is_in_event = is_in_event_;
    stats.current_event = current_event_info_;
    stats.thresholds = {leq_threshold_, peak_threshold_, slope_threshold_, debounce_s_};
    return stats;
}

} // namespace noise_toolkit
