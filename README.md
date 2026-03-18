# noise_info_toolkit_gcc

C++ implementation of noise dose calculation, converted from Python version.

## 功能

- 噪声剂量计算（NIOSH, OSHA_PEL, OSHA_HCA, EU_ISO标准）
- 支持多种声学指标计算
- TDMS文件处理

## 构建

```bash
cd build_test
cmake ..
make
```

## 验证

```bash
# 理论验证
./dose_validator

# TDMS文件处理
./process_tdms_cpp
```

## 对比验证

与Python版本（noise_info_toolkit）的计算结果对比：

| 标准 | 指标 | Python | C++ | 差异 |
|------|------|--------|-----|------|
| NIOSH | Dose% | 0.046840% | 0.046840% | 0 |
| NIOSH | TWA | 51.706172 dBA | 51.706172 dBA | 0 |

✅ 所有计算结果一致

## 文件结构

```
noise_info_toolkit_gcc/
├── include/          # 头文件
├── src/              # C++源文件
├── build_test/       # 构建目录
├── examples/         # 示例代码
├── dose_validator.cpp       # 验证程序
└── process_tdms_cpp.cpp    # TDMS处理
```

## 标准参数

| 标准 | 准则级 (dBA) | 交换率 (dB) | 参考时长 (h) |
|------|-------------|-------------|-------------|
| NIOSH | 85 | 3 | 8 |
| OSHA_PEL | 90 | 5 | 8 |
| OSHA_HCA | 85 | 5 | 8 |
| EU_ISO | 85 | 3 | 8 |

## 剂量计算公式

- 允许时间: `T = Tref / 2^((L - Lc) / ER)`
- 剂量: `Dose% = 100 × (dt/Tref) × 2^((L-Lc)/ER)`
- TWA: `NIOSH/ISO: 10 × log10(Dose%/100) + Lc`, `OSHA: 16.61 × log10(Dose%/100) + Lc`
- LEX,8h: `10 × log10(Dose%/100) + Lc`
