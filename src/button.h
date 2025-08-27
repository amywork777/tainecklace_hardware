#pragma once
#include <Arduino.h>

/**
 * Single Button Control System for XIAO nRF52840 Sense
 * 
 * Features:
 * - Position-based switch control (HIGH = recording, LOW = stop)
 * - Debounced input with configurable timing
 * - 2-second delay before recording starts
 * - One-attempt-per-position logic to prevent loops
 * - Auto BLE transfer after recording stops
 */

// Button action types returned by button_update()
typedef enum {
  BTN_NO_ACTION,     // No action required
  BTN_START_RECORD,  // Switch HIGH for >2s - start recording
  BTN_STOP_RECORD    // Switch LOW - stop recording and start BLE
} ButtonAction;

// Core button functions
bool button_init(int pin);           // Initialize button on specified pin
ButtonAction button_update();        // Process button state, call in main loop
void button_reset_state();           // Reset button state (for error recovery)

// Status and information
bool button_is_pressed();            // Check if button is currently pressed (LOW)
bool button_is_recording_position(); // Check if switch is in recording position (HIGH)
uint32_t button_get_hold_duration(); // Get current hold duration in ms
bool button_has_failed();            // Check if recording attempt failed

// Internal functions (called by main application)
void button_mark_failed();           // Mark recording attempt as failed