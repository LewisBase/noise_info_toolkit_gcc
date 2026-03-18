/**
 * @file dose_validator.cpp
 * @brief 独立剂量计算验证程序 - 用于和Python版本对比
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>
#include <limits>
#include <string>

// ==================== 从C++版本复制的核心代码 ====================

const double REFERENCE_PRESSURE = 20e-6;

struct DoseProfile {
    std::string name;
    double criterion_level;  // Lc (dBA)
    double exchange_rate;    // ER (dB)
    double threshold;       // LT (dBA)
    double reference_duration;  // Tref (hours)
    std::string description;
    
    DoseProfile() : name(""), criterion_level(0), exchange_rate(0), 
                    threshold(0), reference_duration(0), description("") {}
    
    DoseProfile(std::string n, double lc, double er, double lt, double tref, std::string desc)
        : name(n), criterion_level(lc), exchange_rate(er), threshold(lt), 
          reference_duration(tref), description(desc) {}
};

class DoseCalculator {
public:
    std::map<std::string, DoseProfile> profiles_;
    
    DoseCalculator() {
        profiles_["NIOSH"] = DoseProfile(
            "NIOSH", 85.0, 3.0, 0.0, 8.0,
            "NIOSH标准: 85dBA准则级, 3dB交换率, 8小时参考时长"
        );
        profiles_["OSHA_PEL"] = DoseProfile(
            "OSHA_PEL", 90.0, 5.0, 0.0, 8.0,
            "OSHA_PEL标准: 90dBA准则级, 5dB交换率, 8小时参考时长"
        );
        profiles_["OSHA_HCA"] = DoseProfile(
            "OSHA_HCA", 85.0, 5.0, 0.0, 8.0,
            "OSHA_HCA标准: 85dBA准则级, 5dB交换率, 8小时参考时长"
        );
        profiles_["EU_ISO"] = DoseProfile(
            "EU_ISO", 85.0, 3.0, 0.0, 8.0,
            "EU_ISO标准: 85dBA准则级, 3dB交换率, 8小时参考时长"
        );
    }
    
    DoseProfile get_profile(const std::string& standard) const {
        return profiles_.at(standard);
    }
    
    double calculate_dose_increment(double laeq, double duration_s, const DoseProfile& profile) const {
        if (laeq < profile.threshold) {
            return 0.0;
        }
        
        double duration_h = duration_s / 3600.0;
        
        // Dose% = 100 × (dt/Tref) × 2^((L-Lc)/ER)
        double exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
        double dose_increment = 100.0 * (duration_h / profile.reference_duration) * 
                               std::pow(2.0, exponent);
        
        return dose_increment;
    }
    
    double calculate_allowed_time(double laeq, const DoseProfile& profile) const {
        if (laeq < profile.threshold) {
            return std::numeric_limits<double>::infinity();
        }
        
        // T = Tref / 2^((L - Lc) / ER)
        double exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
        double allowed_time = profile.reference_duration / std::pow(2.0, exponent);
        return allowed_time;
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
    std::cout << "  C++版本剂量计算器 - 独立验证程序\n";
    std::cout << "======================================================================\n\n";
    
    DoseCalculator calc;
    
    // 测试用例
    struct TestCase {
        double laeq;
        double duration_h;
        std::string desc;
    };
    
    TestCase tests[] = {
        {85.0, 8.0, "标准8小时 @ 85dBA"},
        {90.0, 8.0, "标准8小时 @ 90dBA"},
        {88.0, 8.0, "标准8小时 @ 88dBA"},
        {100.0, 1.0, "1小时 @ 100dBA"},
        {80.0, 8.0, "8小时 @ 80dBA (低于准则级)"},
    };
    
    std::string standards[] = {"NIOSH", "OSHA_PEL", "OSHA_HCA", "EU_ISO"};
    
    std::cout << std::fixed << std::setprecision(6);
    
    for (const auto& test : tests) {
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "测试: " << test.desc << "\n";
        std::cout << "  LAeq = " << test.laeq << " dBA, Duration = " << test.duration_h << " hours\n";
        std::cout << "----------------------------------------------------------------------\n";
        
        for (const auto& std_name : standards) {
            auto profile = calc.get_profile(std_name);
            double duration_s = test.duration_h * 3600.0;
            
            double dose = calc.calculate_dose_increment(test.laeq, duration_s, profile);
            double allowed = calc.calculate_allowed_time(test.laeq, profile);
            double twa = calc.calculate_twa(dose, profile);
            double lex = calc.calculate_lex(dose, profile);
            
            std::cout << "\n[" << std_name << "]\n";
            std::cout << "  Dose%:     " << dose << " %\n";
            std::cout << "  Allowed_h: " << allowed << " hours\n";
            std::cout << "  TWA:       " << twa << " dBA\n";
            std::cout << "  LEX,8h:    " << lex << " dBA\n";
        }
        std::cout << "\n";
    }
    
    // 理论值验证
    std::cout << "======================================================================\n";
    std::cout << "理论值验证\n";
    std::cout << "======================================================================\n\n";
    
    std::cout << "NIOSH, 85dBA @ 8h: 期望 100%\n";
    auto p = calc.get_profile("NIOSH");
    std::cout << "  结果: " << calc.calculate_dose_increment(85.0, 8*3600.0, p) << "%\n\n";
    
    std::cout << "NIOSH, 88dBA @ 8h: 期望 200%\n";
    std::cout << "  结果: " << calc.calculate_dose_increment(88.0, 8*3600.0, p) << "%\n\n";
    
    std::cout << "OSHA_PEL, 90dBA @ 8h: 期望 100%\n";
    p = calc.get_profile("OSHA_PEL");
    std::cout << "  结果: " << calc.calculate_dose_increment(90.0, 8*3600.0, p) << "%\n\n";
    
    std::cout << "OSHA_PEL, 95dBA @ 8h: 期望 200%\n";
    std::cout << "  结果: " << calc.calculate_dose_increment(95.0, 8*3600.0, p) << "%\n\n";
    
    std::cout << "======================================================================\n";
    std::cout << "验证完成 - 输出结果用于和Python版本对比\n";
    std::cout << "======================================================================\n";
    
    return 0;
}
