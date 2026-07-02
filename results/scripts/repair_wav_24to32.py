#!/usr/bin/env python3
"""
repair_wav_24to32.py — Fix 24-bit PCM audio stored in 32-bit WAV containers.

Problem: The embedded device records 24-bit audio, but stores it as 32-bit
PCM WAV. Standard audio players interpret each 32-bit sample literally,
making the audio ~48 dB too quiet (inaudible).

Fix: Left-shift each sample by 8 bits (multiply by 256) so the 24-bit data
fills the 32-bit container. Also supports optional peak normalization
and conversion to 16-bit format for maximum compatibility.

Usage:
    # Basic fix: left-shift 24→32 bit, output 32-bit WAV
    python3 repair_wav_24to32.py input.WAV -o output.WAV

    # Convert to 16-bit (most compatible)
    python3 repair_wav_24to32.py input.WAV -o output.wav --16bit

    # Batch process all WAVs in a directory
    python3 repair_wav_24to32.py --batch /path/to/dir/ -o /output/dir/

    # With peak normalization
    python3 repair_wav_24to32.py input.WAV -o output.WAV --normalize
"""

import argparse
import struct
import sys
import numpy as np
from pathlib import Path


def read_wav_pcm(path: str) -> tuple[np.ndarray, int, int, int]:
    """
    Read a PCM WAV file.
    Returns (samples_int32, sample_rate, bits_per_sample, channels).
    Samples are int32.
    """
    with open(path, 'rb') as f:
        riff_id = f.read(4)
        if riff_id != b'RIFF':
            raise ValueError(f"Not a RIFF file: {path}")
        file_size = struct.unpack('<I', f.read(4))[0]
        wave_id = f.read(4)
        if wave_id != b'WAVE':
            raise ValueError(f"Not a WAVE file: {path}")

        sample_rate = 0
        bits_per_sample = 0
        channels = 0
        data_bytes = None
        fmt_extra = b''

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
                    raise ValueError(f"Not PCM: audio_format={audio_fmt}")
                if chunk_size > 16:
                    fmt_extra = fmt_data[16:]

            elif chunk_id == b'data':
                data_bytes = f.read(chunk_size)
                break
            else:
                f.seek(chunk_size, 1)

        if data_bytes is None:
            raise ValueError("No data chunk found")

    # Convert bytes to int32 array
    samples = np.frombuffer(data_bytes, dtype=np.int32).copy()

    return samples, sample_rate, bits_per_sample, channels, fmt_extra


def write_wav_pcm(path: str, samples: np.ndarray, sample_rate: int,
                  bits_per_sample: int, channels: int = 1):
    """
    Write a PCM WAV file.

    samples: int32 array (will be scaled/clipped for output bps)
    """
    # Scale and clip to target bit depth
    if bits_per_sample == 32:
        out = np.clip(samples, -2147483648, 2147483647).astype(np.int32)
    elif bits_per_sample == 24:
        # Pack 24-bit samples
        out = np.clip(samples, -8388608, 8388607).astype(np.int32)
        # Convert to 3-byte packed format
        packed = bytearray()
        for s in out:
            # Little-endian 3-byte signed
            packed.extend(struct.pack('<i', s)[:3])
        data = bytes(packed)
    elif bits_per_sample == 16:
        # Scale from 32-bit to 16-bit
        out = np.clip(samples / 65536.0, -32768, 32767).astype(np.int16)
    else:
        raise ValueError(f"Unsupported bit depth: {bits_per_sample}")

    with open(path, 'wb') as f:
        byte_rate = sample_rate * channels * (bits_per_sample // 8)
        block_align = channels * (bits_per_sample // 8)

        if bits_per_sample == 24:
            data = bytes(packed)
        elif bits_per_sample == 32:
            data = out.tobytes()
        else:
            data = out.tobytes()

        data_size = len(data)
        fmt_size = 16  # no extra format data
        file_size = 4 + (8 + fmt_size) + (8 + data_size)

        # RIFF header
        f.write(b'RIFF')
        f.write(struct.pack('<I', file_size))
        f.write(b'WAVE')

        # fmt chunk
        f.write(b'fmt ')
        f.write(struct.pack('<I', fmt_size))
        f.write(struct.pack('<HHIIHH',
                            1,  # PCM
                            channels,
                            sample_rate,
                            byte_rate,
                            block_align,
                            bits_per_sample))

        # data chunk
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        f.write(data)


def process_file(input_path: str, output_path: str, args):
    """Process a single WAV file."""
    print(f"📁 Processing: {input_path}")

    samples, sr, bps, ch, fmt_extra = read_wav_pcm(input_path)
    print(f"   Samples: {len(samples)}, Rate: {sr} Hz, Bits: {bps}, Channels: {ch}")
    print(f"   Original range: [{samples.min()}, {samples.max()}]")

    if bps != 32:
        print(f"   ⚠️  Expected 32-bit, got {bps}-bit. Applying shift as-is.")
        abs_max = np.max(np.abs(samples))
        bits_used = int(np.ceil(np.log2(abs_max + 1))) if abs_max > 0 else 0
        shift_bits = max(0, bps - bits_used) if bits_used > 0 else 0
        shifted = samples.astype(np.int64) << shift_bits
        print(f"   Bits used: {bits_used} / {bps}, shift: {shift_bits} bit(s)")
    else:
        # Auto-detect effective bit depth from data peak
        abs_max = np.max(np.abs(samples))
        bits_used = int(np.ceil(np.log2(abs_max + 1))) if abs_max > 0 else 0
        shift_bits = max(0, 32 - bits_used) if bits_used > 0 else 0

        print(f"   Peak: {abs_max:,}, Effective bits: {bits_used} / 32")
        print(f"   ➜ Shift needed: {shift_bits} bit(s) (not hardcoded 8!)")

        if shift_bits == 0:
            shifted = samples.astype(np.int64)
            print(f"   Already at full scale, no shift applied")
        else:
            shifted = samples.astype(np.int64) << shift_bits
            print(f"   After <<{shift_bits} shift: range [{shifted.min():,}, {shifted.max():,}]")

    # Peak normalization (optional — useful for loudness matching across files)
    if args.normalize:
        abs_peak = np.max(np.abs(shifted))
        if abs_peak > 0:
            target_peak = (1 << 31) - 1  # full 32-bit signed range
            if args.output_16bit:
                target_peak = 32767 << 16  # 16-bit range scaled to 32-bit integer space
            gain = target_peak / abs_peak * args.norm_level
            shifted = (shifted.astype(np.float64) * gain).astype(np.int64)
            print(f"   Peak normalized: gain={gain:.4f}, new range [{shifted.min():,}, {shifted.max():,}]")

    # Output bit depth
    out_bps = 16 if args.output_16bit else 32

    write_wav_pcm(output_path, shifted, sr, out_bps, ch)
    print(f"   ✅ Written: {output_path} ({out_bps}-bit)")
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Fix 24-bit PCM audio stored in 32-bit WAV containers"
    )
    parser.add_argument('input', nargs='?', help='Input WAV file')
    parser.add_argument('-o', '--output', help='Output WAV file or directory (for --batch)')
    parser.add_argument('--batch', action='store_true',
                        help='Process all WAV files in a directory')
    parser.add_argument('--16bit', dest='output_16bit', action='store_true',
                        help='Output 16-bit WAV (more compatible)')
    parser.add_argument('--normalize', action='store_true',
                        help='Peak-normalize to full scale')
    parser.add_argument('--norm-level', type=float, default=0.95,
                        help='Normalization level (0.0-1.0, default: 0.95)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show what would be done without writing files')
    args = parser.parse_args()

    if args.batch:
        if not args.input:
            print("Error: --batch requires input directory")
            sys.exit(1)
        input_dir = Path(args.input)
        if not input_dir.is_dir():
            print(f"Error: not a directory: {input_dir}")
            sys.exit(1)

        wav_files = sorted(input_dir.glob('*.WAV')) + sorted(input_dir.glob('*.wav'))
        if not wav_files:
            print(f"No WAV files found in {input_dir}")
            sys.exit(1)

        output_dir = Path(args.output) if args.output else input_dir / 'repaired'
        if not args.dry_run:
            output_dir.mkdir(parents=True, exist_ok=True)

        print(f"🔧 Batch processing {len(wav_files)} WAV files...\n")

        for wf in wav_files:
            out_path = output_dir / (wf.stem + '.wav')
            if args.dry_run:
                print(f"   [DRY RUN] {wf} → {out_path}")
            else:
                process_file(str(wf), str(out_path), args)

        print(f"✅ Done. {len(wav_files)} files → {output_dir}")

    else:
        if not args.input:
            print("Error: input WAV file required (or use --batch)")
            sys.exit(1)

        output = args.output
        if not output:
            in_path = Path(args.input)
            output = str(in_path.parent / f"{in_path.stem}_repaired.wav")

        if args.dry_run:
            print(f"[DRY RUN] {args.input} → {output}")
        else:
            process_file(args.input, output, args)


if __name__ == '__main__':
    main()
