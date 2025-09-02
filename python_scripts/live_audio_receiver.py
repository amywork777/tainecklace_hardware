#!/usr/bin/env python3
"""
Live Audio Streaming Receiver for XIAO Voice Logger

This script connects to the XIAO-REC device's live audio streaming service
and plays back the real-time compressed audio stream.

Requirements:
    pip install bleak numpy sounddevice threading

Usage:
    python live_audio_receiver.py
    python live_audio_receiver.py --device-address XX:XX:XX:XX:XX:XX
    python live_audio_receiver.py --save-to-file stream_recording.wav
    python live_audio_receiver.py --no-playback  # Just save, don't play
"""

import asyncio
import struct
import argparse
import time
import threading
import queue
from pathlib import Path
from typing import Optional
import numpy as np

try:
    from bleak import BleakScanner, BleakClient
    from bleak.backends.device import BLEDevice
except ImportError:
    print("ERROR: bleak library not installed")
    print("Install with: pip install bleak")
    exit(1)

try:
    import sounddevice as sd
except ImportError:
    print("ERROR: sounddevice library not installed")
    print("Install with: pip install sounddevice")
    exit(1)

# BLE UUIDs for live audio streaming service
UUID_AUDIO_SERVICE = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001"
UUID_AUDIO_STREAM  = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0002"  # notify: audio chunks
UUID_AUDIO_CONTROL = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0003"  # write: start/stop
UUID_AUDIO_STATUS  = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0004"  # read: format info

class ADPCMDecoder:
    """Real-time ADPCM decoder for live streaming"""
    
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
    
    def decode_chunk(self, adpcm_data: bytes) -> np.ndarray:
        """Decode ADPCM bytes to PCM samples for real-time playback"""
        samples = []
        
        for byte_val in adpcm_data:
            # Decode first sample (lower 4 bits)
            sample1 = self.decode_sample(byte_val & 0x0F)
            samples.append(sample1)
            
            # Decode second sample (upper 4 bits)
            sample2 = self.decode_sample((byte_val >> 4) & 0x0F)
            samples.append(sample2)
        
        return np.array(samples, dtype=np.int16)

class AudioBuffer:
    """Thread-safe circular buffer for audio streaming"""
    
    def __init__(self, max_samples: int = 16000 * 2):  # 2 seconds at 16kHz
        self.max_samples = max_samples
        self.buffer = np.zeros(max_samples, dtype=np.int16)
        self.write_pos = 0
        self.read_pos = 0
        self.samples_available = 0
        self.lock = threading.Lock()
        self.overruns = 0
    
    def write(self, samples: np.ndarray):
        """Write samples to buffer"""
        with self.lock:
            samples_to_write = len(samples)
            
            # Check for buffer overflow
            if self.samples_available + samples_to_write > self.max_samples:
                # Calculate how many samples to drop
                overflow = (self.samples_available + samples_to_write) - self.max_samples
                self.overruns += overflow
                
                # Advance read position to make space
                self.read_pos = (self.read_pos + overflow) % self.max_samples
                self.samples_available -= overflow
            
            # Write samples with wraparound
            for i, sample in enumerate(samples):
                self.buffer[self.write_pos] = sample
                self.write_pos = (self.write_pos + 1) % self.max_samples
            
            self.samples_available += samples_to_write
    
    def read(self, num_samples: int) -> np.ndarray:
        """Read samples from buffer"""
        with self.lock:
            if self.samples_available == 0:
                return np.zeros(num_samples, dtype=np.int16)
            
            samples_to_read = min(num_samples, self.samples_available)
            result = np.zeros(samples_to_read, dtype=np.int16)
            
            # Read samples with wraparound
            for i in range(samples_to_read):
                result[i] = self.buffer[self.read_pos]
                self.read_pos = (self.read_pos + 1) % self.max_samples
            
            self.samples_available -= samples_to_read
            
            # Pad with zeros if we don't have enough samples
            if samples_to_read < num_samples:
                padding = np.zeros(num_samples - samples_to_read, dtype=np.int16)
                result = np.concatenate([result, padding])
            
            return result
    
    def get_status(self) -> dict:
        """Get buffer status"""
        with self.lock:
            fill_percent = (self.samples_available / self.max_samples) * 100
            return {
                'samples_available': self.samples_available,
                'fill_percent': fill_percent,
                'overruns': self.overruns
            }

class LiveAudioReceiver:
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.decoder = ADPCMDecoder()
        self.audio_buffer = AudioBuffer()
        self.stream_info = {}
        self.streaming_active = False
        
        # Statistics
        self.packets_received = 0
        self.bytes_received = 0
        self.start_time = 0
        self.last_seq = -1
        self.packet_losses = 0
        
        # Audio playback
        self.audio_stream = None
        self.playback_enabled = True
        
        # Recording to file
        self.save_to_file = False
        self.output_file = None
        self.recorded_samples = []
    
    async def scan_for_device(self, timeout: int = 10) -> Optional[BLEDevice]:
        """Scan for XIAO-REC device"""
        print(f"Scanning for XIAO-REC device (timeout: {timeout}s)...")
        
        devices = await BleakScanner.discover(timeout=timeout)
        
        print(f"Found {len(devices)} BLE devices:")
        xiao_device = None
        
        for device in devices:
            print(f"  - {device.name or 'Unknown'} ({device.address})")
            
            # Check device name
            if device.name and ("XIAO" in device.name.upper() or "REC" in device.name.upper()):
                print(f"  ✓ Found XIAO device: {device.name}")
                xiao_device = device
                break
        
        return xiao_device
    
    async def connect_to_device(self, device: BLEDevice) -> bool:
        """Connect to the BLE device"""
        try:
            print(f"Connecting to {device.address}...")
            self.client = BleakClient(device.address)
            await self.client.connect()
            
            if self.client.is_connected:
                print("✓ Connected successfully")
                return True
            else:
                print("✗ Connection failed")
                return False
                
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    async def read_stream_info(self) -> bool:
        """Read streaming format information"""
        try:
            print("Reading stream format info...")
            status_data = await self.client.read_gatt_char(UUID_AUDIO_STATUS)
            
            if len(status_data) < 12:
                print("Invalid status data")
                return False
            
            # Parse: [sample_rate:4][channels:2][bits_per_sample:2][chunk_size:4]
            sample_rate = struct.unpack('<I', status_data[0:4])[0]
            channels = struct.unpack('<H', status_data[4:6])[0]
            bits_per_sample = struct.unpack('<H', status_data[6:8])[0]
            chunk_size = struct.unpack('<I', status_data[8:12])[0]
            
            self.stream_info = {
                'sample_rate': sample_rate,
                'channels': channels,
                'bits_per_sample': bits_per_sample,
                'chunk_size': chunk_size
            }
            
            print(f"Stream format: {sample_rate}Hz, {channels}ch, {bits_per_sample}-bit ADPCM")
            print(f"Chunk size: {chunk_size} bytes ({chunk_size * 2} samples)")
            
            return True
            
        except Exception as e:
            print(f"Error reading stream info: {e}")
            return False
    
    async def start_streaming(self) -> bool:
        """Start live audio streaming"""
        try:
            print("Starting live audio stream...")
            
            # Subscribe to audio stream notifications
            await self.client.start_notify(UUID_AUDIO_STREAM, self.audio_notification_handler)
            print("✓ Subscribed to audio stream")
            
            # Send start command to device
            await self.client.write_gatt_char(UUID_AUDIO_CONTROL, bytes([1]), response=False)
            print("✓ Sent start command to device")
            
            self.streaming_active = True
            self.start_time = time.time()
            
            return True
            
        except Exception as e:
            print(f"Error starting stream: {e}")
            return False
    
    async def stop_streaming(self):
        """Stop live audio streaming"""
        try:
            if self.streaming_active:
                print("Stopping live audio stream...")
                
                # Send stop command to device
                await self.client.write_gatt_char(UUID_AUDIO_CONTROL, bytes([0]), response=False)
                
                # Stop notifications
                await self.client.stop_notify(UUID_AUDIO_STREAM)
                
                self.streaming_active = False
                print("✓ Streaming stopped")
                
        except Exception as e:
            print(f"Error stopping stream: {e}")
    
    async def audio_notification_handler(self, sender, data: bytes):
        """Handle incoming audio stream packets"""
        if len(data) < 10:
            print(f"Packet too short: {len(data)} bytes")
            return
        
        # Parse packet: [seq:4][timestamp:4][size:2][adpcm_data]
        seq = struct.unpack('<I', data[0:4])[0]
        timestamp = struct.unpack('<I', data[4:8])[0]
        size = struct.unpack('<H', data[8:10])[0]
        adpcm_data = data[10:10+size]
        
        # Check for packet loss
        if self.last_seq >= 0 and seq != self.last_seq + 1:
            lost_packets = seq - self.last_seq - 1
            self.packet_losses += lost_packets
            if lost_packets > 0:
                print(f"\n⚠ Lost {lost_packets} packet(s) (seq {self.last_seq + 1} to {seq - 1})")
        
        self.last_seq = seq
        self.packets_received += 1
        self.bytes_received += len(adpcm_data)
        
        # Decode ADPCM to PCM
        try:
            pcm_samples = self.decoder.decode_chunk(adpcm_data)
            
            # Add to audio buffer for playback
            self.audio_buffer.write(pcm_samples)
            
            # Add to recording if enabled
            if self.save_to_file:
                self.recorded_samples.extend(pcm_samples)
            
            # Print statistics periodically
            if self.packets_received % 50 == 0:  # Every 50 packets
                elapsed = time.time() - self.start_time
                rate = self.bytes_received / elapsed if elapsed > 0 else 0
                buffer_status = self.audio_buffer.get_status()
                
                print(f"\rStream: {self.packets_received} pkts, {self.bytes_received:,} bytes, "
                      f"{rate/1024:.1f} KB/s, buffer: {buffer_status['fill_percent']:.1f}%, "
                      f"losses: {self.packet_losses}", end='', flush=True)
                
        except Exception as e:
            print(f"\nDecode error on packet {seq}: {e}")
    
    def audio_callback(self, outdata, frames, time, status):
        """Callback for audio playback"""
        if status:
            print(f"Audio callback status: {status}")
        
        # Read samples from buffer
        samples = self.audio_buffer.read(frames)
        
        # Convert to float32 for sounddevice (range -1.0 to 1.0)
        outdata[:, 0] = samples.astype(np.float32) / 32768.0
    
    def start_audio_playback(self):
        """Start real-time audio playback"""
        if not self.playback_enabled:
            return
        
        try:
            sample_rate = self.stream_info.get('sample_rate', 16000)
            
            print(f"Starting audio playback at {sample_rate}Hz...")
            
            self.audio_stream = sd.OutputStream(
                samplerate=sample_rate,
                channels=1,  # Mono
                dtype=np.float32,
                callback=self.audio_callback,
                blocksize=1024,  # Small block size for low latency
                latency='low'
            )
            
            self.audio_stream.start()
            print("✓ Audio playback started")
            
        except Exception as e:
            print(f"Audio playback error: {e}")
            print("Audio playback disabled")
            self.playback_enabled = False
    
    def stop_audio_playback(self):
        """Stop audio playback"""
        if self.audio_stream:
            self.audio_stream.stop()
            self.audio_stream.close()
            self.audio_stream = None
            print("✓ Audio playback stopped")
    
    def save_recording(self, output_path: Path):
        """Save recorded audio to WAV file"""
        if not self.recorded_samples:
            print("No audio recorded")
            return
        
        try:
            import wave
            
            sample_rate = self.stream_info.get('sample_rate', 16000)
            samples_array = np.array(self.recorded_samples, dtype=np.int16)
            
            with wave.open(str(output_path), 'wb') as wav_file:
                wav_file.setnchannels(1)  # Mono
                wav_file.setsampwidth(2)  # 16-bit
                wav_file.setframerate(sample_rate)
                wav_file.writeframes(samples_array.tobytes())
            
            duration = len(samples_array) / sample_rate
            file_size = output_path.stat().st_size
            
            print(f"\n✓ Recording saved: {output_path}")
            print(f"  Duration: {duration:.1f}s")
            print(f"  Samples: {len(samples_array):,}")
            print(f"  File size: {file_size:,} bytes")
            
        except Exception as e:
            print(f"Error saving recording: {e}")
    
    async def disconnect(self):
        """Disconnect from device"""
        await self.stop_streaming()
        self.stop_audio_playback()
        
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("✓ Disconnected")

async def main():
    parser = argparse.ArgumentParser(description='Live Audio Streaming Receiver for XIAO Voice Logger')
    parser.add_argument('--device-address', help='Connect to specific device address')
    parser.add_argument('--timeout', type=int, default=10, help='Scan timeout (default: 10s)')
    parser.add_argument('--save-to-file', help='Save stream to WAV file')
    parser.add_argument('--no-playback', action='store_true', help='Disable real-time playback')
    parser.add_argument('--list-audio-devices', action='store_true', help='List available audio devices')
    
    args = parser.parse_args()
    
    if args.list_audio_devices:
        print("Available audio devices:")
        print(sd.query_devices())
        return
    
    receiver = LiveAudioReceiver()
    
    # Configure recording
    if args.save_to_file:
        receiver.save_to_file = True
        receiver.output_file = Path(args.save_to_file)
        print(f"Will save stream to: {receiver.output_file}")
    
    # Configure playback
    if args.no_playback:
        receiver.playback_enabled = False
        print("Real-time playback disabled")
    
    try:
        # Scan for device
        if args.device_address:
            devices = await BleakScanner.discover(timeout=args.timeout)
            device = None
            for d in devices:
                if d.address.lower() == args.device_address.lower():
                    device = d
                    break
            if not device:
                print(f"Device {args.device_address} not found")
                return
        else:
            device = await receiver.scan_for_device(args.timeout)
            if not device:
                print("\nMake sure XIAO device is powered on and advertising.")
                print("On the device, use 'a' command to start audio advertising.")
                return
        
        # Connect and start streaming
        if await receiver.connect_to_device(device):
            if await receiver.read_stream_info():
                # Start audio playback first
                receiver.start_audio_playback()
                
                # Start streaming
                if await receiver.start_streaming():
                    print("\n🎵 Live audio streaming active!")
                    print("Press Ctrl+C to stop streaming\n")
                    
                    try:
                        # Keep streaming until interrupted
                        while receiver.streaming_active:
                            await asyncio.sleep(1)
                            
                            # Print periodic status
                            if receiver.packets_received > 0:
                                elapsed = time.time() - receiver.start_time
                                buffer_status = receiver.audio_buffer.get_status()
                                
                                if elapsed > 0 and receiver.packets_received % 100 == 0:
                                    rate = receiver.bytes_received / elapsed
                                    print(f"\nStatus: {elapsed:.0f}s, {receiver.packets_received} packets, "
                                          f"{rate/1024:.1f} KB/s, buffer: {buffer_status['fill_percent']:.1f}%")
                                    
                                    if buffer_status['overruns'] > 0:
                                        print(f"⚠ Buffer overruns: {buffer_status['overruns']}")
                    
                    except KeyboardInterrupt:
                        print("\nStopping stream...")
                
                else:
                    print("Failed to start streaming")
            else:
                print("Could not read stream info")
        else:
            print("Connection failed")
    
    except Exception as e:
        print(f"Unexpected error: {e}")
    
    finally:
        await receiver.disconnect()
        
        # Save recording if enabled
        if receiver.save_to_file and receiver.output_file:
            receiver.save_recording(receiver.output_file)
        
        # Print final statistics
        if receiver.packets_received > 0:
            elapsed = time.time() - receiver.start_time
            print(f"\nFinal stats:")
            print(f"  Packets received: {receiver.packets_received}")
            print(f"  Bytes received: {receiver.bytes_received:,}")
            print(f"  Packet losses: {receiver.packet_losses}")
            print(f"  Duration: {elapsed:.1f}s")
            if elapsed > 0:
                print(f"  Average rate: {receiver.bytes_received / elapsed / 1024:.1f} KB/s")

if __name__ == '__main__':
    asyncio.run(main())
