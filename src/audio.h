#pragma once
#include <Arduino.h>
#include <SdFat.h>

/**
 * High-quality audio recording system for XIAO nRF52840 Sense
 * 
 * Features:
 * - 16kHz mono audio recording with optional ADPCM compression
 * - Real-time ADPCM encoding for 4:1 compression ratio
 * - Efficient ring buffer with ISR-safe operations
 * - WAV format (uncompressed) or ADPCM format (compressed)
 * - SD card management with error handling
 * - BLE file transfer support with faster transmission for compressed files
 */

// Core audio functions
bool audio_init();                    // Initialize SD card and PDM microphone
bool audio_start_recording();         // Start recording to new WAV file
void audio_process();                 // Process audio data (call in main loop)
void audio_stop_recording();          // Stop recording and finalize WAV file

// Live streaming functions
bool audio_start_streaming();         // Start live audio streaming
void audio_stop_streaming();          // Stop live audio streaming
bool audio_is_streaming();            // Check if currently streaming

// Status and information
bool audio_is_recording();            // Check if currently recording
const char* audio_get_last_filename(); // Get last recorded filename
uint32_t audio_get_bytes_recorded();   // Get total bytes recorded
uint32_t audio_get_buffer_overruns();  // Get number of buffer overruns
uint32_t audio_get_recording_seconds(); // Get recording duration in seconds

// SD card access for other modules (e.g., BLE transfer)
SdFs* audio_get_sd_instance();