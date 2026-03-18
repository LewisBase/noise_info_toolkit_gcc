/**
 * @file wav_reader.hpp
 * @brief WAV file reader
 */

#pragma once

#include "signal_utils.hpp"
#include <string>
#include <vector>

namespace noise_toolkit {

/**
 * @brief WAV file information
 */
struct WAVInfo {
    int sample_rate;
    int channels;
    int bits_per_sample;
    int num_samples;
    double duration;
};

/**
 * @brief WAV file reader class
 */
class WAVReader {
public:
    /**
     * @brief Read WAV file
     * @param file_path Path to WAV file
     * @return Signal object containing audio data
     */
    static Signal read(const std::string& file_path);
    
    /**
     * @brief Read WAV file information only
     */
    static WAVInfo get_info(const std::string& file_path);
    
    /**
     * @brief Check if file is valid WAV
     */
    static bool is_valid_wav(const std::string& file_path);

private:
    struct WAVHeader {
        char riff[4];
        uint32_t file_size;
        char wave[4];
        char fmt[4];
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
    };
};

/**
 * @brief WAV file writer class
 */
class WAVWriter {
public:
    /**
     * @brief Write WAV file
     * @param file_path Output file path
     * @param signal Signal to write
     * @return true on success
     */
    static bool write(const std::string& file_path, const Signal& signal);
    
    /**
     * @brief Write WAV file with specific bit depth
     */
    static bool write(const std::string& file_path, 
                      const Signal& signal,
                      int bits_per_sample);
    
    /**
     * @brief Write raw data as WAV
     */
    static bool write(const std::string& file_path,
                      const std::vector<double>& data,
                      int sample_rate,
                      int channels = 1,
                      int bits_per_sample = 16);
};

} // namespace noise_toolkit
