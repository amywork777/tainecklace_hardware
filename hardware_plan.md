# XIAO BLE Hardware Implementation Plan

## 🔧 HARDWARE REQUIREMENTS

### Bill of Materials (BOM)
| Component | Quantity | Specifications | Purpose |
|-----------|----------|----------------|---------|
| Seeed XIAO BLE Sense | 1 | nRF52840, built-in mic, RGB LED | Main controller |
| MicroSD Card Module | 1 | SPI interface, 3.3V compatible | Audio storage |
| MicroSD Card | 1 | ≤32GB, FAT32, Class 10+ | File storage |
| Tactile Button | 1 | 6mm, normally open | BLE control interface |
| Breadboard | 1 | Half-size or full | Prototyping |
| Jumper Wires | 8 | Male-to-male, various lengths | Connections |
| USB-C Cable | 1 | For power and programming | Power + programming |

**Removed Components** (not needed):
- ❌ Battery pack (using USB power)
- ❌ Power switch (plug/unplug for power)
- ❌ External resistors (XIAO has internal pullups)

### Tools Required
- Wire strippers (for jumper wires if needed)
- USB-C cable for XIAO programming  
- Computer with PlatformIO or Arduino IDE

---

## ⚡ CIRCUIT DESIGN

### Pinout Mapping
```
XIAO BLE Sense (nRF52840):
┌─────────────────────────┐
│  USB-C   [XIAO BLE]     │
├─────────────────────────┤
│ D0  [Not Used]      3V3 │ → SD Card VCC
│ D1  [Button]        GND │ → Common Ground  
│ D2  [Not Used]      D10 │ → SD MOSI
│ D3  [Not Used]       D9 │ → SD MISO
│ D4  [Not Used]       D8 │ → SD SCK
│ D5  [Not Used]       D7 │ [Not Used]
│ D6  [SD CS]         D6  │ [Labeled for clarity]
│ VIN [Battery+]      RST │ [Not Used]
└─────────────────────────┘
```

### Power Circuit
```
USB-C Cable ──────────────── XIAO USB-C Port
(No external power switch needed - plug/unplug for power)
```

### Button Circuit  
```
XIAO D1 ──[Internal 13kΩ Pull-up]── 3V3
    │
    └──[Tactile Button]── GND

When button OPEN:   D1 reads HIGH (3.3V)
When button CLOSED: D1 reads LOW (0V) ← Button pressed
```
*Note: No external resistor needed - XIAO internal pullup enabled in firmware*

### SD Card Module SPI Interface
```
SD Card Module    XIAO Pin
CS    (Pin 1) ──── D6
SCK   (Pin 2) ──── D8  
MOSI  (Pin 3) ──── D10
MISO  (Pin 4) ──── D9
VCC   (Pin 5) ──── 3V3
GND   (Pin 6) ──── GND
```

---

## 🔨 ASSEMBLY INSTRUCTIONS

### STEP 1: Power System (1 minute)
1. **Connect USB Power**:
   - USB-C cable → XIAO USB-C port
   - Other end → Computer USB port or wall adapter

2. **Test Power**:
   - Plug in USB → XIAO LED should illuminate
   - Unplug USB → XIAO turns off
   - **Power control**: Simply plug/unplug as needed

### STEP 2: SD Card Module (10 minutes)  
1. **SPI Connections**:
   ```
   SD Module CS   → XIAO D6  (Chip Select)
   SD Module SCK  → XIAO D8  (Serial Clock)  
   SD Module MOSI → XIAO D10 (Master Out Slave In)
   SD Module MISO → XIAO D9  (Master In Slave Out)
   SD Module VCC  → XIAO 3V3 (Power)
   SD Module GND  → XIAO GND (Ground)
   ```

2. **SD Card Preparation**:
   - Format MicroSD card as FAT32 
   - Ensure ≤32GB capacity (FAT32 limitation)
   - Insert into SD module

3. **Test SD Access**:
   - Upload basic SD test firmware
   - Verify card detection and file creation

### STEP 3: Single Button Interface (5 minutes)
1. **Button Connection**:
   ```
   Tactile Button Pin 1 → XIAO D1
   Tactile Button Pin 2 → XIAO GND
   ```

2. **Button Functions** (handled by firmware):
   - **Single-click**: Start/stop recording toggle
   - **Double-click**: Force BLE advertising (manual sync)  
   - **Long-press (2s)**: Manual sync if connected

3. **Test Button**:
   - Upload firmware with button detection
   - Single-click → Red LED (recording)
   - Single-click again → Blue blink (advertising)
   - Double-click → Blue blink (force advertising)

### STEP 4: System Integration Test (3 minutes)
1. **Power-On Sequence**:
   - USB plugged in → XIAO boots → LED startup sequence
   - SD card initialization → Ready state (LED off)

2. **Button Functionality Test**:
   - Single-click → Red LED (start recording)
   - Single-click → Blue blink (stop + advertise)  
   - Double-click → Blue blink (force advertising)

3. **LED Status Verification**:
   - **Idle**: LED off
   - **Recording**: Solid red
   - **BLE advertising**: Blinking blue  
   - **Connected/transferring**: Solid green
   - **Error**: Red blink

---

## 💻 FIRMWARE ARCHITECTURE

### Core Modules
```cpp
// main.cpp - Main application logic
├── Button Handler
│   ├── Debouncing (50ms)  
│   ├── Click Detection (single/double/long)
│   └── State Machine (idle/recording/advertising)
├── Audio System  
│   ├── PDM Microphone Interface
│   ├── WAV File Writing
│   └── SD Card Management
├── BLE Protocol
│   ├── Service Advertisement  
│   ├── Credit-Based File Transfer
│   └── Connection Management
└── LED Controller
    ├── Status Indication
    ├── RGB Color Control
    └── Blinking Patterns
```

### State Machine
```
[IDLE] ──single_click──→ [RECORDING] ──single_click──→ [ADVERTISING]
  ↑                           │                           │
  │                    ┌──long_press                     │
  │                    ▼                                 │  
  │              [MANUAL_SYNC] ──timeout──→ [IDLE] ←─────┘
  │                    │                    ▲
  └────double_click────┘                    │
       (force advertise)                    │
                                      ──timeout(60s)──
```

### Memory Management
- **RAM Usage**: ~32KB total
  - Audio buffer: 16KB ring buffer
  - BLE stack: ~8KB
  - Application: ~8KB
- **Flash Usage**: ~256KB total  
  - Bootloader: 48KB
  - Application: ~150KB
  - User data: 58KB available

### Power Management
```cpp
// USB-powered operation - no special power management needed
// Future battery operation could use:
void enterSleepMode() {
    sd_power_mode_set(NRF_POWER_MODE_LOWPWR);  // ~5μA sleep mode
    sd_app_evt_wait();                          // Wait for interrupt
}

// Wake on button press or BLE activity
void configureWakeup() {
    nrf_gpio_cfg_sense_input(BUTTON_PIN, BUTTON_PULL, NRF_GPIO_PIN_SENSE_LOW);
}
```

**Current Setup**: USB-powered, no battery management needed. Simply plug/unplug for power control.

---

## 🧪 TESTING PROCEDURES

### Unit Tests

#### Power System Test
```bash
# Expected: Device powers on/off cleanly via USB
1. USB unplugged → No LEDs, no power consumption
2. USB plugged → Boot sequence, ~50mA current draw  
3. USB-powered operation: Stable 5V/3.3V conversion
```

#### SD Card Test
```cpp
// Test code snippet
bool testSDCard() {
    if (!SD.begin(SD_CS_PIN)) return false;
    
    File testFile = SD.open("test.txt", FILE_WRITE);
    if (!testFile) return false;
    
    testFile.println("SD card test");
    testFile.close();
    
    return SD.exists("test.txt");
}
```

#### Button Response Test  
```bash
# Timing validation
Single-click: 50-500ms press duration
Double-click: Two clicks within 500ms, each 50-200ms  
Long-press: >2000ms continuous press
```

#### Audio Recording Test
```bash  
# Expected WAV file properties
Sample Rate: 16000 Hz
Bit Depth: 16-bit
Channels: 1 (mono)
Format: PCM WAV  
File Size: ~32KB per second of audio
```

#### BLE Protocol Test
```bash
# Service discovery validation
Service UUID: a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001
TX Data UUID: a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0002  
RX Credits UUID: a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0003
File Info UUID: a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0004

# Advertisement format
Device Name: "XIAO-REC"
Advertising Interval: 100ms (fast), 1000ms (slow)
```

### Integration Tests

#### End-to-End Recording Flow
1. **Trigger Recording**: Single button click
2. **Verify LED**: Solid red during recording
3. **Audio Capture**: Speak test phrase for 10 seconds  
4. **Stop Recording**: Second button click
5. **File Creation**: Check SD card for REC_XXXX.WAV
6. **BLE Advertisement**: Blue blinking LED for 60 seconds
7. **Mobile Connection**: Device should appear in app scan
8. **File Transfer**: WAV file downloads to mobile app
9. **Transcription**: Audio converts to text correctly

#### Stress Tests  
- **Continuous Recording**: 1 hour recording test (battery and SD space)
- **Multiple Files**: Record 50 short files, verify all transfer correctly
- **Power Cycling**: Test recording interruption and recovery
- **BLE Range**: Test connection stability at 5m, 10m, 15m distances

### Performance Validation
| Metric | Target | Test Method |
|--------|---------|-------------|
| Boot Time | <3 seconds | Power on to ready state |
| Recording Latency | <200ms | Button press to audio start |
| File Transfer Speed | >10KB/s | Measure BLE throughput |
| Battery Life | >8 hours | Continuous operation test |
| SD Card Write Speed | >100KB/s | Sustained write performance |

---

## 🔧 FIRMWARE MODIFICATIONS REQUIRED

### Current Code Analysis
**File**: `tainecklace_hardware/src/main.cpp` (445 lines)

**Issues Identified**:
1. **Lines 9-13**: Currently defines 3 buttons (BTN_RECORD_PIN, BTN_TRANSFER_PIN, BTN_FILES_PIN)
2. **Lines 46-95**: `update_button_state()` handles individual buttons separately  
3. **Lines 97-151**: `update_led_status()` uses simple on/off, needs RGB support
4. **Lines 203-328**: Main loop processes all 3 buttons independently

### Required Changes

#### Change 1: Single Button Pin Definition (1 line)
```cpp
// OLD (Lines 9-13)
#define BTN_RECORD_PIN    D0
#define BTN_TRANSFER_PIN  D1  
#define BTN_FILES_PIN     D2

// NEW
#define BTN_MAIN_PIN      D1    // Single button on D1
```

#### Change 2: Multi-Action Button Handler (50 lines)
```cpp
// Replace update_button_state() function
struct ButtonAction {
  bool single_click;
  bool double_click;  
  bool long_press;
};

ButtonAction updateMainButton() {
  static bool last_state = HIGH;
  static uint32_t press_start = 0;
  static uint32_t last_click = 0;
  static uint8_t click_count = 0;
  
  bool current_state = digitalRead(BTN_MAIN_PIN);
  ButtonAction action = {false, false, false};
  
  // Debouncing and multi-click detection logic
  // ... (implementation details)
  
  return action;
}
```

#### Change 3: RGB LED Controller (30 lines)  
```cpp
// Replace update_led_status() function
void updateRGBLED() {
  switch(current_device_state) {
    case STATE_RECORDING:
      setRGBColor(255, 0, 0);  // Red solid
      break;
    case STATE_CONNECTED: 
      setRGBColor(0, 0, 255);  // Blue solid
      break;
    case STATE_SYNCING:
      setRGBColor(0, 255, 0);  // Green solid  
      break;
    case STATE_ADVERTISING:
      blinkRGBColor(0, 0, 255, 500);  // Blue blink
      break;
    default:
      setRGBColor(0, 0, 0);    // Off
  }
}
```

#### Change 4: Main Loop Simplification (20 lines)
```cpp  
void loop() {
  updateRGBLED();
  
  ButtonAction btn = updateMainButton();
  
  if (btn.single_click) {
    toggleRecording();  // Start/stop recording
  }
  else if (btn.double_click) {
    startFastAdvertising(); // Force BLE advertising
  }
  else if (btn.long_press && ble_is_connected()) {
    startManualSync();  // Manual file sync
  }
  
  audio_process();
  ble_process_transfer();
}
```

### Estimated Modification Time: 25 minutes
- Button logic rewrite: 15 minutes
- LED RGB implementation: 10 minutes  
- Testing and validation: Additional 15 minutes

---

## 📋 HARDWARE VALIDATION CHECKLIST

### Pre-Assembly Validation
- [ ] All components available and correct specifications
- [ ] XIAO BLE Sense board boots and accepts code upload
- [ ] SD card module detected by multimeter continuity test
- [ ] USB-C cable provides stable power connection
- [ ] Tactile button switches cleanly (open/closed)

### Post-Assembly Validation  
- [ ] Power system: USB plug/unplug works, no shorts on breadboard
- [ ] SD card: File read/write operations successful
- [ ] Button: Single-click, double-click, long-press detection works
- [ ] LED: RGB colors display correctly (red/blue/green/off)
- [ ] Audio: WAV files created with correct format and content
- [ ] BLE: Service advertisement visible, characteristics accessible
- [ ] Integration: Full record→transfer→transcribe workflow operational

### Environmental Testing
- [ ] Temperature: Operating range 0°C to 40°C
- [ ] Humidity: Function in 30-80% relative humidity  
- [ ] Vibration: Stable connections during normal handling
- [ ] EMI: No interference with Wi-Fi or other 2.4GHz devices

### Production Readiness
- [ ] Component sourcing: All parts available for scaling
- [ ] Assembly time: <15 minutes per unit (simplified design)
- [ ] Quality control: <5% defect rate in batch testing
- [ ] Documentation: Complete assembly guide with photos
- [ ] Support: Troubleshooting guide for common issues

## 🎯 **SIMPLIFIED HARDWARE SUMMARY**

### **Single Switch BLE Control System:**

- ✅ **One tactile button**: Handles all recording and BLE functions via timing
- ✅ **USB-powered**: Simple plug/unplug power control  
- ✅ **7-wire assembly**: SD card (6) + button (1) = minimal complexity
- ✅ **Smart firmware**: Multi-click detection already implemented
- ✅ **Clear feedback**: RGB LED shows current operation mode

### **Button Operation:**

1. **Single-click**: Record/stop toggle → Auto-advertise after stop
2. **Double-click**: Force BLE advertising for manual sync
3. **Long-press**: Manual sync when connected

### **LED Status:**

- **Red solid**: Recording in progress
- **Blue blink**: BLE advertising (auto or manual)
- **Green solid**: File transfer in progress  
- **Off**: Idle, ready for commands

**Final Hardware Deliverable**: Ultra-simple XIAO BLE device with single-switch control, ready for mobile app integration testing.