# AGENTS.md

## What This Is

C++17 noise dose calculation toolkit, ported from Python `noise_info_toolkit`. Computes occupational noise exposure metrics (LAeq, Dose, TWA, LEX,8h, 1/3 octave bands, kurtosis) per NIOSH/OSHA/EU-ISO standards. All calculations use single-precision `float`.

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
ctest                          # runs test_noise_processor + dose_validator
./test_noise_processor         # direct run, uses assert() — no test framework
./dose_validator               # standalone, no library dependency
./noise_toolkit_example        # demo of both interfaces
```

Tests use bare `assert()` — a failing test aborts with no output. No Catch2/gtest.

## Actual File Layout

```
include/
  noise_toolkit.hpp      # main header, utility fn decls
  noise_metrics.hpp      # SecondMetrics, MinuteMetrics, FreqBandMoments structs + constants
  noise_processor.hpp    # NoiseProcessor class (two-interface design)
  dose_calculator.hpp    # DoseCalculator (static), DoseProfile (POD), DoseStandard enum
  signal_utils.hpp       # Signal struct, weighting/filter/spectral analysis functions
  iir_filter.hpp         # IIRFilter, BiquadFilter, filter_design namespace, octave_filters namespace
  filter_coefficients_48k.hpp  # Pre-computed A/C weighting coefficients for 48kHz, BiquadChain template

src/
  noise_processor.cpp
  dose_calculator.cpp    # PC-only string-based API (guarded by NOISE_EMBEDDED_BUILD)
  signal_utils.cpp
  iir_filter.cpp
```

**That's it.** No audio_processor, event_detector, event_processor, ring_buffer, database, wav_reader, tdms_converter, or time_history_processor. `noise_toolkit.hpp` has forward declarations for these but they are dead code — the classes don't exist.

## Architecture

Two-interface design in `NoiseProcessor`:

1. `process_segment(buffer_start, buffer_end, duration_s)` — per-segment audio → `SecondMetrics` (81 indicators)
2. `aggregate_metrics(second_metrics*, count, unit_duration_s)` — array of `SecondMetrics` → `MinuteMetrics`

`DoseCalculator` is now all-static with a `constexpr` profile table indexed by `DoseStandard` enum. No dynamic allocation. String-based API available only in PC builds (`#ifndef NOISE_EMBEDDED_BUILD`).

## Key Implementation Details

- **Namespace**: `noise_toolkit` (sub-namespaces: `filter_design`, `octave_filters` in iir_filter)
- **Precision**: All calculations use single-precision `float`. Dose calculations were previously `double` but have been unified to `float`.
- **A/C weighting**: Pre-computed `constexpr BiquadChain` for 48kHz in `filter_coefficients_48k.hpp`. Runtime `filter_design::a_weighting_design()` used as fallback for other sample rates.
- **Bandpass filters**: `noise_processor.cpp` redesigns 9 bandpass filters per `process_one_second()` call. This is a known performance issue.
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

- `noise_toolkit.hpp` includes `<vector>` but only for utility function signatures
- Forward declarations in `noise_toolkit.hpp` reference nonexistent classes (dead code)
- `signal_utils.hpp` declares `extern` A/C weighting gain vectors that are unused by the IIR filter path
- No `-Wall -Wextra` in CMakeLists.txt
- Tests use `assert()` which is disabled in Release builds (`-DNDEBUG`)
- Bandpass filter design (`filter_design::bandpass()`) produces marginally stable filters for narrow bands
- `process_tdms_cpp.cpp` referenced in old docs does not exist
