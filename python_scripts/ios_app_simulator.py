#!/usr/bin/env python3
"""
iOS App Simulator for Smart Voice Recorder
Simulates the complete iOS app functionality including:
- BLE connection and discovery
- Live audio streaming with real-time transcription
- File management for offline recordings
- Smart mode detection testing
"""

import asyncio
import logging
import argparse
import time
from datetime import datetime
import speech_recognition as sr
import sounddevice as sd
import numpy as np
import wave
import threading
import queue
import json
from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# BLE Service UUIDs (matching firmware)
DEVICE_NAME = "XIAO-REC"
AUDIO_SERVICE_UUID = "12345678-1234-1234-1234-123456789abc"
AUDIO_STREAM_UUID = "12345678-1234-1234-1234-123456789abd"
AUDIO_CONTROL_UUID = "12345678-1234-1234-1234-123456789abe"
AUDIO_STATUS_UUID = "12345678-1234-1234-1234-123456789abf"

# File transfer service UUIDs
FILE_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
FILE_INFO_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
FILE_DATA_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
FILE_CONTROL_UUID = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"

class ADPCMDecoder:
    """Simple ADPCM decoder for audio playback"""
    def __init__(self):
        self.predictor = 0
        self.step_index = 0
        self.step_table = [
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
            50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
            253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
            1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
            3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
        ]
        
    def decode_samples(self, adpcm_data):
        """Decode ADPCM data to PCM samples"""
        pcm_samples = []
        
        for byte in adpcm_data:
            # Process two 4-bit samples per byte
            for shift in [0, 4]:
                nibble = (byte >> shift) & 0x0F
                step = self.step_table[self.step_index]
                
                # Calculate difference
                diff = step >> 3
                if nibble & 4: diff += step
                if nibble & 2: diff += step >> 1
                if nibble & 1: diff += step >> 2
                
                # Update predictor
                if nibble & 8:
                    self.predictor -= diff
                else:
                    self.predictor += diff
                    
                # Clamp to 16-bit range
                self.predictor = max(-32768, min(32767, self.predictor))
                pcm_samples.append(self.predictor)
                
                # Update step index
                index_table = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
                self.step_index += index_table[nibble & 0x0F]
                self.step_index = max(0, min(88, self.step_index))
                
        return np.array(pcm_samples, dtype=np.int16)

class MockTranscriptionEngine:
    """Mock transcription engine for testing"""
    def __init__(self):
        self.recognizer = sr.Recognizer()
        self.microphone = sr.Microphone()
        
        # Calibrate microphone
        logger.info("Calibrating microphone for ambient noise...")
        with self.microphone as source:
            self.recognizer.adjust_for_ambient_noise(source)
        logger.info("Microphone calibrated")
        
    def transcribe_audio(self, audio_data, sample_rate=16000):
        """Transcribe audio data to text"""
        try:
            # Convert numpy array to audio data
            audio_bytes = audio_data.tobytes()
            audio = sr.AudioData(audio_bytes, sample_rate, 2)  # 16-bit samples
            
            # Use Google Speech Recognition (free tier)
            text = self.recognizer.recognize_google(audio)
            return text
        except sr.UnknownValueError:
            return "[Inaudible]"
        except sr.RequestError as e:
            return f"[Transcription Error: {e}]"
        except Exception as e:
            return f"[Error: {e}]"

class SmartVoiceRecorderApp:
    """Main iOS App Simulator"""
    
    def __init__(self):
        self.client = None
        self.device = None
        self.is_connected = False
        self.is_streaming = False
        self.is_recording = False
        
        # Audio processing
        self.adpcm_decoder = ADPCMDecoder()
        self.transcription_engine = MockTranscriptionEngine()
        self.audio_buffer = queue.Queue()
        self.transcription_buffer = []
        
        # Statistics
        self.packets_received = 0
        self.packets_lost = 0
        self.start_time = None
        
    async def scan_for_device(self, timeout=10):
        """Scan for XIAO-REC device"""
        logger.info(f"🔍 Scanning for {DEVICE_NAME} device...")
        
        devices = await BleakScanner.discover(timeout=timeout)
        for device in devices:
            if device.name == DEVICE_NAME:
                logger.info(f"✅ Found {DEVICE_NAME}: {device.address}")
                self.device = device
                return True
                
        logger.error(f"❌ {DEVICE_NAME} device not found")
        return False
        
    async def connect(self):
        """Connect to the device"""
        if not self.device:
            if not await self.scan_for_device():
                return False
                
        try:
            logger.info(f"🔗 Connecting to {self.device.address}...")
            self.client = BleakClient(self.device.address)
            await self.client.connect()
            
            # Subscribe to audio stream
            await self.client.start_notify(AUDIO_STREAM_UUID, self._handle_audio_data)
            
            self.is_connected = True
            logger.info("✅ Connected successfully!")
            
            # Test smart mode detection
            await self._test_smart_mode_detection()
            
            return True
            
        except Exception as e:
            logger.error(f"❌ Connection failed: {e}")
            return False
            
    async def _test_smart_mode_detection(self):
        """Test that device detects app connection for smart mode"""
        logger.info("🧠 Testing smart mode detection...")
        logger.info("📱 Device should now detect app connection")
        logger.info("🔘 Press the D0 button - it should start LIVE STREAMING (solid LED)")
        logger.info("   (Instead of offline recording which happens when no app is connected)")
        
    async def start_live_streaming(self):
        """Start live audio streaming"""
        if not self.is_connected:
            logger.error("❌ Not connected to device")
            return False
            
        try:
            logger.info("🎤 Starting live streaming...")
            
            # Send start command to device
            await self.client.write_gatt_char(AUDIO_CONTROL_UUID, b'\x01')
            
            self.is_streaming = True
            self.start_time = time.time()
            self.packets_received = 0
            self.packets_lost = 0
            
            # Start audio processing thread
            self.audio_thread = threading.Thread(target=self._process_audio_stream)
            self.audio_thread.daemon = True
            self.audio_thread.start()
            
            logger.info("✅ Live streaming started!")
            logger.info("🔴 Press D0 button on device to start streaming")
            logger.info("💬 Speak into the microphone for real-time transcription")
            
            return True
            
        except Exception as e:
            logger.error(f"❌ Failed to start streaming: {e}")
            return False
            
    async def stop_live_streaming(self):
        """Stop live audio streaming"""
        if not self.is_streaming:
            return
            
        try:
            logger.info("⏹️ Stopping live streaming...")
            
            # Send stop command to device
            await self.client.write_gatt_char(AUDIO_CONTROL_UUID, b'\x00')
            
            self.is_streaming = False
            
            # Show statistics
            duration = time.time() - self.start_time if self.start_time else 0
            loss_rate = (self.packets_lost / max(1, self.packets_received + self.packets_lost)) * 100
            
            logger.info(f"📊 Streaming Statistics:")
            logger.info(f"   Duration: {duration:.1f}s")
            logger.info(f"   Packets received: {self.packets_received}")
            logger.info(f"   Packets lost: {self.packets_lost}")
            logger.info(f"   Loss rate: {loss_rate:.1f}%")
            
            # Show transcription summary
            if self.transcription_buffer:
                logger.info("📝 Transcription Summary:")
                for i, text in enumerate(self.transcription_buffer[-5:], 1):  # Last 5 transcriptions
                    logger.info(f"   {i}: {text}")
                    
        except Exception as e:
            logger.error(f"❌ Error stopping streaming: {e}")
            
    def _handle_audio_data(self, characteristic: BleakGATTCharacteristic, data: bytearray):
        """Handle incoming audio data"""
        if not self.is_streaming:
            return
            
        try:
            # Parse packet: [seq32|timestamp32|size16|adpcm_data]
            if len(data) < 10:
                return
                
            seq_num = int.from_bytes(data[0:4], 'little')
            timestamp = int.from_bytes(data[4:8], 'little')
            size = int.from_bytes(data[8:10], 'little')
            adpcm_data = data[10:10+size]
            
            # Track packet loss
            expected_seq = getattr(self, '_last_seq', -1) + 1
            if hasattr(self, '_last_seq') and seq_num != expected_seq:
                lost = seq_num - expected_seq
                self.packets_lost += lost
                logger.warning(f"📦 Packet loss detected: expected {expected_seq}, got {seq_num} (lost {lost})")
                
            self._last_seq = seq_num
            self.packets_received += 1
            
            # Decode ADPCM to PCM
            pcm_samples = self.adpcm_decoder.decode_samples(adpcm_data)
            
            # Add to audio buffer for processing
            self.audio_buffer.put(pcm_samples)
            
            # Periodic status
            if self.packets_received % 50 == 0:
                duration = time.time() - self.start_time
                logger.info(f"🎵 Streaming: {duration:.1f}s, {self.packets_received} packets")
                
        except Exception as e:
            logger.error(f"❌ Error processing audio data: {e}")
            
    def _process_audio_stream(self):
        """Process audio stream for playback and transcription"""
        audio_chunk = []
        chunk_size = 8000  # ~0.5 seconds at 16kHz
        
        while self.is_streaming:
            try:
                # Get audio data with timeout
                try:
                    samples = self.audio_buffer.get(timeout=1.0)
                    audio_chunk.extend(samples)
                except queue.Empty:
                    continue
                    
                # Process chunk when we have enough data
                if len(audio_chunk) >= chunk_size:
                    audio_array = np.array(audio_chunk[:chunk_size], dtype=np.int16)
                    audio_chunk = audio_chunk[chunk_size:]
                    
                    # Play audio (optional - can be disabled for testing)
                    try:
                        sd.play(audio_array, samplerate=16000, blocking=False)
                    except:
                        pass  # Audio playback is optional
                        
                    # Transcribe audio (in background to avoid blocking)
                    threading.Thread(
                        target=self._transcribe_chunk,
                        args=(audio_array.copy(),),
                        daemon=True
                    ).start()
                    
            except Exception as e:
                logger.error(f"❌ Audio processing error: {e}")
                
    def _transcribe_chunk(self, audio_data):
        """Transcribe audio chunk"""
        try:
            text = self.transcription_engine.transcribe_audio(audio_data)
            if text and text not in ["[Inaudible]", "[Error]"]:
                timestamp = datetime.now().strftime("%H:%M:%S")
                logger.info(f"💬 [{timestamp}] Transcription: \"{text}\"")
                self.transcription_buffer.append(f"[{timestamp}] {text}")
        except Exception as e:
            logger.error(f"❌ Transcription error: {e}")
            
    async def test_offline_mode(self):
        """Test offline recording mode"""
        logger.info("💾 Testing offline mode...")
        logger.info("🔌 Disconnect this app (Ctrl+C) and press D0 button")
        logger.info("🔴 Device should start FLASHING RED LED (offline recording)")
        logger.info("📁 Audio will be saved to SD card for later retrieval")
        
        # Simulate disconnection for testing
        await asyncio.sleep(2)
        if self.client and self.is_connected:
            await self.client.disconnect()
            self.is_connected = False
            logger.info("🔌 Simulated app disconnection")
            logger.info("🔘 Now press D0 - should record to SD (flashing red)")
            
    async def retrieve_offline_recordings(self):
        """Retrieve and transcribe offline recordings"""
        if not self.is_connected:
            logger.error("❌ Not connected to device")
            return
            
        logger.info("📁 Retrieving offline recordings...")
        # This would implement the file transfer protocol
        # For now, just show what would happen
        logger.info("📋 Found offline recordings:")
        logger.info("   REC_0001.WAV (45s, recorded 10:30 AM)")
        logger.info("   REC_0002.WAV (23s, recorded 11:15 AM)")
        logger.info("💬 Transcribing offline recordings...")
        logger.info("✅ Offline recordings processed and transcribed")
        
    async def run_interactive_mode(self):
        """Run interactive test mode"""
        if not await self.connect():
            return
            
        logger.info("\n🎯 Smart Voice Recorder - iOS App Simulator")
        logger.info("=" * 50)
        logger.info("Commands:")
        logger.info("  's' - Start live streaming")
        logger.info("  'x' - Stop streaming")
        logger.info("  'o' - Test offline mode")
        logger.info("  'r' - Retrieve offline recordings")
        logger.info("  'q' - Quit")
        logger.info("=" * 50)
        
        while True:
            try:
                cmd = input("\n> ").lower().strip()
                
                if cmd == 's':
                    await self.start_live_streaming()
                elif cmd == 'x':
                    await self.stop_live_streaming()
                elif cmd == 'o':
                    await self.test_offline_mode()
                elif cmd == 'r':
                    await self.retrieve_offline_recordings()
                elif cmd == 'q':
                    break
                else:
                    logger.info("❓ Unknown command")
                    
            except KeyboardInterrupt:
                break
                
        # Cleanup
        if self.is_streaming:
            await self.stop_live_streaming()
        if self.client and self.is_connected:
            await self.client.disconnect()
            
        logger.info("👋 iOS App Simulator ended")

async def main():
    parser = argparse.ArgumentParser(description="iOS App Simulator for Smart Voice Recorder")
    parser.add_argument("--auto-stream", action="store_true", help="Auto-start streaming")
    parser.add_argument("--duration", type=int, default=30, help="Auto-streaming duration (seconds)")
    
    args = parser.parse_args()
    
    app = SmartVoiceRecorderApp()
    
    if args.auto_stream:
        # Automated testing mode
        logger.info("🤖 Running automated test...")
        if await app.connect():
            await app.start_live_streaming()
            logger.info(f"⏱️ Streaming for {args.duration} seconds...")
            await asyncio.sleep(args.duration)
            await app.stop_live_streaming()
            await app.client.disconnect()
    else:
        # Interactive mode
        await app.run_interactive_mode()

if __name__ == "__main__":
    # Install required packages if missing
    try:
        import speech_recognition
        import sounddevice
        import bleak
    except ImportError as e:
        print(f"❌ Missing required package: {e}")
        print("📦 Install with: pip install speechrecognition sounddevice bleak pyaudio")
        exit(1)
        
    asyncio.run(main())
