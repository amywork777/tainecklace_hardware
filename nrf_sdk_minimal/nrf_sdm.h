#ifndef NRF_SDM_H__
#define NRF_SDM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal Nordic SDK compatibility for Bluefruit library
#define NRF_SUCCESS                     0x0
#define NRF_ERROR_SOFTDEVICE_NOT_ENABLED 0x3001
#define NRF_ERROR_INVALID_STATE         0x08

// SoftDevice version
#define SD_BLE_API_VERSION              7

// BLE event types
#define BLE_EVT_BASE                    0x01
#define BLE_GAP_EVT_BASE               (BLE_EVT_BASE + 0x00)
#define BLE_GATTC_EVT_BASE             (BLE_EVT_BASE + 0x30)
#define BLE_GATTS_EVT_BASE             (BLE_EVT_BASE + 0x50)

// Connection parameters
#define BLE_GAP_CP_MIN_CONN_INTVL_MIN   0x0006
#define BLE_GAP_CP_MIN_CONN_INTVL_MAX   0x0C80
#define BLE_GAP_CP_MAX_CONN_INTVL_MIN   0x0006
#define BLE_GAP_CP_MAX_CONN_INTVL_MAX   0x0C80
#define BLE_GAP_CP_SLAVE_LATENCY_MAX    0x01F3
#define BLE_GAP_CP_CONN_SUP_TIMEOUT_MIN 0x000A
#define BLE_GAP_CP_CONN_SUP_TIMEOUT_MAX 0x0C80

// GATT MTU
#define BLE_GATT_ATT_MTU_DEFAULT        23
#define BLE_GATT_ATT_MTU_MAX           247

// Function declarations (stubs for compatibility)
uint32_t sd_softdevice_enable(void* p_clock_lf_cfg, void* fault_handler);
uint32_t sd_softdevice_disable(void);
uint32_t sd_softdevice_is_enabled(uint8_t* p_softdevice_enabled);

#ifdef __cplusplus
}
#endif

#endif // NRF_SDM_H__
