# AGENTS.md

## What This Is

C++17 noise dose calculation toolkit (v3.1), ported from Python `noise_info_toolkit`. Computes occupational noise exposure metrics (LAeq, Dose, TWA, LEX,8h, 1/3 octave bands, kurtosis) per NIOSH/OSHA/EU-ISO standards. All calculations use single-precision `float`.

**v3.1 core change**: Hot path rewritten from batch vector processing to streaming per-sample processing. Zero heap allocation in `process_segment()`.

## Build

```bash
cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

CMake options: `BUILD_TESTS` (ON), `BUILD_EXAMPLES` (ON). No `BUILD_SHARED_LIBS` option exists. Library is always static (`libnoise_toolkit.a`).

SQLite3 is found at configure time but **never linked** — it's not required.

## Run Tests

```bash
cd build_test
ctest                                  # runs test_noise_processor + test_event_detector + test_dose_state + dose_validator
./test_noise_processor                 # direct run, uses assert() — no test framework
./test_event_detector                  # 14 tests for EventDetector
./test_dose_state                      # 9 tests for DoseState thin-wrapper (v3.1.3)
./dose_validator                       # built from dose_validator.cpp; standalone double reference, not linked to lib
./noise_toolkit_example                # demo of all 4 interfaces (v3.1.3 includes dose state demo)
```

Tests use bare `assert()` — a failing test aborts with no output. No Catch2/gtest.

## Actual File Layout

```
include/
  noise_toolkit.hpp              # umbrella header (includes noise_metrics.hpp)
  noise_metrics.hpp              # SecondMetrics, MinuteMetrics, FreqBandMoments structs + constants
  noise_processor.hpp            # NoiseProcessor class (v3.1 streaming architecture)
  dose_calculator.hpp            # DoseCalculator (static), DoseProfile (POD), DoseStandard enum
  signal_utils.hpp               # Signal struct, weighting/filter/spectral analysis functions
                                #   + apply_a_weighting_inplace / apply_c_weighting_inplace (v3.1)
  iir_filter.hpp                 # IIRFilter, BiquadFilter, filter_design namespace
                                #   + IIRFilter::process_sample(float*, size_t) (v3.1)
  filter_coefficients_48k.hpp    # Pre-computed A/C weighting coefficients for 48kHz, BiquadChain template
  bandpass_coefficients_48k.hpp  # Pre-computed 1/3 octave bandpass coefficients for 48kHz (v3.1)
  math_constants.hpp             # noise_const::PI_F / TWO_PI_F (v3.1, replaces M_PI)

src/
  noise_processor.cpp            # v3.1: streaming, zero heap allocation in process_segment()
  dose_calculator.cpp            # PC-only string-based API (guarded by NOISE_EMBEDDED_BUILD)
  signal_utils.cpp               # v3.1: added inplace weighting functions
  iir_filter.cpp                 # v3.1: added process_sample(float*, size_t)

tools/
  generate_bandpass_coeffs.cpp   # Generates bandpass_coefficients_48k.hpp from iir_filter
```

## Architecture (v3.1)

Two-interface design in `NoiseProcessor`:

1. `process_segment(buffer_start, buffer_end, duration_s)` — per-segment audio → `SecondMetrics` (81 indicators)
2. `aggregate_metrics(second_metrics*, count, unit_duration_s)` — array of `SecondMetrics` → `MinuteMetrics`

**v3.2 streaming data flow**:
```
process_segment(float* buffer, size_t n)
  ├─ Allocate A/C scratch buffers (VLA sized to actual n, zero heap)
  ├─ A-weighting: BiquadChain::process() sample-by-sample (constexpr coeffs)
  ├─ C-weighting: BiquadChain::process() sample-by-sample (constexpr coeffs)
  ├─ Single pass: accumulate raw moments, energy, peaks, kurtosis
  ├─ Leq/Peak/kurtosis calculations
  ├─ Dose calculations
  └─ 9 × bandpass: BiquadFilter::process() sample-by-sample, inline moment accumulation
```

All intermediate buffers are VLAs sized to the actual sample count (not hardcoded 48000). For a typical 10 ms block @ 48 kHz this is only 2 × 480 × 4 bytes ≈ 4 KiB. No vector/new/malloc in the entire call path.

`DoseCalculator` is all-static with a `constexpr` profile table indexed by `DoseStandard` enum. No dynamic allocation. String-based API available only in PC builds (`#ifndef NOISE_EMBEDDED_BUILD`).

## Key Implementation Details

- **Namespace**: `noise_toolkit` (sub-namespaces: `noise_const` in math_constants, `filter_design`/`octave_filters` in iir_filter)
- **Precision**: All calculations use single-precision `float`. Dose calculations unified to `float`.
- **A/C weighting**: Pre-computed `constexpr BiquadChain` for 48kHz in `filter_coefficients_48k.hpp`. Runtime `filter_design::a_weighting_design()` used as fallback for other sample rates.
- **Bandpass filters**: Pre-computed `constexpr BandpassCoeffs[9]` for 48kHz in `bandpass_coefficients_48k.hpp`. Runtime `filter_design::bandpass()` fallback for other rates. 9 persistent `BiquadFilter` members in `NoiseProcessor`.
- **M_PI removed**: All `M_PI` replaced with `noise_const::PI_F` / `TWO_PI_F` for embedded toolchain compatibility.
- **Exceptions**: Guarded by `#ifndef NOISE_EMBEDDED_BUILD` in iir_filter.cpp. PC build still uses throws.
- **`thread_local`**: Removed from signal_utils.cpp, now uses plain `static`.
- **`alignas(64)`**: Removed from `SecondMetrics`.
- **`reinterpret_cast`/`const_cast`**: `noise_metrics.hpp` band_moments_ptr() reinterprets field pointers as `FreqBandMoments*`.

## Code Style

- `snake_case` for functions/variables, `PascalCase` for types, `UPPER_SNAKE_CASE` for constants
- Member variables: `name_` suffix
- Doxygen `@brief`/`@param`/`@return` on public API
- `#pragma once` guards
- `constexpr` for compile-time constants

## Known Issues / Tech Debt

- `signal_utils.hpp` declares `extern` A/C weighting gain vectors that are unused by the IIR filter path
- No `-Wall -Wextra` in CMakeLists.txt
- Tests use `assert()` which is disabled in Release builds (`-DNDEBUG`)
- Bandpass filter design (`filter_design::bandpass()`) produces marginally stable filters for narrow bands
- A/C weighting scratch buffers in `process_segment()` are stack-allocated at 2 × 48KB — works for ≤ 1s blocks @ 48kHz, but would need heap or static allocation for larger blocks
- `test_noise_processor::test_frequency_bands` is flaky on some build configurations (asserts `freq_1khz_spl > freq_63hz_spl` on 1kHz sine — pre-existing issue, unrelated to v3.1.3 dose state work)

## v3.1.3 Dose State API (added 2026-06-04)

New file: `include/dose_state.hpp` — thin wrapper around existing `DoseCalculator` for cumulative dose tracking.

```cpp
#include "dose_state.hpp"
using namespace noise_toolkit;

DoseState state = {};                                              // 8 bytes POD
state = accumulate_dose_frac(state, m.dose_frac_niosh, dt_s);      // per segment
float pct = dose_to_pct(state);                                    // Dose%
float twa = dose_to_twa(state, DoseStandard::NIOSH);               // TWA
float lex = dose_to_lex8h(state, DoseStandard::NIOSH);             // LEX,8h
```

Business side holds 1+ `DoseState` (one per standard). Library is stateless. `DoseStandard` enum automatically selects 3dB/5dB log10 coefficient (10.0 / 16.61) — never manually compute 10·log10 for OSHA data.
