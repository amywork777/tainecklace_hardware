#!/usr/bin/env python3
"""
ADPCM to WAV Decoder for XIAO Voice Logger

This script decodes ADPCM compressed audio files received from the XIAO device
back to standard WAV format for playback and analysis.

Requirements:
    pip install numpy wave

Usage:
    python adpcm_decoder.py input.adpcm output.wav
    python adpcm_decoder.py --batch *.adpcm
    python adpcm_decoder.py --info input.adpcm
"""

import argparse
import struct
import wave
import numpy as np
from pathlib import Path
from typing import Tuple, Optional
import sys
import glob

class ADPCMDecoder:
    """IMA ADPCM Decoder matching the device implementation"""
    
    # IMA ADPCM step size table (89 entries)
    STEP_TABLE = [
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
    
    # Index adjustment table for ADPCM
    INDEX_TABLE = [
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    ]
    
    def __init__(self):
        self.reset()
    
    def reset(self, initial_sample: int = 0, initial_step_index: int = 0):
        """Reset decoder state"""
        self.predicted_sample = initial_sample
        self.step_index = initial_step_index
    
    def decode_sample(self, adpcm_code: int) -> int:
        """Decode a single 4-bit ADPCM code to 16-bit PCM sample"""
        # Get current step size
        step = self.STEP_TABLE[self.step_index]
        
        # Calculate delta
        delta = step >> 3
        
        if adpcm_code & 4:
            delta += step
        if adpcm_code & 2:
            delta += step >> 1
        if adpcm_code & 1:
            delta += step >> 2
        
        # Apply sign
        if adpcm_code & 8:
            self.predicted_sample -= delta
        else:
            self.predicted_sample += delta
        
        # Clamp to 16-bit range
        self.predicted_sample = max(-32768, min(32767, self.predicted_sample))
        
        # Update step index
        self.step_index += self.INDEX_TABLE[adpcm_code & 7]
        self.step_index = max(0, min(88, self.step_index))
        
        return self.predicted_sample
    
    def decode_bytes(self, adpcm_data: bytes) -> np.ndarray:
        """Decode ADPCM bytes to PCM samples"""
        samples = []
        
        for byte_val in adpcm_data:
            # Decode first sample (lower 4 bits)
            sample1 = self.decode_sample(byte_val & 0x0F)
            samples.append(sample1)
            
            # Decode second sample (upper 4 bits)
            sample2 = self.decode_sample((byte_val >> 4) & 0x0F)
            samples.append(sample2)
        
        return np.array(samples, dtype=np.int16)

class ADPCMFile:
    """ADPCM file format handler"""
    
    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.header = None
        self.audio_data = None
        
    def read_header(self) -> dict:
        """Read and parse ADPCM file header"""
        with open(self.filepath, 'rb') as f:
            header_data = f.read(32)  # ADPCMHeader is 32 bytes
            
            if len(header_data) < 32:
                raise ValueError("File too short to contain valid header")
            
            # Parse header: magic[4] + version[4] + sample_rate[4] + channels[2] + 
            #               bits_per_sample[2] + total_samples[4] + data_size[4] + 
            #               initial_sample[2] + initial_step_index[1] + reserved[5]
            header = struct.unpack('<4sIIHHIIhB5s', header_data)
            
            magic = header[0]
            if magic != b'ADPC':
                raise ValueError(f"Invalid magic number: {magic} (expected b'ADPC')")
            
            self.header = {
                'magic': magic,
                'version': header[1],
                'sample_rate': header[2],
                'channels': header[3],
                'bits_per_sample': header[4],
                'total_samples': header[5],
                'data_size': header[6],
                'initial_sample': header[7],
                'initial_step_index': header[8],
                'reserved': header[9]
            }
            
            return self.header
    
    def read_audio_data(self) -> bytes:
        """Read compressed audio data from file"""
        if not self.header:
            self.read_header()
        
        with open(self.filepath, 'rb') as f:
            f.seek(32)  # Skip header
            self.audio_data = f.read(self.header['data_size'])
            
        return self.audio_data
    
    def get_info(self) -> dict:
        """Get file information"""
        if not self.header:
            self.read_header()
        
        duration_seconds = self.header['total_samples'] / self.header['sample_rate']
        original_size = self.header['total_samples'] * 2  # 16-bit samples
        compressed_size = self.header['data_size']
        compression_ratio = (1.0 - compressed_size / original_size) * 100
        
        return {
            'filepath': self.filepath,
            'sample_rate': self.header['sample_rate'],
            'channels': self.header['channels'],
            'total_samples': self.header['total_samples'],
            'duration_seconds': duration_seconds,
            'original_size_bytes': original_size,
            'compressed_size_bytes': compressed_size,
            'compression_ratio_percent': compression_ratio,
            'initial_sample': self.header['initial_sample'],
            'initial_step_index': self.header['initial_step_index']
        }

def decode_adpcm_file(input_path: Path, output_path: Optional[Path] = None) -> bool:
    """Decode an ADPCM file to WAV format"""
    try:
        # Read ADPCM file
        adpcm_file = ADPCMFile(input_path)
        header = adpcm_file.read_header()
        audio_data = adpcm_file.read_audio_data()
        
        print(f"Decoding {input_path.name}...")
        print(f"  Sample rate: {header['sample_rate']} Hz")
        print(f"  Channels: {header['channels']}")
        print(f"  Total samples: {header['total_samples']:,}")
        print(f"  Compressed size: {header['data_size']:,} bytes")
        
        # Initialize decoder with file's initial state
        decoder = ADPCMDecoder()
        decoder.reset(header['initial_sample'], header['initial_step_index'])
        
        # Decode audio data
        pcm_samples = decoder.decode_bytes(audio_data)
        
        # Truncate to expected sample count (in case of padding)
        if len(pcm_samples) > header['total_samples']:
            pcm_samples = pcm_samples[:header['total_samples']]
        
        print(f"  Decoded {len(pcm_samples):,} samples")
        
        # Generate output filename if not provided
        if output_path is None:
            output_path = input_path.with_suffix('.wav')
        
        # Write WAV file
        with wave.open(str(output_path), 'wb') as wav_file:
            wav_file.setnchannels(header['channels'])
            wav_file.setsampwidth(2)  # 16-bit samples
            wav_file.setframerate(header['sample_rate'])
            wav_file.writeframes(pcm_samples.tobytes())
        
        file_size = output_path.stat().st_size
        duration = len(pcm_samples) / header['sample_rate']
        compression_ratio = (1.0 - header['data_size'] / (len(pcm_samples) * 2)) * 100
        
        print(f"  ✓ Saved: {output_path} ({file_size:,} bytes, {duration:.1f}s)")
        print(f"  Compression ratio: {compression_ratio:.1f}%")
        
        return True
        
    except Exception as e:
        print(f"  ✗ Error decoding {input_path}: {e}")
        return False

def show_file_info(input_path: Path):
    """Show information about an ADPCM file"""
    try:
        adpcm_file = ADPCMFile(input_path)
        info = adpcm_file.get_info()
        
        print(f"\nFile: {info['filepath']}")
        print(f"Sample rate: {info['sample_rate']:,} Hz")
        print(f"Channels: {info['channels']}")
        print(f"Duration: {info['duration_seconds']:.2f} seconds")
        print(f"Total samples: {info['total_samples']:,}")
        print(f"Original size: {info['original_size_bytes']:,} bytes ({info['original_size_bytes']/1024:.1f} KB)")
        print(f"Compressed size: {info['compressed_size_bytes']:,} bytes ({info['compressed_size_bytes']/1024:.1f} KB)")
        print(f"Compression ratio: {info['compression_ratio_percent']:.1f}%")
        print(f"Initial decoder state: sample={info['initial_sample']}, step_index={info['initial_step_index']}")
        
    except Exception as e:
        print(f"Error reading {input_path}: {e}")

def main():
    parser = argparse.ArgumentParser(description='ADPCM to WAV Decoder for XIAO Voice Logger')
    parser.add_argument('input', nargs='?', help='Input ADPCM file(s) or pattern')
    parser.add_argument('output', nargs='?', help='Output WAV file (optional)')
    parser.add_argument('--batch', action='store_true', help='Batch process multiple files')
    parser.add_argument('--info', action='store_true', help='Show file information only')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    if not args.input:
        # Look for ADPCM files in current directory
        adpcm_files = list(Path('.').glob('*.adpcm')) + list(Path('.').glob('*.ADPCM'))
        if not adpcm_files:
            print("No ADPCM files found. Please specify input file(s).")
            print("\nUsage examples:")
            print("  python adpcm_decoder.py REC_0001.adpcm")
            print("  python adpcm_decoder.py REC_0001.adpcm output.wav")
            print("  python adpcm_decoder.py --batch *.adpcm")
            print("  python adpcm_decoder.py --info REC_0001.adpcm")
            return
        
        print(f"Found {len(adpcm_files)} ADPCM file(s) in current directory:")
        for f in adpcm_files:
            print(f"  {f}")
        
        response = input("\nDecode all files? (y/N): ")
        if response.lower() != 'y':
            return
        
        args.batch = True
        input_files = adpcm_files
    else:
        # Handle glob patterns
        if '*' in args.input or '?' in args.input:
            input_files = [Path(f) for f in glob.glob(args.input)]
            args.batch = True
        else:
            input_files = [Path(args.input)]
    
    if not input_files:
        print(f"No files found matching: {args.input}")
        return
    
    success_count = 0
    
    for input_path in input_files:
        if not input_path.exists():
            print(f"File not found: {input_path}")
            continue
        
        if args.info:
            show_file_info(input_path)
            success_count += 1
        else:
            output_path = None
            if not args.batch and args.output:
                output_path = Path(args.output)
            
            if decode_adpcm_file(input_path, output_path):
                success_count += 1
    
    if not args.info:
        print(f"\nCompleted: {success_count}/{len(input_files)} files processed successfully")

if __name__ == '__main__':
    main()
