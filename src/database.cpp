/**
 * @file database.cpp
 * @brief Database implementation
 */

#include "database.hpp"
#include <sstream>
#include <iostream>

namespace noise_toolkit {

Database::Database() : db_(nullptr) {}

Database::~Database() {
    close();
}

bool Database::open(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return false;
    }
    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::initialize_schema() {
    return create_tables();
}

bool Database::create_tables() {
    const char* create_dose_profiles = R"(
        CREATE TABLE IF NOT EXISTS dose_profiles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            profile_name TEXT UNIQUE NOT NULL,
            criterion_level_dBA REAL DEFAULT 85.0,
            exchange_rate_dB REAL DEFAULT 3.0,
            threshold_dBA REAL DEFAULT 0.0,
            reference_duration_h REAL DEFAULT 8.0,
            description TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
    const char* create_time_history = R"(
        CREATE TABLE IF NOT EXISTS time_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            device_id TEXT,
            profile_name TEXT,
            timestamp_utc TIMESTAMP NOT NULL,
            duration_s REAL DEFAULT 1.0,
            LAeq_dB REAL,
            LCeq_dB REAL,
            LZeq_dB REAL,
            LAFmax_dB REAL,
            LZpeak_dB REAL,
            LCpeak_dB REAL,
            dose_frac_niosh REAL DEFAULT 0.0,
            dose_frac_osha_pel REAL DEFAULT 0.0,
            dose_frac_osha_hca REAL DEFAULT 0.0,
            dose_frac_eu_iso REAL DEFAULT 0.0,
            wearing_state BOOLEAN DEFAULT 1,
            overload_flag BOOLEAN DEFAULT 0,
            underrange_flag BOOLEAN DEFAULT 0,
            temp_C REAL,
            humidity_pct REAL,
            pressure_hPa REAL
        );
    )";
    
    const char* create_event_log = R"(
        CREATE TABLE IF NOT EXISTS event_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            event_id TEXT NOT NULL,
            start_time_utc TIMESTAMP NOT NULL,
            end_time_utc TIMESTAMP,
            duration_s REAL,
            trigger_type TEXT,
            LZpeak_dB REAL,
            LCpeak_dB REAL,
            LAeq_event_dB REAL,
            SEL_LAE_dB REAL,
            beta_excess_event_Z REAL,
            audio_file_path TEXT,
            pretrigger_s REAL DEFAULT 2.0,
            posttrigger_s REAL DEFAULT 8.0,
            notes TEXT
        );
    )";
    
    const char* create_session_summary = R"(
        CREATE TABLE IF NOT EXISTS session_summary (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT UNIQUE NOT NULL,
            profile_name TEXT,
            start_time_utc TIMESTAMP NOT NULL,
            end_time_utc TIMESTAMP,
            total_duration_h REAL,
            LAeq_T REAL,
            LEX_8h REAL,
            total_dose_pct REAL,
            TWA REAL,
            peak_max_dB REAL,
            LCpeak_max_dB REAL,
            events_count INTEGER DEFAULT 0,
            overload_count INTEGER DEFAULT 0,
            underrange_count INTEGER DEFAULT 0
        );
    )";
    
    const char* create_metadata = R"(
        CREATE TABLE IF NOT EXISTS metadata (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT UNIQUE NOT NULL,
            start_time_utc TIMESTAMP NOT NULL,
            end_time_utc TIMESTAMP,
            organization TEXT,
            operator TEXT,
            worker_role TEXT,
            device_model TEXT,
            device_serial TEXT,
            mic_type TEXT,
            mic_diameter_inch REAL,
            mic_sensitivity_mV_Pa REAL,
            sampling_rate_Hz INTEGER DEFAULT 48000,
            bit_depth INTEGER DEFAULT 24,
            calibration_level_dB REAL,
            calibration_time_utc TIMESTAMP,
            post_cal_level_dB REAL,
            compliance_standards TEXT,
            data_format_version TEXT DEFAULT '1.0',
            notes TEXT
        );
    )";
    
    // Execute all table creation statements
    if (!execute_sql(create_dose_profiles)) return false;
    if (!execute_sql(create_time_history)) return false;
    if (!execute_sql(create_event_log)) return false;
    if (!execute_sql(create_session_summary)) return false;
    if (!execute_sql(create_metadata)) return false;
    
    // Create indexes
    execute_sql("CREATE INDEX IF NOT EXISTS idx_time_history_session ON time_history(session_id);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_time_history_timestamp ON time_history(timestamp_utc);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_event_log_session ON event_log(session_id);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_event_log_event_id ON event_log(event_id);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_session_summary_session ON session_summary(session_id);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_metadata_session ON metadata(session_id);");
    
    // Insert default dose profiles
    const char* insert_profiles = R"(
        INSERT OR IGNORE INTO dose_profiles 
        (profile_name, criterion_level_dBA, exchange_rate_dB, reference_duration_h, description)
        VALUES 
        ('NIOSH', 85.0, 3.0, 8.0, 'NIOSH标准: 85dBA准则级, 3dB交换率, 8小时参考时长'),
        ('OSHA_PEL', 90.0, 5.0, 8.0, 'OSHA_PEL标准: 90dBA准则级, 5dB交换率, 8小时参考时长'),
        ('OSHA_HCA', 85.0, 5.0, 8.0, 'OSHA_HCA标准: 85dBA准则级, 5dB交换率, 8小时参考时长'),
        ('EU_ISO', 85.0, 3.0, 8.0, 'EU_ISO标准: 85dBA准则级, 3dB交换率, 8小时参考时长');
    )";
    
    execute_sql(insert_profiles);
    
    return true;
}

bool Database::execute_sql(const std::string& sql) {
    if (!db_) return false;
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

// Time history operations
bool Database::insert_time_history(const TimeHistoryRecord& record) {
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT INTO time_history 
        (session_id, device_id, profile_name, timestamp_utc, duration_s,
         LAeq_dB, LCeq_dB, LZeq_dB, LAFmax_dB, LZpeak_dB, LCpeak_dB,
         dose_frac_niosh, dose_frac_osha_pel, dose_frac_osha_hca, dose_frac_eu_iso,
         wearing_state, overload_flag, underrange_flag, temp_C, humidity_pct, pressure_hPa)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, record.session_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record.device_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record.profile_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, record.timestamp_utc.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, record.duration_s);
    sqlite3_bind_double(stmt, 6, record.LAeq_dB);
    sqlite3_bind_double(stmt, 7, record.LCeq_dB);
    sqlite3_bind_double(stmt, 8, record.LZeq_dB);
    sqlite3_bind_double(stmt, 9, record.LAFmax_dB.value_or(0.0));
    sqlite3_bind_double(stmt, 10, record.LZpeak_dB);
    sqlite3_bind_double(stmt, 11, record.LCpeak_dB);
    sqlite3_bind_double(stmt, 12, record.dose_frac_niosh);
    sqlite3_bind_double(stmt, 13, record.dose_frac_osha_pel);
    sqlite3_bind_double(stmt, 14, record.dose_frac_osha_hca);
    sqlite3_bind_double(stmt, 15, record.dose_frac_eu_iso);
    sqlite3_bind_int(stmt, 16, record.wearing_state ? 1 : 0);
    sqlite3_bind_int(stmt, 17, record.overload_flag ? 1 : 0);
    sqlite3_bind_int(stmt, 18, record.underrange_flag ? 1 : 0);
    sqlite3_bind_double(stmt, 19, record.temp_C.value_or(0.0));
    sqlite3_bind_double(stmt, 20, record.humidity_pct.value_or(0.0));
    sqlite3_bind_double(stmt, 21, record.pressure_hPa.value_or(0.0));
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::insert_time_history_batch(const std::vector<TimeHistoryRecord>& records) {
    if (!db_) return false;
    
    // Begin transaction
    execute_sql("BEGIN TRANSACTION;");
    
    bool success = true;
    for (const auto& record : records) {
        if (!insert_time_history(record)) {
            success = false;
            break;
        }
    }
    
    if (success) {
        execute_sql("COMMIT;");
    } else {
        execute_sql("ROLLBACK;");
    }
    
    return success;
}

// Event log operations
bool Database::insert_event_log(const EventLogRecord& record) {
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT INTO event_log 
        (session_id, event_id, start_time_utc, end_time_utc, duration_s,
         trigger_type, LZpeak_dB, LCpeak_dB, LAeq_event_dB, SEL_LAE_dB,
         beta_excess_event_Z, audio_file_path, pretrigger_s, posttrigger_s, notes)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, record.session_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record.event_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record.start_time_utc.c_str(), -1, SQLITE_STATIC);
    if (record.end_time_utc.has_value()) {
        sqlite3_bind_text(stmt, 4, record.end_time_utc.value().c_str(), -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 4);
    }
    sqlite3_bind_double(stmt, 5, record.duration_s);
    sqlite3_bind_text(stmt, 6, record.trigger_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 7, record.LZpeak_dB);
    sqlite3_bind_double(stmt, 8, record.LCpeak_dB);
    sqlite3_bind_double(stmt, 9, record.LAeq_event_dB);
    sqlite3_bind_double(stmt, 10, record.SEL_LAE_dB);
    sqlite3_bind_double(stmt, 11, record.beta_excess_event_Z.value_or(0.0));
    if (record.audio_file_path.has_value()) {
        sqlite3_bind_text(stmt, 12, record.audio_file_path.value().c_str(), -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 12);
    }
    sqlite3_bind_double(stmt, 13, record.pretrigger_s);
    sqlite3_bind_double(stmt, 14, record.posttrigger_s);
    if (record.notes.has_value()) {
        sqlite3_bind_text(stmt, 15, record.notes.value().c_str(), -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 15);
    }
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

} // namespace noise_toolkit
