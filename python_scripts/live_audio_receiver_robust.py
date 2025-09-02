#!/usr/bin/env python3
"""
Robust Live Audio Streaming Receiver - Handles packet loss better
"""

import asyncio
import struct
import argparse
import time
import threading
import queue
import numpy as np
from bleak import BleakScanner, BleakClient

# BLE UUIDs for live audio streaming service
UUID_AUDIO_SERVICE = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001"
UUID_AUDIO_STREAM  = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0002"
UUID_AUDIO_CONTROL = "b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0003"

class RobustADPCMDecoder:
    """ADPCM decoder with packet loss handling"""
    
    def __init__(self):
        self.reset()
    
    def reset(self):
        self.predictor = 0
        self.step_index = 0
        self.step_table = [
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
            34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
            157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
            3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
            15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
        ]
        self.last_sample = 0
    
    def decode_samples(self, adpcm_data, output_samples):
        """Decode ADPCM with error recovery for lost packets"""
        output_count = 0
        
        for byte_val in adpcm_data:
            if output_count >= len(output_samples) - 1:
                break
                
            # Process two 4-bit samples per byte
            sample1 = byte_val & 0x0F
            sample2 = (byte_val >> 4) & 0x0F
            
            # Decode first sample
            output_samples[output_count] = self._decode_sample(sample1)
            output_count += 1
            
            if output_count < len(output_samples):
                # Decode second sample
                output_samples[output_count] = self._decode_sample(sample2)
                output_count += 1
        
        return output_count
    
    def _decode_sample(self, adpcm_code):
        step = self.step_table[self.step_index]
        
        # Calculate difference
        diff = step >> 3
        if adpcm_code & 4:
            diff += step
        if adpcm_code & 2:
            diff += step >> 1
        if adpcm_code & 1:
            diff += step >> 2
        
        if adpcm_code & 8:
            self.predictor -= diff
        else:
            self.predictor += diff
        
        # Clamp predictor
        if self.predictor > 32767:
            self.predictor = 32767
        elif self.predictor < -32768:
            self.predictor = -32768
        
        # Update step index
        index_delta = [-1, -1, -1, -1, 2, 4, 6, 8][adpcm_code & 7]
        self.step_index += index_delta
        
        if self.step_index < 0:
            self.step_index = 0
        elif self.step_index >= len(self.step_table):
            self.step_index = len(self.step_table) - 1
        
        self.last_sample = self.predictor
        return self.predictor

class RobustStreamingReceiver:
    def __init__(self):
        self.decoder = RobustADPCMDecoder()
        self.audio_queue = queue.Queue(maxsize=100)  # Larger buffer
        self.last_seq = -1
        self.lost_packets = 0
        self.total_packets = 0
        self.running = False
        
    async def stream_handler(self, sender, data):
        """Handle incoming audio stream with packet loss detection"""
        if len(data) < 10:
            return
            
        # Parse packet: [seq32|timestamp32|size16|adpcm_data]
        seq, timestamp, size = struct.unpack('<IIH', data[:10])
        adpcm_data = data[10:10+size]
        
        self.total_packets += 1
        
        # Detect lost packets
        if self.last_seq >= 0 and seq != self.last_seq + 1:
            lost_count = seq - self.last_seq - 1
            self.lost_packets += lost_count
            print(f"⚠ Lost {lost_count} packet(s) (seq {self.last_seq + 1} to {seq - 1})")
            
            # Insert silence for lost packets to maintain timing
            silence_samples = np.zeros(128, dtype=np.int16)  # Assume ~64 ADPCM bytes = 128 samples
            for _ in range(lost_count):
                try:
                    self.audio_queue.put_nowait(silence_samples)
                except queue.Full:
                    pass
        
        self.last_seq = seq
        
        # Decode ADPCM data
        if len(adpcm_data) > 0:
            pcm_samples = np.zeros(len(adpcm_data) * 2, dtype=np.int16)
            sample_count = self.decoder.decode_samples(adpcm_data, pcm_samples)
            
            if sample_count > 0:
                try:
                    self.audio_queue.put_nowait(pcm_samples[:sample_count])
                except queue.Full:
                    # Drop oldest sample if buffer full
                    try:
                        self.audio_queue.get_nowait()
                        self.audio_queue.put_nowait(pcm_samples[:sample_count])
                    except queue.Empty:
                        pass

    def play_audio(self):
        """Audio playback thread with better buffering"""
        import sounddevice as sd
        
        sample_rate = 16000
        chunk_size = 256  # Smaller chunks for lower latency
        
        def audio_callback(outdata, frames, time, status):
            if status:
                print(f"Audio status: {status}")
            
            try:
                # Try to get audio data
                audio_data = self.audio_queue.get_nowait()
                
                # Resize to match requested frames
                if len(audio_data) > frames:
                    audio_data = audio_data[:frames]
                elif len(audio_data) < frames:
                    # Pad with zeros if not enough data
                    padding = np.zeros(frames - len(audio_data), dtype=np.int16)
                    audio_data = np.concatenate([audio_data, padding])
                
                # Convert to float and normalize
                outdata[:, 0] = audio_data.astype(np.float32) / 32768.0
                
            except queue.Empty:
                # No audio data available, output silence
                outdata.fill(0)
        
        with sd.OutputStream(
            samplerate=sample_rate,
            channels=1,
            dtype='float32',
            callback=audio_callback,
            blocksize=chunk_size,
            latency='low'
        ):
            print("🔊 Audio playback started (robust mode)")
            while self.running:
                time.sleep(0.1)
                
                # Print statistics periodically
                if self.total_packets > 0 and self.total_packets % 100 == 0:
                    loss_rate = (self.lost_packets / self.total_packets) * 100
                    print(f"📊 Packets: {self.total_packets}, Lost: {self.lost_packets} ({loss_rate:.1f}%)")

    async def connect_and_stream(self, device_address=None):
        """Connect to device and start streaming"""
        
        # Find device
        print("🔍 Scanning for XIAO-REC device...")
        devices = await BleakScanner.discover(timeout=10.0)
        
        target_device = None
        for device in devices:
            if device.name == "XIAO-REC" or (device_address and device.address == device_address):
                target_device = device
                break
        
        if not target_device:
            print("❌ XIAO-REC device not found")
            return
        
        print(f"✅ Found device: {target_device.name} ({target_device.address})")
        
        # Connect and stream
        async with BleakClient(target_device) as client:
            print("🔗 Connected! Starting audio stream...")
            
            # Start notifications
            await client.start_notify(UUID_AUDIO_STREAM, self.stream_handler)
            
            # Send start command
            await client.write_gatt_char(UUID_AUDIO_CONTROL, bytes([1]))
            
            # Start audio playback thread
            self.running = True
            playback_thread = threading.Thread(target=self.play_audio, daemon=True)
            playback_thread.start()
            
            print("🎵 Streaming started! Press Ctrl+C to stop")
            
            try:
                # Keep connection alive
                while True:
                    await asyncio.sleep(1)
                    
            except KeyboardInterrupt:
                print("\n⏹ Stopping stream...")
                self.running = False
                await client.write_gatt_char(UUID_AUDIO_CONTROL, bytes([0]))
                await client.stop_notify(UUID_AUDIO_STREAM)

async def main():
    receiver = RobustStreamingReceiver()
    await receiver.connect_and_stream()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nExiting...")
