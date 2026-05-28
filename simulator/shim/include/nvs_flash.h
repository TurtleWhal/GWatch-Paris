#ifndef SIM_NVS_FLASH_H
#define SIM_NVS_FLASH_H
#include "esp_err.h"
#include "nvs.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline esp_err_t nvs_flash_init(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_erase(void) { return ESP_OK; }
#ifdef __cplusplus
}
#endif
#endif
