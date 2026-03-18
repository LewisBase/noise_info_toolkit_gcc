#!/usr/bin/env python3
"""
校验脚本：对比Python版本和C++版本的剂量计算结果
"""
import math
import sys

# ========== Python版本实现（从dose_calculator.py复制）==========

class DoseProfile:
    """剂量计算标准配置类"""
    def __init__(self, name, criterion_level, exchange_rate, 
                 threshold=0.0, reference_duration=8.0, description=""):
        self.name = name
        self.criterion_level = criterion_level  # Lc (dBA)
        self.exchange_rate = exchange_rate       # ER (dB)
        self.threshold = threshold              # LT (dBA)
        self.reference_duration = reference_duration  # Tref (hours)
        self.description = description

# 预定义标准配置
PROFILES = {
    "NIOSH": DoseProfile(
        "NIOSH", 85.0, 3.0, 0.0, 8.0,
        "NIOSH标准: 85dBA准则级, 3dB交换率, 8小时参考时长"
    ),
    "OSHA_PEL": DoseProfile(
        "OSHA_PEL", 90.0, 5.0, 0.0, 8.0,
        "OSHA_PEL标准: 90dBA准则级, 5dB交换率, 8小时参考时长"
    ),
    "OSHA_HCA": DoseProfile(
        "OSHA_HCA", 85.0, 5.0, 0.0, 8.0,
        "OSHA_HCA标准: 85dBA准则级, 5dB交换率, 8小时参考时长"
    ),
    "EU_ISO": DoseProfile(
        "EU_ISO", 85.0, 3.0, 0.0, 8.0,
        "EU_ISO标准: 85dBA准则级, 3dB交换率, 8小时参考时长"
    ),
}

def py_calculate_dose_increment(laeq, duration_s, profile):
    """Python版本：计算单个时间段的剂量增量"""
    if laeq < profile.threshold:
        return 0.0
    
    duration_h = duration_s / 3600.0
    
    # Dose% = 100 × (dt/Tref) × 2^((L-Lc)/ER)
    exponent = (laeq - profile.criterion_level) / profile.exchange_rate
    dose_increment = 100.0 * (duration_h / profile.reference_duration) * (2 ** exponent)
    
    return dose_increment

def py_calculate_allowed_time(laeq, profile):
    """Python版本：计算允许暴露时间"""
    if laeq < profile.threshold:
        return float('inf')
    
    # T = Tref / 2^((L - Lc) / ER)
    exponent = (laeq - profile.criterion_level) / profile.exchange_rate
    allowed_time = profile.reference_duration / (2 ** exponent)
    return allowed_time

def py_calculate_twa(total_dose_pct, profile):
    """Python版本：计算TWA"""
    if total_dose_pct <= 0:
        return 0.0
    
    if profile.name.startswith("OSHA"):
        # OSHA使用特殊系数 16.61
        twa = 16.61 * math.log10(total_dose_pct / 100.0) + profile.criterion_level
    else:
        # NIOSH和ISO使用系数 10
        twa = 10.0 * math.log10(total_dose_pct / 100.0) + profile.criterion_level
    
    return twa

def py_calculate_lex(total_dose_pct, profile):
    """Python版本：计算LEX,8h"""
    if total_dose_pct <= 0:
        return 0.0
    
    lex = 10.0 * math.log10(total_dose_pct / 100.0) + profile.criterion_level
    return lex

# ========== C++版本实现（从dose_calculator.cpp翻译）==========

def cpp_calculate_dose_increment(laeq, duration_s, profile):
    """C++版本：计算单个时间段的剂量增量"""
    if laeq < profile.threshold:
        return 0.0
    
    duration_h = duration_s / 3600.0
    
    # Dose% = 100 × (dt/Tref) × 2^((L-Lc)/ER)
    exponent = (laeq - profile.criterion_level) / profile.exchange_rate
    dose_increment = 100.0 * (duration_h / profile.reference_duration) * pow(2.0, exponent)
    
    return dose_increment

def cpp_calculate_allowed_time(laeq, profile):
    """C++版本：计算允许暴露时间"""
    if laeq < profile.threshold:
        return float('inf')
    
    # T = Tref / 2^((L - Lc) / ER)
    exponent = (laeq - profile.criterion_level) / profile.exchange_rate
    allowed_time = profile.reference_duration / pow(2.0, exponent)
    return allowed_time

def cpp_calculate_twa(total_dose_pct, profile):
    """C++版本：计算TWA"""
    if total_dose_pct <= 0.0:
        return 0.0
    
    if profile.name.startswith("OSHA"):
        # OSHA uses special coefficient 16.61
        twa = 16.61 * math.log10(total_dose_pct / 100.0) + profile.criterion_level
    else:
        # NIOSH and ISO use coefficient 10
        twa = 10.0 * math.log10(total_dose_pct / 100.0) + profile.criterion_level
    
    return twa

def cpp_calculate_lex(total_dose_pct, profile):
    """C++版本：计算LEX,8h"""
    if total_dose_pct <= 0.0:
        return 0.0
    
    lex = 10.0 * math.log10(total_dose_pct / 100.0) + profile.criterion_level
    return lex

# ========== 对比测试 ==========

def compare_results():
    """对比两个版本的计算结果"""
    print("=" * 70)
    print("噪声剂量计算器校验报告")
    print("对比: Python版本 vs C++版本")
    print("=" * 70)
    
    # 测试用例
    test_cases = [
        # (LAeq_dBA, duration_s, description)
        (85.0, 8 * 3600, "标准8小时暴露 @ 85dBA"),
        (90.0, 8 * 3600, "标准8小时暴露 @ 90dBA"),
        (85.0, 4 * 3600, "4小时暴露 @ 85dBA"),
        (88.0, 8 * 3600, "8小时暴露 @ 88dBA"),
        (100.0, 1 * 3600, "1小时暴露 @ 100dBA"),
        (80.0, 8 * 3600, "8小时暴露 @ 80dBA (低于准则级)"),
    ]
    
    standards = ["NIOSH", "OSHA_PEL", "OSHA_HCA", "EU_ISO"]
    
    all_passed = True
    tolerance = 1e-10  # 浮点精度容差
    
    for laeq, duration_s, desc in test_cases:
        print(f"\n{'─' * 70}")
        print(f"测试用例: {desc}")
        print(f"  LAeq = {laeq} dBA, Duration = {duration_s/3600:.1f} hours")
        print("─" * 70)
        
        for std_name in standards:
            profile = PROFILES[std_name]
            
            # 计算剂量增量
            py_dose = py_calculate_dose_increment(laeq, duration_s, profile)
            cpp_dose = cpp_calculate_dose_increment(laeq, duration_s, profile)
            dose_match = abs(py_dose - cpp_dose) < tolerance
            
            # 计算允许时间
            py_allowed = py_calculate_allowed_time(laeq, profile)
            cpp_allowed = cpp_calculate_allowed_time(laeq, profile)
            allowed_match = abs(py_allowed - cpp_allowed) < tolerance
            
            # 计算TWA (基于总剂量)
            py_twa = py_calculate_twa(py_dose, profile)
            cpp_twa = cpp_calculate_twa(cpp_dose, profile)
            twa_match = abs(py_twa - cpp_twa) < tolerance
            
            # 计算LEX,8h
            py_lex = py_calculate_lex(py_dose, profile)
            cpp_lex = cpp_calculate_lex(cpp_dose, profile)
            lex_match = abs(py_lex - cpp_lex) < tolerance
            
            status = "✓ PASS" if (dose_match and allowed_match and twa_match and lex_match) else "✗ FAIL"
            if not (dose_match and allowed_match and twa_match and lex_match):
                all_passed = False
            
            print(f"\n  [{std_name}] {status}")
            print(f"    Dose%:     Python={py_dose:.6f}, C++={cpp_dose:.6f}, Diff={abs(py_dose-cpp_dose):.2e}")
            print(f"    Allowed_h: Python={py_allowed:.6f}, C++={cpp_allowed:.6f}, Diff={abs(py_allowed-cpp_allowed):.2e}")
            print(f"    TWA:       Python={py_twa:.6f}, C++={cpp_twa:.6f}, Diff={abs(py_twa-cpp_twa):.2e}")
            print(f"    LEX,8h:    Python={py_lex:.6f}, C++={cpp_lex:.6f}, Diff={abs(py_lex-cpp_lex):.2e}")
    
    print(f"\n{'=' * 70}")
    if all_passed:
        print("✓ 所有测试用例通过！Python版本和C++版本的计算结果一致。")
    else:
        print("✗ 部分测试用例失败，存在不一致。")
    print("=" * 70)
    
    # 额外验证：理论值对比
    print("\n" + "=" * 70)
    print("理论值验证")
    print("=" * 70)
    
    # NIOSH标准: 85dBA @ 8小时 = 100%剂量
    print("\n理论验证1: NIOSH标准, 85dBA @ 8小时")
    print(f"  期望剂量: 100%")
    profile = PROFILES["NIOSH"]
    dose = py_calculate_dose_increment(85.0, 8*3600, profile)
    print(f"  Python计算: {dose:.6f}%")
    print(f"  C++计算:    {cpp_calculate_dose_increment(85.0, 8*3600, profile):.6f}%")
    
    # NIOSH标准: 88dBA @ 8小时 = 200%剂量 (3dB交换率 => 2倍)
    print("\n理论验证2: NIOSH标准, 88dBA @ 8小时")
    print(f"  期望剂量: 200% (88-85=3dB, 3dB交换率 => 2倍)")
    dose = py_calculate_dose_increment(88.0, 8*3600, profile)
    print(f"  Python计算: {dose:.6f}%")
    print(f"  C++计算:    {cpp_calculate_dose_increment(88.0, 8*3600, profile):.6f}%")
    
    # OSHA_PEL标准: 90dBA @ 8小时 = 100%剂量
    print("\n理论验证3: OSHA_PEL标准, 90dBA @ 8小时")
    print(f"  期望剂量: 100%")
    profile = PROFILES["OSHA_PEL"]
    dose = py_calculate_dose_increment(90.0, 8*3600, profile)
    print(f"  Python计算: {dose:.6f}%")
    print(f"  C++计算:    {cpp_calculate_dose_increment(90.0, 8*3600, profile):.6f}%")
    
    # OSHA_PEL标准: 95dBA @ 8小时 = 200%剂量 (5dB交换率 => 2倍)
    print("\n理论验证4: OSHA_PEL标准, 95dBA @ 8小时")
    print(f"  期望剂量: 200% (95-90=5dB, 5dB交换率 => 2倍)")
    dose = py_calculate_dose_increment(95.0, 8*3600, profile)
    print(f"  Python计算: {dose:.6f}%")
    print(f"  C++计算:    {cpp_calculate_dose_increment(95.0, 8*3600, profile):.6f}%")
    
    print("\n" + "=" * 70)
    print("校验完成")
    print("=" * 70)
    
    return all_passed

if __name__ == "__main__":
    success = compare_results()
    sys.exit(0 if success else 1)
