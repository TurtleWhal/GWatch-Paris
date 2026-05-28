#ifndef SIM_FREERTOS_IDF_ADDITIONS_H
#define SIM_FREERTOS_IDF_ADDITIONS_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* xTaskCreateWithCaps signature matches IDF's: same as xTaskCreate plus
 * a memory-capabilities flag we ignore on the host. */
BaseType_t xTaskCreateWithCaps(TaskFunction_t fn, const char *name,
                               uint32_t stack, void *arg, UBaseType_t prio,
                               TaskHandle_t *out_handle, uint32_t caps);

void vTaskDeleteWithCaps(TaskHandle_t h);

#ifdef __cplusplus
}
#endif

#endif
