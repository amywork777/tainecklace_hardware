#!/usr/bin/env python3
"""
WAV File Player and Analyzer for XIAO Voice Logger

This script can:
- Play WAV files recorded by the device
- Analyze audio properties 
- Convert between formats
- Visualize waveforms

Usage:
    python open_wav.py <filename.wav>
    python open_wav.py --list
    python open_wav.py --analyze <filename.wav>
"""

import os
import sys
import wave
import struct
import argparse
from pathlib import Path

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("Note: Install numpy for waveform analysis: pip install numpy")

try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Note: Install matplotlib for plotting: pip install matplotlib")

try:
    import pygame
    HAS_PYGAME = True
except ImportError:
    HAS_PYGAME = False
    print("Note: Install pygame for audio playback: pip install pygame")

def find_wav_files():
    """Find all WAV files in current directory"""
    wav_files = []
    for f in Path('.').glob('*.wav'):
        wav_files.append(f)
    for f in Path('.').glob('*.WAV'):
        wav_files.append(f)
    return sorted(wav_files)

def analyze_wav(filename):
    """Analyze WAV file properties"""
    try:
        with wave.open(str(filename), 'rb') as w:
            frames = w.getnframes()
            sample_rate = w.getframerate()
            channels = w.getnchannels()
            sample_width = w.getsampwidth()
            duration = frames / sample_rate
            
            print(f"\n=== WAV File Analysis: {filename} ===")
            print(f"Duration: {duration:.2f} seconds")
            print(f"Sample Rate: {sample_rate} Hz")
            print(f"Channels: {channels}")
            print(f"Sample Width: {sample_width} bytes ({sample_width*8} bit)")
            print(f"Total Frames: {frames:,}")
            print(f"File Size: {os.path.getsize(filename):,} bytes")
            
            if HAS_NUMPY:
                # Read audio data for analysis
                audio_data = w.readframes(frames)
                if sample_width == 2:  # 16-bit
                    samples = np.frombuffer(audio_data, dtype=np.int16)
                    if channels == 2:
                        samples = samples.reshape(-1, 2)
                    
                    print(f"Peak Amplitude: {np.max(np.abs(samples))}")
                    print(f"RMS Level: {np.sqrt(np.mean(samples**2)):.1f}")
                    
                    # Check for clipping
                    max_val = 2**(sample_width*8-1) - 1
                    clipped = np.sum(np.abs(samples) >= max_val)
                    if clipped > 0:
                        print(f"⚠️  Clipped samples: {clipped} ({clipped/len(samples)*100:.2f}%)")
                    else:
                        print("✓ No clipping detected")
                        
            return True
            
    except Exception as e:
        print(f"Error analyzing {filename}: {e}")
        return False

def plot_waveform(filename, max_points=10000):
    """Plot waveform of WAV file"""
    if not HAS_NUMPY or not HAS_MATPLOTLIB:
        print("Need numpy and matplotlib for plotting")
        return
        
    try:
        with wave.open(str(filename), 'rb') as w:
            frames = w.getnframes()
            sample_rate = w.getframerate()
            channels = w.getnchannels()
            sample_width = w.getsampwidth()
            
            audio_data = w.readframes(frames)
            if sample_width == 2:
                samples = np.frombuffer(audio_data, dtype=np.int16)
                if channels == 2:
                    samples = samples[:, 0]  # Take left channel
                
                # Downsample for plotting if too many points
                if len(samples) > max_points:
                    step = len(samples) // max_points
                    samples = samples[::step]
                
                time = np.arange(len(samples)) / sample_rate
                
                plt.figure(figsize=(12, 6))
                plt.plot(time, samples)
                plt.title(f'Waveform: {filename}')
                plt.xlabel('Time (seconds)')
                plt.ylabel('Amplitude')
                plt.grid(True, alpha=0.3)
                plt.show()
                
    except Exception as e:
        print(f"Error plotting {filename}: {e}")

def play_wav(filename):
    """Play WAV file using pygame"""
    if not HAS_PYGAME:
        print("Install pygame for audio playback: pip install pygame")
        return
        
    try:
        pygame.mixer.init()
        pygame.mixer.music.load(str(filename))
        print(f"Playing {filename}... (Press Ctrl+C to stop)")
        pygame.mixer.music.play()
        
        while pygame.mixer.music.get_busy():
            pygame.time.wait(100)
            
    except KeyboardInterrupt:
        pygame.mixer.music.stop()
        print("\nPlayback stopped")
    except Exception as e:
        print(f"Error playing {filename}: {e}")

def list_wav_files():
    """List all WAV files with basic info"""
    files = find_wav_files()
    if not files:
        print("No WAV files found in current directory")
        return
        
    print(f"\n=== Found {len(files)} WAV files ===")
    for f in files:
        size = os.path.getsize(f)
        print(f"  {f.name:<20} ({size:,} bytes)")
        
        # Quick analysis
        try:
            with wave.open(str(f), 'rb') as w:
                duration = w.getnframes() / w.getframerate()
                print(f"    {duration:.1f}s, {w.getframerate()}Hz, {w.getnchannels()}ch")
        except:
            print("    (Error reading file)")

def main():
    parser = argparse.ArgumentParser(description='WAV file player and analyzer')
    parser.add_argument('filename', nargs='?', help='WAV file to process')
    parser.add_argument('--list', '-l', action='store_true', help='List all WAV files')
    parser.add_argument('--analyze', '-a', metavar='FILE', help='Analyze WAV file')
    parser.add_argument('--plot', '-p', metavar='FILE', help='Plot waveform')
    parser.add_argument('--play', action='store_true', help='Play the audio file')
    
    args = parser.parse_args()
    
    if args.list:
        list_wav_files()
        return
        
    if args.analyze:
        analyze_wav(args.analyze)
        return
        
    if args.plot:
        plot_waveform(args.plot)
        return
        
    if args.filename:
        filename = Path(args.filename)
        if not filename.exists():
            print(f"File not found: {filename}")
            return
            
        print(f"Processing: {filename}")
        analyze_wav(filename)
        
        if args.play:
            play_wav(filename)
        else:
            response = input("\nPlay audio? (y/n): ").lower()
            if response.startswith('y'):
                play_wav(filename)
                
        response = input("Show waveform plot? (y/n): ").lower()
        if response.startswith('y'):
            plot_waveform(filename)
    else:
        # No arguments - show available files
        list_wav_files()
        print("\nUsage examples:")
        print("  python open_wav.py REC_0001.WAV")
        print("  python open_wav.py --analyze REC_0001.WAV")
        print("  python open_wav.py --plot REC_0001.WAV")

if __name__ == '__main__':
    main()