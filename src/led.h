#pragma once
#include <Arduino.h>

/**
 * LED Status Indicator System for XIAO nRF52840 Sense
 * 
 * Features:
 * - Multiple LED patterns for different system states
 * - Non-blocking pattern updates
 * - Configurable timing and behavior
 * - Clean separation from main application logic
 */

// LED status states
typedef enum {
  LED_IDLE,          // Slow blink (ready/waiting)
  LED_RECORDING,     // Solid on (recording active)
  LED_BLE_ADVERTISING, // Medium blink (BLE advertising)
  LED_TRANSFERRING,  // Fast blink (BLE transfer)
  LED_ERROR          // Quick flashes then off (error state)
} LedStatus;

// Core LED functions
bool led_init(int pin);              // Initialize LED on specified pin
void led_update();                   // Update LED patterns (call in main loop)
void led_set_status(LedStatus status); // Set current LED status/pattern

// LED control functions
void led_force_on();                 // Force LED on (override patterns)
void led_force_off();                // Force LED off (override patterns)
void led_resume_patterns();          // Resume normal pattern behavior

// Status information
LedStatus led_get_status();          // Get current LED status
bool led_is_on();                    // Check if LED is currently lit