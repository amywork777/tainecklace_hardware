# iOS App Testing Suite for Smart Voice Recorder

This directory contains Python scripts that simulate iOS app functionality, allowing you to test the complete workflow before iOS development.

## 🚀 Quick Start

1. **Setup the testing environment:**
   ```bash
   cd python_scripts
   python setup_ios_testing.py
   ```

2. **Test smart mode detection:**
   ```bash
   python test_smart_modes.py
   ```

3. **Run full iOS app simulation:**
   ```bash
   python ios_app_simulator.py
   ```

## 📋 Testing Scripts Overview

### 1. `test_smart_modes.py` - Smart Mode Detection Test
**Purpose:** Test the core smart mode detection functionality

**What it tests:**
- ✅ Device detects when app is connected → Live streaming mode (solid LED)
- ✅ Device detects when no app connected → Offline recording (flashing LED)
- ✅ Button behavior changes based on connection status

**Usage:**
```bash
python test_smart_modes.py
```

**Expected Results:**
- **With app connected:** Press D0 → Solid LED → Live streaming
- **Without app:** Press D0 → Flashing red LED → Offline recording

### 2. `ios_app_simulator.py` - Full iOS App Simulation
**Purpose:** Complete iOS app functionality simulation

**Features:**
- 🔍 **Device Discovery** - Scans and connects to XIAO-REC
- 🎵 **Live Audio Streaming** - Receives ADPCM audio via BLE
- 💬 **Real-time Transcription** - Converts speech to text using Google Speech API
- 📊 **Statistics** - Packet loss, connection quality, duration
- 📁 **File Management** - Simulates offline recording retrieval
- 🎛️ **Interactive Interface** - Command-based testing

**Usage:**
```bash
# Interactive mode
python ios_app_simulator.py

# Automated test (30 seconds)
python ios_app_simulator.py --auto-stream --duration 30
```

**Interactive Commands:**
- `s` - Start live streaming
- `x` - Stop streaming  
- `o` - Test offline mode
- `r` - Retrieve offline recordings
- `q` - Quit

### 3. `live_audio_receiver_robust.py` - Basic Streaming Test
**Purpose:** Simple audio streaming test with playback

**Features:**
- 🎵 Real-time audio playback
- 📦 Packet loss detection and handling
- 💾 Optional WAV file saving
- 🔊 Audio quality monitoring

**Usage:**
```bash
# Basic streaming
python live_audio_receiver_robust.py

# Save to file
python live_audio_receiver_robust.py --save-to-file test_recording.wav
```

### 4. `ble_receiver.py` - Device Discovery & File Transfer
**Purpose:** Test BLE connection and file transfer

**Features:**
- 🔍 Device discovery and connection
- 📁 File transfer from SD card
- 📋 Device information display

**Usage:**
```bash
python ble_receiver.py --timeout 10
```

## 🧪 Testing Workflow

### Complete iOS App Testing Process:

1. **Setup Environment:**
   ```bash
   python setup_ios_testing.py
   ```

2. **Test Smart Mode Detection:**
   ```bash
   python test_smart_modes.py
   ```
   - Verifies device correctly switches between streaming/recording modes
   - Tests LED status indicators
   - Validates button behavior

3. **Test Live Streaming:**
   ```bash
   python ios_app_simulator.py
   ```
   - Connect to device
   - Press `s` to start streaming
   - Press D0 button on device
   - Speak into microphone → See real-time transcription
   - Press D0 again to stop
   - Press `x` to end streaming

4. **Test Offline Mode:**
   ```bash
   python ios_app_simulator.py
   ```
   - Connect to device
   - Press `o` to test offline mode (simulates disconnection)
   - Press D0 button → Should see flashing red LED
   - Audio saves to SD card

5. **Verify Audio Quality:**
   ```bash
   python live_audio_receiver_robust.py --save-to-file quality_test.wav
   ```
   - Test audio clarity and packet loss
   - Save recording for analysis

## 📊 What to Expect

### Smart Mode Detection Results:
- **📱 App Connected:** 
  - Press D0 → Solid LED → Live streaming active
  - Audio streams to Python app in real-time
  - Transcription appears in terminal

- **💾 App Disconnected:**
  - Press D0 → Flashing red LED → Recording to SD
  - No streaming, audio saved locally
  - Can retrieve later via file transfer

### Performance Metrics:
- **Packet Loss:** Should be < 5% for good quality
- **Latency:** ~200-500ms end-to-end
- **Transcription:** Real-time speech-to-text
- **Battery:** Device runs continuously during testing

## 🔧 Troubleshooting

### Common Issues:

1. **Device Not Found:**
   ```bash
   # Check if device is advertising
   python ble_receiver.py --timeout 5
   ```

2. **Audio Transcription Not Working:**
   ```bash
   # Install missing dependencies
   pip install speechrecognition pyaudio
   ```

3. **Connection Drops:**
   - Check BLE signal strength
   - Reduce distance between device and computer
   - Restart both device and Python script

4. **No Audio Playback:**
   ```bash
   # Test audio system
   python -c "import sounddevice; print(sounddevice.query_devices())"
   ```

## 🎯 Testing Checklist

Before iOS development, verify:

- [ ] Device discovery and connection works
- [ ] Smart mode detection (connected vs offline)
- [ ] LED indicators work correctly (solid vs flashing)
- [ ] Live audio streaming with acceptable quality
- [ ] Real-time transcription functionality
- [ ] Offline recording mode saves to SD
- [ ] File transfer retrieves saved recordings
- [ ] Packet loss is minimal (< 5%)
- [ ] Connection is stable during use

## 🔗 BLE Service Reference

For iOS development, use these UUIDs:

```swift
// Audio Streaming Service
let AUDIO_SERVICE_UUID = "12345678-1234-1234-1234-123456789abc"
let AUDIO_STREAM_UUID  = "12345678-1234-1234-1234-123456789abd"
let AUDIO_CONTROL_UUID = "12345678-1234-1234-1234-123456789abe"
let AUDIO_STATUS_UUID  = "12345678-1234-1234-1234-123456789abf"

// File Transfer Service  
let FILE_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
let FILE_INFO_UUID    = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
let FILE_DATA_UUID    = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
let FILE_CONTROL_UUID = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"
```

## 📱 Next Steps

Once Python testing is complete:
1. Use these scripts as reference for iOS Core Bluetooth implementation
2. Implement similar transcription using iOS Speech framework
3. Add iOS-specific UI and user experience features
4. Test iOS app against the same device firmware

**The Python scripts provide a complete reference implementation for your iOS development team!** 🎉
