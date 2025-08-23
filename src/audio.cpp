#include "config.h"
#include "audio.h"
#include <SdFat.h>
#include <PDM.h>

// ========================================
// Constants and Configuration
// ========================================

// Ring buffer mask for efficient modulo operations
constexpr size_t RING_BUFFER_MASK = RING_BUFFER_SIZE - 1;

// SD card sector size for aligned writes
constexpr size_t SD_SECTOR_SIZE = 512;

// Maximum PDM data to read per ISR call (256 samples * 2 bytes)
constexpr size_t MAX_PDM_BYTES_PER_ISR = 512;

// Status update interval in milliseconds
constexpr uint32_t STATUS_UPDATE_INTERVAL_MS = 500;

// ========================================
// WAV File Format Structures
// ========================================

struct __attribute__((packed)) WAVHeader {
    // RIFF chunk
    char riff_id[4] = {'R', 'I', 'F', 'F'};
    uint32_t file_size = 36;  // Will be updated when recording stops
    char wave_id[4] = {'W', 'A', 'V', 'E'};
    
    // Format chunk
    char fmt_id[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t num_channels = CHANNELS;
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate = BYTES_PER_SECOND;
    uint16_t block_align = BLOCK_ALIGN;
    uint16_t bits_per_sample = BITS_PER_SAMPLE;
    
    // Data chunk header
    char data_id[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;  // Will be updated when recording stops
};

// ========================================
// Global State Variables
// ========================================

// SD card and file system
static SdFs g_sd_card;
static FsFile g_audio_file;

// Ring buffer for PDM data (written by ISR, read by main loop)
alignas(4) static uint8_t g_ring_buffer[RING_BUFFER_SIZE];
static volatile uint32_t g_ring_write_pos = 0;  // Written by ISR
static volatile uint32_t g_ring_read_pos = 0;   // Written by main loop

// SD write buffer for efficient sector-aligned writes
alignas(4) static uint8_t g_sd_write_buffer[SD_WRITE_BUFFER_SIZE];
static size_t g_sd_buffer_fill = 0;

// Recording state
static volatile bool g_is_recording = false;
static uint32_t g_total_bytes_recorded = 0;
static volatile uint32_t g_buffer_overruns = 0;
static uint32_t g_file_counter = 0;
static char g_current_filename[MAX_FILENAME_LENGTH] = {0};
static uint32_t g_last_status_update_ms = 0;

// ========================================
// Ring Buffer Utility Functions
// ========================================

/**
 * Get number of bytes available to read from ring buffer
 * Thread-safe: can be called from main loop while ISR is writing
 */
inline uint32_t ring_buffer_available() {
    uint32_t write_pos = g_ring_write_pos;  // Atomic snapshot
    uint32_t read_pos = g_ring_read_pos;    // Atomic snapshot
    return (write_pos - read_pos) & RING_BUFFER_MASK;
}

/**
 * Get number of free bytes in ring buffer
 * Thread-safe: can be called from ISR while main loop is reading
 */
inline uint32_t ring_buffer_free_space() {
    return RING_BUFFER_SIZE - ring_buffer_available();
}

// ========================================
// PDM Interrupt Service Routine
// ========================================

/**
 * PDM data ready callback - called from interrupt context
 * Must be fast and non-blocking
 */
static void on_pdm_data_ready() {
    int available_bytes;
    
    // Process all available PDM data
    while ((available_bytes = PDM.available()) > 0) {
        // Limit read size to prevent ISR from running too long
        int bytes_to_read = min(available_bytes, (int)MAX_PDM_BYTES_PER_ISR);
        
        // Check if we have enough space in ring buffer
        if (ring_buffer_free_space() < (uint32_t)bytes_to_read) {
            g_buffer_overruns++;
            return;  // Drop data to prevent buffer overflow
        }
        
        // Read PDM data into temporary buffer
        uint8_t temp_buffer[MAX_PDM_BYTES_PER_ISR];
        int bytes_read = PDM.read(temp_buffer, bytes_to_read);
        
        if (bytes_read <= 0) {
            break;  // No more data available
        }
        
        // Copy to ring buffer with wraparound handling
        uint32_t write_pos = g_ring_write_pos & RING_BUFFER_MASK;
        uint32_t bytes_until_end = RING_BUFFER_SIZE - write_pos;
        
        if ((uint32_t)bytes_read <= bytes_until_end) {
            // No wraparound needed
            memcpy(&g_ring_buffer[write_pos], temp_buffer, bytes_read);
        } else {
            // Handle wraparound
            memcpy(&g_ring_buffer[write_pos], temp_buffer, bytes_until_end);
            memcpy(&g_ring_buffer[0], temp_buffer + bytes_until_end, bytes_read - bytes_until_end);
        }
        
        // Update write position atomically
        g_ring_write_pos += bytes_read;
    }
}

// ========================================
// SD Card Write Functions
// ========================================

/**
 * Flush sector-aligned data from SD write buffer to file
 * Only writes complete 512-byte sectors for optimal performance
 */
static bool flush_aligned_sectors() {
    size_t aligned_bytes = g_sd_buffer_fill & ~(SD_SECTOR_SIZE - 1);
    
    if (aligned_bytes == 0) {
        return true;  // Nothing to flush
    }
    
    size_t bytes_written = g_audio_file.write(g_sd_write_buffer, aligned_bytes);
    if (bytes_written != aligned_bytes) {
        Serial.println("ERROR: SD write failed");
        g_is_recording = false;
        return false;
    }
    
    // Move remaining unaligned data to beginning of buffer
    size_t remaining_bytes = g_sd_buffer_fill - aligned_bytes;
    if (remaining_bytes > 0) {
        memmove(g_sd_write_buffer, g_sd_write_buffer + aligned_bytes, remaining_bytes);
    }
    g_sd_buffer_fill = remaining_bytes;
    
    return true;
}

/**
 * Transfer data from ring buffer to SD write buffer
 * Handles ring buffer wraparound and triggers SD writes when buffer is full
 */
static bool process_ring_buffer_data() {
    uint32_t available_bytes = ring_buffer_available();
    
    while (available_bytes > 0) {
        // Check if SD write buffer has space
        size_t buffer_space = SD_WRITE_BUFFER_SIZE - g_sd_buffer_fill;
        if (buffer_space == 0) {
            if (!flush_aligned_sectors()) {
                return false;  // SD write error
            }
            buffer_space = SD_WRITE_BUFFER_SIZE - g_sd_buffer_fill;
            if (buffer_space == 0) {
                return false;  // Still no space after flush
            }
        }
        
        // Calculate how much data to transfer
        uint32_t read_pos = g_ring_read_pos & RING_BUFFER_MASK;
        uint32_t bytes_to_transfer = min(available_bytes, (uint32_t)buffer_space);
        uint32_t bytes_until_end = RING_BUFFER_SIZE - read_pos;
        uint32_t first_chunk = min(bytes_to_transfer, bytes_until_end);
        
        // Copy first chunk (no wraparound)
        memcpy(g_sd_write_buffer + g_sd_buffer_fill, &g_ring_buffer[read_pos], first_chunk);
        
        // Copy second chunk if wraparound occurred
        if (bytes_to_transfer > first_chunk) {
            uint32_t second_chunk = bytes_to_transfer - first_chunk;
            memcpy(g_sd_write_buffer + g_sd_buffer_fill + first_chunk, &g_ring_buffer[0], second_chunk);
        }
        
        // Update positions and counters
        g_ring_read_pos += bytes_to_transfer;
        g_sd_buffer_fill += bytes_to_transfer;
        g_total_bytes_recorded += bytes_to_transfer;
        available_bytes -= bytes_to_transfer;
        
        // Flush if buffer is getting full
        if (g_sd_buffer_fill >= SD_SECTOR_SIZE) {
            if (!flush_aligned_sectors()) {
                return false;
            }
        }
    }
    
    return true;
}

// ========================================
// File Management Functions
// ========================================

/**
 * Generate next available filename (REC_0000.WAV, REC_0001.WAV, etc.)
 */
static void generate_next_filename() {
    do {
        snprintf(g_current_filename, sizeof(g_current_filename), 
                "%s%04lu%s", FILE_PREFIX, (unsigned long)g_file_counter, FILE_EXTENSION);
        g_file_counter++;
    } while (g_sd_card.exists(g_current_filename));
}

/**
 * Create new WAV file with proper header
 */
static bool create_wav_file() {
    generate_next_filename();
    
    Serial.print("Creating audio file: ");
    Serial.println(g_current_filename);
    
    // Close any existing file
    if (g_audio_file.isOpen()) {
        g_audio_file.close();
    }
    
    // Create new file
    if (!g_audio_file.open(g_current_filename, O_WRITE | O_CREAT | O_TRUNC)) {
        Serial.print("ERROR: Failed to create file: ");
        Serial.println(g_sd_card.sdErrorCode());
        return false;
    }
    
    // Pre-allocate space for better performance
    if (g_audio_file.preAllocate(PREALLOC_BYTES)) {
        Serial.println("Pre-allocated file space");
    }
    
    // Write WAV header (will be updated when recording stops)
    WAVHeader header;
    size_t bytes_written = g_audio_file.write(&header, sizeof(header));
    if (bytes_written != sizeof(header)) {
        Serial.println("ERROR: Failed to write WAV header");
        g_audio_file.close();
        return false;
    }
    
    return true;
}

/**
 * Finalize WAV file by updating header with actual data size
 */
static bool finalize_wav_file() {
    if (!g_audio_file.isOpen()) {
        return false;
    }
    
    // Flush any remaining data
    if (g_sd_buffer_fill > 0) {
        g_audio_file.write(g_sd_write_buffer, g_sd_buffer_fill);
        g_sd_buffer_fill = 0;
    }
    
    // Update WAV header with actual file size
    WAVHeader header;
    header.data_size = g_total_bytes_recorded;
    header.file_size = 36 + g_total_bytes_recorded;
    
    // Seek to beginning and write updated header
    if (!g_audio_file.seekSet(0)) {
        Serial.println("ERROR: Failed to seek to file beginning");
        return false;
    }
    
    size_t bytes_written = g_audio_file.write(&header, sizeof(header));
    if (bytes_written != sizeof(header)) {
        Serial.println("ERROR: Failed to update WAV header");
        return false;
    }
    
    // Truncate file to actual size (removes pre-allocated unused space)
    uint32_t final_file_size = sizeof(WAVHeader) + g_total_bytes_recorded;
    if (!g_audio_file.truncate(final_file_size)) {
        Serial.println("WARNING: Failed to truncate file to final size");
    }
    
    // Flush and close file
    g_audio_file.flush();
    g_audio_file.close();
    
    Serial.print("Recording finalized: ");
    Serial.print(g_total_bytes_recorded);
    Serial.println(" bytes");
    
    return true;
}

// ========================================
// Public API Implementation
// ========================================

bool audio_init() {
    Serial.println("Initializing audio system...");
    
    // Initialize SD card
    Serial.print("Initializing SD card on CS pin ");
    Serial.print(SD_CS_PIN);
    Serial.println("...");
    
    if (!g_sd_card.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(SD_SPEED_MHZ)))) {
        Serial.print("ERROR: SD card initialization failed! Error code: ");
        Serial.println(g_sd_card.card()->errorCode());
        return false;
    }
    
    // Test SD card write capability
    Serial.println("Testing SD card write capability...");
    FsFile test_file;
    if (test_file.open("AUDIO_TEST.TXT", O_WRITE | O_CREAT | O_TRUNC)) {
        test_file.println("Audio system test");
        test_file.close();
        g_sd_card.remove("AUDIO_TEST.TXT");
        Serial.println("SD card write test passed");
    } else {
        Serial.println("ERROR: SD card write test failed!");
        return false;
    }
    
    // Initialize PDM microphone
    Serial.println("Initializing PDM microphone...");
    PDM.onReceive(on_pdm_data_ready);
    PDM.setGain(PDM_GAIN);
    
    #ifdef ARDUINO_ARCH_MBED
    PDM.setBufferSize(4096);  // Optimal buffer size for nRF52840
    #endif
    
    if (!PDM.begin(CHANNELS, SAMPLE_RATE)) {
        Serial.println("ERROR: PDM microphone initialization failed!");
        return false;
    }
    
    Serial.println("Audio system initialized successfully");
    return true;
}

bool audio_start_recording() {
    if (g_is_recording) {
        Serial.println("ERROR: Already recording!");
        return false;
    }
    
    Serial.println("Starting audio recording...");
    
    // Reset all counters and buffers
    g_ring_write_pos = 0;
    g_ring_read_pos = 0;
    g_total_bytes_recorded = 0;
    g_buffer_overruns = 0;
    g_sd_buffer_fill = 0;
    g_last_status_update_ms = millis();
    
    // Create new WAV file
    if (!create_wav_file()) {
        return false;
    }
    
    // Start recording
    g_is_recording = true;
    
    Serial.print("Recording started: ");
    Serial.println(g_current_filename);
    return true;
}

void audio_process() {
    if (!g_is_recording) {
        return;  // Nothing to do if not recording
    }
    
    // Process ring buffer data
    if (!process_ring_buffer_data()) {
        Serial.println("ERROR: Audio processing failed, stopping recording");
        audio_stop_recording();
        return;
    }
    
    // Print status updates periodically
    uint32_t current_time = millis();
    if (current_time - g_last_status_update_ms >= STATUS_UPDATE_INTERVAL_MS) {
        g_last_status_update_ms = current_time;
        
        uint32_t recording_seconds = g_total_bytes_recorded / BYTES_PER_SECOND;
        uint32_t buffer_usage_percent = (ring_buffer_available() * 100) / RING_BUFFER_SIZE;
        
        Serial.print("Recording: ");
        Serial.print(recording_seconds);
        Serial.print("s, Buffer: ");
        Serial.print(buffer_usage_percent);
        Serial.print("%, Overruns: ");
        Serial.println(g_buffer_overruns);
    }
}

void audio_stop_recording() {
    if (!g_is_recording) {
        Serial.println("WARNING: Not currently recording");
        return;
    }
    
    Serial.println("Stopping audio recording...");
    g_is_recording = false;
    
    // Process any remaining data in ring buffer
    process_ring_buffer_data();
    
    // Finalize WAV file
    if (!finalize_wav_file()) {
        Serial.println("ERROR: Failed to finalize WAV file");
    }
    
    // Print final statistics
    uint32_t recording_seconds = g_total_bytes_recorded / BYTES_PER_SECOND;
    Serial.print("Recording completed: ");
    Serial.print(g_current_filename);
    Serial.print(" (");
    Serial.print(recording_seconds);
    Serial.print("s, ");
    Serial.print(g_total_bytes_recorded);
    Serial.print(" bytes");
    if (g_buffer_overruns > 0) {
        Serial.print(", ");
        Serial.print(g_buffer_overruns);
        Serial.print(" overruns");
    }
    Serial.println(")");
}

bool audio_is_recording() {
    return g_is_recording;
}

const char* audio_get_last_filename() {
    return g_current_filename;
}

uint32_t audio_get_bytes_recorded() {
    return g_total_bytes_recorded;
}

uint32_t audio_get_buffer_overruns() {
    return g_buffer_overruns;
}

uint32_t audio_get_recording_seconds() {
    return g_total_bytes_recorded / BYTES_PER_SECOND;
}

SdFs* audio_get_sd_instance() {
    return &g_sd_card;
}