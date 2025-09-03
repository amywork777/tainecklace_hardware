#ifndef ADAFRUIT_FIFO_H_
#define ADAFRUIT_FIFO_H_

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Simple FIFO buffer for Bluefruit compatibility
typedef struct {
    uint8_t* buffer;
    uint16_t depth;
    uint16_t count;
    uint16_t wr_idx;
    uint16_t rd_idx;
    bool overwritable;
} adafruit_fifo_t;

// FIFO function stubs
inline bool adafruit_fifo_init(adafruit_fifo_t* ff, uint8_t* buffer, uint16_t depth, bool overwritable) {
    if (!ff || !buffer) return false;
    
    ff->buffer = buffer;
    ff->depth = depth;
    ff->count = 0;
    ff->wr_idx = 0;
    ff->rd_idx = 0;
    ff->overwritable = overwritable;
    
    return true;
}

inline void adafruit_fifo_clear(adafruit_fifo_t* ff) {
    if (ff) {
        ff->count = ff->wr_idx = ff->rd_idx = 0;
    }
}

inline bool adafruit_fifo_empty(adafruit_fifo_t* ff) {
    return (ff && ff->count == 0);
}

inline bool adafruit_fifo_full(adafruit_fifo_t* ff) {
    return (ff && ff->count == ff->depth);
}

inline uint16_t adafruit_fifo_count(adafruit_fifo_t* ff) {
    return ff ? ff->count : 0;
}

inline bool adafruit_fifo_write(adafruit_fifo_t* ff, const void* data, uint16_t len) {
    if (!ff || !data || len == 0) return false;
    
    const uint8_t* src = (const uint8_t*)data;
    for (uint16_t i = 0; i < len; i++) {
        if (ff->count >= ff->depth) {
            if (!ff->overwritable) return false;
            // Overwrite oldest data
            ff->rd_idx = (ff->rd_idx + 1) % ff->depth;
        } else {
            ff->count++;
        }
        
        ff->buffer[ff->wr_idx] = src[i];
        ff->wr_idx = (ff->wr_idx + 1) % ff->depth;
    }
    
    return true;
}

inline uint16_t adafruit_fifo_read(adafruit_fifo_t* ff, void* buffer, uint16_t len) {
    if (!ff || !buffer || len == 0) return 0;
    
    uint8_t* dst = (uint8_t*)buffer;
    uint16_t read_count = 0;
    
    while (read_count < len && ff->count > 0) {
        dst[read_count] = ff->buffer[ff->rd_idx];
        ff->rd_idx = (ff->rd_idx + 1) % ff->depth;
        ff->count--;
        read_count++;
    }
    
    return read_count;
}

#ifdef __cplusplus
}
#endif

#endif /* ADAFRUIT_FIFO_H_ */
