#pragma once

#include <Arduino.h>
#include <cstdint>

/**
 * Lightweight ADPCM (Adaptive Differential Pulse Code Modulation) Codec
 * 
 * Features:
 * - 4:1 compression ratio (16-bit PCM → 4-bit ADPCM)
 * - Optimized for real-time encoding on nRF52840
 * - Low memory footprint (~32 bytes state)
 * - Voice-optimized parameters
 * 
 * Usage:
 *   ADPCMEncoder encoder;
 *   encoder.reset();
 *   uint32_t compressed = encoder.encode_samples(pcm_samples, count, output_buffer);
 * 
 *   ADPCMDecoder decoder;
 *   decoder.reset();
 *   uint32_t decompressed = decoder.decode_samples(adpcm_data, count, output_buffer);
 */

// ADPCM step size table (IMA ADPCM variant)
extern const int16_t adpcm_step_table[89];

// ADPCM index adjustment table
extern const int8_t adpcm_index_table[16];

/**
 * ADPCM Encoder State
 */
struct ADPCMEncoder {
    int16_t predicted_sample;  // Previous predicted sample
    int8_t step_index;         // Current step size index
    
    void reset();
    
    /**
     * Encode PCM samples to ADPCM
     * @param pcm_samples Input 16-bit PCM samples
     * @param sample_count Number of samples to encode
     * @param output_buffer Output buffer for ADPCM data (sample_count/2 bytes needed)
     * @return Number of bytes written to output_buffer
     */
    uint32_t encode_samples(const int16_t* pcm_samples, uint32_t sample_count, uint8_t* output_buffer);
    
    /**
     * Encode a single sample
     * @param sample 16-bit PCM sample
     * @return 4-bit ADPCM code (0-15)
     */
    uint8_t encode_sample(int16_t sample);
};

/**
 * ADPCM Decoder State
 */
struct ADPCMDecoder {
    int16_t predicted_sample;  // Previous predicted sample
    int8_t step_index;         // Current step size index
    
    void reset();
    
    /**
     * Decode ADPCM data to PCM samples
     * @param adpcm_data Input ADPCM data (4-bit codes packed in bytes)
     * @param adpcm_byte_count Number of ADPCM bytes to decode
     * @param output_buffer Output buffer for 16-bit PCM samples (adpcm_byte_count*2 samples)
     * @return Number of samples written to output_buffer
     */
    uint32_t decode_samples(const uint8_t* adpcm_data, uint32_t adpcm_byte_count, int16_t* output_buffer);
    
    /**
     * Decode a single 4-bit ADPCM code
     * @param adpcm_code 4-bit ADPCM code (0-15)
     * @return 16-bit PCM sample
     */
    int16_t decode_sample(uint8_t adpcm_code);
};

/**
 * ADPCM File Header for custom format
 */
struct __attribute__((packed)) ADPCMHeader {
    char magic[4] = {'A', 'D', 'P', 'C'};          // File magic number (4 bytes)
    uint32_t version = 1;                           // Format version (4 bytes)
    uint32_t sample_rate = 16000;                   // Sample rate (4 bytes)
    uint16_t channels = 1;                          // Number of channels (2 bytes)
    uint16_t bits_per_sample = 4;                   // ADPCM bits per sample (2 bytes)
    uint32_t total_samples = 0;                     // Total samples (4 bytes)
    uint32_t data_size = 0;                         // Data size (4 bytes)
    int16_t initial_sample = 0;                     // Initial sample (2 bytes)
    int8_t initial_step_index = 0;                  // Initial step index (1 byte)
    uint8_t reserved[5] = {0};                      // Padding (5 bytes) = 32 total
};

static_assert(sizeof(ADPCMHeader) == 32, "ADPCMHeader must be 32 bytes for efficient I/O");

/**
 * Utility functions for file I/O
 */
namespace adpcm_utils {
    /**
     * Calculate compressed size for given number of PCM samples
     */
    inline uint32_t compressed_size(uint32_t pcm_samples) {
        return (pcm_samples + 1) / 2;  // 2 samples per byte
    }
    
    /**
     * Calculate decompressed size for given ADPCM bytes
     */
    inline uint32_t decompressed_size(uint32_t adpcm_bytes) {
        return adpcm_bytes * 2;  // 2 samples per byte
    }
    
    /**
     * Calculate compression ratio percentage
     */
    inline float compression_ratio(uint32_t original_bytes, uint32_t compressed_bytes) {
        return (1.0f - (float)compressed_bytes / (float)original_bytes) * 100.0f;
    }
}
