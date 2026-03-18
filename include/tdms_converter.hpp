/**
 * @file tdms_converter.hpp
 * @brief TDMS to WAV converter
 */

#pragma once

#include "noise_toolkit.hpp"
#include <string>
#include <vector>
#include <map>

namespace noise_toolkit {

/**
 * @brief TDMS channel data
 */
struct TDMSChannel {
    std::string name;
    std::vector<double> data;
    double sample_rate;
    std::map<std::string, std::string> properties;
};

/**
 * @brief TDMS group data
 */
struct TDMSGroup {
    std::string name;
    std::vector<TDMSChannel> channels;
};

/**
 * @brief TDMS file data
 */
struct TDMSFile {
    std::string file_path;
    std::vector<TDMSGroup> groups;
    std::map<std::string, std::string> properties;
};

/**
 * @brief TDMS to WAV converter class
 * 
 * Note: This is a simplified interface. Full TDMS parsing requires
 * either external library or custom implementation of TDMS format.
 */
class TDMSConverter {
public:
    /**
     * @brief Constructor
     */
    TDMSConverter();
    
    /**
     * @brief Convert TDMS file to WAV format
     * @param tdms_file_path Input TDMS file path
     * @param wav_file_path Output WAV file path (if empty, auto-generate)
     * @param sampling_rate Desired sampling rate (uses TDMS rate if available)
     * @return Path to converted WAV file
     */
    std::string convert_tdms_to_wav(const std::string& tdms_file_path,
                                    const std::string& wav_file_path = "",
                                    int sampling_rate = 44100);
    
    /**
     * @brief Batch convert all TDMS files in directory
     * @param tdms_directory Input directory
     * @param wav_directory Output directory (if empty, same as input)
     * @return List of converted file paths
     */
    std::vector<std::string> batch_convert_tdms_files(
        const std::string& tdms_directory,
        const std::string& wav_directory = "");
    
    /**
     * @brief Read TDMS file structure (metadata only)
     * @return TDMS file structure
     */
    TDMSFile read_tdms_structure(const std::string& tdms_file_path);
    
    /**
     * @brief Get last error message
     */
    std::string get_last_error() const { return last_error_; }

private:
    std::string last_error_;
    
    bool write_wav_file(const std::string& file_path,
                        const std::vector<double>& data,
                        int sample_rate);
};

} // namespace noise_toolkit
