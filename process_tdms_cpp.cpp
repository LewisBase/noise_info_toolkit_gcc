/**
 * @file dose_validator.cpp
 * @brief C++版本 - TDMS文件噪声分析（验证用）
 * 输入: LAeq=78.540761 dBA, Duration=60秒
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>
#include <limits>
#include <string>

// ==================== C++版本核心代码 ====================

const double REFERENCE_PRESSURE = 20e-6;

struct DoseProfile {
    std::string name;
    double criterion_level;  // Lc (dBA)
    double exchange_rate;    // ER (dB)
    double threshold;       // LT (dBA)
    double reference_duration;  // Tref (hours)
    
    DoseProfile() : name(""), criterion_level(0), exchange_rate(0), 
                    threshold(0), reference_duration(0) {}
    
    DoseProfile(std::string n, double lc, double er, double lt, double tref)
        : name(n), criterion_level(lc), exchange_rate(er), 
          threshold(lt), reference_duration(tref) {}
};

class DoseCalculator {
public:
    std::map<std::string, DoseProfile> profiles_;
    
    DoseCalculator() {
        profiles_["NIOSH"] = DoseProfile("NIOSH", 85.0, 3.0, 0.0, 8.0);
        profiles_["OSHA_PEL"] = DoseProfile("OSHA_PEL", 90.0, 5.0, 0.0, 8.0);
        profiles_["OSHA_HCA"] = DoseProfile("OSHA_HCA", 85.0, 5.0, 0.0, 8.0);
        profiles_["EU_ISO"] = DoseProfile("EU_ISO", 85.0, 3.0, 0.0, 8.0);
    }
    
    DoseProfile get_profile(const std::string& standard) const {
        return profiles_.at(standard);
    }
    
    double calculate_dose_increment(double laeq, double duration_s, const DoseProfile& profile) const {
        if (laeq < profile.threshold) {
            return 0.0;
        }
        
        double duration_h = duration_s / 3600.0;
        double exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
        double dose_increment = 100.0 * (duration_h / profile.reference_duration) * 
                               std::pow(2.0, exponent);
        
        return dose_increment;
    }
    
    double calculate_twa(double total_dose_pct, const DoseProfile& profile) const {
        if (total_dose_pct <= 0.0) {
            return 0.0;
        }
        
        double twa;
        if (profile.name.find("OSHA") == 0) {
            twa = 16.61 * std::log10(total_dose_pct / 100.0) + profile.criterion_level;
        } else {
            twa = 10.0 * std::log10(total_dose_pct / 100.0) + profile.criterion_level;
        }
        
        return twa;
    }
    
    double calculate_lex(double total_dose_pct, const DoseProfile& profile) const {
        if (total_dose_pct <= 0.0) {
            return 0.0;
        }
        
        double lex = 10.0 * std::log10(total_dose_pct / 100.0) + profile.criterion_level;
        return lex;
    }
};

// ==================== 主程序 ====================

int main() {
    std::cout << "======================================================================\n";
    std::cout << "C++版本 - TDMS文件噪声分析\n";
    std::cout << "======================================================================\n\n";
    
    // 输入参数（来自Python版本的计算结果）
    double laeq = 78.540761;  // dBA
    double duration_s = 60.0;  // 秒
    
    std::cout << "输入参数:\n";
    std::cout << "  LAeq = " << laeq << " dBA\n";
    std::cout << "  Duration = " << duration_s << " 秒\n\n";
    
    DoseCalculator calc;
    
    std::string standards[] = {"NIOSH", "OSHA_PEL", "OSHA_HCA", "EU_ISO"};
    
    std::cout << "======================================================================\n";
    std::cout << "剂量计算结果\n";
    std::cout << "======================================================================\n\n";
    
    std::cout << std::fixed << std::setprecision(6);
    
    for (const auto& std_name : standards) {
        auto profile = calc.get_profile(std_name);
        
        double dose = calc.calculate_dose_increment(laeq, duration_s, profile);
        double twa = calc.calculate_twa(dose, profile);
        double lex = calc.calculate_lex(dose, profile);
        
        std::cout << "[" << std_name << "]\n";
        std::cout << "  剂量: " << dose << "%\n";
        std::cout << "  TWA: " << twa << " dBA\n";
        std::cout << "  LEX,8h: " << lex << " dBA\n\n";
    }
    
    // 输出汇总（用于对比）
    std::cout << "======================================================================\n";
    std::cout << "结果汇总 (用于和Python版本对比)\n";
    std::cout << "======================================================================\n\n";
    
    for (const auto& std_name : standards) {
        auto profile = calc.get_profile(std_name);
        double dose = calc.calculate_dose_increment(laeq, duration_s, profile);
        double twa = calc.calculate_twa(dose, profile);
        double lex = calc.calculate_lex(dose, profile);
        
        std::cout << std_name << ": Dose=" << dose << "%, TWA=" << twa << " dBA, LEX=" << lex << " dBA\n";
    }
    
    return 0;
}
