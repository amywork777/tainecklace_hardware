#ifndef ADAFRUIT_NRFCRYPTO_H
#define ADAFRUIT_NRFCRYPTO_H

// Stub header for nRFCrypto compatibility
// This provides minimal definitions to allow compilation

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Crypto function stubs (minimal implementation)
typedef struct {
    uint8_t data[32];
} nrf_crypto_key_t;

// Function stubs - these won't work but allow compilation
inline int nrf_crypto_init(void) { return 0; }
inline int nrf_crypto_uninit(void) { return 0; }

#ifdef __cplusplus
}
#endif

#endif // ADAFRUIT_NRFCRYPTO_H
