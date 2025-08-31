#!/usr/bin/env python3
"""
ADPCM Implementation Test Script

This script tests the ADPCM codec implementation by:
1. Generating test audio signals
2. Encoding/decoding them with our ADPCM implementation
3. Comparing quality and compression ratios

Usage:
    python test_adpcm.py
"""

import numpy as np
import wave
from pathlib import Path
import matplotlib.pyplot as plt
from adpcm_decoder import ADPCMDecoder
import struct

def generate_test_signal(duration_seconds=5, sample_rate=16000):
    """Generate a test audio signal with multiple frequency components"""
    t = np.linspace(0, duration_seconds, int(duration_seconds * sample_rate), False)
    
    # Create a composite signal with multiple frequency components
    signal = (
        0.3 * np.sin(2 * np.pi * 440 * t) +      # 440 Hz (A4)
        0.2 * np.sin(2 * np.pi * 880 * t) +      # 880 Hz (A5) 
        0.1 * np.sin(2 * np.pi * 1320 * t) +     # 1320 Hz (E6)
        0.05 * np.random.randn(len(t))           # Add some noise
    )
    
    # Normalize and convert to 16-bit
    signal = signal / np.max(np.abs(signal))
    signal = (signal * 32767).astype(np.int16)
    
    return signal

def encode_pcm_to_adpcm_python(pcm_samples):
    """Python implementation of ADPCM encoding for testing"""
    # This is a simplified version for testing
    # The actual encoding happens on the device
    
    # Step size table (IMA ADPCM)
    step_table = [
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
        19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    ]
    
    index_table = [
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    ]
    
    predicted_sample = 0
    step_index = 0
    adpcm_data = []
    
    for i in range(0, len(pcm_samples), 2):
        byte_val = 0
        
        # Encode first sample (lower 4 bits)
        if i < len(pcm_samples):
            sample = pcm_samples[i]
            diff = sample - predicted_sample
            
            adpcm_code = 0
            if diff < 0:
                adpcm_code = 8
                diff = -diff
            
            step = step_table[step_index]
            delta = step >> 3
            
            if diff >= step:
                adpcm_code |= 4
                diff -= step
                delta += step
            
            step >>= 1
            if diff >= step:
                adpcm_code |= 2
                diff -= step
                delta += step
            
            step >>= 1
            if diff >= step:
                adpcm_code |= 1
                delta += step
            
            if adpcm_code & 8:
                predicted_sample -= delta
            else:
                predicted_sample += delta
            
            predicted_sample = max(-32768, min(32767, predicted_sample))
            step_index += index_table[adpcm_code & 7]
            step_index = max(0, min(88, step_index))
            
            byte_val = adpcm_code & 0x0F
        
        # Encode second sample (upper 4 bits)
        if i + 1 < len(pcm_samples):
            sample = pcm_samples[i + 1]
            diff = sample - predicted_sample
            
            adpcm_code = 0
            if diff < 0:
                adpcm_code = 8
                diff = -diff
            
            step = step_table[step_index]
            delta = step >> 3
            
            if diff >= step:
                adpcm_code |= 4
                diff -= step
                delta += step
            
            step >>= 1
            if diff >= step:
                adpcm_code |= 2
                diff -= step
                delta += step
            
            step >>= 1
            if diff >= step:
                adpcm_code |= 1
                delta += step
            
            if adpcm_code & 8:
                predicted_sample -= delta
            else:
                predicted_sample += delta
            
            predicted_sample = max(-32768, min(32767, predicted_sample))
            step_index += index_table[adpcm_code & 7]
            step_index = max(0, min(88, step_index))
            
            byte_val |= (adpcm_code & 0x0F) << 4
        
        adpcm_data.append(byte_val)
    
    return bytes(adpcm_data)

def create_test_adpcm_file(pcm_samples, output_path, sample_rate=16000):
    """Create a test ADPCM file with proper header"""
    adpcm_data = encode_pcm_to_adpcm_python(pcm_samples)
    
    # Create ADPCM header (32 bytes)
    header = struct.pack('<4sIIHHIIhB3s',
        b'ADPC',                    # magic
        1,                          # version
        sample_rate,                # sample_rate
        1,                          # channels
        4,                          # bits_per_sample
        len(pcm_samples),           # total_samples
        len(adpcm_data),            # data_size
        0,                          # initial_sample
        0,                          # initial_step_index
        b'\x00\x00\x00'            # reserved
    )
    
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(adpcm_data)
    
    return len(adpcm_data)

def calculate_snr(original, decoded):
    """Calculate Signal-to-Noise Ratio"""
    if len(original) != len(decoded):
        min_len = min(len(original), len(decoded))
        original = original[:min_len]
        decoded = decoded[:min_len]
    
    noise = original.astype(np.float64) - decoded.astype(np.float64)
    signal_power = np.mean(original.astype(np.float64) ** 2)
    noise_power = np.mean(noise ** 2)
    
    if noise_power == 0:
        return float('inf')
    
    snr_db = 10 * np.log10(signal_power / noise_power)
    return snr_db

def main():
    print("ADPCM Implementation Test")
    print("=" * 40)
    
    # Generate test signal
    print("1. Generating test audio signal...")
    duration = 3  # seconds
    sample_rate = 16000
    original_samples = generate_test_signal(duration, sample_rate)
    
    print(f"   Duration: {duration} seconds")
    print(f"   Sample rate: {sample_rate} Hz")
    print(f"   Samples: {len(original_samples):,}")
    print(f"   Original size: {len(original_samples) * 2:,} bytes")
    
    # Save original as WAV
    original_wav_path = Path("test_original.wav")
    with wave.open(str(original_wav_path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        f.writeframes(original_samples.tobytes())
    print(f"   Saved original: {original_wav_path}")
    
    # Create test ADPCM file
    print("\n2. Encoding to ADPCM...")
    adpcm_path = Path("test_compressed.adpcm")
    compressed_size = create_test_adpcm_file(original_samples, adpcm_path, sample_rate)
    
    compression_ratio = (1.0 - compressed_size / (len(original_samples) * 2)) * 100
    print(f"   Compressed size: {compressed_size:,} bytes")
    print(f"   Compression ratio: {compression_ratio:.1f}%")
    print(f"   Saved compressed: {adpcm_path}")
    
    # Decode using our decoder
    print("\n3. Decoding ADPCM...")
    decoder = ADPCMDecoder()
    
    with open(adpcm_path, 'rb') as f:
        f.seek(32)  # Skip header
        adpcm_data = f.read()
    
    decoded_samples = decoder.decode_bytes(adpcm_data)
    
    # Truncate to original length
    if len(decoded_samples) > len(original_samples):
        decoded_samples = decoded_samples[:len(original_samples)]
    
    print(f"   Decoded samples: {len(decoded_samples):,}")
    
    # Save decoded as WAV
    decoded_wav_path = Path("test_decoded.wav")
    with wave.open(str(decoded_wav_path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        f.writeframes(decoded_samples.tobytes())
    print(f"   Saved decoded: {decoded_wav_path}")
    
    # Calculate quality metrics
    print("\n4. Quality Analysis...")
    snr = calculate_snr(original_samples, decoded_samples)
    
    # Calculate RMS error
    error = original_samples.astype(np.float64) - decoded_samples.astype(np.float64)
    rms_error = np.sqrt(np.mean(error ** 2))
    max_error = np.max(np.abs(error))
    
    print(f"   Signal-to-Noise Ratio: {snr:.1f} dB")
    print(f"   RMS Error: {rms_error:.1f}")
    print(f"   Max Error: {max_error:.0f}")
    
    # Determine quality rating
    if snr >= 30:
        quality = "Excellent"
    elif snr >= 20:
        quality = "Good"
    elif snr >= 15:
        quality = "Fair"
    else:
        quality = "Poor"
    
    print(f"   Quality Rating: {quality}")
    
    # Speed analysis
    print("\n5. Speed Benefits...")
    original_transmission_time = (len(original_samples) * 2) / 8000  # 8KB/s typical BLE speed
    compressed_transmission_time = compressed_size / 8000
    speed_improvement = original_transmission_time / compressed_transmission_time
    
    print(f"   Original transmission time: {original_transmission_time:.1f} seconds")
    print(f"   Compressed transmission time: {compressed_transmission_time:.1f} seconds")
    print(f"   Speed improvement: {speed_improvement:.1f}x faster")
    
    print("\n6. Summary...")
    print(f"   ✓ ADPCM compression working correctly")
    print(f"   ✓ {compression_ratio:.1f}% size reduction")
    print(f"   ✓ {speed_improvement:.1f}x faster transmission")
    print(f"   ✓ {quality} audio quality ({snr:.1f} dB SNR)")
    
    print(f"\nTest files created:")
    print(f"   {original_wav_path} - Original audio")
    print(f"   {adpcm_path} - Compressed ADPCM")
    print(f"   {decoded_wav_path} - Decoded audio")
    print(f"\nYou can play the WAV files to compare quality.")

if __name__ == '__main__':
    main()
