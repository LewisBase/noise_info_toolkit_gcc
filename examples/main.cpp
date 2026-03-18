/**
 * @file main.cpp
 * @brief Example usage of noise_info_toolkit C++ library
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>

#include "noise_toolkit.hpp"
#include "audio_processor.hpp"
#include "dose_calculator.hpp"
#include "event_detector.hpp"
#include "event_processor.hpp"
#include "time_history_processor.hpp"
#include "ring_buffer.hpp"
#include "wav_reader.hpp"
#include "database.hpp"

using namespace noise_toolkit;

// Helper function to generate test signal
std::vector<double> generate_test_signal(double duration_s, double sample_rate, double db_level = 80.0) {
    size_t num_samples = static_cast<size_t>(duration_s * sample_rate);
    std::vector<double> signal(num_samples);
    
    // Generate white noise at specified dB level
    double rms = db_to_pressure(db_level);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> dis(0.0, rms);
    
    for (size_t i = 0; i < num_samples; ++i) {
        signal[i] = dis(gen);
    }
    
    return signal;
}

void print_noise_metrics(const NoiseMetrics& metrics) {
    std::cout << "\n========== Noise Metrics ==========\n";
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "\n--- Overall Sound Levels ---\n";
    std::cout << "Leq:    " << metrics.leq << " dB\n";
    std::cout << "LAeq:   " << metrics.laeq << " dB\n";
    std::cout << "LCeq:   " << metrics.lceq << " dB\n";
    
    std::cout << "\n--- Peak Levels ---\n";
    std::cout << "Peak SPL:     " << metrics.peak_spl << " dB\n";
    std::cout << "Peak ASPL:    " << metrics.peak_aspl << " dB\n";
    std::cout << "Peak CSPL:    " << metrics.peak_cspl << " dB\n";
    
    std::cout << "\n--- Kurtosis ---\n";
    std::cout << "Total Kurtosis:       " << metrics.total_kurtosis << "\n";
    std::cout << "A-weighted Kurtosis:  " << metrics.a_weighted_kurtosis << "\n";
    std::cout << "C-weighted Kurtosis:  " << metrics.c_weighted_kurtosis << "\n";
    
    std::cout << "\n--- Dose Metrics ---\n";
    std::cout << "NIOSH Dose:    " << metrics.dose_niosh << " %\n";
    std::cout << "OSHA_PEL Dose: " << metrics.dose_osha_pel << " %\n";
    std::cout << "OSHA_HCA Dose: " << metrics.dose_osha_hca << " %\n";
    std::cout << "EU_ISO Dose:   " << metrics.dose_eu_iso << " %\n";
    
    std::cout << "\n--- TWA ---\n";
    std::cout << "NIOSH TWA:    " << metrics.twa_niosh << " dBA\n";
    std::cout << "OSHA_PEL TWA: " << metrics.twa_osha_pel << " dBA\n";
    std::cout << "OSHA_HCA TWA: " << metrics.twa_osha_hca << " dBA\n";
    std::cout << "EU_ISO TWA:   " << metrics.twa_eu_iso << " dBA\n";
    
    std::cout << "\n--- LEX,8h ---\n";
    std::cout << "NIOSH LEX,8h:    " << metrics.lex_niosh << " dBA\n";
    std::cout << "OSHA_PEL LEX,8h: " << metrics.lex_osha_pel << " dBA\n";
    std::cout << "OSHA_HCA LEX,8h: " << metrics.lex_osha_hca << " dBA\n";
    std::cout << "EU_ISO LEX,8h:   " << metrics.lex_eu_iso << " dBA\n";
    
    std::cout << "\n--- Signal Info ---\n";
    std::cout << "Duration: " << metrics.duration << " s\n";
    std::cout << "Sample Rate: " << metrics.sampling_rate << " Hz\n";
    std::cout << "Channels: " << metrics.channels << "\n";
    
    std::cout << "====================================\n";
}

void test_dose_calculator() {
    std::cout << "\n========== Testing Dose Calculator ==========\n";
    
    DoseCalculator calc;
    
    // Test single dose calculation
    double laeq = 85.0;
    double duration_h = 8.0;
    double duration_s = duration_h * 3600.0;
    
    std::cout << "\n--- Single Measurement ---\n";
    std::cout << "LAeq: " << laeq << " dBA, Duration: " << duration_h << " hours\n";
    
    auto profile = calc.get_profile("NIOSH");
    double dose = calc.calculate_dose_increment(laeq, duration_s, profile);
    double allowed_time = calc.calculate_allowed_time(laeq, profile);
    double twa = calc.calculate_twa(dose, profile);
    double lex = calc.calculate_lex(dose, profile);
    
    std::cout << "Dose: " << dose << " %\n";
    std::cout << "Allowed Time: " << allowed_time << " hours\n";
    std::cout << "TWA: " << twa << " dBA\n";
    std::cout << "LEX,8h: " << lex << " dBA\n";
    
    // Test multi-standard
    std::cout << "\n--- Multi-Standard Results ---\n";
    auto results = calc.calculate_multi_standard(laeq, duration_s);
    for (const auto& [name, metrics] : results) {
        std::cout << name << ": Dose=" << metrics.dose_pct << "%, "
                  << "TWA=" << metrics.twa << " dBA, "
                  << "LEX=" << metrics.lex_8h << " dBA\n";
    }
    
    std::cout << "========================================\n";
}

void test_audio_processor() {
    std::cout << "\n========== Testing Audio Processor ==========\n";
    
    // Generate test signal (10 seconds of noise at 85 dB)
    double duration = 10.0;
    double sample_rate = 48000.0;
    double db_level = 85.0;
    
    std::cout << "Generating test signal: " << duration << "s at " << db_level << " dB\n";
    auto signal_data = generate_test_signal(duration, sample_rate, db_level);
    
    Signal signal(signal_data, sample_rate, 1);
    
    AudioProcessor processor;
    NoiseMetrics metrics = processor.process_signal(signal);
    
    print_noise_metrics(metrics);
    
    std::cout << "========================================\n";
}

void test_time_history() {
    std::cout << "\n========== Testing Time History Processor ==========\n";
    
    // Generate test signal (60 seconds)
    double duration = 60.0;
    double sample_rate = 48000.0;
    double db_level = 80.0;
    
    std::cout << "Generating test signal: " << duration << "s at " << db_level << " dB\n";
    auto signal_data = generate_test_signal(duration, sample_rate, db_level);
    
    Signal signal(signal_data, sample_rate, 1);
    
    TimeHistoryProcessor processor;
    auto time_history = processor.process_signal_per_second(signal);
    
    std::cout << "Processed " << time_history.size() << " seconds of data\n";
    
    // Print first few seconds
    std::cout << "\n--- First 5 seconds ---\n";
    for (size_t i = 0; i < std::min(size_t(5), time_history.size()); ++i) {
        const auto& m = time_history[i];
        std::cout << "t=" << i << "s: LAeq=" << m.LAeq << " dB, "
                  << "LZpeak=" << m.LZpeak << " dB, "
                  << "dose_niosh=" << (m.dose_frac_niosh * 100) << "%\n";
    }
    
    // Aggregate session metrics
    auto session_metrics = TimeHistoryProcessor::aggregate_session_metrics(
        time_history, DoseStandard::NIOSH);
    
    std::cout << "\n--- Session Summary ---\n";
    std::cout << "Total Duration: " << session_metrics.total_duration_h << " hours\n";
    std::cout << "Total Dose: " << session_metrics.total_dose_pct << " %\n";
    std::cout << "LAeq,T: " << session_metrics.LAeq_T << " dB\n";
    std::cout << "TWA: " << session_metrics.TWA << " dBA\n";
    std::cout << "LEX,8h: " << session_metrics.LEX_8h << " dBA\n";
    std::cout << "Peak Max: " << session_metrics.peak_max_dB << " dB\n";
    
    std::cout << "========================================\n";
}

void test_ring_buffer() {
    std::cout << "\n========== Testing Ring Buffer ==========\n";
    
    RingBuffer buffer(48000, 12.0, 2.0, 8.0, 1);
    
    // Write 10 seconds of data
    std::cout << "Writing 10 seconds of test data...\n";
    auto data = generate_test_signal(10.0, 48000, 80.0);
    buffer.write(data);
    
    auto info = buffer.get_info();
    std::cout << "Buffer is " << (info.is_full ? "full" : "not full") << "\n";
    std::cout << "Total written: " << info.total_written << " samples\n";
    
    // Get pre-trigger data
    auto pretrigger = buffer.get_pretrigger_data();
    std::cout << "Pre-trigger data: " << pretrigger.size() << " samples ("
              << (pretrigger.size() / 48000.0) << "s)\n";
    
    std::cout << "========================================\n";
}

void test_database() {
    std::cout << "\n========== Testing Database ==========\n";
    
    Database db;
    
    // Open in-memory database for testing
    if (db.open(":memory:")) {
        std::cout << "Database opened successfully\n";
        
        if (db.initialize_schema()) {
            std::cout << "Schema initialized successfully\n";
            
            // Test time history insert
            TimeHistoryRecord record;
            record.session_id = "test_session_001";
            record.device_id = "device_001";
            record.profile_name = "NIOSH";
            record.timestamp_utc = "2026-03-14T10:00:00";
            record.duration_s = 1.0;
            record.LAeq_dB = 85.0;
            record.LCeq_dB = 85.5;
            record.LZeq_dB = 86.0;
            record.LAFmax_dB = 90.0;
            record.LZpeak_dB = 100.0;
            record.LCpeak_dB = 99.0;
            record.dose_frac_niosh = 0.5;
            record.dose_frac_osha_pel = 0.25;
            record.dose_frac_osha_hca = 0.5;
            record.dose_frac_eu_iso = 0.5;
            record.wearing_state = true;
            record.overload_flag = false;
            record.underrange_flag = false;
            
            if (db.insert_time_history(record)) {
                std::cout << "Time history record inserted successfully\n";
            } else {
                std::cout << "Failed to insert time history record\n";
            }
        }
        
        db.close();
    } else {
        std::cout << "Failed to open database\n";
    }
    
    std::cout << "========================================\n";
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <test_name> [wav_file]\n";
    std::cout << "\nAvailable tests:\n";
    std::cout << "  dose          - Test dose calculator\n";
    std::cout << "  audio         - Test audio processor\n";
    std::cout << "  time          - Test time history processor\n";
    std::cout << "  buffer        - Test ring buffer\n";
    std::cout << "  database      - Test database\n";
    std::cout << "  all           - Run all tests\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program_name << " audio test.wav\n";
}

int main(int argc, char* argv[]) {
    std::cout << "================================================\n";
    std::cout << "  Noise Info Toolkit C++ Implementation\n";
    std::cout << "  Version 1.0.0\n";
    std::cout << "================================================\n";
    
    if (argc < 2) {
        // Run all tests by default
        test_dose_calculator();
        test_audio_processor();
        test_time_history();
        test_ring_buffer();
        test_database();
        return 0;
    }
    
    std::string test_name = argv[1];
    
    if (test_name == "dose") {
        test_dose_calculator();
    } else if (test_name == "audio") {
        if (argc > 2) {
            // Process actual WAV file
            std::cout << "Processing WAV file: " << argv[2] << "\n";
            try {
                auto signal = WAVReader::read(argv[2]);
                AudioProcessor processor;
                auto metrics = processor.process_signal(signal);
                print_noise_metrics(metrics);
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return 1;
            }
        } else {
            test_audio_processor();
        }
    } else if (test_name == "time") {
        test_time_history();
    } else if (test_name == "buffer") {
        test_ring_buffer();
    } else if (test_name == "database") {
        test_database();
    } else if (test_name == "all") {
        test_dose_calculator();
        test_audio_processor();
        test_time_history();
        test_ring_buffer();
        test_database();
    } else {
        std::cerr << "Unknown test: " << test_name << "\n";
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
