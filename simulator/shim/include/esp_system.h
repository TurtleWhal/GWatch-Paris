#ifndef SIM_ESP_SYSTEM_H
#define SIM_ESP_SYSTEM_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On-device `esp_restart()` reboots the SoC. On the host we exit the
 * process — close the SDL window and let the user re-launch. */
void esp_restart(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
