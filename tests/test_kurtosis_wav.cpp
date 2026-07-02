/**
 * Quick test: read WAV, compute kurtosis for Z/A/C weighted signals.
 * This reproduces the bug where kurtosis_a_weighted and kurtosis_c_weighted
 * are always exactly 3.0.
 */
#include "noise_processor.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>

using namespace noise_toolkit;

std::vector<float> read_wav_int32_as_float(const char* path, int* out_sr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot open: " << path << "\n"; return {}; }

    char riff[4], wave[4];
    uint32_t file_size;
    f.read(riff, 4);
    f.read(reinterpret_cast<char*>(&file_size), 4);
    f.read(wave, 4);

    if (std::memcmp(riff, "RIFF", 4) || std::memcmp(wave, "WAVE", 4)) {
        std::cerr << "Not a WAV file\n";
        return {};
    }

    int sr = 0, bps = 0, ch = 0;
    std::vector<int32_t> raw;
    bool data_found = false;

    while (!data_found) {
        char ckid[4];
        uint32_t csize;
        f.read(ckid, 4);
        if (f.gcount() < 4) break;
        f.read(reinterpret_cast<char*>(&csize), 4);

        if (std::memcmp(ckid, "fmt ", 4) == 0) {
            uint16_t af, nc;
            uint32_t sr32, br;
            uint16_t ba, bps16;
            f.read(reinterpret_cast<char*>(&af), 2);
            f.read(reinterpret_cast<char*>(&nc), 2);
            f.read(reinterpret_cast<char*>(&sr32), 4);
            f.read(reinterpret_cast<char*>(&br), 4);
            f.read(reinterpret_cast<char*>(&ba), 2);
            f.read(reinterpret_cast<char*>(&bps16), 2);
            sr = sr32;
            bps = bps16;
            ch = nc;
            f.seekg(csize - 16, std::ios::cur);
        } else if (std::memcmp(ckid, "data", 4) == 0) {
            size_t nsamp = csize / (bps / 8);
            raw.resize(nsamp);
            // Read as int32
            std::vector<int32_t> tmp(nsamp);
            f.read(reinterpret_cast<char*>(tmp.data()), csize);
            raw = std::move(tmp);
            data_found = true;
        } else {
            f.seekg(csize, std::ios::cur);
        }
    }

    *out_sr = sr;

    // Convert int32 to float (same as what the embedded device does with raw ADC values)
    std::vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        samples[i] = static_cast<float>(raw[i]);
    }

    std::cout << "  Read " << raw.size() << " samples, " << sr << " Hz, "
              << bps << "-bit, " << ch << " ch\n";
    std::cout << "  Range: [" << raw[0];
    int32_t vmin = raw[0], vmax = raw[0];
    for (auto v : raw) { vmin = std::min(vmin, v); vmax = std::max(vmax, v); }
    std::cout << ", ..., " << vmin << ".." << vmax << "]\n";

    return samples;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1]
        : "../results/SES00003_260702/00-04-59.WAV";

    std::cout << "=== Kurtosis Bug Reproduction Test ===\n";
    std::cout << "File: " << path << "\n";

    int sr;
    auto samples = read_wav_int32_as_float(path, &sr);
    if (samples.empty()) return 1;

    NoiseProcessor proc(sr);
    size_t n_per_sec = sr;  // 48000 samples = 1 second

    std::cout << "\nPer-second kurtosis analysis:\n";
    std::cout << "sec  kurt_Z   kurt_A   kurt_C   LAeq     LCeq     LZeq\n";
    std::cout << "-------------------------------------------------------\n";

    bool bug_found_a = false, bug_found_c = false;
    int n_sec = 0;

    for (size_t off = 0; off + n_per_sec <= samples.size(); off += n_per_sec) {
        auto m = proc.process_segment(
            samples.data() + off,
            samples.data() + off + n_per_sec,
            1.0f);

        bool suspicious_a = std::abs(m.kurtosis_a_weighted - 3.0f) < 1e-5f;
        bool suspicious_c = std::abs(m.kurtosis_c_weighted - 3.0f) < 1e-5f;

        printf("%3d  %8.4f %8.4f %8.4f  %7.2f  %7.2f  %7.2f",
               n_sec,
               m.kurtosis_total,
               m.kurtosis_a_weighted,
               m.kurtosis_c_weighted,
               m.LAeq, m.LCeq, m.LZeq);

        if (suspicious_a || suspicious_c) {
            printf(" ⚠️");
            if (suspicious_a) bug_found_a = true;
            if (suspicious_c) bug_found_c = true;
        }
        printf("\n");
        n_sec++;
    }

    std::cout << "\n=== Diagnosis ===\n";
    if (bug_found_a) {
        std::cout << "⚠️  BUG CONFIRMED: kurtosis_a_weighted is ALWAYS exactly 3.0\n";
        std::cout << "   Expected: varying values (like kurtosis_total)\n";
        std::cout << "   Got: constant 3.0 (Gaussian fallback, meaning m2 <= 0)\n";
    }
    if (bug_found_c) {
        std::cout << "⚠️  BUG CONFIRMED: kurtosis_c_weighted is ALWAYS exactly 3.0\n";
    }
    if (!bug_found_a && !bug_found_c) {
        std::cout << "✅ No bug in C++ path — check firmware integration\n";
    }

    // Also test: manually compute kurtosis on raw data to verify lambda works
    std::cout << "\n=== Sanity check: manual kurtosis on raw data ===\n";
    auto m0 = proc.process_segment(samples.data(), samples.data() + n_per_sec, 1.0f);
    std::cout << "kurtosis_total (Z): " << m0.kurtosis_total << "\n";
    std::cout << "kurtosis_a_weighted: " << m0.kurtosis_a_weighted << "\n";
    std::cout << "kurtosis_c_weighted: " << m0.kurtosis_c_weighted << "\n";

    // Manual check: compute kurtosis directly from samples
    float mean_z = 0, m2_z = 0, m4_z = 0;
    for (size_t i = 0; i < n_per_sec; ++i) {
        mean_z += samples[i];
    }
    mean_z /= n_per_sec;
    for (size_t i = 0; i < n_per_sec; ++i) {
        float d = samples[i] - mean_z;
        float d2 = d * d;
        m2_z += d2;
        m4_z += d2 * d2;
    }
    m2_z /= n_per_sec;
    m4_z /= n_per_sec;
    float manual_kurt = (m2_z > 0) ? m4_z / (m2_z * m2_z) : 3.0f;
    std::cout << "Manual kurtosis (raw): " << manual_kurt << " (should match kurtosis_total)\n";

    return (bug_found_a || bug_found_c) ? 1 : 0;
}
