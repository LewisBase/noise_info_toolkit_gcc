/**
 * @file event_processor.cpp
 * @brief Event processor implementation
 */

#include "event_processor.hpp"
#include <algorithm>
#include <fstream>
#include <cmath>
#include <iostream>

namespace noise_toolkit {

EventProcessor::EventProcessor(int sample_rate,
                                double leq_threshold,
                                double peak_threshold,
                                double debounce_s,
                                const std::string& output_dir,
                                bool enable_audio_save)
    : sample_rate_(sample_rate),
      output_dir_(output_dir),
      enable_audio_save_(enable_audio_save),
      event_detector_(leq_threshold, peak_threshold, debounce_s, sample_rate),
      ring_buffer_(sample_rate, 12.0, 2.0, 8.0, 1),
      is_running_(false) {
    
    // Register callbacks
    event_detector_.add_event_start_callback([this](const EventInfo& info) {
        this->on_event_start(info);
    });
    
    event_detector_.add_event_end_callback([this](const EventInfo& info) {
        this->on_event_end(info);
    });
}

void EventProcessor::add_event_callback(std::function<void(const EventInfo&)> callback) {
    event_callbacks_.push_back(callback);
}

void EventProcessor::start(const std::string& session_id) {
    is_running_ = true;
    session_id_ = session_id;
    events_.clear();
    ring_buffer_.clear();
    current_event_post_data_.clear();
}

std::vector<EventInfo> EventProcessor::stop() {
    is_running_ = false;
    
    // Force end current event
    if (event_detector_.is_in_event()) {
        auto info = event_detector_.force_end_event(std::chrono::system_clock::now());
        if (info.has_value()) {
            finalize_event(info.value());
            events_.push_back(info.value());
        }
    }
    
    return events_;
}

std::optional<EventInfo> EventProcessor::process_audio_chunk(
    const std::vector<double>& audio_data,
    std::optional<std::chrono::system_clock::time_point> timestamp) {
    
    if (!is_running_) {
        return std::nullopt;
    }
    
    if (!timestamp.has_value()) {
        timestamp = std::chrono::system_clock::now();
    }
    
    // Write to ring buffer
    ring_buffer_.write(audio_data);
    
    // If recording post-trigger data
    if (!current_event_post_data_.empty()) {
        current_event_post_data_.insert(current_event_post_data_.end(),
                                        audio_data.begin(), audio_data.end());
        
        // Check if post-trigger duration reached
        int post_samples_needed = ring_buffer_.get_posttrigger_samples();
        if (static_cast<int>(current_event_post_data_.size()) >= post_samples_needed) {
            return finish_event_recording();
        }
    }
    
    // Process samples for event detection
    std::optional<EventInfo> completed_event = std::nullopt;
    for (double sample : audio_data) {
        auto event_info = event_detector_.process_sample(
            sample, sample, timestamp.value(), session_id_);
        if (event_info.has_value()) {
            completed_event = event_info;
            break;
        }
    }
    
    return completed_event;
}

void EventProcessor::on_event_start(const EventInfo& info) {
    // Start collecting post-trigger data
    current_event_post_data_.clear();
}

void EventProcessor::on_event_end(const EventInfo& info) {
    // Event ended, but post-trigger data collection may not be complete
    // The process_audio_chunk will handle completing the recording
}

std::optional<EventInfo> EventProcessor::finish_event_recording() {
    if (current_event_post_data_.empty() || events_.empty()) {
        current_event_post_data_.clear();
        return std::nullopt;
    }
    
    // Get the last event
    EventInfo& event_info = events_.back();
    
    // Save event audio if enabled
    if (enable_audio_save_ && !current_event_post_data_.empty()) {
        int post_samples = ring_buffer_.get_posttrigger_samples();
        std::vector<double> post_data(current_event_post_data_.begin(),
                                        current_event_post_data_.begin() + 
                                        std::min(static_cast<size_t>(post_samples),
                                                current_event_post_data_.size()));
        
        std::string audio_path = ring_buffer_.save_event_audio(
            event_info.event_id, post_data, output_dir_);
        event_info.audio_file_path = audio_path;
    }
    
    // Calculate event metrics
    calculate_event_metrics(event_info);
    
    // Clear post data
    current_event_post_data_.clear();
    
    // Call external callbacks
    for (const auto& callback : event_callbacks_) {
        callback(event_info);
    }
    
    return event_info;
}

void EventProcessor::finalize_event(EventInfo& info) {
    info.audio_file_path = std::nullopt;  // Force end doesn't save audio
    events_.push_back(info);
    
    for (const auto& callback : event_callbacks_) {
        callback(info);
    }
}

void EventProcessor::calculate_event_metrics(EventInfo& info) {
    // Simplified: estimate excess kurtosis
    info.beta_excess_z = 3.0;  // Default normal distribution kurtosis
}

EventProcessor::Stats EventProcessor::get_stats() const {
    Stats stats;
    stats.is_running = is_running_;
    stats.session_id = session_id_;
    stats.event_count = static_cast<int>(events_.size());
    stats.buffer_info = ring_buffer_.get_info();
    stats.detector_stats = event_detector_.get_stats();
    return stats;
}

// BatchEventProcessor implementation
BatchEventProcessor::BatchEventProcessor(double leq_threshold,
                                          double peak_threshold,
                                          double debounce_s)
    : leq_threshold_(leq_threshold),
      peak_threshold_(peak_threshold),
      debounce_s_(debounce_s) {}

std::vector<EventInfo> BatchEventProcessor::process_file(const std::string& file_path,
                                                         const std::string& session_id) {
    // This would load the file and process it
    // For now, return empty vector (file loading not implemented)
    return {};
}

std::vector<EventInfo> BatchEventProcessor::process_data(const std::vector<double>& audio_data,
                                                         double sample_rate,
                                                         const std::string& session_id) {
    EventProcessor processor(sample_rate, leq_threshold_, peak_threshold_, 
                            debounce_s_, "", false);
    
    processor.start(session_id);
    
    // Process in 1-second chunks
    int chunk_size = static_cast<int>(sample_rate);
    for (size_t i = 0; i < audio_data.size(); i += chunk_size) {
        size_t end = std::min(i + chunk_size, audio_data.size());
        std::vector<double> chunk(audio_data.begin() + i, audio_data.begin() + end);
        
        // Pad if needed
        if (chunk.size() < static_cast<size_t>(chunk_size)) {
            chunk.resize(chunk_size, 0.0);
        }
        
        processor.process_audio_chunk(chunk);
    }
    
    return processor.stop();
}

} // namespace noise_toolkit
