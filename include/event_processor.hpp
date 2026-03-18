/**
 * @file event_processor.hpp
 * @brief Event processor - integrates event detection and ring buffer
 * 
 * Processes audio streams, detects impulsive noise events, and saves event audio
 */

#pragma once

#include "event_detector.hpp"
#include "ring_buffer.hpp"
#include <vector>
#include <string>
#include <functional>

namespace noise_toolkit {

/**
 * @brief Event processor class
 * 
 * Integrates event detection, ring buffer, and audio saving:
 * 1. Continuously receives audio stream
 * 2. Real-time detection of impulsive noise events
 * 3. Saves event audio (pre 2s + post 8s)
 * 4. Calculates event metrics (kurtosis, SEL, etc.)
 */
class EventProcessor {
public:
    /**
     * @brief Constructor
     */
    EventProcessor(int sample_rate = 48000,
                   double leq_threshold = 90.0,
                   double peak_threshold = 130.0,
                   double debounce_s = 0.5,
                   const std::string& output_dir = "./audio_events",
                   bool enable_audio_save = true);
    
    /**
     * @brief Add event callback function
     */
    void add_event_callback(std::function<void(const EventInfo&)> callback);
    
    /**
     * @brief Start event processing
     * @param session_id Session identifier
     */
    void start(const std::string& session_id);
    
    /**
     * @brief Stop event processing
     * @return List of all detected events in this session
     */
    std::vector<EventInfo> stop();
    
    /**
     * @brief Process audio chunk
     * @param audio_data Audio samples (Z-weighted or raw pressure)
     * @param timestamp Optional timestamp
     * @return EventInfo if event completed, otherwise std::nullopt
     */
    std::optional<EventInfo> process_audio_chunk(const std::vector<double>& audio_data,
                                                   std::optional<std::chrono::system_clock::time_point> timestamp = std::nullopt);
    
    /**
     * @brief Get all events
     */
    const std::vector<EventInfo>& get_events() const { return events_; }
    
    /**
     * @brief Get event count
     */
    int get_event_count() const { return static_cast<int>(events_.size()); }
    
    /**
     * @brief Get processor statistics
     */
    struct Stats {
        bool is_running;
        std::string session_id;
        int event_count;
        RingBuffer::Info buffer_info;
        EventDetector::Stats detector_stats;
    };
    Stats get_stats() const;
    
    /**
     * @brief Check if processor is running
     */
    bool is_running() const { return is_running_; }

private:
    int sample_rate_;
    std::string output_dir_;
    bool enable_audio_save_;
    
    // Components
    EventDetector event_detector_;
    RingBuffer ring_buffer_;
    
    // State
    bool is_running_;
    std::string session_id_;
    std::vector<EventInfo> events_;
    std::vector<double> current_event_post_data_;
    
    // Callbacks
    std::vector<std::function<void(const EventInfo&)>> event_callbacks_;
    
    // Internal handlers
    void on_event_start(const EventInfo& info);
    void on_event_end(const EventInfo& info);
    std::optional<EventInfo> finish_event_recording();
    void finalize_event(EventInfo& info);
    void calculate_event_metrics(EventInfo& info);
};

/**
 * @brief Batch event processor for processing recorded audio files
 */
class BatchEventProcessor {
public:
    /**
     * @brief Constructor
     */
    BatchEventProcessor(double leq_threshold = 90.0,
                        double peak_threshold = 130.0,
                        double debounce_s = 0.5);
    
    /**
     * @brief Process audio file and detect events
     * @param file_path Path to audio file
     * @param session_id Session identifier
     * @return List of detected events
     */
    std::vector<EventInfo> process_file(const std::string& file_path,
                                         const std::string& session_id = "default");
    
    /**
     * @brief Process raw audio data
     */
    std::vector<EventInfo> process_data(const std::vector<double>& audio_data,
                                         double sample_rate,
                                         const std::string& session_id = "default");

private:
    double leq_threshold_;
    double peak_threshold_;
    double debounce_s_;
};

} // namespace noise_toolkit
