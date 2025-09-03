#ifndef BLE_H__
#define BLE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// BLE Common definitions
#define BLE_UUID_TYPE_BLE               0x01
#define BLE_UUID_TYPE_VENDOR_BEGIN      0x02

// BLE GAP definitions
#define BLE_GAP_ADDR_LEN                6
#define BLE_GAP_ADV_MAX_SIZE           31

// BLE GATT definitions  
#define BLE_GATT_HANDLE_INVALID        0x0000
#define BLE_GATT_OP_INVALID            0x00

// BLE event structure (minimal)
typedef struct {
    uint16_t evt_id;
    uint16_t evt_len;
} ble_evt_hdr_t;

typedef struct {
    ble_evt_hdr_t header;
    union {
        uint32_t gap_evt;
        uint32_t gattc_evt;  
        uint32_t gatts_evt;
    } evt;
} ble_evt_t;

// UUID structure
typedef struct {
    uint16_t uuid;
    uint8_t  type;
} ble_uuid_t;

// Connection handle type
typedef uint16_t ble_conn_handle_t;

#ifdef __cplusplus
}
#endif

#endif // BLE_H__
