#include "led.h"

// ========================================
// LED Configuration Constants
// ========================================

// LED timing constants (in milliseconds)
static const uint32_t LED_IDLE_INTERVAL = 500;        // Slow blink: 1Hz (500ms on/off)
static const uint32_t LED_BLE_ADVERTISING_INTERVAL = 250; // Medium blink: 2Hz (250ms on/off)
static const uint32_t LED_TRANSFERRING_INTERVAL = 100; // Fast blink: 5Hz (100ms on/off)
static const uint32_t LED_ERROR_FLASH_INTERVAL = 150;  // Error flash timing
static const uint32_t LED_ERROR_PAUSE_INTERVAL = 1000; // Pause between error sequences

// ========================================
// LED State Variables
// ========================================

static int g_led_pin = -1;                    // LED pin number
static LedStatus g_current_status = LED_IDLE; // Current LED status
static uint32_t g_last_update = 0;            // Last LED update time
static bool g_led_state = false;              // Current LED on/off state
static uint8_t g_error_flash_count = 0;       // Error flash counter
static bool g_force_override = false;         // Override normal patterns
static bool g_force_state = false;            // Forced LED state when overridden

// ========================================
// Public API Implementation
// ========================================

bool led_init(int pin) {
    g_led_pin = pin;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    
    // Initialize LED state
    g_current_status = LED_IDLE;
    g_last_update = millis();
    g_led_state = false;
    g_error_flash_count = 0;
    g_force_override = false;
    
    Serial.println("LED module initialized");
    return true;
}

void led_update() {
    if (g_led_pin == -1) {
        return; // Not initialized
    }
    
    // Handle forced override states
    if (g_force_override) {
        digitalWrite(g_led_pin, g_force_state);
        return;
    }
    
    uint32_t current_time = millis();
    
    // Update LED patterns based on current status
    switch (g_current_status) {
        case LED_IDLE:
            // Slow blink (1Hz) - breathing pattern for "ready" state
            if (current_time - g_last_update >= LED_IDLE_INTERVAL) {
                g_led_state = !g_led_state;
                digitalWrite(g_led_pin, g_led_state);
                g_last_update = current_time;
            }
            break;
            
        case LED_RECORDING:
            // Solid on - indicates active recording
            digitalWrite(g_led_pin, HIGH);
            break;
            
        case LED_BLE_ADVERTISING:
            // Medium blink (2Hz) - indicates BLE advertising
            if (current_time - g_last_update >= LED_BLE_ADVERTISING_INTERVAL) {
                g_led_state = !g_led_state;
                digitalWrite(g_led_pin, g_led_state);
                g_last_update = current_time;
            }
            break;
            
        case LED_TRANSFERRING:
            // Fast blink (5Hz) - indicates data transfer activity
            if (current_time - g_last_update >= LED_TRANSFERRING_INTERVAL) {
                g_led_state = !g_led_state;
                digitalWrite(g_led_pin, g_led_state);
                g_last_update = current_time;
            }
            break;
            
        case LED_ERROR:
            // 3 quick flashes then pause - error indication
            if (current_time - g_last_update >= LED_ERROR_FLASH_INTERVAL) {
                if (g_error_flash_count < 6) { // 3 on/off cycles = 6 state changes
                    g_led_state = !g_led_state;
                    digitalWrite(g_led_pin, g_led_state);
                    g_error_flash_count++;
                    g_last_update = current_time;
                } else {
                    // Pause phase - LED off and wait
                    digitalWrite(g_led_pin, LOW);
                    if (current_time - g_last_update >= LED_ERROR_PAUSE_INTERVAL) {
                        g_error_flash_count = 0; // Reset for next error sequence
                        g_last_update = current_time;
                    }
                }
            }
            break;
    }
}

void led_set_status(LedStatus status) {
    // Only change if different status
    if (status != g_current_status) {
        g_current_status = status;
        g_last_update = millis();
        g_error_flash_count = 0; // Reset error counter on status change
        
        // Immediate LED update for certain states
        if (status == LED_RECORDING) {
            digitalWrite(g_led_pin, HIGH);
        } else if (status == LED_IDLE) {
            g_led_state = false; // Start idle pattern in OFF state
        }
    }
}

void led_force_on() {
    g_force_override = true;
    g_force_state = true;
    if (g_led_pin != -1) {
        digitalWrite(g_led_pin, HIGH);
    }
}

void led_force_off() {
    g_force_override = true;
    g_force_state = false;
    if (g_led_pin != -1) {
        digitalWrite(g_led_pin, LOW);
    }
}

void led_resume_patterns() {
    g_force_override = false;
    g_last_update = millis(); // Reset timing for smooth pattern resume
}

LedStatus led_get_status() {
    return g_current_status;
}

bool led_is_on() {
    if (g_led_pin == -1) return false;
    return digitalRead(g_led_pin);
}