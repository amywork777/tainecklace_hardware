# Single Button Sanity Test Guide

## Button Timing Modes

### SHORT PRESS (<1 second)
- **Function**: Record Toggle
- **Behavior**: 
  - If IDLE → START recording (LED solid red)
  - If RECORDING → STOP recording + auto-start BLE advertising (LED blue blink)

### LONG PRESS (1-3 seconds)  
- **Function**: Manual BLE Transfer
- **Behavior**:
  - If IDLE → Start BLE advertising for last recorded file
  - If RECORDING → Show error (cannot transfer while recording)

### TOO LONG (>3 seconds)
- **Function**: Ignored
- **Behavior**: No action taken, press ignored

## Expected Serial Output

### Button Press Detection:
```
=== BUTTON PRESSED - TIMING STARTED ===
Current mode: IDLE (short press will START recording)
```

### Real-time Feedback While Holding:
```
Holding 500ms - still in SHORT PRESS range (<1s)
Holding 1000ms - in LONG PRESS range (1-3s) - BLE transfer mode
Holding 1500ms - in LONG PRESS range (1-3s) - BLE transfer mode
```

### Button Release and Action:
```
=== BUTTON RELEASED after 800ms ===
-> SHORT PRESS detected (<1s) - RECORD TOGGLE MODE
   Action: Will START new recording

=====================================
EXECUTING SHORT PRESS ACTION
=====================================
> Mode: START RECORDING
✓ Recording started: REC_001.WAV
=====================================
```

## Test Procedure

1. **Power On Test**
   - Connect USB-C cable
   - Verify startup message shows single button mode
   - Check that LED shows idle status

2. **Short Press Test (Start Recording)**
   - Press button for ~0.5 seconds
   - Verify "SHORT PRESS" detection in serial
   - Check LED turns solid red
   - Confirm recording starts

3. **Short Press Test (Stop Recording)**
   - Press button for ~0.5 seconds while recording
   - Verify recording stops and BLE advertising starts
   - Check LED changes to blue blink pattern

4. **Long Press Test (BLE Transfer)**
   - Press button for ~2 seconds when idle
   - Verify "LONG PRESS" detection in serial
   - Confirm BLE advertising starts immediately

5. **Error Condition Test**
   - Try long press while recording
   - Verify error message appears
   - Confirm no BLE advertising starts

6. **Timing Boundary Tests**
   - Test exactly 1 second press
   - Test exactly 3 second press
   - Test >3 second press (should be ignored)

## Success Criteria

- ✅ Clear serial feedback for all button states
- ✅ Accurate timing detection (<1s, 1-3s, >3s)
- ✅ Proper mode switching based on current state
- ✅ LED patterns match expected behavior
- ✅ Error handling for invalid operations
- ✅ Real-time feedback while button is held