#include "config.h"
#include "audio.h"
#include "adpcm.h"
#include "ble.h"
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
// File Format Structures
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

// ADPCM compression state and buffers
static ADPCMEncoder g_adpcm_encoder;
alignas(4) static int16_t g_pcm_temp_buffer[256];  // Temp buffer for PCM samples
alignas(4) static uint8_t g_adpcm_temp_buffer[128]; // Temp buffer for ADPCM data (half size)

// Recording state
static volatile bool g_is_recording = false;
static uint32_t g_total_bytes_recorded = 0;
static volatile uint32_t g_buffer_overruns = 0;
static uint32_t g_file_counter = 0;
static char g_current_filename[MAX_FILENAME_LENGTH] = {0};
static uint32_t g_last_status_update_ms = 0;

// Streaming state
static volatile bool g_is_streaming = false;
static ADPCMEncoder g_stream_adpcm_encoder;  // Separate encoder for streaming
static uint8_t g_stream_buffer[STREAM_CHUNK_SIZE];
static size_t g_stream_buffer_fill = 0;

// ========================================
// Forward Declarations
// ========================================
static void process_streaming_data();

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
 * Process PCM samples through ADPCM compression if enabled
 */
static uint32_t process_pcm_samples(const int16_t* pcm_samples, uint32_t sample_count, uint8_t* output_buffer) {
    if (ENABLE_ADPCM_COMPRESSION) {
        // Compress PCM to ADPCM (4:1 compression)
        return g_adpcm_encoder.encode_samples(pcm_samples, sample_count, output_buffer);
    } else {
        // Copy PCM data directly
        memcpy(output_buffer, pcm_samples, sample_count * sizeof(int16_t));
        return sample_count * sizeof(int16_t);
    }
}

/**
 * Transfer data from ring buffer to SD write buffer
 * Handles ring buffer wraparound, ADPCM compression, and triggers SD writes when buffer is full
 */
static bool process_ring_buffer_data() {
    uint32_t available_bytes = ring_buffer_available();
    
    while (available_bytes >= sizeof(int16_t) * 2) {  // Process at least 2 samples for ADPCM
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
        
        // Calculate how many samples we can process
        uint32_t samples_available = available_bytes / sizeof(int16_t);
        uint32_t samples_to_process = min(samples_available, (uint32_t)(sizeof(g_pcm_temp_buffer) / sizeof(int16_t)));
        
        // Ensure even number of samples for ADPCM (which packs 2 samples per byte)
        if (ENABLE_ADPCM_COMPRESSION && samples_to_process % 2 != 0) {
            samples_to_process--;
        }
        
        if (samples_to_process == 0) {
            break;  // Not enough samples to process
        }
        
        uint32_t bytes_to_read = samples_to_process * sizeof(int16_t);
        
        // Copy PCM data from ring buffer to temp buffer, handling wraparound
        uint32_t read_pos = g_ring_read_pos & RING_BUFFER_MASK;
        uint32_t bytes_until_end = RING_BUFFER_SIZE - read_pos;
        
        if (bytes_to_read <= bytes_until_end) {
            // No wraparound needed
            memcpy(g_pcm_temp_buffer, &g_ring_buffer[read_pos], bytes_to_read);
        } else {
            // Handle wraparound
            memcpy(g_pcm_temp_buffer, &g_ring_buffer[read_pos], bytes_until_end);
            memcpy((uint8_t*)g_pcm_temp_buffer + bytes_until_end, &g_ring_buffer[0], bytes_to_read - bytes_until_end);
        }
        
        // Process PCM samples (compress if ADPCM enabled)
        uint32_t output_bytes = process_pcm_samples(g_pcm_temp_buffer, samples_to_process, 
                                                    ENABLE_ADPCM_COMPRESSION ? g_adpcm_temp_buffer : (uint8_t*)g_pcm_temp_buffer);
        
        // Check if output fits in SD write buffer
        if (output_bytes > buffer_space) {
            if (!flush_aligned_sectors()) {
                return false;
            }
            buffer_space = SD_WRITE_BUFFER_SIZE - g_sd_buffer_fill;
            if (output_bytes > buffer_space) {
                break;  // Still no space, try next iteration
            }
        }
        
        // Copy processed data to SD write buffer
        const uint8_t* source_data = ENABLE_ADPCM_COMPRESSION ? g_adpcm_temp_buffer : (uint8_t*)g_pcm_temp_buffer;
        memcpy(g_sd_write_buffer + g_sd_buffer_fill, source_data, output_bytes);
        
        // Update positions and counters
        g_ring_read_pos += bytes_to_read;
        g_sd_buffer_fill += output_bytes;
        g_total_bytes_recorded += output_bytes;
        available_bytes -= bytes_to_read;
        
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
 * Create new audio file with proper header (WAV or ADPCM)
 */
static bool create_audio_file() {
    generate_next_filename();
    
    Serial.print("Creating audio file: ");
    Serial.println(g_current_filename);
    
    // Close any existing file
    if (g_audio_file.isOpen()) {
        g_audio_file.close();
    }
    
    // Create new file with multiple attempts and better error diagnostics
    bool file_created = false;
    uint8_t error_code = 0;
    
    // Attempt 1: Standard file creation
    if (g_audio_file.open(g_current_filename, O_WRITE | O_CREAT | O_TRUNC)) {
        file_created = true;
    } else {
        error_code = g_sd_card.sdErrorCode();
        Serial.print("⚠️  First file creation attempt failed (Error ");
        Serial.print(error_code);
        Serial.println("), trying alternatives...");
        
        // Attempt 2: Try different file flags
        if (g_audio_file.open(g_current_filename, O_WRITE | O_CREAT)) {
            file_created = true;
            Serial.println("✅ File created with alternative flags");
        } else {
            // Attempt 3: Try creating in root directory with simpler name
            char simple_name[32];
            snprintf(simple_name, sizeof(simple_name), "REC%04lu.ADP", (unsigned long)g_file_counter);
            
            if (g_audio_file.open(simple_name, O_WRITE | O_CREAT | O_TRUNC)) {
                file_created = true;
                strncpy(g_current_filename, simple_name, sizeof(g_current_filename));
                Serial.print("✅ File created with simple name: ");
                Serial.println(simple_name);
            } else {
                // Attempt 4: Force sync and try again
                // Note: SdFat sync is automatic, just add a delay for SD card stabilization
                delay(200);
                
                if (g_audio_file.open(g_current_filename, O_WRITE | O_CREAT | O_TRUNC)) {
                    file_created = true;
                    Serial.println("✅ File created after sync");
                }
            }
        }
    }
    
    if (!file_created) {
        error_code = g_sd_card.sdErrorCode();
        Serial.print("ERROR: All file creation attempts failed: ");
        Serial.print(error_code);
        Serial.print(" (");
        switch(error_code) {
            case 12: Serial.print("Address Error - SD card addressing issue"); break;
            case 13: Serial.print("Parameter Error - Invalid parameter or write protected"); break;
            case 14: Serial.print("Card Write Protected"); break;
            case 15: Serial.print("Card Locked"); break;
            case 16: Serial.print("Write Error - General write failure"); break;
            case 17: Serial.print("Card ECC Failed"); break;
            case 18: Serial.print("Card Controller Error"); break;
            case 19: Serial.print("General/Unknown Error"); break;
            case 28: Serial.print("Write Protected or File System Error"); break;
            default: Serial.print("Unknown error code");
        }
        Serial.println(")");
        
        // Additional diagnostics
        Serial.print("Free space: ");
        Serial.print(g_sd_card.freeClusterCount() * g_sd_card.sectorsPerCluster() * 512UL);
        Serial.println(" bytes");
        
        // Check if SD card is write protected
        Serial.println("💡 Possible solutions:");
        Serial.println("   - Check SD card write-protect switch (slide to unlock)");
        Serial.println("   - Try formatting SD card on computer (FAT32)");
        Serial.println("   - Try a different SD card");
        
        return false;
    }
    
    // Pre-allocate space for better performance
    if (g_audio_file.preAllocate(PREALLOC_BYTES)) {
        Serial.println("Pre-allocated file space");
    }
    
    // Write appropriate header based on compression mode
    if (ENABLE_ADPCM_COMPRESSION) {
        // Write ADPCM header
        ADPCMHeader header;
        header.sample_rate = SAMPLE_RATE;
        header.channels = CHANNELS;
        header.bits_per_sample = ADPCM_BITS_PER_SAMPLE;
        header.initial_sample = 0;           // Will be set by encoder
        header.initial_step_index = 0;       // Will be set by encoder
        
        size_t bytes_written = g_audio_file.write(&header, sizeof(header));
        if (bytes_written != sizeof(header)) {
            Serial.println("ERROR: Failed to write ADPCM header");
            g_audio_file.close();
            return false;
        }
        Serial.println("ADPCM compression enabled (4:1 ratio)");
    } else {
        // Write WAV header (will be updated when recording stops)
        WAVHeader header;
        size_t bytes_written = g_audio_file.write(&header, sizeof(header));
        if (bytes_written != sizeof(header)) {
            Serial.println("ERROR: Failed to write WAV header");
            g_audio_file.close();
            return false;
        }
        Serial.println("PCM recording (uncompressed)");
    }
    
    return true;
}

/**
 * Finalize audio file by updating header with actual data size
 */
static bool finalize_audio_file() {
    if (!g_audio_file.isOpen()) {
        return false;
    }
    
    // Flush any remaining data
    if (g_sd_buffer_fill > 0) {
        g_audio_file.write(g_sd_write_buffer, g_sd_buffer_fill);
        g_sd_buffer_fill = 0;
    }
    
    // Update header with actual file size based on format
    if (ENABLE_ADPCM_COMPRESSION) {
        // Update ADPCM header
        ADPCMHeader header;
        header.sample_rate = SAMPLE_RATE;
        header.channels = CHANNELS;
        header.bits_per_sample = ADPCM_BITS_PER_SAMPLE;
        header.data_size = g_total_bytes_recorded;
        header.total_samples = g_total_bytes_recorded * 2;  // 2 samples per byte in ADPCM
        header.initial_sample = 0;      // Could save encoder state for perfect resume
        header.initial_step_index = 0;
        
        // Seek to beginning and write updated header
        if (!g_audio_file.seekSet(0)) {
            Serial.println("ERROR: Failed to seek to file beginning");
            return false;
        }
        
        size_t bytes_written = g_audio_file.write(&header, sizeof(header));
        if (bytes_written != sizeof(header)) {
            Serial.println("ERROR: Failed to update ADPCM header");
            return false;
        }
        
        // Truncate file to actual size
        uint32_t final_file_size = sizeof(ADPCMHeader) + g_total_bytes_recorded;
        if (!g_audio_file.truncate(final_file_size)) {
            Serial.println("WARNING: Failed to truncate file to final size");
        }
        
        Serial.print("ADPCM recording finalized: ");
        Serial.print(header.total_samples);
        Serial.print(" samples, ");
        Serial.print(g_total_bytes_recorded);
        Serial.print(" bytes (");
        float compression_ratio = adpcm_utils::compression_ratio(header.total_samples * 2, g_total_bytes_recorded);
        Serial.print(compression_ratio, 1);
        Serial.println("% compression)");
    } else {
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
        
        Serial.print("WAV recording finalized: ");
        Serial.print(g_total_bytes_recorded);
        Serial.println(" bytes");
    }
    
    // Flush and close file
    g_audio_file.flush();
    g_audio_file.close();
    
    return true;
}

// ========================================
// Public API Implementation
// ========================================

bool audio_init() {
    Serial.println("Initializing audio system...");
    
    // Initialize SD card with multiple attempts and different configurations
    Serial.print("Initializing SD card on CS pin ");
    Serial.print(SD_CS_PIN);
    Serial.println("...");
    
    // Try different SD card configurations for better compatibility
    bool sd_initialized = false;
    
    // First attempt: Low speed, shared SPI
    Serial.println("Attempt 1: Low speed (4 MHz)...");
    if (g_sd_card.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(4)))) {
        sd_initialized = true;
        Serial.println("✓ SD card initialized at 4 MHz");
    }
    
    // Second attempt: Even lower speed
    if (!sd_initialized) {
        Serial.println("Attempt 2: Ultra low speed (1 MHz)...");
        if (g_sd_card.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(1)))) {
            sd_initialized = true;
            Serial.println("✓ SD card initialized at 1 MHz");
        }
    }
    
    // Third attempt: Default SPI settings
    if (!sd_initialized) {
        Serial.println("Attempt 3: Default settings...");
        if (g_sd_card.begin(SD_CS_PIN)) {
            sd_initialized = true;
            Serial.println("✓ SD card initialized with default settings");
        }
    }
    
    if (!sd_initialized) {
        Serial.print("ERROR: All SD card initialization attempts failed! Last error code: ");
        Serial.println(g_sd_card.card()->errorCode());
        Serial.println("Please check:");
        Serial.println("- SD card is properly inserted");
        Serial.println("- SD card is FAT16/FAT32 formatted");
        Serial.println("- SD card is 32GB or smaller");
        Serial.println("- Connections are secure");
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
    
    // Reset ADPCM encoder if compression is enabled
    if (ENABLE_ADPCM_COMPRESSION) {
        g_adpcm_encoder.reset();
    }
    
    // Create new audio file
    if (!create_audio_file()) {
        return false;
    }
    
    // Start recording
    g_is_recording = true;
    
    Serial.print("Recording started: ");
    Serial.println(g_current_filename);
    return true;
}

void audio_process() {
    // Process recording data
    if (g_is_recording) {
        if (!process_ring_buffer_data()) {
            Serial.println("ERROR: Audio processing failed, stopping recording");
            audio_stop_recording();
            return;
        }
    }
    
    // Process streaming data (only if not recording to avoid conflicts)
    if (ENABLE_LIVE_STREAMING && g_is_streaming && !g_is_recording) {
        process_streaming_data();
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
    
    // Finalize audio file
    if (!finalize_audio_file()) {
        Serial.println("ERROR: Failed to finalize audio file");
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
    if (ENABLE_ADPCM_COMPRESSION) {
        Serial.print(" compressed");
    }
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

// SD card access for cleanup functionality
SdFat& audio_get_sd_card() {
    return g_sd_card;
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

// ========================================
// Live Audio Streaming Implementation
// ========================================

bool audio_start_streaming() {
    if (g_is_streaming) {
        Serial.println("ERROR: Already streaming!");
        return false;
    }
    
    if (!ENABLE_LIVE_STREAMING) {
        Serial.println("ERROR: Live streaming is disabled in config");
        return false;
    }
    
    Serial.println("Starting audio streaming...");
    
    // Reset streaming state
    g_stream_buffer_fill = 0;
    
    // Reset streaming ADPCM encoder
    g_stream_adpcm_encoder.reset();
    
    // Start streaming
    g_is_streaming = true;
    
    Serial.println("Audio streaming started");
    return true;
}

void audio_stop_streaming() {
    if (!g_is_streaming) {
        Serial.println("WARNING: Not currently streaming");
        return;
    }
    
    Serial.println("Stopping audio streaming...");
    g_is_streaming = false;
    
    // Send any remaining data in stream buffer
    if (g_stream_buffer_fill > 0 && ble_streaming_is_connected()) {
        ble_streaming_send_chunk(g_stream_buffer, g_stream_buffer_fill);
        g_stream_buffer_fill = 0;
    }
    
    Serial.println("Audio streaming stopped");
}

bool audio_is_streaming() {
    return g_is_streaming;
}

// ========================================
// Streaming Data Processing
// ========================================

static void process_streaming_data() {
    if (!ble_streaming_is_connected()) {
        return;  // No client connected
    }
    
    uint32_t available_bytes = ring_buffer_available();
    
    // Process smaller chunks more frequently to reduce latency and clicking
    while (available_bytes >= 256) {  // Process in 256-byte chunks (128 samples)
        const uint32_t samples_to_process = 128;
        const uint32_t bytes_to_read = samples_to_process * sizeof(int16_t);
        
        // Copy PCM data from ring buffer to temp buffer, handling wraparound
        uint32_t read_pos = g_ring_read_pos & RING_BUFFER_MASK;
        uint32_t bytes_until_end = RING_BUFFER_SIZE - read_pos;
        
        if (bytes_to_read <= bytes_until_end) {
            // No wraparound needed
            memcpy(g_pcm_temp_buffer, &g_ring_buffer[read_pos], bytes_to_read);
        } else {
            // Handle wraparound
            memcpy(g_pcm_temp_buffer, &g_ring_buffer[read_pos], bytes_until_end);
            memcpy((uint8_t*)g_pcm_temp_buffer + bytes_until_end, &g_ring_buffer[0], bytes_to_read - bytes_until_end);
        }
        
        // Update ring buffer read position atomically
        g_ring_read_pos += bytes_to_read;
        available_bytes -= bytes_to_read;
        
        // Compress to ADPCM
        uint8_t adpcm_chunk[64];  // 128 samples -> 64 bytes (4:1 compression)
        uint32_t compressed_bytes = g_stream_adpcm_encoder.encode_samples(g_pcm_temp_buffer, samples_to_process, adpcm_chunk);
        
        // Send chunk immediately to reduce latency
        if (compressed_bytes > 0) {
            ble_streaming_send_chunk(adpcm_chunk, compressed_bytes);
        }
    }
}