#pragma once
#include <Arduino.h>
#include <SdFat.h>

/**
 * High-quality PCM audio recording system for XIAO nRF52840 Sense
 * 
 * Features:
 * - 16kHz 16-bit mono PCM recording
 * - Efficient ring buffer with ISR-safe operations
 * - Proper WAV file format with correct headers
 * - SD card management with error handling
 * - BLE file transfer support
 */

// Core audio functions
bool audio_init();                    // Initialize SD card and PDM microphone
bool audio_start_recording();         // Start recording to new WAV file
void audio_process();                 // Process audio data (call in main loop)
void audio_stop_recording();          // Stop recording and finalize WAV file

// Status and information
bool audio_is_recording();            // Check if currently recording
const char* audio_get_last_filename(); // Get last recorded filename
uint32_t audio_get_bytes_recorded();   // Get total bytes recorded
uint32_t audio_get_buffer_overruns();  // Get number of buffer overruns
uint32_t audio_get_recording_seconds(); // Get recording duration in seconds

// SD card health monitoring
uint32_t audio_get_sd_write_errors();  // Get number of SD write errors
uint32_t audio_get_sd_retry_count();   // Get number of SD write retries
uint32_t audio_get_last_write_latency_ms(); // Get last write latency in ms

// SD card access for other modules (e.g., BLE transfer)
SdFs* audio_get_sd_instance();