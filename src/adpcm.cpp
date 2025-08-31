#include "adpcm.h"
#include <algorithm>

// IMA ADPCM step size table (89 entries)
const int16_t adpcm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

// Index adjustment table for ADPCM
const int8_t adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

// ========================================
// ADPCM Encoder Implementation
// ========================================

void ADPCMEncoder::reset() {
    predicted_sample = 0;
    step_index = 0;
}

uint8_t ADPCMEncoder::encode_sample(int16_t sample) {
    // Calculate difference from prediction
    int32_t diff = sample - predicted_sample;
    
    // Determine sign
    uint8_t adpcm_code = 0;
    if (diff < 0) {
        adpcm_code = 8;  // Set sign bit
        diff = -diff;
    }
    
    // Get current step size
    int32_t step = adpcm_step_table[step_index];
    
    // Quantize the difference
    int32_t delta = step >> 3;  // Initialize with step/8
    
    if (diff >= step) {
        adpcm_code |= 4;
        diff -= step;
        delta += step;
    }
    
    step >>= 1;
    if (diff >= step) {
        adpcm_code |= 2;
        diff -= step;
        delta += step;
    }
    
    step >>= 1;
    if (diff >= step) {
        adpcm_code |= 1;
        delta += step;
    }
    
    // Update predicted sample
    if (adpcm_code & 8) {
        predicted_sample -= delta;
    } else {
        predicted_sample += delta;
    }
    
    // Clamp to 16-bit range
    predicted_sample = std::max(-32768, std::min(32767, (int)predicted_sample));
    
    // Update step index
    step_index += adpcm_index_table[adpcm_code & 7];
    step_index = std::max(0, std::min(88, (int)step_index));
    
    return adpcm_code & 0x0F;
}

uint32_t ADPCMEncoder::encode_samples(const int16_t* pcm_samples, uint32_t sample_count, uint8_t* output_buffer) {
    uint32_t bytes_written = 0;
    
    for (uint32_t i = 0; i < sample_count; i += 2) {
        uint8_t byte_value = 0;
        
        // Encode first sample (lower 4 bits)
        byte_value = encode_sample(pcm_samples[i]);
        
        // Encode second sample if available (upper 4 bits)
        if (i + 1 < sample_count) {
            byte_value |= (encode_sample(pcm_samples[i + 1]) << 4);
        }
        
        output_buffer[bytes_written++] = byte_value;
    }
    
    return bytes_written;
}

// ========================================
// ADPCM Decoder Implementation
// ========================================

void ADPCMDecoder::reset() {
    predicted_sample = 0;
    step_index = 0;
}

int16_t ADPCMDecoder::decode_sample(uint8_t adpcm_code) {
    // Get current step size
    int32_t step = adpcm_step_table[step_index];
    
    // Calculate delta
    int32_t delta = step >> 3;
    
    if (adpcm_code & 4) delta += step;
    if (adpcm_code & 2) delta += step >> 1;
    if (adpcm_code & 1) delta += step >> 2;
    
    // Apply sign
    if (adpcm_code & 8) {
        predicted_sample -= delta;
    } else {
        predicted_sample += delta;
    }
    
    // Clamp to 16-bit range
    predicted_sample = std::max(-32768, std::min(32767, (int)predicted_sample));
    
    // Update step index
    step_index += adpcm_index_table[adpcm_code & 7];
    step_index = std::max(0, std::min(88, (int)step_index));
    
    return predicted_sample;
}

uint32_t ADPCMDecoder::decode_samples(const uint8_t* adpcm_data, uint32_t adpcm_byte_count, int16_t* output_buffer) {
    uint32_t samples_written = 0;
    
    for (uint32_t i = 0; i < adpcm_byte_count; i++) {
        uint8_t byte_value = adpcm_data[i];
        
        // Decode first sample (lower 4 bits)
        output_buffer[samples_written++] = decode_sample(byte_value & 0x0F);
        
        // Decode second sample (upper 4 bits)
        output_buffer[samples_written++] = decode_sample((byte_value >> 4) & 0x0F);
    }
    
    return samples_written;
}
