#include "button.h"

// ========================================
// Button Configuration Constants
// ========================================

// Switch timing constants
static const uint32_t DEBOUNCE_DELAY_MS = 100;         // Longer debounce for reliable stop detection
static const uint32_t RECORDING_START_DELAY_MS = 2000; // 2s delay before recording starts

// ========================================
// Button State Management
// ========================================

// Switch state structure
struct SwitchState {
  bool current_state;      // Current debounced state (HIGH/LOW)
  bool last_state;         // Last raw reading for debounce
  uint32_t press_start_time;     // When switch went to HIGH
  uint32_t last_debounce_time;   // For debouncing
  bool recording_started;        // Track if we've already tried to start recording
  bool recording_failed;         // Track if recording failed this position
};

// Button state variables
static SwitchState g_switch = {HIGH, HIGH, 0, 0, false, false};
static int g_button_pin = -1;

// ========================================
// Public API Implementation
// ========================================

bool button_init(int pin) {
    g_button_pin = pin;
    pinMode(pin, INPUT_PULLUP);
    
    // Allow pin to stabilize
    delay(100);
    bool initial_reading = digitalRead(pin);
    
    // Initialize switch state to current physical position
    g_switch.current_state = initial_reading;
    g_switch.last_state = initial_reading;
    g_switch.last_debounce_time = millis();
    g_switch.recording_started = false;
    g_switch.recording_failed = false;
    
    Serial.println("Button module initialized");
    Serial.print("Initial switch position: ");
    Serial.println(initial_reading ? "HIGH (recording position)" : "LOW (stop position)");
    
    return true;
}

ButtonAction button_update() {
    if (g_button_pin == -1) {
        return BTN_NO_ACTION; // Not initialized
    }
    
    bool current_reading = digitalRead(g_button_pin);
    ButtonAction action = BTN_NO_ACTION;
    
    // Handle debouncing
    if (current_reading != g_switch.last_state) {
        g_switch.last_debounce_time = millis();
    }
    
    // Only process after debounce delay
    if ((millis() - g_switch.last_debounce_time) > DEBOUNCE_DELAY_MS) {
        // Check for actual state change
        if (current_reading != g_switch.current_state) {
            g_switch.current_state = current_reading;
            
            if (current_reading) {
                // Switch flipped to HIGH - reset state and start delay
                Serial.println("=== SWITCH TO HIGH (RECORDING POSITION) ===");
                Serial.println("-> Starting 2-second delay before recording...");
                g_switch.press_start_time = millis();
                g_switch.recording_started = false;  // Reset - allow new attempt
                g_switch.recording_failed = false;   // Reset failure state
            } else {
                // Switch flipped to LOW - stop recording and reset state
                Serial.println("=== SWITCH TO LOW (STOP POSITION) ===");
                Serial.println("-> Requesting recording stop and BLE transfer...");
                action = BTN_STOP_RECORD;
                
                // Reset all state when going LOW
                g_switch.press_start_time = 0;
                g_switch.recording_started = false;
                g_switch.recording_failed = false;
            }
        }
        
        // Handle HIGH position - only try to start recording ONCE per position
        if (current_reading && g_switch.current_state && 
            !g_switch.recording_started && !g_switch.recording_failed) {
            uint32_t hold_duration = millis() - g_switch.press_start_time;
            
            if (hold_duration >= RECORDING_START_DELAY_MS) {
                // 2 seconds reached - try to start recording (only once!)
                g_switch.recording_started = true; // Mark that we attempted
                Serial.println("=== 2 SECONDS REACHED ===");
                Serial.println("-> Requesting recording start (switch HIGH for >2s)...");
                action = BTN_START_RECORD;
            } else {
                // Still in delay period - show countdown occasionally
                static uint32_t last_feedback = 0;
                if (millis() - last_feedback > 1000) { // Every 1 second
                    uint32_t remaining = RECORDING_START_DELAY_MS - hold_duration;
                    Serial.print("Delay: "); 
                    Serial.print(remaining); 
                    Serial.println("ms remaining before recording starts");
                    last_feedback = millis();
                }
            }
        }
    }
    
    g_switch.last_state = current_reading;
    return action;
}

void button_reset_state() {
    g_switch.press_start_time = 0;
    g_switch.recording_started = false;
    g_switch.recording_failed = false;
    
    Serial.println("Button state reset");
}

bool button_is_pressed() {
    if (g_button_pin == -1) return false;
    return !g_switch.current_state; // LOW = pressed (active low with pullup)
}

bool button_is_recording_position() {
    if (g_button_pin == -1) return false;
    return g_switch.current_state; // HIGH = recording position
}

uint32_t button_get_hold_duration() {
    if (g_button_pin == -1 || g_switch.press_start_time == 0) return 0;
    return millis() - g_switch.press_start_time;
}

bool button_has_failed() {
    return g_switch.recording_failed;
}

// ========================================
// Internal Helper Functions
// ========================================

// Function to mark recording attempt as failed (called by main.cpp)
void button_mark_failed() {
    g_switch.recording_failed = true;
    Serial.println("Button: Recording attempt marked as failed");
}