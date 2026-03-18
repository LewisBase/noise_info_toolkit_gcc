/**
 * @file ring_buffer.cpp
 * @brief Ring buffer implementation
 */

#include "ring_buffer.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <filesystem>
#include <string>

namespace noise_toolkit {

RingBuffer::RingBuffer(int sample_rate,
                       double buffer_duration_s,
                       double pretrigger_s,
                       double posttrigger_s,
                       int channels)
    : sample_rate_(sample_rate),
      buffer_duration_s_(buffer_duration_s),
      pretrigger_s_(pretrigger_s),
      posttrigger_s_(posttrigger_s),
      channels_(channels),
      write_index_(0),
      is_full_(false),
      total_written_(0) {
    
    buffer_size_ = static_cast<size_t>(sample_rate * buffer_duration_s);
    pretrigger_samples_ = static_cast<size_t>(sample_rate * pretrigger_s);
    posttrigger_samples_ = static_cast<size_t>(sample_rate * posttrigger_s);
    
    buffer_.resize(buffer_size_, 0.0f);
}

size_t RingBuffer::write(const std::vector<double>& samples) {
    size_t samples_to_write = samples.size();
    
    for (size_t i = 0; i < samples_to_write; ++i) {
        buffer_[write_index_] = static_cast<float>(samples[i]);
        write_index_ = (write_index_ + 1) % buffer_size_;
    }
    
    total_written_ += samples_to_write;
    
    if (!is_full_ && total_written_ >= buffer_size_) {
        is_full_ = true;
    }
    
    return samples_to_write;
}

std::vector<double> RingBuffer::get_pretrigger_data() const {
    std::vector<double> result;
    result.reserve(pretrigger_samples_);
    
    if (write_index_ >= pretrigger_samples_) {
        // Continuous data
        for (size_t i = write_index_ - pretrigger_samples_; i < write_index_; ++i) {
            result.push_back(static_cast<double>(buffer_[i]));
        }
    } else {
        // Wrapped data
        size_t first_part = pretrigger_samples_ - write_index_;
        for (size_t i = buffer_size_ - first_part; i < buffer_size_; ++i) {
            result.push_back(static_cast<double>(buffer_[i]));
        }
        for (size_t i = 0; i < write_index_; ++i) {
            result.push_back(static_cast<double>(buffer_[i]));
        }
    }
    
    return result;
}

std::vector<double> RingBuffer::get_continuous_buffer() const {
    std::vector<double> result;
    
    if (!is_full_) {
        // Buffer not full, return written portion
        result.reserve(write_index_);
        for (size_t i = 0; i < write_index_; ++i) {
            result.push_back(static_cast<double>(buffer_[i]));
        }
    } else {
        // Buffer full, reorder
        result.reserve(buffer_size_);
        for (size_t i = write_index_; i < buffer_size_; ++i) {
            result.push_back(static_cast<double>(buffer_[i]));
        }
        for (size_t i = 0; i < write_index_; ++i) {
            result.push_back(static_cast<double>(buffer_[i]));
        }
    }
    
    return result;
}

std::string RingBuffer::save_event_audio(const std::string& event_id,
                                          const std::vector<double>& posttrigger_data,
                                          const std::string& output_dir) {
    // Create output directory
    std::filesystem::path output_path(output_dir);
    std::filesystem::create_directories(output_path);
    
    // Get pre-trigger data
    std::vector<double> pretrigger_data = get_pretrigger_data();
    
    // Merge data
    std::vector<double> full_event_audio;
    full_event_audio.reserve(pretrigger_data.size() + posttrigger_data.size());
    full_event_audio.insert(full_event_audio.end(), pretrigger_data.begin(), pretrigger_data.end());
    full_event_audio.insert(full_event_audio.end(), posttrigger_data.begin(), posttrigger_data.end());
    
    // Prevent clipping
    double max_val = 0.0;
    for (double x : full_event_audio) {
        max_val = std::max(max_val, std::abs(x));
    }
    if (max_val > 1.0) {
        for (double& x : full_event_audio) {
            x = x / max_val * 0.95;
        }
    }
    
    // Generate filename
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    std::string timestamp = ss.str();
    
    std::string filename = event_id + "_" + timestamp + ".wav";
    std::filesystem::path filepath = output_path / filename;
    
    // Write WAV file (simplified - raw PCM, no headers for now)
    // In production, use proper WAV writer with headers
    std::ofstream file(filepath, std::ios::binary);
    if (file.is_open()) {
        // Write simple header (44 bytes for standard WAV)
        // This is a simplified version
        file.write("RIFF", 4);
        int32_t file_size = 36 + full_event_audio.size() * 2;
        file.write(reinterpret_cast<const char*>(&file_size), 4);
        file.write("WAVE", 4);
        file.write("fmt ", 4);
        int32_t fmt_size = 16;
        file.write(reinterpret_cast<const char*>(&fmt_size), 4);
        int16_t audio_format = 1;  // PCM
        file.write(reinterpret_cast<const char*>(&audio_format), 2);
        int16_t num_channels = 1;
        file.write(reinterpret_cast<const char*>(&num_channels), 2);
        int32_t sample_rate = sample_rate_;
        file.write(reinterpret_cast<const char*>(&sample_rate), 4);
        int32_t byte_rate = sample_rate_ * 2;
        file.write(reinterpret_cast<const char*>(&byte_rate), 4);
        int16_t block_align = 2;
        file.write(reinterpret_cast<const char*>(&block_align), 2);
        int16_t bits_per_sample = 16;
        file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
        file.write("data", 4);
        int32_t data_size = full_event_audio.size() * 2;
        file.write(reinterpret_cast<const char*>(&data_size), 4);
        
        // Write audio data (16-bit PCM)
        for (double sample : full_event_audio) {
            int16_t pcm_sample = static_cast<int16_t>(sample * 32767.0);
            file.write(reinterpret_cast<const char*>(&pcm_sample), 2);
        }
        
        file.close();
    }
    
    return filepath.string();
}

RingBuffer::Info RingBuffer::get_info() const {
    Info info;
    info.sample_rate = sample_rate_;
    info.buffer_duration_s = buffer_duration_s_;
    info.buffer_size_samples = buffer_size_;
    info.pretrigger_s = pretrigger_s_;
    info.pretrigger_samples = pretrigger_samples_;
    info.posttrigger_s = posttrigger_s_;
    info.posttrigger_samples = posttrigger_samples_;
    info.channels = channels_;
    info.is_full = is_full_;
    info.write_index = write_index_;
    info.total_written = total_written_;
    return info;
}

void RingBuffer::clear() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    write_index_ = 0;
    is_full_ = false;
    total_written_ = 0;
}

// MultiChannelRingBuffer implementation
MultiChannelRingBuffer::MultiChannelRingBuffer(int sample_rate,
                                                double buffer_duration_s,
                                                double pretrigger_s,
                                                double posttrigger_s,
                                                int num_channels)
    : sample_rate_(sample_rate),
      num_channels_(num_channels) {
    
    for (int i = 0; i < num_channels; ++i) {
        buffers_.emplace_back(sample_rate, buffer_duration_s, pretrigger_s, 
                             posttrigger_s, 1);
    }
}

size_t MultiChannelRingBuffer::write(const std::vector<std::vector<double>>& samples) {
    if (samples.empty()) return 0;
    
    size_t samples_written = 0;
    for (int ch = 0; ch < std::min(num_channels_, static_cast<int>(samples.size())); ++ch) {
        samples_written = buffers_[ch].write(samples[ch]);
    }
    
    return samples_written;
}

std::vector<double> MultiChannelRingBuffer::get_pretrigger_data(int channel) const {
    if (channel >= 0 && channel < num_channels_) {
        return buffers_[channel].get_pretrigger_data();
    }
    return {};
}

std::string MultiChannelRingBuffer::save_event_audio(const std::string& event_id,
                                                      const std::vector<double>& posttrigger_data,
                                                      int channel,
                                                      const std::string& output_dir) {
    if (channel >= 0 && channel < num_channels_) {
        return buffers_[channel].save_event_audio(event_id, posttrigger_data, output_dir);
    }
    return "";
}

void MultiChannelRingBuffer::clear() {
    for (auto& buf : buffers_) {
        buf.clear();
    }
}

} // namespace noise_toolkit
