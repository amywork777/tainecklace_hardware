# ADPCM Audio Compression Implementation

This document describes the ADPCM (Adaptive Differential Pulse Code Modulation) compression implementation for the XIAO Voice Logger, which provides **4x faster BLE transmission** through 4:1 audio compression.

## 🚀 Performance Improvements

### Before ADPCM (16-bit PCM)
- **Data rate:** 32,000 bytes/second (16kHz × 2 bytes)
- **1-minute recording:** ~1.9 MB
- **BLE transmission time:** 4-8 minutes (at 4-8 KB/s)

### After ADPCM (4-bit compressed)
- **Data rate:** 8,000 bytes/second (16kHz × 0.5 bytes)
- **1-minute recording:** ~480 KB
- **BLE transmission time:** 1-2 minutes (at 4-8 KB/s)
- **Speed improvement:** **4x faster transmission**
- **Storage savings:** **75% smaller files**

## 📁 Implementation Files

### Firmware (C++)
- `src/adpcm.h` - ADPCM codec header and data structures
- `src/adpcm.cpp` - ADPCM encoder/decoder implementation
- `src/config.h` - Updated with compression configuration
- `src/audio.cpp` - Modified to integrate real-time ADPCM compression

### Python Scripts
- `python_scripts/adpcm_decoder.py` - Decode ADPCM files to WAV format
- `python_scripts/ble_receiver.py` - Updated to auto-decode received files
- `python_scripts/test_adpcm.py` - Test script to validate implementation

## 🔧 Configuration

The compression can be enabled/disabled via `src/config.h`:

```cpp
// Enable ADPCM 4:1 compression
constexpr bool ENABLE_ADPCM_COMPRESSION = true;

// When enabled:
// - Files saved as .ADPCM format instead of .WAV
// - 4:1 compression ratio (16-bit → 4-bit per sample)
// - Real-time encoding during recording
// - 4x faster BLE transmission
```

## 📋 File Formats

### ADPCM File Format (.adpcm)
```
Offset | Size | Field              | Description
-------|------|--------------------|---------------------------------
0      | 4    | magic              | "ADPC" file signature
4      | 4    | version            | Format version (1)
8      | 4    | sample_rate        | Sample rate in Hz (16000)
12     | 2    | channels           | Number of channels (1)
14     | 2    | bits_per_sample    | ADPCM bits per sample (4)
16     | 4    | total_samples      | Total number of PCM samples
20     | 4    | data_size          | Compressed data size in bytes
24     | 2    | initial_sample     | Initial decoder state
26     | 1    | initial_step_index | Initial step index
27     | 3    | reserved           | Padding
31     | N    | compressed_data    | ADPCM compressed audio data
```

### WAV File Format (.wav)
Standard WAV format used when compression is disabled.

## 🎵 Usage Instructions

### 1. Recording Audio (Firmware)

The compression happens automatically when `ENABLE_ADPCM_COMPRESSION = true`:

```cpp
// Start recording (same API as before)
audio_start_recording();

// Process audio in main loop (compression happens here)
while (audio_is_recording()) {
    audio_process();  // Real-time ADPCM encoding
    // ... other tasks
}

// Stop recording
audio_stop_recording();
// Result: REC_0001.ADPCM file (compressed)
```

### 2. Downloading via BLE

Use the existing BLE receiver script:

```bash
# Download compressed file
python python_scripts/ble_receiver.py

# The script will:
# 1. Download REC_0001.ADPCM (4x faster)
# 2. Auto-decode to REC_0001.WAV for playback
```

### 3. Manual Decoding

Decode ADPCM files to WAV format:

```bash
# Single file
python python_scripts/adpcm_decoder.py REC_0001.adpcm

# Batch processing
python python_scripts/adpcm_decoder.py --batch *.adpcm

# Show file information
python python_scripts/adpcm_decoder.py --info REC_0001.adpcm
```

### 4. Testing Implementation

Validate the ADPCM codec:

```bash
python python_scripts/test_adpcm.py
```

This will:
- Generate test audio signals
- Encode/decode with ADPCM
- Measure compression ratio and quality
- Create test files for listening comparison

## 🔍 Technical Details

### ADPCM Algorithm (IMA Variant)
- **Compression:** 16-bit PCM → 4-bit ADPCM codes
- **Encoding:** Adaptive differential quantization
- **Quality:** Good for voice, excellent compression ratio
- **Latency:** Real-time encoding with minimal delay

### Memory Usage
- **ADPCM State:** 32 bytes (encoder + decoder)
- **Temp Buffers:** 640 bytes (256 PCM samples + 128 ADPCM bytes)
- **Total Overhead:** ~672 bytes additional RAM usage

### Performance Characteristics
- **Encoding Speed:** Real-time at 16kHz (no audio dropouts)
- **Quality:** 20-30 dB SNR for voice (Good to Excellent)
- **Compression:** Consistent 4:1 ratio regardless of content
- **Transmission:** 4x faster over BLE

## 🚨 Troubleshooting

### If Audio Quality is Poor
1. Check PDM gain settings (`PDM_GAIN` in config.h)
2. Verify sample rate is 16kHz (ADPCM optimized for voice)
3. Ensure proper microphone positioning

### If Compression Doesn't Work
1. Verify `ENABLE_ADPCM_COMPRESSION = true` in config.h
2. Check for compilation errors in adpcm.cpp
3. Monitor serial output for encoder status messages

### If Files Won't Decode
1. Ensure file has .adpcm extension and proper header
2. Check file isn't truncated (incomplete BLE transfer)
3. Use `--info` flag to inspect file header

### If BLE Transfer is Still Slow
1. ADPCM compression should give 4x improvement
2. If not, check BLE connection quality
3. Monitor transfer speed in Python script output

## 🔄 Switching Between Modes

### Enable Compression
```cpp
// In src/config.h
constexpr bool ENABLE_ADPCM_COMPRESSION = true;
```
- Files saved as: `REC_0001.ADPCM`
- Size: ~8 KB/second
- Needs decoding for playback

### Disable Compression (Original Mode)
```cpp
// In src/config.h
constexpr bool ENABLE_ADPCM_COMPRESSION = false;
```
- Files saved as: `REC_0001.WAV`
- Size: ~32 KB/second
- Ready for immediate playback

## 📊 Quality Comparison

| Metric | Uncompressed WAV | ADPCM Compressed |
|--------|------------------|------------------|
| File Size | 32 KB/second | 8 KB/second |
| BLE Transfer | 4-8 minutes/minute | 1-2 minutes/minute |
| Audio Quality | Perfect (48+ dB SNR) | Good (20-30 dB SNR) |
| Storage Usage | 100% | 25% |
| Processing | None | Real-time encoding |
| Playback | Direct | Requires decoding |

## 💡 Recommendations

### For Voice Recording (Recommended)
- **Use ADPCM compression** for 4x faster transfers
- Quality is excellent for speech/voice applications
- Massive storage and transmission savings

### For Music/High-Fidelity
- **Disable compression** if audio quality is critical
- Use for music recording or analysis applications
- Accept slower transfer times for perfect quality

### For Development/Testing
- Use test script to validate implementation
- Compare quality with your specific audio content
- Monitor system performance and memory usage

## 🔮 Future Enhancements

Potential improvements for future versions:

1. **Variable Bitrate:** Adjust compression based on audio content
2. **Streaming Compression:** Real-time BLE streaming with compression
3. **Error Resilience:** Add error correction to compressed stream
4. **Quality Modes:** Multiple compression levels (2-bit, 3-bit, 4-bit)
5. **Metadata:** Embed recording timestamps and device info

---

The ADPCM implementation provides an excellent balance of compression ratio, audio quality, and transmission speed for voice recording applications. The 4x speed improvement makes BLE transfers much more practical for regular use.
