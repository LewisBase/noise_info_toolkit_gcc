/**
 * @file database.hpp
 * @brief Database interface for noise info toolkit
 * 
 * Maintains table structure compatible with Python implementation
 */

#pragma once

#include "noise_toolkit.hpp"
#include "time_history_processor.hpp"
#include "event_detector.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>

namespace noise_toolkit {

/**
 * @brief Dose profile database record
 */
struct DoseProfileRecord {
    int id;
    std::string profile_name;
    double criterion_level_dBA;
    double exchange_rate_dB;
    double threshold_dBA;
    double reference_duration_h;
    std::string description;
    std::string created_at;
};

/**
 * @brief Time history database record
 */
struct TimeHistoryRecord {
    int id;
    std::string session_id;
    std::string device_id;
    std::string profile_name;
    std::string timestamp_utc;
    double duration_s;
    double LAeq_dB;
    double LCeq_dB;
    double LZeq_dB;
    std::optional<double> LAFmax_dB;
    double LZpeak_dB;
    double LCpeak_dB;
    double dose_frac_niosh;
    double dose_frac_osha_pel;
    double dose_frac_osha_hca;
    double dose_frac_eu_iso;
    bool wearing_state;
    bool overload_flag;
    bool underrange_flag;
    std::optional<double> temp_C;
    std::optional<double> humidity_pct;
    std::optional<double> pressure_hPa;
};

/**
 * @brief Event log database record
 */
struct EventLogRecord {
    int id;
    std::string session_id;
    std::string event_id;
    std::string start_time_utc;
    std::optional<std::string> end_time_utc;
    double duration_s;
    std::string trigger_type;
    double LZpeak_dB;
    double LCpeak_dB;
    double LAeq_event_dB;
    double SEL_LAE_dB;
    std::optional<double> beta_excess_event_Z;
    std::optional<std::string> audio_file_path;
    double pretrigger_s;
    double posttrigger_s;
    std::optional<std::string> notes;
};

/**
 * @brief Session summary database record
 */
struct SessionSummaryRecord {
    int id;
    std::string session_id;
    std::string profile_name;
    std::string start_time_utc;
    std::optional<std::string> end_time_utc;
    double total_duration_h;
    double LAeq_T;
    double LEX_8h;
    double total_dose_pct;
    double TWA;
    double peak_max_dB;
    std::optional<double> LCpeak_max_dB;
    int events_count;
    int overload_count;
    int underrange_count;
};

/**
 * @brief Metadata database record
 */
struct MetadataRecord {
    int id;
    std::string session_id;
    std::string start_time_utc;
    std::optional<std::string> end_time_utc;
    std::optional<std::string> organization;
    std::optional<std::string> operator_name;
    std::optional<std::string> worker_role;
    std::optional<std::string> device_model;
    std::optional<std::string> device_serial;
    std::optional<std::string> mic_type;
    std::optional<double> mic_diameter_inch;
    std::optional<double> mic_sensitivity_mV_Pa;
    int sampling_rate_Hz;
    int bit_depth;
    std::optional<double> calibration_level_dB;
    std::optional<std::string> calibration_time_utc;
    std::optional<double> post_cal_level_dB;
    std::optional<std::string> compliance_standards;
    std::string data_format_version;
    std::optional<std::string> notes;
};

/**
 * @brief Database class
 */
class Database {
public:
    /**
     * @brief Constructor
     */
    Database();
    
    /**
     * @brief Destructor
     */
    ~Database();
    
    /**
     * @brief Open database connection
     */
    bool open(const std::string& db_path);
    
    /**
     * @brief Close database connection
     */
    void close();
    
    /**
     * @brief Initialize database schema
     */
    bool initialize_schema();
    
    /**
     * @brief Check if database is open
     */
    bool is_open() const { return db_ != nullptr; }
    
    // Dose profile operations
    bool insert_dose_profile(const DoseProfileRecord& record);
    std::vector<DoseProfileRecord> get_all_dose_profiles();
    
    // Time history operations
    bool insert_time_history(const TimeHistoryRecord& record);
    bool insert_time_history_batch(const std::vector<TimeHistoryRecord>& records);
    std::vector<TimeHistoryRecord> get_time_history_by_session(const std::string& session_id);
    
    // Event log operations
    bool insert_event_log(const EventLogRecord& record);
    std::vector<EventLogRecord> get_event_log_by_session(const std::string& session_id);
    
    // Session summary operations
    bool insert_session_summary(const SessionSummaryRecord& record);
    bool update_session_summary(const SessionSummaryRecord& record);
    std::optional<SessionSummaryRecord> get_session_summary(const std::string& session_id);
    
    // Metadata operations
    bool insert_metadata(const MetadataRecord& record);
    std::optional<MetadataRecord> get_metadata(const std::string& session_id);
    
    // Utility
    bool execute_sql(const std::string& sql);

private:
    sqlite3* db_;
    
    bool create_tables();
};

} // namespace noise_toolkit
