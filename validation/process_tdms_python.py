#!/usr/bin/env python3
"""
使用Python版本处理TDMS文件，计算噪声指标
直接实现剂量计算核心逻辑，不依赖复杂库
"""
import numpy as np
from nptdms import TdmsFile

# TDMS文件路径
TDMS_FILE = '/home/lewisbase/workspace/浙健/2.Code/Mine/noise_info_toolkit/audio_files/CH1_20251031_110019.tdms'

# 采样率
SAMPLING_RATE = 48000  # Hz
REFERENCE_PRESSURE = 20e-6

def db_to_pressure(db):
    """分贝转声压"""
    return REFERENCE_PRESSURE * 10 ** (db / 20.0)

def pressure_to_db(pressure):
    """声压转分贝"""
    if pressure <= 0:
        return -np.inf
    return 20 * np.log10(pressure / REFERENCE_PRESSURE)

def calculate_leq(signal):
    """计算Leq"""
    rms = np.sqrt(np.mean(signal ** 2))
    if rms > 0:
        return pressure_to_db(rms)
    return -np.inf

def calculate_peak(signal):
    """计算峰值"""
    return pressure_to_db(np.max(np.abs(signal)))

# ==================== 剂量计算核心（从Python版本复制）====================

class DoseProfile:
    def __init__(self, name, criterion_level, exchange_rate, threshold=0.0, reference_duration=8.0):
        self.name = name
        self.criterion_level = criterion_level
        self.exchange_rate = exchange_rate
        self.threshold = threshold
        self.reference_duration = reference_duration

PROFILES = {
    "NIOSH": DoseProfile("NIOSH", 85.0, 3.0, 0.0, 8.0),
    "OSHA_PEL": DoseProfile("OSHA_PEL", 90.0, 5.0, 0.0, 8.0),
    "OSHA_HCA": DoseProfile("OSHA_HCA", 85.0, 5.0, 0.0, 8.0),
    "EU_ISO": DoseProfile("EU_ISO", 85.0, 3.0, 0.0, 8.0),
}

def calculate_dose_increment(laeq, duration_s, profile):
    """计算剂量增量"""
    if laeq < profile.threshold:
        return 0.0
    duration_h = duration_s / 3600.0
    exponent = (laeq - profile.criterion_level) / profile.exchange_rate
    dose_increment = 100.0 * (duration_h / profile.reference_duration) * (2 ** exponent)
    return dose_increment

def calculate_twa(total_dose_pct, profile):
    """计算TWA"""
    if total_dose_pct <= 0:
        return 0.0
    if profile.name.startswith("OSHA"):
        twa = 16.61 * np.log10(total_dose_pct / 100.0) + profile.criterion_level
    else:
        twa = 10.0 * np.log10(total_dose_pct / 100.0) + profile.criterion_level
    return twa

def calculate_lex(total_dose_pct, profile):
    """计算LEX,8h"""
    if total_dose_pct <= 0:
        return 0.0
    lex = 10.0 * np.log10(total_dose_pct / 100.0) + profile.criterion_level
    return lex

# ==================== 主程序 ====================

def main():
    print("=" * 70)
    print("Python版本 - TDMS文件噪声分析")
    print("=" * 70)
    
    # 读取TDMS文件
    print(f"\n读取文件: {TDMS_FILE}")
    tdms_file = TdmsFile.read(TDMS_FILE)
    channel = tdms_file['Microphone']['Microphone']
    data = channel[:]
    
    print(f"采样率: {SAMPLING_RATE} Hz")
    print(f"采样点数: {len(data)}")
    print(f"时长: {len(data) / SAMPLING_RATE:.2f} 秒")
    
    # 计算基础声学指标
    print("\n" + "=" * 70)
    print("基础声学指标")
    print("=" * 70)
    
    leq = calculate_leq(data)
    peak = calculate_peak(data)
    
    print(f"Leq (线性等效声级): {leq:.2f} dB")
    print(f"峰值声级 (Peak): {peak:.2f} dB")
    
    # 简化处理：假设LAeq = Leq（实际应该用A计权滤波器）
    laeq = leq
    print(f"LAeq (简化，假设A计权=线性): {laeq:.2f} dB")
    
    # 计算剂量
    print("\n" + "=" * 70)
    print("噪声剂量计算")
    print("=" * 70)
    
    duration_s = len(data) / SAMPLING_RATE
    duration_h = duration_s / 3600.0
    
    print(f"暴露时长: {duration_s:.2f} 秒 = {duration_h:.4f} 小时")
    print(f"等效声级: {laeq:.2f} dBA")
    
    # 使用不同标准计算剂量
    for name, profile in PROFILES.items():
        dose = calculate_dose_increment(laeq, duration_s, profile)
        twa = calculate_twa(dose, profile)
        lex = calculate_lex(dose, profile)
        
        print(f"\n[{name}]")
        print(f"  剂量: {dose:.6f}%")
        print(f"  TWA: {twa:.6f} dBA")
        print(f"  LEX,8h: {lex:.6f} dBA")
    
    # 输出总结（用于对比）
    print("\n" + "=" * 70)
    print("结果汇总 (用于和C++版本对比)")
    print("=" * 70)
    print(f"\nLAeq = {laeq:.6f} dBA")
    print(f"Duration = {duration_s:.6f} 秒")
    
    for name, profile in PROFILES.items():
        dose = calculate_dose_increment(laeq, duration_s, profile)
        twa = calculate_twa(dose, profile)
        lex = calculate_lex(dose, profile)
        print(f"{name}: Dose={dose:.6f}%, TWA={twa:.6f} dBA, LEX={lex:.6f} dBA")

if __name__ == '__main__':
    main()
