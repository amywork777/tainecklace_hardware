#ifndef NRF_SD_DEF_H__
#define NRF_SD_DEF_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SoftDevice resource definitions for Nordic SDK compatibility
#define SD_RESOURCE_BASE        0x10000000UL

// Memory resource definitions
#define SD_FLASH_BASE           0x00000000UL
#define SD_FLASH_SIZE           0x00026000UL
#define SD_RAM_BASE             0x20000000UL  
#define SD_RAM_SIZE             0x00008000UL

// SoftDevice version info
#define SD_FWID_PRESENT         1
#define SD_FWID_GET(x)          ((uint16_t)((x) >> 16))
#define SD_FWID_SET(x, id)      ((x) = ((x) & 0x0000FFFFUL) | (((uint32_t)(id)) << 16))

// Resource allocation macros
#define RESOURCE_NONE           0x00000000UL
#define RESOURCE_SOFTDEVICE     0x00000001UL

// SoftDevice state
extern uint8_t sd_state;

#ifdef __cplusplus
}
#endif

#endif // NRF_SD_DEF_H__
