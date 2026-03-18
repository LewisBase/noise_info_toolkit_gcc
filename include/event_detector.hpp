/**
 * @file event_detector.hpp
 * @brief Impulsive noise event detector
 * 
 * Supports three trigger modes:
 * - LEQ trigger: LZeq_125 >= threshold (default 90-95 dB)
 * - Peak trigger: LCpeak >= threshold (default 130 dB)
 * - Slope trigger: ΔLZeq >= threshold (default 10 dB/50ms)
 */

#pragma once

#include "noise_toolkit.hpp"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <deque>
#include <optional>

namespace noise_toolkit {

/**
 * @brief Event trigger type enumeration
 */
enum class TriggerType {
    LEQ,      // Sound level trigger
    PEAK,     // Peak trigger
    SLOPE,    // Slope trigger
    UNKNOWN   // Unknown/undefined
};

/**
 * @brief Event information structure
 */
struct EventInfo {
    std::string event_id;
    std::string session_id;
    std::chrono::system_clock::time_point start_time;
    std::optional<std::chrono::system_clock::time_point> end_time;
    double duration_s;
    TriggerType trigger_type;
    
    // Sound level metrics
    double lzpeak_db;
    double lcpeak_db;
    double laeq_event_db;
    double sel_lae_db;  // Sound Exposure Level
    
    // Kurtosis
    std::optional<double> beta_excess_z;
    
    // Audio file
    std::optional<std::string> audio_file_path;
    double pretrigger_s;
    double posttrigger_s;
    
    // Notes
    std::optional<std::string> notes;
    
    EventInfo() : duration_s(0.0), trigger_type(TriggerType::UNKNOWN),
                  lzpeak_db(0.0), lcpeak_db(0.0), laeq_event_db(0.0),
                  sel_lae_db(0.0), pretrigger_s(2.0), posttrigger_s(8.0) {}
};

/**
 * @brief Sliding window calculator for LZeq_125
 */
class SlidingWindowCalculator {
public:
    /**
     * @brief Constructor
     * @param window_duration_s Window duration in seconds (default 125ms)
     * @param sample_rate Sample rate in Hz
     */
    SlidingWindowCalculator(double window_duration_s = 0.125, int sample_rate = 48000);
    
    /**
     * @brief Add sample and return current Leq if window is full
     * @param sample Sound pressure sample (Pa)
     * @return Current window Leq (dB), or std::nullopt if window not full
     */
    std::optional<double> add_sample(double sample);
    
    /**
     * @brief Reset the calculator
     */
    void reset();
    
    /**
     * @brief Check if window is full
     */
    bool is_full() const { return buffer_.size() >= window_samples_; }

private:
    double window_duration_s_;
    int sample_rate_;
    int window_samples_;
    std::deque<double> buffer_;
    double reference_pressure_;
};

/**
 * @brief Event detector class
 */
class EventDetector {
public:
    // Default thresholds (from white paper)
    static constexpr double DEFAULT_LEQ_THRESHOLD = 90.0;      // dB
    static constexpr double DEFAULT_PEAK_THRESHOLD = 130.0;    // dB
    static constexpr double DEFAULT_SLOPE_THRESHOLD = 10.0;    // dB/50ms
    static constexpr double DEFAULT_DEBOUNCE_S = 0.5;          // seconds
    
    /**
     * @brief Constructor
     */
    EventDetector(double leq_threshold = DEFAULT_LEQ_THRESHOLD,
                  double peak_threshold = DEFAULT_PEAK_THRESHOLD,
                  double slope_threshold = DEFAULT_SLOPE_THRESHOLD,
                  double debounce_s = DEFAULT_DEBOUNCE_S,
                  int sample_rate = 48000,
                  double reference_pressure = REFERENCE_PRESSURE);
    
    /**
     * @brief Add callback for event start
     */
    void add_event_start_callback(std::function<void(const EventInfo&)> callback);
    
    /**
     * @brief Add callback for event end
     */
    void add_event_end_callback(std::function<void(const EventInfo&)> callback);
    
    /**
     * @brief Process a single sample
     * @param sample_z Z-weighted sound pressure (Pa)
     * @param sample_c C-weighted sound pressure (Pa)
     * @param current_time Current timestamp
     * @param session_id Session identifier
     * @return EventInfo if event ended, otherwise std::nullopt
     */
    std::optional<EventInfo> process_sample(double sample_z,
                                             double sample_c,
                                             std::chrono::system_clock::time_point current_time,
                                             const std::string& session_id = "default");
    
    /**
     * @brief Force end current event
     */
    std::optional<EventInfo> force_end_event(std::chrono::system_clock::time_point end_time);
    
    /**
     * @brief Get detector statistics
     */
    struct Stats {
        int total_events;
        bool is_in_event;
        std::optional<EventInfo> current_event;
        struct Thresholds {
            double leq;
            double peak;
            double slope;
            double debounce_s;
        } thresholds;
    };
    Stats get_stats() const;
    
    // Accessors
    bool is_in_event() const { return is_in_event_; }
    int get_event_count() const { return event_counter_; }

private:
    double leq_threshold_;
    double peak_threshold_;
    double slope_threshold_;
    double debounce_s_;
    int sample_rate_;
    double reference_pressure_;
    
    // State
    std::optional<std::chrono::system_clock::time_point> last_event_time_;
    int event_counter_;
    bool is_in_event_;
    std::optional<EventInfo> current_event_info_;
    
    // Sliding window calculator
    SlidingWindowCalculator leq_125_calculator_;
    
    // Slope detection history
    int slope_window_samples_;
    std::deque<double> leq_history_;
    
    // Event tracking
    double event_lzpeak_max_;
    double event_lcpeak_max_;
    
    // Callbacks
    std::vector<std::function<void(const EventInfo&)>> event_start_callbacks_;
    std::vector<std::function<void(const EventInfo&)>> event_end_callbacks_;
    
    // Helper methods
    bool check_debounce(std::chrono::system_clock::time_point current_time) const;
    std::pair<bool, TriggerType> detect_trigger(double lzeq_125, double lcpeak, 
                                                  std::optional<double> slope) const;
    void start_event(std::chrono::system_clock::time_point start_time,
                     const std::string& session_id,
                     TriggerType trigger_type,
                     double lzpeak, double lcpeak);
    void update_event(double lzpeak, double lcpeak);
    EventInfo end_event(std::chrono::system_clock::time_point end_time);
};

} // namespace noise_toolkit
