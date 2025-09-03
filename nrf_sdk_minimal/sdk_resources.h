#ifndef SDK_RESOURCES_H__
#define SDK_RESOURCES_H__

// Missing SDK resource definitions for Seeed compatibility

#ifdef __cplusplus
extern "C" {
#endif

// SoftDevice PPI channels (stub for compatibility)
#define SD_PPI_CHANNELS_USED    0x00000000UL

// GZLL PPI channels (stub)
#define GZLL_PPI_CHANNELS_USED  0x00000000UL

// ESB PPI channels (stub)
#define ESB_PPI_CHANNELS_USED   0x00000000UL

// Combined PPI channels
#define NRF_PPI_CHANNELS_USED (SD_PPI_CHANNELS_USED | GZLL_PPI_CHANNELS_USED | ESB_PPI_CHANNELS_USED)

#ifdef __cplusplus
}
#endif

#endif // SDK_RESOURCES_H__
