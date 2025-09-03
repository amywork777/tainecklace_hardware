#ifndef NRF_SOC_H__
#define NRF_SOC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SoC event types
#define NRF_EVT_HFCLKSTARTED            0x00
#define NRF_EVT_POWER_FAILURE_WARNING   0x01
#define NRF_EVT_FLASH_OPERATION_SUCCESS 0x02
#define NRF_EVT_FLASH_OPERATION_ERROR   0x03
#define NRF_EVT_RADIO_BLOCKED           0x04
#define NRF_EVT_RADIO_CANCELED          0x05
#define NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN 0x06
#define NRF_EVT_RADIO_SESSION_IDLE      0x07
#define NRF_EVT_RADIO_SESSION_CLOSED    0x08

// Power management
#define NRF_POWER_MODE_CONSTLAT         0x00
#define NRF_POWER_MODE_LOWPWR           0x01

// Function stubs
uint32_t sd_power_mode_set(uint8_t power_mode);
uint32_t sd_power_system_off(void);
uint32_t sd_power_reset_reason_get(uint32_t* p_reset_reason);
uint32_t sd_power_reset_reason_clr(uint32_t reset_reason_clr_msk);

#ifdef __cplusplus
}
#endif

#endif // NRF_SOC_H__
