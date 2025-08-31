#pragma once

#include <cstdint>
#include <cstddef>

// -------- Audio Configuration --------
constexpr uint32_t SAMPLE_RATE = 16000;  // Hz - optimized for voice
constexpr uint16_t CHANNELS = 1;          // Mono recording
constexpr uint16_t BITS_PER_SAMPLE = 16; // 16-bit PCM
constexpr uint8_t PDM_GAIN = 60;          // PDM gain (0-127, adjust for voice levels)

// -------- Compression Configuration --------
constexpr bool ENABLE_ADPCM_COMPRESSION = true;  // Enable ADPCM 4:1 compression
constexpr uint16_t ADPCM_BITS_PER_SAMPLE = 4;    // ADPCM compressed bits per sample

// Calculated audio parameters
constexpr uint32_t BYTES_PER_SECOND = ENABLE_ADPCM_COMPRESSION ? 
    (SAMPLE_RATE * ADPCM_BITS_PER_SAMPLE) / 8 * CHANNELS :  // ADPCM: 8KB/s
    SAMPLE_RATE * (BITS_PER_SAMPLE / 8) * CHANNELS;         // PCM: 32KB/s
constexpr uint16_t BLOCK_ALIGN = CHANNELS * (BITS_PER_SAMPLE / 8);

// -------- Buffer Configuration --------
constexpr size_t RING_BUFFER_SIZE = 64 * 1024;  // 64KB ring buffer for PDM ISR
constexpr size_t SD_WRITE_BUFFER_SIZE = 16 * 1024;  // 16KB SD write buffer (multiple of 512)

// Ensure ring buffer is power of 2 for efficient masking
static_assert((RING_BUFFER_SIZE & (RING_BUFFER_SIZE - 1)) == 0, "Ring buffer size must be power of 2");

// -------- SD Card Configuration --------
constexpr int SD_CS_PIN = 6;    // XIAO Sense CS pin
constexpr int SD_SPEED_MHZ = 25; // SD card speed (try 40 if stable)

// File pre-allocation (16MB for ~8.5 hours of 16kHz mono PCM)
constexpr uint32_t PREALLOC_SIZE_MB = 16;
constexpr uint32_t PREALLOC_BYTES = PREALLOC_SIZE_MB * 1024 * 1024;

// -------- File Naming --------
constexpr const char* FILE_PREFIX = "REC_";
constexpr const char* FILE_EXTENSION = ENABLE_ADPCM_COMPRESSION ? ".ADPCM" : ".WAV";
constexpr size_t MAX_FILENAME_LENGTH = 32;

// -------- BLE Configuration --------
#define BLE_SERVICE_UUID     "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001"
#define BLE_TX_DATA_UUID     "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0002" // notify, up to 244B
#define BLE_RX_CREDITS_UUID  "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0003" // write w/o resp, 1B
#define BLE_FILE_INFO_UUID   "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0004" // read: [u32 size][name...]