#!/usr/bin/env python3
"""
check_kurtosis.py — Verify kurtosis computation for A/C/Z-weighted signals.

Reads a 32-bit int PCM WAV file, applies IEC 61672-1 A-weighting and C-weighting
filters, then computes per-second kurtosis for raw (Z), A-weighted, and C-weighted
signals. Used to diagnose why the C++ code reports kurtosis_a_weighted and
kurtosis_c_weighted as always exactly 3.0.

Usage:
    python3 check_kurtosis.py <wav_file> [--plot]
"""

import argparse
import struct
import sys
import numpy as np
from scipy import signal
from pathlib import Path


# ─── WAV Reading ───────────────────────────────────────────────────────────

def read_wav_int32(path: str) -> tuple[np.ndarray, int]:
    """
    Read a 32-bit signed integer PCM WAV file.
    Returns (samples, sample_rate).
    Samples are float32 in range [-1.0, 1.0].
    """
    with open(path, 'rb') as f:
        riff_id = f.read(4)
        if riff_id != b'RIFF':
            raise ValueError(f"Not a RIFF file: {path}")
        f.read(4)  # file_size
        wave_id = f.read(4)
        if wave_id != b'WAVE':
            raise ValueError(f"Not a WAVE file: {path}")

        sample_rate = 0
        bits_per_sample = 0
        data_bytes = None

        while True:
            chunk_id = f.read(4)
            if len(chunk_id) < 4:
                break
            chunk_size = struct.unpack('<I', f.read(4))[0]

            if chunk_id == b'fmt ':
                fmt_data = f.read(chunk_size)
                audio_fmt, channels, sr, _, _, bps = struct.unpack('<HHIIHH', fmt_data[:16])
                sample_rate = sr
                bits_per_sample = bps
                if audio_fmt != 1:
                    raise ValueError(f"Not PCM format: audio_format={audio_fmt}")
                if channels != 1:
                    raise ValueError(f"Expected mono, got {channels} channels")
                if bps != 32:
                    raise ValueError(f"Expected 32-bit, got {bps}-bit")

            elif chunk_id == b'data':
                data_bytes = f.read(chunk_size)
                break
            else:
                f.seek(chunk_size, 1)

        if data_bytes is None:
            raise ValueError("No data chunk found")

    # Convert 32-bit signed int to float32 [-1.0, 1.0]
    samples = np.frombuffer(data_bytes, dtype=np.int32).astype(np.float32)
    samples /= 2147483648.0  # 2^31

    return samples, sample_rate


# ─── IEC 61672-1 Weighting Filters (scipy-based) ────────────────────────────

def design_a_weighting(fs: float):
    """
    Design IEC 61672-1 A-weighting filter as SOS.
    Returns (sos, gain_1khz) — gain_1khz is the total gain at 1 kHz,
    which should be applied AFTER the SOS chain for normalization.
    """
    f1 = 20.598997
    f2 = 107.65265
    f3 = 737.86223
    f4 = 12194.217
    A1000 = 1.9997

    num = [(2 * np.pi * f4) ** 2, 0, 0, 0, 0]
    den = np.polymul(
        [1, 4 * np.pi * f4, (2 * np.pi * f4) ** 2],
        [1, 4 * np.pi * f1, (2 * np.pi * f1) ** 2]
    )
    den = np.polymul(den, [1, 2 * np.pi * f3])
    den = np.polymul(den, [1, 2 * np.pi * f2])

    # Scale for 0 dB at 1 kHz
    w_1k = 2 * np.pi * 1000
    gain_1k = np.abs(np.polyval(num, 1j * w_1k) / np.polyval(den, 1j * w_1k))
    num = np.array(num) * (A1000 / gain_1k)

    # Bilinear transform
    b, a = signal.bilinear(num, den, fs)
    sos = signal.tf2sos(b, a)

    # Compute actual gain at 1 kHz
    w, h = signal.sosfreqz(sos, worN=[2 * np.pi * 1000 / fs])
    actual_gain = np.abs(h[0])
    gain_1khz = 1.0 / actual_gain if actual_gain > 1e-10 else 1.0

    return sos, gain_1khz


def design_c_weighting(fs: float):
    """
    Design IEC 61672-1 C-weighting filter as SOS.
    Returns (sos, gain_1khz).
    """
    f1 = 20.598997
    f4 = 12194.217
    C1000 = 0.99997

    num = [(2 * np.pi * f4) ** 2, 0, 0]
    den = np.polymul(
        [1, 4 * np.pi * f4, (2 * np.pi * f4) ** 2],
        [1, 4 * np.pi * f1, (2 * np.pi * f1) ** 2]
    )

    w_1k = 2 * np.pi * 1000
    gain_1k = np.abs(np.polyval(num, 1j * w_1k) / np.polyval(den, 1j * w_1k))
    num = np.array(num) * (C1000 / gain_1k)

    b, a = signal.bilinear(num, den, fs)
    sos = signal.tf2sos(b, a)

    w, h = signal.sosfreqz(sos, worN=[2 * np.pi * 1000 / fs])
    actual_gain = np.abs(h[0])
    gain_1khz = 1.0 / actual_gain if actual_gain > 1e-10 else 1.0

    return sos, gain_1khz


# ─── Kurtosis (Pearson, excess+3) ──────────────────────────────────────────

def kurtosis_pearson(x: np.ndarray) -> float:
    """
    Compute Pearson kurtosis (β₂ = μ₄/σ⁴).
    For Gaussian: β₂ = 3.
    For uniform: β₂ = 1.8.
    For Laplace: β₂ = 6.
    """
    n = len(x)
    if n < 4:
        return 3.0
    mu = np.mean(x)
    m2 = np.mean((x - mu) ** 2)
    m4 = np.mean((x - mu) ** 4)
    if m2 <= 0:
        return 3.0
    return m4 / (m2 * m2)


# ─── Main ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Check kurtosis computation")
    parser.add_argument('wav_file', help='Path to 32-bit int PCM WAV file')
    parser.add_argument('--plot', action='store_true', help='Generate histogram plots')
    parser.add_argument('--window-size', type=float, default=1.0,
                        help='Analysis window in seconds (default: 1.0)')
    args = parser.parse_args()

    wav_path = Path(args.wav_file)
    print(f"📁 Reading: {wav_path}")
    print(f"   File size: {wav_path.stat().st_size / 1024:.1f} KB")

    samples, fs = read_wav_int32(str(wav_path))
    duration = len(samples) / fs
    print(f"   Samples: {len(samples)}, Rate: {fs} Hz, Duration: {duration:.2f}s")

    # Design weighting filters
    print("\n🔧 Designing IEC 61672-1 weighting filters...")
    sos_a, gain_a = design_a_weighting(fs)
    sos_c, gain_c = design_c_weighting(fs)
    print(f"   A-weighting: {len(sos_a)} SOS sections, 1kHz gain factor = {gain_a:.6f}")
    print(f"   C-weighting: {len(sos_c)} SOS sections, 1kHz gain factor = {gain_c:.6f}")

    # Apply weighting
    print("\n🔧 Applying weighting filters...")
    a_weighted = signal.sosfilt(sos_a, samples) * gain_a
    c_weighted = signal.sosfilt(sos_c, samples) * gain_c
    z_weighted = samples  # raw

    # Quick sanity: compute SPL
    ref = 20e-6  # reference pressure in Pa equivalent
    # The float samples are scaled to [-1,1] representing pressure
    # For actual SPL we'd need a calibration factor, but relative is fine for diagnosis
    rms_z = np.sqrt(np.mean(z_weighted ** 2))
    rms_a = np.sqrt(np.mean(a_weighted ** 2))
    rms_c = np.sqrt(np.mean(c_weighted ** 2))
    print(f"\n   Overall RMS: Z={rms_z:.6f}, A={rms_a:.6f}, C={rms_c:.6f}")
    print(f"   If 1.0 = 94 dB SPL: Z={20*np.log10(rms_z)+94:.1f}, "
          f"A={20*np.log10(rms_a)+94:.1f}, C={20*np.log10(rms_c)+94:.1f} dB")

    # Per-window kurtosis analysis
    window_samples = int(args.window_size * fs)
    if window_samples < 4:
        window_samples = 4

    n_windows = len(samples) // window_samples
    print(f"\n📊 Computing per-{args.window_size}s kurtosis ({window_samples} samples/window, {n_windows} windows):")
    print(f"{'Window':>6s}  {'kurt_Z':>8s}  {'kurt_A':>8s}  {'kurt_C':>8s}  {'RMS_Z_dB':>9s}  {'RMS_A_dB':>9s}  {'RMS_C_dB':>9s}")
    print("-" * 80)

    kz_vals, ka_vals, kc_vals = [], [], []

    for w in range(n_windows):
        start = w * window_samples
        end = start + window_samples

        kw_z = kurtosis_pearson(z_weighted[start:end])
        kw_a = kurtosis_pearson(a_weighted[start:end])
        kw_c = kurtosis_pearson(c_weighted[start:end])

        kz_vals.append(kw_z)
        ka_vals.append(kw_a)
        kc_vals.append(kw_c)

        rms_w_z = np.sqrt(np.mean(z_weighted[start:end] ** 2))
        rms_w_a = np.sqrt(np.mean(a_weighted[start:end] ** 2))
        rms_w_c = np.sqrt(np.mean(c_weighted[start:end] ** 2))

        # Mark suspicious windows (A or C kurtosis exactly 3.0)
        flag = ""
        if abs(kw_a - 3.0) < 1e-6 or abs(kw_c - 3.0) < 1e-6:
            flag = " ⚠️"

        print(f"{w:6d}  {kw_z:8.4f}  {kw_a:8.4f}  {kw_c:8.4f}  "
              f"{20*np.log10(rms_w_z)+94:9.2f}  {20*np.log10(rms_w_a)+94:9.2f}  "
              f"{20*np.log10(rms_w_c)+94:9.2f}{flag}")

    # Summary
    print("\n" + "=" * 80)
    print("📈 Summary Statistics:")
    print(f"   kurtosis_Z: mean={np.mean(kz_vals):.4f}, min={np.min(kz_vals):.4f}, "
          f"max={np.max(kz_vals):.4f}, std={np.std(kz_vals):.4f}")
    print(f"   kurtosis_A: mean={np.mean(ka_vals):.4f}, min={np.min(ka_vals):.4f}, "
          f"max={np.max(ka_vals):.4f}, std={np.std(ka_vals):.4f}")
    print(f"   kurtosis_C: mean={np.mean(kc_vals):.4f}, min={np.min(kc_vals):.4f}, "
          f"max={np.max(kc_vals):.4f}, std={np.std(kc_vals):.4f}")

    # Diagnosis
    n_flat_a = sum(1 for k in ka_vals if abs(k - 3.0) < 1e-4)
    n_flat_c = sum(1 for k in kc_vals if abs(k - 3.0) < 1e-4)
    print(f"\n🔍 Diagnosis:")
    print(f"   Windows with kurtosis_A exactly 3.0: {n_flat_a}/{n_windows}")
    print(f"   Windows with kurtosis_C exactly 3.0: {n_flat_c}/{n_windows}")
    if n_flat_a == n_windows:
        print("   ⚠️  ALL A-weighted windows have kurtosis=3.0 → BUG CONFIRMED")
    if n_flat_c == n_windows:
        print("   ⚠️  ALL C-weighted windows have kurtosis=3.0 → BUG CONFIRMED")
    if n_flat_a < n_windows and n_flat_c < n_windows:
        print("   ✅ Kurtosis varies naturally — no bug detected in Python path")
        print("   Bug is likely in C++ code (not the DSP theory)")

    # Plot if requested
    if args.plot:
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(3, 2, figsize=(14, 10))

        # Time series
        axes[0, 0].plot(np.arange(len(z_weighted)) / fs, z_weighted, alpha=0.7)
        axes[0, 0].set_title('Z-weighted (Raw)')
        axes[0, 0].set_ylabel('Amplitude')

        axes[1, 0].plot(np.arange(len(a_weighted)) / fs, a_weighted, alpha=0.7, color='orange')
        axes[1, 0].set_title('A-weighted')
        axes[1, 0].set_ylabel('Amplitude')

        axes[2, 0].plot(np.arange(len(c_weighted)) / fs, c_weighted, alpha=0.7, color='green')
        axes[2, 0].set_title('C-weighted')
        axes[2, 0].set_xlabel('Time (s)')
        axes[2, 0].set_ylabel('Amplitude')

        # Histograms
        for ax, data, title, color in [
            (axes[0, 1], z_weighted, 'Z-weighted Histogram', 'blue'),
            (axes[1, 1], a_weighted, 'A-weighted Histogram', 'orange'),
            (axes[2, 1], c_weighted, 'C-weighted Histogram', 'green'),
        ]:
            ax.hist(data, bins=100, density=True, alpha=0.7, color=color)
            # Overlay Gaussian
            mu, sigma = np.mean(data), np.std(data)
            x = np.linspace(mu - 4 * sigma, mu + 4 * sigma, 200)
            ax.plot(x, 1 / (sigma * np.sqrt(2 * np.pi)) * np.exp(-0.5 * ((x - mu) / sigma) ** 2),
                    'r--', linewidth=2, label=f'Gaussian (k=3.0)')
            k = kurtosis_pearson(data)
            ax.set_title(f'{title}\nkurtosis = {k:.4f}')
            ax.legend()

        plt.tight_layout()
        out_path = wav_path.parent / f"{wav_path.stem}_kurtosis_check.png"
        plt.savefig(out_path, dpi=150)
        print(f"\n📊 Plot saved: {out_path}")


if __name__ == '__main__':
    main()
