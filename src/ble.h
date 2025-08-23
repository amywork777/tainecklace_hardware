#pragma once
#include <Arduino.h>
#include <SdFat.h>

/**
 * High-performance BLE file transfer system
 * 
 * Features:
 * - Credit-based flow control for reliable transfers
 * - Large MTU support for maximum throughput
 * - Resume capability for interrupted transfers
 * - CRC16 error detection
 */

// Core BLE functions
bool ble_init();                           // Initialize BLE service and characteristics
void ble_start_advertising();              // Start advertising for file transfer
void ble_stop_advertising();               // Stop advertising and disconnect
bool ble_is_connected();                   // Check if client is connected

// File transfer functions
void ble_set_transfer_file(FsFile* file, uint32_t file_size, const char* filename);
void ble_process_transfer();               // Process file transfer (call in main loop)