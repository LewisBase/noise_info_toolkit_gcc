/**
 * @file ring_buffer.hpp
 * @brief Ring waveform buffer for saving pre/post-trigger audio
 * 
 * According to white paper requirements:
 * - Buffer duration: ≥ 12 seconds
 * - Pre-trigger retention: 2 seconds
 * - Post-trigger recording: 8 seconds
 */

#pragma once

#include <vector>
#include <string>
#include <cstddef>

namespace noise_toolkit {

/**
 * @brief Ring buffer class for audio event capture
 */
class RingBuffer {
public:
    /**
     * @brief Buffer information structure
     */
    struct Info {
        int sample_rate;
        double buffer_duration_s;
        size_t buffer_size_samples;
        double pretrigger_s;
        size_t pretrigger_samples;
        double posttrigger_s;
        size_t posttrigger_samples;
        int channels;
        bool is_full;
        size_t write_index;
        size_t total_written;
    };
    
    /**
     * @brief Constructor
     */
    RingBuffer(int sample_rate = 48000,
               double buffer_duration_s = 12.0,
               double pretrigger_s = 2.0,
               double posttrigger_s = 8.0,
               int channels = 1);
    
    /**
     * @brief Write samples to buffer
     * @param samples Audio samples
     * @return Number of samples written
     */
    size_t write(const std::vector<double>& samples);
    
    /**
     * @brief Get pre-trigger data
     * @return Audio data from before trigger
     */
    std::vector<double> get_pretrigger_data() const;
    
    /**
     * @brief Get continuous buffer data (chronologically ordered)
     * @return Full buffer in chronological order
     */
    std::vector<double> get_continuous_buffer() const;
    
    /**
     * @brief Save event audio (pre-trigger + post-trigger)
     * @param event_id Event identifier
     * @param posttrigger_data Post-trigger audio data
     * @param output_dir Output directory
     * @return Path to saved file
     */
    std::string save_event_audio(const std::string& event_id,
                                  const std::vector<double>& posttrigger_data,
                                  const std::string& output_dir = "./audio_events");
    
    /**
     * @brief Get buffer information
     */
    Info get_info() const;
    
    /**
     * @brief Clear the buffer
     */
    void clear();
    
    // Accessors
    int get_sample_rate() const { return sample_rate_; }
    size_t get_pretrigger_samples() const { return pretrigger_samples_; }
    size_t get_posttrigger_samples() const { return posttrigger_samples_; }
    bool is_full() const { return is_full_; }

private:
    int sample_rate_;
    double buffer_duration_s_;
    double pretrigger_s_;
    double posttrigger_s_;
    int channels_;
    
    size_t buffer_size_;
    size_t pretrigger_samples_;
    size_t posttrigger_samples_;
    
    std::vector<double> buffer_;
    size_t write_index_;
    bool is_full_;
    size_t total_written_;
};

/**
 * @brief Multi-channel ring buffer manager
 */
class MultiChannelRingBuffer {
public:
    /**
     * @brief Constructor
     */
    MultiChannelRingBuffer(int sample_rate = 48000,
                           double buffer_duration_s = 12.0,
                           double pretrigger_s = 2.0,
                           double posttrigger_s = 8.0,
                           int num_channels = 2);
    
    /**
     * @brief Write multi-channel samples
     */
    size_t write(const std::vector<std::vector<double>>& samples);
    
    /**
     * @brief Get pre-trigger data for specific channel
     */
    std::vector<double> get_pretrigger_data(int channel = 0) const;
    
    /**
     * @brief Save event audio for specific channel
     */
    std::string save_event_audio(const std::string& event_id,
                                  const std::vector<double>& posttrigger_data,
                                  int channel = 0,
                                  const std::string& output_dir = "./audio_events");
    
    /**
     * @brief Clear all channels
     */
    void clear();

private:
    int sample_rate_;
    int num_channels_;
    std::vector<RingBuffer> buffers_;
};

} // namespace noise_toolkit
