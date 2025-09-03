#ifndef ADACALLBACK_H_
#define ADACALLBACK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Simple callback system for Bluefruit compatibility
typedef void (*ada_callback_t)(void);
typedef void (*ada_callback_with_arg_t)(void* arg);

// Callback function stubs
inline void ada_callback_init(void) {
    // Stub implementation
}

inline void ada_callback_register(ada_callback_t callback) {
    // Stub implementation - store callback if needed
    (void)callback;
}

inline void ada_callback_invoke(void) {
    // Stub implementation - invoke stored callbacks
}

#ifdef __cplusplus
}
#endif

#endif /* ADACALLBACK_H_ */
