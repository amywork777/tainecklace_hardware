# installing :P

## Openvscode or cusor, install the PlatformIO extension
- clone this repo
- click the button in the bottom left with the checkmark to compile
- click the arrow `-->` to upload to the device
- everything is automatically handled packages are automatically installed from the platformio.ini file

## Parts Needed
- 1x Seeed XIAO BLE Sense (has built-in microphone)
- 1x MicroSD card module (SPI interface)
- 1x MicroSD card (FAT32 formatted <-- really important wont work without thi, ≤32GB)
- 3x Momentary push buttons (normally open type)
- Arduino jumper wires, may need to strip and make shorter, I already sent some pre stripped (male-to-male)

## Step-by-Step Assembly

### 1. SD Card Wiring
Connect SD card module to XIAO using 6 wires:

``` 
XIAO Pin --> SD Module Pin
D6       --> CS (chip select)
D8       --> CLK/SCK (serial clock) 
D9       --> MISO/DO (data out from SD)
D10      --> MOSI/DI (data in to SD)
3V3      --> VCC (power - MUST be 3.3V, if you ever change boards it wont work with 5V)
GND      --> GND (ground)
```

### 2. Button Wiring
Connect 3 buttons between XIAO pins and ground (or just have claude modify for a single button press based on time held, i.e., complete circuit for 2seconds vs 4seconds vs 8seconds for each function). Each button needs 2 wires:

```
Button Function     --> XIAO Pin --> Button --> GND
Record/Stop         --> D0       --> SW1    --> GND
BLE Transfer        --> D1       --> SW2    --> GND  
File Management     --> D2       --> SW3    --> GND
```

**Button Details:**
- Use normally-open momentary switches (not latching)
- When pressed, button connects pin to GND (active low)
- Internal pull-up resistors enabled automatically in code

## How the Buttons Work
The code maps CLI commands to button presses based on duration:

- **D0 (Record Button)**: Single press toggles recording on/off (like typing 'r' or 's' commands)
- **D1 (Transfer Button)**: Hold for 2 seconds to start BLE advertising (like 'c' command)
- **D2 (File Button)**: 
  - Single press = list all WAV files (like 'l' command)
  - Hold 2 seconds = delete all WAV files (like 'd' command)

## Programming & Setup
1. Wire everything as shown above
2. Install PlatformIO in VS Code
3. Clone/download this code
4. Format SD card as FAT32 
5. Flash firmware: `platformio run --target upload`
6. Open serial monitor at 115200 baud to see status messages

## Using the Device
**Serial Control:** Connect USB and type commands in serial monitor:
- `r` = start recording
- `s` = stop recording  
- `c` = start BLE transfer
- `l` = list files
- `d` = delete all files

**Button Control:** Use physical buttons for same functions without computer

**LED Status:**
- Slow blink = ready/idle
- Solid on = recording  
- Fast blink = BLE transferring
- 3 quick flashes = error

## File Transfer
1. Record audio (button or serial)
2. Start BLE transfer (hold D1 or type 'c')
3. On computer: `python python_scripts/ble_receiver.py`
4. Files download wirelessly as .WAV

## Code Structure
- `src/main.cpp` - Main program with button handling
- `src/audio.cpp` - Audio recording system with ring buffers
- `src/ble.cpp` - Bluetooth file transfer protocol
- `src/config.h` - All settings (sample rate, pins, buffer sizes)
- `python_scripts/` - Computer tools for downloading/playing files/testing and what not