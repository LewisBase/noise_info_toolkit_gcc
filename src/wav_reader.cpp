/**
 * @file wav_reader.cpp
 * @brief WAV file reader/writer implementation
 */

#include "wav_reader.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace noise_toolkit {

// WAVReader implementation
Signal WAVReader::read(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }
    
    // Read WAV header
    WAVHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // Verify RIFF header
    if (std::string(header.riff, 4) != "RIFF" || 
        std::string(header.wave, 4) != "WAVE") {
        throw std::runtime_error("Invalid WAV file: " + file_path);
    }
    
    // Determine format
    bool is_float = (header.audio_format == 3);
    bool is_pcm = (header.audio_format == 1);
    
    if (!is_float && !is_pcm) {
        throw std::runtime_error("Unsupported audio format: " + std::to_string(header.audio_format));
    }
    
    // Find data chunk
    char chunk_id[4];
    uint32_t chunk_size;
    
    // Skip to data chunk
    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&chunk_size), 4);
        
        if (std::string(chunk_id, 4) == "data") {
            break;
        }
        
        // Skip this chunk
        file.seekg(chunk_size, std::ios::cur);
    }
    
    if (file.eof()) {
        throw std::runtime_error("No data chunk found in WAV file");
    }
    
    // Calculate number of samples
    size_t num_samples = chunk_size / (header.bits_per_sample / 8);
    
    // Read audio data
    std::vector<double> data;
    data.reserve(num_samples);
    
    int bytes_per_sample = header.bits_per_sample / 8;
    
    for (size_t i = 0; i < num_samples; ++i) {
        if (is_float) {
            float sample;
            file.read(reinterpret_cast<char*>(&sample), sizeof(sample));
            data.push_back(static_cast<double>(sample));
        } else {
            // PCM format
            if (header.bits_per_sample == 8) {
                uint8_t sample;
                file.read(reinterpret_cast<char*>(&sample), 1);
                data.push_back((static_cast<double>(sample) - 128.0) / 128.0);
            } else if (header.bits_per_sample == 16) {
                int16_t sample;
                file.read(reinterpret_cast<char*>(&sample), 2);
                data.push_back(static_cast<double>(sample) / 32768.0);
            } else if (header.bits_per_sample == 24) {
                // Read 3 bytes and combine
                char bytes[3];
                file.read(bytes, 3);
                int32_t sample = (static_cast<int8_t>(bytes[2]) << 16) | 
                                (static_cast<uint8_t>(bytes[1]) << 8) | 
                                static_cast<uint8_t>(bytes[0]);
                data.push_back(static_cast<double>(sample) / 8388608.0);
            } else if (header.bits_per_sample == 32) {
                int32_t sample;
                file.read(reinterpret_cast<char*>(&sample), 4);
                data.push_back(static_cast<double>(sample) / 2147483648.0);
            }
        }
    }
    
    file.close();
    
    return Signal(data, static_cast<double>(header.sample_rate), header.num_channels);
}

WAVInfo WAVReader::get_info(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }
    
    WAVHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    file.close();
    
    WAVInfo info;
    info.sample_rate = header.sample_rate;
    info.channels = header.num_channels;
    info.bits_per_sample = header.bits_per_sample;
    
    size_t data_size = (header.file_size - 36) > 0 ? (header.file_size - 36) : 0;
    info.num_samples = data_size / (header.bits_per_sample / 8);
    info.duration = static_cast<double>(info.num_samples) / 
                   (info.sample_rate * info.channels);
    
    return info;
}

bool WAVReader::is_valid_wav(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;
    
    WAVHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    return (std::string(header.riff, 4) == "RIFF" && 
            std::string(header.wave, 4) == "WAVE");
}

// WAVWriter implementation
bool WAVWriter::write(const std::string& file_path, const Signal& signal) {
    return write(file_path, signal, 16);
}

bool WAVWriter::write(const std::string& file_path, const Signal& signal, int bits_per_sample) {
    return write(file_path, signal.data, signal.sample_rate, signal.channels, bits_per_sample);
}

bool WAVWriter::write(const std::string& file_path,
                      const std::vector<double>& data,
                      int sample_rate,
                      int channels,
                      int bits_per_sample) {
    
    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    int bytes_per_sample = bits_per_sample / 8;
    size_t num_samples = data.size();
    uint32_t data_size = num_samples * bytes_per_sample;
    uint32_t file_size = 36 + data_size;
    
    // Write RIFF header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&file_size), 4);
    file.write("WAVE", 4);
    
    // Write fmt chunk
    file.write("fmt ", 4);
    uint32_t fmt_size = 16;
    file.write(reinterpret_cast<const char*>(&fmt_size), 4);
    uint16_t audio_format = (bits_per_sample == 32 && false) ? 3 : 1;  // Use PCM
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    uint16_t num_channels = channels;
    file.write(reinterpret_cast<const char*>(&num_channels), 2);
    file.write(reinterpret_cast<const char*>(&sample_rate), 4);
    uint32_t byte_rate = sample_rate * channels * bytes_per_sample;
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    uint16_t block_align = channels * bytes_per_sample;
    file.write(reinterpret_cast<const char*>(&block_align), 2);
    file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    
    // Write data chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&data_size), 4);
    
    // Write audio samples
    for (double sample : data) {
        // Clamp to [-1, 1]
        double clamped = std::max(-1.0, std::min(1.0, sample));
        
        if (bits_per_sample == 8) {
            uint8_t out = static_cast<uint8_t>((clamped + 1.0) * 127.5);
            file.write(reinterpret_cast<const char*>(&out), 1);
        } else if (bits_per_sample == 16) {
            int16_t out = static_cast<int16_t>(clamped * 32767.0);
            file.write(reinterpret_cast<const char*>(&out), 2);
        } else if (bits_per_sample == 24) {
            int32_t out = static_cast<int32_t>(clamped * 8388607.0);
            char bytes[3] = {static_cast<char>(out & 0xFF),
                            static_cast<char>((out >> 8) & 0xFF),
                            static_cast<char>((out >> 16) & 0xFF)};
            file.write(bytes, 3);
        } else if (bits_per_sample == 32) {
            int32_t out = static_cast<int32_t>(clamped * 2147483647.0);
            file.write(reinterpret_cast<const char*>(&out), 4);
        }
    }
    
    file.close();
    return true;
}

} // namespace noise_toolkit
