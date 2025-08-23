# XIAO Voice Logger

Audio recording device using Seeed XIAO BLE Sense with SD card storage and BLE file transfer.

## Hardware Requirements

- Seeed XIAO BLE Sense (nRF52840)
- MicroSD card (FAT32 formatted)
- SD card breakout board connected to:
  - CS = D6
  - SCK = D8  
  - MISO = D9
  - MOSI = D10

## Features

- **Audio Recording**: 16kHz 16-bit mono PCM to WAV files
- **SD Card Storage**: Automatic file naming (REC_0001.WAV, etc.)
- **BLE File Transfer**: Wireless download of recordings
- **Serial Commands**: Simple text interface for control

## Usage

### Device Commands (Serial @ 115200 baud)

- `r` - Start recording
- `s` - Stop recording  
- `c` - Connect for BLE transfer of last file
- `l` - List WAV files on SD card

### Python Scripts

#### Install Requirements
```bash
pip install -r requirements.txt
```

#### BLE Audio Receiver
Download recordings wirelessly via BLE:
```bash
python ble_receiver.py                    # Auto-discover and download
python ble_receiver.py --scan-only        # Just scan for devices
python ble_receiver.py --device-address XX:XX:XX:XX:XX:XX
```

#### WAV File Player/Analyzer
Process downloaded audio files:
```bash
python open_wav.py                        # List all WAV files
python open_wav.py REC_0001.WAV          # Analyze and play file
python open_wav.py --analyze REC_0001.WAV # Detailed analysis
python open_wav.py --plot REC_0001.WAV   # Show waveform plot
```

## Workflow

1. **Record Audio**: Press 'r' on serial monitor to start recording
2. **Stop Recording**: Press 's' to stop and finalize WAV file
3. **Transfer via BLE**: Press 'c' to start BLE advertising
4. **Download**: Run `python ble_receiver.py` to download the file
5. **Analyze**: Use `python open_wav.py filename.wav` to play/analyze

## Configuration

Edit `src/config.h` for:
- Sample rate (default: 16kHz)
- Buffer sizes 
- SD card settings
- PDM gain levels
- File naming

## BLE Protocol

Custom protocol for reliable file transfer:
- Service UUID: `a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001`
- Flow control with credits system
- CRC16 validation for data integrity
- Automatic reconnection handling

## Building

Use PlatformIO:
```bash
platformio run
platformio run --target upload
```

## Troubleshooting

### No Serial Output
- Check baud rate (115200)
- Device may need to be reset after upload

### Recording Shows 0 Bytes
- Check SD card connection and formatting
- Verify PDM microphone is working
- Check serial output for error messages

### BLE Transfer Issues
- Ensure device is advertising (press 'c')
- Check that `bleak` library is installed
- Try scanning: `python ble_receiver.py --scan-only`