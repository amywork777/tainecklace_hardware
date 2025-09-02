# Live Audio Streaming Setup Guide

## Quick Start

### 1. Install Dependencies
```bash
pip install bleak numpy sounddevice
```

### 2. Start Device Streaming
On your XIAO device, use these commands:
```
t    # Start live streaming mode
a    # Start BLE advertising (if not connected)
```

### 3. Run the Receiver
```bash
python live_audio_receiver.py
```

## Usage Examples

### Basic Live Streaming
```bash
python live_audio_receiver.py
# Automatically finds device and starts real-time playback
```

### Stream with Recording
```bash
python live_audio_receiver.py --save-to-file my_stream.wav
# Plays audio live AND saves to file
```

### Recording Only (No Playback)
```bash
python live_audio_receiver.py --save-to-file recording.wav --no-playback
# Records to file without playing audio (useful for headless systems)
```

### Connect to Specific Device
```bash
python live_audio_receiver.py --device-address XX:XX:XX:XX:XX:XX
```

### List Audio Devices
```bash
python live_audio_receiver.py --list-audio-devices
```

## How It Works

### Device Side (XIAO)
1. **PDM Microphone** captures audio at 16kHz
2. **ADPCM Compression** reduces data 4:1 (32KB/s → 8KB/s)
3. **BLE Streaming** sends 128-byte chunks (~256 samples each)
4. **Optional SD Backup** saves stream to `STREAM_0000.ADPCM`

### Receiver Side (Python)
1. **BLE Connection** to live audio service
2. **Real-time Decoding** of ADPCM chunks to PCM
3. **Audio Buffer** manages smooth playback with ~100ms latency
4. **Sound Playback** via system audio device
5. **Optional Recording** saves to standard WAV file

## Streaming Protocol

### BLE Services
- **Service UUID**: `b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001`
- **Stream Data**: `b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0002` (notifications)
- **Control**: `b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0003` (write: 1=start, 0=stop)
- **Status**: `b3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0004` (read: format info)

### Packet Format
```
[seq:4][timestamp:4][size:2][adpcm_data:size]
```
- **seq**: Sequence number for packet ordering
- **timestamp**: Milliseconds since stream start
- **size**: Number of ADPCM bytes (typically 128)
- **adpcm_data**: Compressed audio (256 samples when decoded)

## Performance

### Expected Throughput
- **Raw Audio**: 32 KB/s (16kHz × 16-bit × 1ch)
- **ADPCM Compressed**: 8 KB/s (4:1 compression)
- **BLE Overhead**: ~10-12 KB/s (including headers, flow control)
- **Latency**: ~100-200ms (buffering + BLE + audio system)

### Troubleshooting

#### No Audio Output
```bash
python live_audio_receiver.py --list-audio-devices
# Check if your audio device is available
```

#### Connection Issues
1. Make sure device is advertising: press `a` on XIAO
2. Check device is in streaming mode: press `t` on XIAO
3. Verify Bluetooth is enabled on your computer
4. Try specific device address if auto-discovery fails

#### Audio Dropouts
- **Buffer overruns**: Audio buffer is full (decrease system audio latency)
- **Packet losses**: BLE connection quality issues (move devices closer)
- **High CPU**: Close other applications during streaming

#### Poor Audio Quality
- Check sample rate matches (16kHz)
- Verify ADPCM decoding is working (should see decoded samples)
- Test with traditional file recording first

## Device Commands Reference

### XIAO Serial Commands
```
t    # Toggle streaming on/off
a    # Start BLE audio advertising
r    # Start traditional recording (separate from streaming)
s    # Stop recording
c    # Transfer recorded files (traditional BLE transfer)
l    # List files on SD card
d    # Delete all WAV files
```

### Streaming vs Recording
- **Streaming**: Live audio with immediate BLE transmission
- **Recording**: Traditional record-then-transfer workflow
- **Both**: Stream with SD backup (saves `STREAM_0000.ADPCM` files)

## Advanced Usage

### Multiple Receivers
You can run multiple Python receivers to the same device, but only one can control streaming (start/stop). Others will receive the same audio stream.

### Custom Processing
The `live_audio_receiver.py` script can be modified to:
- Apply real-time audio processing (filters, effects)
- Stream to network endpoints
- Integrate with voice recognition
- Save in different formats

### Integration Example
```python
# In your own code
from live_audio_receiver import LiveAudioReceiver

receiver = LiveAudioReceiver()
# ... setup and connect ...

# Access decoded samples in real-time
def custom_audio_handler(samples):
    # Your custom processing here
    process_audio_samples(samples)

receiver.custom_handler = custom_audio_handler
```

## System Requirements

- **Python 3.7+**
- **Bluetooth 4.0+ (BLE support)**
- **Audio output device**
- **Operating System**: Windows 10+, macOS 10.12+, Linux with BlueZ

## File Formats

### Streaming Output
- **WAV**: Standard PCM format (if using `--save-to-file`)
- **16-bit signed PCM**
- **16kHz sample rate**
- **Mono (1 channel)**

### Device Backup Files
- **STREAM_0000.ADPCM**: Compressed backup on SD card
- Use existing `adpcm_decoder.py` to convert to WAV
